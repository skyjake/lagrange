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

/* Text rendering backend using FreeType for Windows, Linux, and Android.
   Uses HarfBuzz for shaping and the shared text_backend
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
#include "media/stb_image_resize2.h"

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

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_version.h>

#if defined (LAGRANGE_ENABLE_HB_RASTER)
#   include <hb-ot.h>
#   include <hb-raster.h>
#endif

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*----------------------------------------------------------------------------------------------*/

static FT_Library ftLibrary_     = NULL;
static int        ftLibraryRefs_ = 0;
#if defined (LAGRANGE_ENABLE_HB_RASTER)
static hb_raster_paint_t *rasterPaint_ = NULL; /* reused for all COLRv1 glyphs */
#endif

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
#if defined (LAGRANGE_ENABLE_HB_RASTER)
        if (rasterPaint_) {
            hb_raster_paint_destroy(rasterPaint_);
            rasterPaint_ = NULL;
        }
#endif
    }
}

void doneFtLibrary_FtText(void) {
    doneFtLibrary_();
}

/*----------------------------------------------------------------------------------------------*/
/* FontFile backend API (declared in fontpack.h, called from fontpack.c). */

/* Some color fonts (e.g. Segoe UI Emoji) have a low ascent, so the glyphs extend above the
   line box. Returns the shift that centers a reference Emoji's ink in the line box. */
static float colorVertShift_(FT_Face face, int ascent, int descent) {
    const FT_UInt glyphIndex = FT_Get_Char_Index(face, 0x1F600); /* grinning face */
    if (!glyphIndex) return 0.0f;
    FT_Pos yMin = 0, yMax = 0;
    iBool  found = iFalse;
    FT_LayerIterator iter;
    memset(&iter, 0, sizeof(iter));
    FT_UInt layerGlyphIdx, layerColorIdx;
    while (FT_Get_Color_Glyph_Layer(face, glyphIndex, &layerGlyphIdx, &layerColorIdx, &iter)) {
        if (FT_Load_Glyph(face, layerGlyphIdx, FT_LOAD_NO_SCALE) != 0) continue;
        FT_BBox bbox;
        FT_Outline_Get_CBox(&face->glyph->outline, &bbox);
        if (bbox.yMin >= bbox.yMax) continue;
        yMin  = found ? iMin(yMin, bbox.yMin) : bbox.yMin;
        yMax  = found ? iMax(yMax, bbox.yMax) : bbox.yMax;
        found = iTrue;
    }
    if (!found) return 0.0f;
    return (float) (yMin + yMax) / 2.0f - (float) (ascent + descent) / 2.0f;
}

/* Bitmap strikes (CBDT, sbix) and COLR v0 layers can be rendered, and COLRv1 paint graphs
   with hb-raster. Otherwise COLRv1 fonts (e.g., Noto Color Emoji on Android 13+) would
   produce empty glyphs. */
static iBool hasRenderableColor_(FT_Face face, iBool *hasColrV1_out) {
    *hasColrV1_out = iFalse;
    if (!FT_HAS_COLOR(face)) {
        return iFalse;
    }
    if (FT_HAS_FIXED_SIZES(face)) {
        return iTrue;
    }
    FT_Byte  header[4];
    FT_ULong len = sizeof(header);
    if (FT_Load_Sfnt_Table(face, TTAG_COLR, 0, header, &len) == 0) {
#if defined (LAGRANGE_ENABLE_HB_RASTER)
        if (((header[0] << 8) | header[1]) >= 1) {
            *hasColrV1_out = iTrue;
            return iTrue;
        }
#endif
        return ((header[2] << 8) | header[3]) > 0; /* numBaseGlyphRecords */
    }
    return iFalse;
}

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
    fd->hasColorGlyphs = hasRenderableColor_(face, &fd->hasColrV1);
    /* Some color fonts (e.g. Noto Color Emoji) have only fixed-size bitmap strikes;
       FT_Set_Pixel_Sizes() only works at their native size, so pick the best one now. */
    fd->isFixedSize = !FT_IS_SCALABLE(face) && face->num_fixed_sizes > 0;
    if (fd->isFixedSize) {
        int best = 0;
        for (int i = 1; i < face->num_fixed_sizes; i++) {
            if (face->available_sizes[i].height > face->available_sizes[best].height) {
                best = i;
            }
        }
        fd->fixedSizeIndex = best;
        fd->fixedSizePpem  = face->available_sizes[best].height;
    }
    else if (fd->hasColorGlyphs) {
        fd->colorVertShift = colorVertShift_(face, d->ascent, d->descent);
    }
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
    if (d->charMap && (ch = d->charMap(ch)) == 0) {
        return 0;
    }
    return FT_Get_Char_Index(fd->ftFace, ch);
}

#if defined (LAGRANGE_ENABLE_HARFBUZZ)
static hb_bool_t mappedNominalGlyph_(hb_font_t *font, void *fontData, hb_codepoint_t unicode,
                                     hb_codepoint_t *glyph, void *userData) {
    const iFontFile *d      = fontData;
    const iChar      mapped = d->charMap(unicode);
    iUnused(userData);
    return mapped && hb_font_get_nominal_glyph(hb_font_get_parent(font), mapped, glyph);
}
#endif

void setCharMap_FontFile(iFontFile *d, iChar (*charMap)(iChar)) {
    iAssert(!d->charMap);
    d->charMap = charMap;
#if defined (LAGRANGE_ENABLE_HARFBUZZ)
    /* Shaping must see the remapped characters, too. */
    iFtFontData *fd = ftData_FontFile(d);
    if (charMap && fd && fd->hbFont) {
        static hb_font_funcs_t *funcs_;
        if (!funcs_) {
            funcs_ = hb_font_funcs_create();
            hb_font_funcs_set_nominal_glyph_func(funcs_, mappedNominalGlyph_, NULL, NULL);
            hb_font_funcs_make_immutable(funcs_);
        }
        hb_font_t *mapped = hb_font_create_sub_font(fd->hbFont);
        hb_font_set_funcs(mapped, funcs_, d, NULL);
        hb_font_destroy(fd->hbFont);
        fd->hbFont = mapped;
    }
#endif
}

iBool isMonospace_FontFile(const iFontFile *d) {
    const iFtFontData *fd = ftData_FontFile(d);
    if (!fd || !fd->ftFace) return iFalse;
    return FT_IS_FIXED_WIDTH(fd->ftFace) != 0;
}

float scaleForPixelHeight_FontFile(const iFontFile *d, int pixelHeight) {
    if (d->ascent - d->descent <= 0) return 1.0f;
    return (float) pixelHeight / (float) (d->ascent - d->descent);
}

/* Selects the native strike and returns the scale factor to the requested xScale size
   (using ascent-descent as units-per-em, since unitsPerEm is 0 for these faces). */
static float prepareFixedSize_(FT_Face face, const iFontFile *d, const iFtFontData *fd,
                                float xScale) {
    FT_Select_Size(face, fd->fixedSizeIndex);
    if (fd->fixedSizePpem <= 0) return 1.0f;
    const float targetPpem = xScale * (float) (d->ascent - d->descent);
    return targetPpem / (float) fd->fixedSizePpem;
}

int glyphAdvance_FontFile(const iFontFile *d, uint32_t glyphIndex) {
    const iFtFontData *fd = ftData_FontFile(d);
    if (!fd || !fd->ftFace) return 0;
    if (fd->isFixedSize) {
        /* Advance is unused here (HarfBuzz computes it); just select the right strike. */
        FT_Select_Size(fd->ftFace, fd->fixedSizeIndex);
        return 0;
    }
    /* Design units, regardless of the face's current char size. */
    if (FT_Load_Glyph(fd->ftFace, glyphIndex, FT_LOAD_NO_SCALE) != 0) {
        return 0;
    }
    return (int) fd->ftFace->glyph->advance.x;
}

/* Vertical-only hinting keeps glyph shapes consistent with the unhinted HarfBuzz advances,
   which is required for fractional positioning. No autohinter: it stretches glyphs vertically
   to round the x-height, while native TrueType (v40) hints are vertical-only, too. */
static const FT_Int32 outlineLoadFlags_ = FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_AUTOHINT;

static FT_UInt colorPpem_(const iFontFile *d, float xScale) {
    return iMax(1, (FT_UInt) roundf(xScale * (float) d->unitsPerEm));
}

/* Fractional size matches the unrounded scale of HarfBuzz advances. */
static void setCharSize_(FT_Face face, const iFontFile *d, float xScale, float yScale) {
    const float upm64 = (float) d->unitsPerEm * 64.0f;
    FT_Set_Char_Size(face,
                     iMax(64, (FT_F26Dot6) roundf(xScale * upm64)),
                     iMax(64, (FT_F26Dot6) roundf(yScale * upm64)),
                     72, 72);
}

/* Bitmap glyphs are not affected by the shift. */
static FT_Error loadShiftedGlyph_(FT_Face face, uint32_t glyphIndex, FT_Int32 loadFlags,
                                  float xShift) {
    FT_Vector delta = { (FT_Pos) roundf(xShift * 64.0f), 0 };
    FT_Set_Transform(face, NULL, &delta);
    const FT_Error err = FT_Load_Glyph(face, glyphIndex, loadFlags);
    FT_Set_Transform(face, NULL, NULL);
    return err;
}

/* COLR v0 canvas bounds in FreeType coordinates (Y up). */
static iBool colrV0Bounds_(FT_Face face, FT_UInt glyphIndex, FT_UInt ppem,
                           int *x0, int *y0, int *x1, int *y1) {
    FT_LayerIterator iter;
    memset(&iter, 0, sizeof(iter));
    FT_UInt layerGlyphIdx, layerColorIdx;
    if (!FT_Get_Color_Glyph_Layer(face, glyphIndex, &layerGlyphIdx, &layerColorIdx, &iter)) {
        return iFalse;
    }
    FT_Set_Pixel_Sizes(face, ppem, ppem);
    if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0) {
        return iFalse;
    }
    /* Many COLR base glyphs have an empty outline; fall back to the scaled
       ascender/descender range in that case. */
    FT_BBox bbox;
    FT_Outline_Get_CBox(&face->glyph->outline, &bbox);
    *x0 = (int)(bbox.xMin >> 6);
    *y0 = (int)(bbox.yMin >> 6);
    *x1 = (int)((bbox.xMax + 63) >> 6);
    *y1 = (int)((bbox.yMax + 63) >> 6);
    if (*x0 >= *x1 || *y0 >= *y1) {
        const float scale = (float) ppem / (float) face->units_per_EM;
        *x0 = 0;
        *y0 = (int)(face->descender * scale);
        *x1 = (int)((face->glyph->metrics.horiAdvance >> 6) + 1);
        *y1 = (int)(face->ascender * scale);
    }
    return *x1 > *x0 && *y1 > *y0;
}

#if defined (LAGRANGE_ENABLE_HB_RASTER)
static iBool isColrV1Glyph_(const iFtFontData *fd, uint32_t glyphIndex) {
    return fd->hasColrV1 && fd->hbFont &&
           hb_ot_color_glyph_has_paint(hb_font_get_face(fd->hbFont), glyphIndex);
}

static hb_raster_paint_t *rasterPaint_FtText_(void) {
    if (!rasterPaint_) {
        rasterPaint_ = hb_raster_paint_create_or_fail();
    }
    return rasterPaint_;
}

/* Sets up the paint extents of a COLRv1 glyph at `ppem` pixels; font units become pixels. */
static hb_font_t *prepareColrV1_(const iFtFontData *fd, uint32_t glyphIndex, FT_UInt ppem,
                                 hb_raster_extents_t *ext_out) {
    hb_raster_paint_t *paint = rasterPaint_FtText_();
    if (!paint) return NULL;
    hb_font_t *font = hb_font_create_sub_font(fd->hbFont);
    hb_font_set_scale(font, (int) ppem, (int) ppem);
    hb_glyph_extents_t gext;
    if (!hb_font_get_glyph_extents(font, glyphIndex, &gext) ||
        !hb_raster_paint_set_glyph_extents(paint, &gext) ||
        !hb_raster_paint_get_extents(paint, ext_out)) {
        hb_font_destroy(font);
        return NULL;
    }
    return font;
}

static iBool colrV1Bounds_(const iFtFontData *fd, uint32_t glyphIndex, FT_UInt ppem,
                           int *x0, int *y0, int *x1, int *y1) {
    hb_raster_extents_t ext;
    hb_font_t *font = prepareColrV1_(fd, glyphIndex, ppem, &ext);
    if (!font) return iFalse;
    hb_font_destroy(font);
    hb_raster_paint_clear(rasterPaint_);
    /* Y up, like colrV0Bounds_(). */
    *x0 = ext.x_origin;
    *y0 = ext.y_origin;
    *x1 = ext.x_origin + (int) ext.width;
    *y1 = ext.y_origin + (int) ext.height;
    return ext.width > 0 && ext.height > 0;
}

/* Returns a top-down BGRA bitmap with premultiplied alpha. */
static uint8_t *renderColrV1_(const iFtFontData *fd, uint32_t glyphIndex, FT_UInt ppem,
                              int *w_out, int *h_out) {
    hb_raster_extents_t ext;
    hb_font_t *font = prepareColrV1_(fd, glyphIndex, ppem, &ext);
    if (!font) return NULL;
    hb_raster_paint_t *paint = rasterPaint_;
    uint8_t *buf = NULL;
    if (hb_raster_paint_glyph_or_fail(paint, font, glyphIndex)) {
        hb_raster_image_t *img = hb_raster_paint_render(paint);
        if (img) {
            hb_raster_image_get_extents(img, &ext);
            const uint8_t *src = hb_raster_image_get_buffer(img);
            if (src && ext.width > 0 && ext.height > 0) {
                const size_t rowSize = (size_t) ext.width * 4;
                buf = malloc(rowSize * ext.height);
                for (unsigned int row = 0; row < ext.height; row++) {
                    /* Image rows are stored bottom-to-top. */
                    memcpy(buf + row * rowSize, src + (ext.height - 1 - row) * ext.stride, rowSize);
                }
                *w_out = (int) ext.width;
                *h_out = (int) ext.height;
            }
            hb_raster_paint_recycle_image(paint, img);
        }
    }
    else {
        hb_raster_paint_clear(paint);
    }
    hb_font_destroy(font);
    return buf;
}
#endif /* LAGRANGE_ENABLE_HB_RASTER */

static void measureGlyphEx_FontFile_(const iFontFile *d, uint32_t glyphIndex,
                                     float xScale, float yScale, float xShift,
                                     int *x0, int *y0, int *x1, int *y1, iBool *isBitmap_out) {
    const iFtFontData *fd = ftData_FontFile(d);
    *x0 = *y0 = *x1 = *y1 = 0;
    *isBitmap_out = iFalse;
    if (!fd || !fd->ftFace) return;
    FT_Face face = fd->ftFace;
    if (fd->isFixedSize) {
        *isBitmap_out = iTrue;
        const float scale = prepareFixedSize_(face, d, fd, xScale);
        if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_COLOR | FT_LOAD_RENDER) != 0) return;
        FT_GlyphSlot slot = face->glyph;
        if (slot->bitmap.width == 0 || slot->bitmap.rows == 0) return;
        /* Match rasterizeGlyphEx_FontFile_()'s rounding so sizes stay consistent. */
        *x0 = (int) roundf(slot->bitmap_left * scale);
        *y0 = (int) roundf(-slot->bitmap_top * scale);
        *x1 = *x0 + iMax(1, (int) roundf((float) slot->bitmap.width * scale));
        *y1 = *y0 + iMax(1, (int) roundf((float) slot->bitmap.rows * scale));
        return;
    }
    if (fd->hasColorGlyphs && isColorGlyph_(d, glyphIndex)) {
        /* Measure the same bitmap that rasterizeGlyphEx_FontFile_() produces; the composited
           layers may not match the base glyph's outline metrics. */
        const FT_UInt ppem  = colorPpem_(d, xScale);
        const int     shift = (int) roundf(fd->colorVertShift * yScale);
        int cx0, cy0, cx1, cy1;
#if defined (LAGRANGE_ENABLE_HB_RASTER)
        if (isColrV1Glyph_(fd, glyphIndex)) {
            if (colrV1Bounds_(fd, glyphIndex, ppem, &cx0, &cy0, &cx1, &cy1)) {
                *x0 = cx0;
                *y0 = shift - cy1;
                *x1 = cx1;
                *y1 = shift - cy0;
            }
            return;
        }
#endif
        FT_Set_Pixel_Sizes(face, ppem, ppem);
        if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_COLOR | FT_LOAD_RENDER) == 0) {
            FT_GlyphSlot slot = face->glyph;
            if (slot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA &&
                slot->bitmap.width > 0 && slot->bitmap.rows > 0) {
                *x0 = slot->bitmap_left;
                *y0 = shift - slot->bitmap_top;
                *x1 = *x0 + (int) slot->bitmap.width;
                *y1 = *y0 + (int) slot->bitmap.rows;
                return;
            }
        }
        if (colrV0Bounds_(face, glyphIndex, ppem, &cx0, &cy0, &cx1, &cy1)) {
            *x0 = cx0;
            *y0 = shift - cy1;
            *x1 = cx1;
            *y1 = shift - cy0;
        }
        return;
    }
    setCharSize_(face, d, xScale, yScale);
    /* Without FT_LOAD_RENDER, the bitmap metrics are preset to match a later render. */
    if (loadShiftedGlyph_(face, glyphIndex, outlineLoadFlags_, xShift) != 0) return;
    FT_GlyphSlot slot = face->glyph;
    *isBitmap_out = (slot->format == FT_GLYPH_FORMAT_BITMAP);
    if (slot->bitmap.width == 0 || slot->bitmap.rows == 0) return;
    *x0 = slot->bitmap_left;
    *y0 = -slot->bitmap_top;
    *x1 = *x0 + (int) slot->bitmap.width;
    *y1 = *y0 + (int) slot->bitmap.rows;
}

void measureGlyph_FontFile(const iFontFile *d, uint32_t glyphIndex,
                           float xScale, float yScale, float xShift,
                           int *x0, int *y0, int *x1, int *y1) {
    iBool isBitmap;
    measureGlyphEx_FontFile_(d, glyphIndex, xScale, yScale, xShift, x0, y0, x1, y1, &isBitmap);
}

/* COLR v0: composite all layers into a BGRA canvas.
   Used when FT_LOAD_COLOR doesn't auto-produce BGRA (FreeType < 2.12). */
static uint8_t *renderColrV0_(FT_Face face, FT_UInt glyphIndex, FT_UInt ppem,
                               int *w_out, int *h_out) {
    int x0, y0, x1, y1;
    if (!colrV0Bounds_(face, glyphIndex, ppem, &x0, &y0, &x1, &y1)) {
        return NULL;
    }
    const int cw = x1 - x0;
    const int ch = y1 - y0;
    FT_LayerIterator iter;
    FT_UInt layerGlyphIdx, layerColorIdx;

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
    if (fd->isFixedSize) {
        const float scale = prepareFixedSize_(face, d, fd, xScale);
        if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_COLOR | FT_LOAD_RENDER) != 0) return NULL;
        FT_GlyphSlot slot = face->glyph;
        if (slot->bitmap.pixel_mode != FT_PIXEL_MODE_BGRA ||
            slot->bitmap.width == 0 || slot->bitmap.rows == 0) {
            return NULL;
        }
        const int sw = (int) slot->bitmap.width;
        const int sh = (int) slot->bitmap.rows;
        const int dw = iMax(1, (int) roundf(sw * scale));
        const int dh = iMax(1, (int) roundf(sh * scale));
        *w = dw;
        *h = dh;
        *isColor_out = iTrue;
        /* FreeType's BGRA bitmaps use premultiplied alpha; STBIR_BGRA_PM matches that. */
        return stbir_resize_uint8_linear(slot->bitmap.buffer, sw, sh, slot->bitmap.pitch,
                                         NULL, dw, dh, 0, STBIR_BGRA_PM);
    }
    /* First try FT_LOAD_COLOR which on FreeType >= 2.12 renders both CBDT and COLR as BGRA. */
    if (fd->hasColorGlyphs) {
        const FT_UInt ppem = colorPpem_(d, xScale);
#if defined (LAGRANGE_ENABLE_HB_RASTER)
        if (isColrV1Glyph_(fd, glyphIndex)) {
            uint8_t *colr = renderColrV1_(fd, glyphIndex, ppem, w, h);
            if (colr) {
                *isColor_out = iTrue;
                return colr;
            }
        }
#endif
        FT_Set_Pixel_Sizes(face, ppem, ppem);
        if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_COLOR | FT_LOAD_RENDER) == 0) {
            FT_GlyphSlot slot = face->glyph;
            if (slot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA &&
                slot->bitmap.width > 0 && slot->bitmap.rows > 0) {
                *w = (int) slot->bitmap.width;
                *h = (int) slot->bitmap.rows;
                *isColor_out = iTrue;
                const size_t rowSize = (size_t)(*w) * 4;
                uint8_t *buf = malloc(rowSize * (size_t)(*h));
                for (int row = 0; row < *h; row++) {
                    memcpy(buf + row * rowSize,
                           slot->bitmap.buffer + row * slot->bitmap.pitch,
                           rowSize);
                }
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
    setCharSize_(face, d, xScale, yScale);
    if (loadShiftedGlyph_(face, glyphIndex, outlineLoadFlags_ | FT_LOAD_RENDER, xShift) != 0) {
        return NULL;
    }
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

/* Returns a heap-allocated 8-bit grayscale (or BGRA color) bitmap. The caller must free() it. */
uint8_t *rasterizeGlyph_FontFile(const iFontFile *d, float xScale, float yScale, float xShift,
                                 uint32_t glyphIndex, int *w, int *h) {
    iBool isColor;
    return rasterizeGlyphEx_FontFile_(d, xScale, yScale, xShift, glyphIndex, w, h, &isColor);
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
    for (int h = cc->rowAllocStep; h <= 5 * textSize + cc->rowAllocStep; h += cc->rowAllocStep) {
        pushBack_Array(&cc->rows, &(iCacheRow){ .height = 0 });
    }
    cc->bottom       = 0;
    cc->texture      = SDL_CreateTexture(d->base.render,
                                         SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC | SDL_TEXTUREACCESS_TARGET,
                                         cc->size.x, cc->size.y);
    SDL_SetTextureScaleMode(cc->texture, SDL_SCALEMODE_NEAREST);
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
    if (hoff > 0 && !hasOffsetVariants_Glyph_(glyph)) {
        *glRect        = zero_Rect();
        glyph->d[hoff] = glyph->d[0];
        setRasterized_Glyph_(glyph, hoff);
        return;
    }
    int   x0, y0, x1, y1;
    iBool isBitmap;
    measureGlyphEx_FontFile_(d->font.file, index_Glyph_(glyph), d->xScale, d->yScale,
                             hoff * offsetStep_Glyph_(), &x0, &y0, &x1, &y1, &isBitmap);
    if (isBitmap) {
        glyph->flags |= fixedOffset_GlyphFlag;
    }
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
static void uploadColorGlyph_(iFtText *tx, uint8_t *bgraBitmap, iRect atlasRect) {
    iGlyphCache *cc = &tx->colorCache;
    const int    w  = atlasRect.size.x;
    const int    h  = atlasRect.size.y;
    /* Swap BGRA → RGBA for SDL (SDL_PIXELFORMAT_RGBA32 is RGBA on all platforms). */
    uint8_t *rgba = malloc((size_t) w * h * 4);
    for (int i = 0; i < w * h; i++) {
        rgba[i * 4 + 0] = bgraBitmap[i * 4 + 2]; /* R ← B */
        rgba[i * 4 + 1] = bgraBitmap[i * 4 + 1]; /* G */
        rgba[i * 4 + 2] = bgraBitmap[i * 4 + 0]; /* B ← R */
        rgba[i * 4 + 3] = bgraBitmap[i * 4 + 3]; /* A */
    }
    SDL_Surface *surf = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, rgba, w * 4);
    SDL_SetSurfaceBlendMode(surf, SDL_BLENDMODE_NONE);
    SDL_Renderer *render   = current_Text()->render;
    SDL_Texture  *bufTex   = SDL_CreateTextureFromSurface(render, surf);
    SDL_Texture  *oldTarget = SDL_GetRenderTarget(render);
    SDL_SetRenderTarget(render, cc->texture);
    SDL_SetTextureBlendMode(bufTex, SDL_BLENDMODE_NONE);
    SDL_RenderTexture(render, bufTex, NULL,
                      &(SDL_FRect){ atlasRect.pos.x, atlasRect.pos.y,
                                    atlasRect.size.x, atlasRect.size.y });
    SDL_SetRenderTarget(render, oldTarget);
    SDL_DestroyTexture(bufTex);
    SDL_DestroySurface(surf);
    free(rgba);
}

iBool hasColorGlyphs_FontFile(const iFontFile *d) {
    const iFtFontData *fd = ftData_FontFile(d);
    return fd && fd->hasColorGlyphs;
}

iBool isColorGlyph_(const iFontFile *d, uint32_t glyphIndex) {
    const iFtFontData *fd = ftData_FontFile(d);
    if (!fd || !fd->ftFace || !fd->hasColorGlyphs) return iFalse;
#if defined (LAGRANGE_ENABLE_HB_RASTER)
    if (isColrV1Glyph_(fd, glyphIndex)) {
        return iTrue;
    }
#endif
    /* Check COLR v0: glyph has color layers. */
    FT_LayerIterator iter;
    memset(&iter, 0, sizeof(iter));
    FT_UInt layerGlyphIdx, layerColorIdx;
    if (FT_Get_Color_Glyph_Layer(fd->ftFace, glyphIndex,
                                  &layerGlyphIdx, &layerColorIdx, &iter)) {
        return iTrue;
    }
    /* Check CBDT/CBLC: glyph has an embedded color bitmap. Fixed-size faces must select their
       native strike directly; FT_Set_Pixel_Sizes() with an arbitrary size yields an empty bitmap. */
    if (fd->isFixedSize) {
        FT_Select_Size(fd->ftFace, fd->fixedSizeIndex);
    }
    else {
        FT_Set_Pixel_Sizes(fd->ftFace, 0, 16); /* any size; just checking for bitmap */
    }
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
                /* colorRect was already reserved in glyphByIndex_Font_(); sizes must match. */
                const iBool sizeMatches =
                    w == glyph->colorRect.size.x && h == glyph->colorRect.size.y;
                iAssert(bmp == NULL || sizeMatches);
                if (bmp && sizeMatches) {
                    uploadColorGlyph_(currentRaster_Text_(), bmp, glyph->colorRect);
                }
                free(bmp);
                glyph->flags |= isColor_GlyphFlag | rasterized0_GlyphFlag;
                break; /* color emoji handled; all subpixel slots share rect[0] */
            }
            if (bmp) {
                surfaces[si] = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_INDEX8, bmp, w);
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
