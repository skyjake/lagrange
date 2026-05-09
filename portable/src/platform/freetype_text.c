/* Copyright 2026 Jaakko Keränen <jaakko.keranen@iki.fi>

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

/* Text rendering backend using FreeType for Windows and Linux.
   Uses HarfBuzz (via hb-ft.h) for shaping and the shared text_backend
   infrastructure for GlyphBuffer/FontRun management. Color emoji is supported
   via FreeType's CBDT/CBLC and COLR v0 facilities. */

#include "freetype_text.h"
#include "render/text.h"
#include "render/paint.h"
#include "render/attributedtext.h"
#include "fontpack.h"
#include "color.h"
#include "app.h"
#include "ui/metrics.h"
#include "ui/window.h"

#include <lagrange/prefs.h>
#include <lagrange/defs.h>
#include <lagrange/resources.h>

#include <the_Foundation/array.h>
#include <the_Foundation/block.h>
#include <the_Foundation/file.h>
#include <the_Foundation/math.h>
#include <the_Foundation/path.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/string.h>

#include <SDL_render.h>
#include <SDL_surface.h>
#include <SDL_hints.h>
#include <SDL_version.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*----------------------------------------------------------------------------------------------*/

static FT_Library ftLibrary_     = NULL;
static int        ftLibraryRefs_ = 0;

static void initFtLibrary_(void) {
    if (!ftLibrary_) {
        const FT_Error err = FT_Init_FreeType(&ftLibrary_);
        iAssert(err == 0);
        /* LCD filter makes subpixel rendering smoother without fringing. */
        FT_Library_SetLcdFilter(ftLibrary_, FT_LCD_FILTER_DEFAULT);
    }
    ftLibraryRefs_++;
}

FT_Library ftLibrary_FtText(void) {
    initFtLibrary_();
    return ftLibrary_;
}

static void doneFtLibrary_(void) {
    if (ftLibrary_ && --ftLibraryRefs_ == 0) {
        FT_Done_FreeType(ftLibrary_);
        ftLibrary_ = NULL;
    }
}

void doneFtLibrary_FtText(void) {
    doneFtLibrary_();
}

/*----------------------------------------------------------------------------------------------*/
/* FontFile backend API (declared in fontpack.h, called from fontpack.c). */

void allocData_FontFile(iFontFile *d) {
    initFtLibrary_();
    iFtFontData *fd = calloc(1, sizeof(iFtFontData));
    FT_Error err;
    if (size_Block(&d->sourceData) > 0) {
        /* Font loaded from a fontpack ZIP. */
        err = FT_New_Memory_Face(ftLibrary_,
                                 (const FT_Byte *) constData_Block(&d->sourceData),
                                 (FT_Long) size_Block(&d->sourceData),
                                 d->colIndex,
                                 &fd->ftFace);
    }
    else {
        /* System font: file path stored in d->id. */
        err = FT_New_Face(ftLibrary_, cstr_String(&d->id), d->colIndex, &fd->ftFace);
    }
    if (err || !fd->ftFace) {
        free(fd);
        return;
    }
    FT_Face face = fd->ftFace;
    /* Extract metrics from OS/2 and HHea tables. */
    TT_OS2 *os2 = FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
    if (os2) {
        d->winAscent  = (int) os2->usWinAscent;
        d->winDescent = (int) os2->usWinDescent;
    }
    TT_HoriHeader *hhea = FT_Get_Sfnt_Table(face, FT_SFNT_HHEA);
    if (hhea) {
        d->ascent  = (int) hhea->Ascender;
        d->descent = (int) hhea->Descender; /* negative */
        d->lineGap = (int) hhea->Line_Gap;
    }
    else if (face->ascender || face->descender) {
        d->ascent  = (int) face->ascender;
        d->descent = (int) face->descender;
        d->lineGap = 0;
    }
    d->unitsPerEm = (int) face->units_per_EM;
    /* Use 'M' advance as em-advance metric. */
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt) face->units_per_EM);
    if (FT_Load_Char(face, 'M', FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) == 0) {
        d->emAdvance = (int) (face->glyph->advance.x >> 6);
    }
    else {
        d->emAdvance = (int) face->units_per_EM;
    }
    fd->hasColorGlyphs = FT_HAS_COLOR(face);
#if defined (LAGRANGE_ENABLE_HARFBUZZ)
    /* Raw blob (not hb_ft_font_create) gives design-unit advances, consistent with xScale. */
    hb_blob_t *hbBlob;
    if (size_Block(&d->sourceData) > 0) {
        hbBlob = hb_blob_create(constData_Block(&d->sourceData),
                                (unsigned int) size_Block(&d->sourceData),
                                HB_MEMORY_MODE_READONLY, NULL, NULL);
    }
    else {
        hbBlob = hb_blob_create_from_file(cstr_String(&d->id));
    }
    hb_face_t *hbFace = hb_face_create(hbBlob, (unsigned int) d->colIndex);
    fd->hbFont = hb_font_create(hbFace);
    hb_face_destroy(hbFace);
    hb_blob_destroy(hbBlob);
#endif
    d->data = fd;
}

void deallocData_FontFile(iFontFile *d) {
    iFtFontData *fd = ftData_FontFile(d);
    if (!fd) return;
#if defined (LAGRANGE_ENABLE_HARFBUZZ)
    if (fd->hbFont) {
        hb_font_destroy(fd->hbFont);
        fd->hbFont = NULL;
    }
#endif
    if (fd->ftFace) {
        FT_Done_Face(fd->ftFace);
        fd->ftFace = NULL;
    }
    free(fd);
    d->data = NULL;
    doneFtLibrary_();
}

#if defined (LAGRANGE_ENABLE_HARFBUZZ)
hb_font_t *hbFont_FontFile(const iFontFile *d) {
    return ftData_FontFile(d)->hbFont;
}
#endif

uint32_t findGlyphIndex_FontFile(const iFontFile *d, iChar ch) {
    const iFtFontData *fd = ftData_FontFile(d);
    if (!fd || !fd->ftFace) return 0;
    return FT_Get_Char_Index(fd->ftFace, ch);
}

iBool isMonospace_FontFile(const iFontFile *d) {
    const iFtFontData *fd = ftData_FontFile(d);
    if (!fd || !fd->ftFace) return iFalse;
    return FT_IS_FIXED_WIDTH(fd->ftFace) != 0;
}

float scaleForPixelHeight_FontFile(const iFontFile *d, int pixelHeight) {
    if (!d->ascent || !d->descent) return 1.0f;
    return (float) pixelHeight / (float) (d->ascent - d->descent);
}

int glyphAdvance_FontFile(const iFontFile *d, uint32_t glyphIndex) {
    const iFtFontData *fd = ftData_FontFile(d);
    if (!fd || !fd->ftFace) return 0;
    if (FT_Load_Glyph(fd->ftFace, glyphIndex, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) != 0) {
        return 0;
    }
    return (int) (fd->ftFace->glyph->advance.x >> 6);
}

void measureGlyph_FontFile(const iFontFile *d, uint32_t glyphIndex,
                           float xScale, float yScale, float xShift,
                           int *x0, int *y0, int *x1, int *y1) {
    const iFtFontData *fd = ftData_FontFile(d);
    *x0 = *y0 = *x1 = *y1 = 0;
    if (!fd || !fd->ftFace) return;
    FT_Face face = fd->ftFace;
    const FT_UInt ppem = iMax(1, (FT_UInt) roundf(xScale * (float) d->unitsPerEm));
    FT_Set_Pixel_Sizes(face, ppem, ppem);
    /* Hinted load: metrics are grid-snapped, matching the rasterizer and avoiding 1px y-jitter. */
    if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_NO_BITMAP) != 0) return;
    iUnused(yScale, xShift);
    const FT_Glyph_Metrics *m = &face->glyph->metrics;
    if (m->width == 0 || m->height == 0) return;
    *x0 =  (int)(m->horiBearingX >> 6);
    *y0 = -(int)(m->horiBearingY >> 6);
    *x1 =  (int)((m->horiBearingX + m->width  + 63) >> 6);
    *y1 =  (int)((m->height - m->horiBearingY + 63) >> 6);
}

/* Returns a heap-allocated 8-bit grayscale bitmap. The caller must free() it. */
uint8_t *rasterizeGlyph_FontFile(const iFontFile *d, float xScale, float yScale, float xShift,
                                 uint32_t glyphIndex, int *w, int *h) {
    const iFtFontData *fd = ftData_FontFile(d);
    *w = *h = 0;
    if (!fd || !fd->ftFace) return NULL;
    FT_Face face = fd->ftFace;
    const FT_UInt ppem = iMax(1, (FT_UInt) roundf(xScale * (float) d->unitsPerEm));
    FT_Set_Pixel_Sizes(face, ppem, ppem);
    FT_Int32 loadFlags = FT_LOAD_RENDER;
    if (fd->hasColorGlyphs) {
        loadFlags |= FT_LOAD_COLOR;
    }
    if (FT_Load_Glyph(face, glyphIndex, loadFlags) != 0) {
        return NULL;
    }
    FT_GlyphSlot slot = face->glyph;
    if (slot->bitmap.width == 0 || slot->bitmap.rows == 0) {
        return NULL;
    }
    *w = (int) slot->bitmap.width;
    *h = (int) slot->bitmap.rows;
    iUnused(yScale, xShift);
    if (slot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
        /* Color emoji: copy as-is (BGRA). The caller (cacheGlyphs_Font_) handles this. */
        const size_t sz = (size_t) (*w) * (size_t) (*h) * 4;
        uint8_t *buf = malloc(sz);
        memcpy(buf, slot->bitmap.buffer, sz);
        return buf;
    }
    /* Grayscale: convert from FT's 8-bit to an 8-bit output. */
    if (slot->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
        const size_t sz = (size_t) (*w) * (size_t) (*h);
        uint8_t *buf = malloc(sz);
        for (int row = 0; row < *h; row++) {
            memcpy(buf + row * *w,
                   slot->bitmap.buffer + row * slot->bitmap.pitch,
                   (size_t) *w);
        }
        return buf;
    }
    /* Mono bitmap: convert to 8-bit grayscale. */
    if (slot->bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
        const size_t sz = (size_t) (*w) * (size_t) (*h);
        uint8_t *buf = calloc(sz, 1);
        for (int row = 0; row < *h; row++) {
            const uint8_t *src = slot->bitmap.buffer + row * slot->bitmap.pitch;
            uint8_t       *dst = buf + row * *w;
            for (int col = 0; col < *w; col++) {
                dst[col] = (src[col / 8] & (0x80 >> (col % 8))) ? 255 : 0;
            }
        }
        return buf;
    }
    return NULL;
}

/* COLR v0: composite all layers into a BGRA canvas.
   Used when FT_LOAD_COLOR doesn't auto-produce BGRA (FreeType < 2.12). */
static uint8_t *renderColrV0_(FT_Face face, FT_UInt glyphIndex, FT_UInt ppem,
                               int *w_out, int *h_out) {
    /* Need at least one COLR layer to proceed. */
    FT_LayerIterator iter;
    memset(&iter, 0, sizeof(iter));
    FT_UInt layerGlyphIdx, layerColorIdx;
    if (!FT_Get_Color_Glyph_Layer(face, glyphIndex, &layerGlyphIdx, &layerColorIdx, &iter)) {
        return NULL;
    }
    /* Determine canvas size from the composite glyph's metrics. */
    FT_Set_Pixel_Sizes(face, ppem, ppem);
    if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0) {
        return NULL;
    }
    /* Derive canvas bounds from the composite glyph's outline bbox.
       Many COLR base glyphs have an empty outline; fall back to the scaled
       ascender/descender range in that case. */
    FT_BBox bbox;
    FT_Outline_Get_CBox(&face->glyph->outline, &bbox);
    int x0 = (int)(bbox.xMin >> 6);
    int y0 = (int)(bbox.yMin >> 6);
    int x1 = (int)((bbox.xMax + 63) >> 6);
    int y1 = (int)((bbox.yMax + 63) >> 6);
    if (x0 >= x1 || y0 >= y1) {
        /* Empty outline: use scaled metrics from the face. */
        const float scale = (float) ppem / (float) face->units_per_EM;
        x0 = 0;
        y0 = (int)(face->descender * scale);
        x1 = (int)((face->glyph->metrics.horiAdvance >> 6) + 1);
        y1 = (int)(face->ascender * scale);
    }
    const int cw = x1 - x0;
    const int ch = y1 - y0;
    if (cw <= 0 || ch <= 0) return NULL;

    uint8_t *canvas = calloc((size_t) cw * ch * 4, 1); /* BGRA, pre-zeroed transparent */

    /* Load the default colour palette (index 0). */
    FT_Color    *palette   = NULL;
    FT_Palette_Data pdata;
    memset(&pdata, 0, sizeof(pdata));
    FT_Palette_Data_Get(face, &pdata);
    FT_Palette_Select(face, 0, &palette);

    /* Iterate layers from the beginning (reset the iterator). */
    memset(&iter, 0, sizeof(iter));
    while (FT_Get_Color_Glyph_Layer(face, glyphIndex, &layerGlyphIdx, &layerColorIdx, &iter)) {
        if (FT_Load_Glyph(face, layerGlyphIdx,
                          FT_LOAD_RENDER | FT_LOAD_NO_HINTING) != 0) {
            continue;
        }
        FT_GlyphSlot slot = face->glyph;
        if (slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) continue;

        /* Resolve the layer colour from the palette. */
        uint8_t lr = 0xff, lg = 0xff, lb = 0xff, la = 0xff;
        if (palette && layerColorIdx < pdata.num_palette_entries) {
            FT_Color c = palette[layerColorIdx];
            lb = c.blue; lg = c.green; lr = c.red; la = c.alpha;
        }
        else if (layerColorIdx == 0xffff) {
            /* Foreground color: use opaque white as a neutral stand-in. */
            lr = lg = lb = la = 0xff;
        }

        /* Blit the layer's grayscale mask onto the canvas (source-over). */
        const int ox = slot->bitmap_left - x0;
        const int oy = (y1 - 1) - slot->bitmap_top; /* flip: FT origin is bottom-left */
        for (int row = 0; row < (int) slot->bitmap.rows; row++) {
            for (int col = 0; col < (int) slot->bitmap.width; col++) {
                const uint8_t mask = slot->bitmap.buffer[row * slot->bitmap.pitch + col];
                if (!mask) continue;
                const int cx = ox + col;
                const int cy = oy + row;
                if (cx < 0 || cy < 0 || cx >= cw || cy >= ch) continue;
                uint8_t *px = canvas + (cy * cw + cx) * 4;
                /* Pre-multiplied alpha composite: src_over. */
                const float sa = (mask / 255.0f) * (la / 255.0f);
                const float da = 1.0f - sa;
                px[0] = (uint8_t)(lb * sa + px[0] * da); /* B */
                px[1] = (uint8_t)(lg * sa + px[1] * da); /* G */
                px[2] = (uint8_t)(lr * sa + px[2] * da); /* R */
                px[3] = (uint8_t)(iMin(255, (int)(sa * 255.0f + px[3] * da)));
            }
        }
    }
    *w_out = cw;
    *h_out = ch;
    return canvas;
}

/* Variant that also reports whether the returned bitmap is BGRA color. */
static uint8_t *rasterizeGlyphEx_FontFile_(const iFontFile *d, float xScale, float yScale,
                                           float xShift, uint32_t glyphIndex,
                                           int *w, int *h, iBool *isColor_out) {
    const iFtFontData *fd = ftData_FontFile(d);
    *w = *h = 0;
    *isColor_out = iFalse;
    if (!fd || !fd->ftFace) return NULL;
    FT_Face face = fd->ftFace;
    const FT_UInt ppem = iMax(1, (FT_UInt) roundf(xScale * (float) d->unitsPerEm));
    FT_Set_Pixel_Sizes(face, ppem, ppem);
    iUnused(yScale, xShift);
    /* First try FT_LOAD_COLOR which on FreeType >= 2.12 renders both CBDT and COLR as BGRA. */
    if (fd->hasColorGlyphs) {
        if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_COLOR | FT_LOAD_RENDER) == 0) {
            FT_GlyphSlot slot = face->glyph;
            if (slot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA &&
                slot->bitmap.width > 0 && slot->bitmap.rows > 0) {
                *w = (int) slot->bitmap.width;
                *h = (int) slot->bitmap.rows;
                *isColor_out = iTrue;
                const size_t sz = (size_t)(*w) * (size_t)(*h) * 4;
                uint8_t *buf = malloc(sz);
                memcpy(buf, slot->bitmap.buffer, sz);
                return buf;
            }
        }
        /* FT_LOAD_COLOR didn't produce BGRA. Try COLR v0 manual compositing.
           This covers FreeType < 2.12 where COLR is not auto-rendered. */
        uint8_t *colr = renderColrV0_(face, glyphIndex, ppem, w, h);
        if (colr) {
            *isColor_out = iTrue;
            return colr;
        }
    }
    /* Grayscale path: render outline normally. */
    if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER) != 0) return NULL;
    FT_GlyphSlot slot = face->glyph;
    if (slot->bitmap.width == 0 || slot->bitmap.rows == 0) return NULL;
    *w = (int) slot->bitmap.width;
    *h = (int) slot->bitmap.rows;
    if (slot->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
        const size_t sz = (size_t)(*w) * (size_t)(*h);
        uint8_t *buf = malloc(sz);
        for (int row = 0; row < *h; row++) {
            memcpy(buf + row * *w,
                   slot->bitmap.buffer + row * slot->bitmap.pitch,
                   (size_t) *w);
        }
        return buf;
    }
    if (slot->bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
        const size_t sz = (size_t)(*w) * (size_t)(*h);
        uint8_t *buf = calloc(sz, 1);
        for (int row = 0; row < *h; row++) {
            const uint8_t *src = slot->bitmap.buffer + row * slot->bitmap.pitch;
            uint8_t       *dst = buf + row * *w;
            for (int col = 0; col < *w; col++) {
                dst[col] = (src[col / 8] & (0x80 >> (col % 8))) ? 255 : 0;
            }
        }
        return buf;
    }
    return NULL;
}

/*----------------------------------------------------------------------------------------------*/

static void initCache_FtText_(iFtText *d) {
    initGrayscaleCache_RasterText_(d);
    /* Color RGBA atlas for color emoji (FreeType-only). */
    const int    textSize = d->base.contentFontSize * fontSize_UI;
    iGlyphCache *cc       = &d->colorCache;
    init_GlyphCache(cc);
    cc->size.x       = iMin(2048, d->grayscaleCache.size.x);
    cc->size.y       = iMin(2048, d->grayscaleCache.size.y);
    cc->rowAllocStep = iMax(4, textSize / 4);
    cc->bottom       = 0;
    cc->texture      = SDL_CreateTexture(d->base.render,
                                         SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC | SDL_TEXTUREACCESS_TARGET,
                                         cc->size.x, cc->size.y);
    SDL_SetTextureBlendMode(cc->texture, SDL_BLENDMODE_BLEND);
}

static void deinitCache_FtText_(iFtText *d) {
    deinitGrayscaleCache_RasterText_(d);
    deinit_GlyphCache(&d->colorCache);
}

static void resetCache_FtText_(iFtText *d) {
    deinitCache_FtText_(d);
    iForEach(Array, i, &d->fonts) {
        clearGlyphs_GlyphTable_(((iFont *) i.value)->table);
    }
    initCache_FtText_(d);
}

void resetGlyphCaches_(void) {
    resetCache_FtText_(current_FtText_());
}

/*----------------------------------------------------------------------------------------------*/

static enum iFontId fontId_Text_(const iFont *font) {
    return (enum iFontId) (font - (const iFont *) constData_Array(&current_FtText_()->fonts));
}

/* glyph_Font_ is defined in text_backend.c. */

void allocate_Font_(iRasterFont *d, iGlyph *glyph, int hoff) {
    iRect *glRect = &glyph->rect[hoff];
    int    x0, y0, x1, y1;
    measureGlyph_FontFile(d->font.file, index_Glyph_(glyph), d->xScale, d->yScale,
                          hoff * offsetStep_Glyph_(), &x0, &y0, &x1, &y1);
    glRect->size = init_I2(x1 - x0, y1 - y0);
    if (glRect->size.x > 0 && glRect->size.y > 0) {
        glRect->pos = assignPos_GlyphCache(&current_FtText_()->grayscaleCache, glRect->size);
    }
    else {
        setRasterized_Glyph_(glyph, hoff);
    }
    glyph->d[hoff] = init_I2(x0, y0);
    glyph->d[hoff].y += d->vertOffset;
    if (hoff == 0) {
        glyph->advance = d->xScale * glyphAdvance_FontFile(d->font.file, index_Glyph_(glyph));
    }
}

/*----------------------------------------------------------------------------------------------*/
/* Glyph rasterization: upload bitmaps to atlas textures */

static SDL_Palette *glyphPalette_(void) {
    return prefs_App()->fontSmoothing ? current_FtText_()->grayscale
                                      : current_FtText_()->blackAndWhite;
}

/* Upload a BGRA color glyph bitmap to the color atlas. Returns the atlas rect. */
static iRect uploadColorGlyph_(iFtText *tx, uint8_t *bgraBitmap, int w, int h) {
    iGlyphCache *cc = &tx->colorCache;
    /* Find a position in the color cache. The color cache uses assignPos_GlyphCache. */
    const iInt2 sz  = init_I2(w, h);
    const iInt2 pos = assignPos_GlyphCache(cc, sz);
    iRect atlasRect = { pos, sz };
    /* Swap BGRA → RGBA for SDL (SDL_PIXELFORMAT_RGBA32 is RGBA on all platforms). */
    uint8_t *rgba = malloc((size_t) w * h * 4);
    for (int i = 0; i < w * h; i++) {
        rgba[i * 4 + 0] = bgraBitmap[i * 4 + 2]; /* R ← B */
        rgba[i * 4 + 1] = bgraBitmap[i * 4 + 1]; /* G */
        rgba[i * 4 + 2] = bgraBitmap[i * 4 + 0]; /* B ← R */
        rgba[i * 4 + 3] = bgraBitmap[i * 4 + 3]; /* A */
    }
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormatFrom(rgba, w, h, 32, w * 4,
                                                           SDL_PIXELFORMAT_RGBA32);
    SDL_SetSurfaceBlendMode(surf, SDL_BLENDMODE_NONE);
    SDL_Renderer *render   = current_Text()->render;
    SDL_Texture  *bufTex   = SDL_CreateTextureFromSurface(render, surf);
    SDL_Texture  *oldTarget = SDL_GetRenderTarget(render);
    SDL_SetRenderTarget(render, cc->texture);
    SDL_SetTextureBlendMode(bufTex, SDL_BLENDMODE_NONE);
    SDL_RenderCopy(render, bufTex, NULL, (const SDL_Rect *) &atlasRect);
    SDL_SetRenderTarget(render, oldTarget);
    SDL_DestroyTexture(bufTex);
    SDL_FreeSurface(surf);
    free(rgba);
    return atlasRect;
}

iBool isColorGlyph_(const iFontFile *d, uint32_t glyphIndex) {
    const iFtFontData *fd = ftData_FontFile(d);
    if (!fd || !fd->ftFace || !fd->hasColorGlyphs) return iFalse;
    /* Check COLR v0: glyph has color layers. */
    FT_LayerIterator iter;
    memset(&iter, 0, sizeof(iter));
    FT_UInt layerGlyphIdx, layerColorIdx;
    if (FT_Get_Color_Glyph_Layer(fd->ftFace, glyphIndex,
                                  &layerGlyphIdx, &layerColorIdx, &iter)) {
        return iTrue;
    }
    /* Check CBDT/CBLC: glyph has an embedded color bitmap at a reasonable size. */
    FT_Set_Pixel_Sizes(fd->ftFace, 0, 16); /* any size; just checking for bitmap */
    if (FT_Load_Glyph(fd->ftFace, glyphIndex, FT_LOAD_COLOR | FT_LOAD_NO_HINTING) == 0 &&
        fd->ftFace->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
        return iTrue;
    }
    return iFalse;
}

float horizKern_Font_(iRasterFont *d, uint32_t glyph1, uint32_t glyph2) {
    iUnused(d, glyph1, glyph2);
    return 0.0f; /* FreeType uses HarfBuzz for all kerning; no manual tweaks needed */
}

iBool rasterizeForCache_Font_(iRasterFont *font, iGlyph *glyph, SDL_Surface *surfaces[4],
                              SDL_Palette *palette) {
    iBool isColor = iFalse;
    for (int si = 0; si < numOffsetSteps_Glyph_; si++) {
        if (!isRasterized_Glyph_(glyph, si)) {
            int w = 0, h = 0;
            uint8_t *bmp = rasterizeGlyphEx_FontFile_(
                font->font.file, font->xScale, font->yScale,
                si * offsetStep_Glyph_(), index_Glyph_(glyph),
                &w, &h, &isColor);
            if (isColor) {
                glyph->colorRect = uploadColorGlyph_(currentRaster_Text_(), bmp, w, h);
                glyph->flags    |= isColor_GlyphFlag | rasterized0_GlyphFlag;
                free(bmp);
                break; /* color emoji handled; all subpixel slots share rect[0] */
            }
            if (bmp) {
                surfaces[si] = SDL_CreateRGBSurfaceWithFormatFrom(
                    bmp, w, h, 8, w, SDL_PIXELFORMAT_INDEX8);
                SDL_SetSurfaceBlendMode(surfaces[si], SDL_BLENDMODE_NONE);
                SDL_SetSurfacePalette(surfaces[si], palette);
            }
        }
    }
    return isColor;
}

/*----------------------------------------------------------------------------------------------*/

enum iFontId fontId_Text(const void *font) {
    return fontId_Text_(font);
}

iBaseFont *font_Text(enum iFontId id) {
    return (iBaseFont *) font_FtText_(id);
}

void init_FtText(iFtText *d, SDL_Renderer *render, float documentFontSizeFactor) {
    initFtLibrary_();
    init_RasterText(d, render, documentFontSizeFactor);
    iText *oldActive = current_Text();
    setCurrent_Text(&d->base);
    initCache_FtText_(d);
    initFonts_RasterText(d);
    setCurrent_Text(oldActive);
}

void deinit_FtText(iFtText *d) {
    deinitFonts_RasterText(d);
    deinitCache_FtText_(d);
    deinit_RasterText(d);
    doneFtLibrary_();
}

void setOpacity_Text(float opacity) {
    SDL_SetTextureAlphaMod(current_FtText_()->grayscaleCache.texture,
                           iClamp(opacity, 0.0f, 1.0f) * 255 + 0.5f);
}

void resetFontsIfNeeded_Text(iText *d) {
    if (!d->needRefresh) return;
    d->needRefresh = iFalse;
    iText *oldActive = current_Text();
    iFtText *s = (iFtText *) d;
    setCurrent_Text(d);
    clearCachedFontRuns_RasterText_(s);
    deinitFonts_RasterText(s);
    deinitCache_FtText_(s);
    initCache_FtText_(s);
    initFonts_RasterText(s);
    setCurrent_Text(oldActive);
}

void resetFontCache_Text(iText *d) {
    iText *oldActive = current_Text();
    setCurrent_Text(d);
    resetCache_FtText_((iFtText *) d);
    setCurrent_Text(oldActive);
}

iBool checkMissing_Text(void) {
    iFtText *d = current_FtText_();
    const iBool missing = d->missingGlyphs;
    d->missingGlyphs = iFalse;
    return missing;
}

iChar missing_Text(size_t index) {
    iFtText *d = current_FtText_();
    if (index >= iElemCount(d->missingChars)) return 0;
    return d->missingChars[index];
}

void resetMissing_Text(iText *d) {
    ((iFtText *) d)->missingGlyphs = iFalse;
    iZap(((iFtText *) d)->missingChars);
}

SDL_Texture *glyphCache_Text(void) {
    return current_FtText_()->grayscaleCache.texture;
}

iText *new_Text(SDL_Renderer *render, float documentFontSizeFactor) {
    iFtText *d = iMalloc(FtText);
    init_FtText(d, render, documentFontSizeFactor);
    return (iText *) d;
}

void delete_Text(iText *d) {
    deinit_FtText((iFtText *) d);
    free(d);
}
