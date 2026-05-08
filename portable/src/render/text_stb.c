/* Copyright 2020-2026 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "text.h"
#include "text_stb.h"
#include "app.h"
#include "attributedtext.h"
#include "color.h"
#include "paint.h"
#include <lagrange/resources.h>
#include "ui/metrics.h"
#include "ui/window.h"

#include <the_Foundation/array.h>
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/hash.h>
#include <the_Foundation/math.h>
#include <the_Foundation/stringlist.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/path.h>
#include <the_Foundation/ptrset.h>
#include <the_Foundation/vec2.h>
#include <SDL_surface.h>
#include <SDL_render.h>
#include <SDL_hints.h>
#include <SDL_version.h>
#include <stdarg.h>

#if defined (LAGRANGE_ENABLE_HARFBUZZ)
#   include <hb.h>
#endif

#if defined (LAGRANGE_ENABLE_FRIBIDI)
#   include <fribidi.h>
#endif

#if SDL_VERSION_ATLEAST(2, 0, 10)
#   define LAGRANGE_RASTER_DEPTH    8
#   define LAGRANGE_RASTER_FORMAT   SDL_PIXELFORMAT_INDEX8
#else
#   define LAGRANGE_RASTER_DEPTH    32
#   define LAGRANGE_RASTER_FORMAT   SDL_PIXELFORMAT_RGBA8888
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "../stb_truetype.h"

typedef iRasterFont iFont;
typedef iRasterText iStbText;


#if defined (LAGRANGE_ENABLE_HARFBUZZ)
hb_font_t *hbFont_FontFile(const iFontFile *d) {
    return stbData_FontFile(d)->hbFont;
}
#endif

iLocalDef iStbText *current_StbText_(void) {
    return currentRaster_Text_();
}

iLocalDef iFont *font_Text_(enum iFontId id) {
    iAssert(current_StbText_());
    return at_Array(&current_StbText_()->fonts, id & mask_FontId);
}

iBaseFont *font_Text(enum iFontId id) {
    return (iBaseFont *) font_Text_(id);
}

static enum iFontId fontId_Text_(const iFont *font) {
    return (enum iFontId) (font - (const iFont *) constData_Array(&current_StbText_()->fonts));
}

enum iFontId fontId_Text(const void *font) {
    return fontId_Text_(font);
}

static void initCache_StbText_  (iStbText *d) { initGrayscaleCache_RasterText_(d); }
static void deinitCache_StbText_(iStbText *d) { deinitGrayscaleCache_RasterText_(d); }

void init_StbText(iStbText *d, SDL_Renderer *render, float documentFontSizeFactor) {
    init_RasterText(d, render, documentFontSizeFactor);
    iText *oldActive = current_Text();
    setCurrent_Text(&d->base);
    initCache_StbText_(d);
    initFonts_RasterText(d);
    setCurrent_Text(oldActive);
}

void deinit_StbText(iStbText *d) {
    deinitFonts_RasterText(d);
    deinitCache_StbText_(d);
    deinit_RasterText(d);
}

/*----------------------------------------------------------------------------------------------*/
/* Backend font metrics API (called from fontpack.c). */

void allocData_FontFile(iFontFile *d) {
    iStbFontData *bd = malloc(sizeof(iStbFontData));
    const size_t offset = stbtt_GetFontOffsetForIndex(constData_Block(&d->sourceData),
                                                      d->colIndex);
    stbtt_InitFont(&bd->stbInfo, constData_Block(&d->sourceData), offset);
    stbtt_GetFontVMetrics(&bd->stbInfo, &d->ascent, &d->descent, NULL);
    stbtt_GetCodepointHMetrics(&bd->stbInfo, 'M', &d->emAdvance, NULL);
#if defined (LAGRANGE_ENABLE_HARFBUZZ)
    bd->hbBlob = hb_blob_create(constData_Block(&d->sourceData), size_Block(&d->sourceData),
                                HB_MEMORY_MODE_READONLY, NULL, NULL);
    bd->hbFace = hb_face_create(bd->hbBlob, d->colIndex);
    bd->hbFont = hb_font_create(bd->hbFace);
#endif
    d->data = bd;
}

void deallocData_FontFile(iFontFile *d) {
    iStbFontData *bd = stbData_FontFile(d);
    if (!bd) return;
#if defined (LAGRANGE_ENABLE_HARFBUZZ)
    hb_font_destroy(bd->hbFont);
    hb_face_destroy(bd->hbFace);
    hb_blob_destroy(bd->hbBlob);
#endif
    free(bd);
    d->data = NULL;
}

uint32_t findGlyphIndex_FontFile(const iFontFile *d, iChar ch) {
    return stbtt_FindGlyphIndex(&stbData_FontFile(d)->stbInfo, ch);
}

iBool isMonospace_FontFile(const iFontFile *d) {
    const iStbFontData *bd = stbData_FontFile(d);
    if (!bd) return iFalse;
    int em, i, period;
    stbtt_GetCodepointHMetrics(&bd->stbInfo, 'M', &em, NULL);
    stbtt_GetCodepointHMetrics(&bd->stbInfo, 'i', &i, NULL);
    stbtt_GetCodepointHMetrics(&bd->stbInfo, '.', &period, NULL);
    return em == i && em == period;
}

float scaleForPixelHeight_FontFile(const iFontFile *d, int pixelHeight) {
    return stbtt_ScaleForPixelHeight(&stbData_FontFile(d)->stbInfo, pixelHeight);
}

uint8_t *rasterizeGlyph_FontFile(const iFontFile *d, float xScale, float yScale, float xShift,
                                 uint32_t glyphIndex, int *w, int *h) {
    return stbtt_GetGlyphBitmapSubpixel(
        &stbData_FontFile(d)->stbInfo, xScale, yScale, xShift, 0.0f, glyphIndex, w, h, 0, 0);
}

void measureGlyph_FontFile(const iFontFile *d, uint32_t glyphIndex,
                           float xScale, float yScale, float xShift,
                           int *x0, int *y0, int *x1, int *y1) {
    stbtt_GetGlyphBitmapBoxSubpixel(
        &stbData_FontFile(d)->stbInfo, glyphIndex, xScale, yScale, xShift, 0.0f, x0, y0, x1, y1);
}

int glyphAdvance_FontFile(const iFontFile *d, uint32_t glyphIndex) {
    int adv = 0;
    stbtt_GetGlyphHMetrics(&stbData_FontFile(d)->stbInfo, glyphIndex, &adv, NULL);
    return adv;
}

/*----------------------------------------------------------------------------------------------*/

iText *new_Text(SDL_Renderer *render, float documentFontSizeFactor) {
    iStbText *d = iMalloc(StbText);
    init_StbText(d, render, documentFontSizeFactor);
    return (iText *) d;
}

void delete_Text(iText *d) {
    deinit_StbText((iStbText *) d);
    free(d);
}

void setOpacity_Text(float opacity) {
    SDL_SetTextureAlphaMod(current_StbText_()->grayscaleCache.texture,
                           iClamp(opacity, 0.0f, 1.0f) * 255 + 0.5f);
}

static void resetCache_StbText_(iStbText *d) {
    deinitCache_StbText_(d);
    iForEach(Array, i, &d->fonts) {
        clearGlyphs_GlyphTable_(((iFont *) i.value)->table);
    }
    initCache_StbText_(d);
}

void resetGlyphCaches_(void) {
    resetCache_StbText_(current_StbText_());
}

void resetFontsIfNeeded_Text(iText *d) {
    if (!d->needRefresh) return;
    d->needRefresh = iFalse;
    iText *oldActive = current_Text();
    iStbText *s = (iStbText *) d;
    setCurrent_Text(d); /* some routines rely on the global `activeText_` pointer */
    deinitFonts_RasterText(s);
    deinitCache_StbText_(s);
    initCache_StbText_(s);
    initFonts_RasterText(s);
    setCurrent_Text(oldActive);
}

void resetFontCache_Text(iText *d) {
    iText *oldActive = current_Text();
    setCurrent_Text(d); /* some routines rely on the global `activeText_` pointer */
    resetCache_StbText_((iStbText *) d);
    setCurrent_Text(oldActive);
}

static SDL_Palette *glyphPalette_(void) {
    return prefs_App()->fontSmoothing ? current_StbText_()->grayscale
                                      : current_StbText_()->blackAndWhite;
}

static SDL_Surface *rasterizeGlyph_Font_(const iFont *d, uint32_t glyphIndex, float xShift) {
    int w, h;
    uint8_t *bmp = rasterizeGlyph_FontFile(d->font.file, d->xScale, d->yScale, xShift, glyphIndex,
                                           &w, &h);
    SDL_Surface *surface8 =
        SDL_CreateRGBSurfaceWithFormatFrom(bmp, w, h, 8, w, SDL_PIXELFORMAT_INDEX8);
    SDL_SetSurfaceBlendMode(surface8, SDL_BLENDMODE_NONE);
    SDL_SetSurfacePalette(surface8, glyphPalette_());
#if LAGRANGE_RASTER_DEPTH != 8
    /* Convert to the cache format. */
    SDL_Surface *surf = SDL_ConvertSurfaceFormat(surface8, LAGRANGE_RASTER_FORMAT, 0);
    SDL_SetSurfaceBlendMode(surf, SDL_BLENDMODE_NONE);
    free(bmp);
    SDL_FreeSurface(surface8);
    return surf;
#else
    return surface8;
#endif
}

void allocate_Font_(iRasterFont *d, iGlyph *glyph, int hoff) {
    iRect *glRect = &glyph->rect[hoff];
    int    x0, y0, x1, y1;
    measureGlyph_FontFile(d->font.file, index_Glyph_(glyph), d->xScale, d->yScale,
                          hoff * offsetStep_Glyph_(),
                          &x0, &y0, &x1, &y1);
    glRect->size = init_I2(x1 - x0, y1 - y0);
    if (glRect->size.x > 0 && glRect->size.y > 0) {
        glRect->pos = assignPos_GlyphCache(&current_StbText_()->grayscaleCache, glRect->size);
    }
    else {
        /* Zero-size glyph (whitespace, invisible): nothing to rasterize or cache. */
        setRasterized_Glyph_(glyph, hoff);
    }
    glyph->d[hoff] = init_I2(x0, y0);
    glyph->d[hoff].y += d->vertOffset;
    if (hoff == 0) { /* hoff>=1 uses same metrics as `glyph` */
        glyph->advance = d->xScale * glyphAdvance_FontFile(d->font.file, index_Glyph_(glyph));
    }
}

iBool rasterizeForCache_Font_(iRasterFont *font, iGlyph *glyph, SDL_Surface *surfaces[4],
                              SDL_Palette *palette) {
    for (int si = 0; si < numOffsetSteps_Glyph_; si++) {
        if (!isRasterized_Glyph_(glyph, si)) {
            int w, h;
            uint8_t *bmp = rasterizeGlyph_FontFile(
                font->font.file, font->xScale, font->yScale,
                si * offsetStep_Glyph_(), index_Glyph_(glyph), &w, &h);
            if (!bmp) continue;
            SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(
                bmp, w, h, 8, w, SDL_PIXELFORMAT_INDEX8);
            SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_NONE);
            SDL_SetSurfacePalette(s, palette);
#if LAGRANGE_RASTER_DEPTH != 8
            SDL_Surface *conv = SDL_ConvertSurfaceFormat(s, LAGRANGE_RASTER_FORMAT, 0);
            SDL_SetSurfaceBlendMode(conv, SDL_BLENDMODE_NONE);
            free(bmp);
            SDL_FreeSurface(s);
            surfaces[si] = conv;
#else
            surfaces[si] = s; /* pixels owned by surface (SDL_PREALLOC) */
#endif
        }
    }
    return iFalse; /* grayscale only; no color emoji in STB backend */
}

iBool isColorGlyph_(const iFontFile *d, uint32_t glyphIndex) {
    iUnused(d, glyphIndex);
    return iFalse; /* STB TrueType does not support color glyphs */
}

float horizKern_Font_(iFont *d, uint32_t glyph1, uint32_t glyph2) {
#if defined (LAGRANGE_ENABLE_KERNING)
    if (!enableKerning_Text || ~d->font.spec->flags & fixNunitoKerning_FontSpecFlag) {
        return 0.0f;
    }
    if (glyph1 && glyph2) {
        /* These indices will be quickly found from the lookup table. */
        const uint32_t gi_h = glyphIndex_Font_(d, 'h');
        const uint32_t gi_i = glyphIndex_Font_(d, 'i');
        int kern = 0;
        /* Nunito needs some kerning fixes. */
        if (glyph1 == glyphIndex_Font_(d, 'W') && (glyph2 == gi_h || glyph2 == gi_i)) {
            kern = -60;
        }
        else if (glyph1 == glyphIndex_Font_(d, 'T') && glyph2 == gi_h) {
            kern = -25;
        }
        else if (glyph1 == glyphIndex_Font_(d, 'V') && glyph2 == gi_i) {
            kern = -40;
        }
        return d->xScale * kern;
    }
#endif
    return 0.0f;
}

/*----------------------------------------------------------------------------------------------*/

iBool checkMissing_Text(void) {
    iStbText *d = current_StbText_();
    const iBool missing = d->missingGlyphs;
    d->missingGlyphs = iFalse;
    return missing;
}

iChar missing_Text(size_t index) {
    iStbText *d = current_StbText_();
    if (index >= iElemCount(d->missingChars)) {
        return 0;
    }
    return d->missingChars[index];
}

void resetMissing_Text(iText *d) {
    ((iStbText *) d)->missingGlyphs = iFalse;
    iZap(((iStbText *) d)->missingChars);
}

SDL_Texture *glyphCache_Text(void) {
    return current_StbText_()->grayscaleCache.texture;
}
