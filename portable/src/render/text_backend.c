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

/* Overview of types:

- Text : top-level text renderer instance (one per window)
- Font : a font's assets for rendering, e.g., metrics and cached glyphs
- Glyph : hash node; a single cached glyph, with Rect in cache texture
- AttributedText : text string to be drawn that is split into sub-runs by attributes (font, color)
- AttributedRun : a run inside AttributedText
- GlyphBuffer : HarfBuzz-shaped glyphs corresponding to an AttributedRun
- FontRun : cached state (e.g., AttributedText, glyphs) needed for rendering a text string
- FontRunArgs : set of arguments for constructing a FontRun
- RunArgs : input arguments for `run_Font_` (the low-level text rendering routine)
- RunLayer : arguments for processing the glyphs of a GlyphBuffer (layers: background, foreground)

Optimization notes:

- Caching FontRuns is quite effective, but there is still plenty of unnecessary iteration
  of glyphs during wrapping of long text. It could help if there is a direct mapping between
  wrapPosRange and a GlyphBuffer's glyph indices.
*/

#include "text_backend.h"
#include "text.h"
#include "attributedtext.h"
#include "paint.h"
#include "../app.h"
#include "color.h"

#include <the_Foundation/math.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/string.h>
#include <lagrange/defs.h>
#include <lagrange/prefs.h>

#include <SDL_hints.h>
#include <SDL_version.h>

#if SDL_VERSION_ATLEAST(2, 0, 10)
#   define LAGRANGE_RASTER_DEPTH    8
#   define LAGRANGE_RASTER_FORMAT   SDL_PIXELFORMAT_INDEX8
#else
#   define LAGRANGE_RASTER_DEPTH    32
#   define LAGRANGE_RASTER_FORMAT   SDL_PIXELFORMAT_RGBA8888
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

iLocalDef enum iFontSize sizeId_Text_(const iRasterFont *d) {
    return size_FontId(fontId_Text(d));
}

iLocalDef enum iFontStyle styleId_Text_(const iRasterFont *d) {
    return style_FontId(fontId_Text(d));
}

/*----------------------------------------------------------------------------------------------*/
/* Subpixel rendering settings */

int enableHalfPixelGlyphs_Text = iTrue;
int enableKerning_Text          = iTrue;
int numOffsetSteps_Glyph_       = 4;
int rasterizedAll_GlyphFlag_    = 0xf;

int makeRasterizedAll_GlyphFlag_(int n) {
    int flag = rasterized0_GlyphFlag;
    if (n > 1) flag |= rasterized1_GlyphFlag;
    if (n > 2) flag |= rasterized2_GlyphFlag;
    if (n > 3) flag |= rasterized3_GlyphFlag;
    return flag;
}

/*----------------------------------------------------------------------------------------------*/
/* Glyph */

void init_Glyph(iGlyph *d, uint32_t glyphIndex) {
    d->node.key = glyphIndex;
    d->flags    = 0;
    d->font     = NULL;
    d->advance  = 0.0f;
    iZap(d->rect);
    iZap(d->d);
}

void deinit_Glyph(iGlyph *d) {
    iUnused(d);
}

iDefineTypeConstructionArgs(Glyph, (uint32_t glyphIndex), glyphIndex)

/*----------------------------------------------------------------------------------------------*/
/* GlyphTable */

void clearGlyphs_GlyphTable_(iGlyphTable *d) {
    if (d) {
        iForEach(Hash, i, &d->glyphs) {
            delete_Glyph((iGlyph *) i.value);
        }
        clear_Hash(&d->glyphs);
    }
}

void init_GlyphTable(iGlyphTable *d) {
    init_Hash(&d->glyphs);
    memset(d->indexTable, 0xff, sizeof(d->indexTable));
}

void deinit_GlyphTable(iGlyphTable *d) {
    clearGlyphs_GlyphTable_(d);
    deinit_Hash(&d->glyphs);
}

iDefineTypeConstruction(GlyphTable)

/*----------------------------------------------------------------------------------------------*/
/* GlyphCache */

void init_GlyphCache(iGlyphCache *d) {
    d->texture      = NULL;
    d->size         = zero_I2();
    d->rowAllocStep = 0;
    d->bottom       = 0;
    init_Array(&d->rows, sizeof(iCacheRow));
}

void deinit_GlyphCache(iGlyphCache *d) {
    if (d->texture) {
        SDL_DestroyTexture(d->texture);
        d->texture = NULL;
    }
    deinit_Array(&d->rows);
}

iInt2 assignPos_GlyphCache(iGlyphCache *d, iInt2 glyphSize) {
    iAssert(d->texture);
    iAssert(glyphSize.x > 0 && glyphSize.y > 0);
    /* Index directly into the pre-allocated rows array: one bucket per height step. */
    iCacheRow *cur = at_Array(&d->rows, (glyphSize.y - 1) / d->rowAllocStep);
    if (cur->height == 0) {
        cur->height = (1 + (glyphSize.y - 1) / d->rowAllocStep) * d->rowAllocStep;
        cur->pos.y  = d->bottom;
        d->bottom   = cur->pos.y + cur->height;
    }
    if (cur->pos.x + glyphSize.x > d->size.x) {
        /* Row full: advance vertically to a new strip. */
        cur->pos.y  = d->bottom;
        cur->pos.x  = 0;
        d->bottom  += cur->height;
        iAssert(d->bottom <= d->size.y);
    }
    const iInt2 assigned = cur->pos;
    cur->pos.x += glyphSize.x;
    return assigned;
}

void reset_GlyphCache(iGlyphCache *d) {
    if (!d->texture) return;
    d->bottom = 0;
    iForEach(Array, i, &d->rows) {
        iCacheRow *row = i.value;
        row->height    = 0;
        row->pos       = zero_I2();
    }
    /* Clear the texture. */
    SDL_SetRenderTarget(current_Text()->render, d->texture);
    SDL_SetRenderDrawColor(current_Text()->render, 0, 0, 0, 0);
    SDL_RenderClear(current_Text()->render);
    SDL_SetRenderTarget(current_Text()->render, NULL);
}

/*----------------------------------------------------------------------------------------------*/
/* RasterText common init/deinit */

void init_RasterText(iRasterText *d, SDL_Renderer *render, float documentFontSizeFactor) {
    init_Text(&d->base, render, documentFontSizeFactor);
    init_Array(&d->fonts, sizeof(iRasterFont));
    init_Array(&d->fontPriorityOrder, sizeof(iPrioMapItem));
    d->overrideFontId = -1;
    d->missingGlyphs  = iFalse;
    iZap(d->missingChars);
    iZap(d->cachedFontRuns);
    init_GlyphCache(&d->colorCache);
    /* Grayscale palette: antialiased glyph mask tinted at render time. */ {
        SDL_Color colors[256];
        for (int i = 0; i < 256; ++i) {
            colors[i] = (SDL_Color){ 255, 255, 255, (uint8_t)(255 * powf(i / 255.0f, 1.0f) + 0.5f) };
        }
        d->grayscale = SDL_AllocPalette(256);
        SDL_SetPaletteColors(d->grayscale, colors, 0, 256);
    }
    /* Black-and-white palette for unsmoothed (bitmap) glyphs. */ {
        SDL_Color colors[256];
        for (int i = 0; i < 256; ++i) {
            colors[i] = (SDL_Color){ 255, 255, 255, i < 100 ? 0 : 255 };
        }
        d->blackAndWhite = SDL_AllocPalette(256);
        SDL_SetPaletteColors(d->blackAndWhite, colors, 0, 256);
    }
}

void clearCachedFontRuns_RasterText_(iRasterText *d) {
#if defined (LAGRANGE_ENABLE_HARFBUZZ)
    iForIndices(i, d->cachedFontRuns) {
        delete_FontRun(d->cachedFontRuns[i]);
        d->cachedFontRuns[i] = NULL;
    }
#else
    iUnused(d);
#endif
}

void deinit_RasterText(iRasterText *d) {
#if defined (LAGRANGE_ENABLE_HARFBUZZ)
    clearCachedFontRuns_RasterText_(d);
#endif
    SDL_FreePalette(d->blackAndWhite);
    SDL_FreePalette(d->grayscale);
    deinit_Array(&d->fontPriorityOrder);
    deinit_Array(&d->fonts);
    deinit_Text(&d->base);
}

int maxGlyphHeight_Text_(const iText *d) {
    return 4 * d->contentFontSize * fontSize_UI; /* max is huge = 2x contentFontSize */
}

void initGrayscaleCache_RasterText_(iRasterText *d) {
    iGlyphCache *gc      = &d->grayscaleCache;
    const int    textSize = d->base.contentFontSize * fontSize_UI;
    iAssert(textSize > 0);
    numOffsetSteps_Glyph_    = get_Window()->pixelRatio < 2.0f   ? 4
                               : get_Window()->pixelRatio < 2.5f ? 3
                                                                  : 2;
    rasterizedAll_GlyphFlag_ = makeRasterizedAll_GlyphFlag_(numOffsetSteps_Glyph_);
#if !defined (NDEBUG)
    printf("[Text] subpixel offsets: %d\n", numOffsetSteps_Glyph_);
#endif
    init_GlyphCache(gc);
    const iInt2 cacheDims = init_I2(8 * numOffsetSteps_Glyph_, 40);
    gc->size = mul_I2(cacheDims, init1_I2(iMax(textSize, fontSize_UI)));
    SDL_RendererInfo ri;
    SDL_GetRendererInfo(d->base.render, &ri);
    if (ri.max_texture_height > 0 && gc->size.y > ri.max_texture_height) {
        gc->size.y = ri.max_texture_height;
        gc->size.x = ri.max_texture_width;
    }
    gc->rowAllocStep = iMax(2, textSize / 6);
    for (int h = gc->rowAllocStep; h <= 5 * textSize + gc->rowAllocStep; h += gc->rowAllocStep) {
        pushBack_Array(&gc->rows, &(iCacheRow){ .height = 0 });
    }
    gc->bottom = 0;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    gc->texture = SDL_CreateTexture(d->base.render,
                                    SDL_PIXELFORMAT_RGBA4444,
                                    SDL_TEXTUREACCESS_STATIC | SDL_TEXTUREACCESS_TARGET,
                                    gc->size.x, gc->size.y);
    SDL_SetTextureBlendMode(gc->texture, SDL_BLENDMODE_BLEND);
}

void deinitGrayscaleCache_RasterText_(iRasterText *d) {
    deinit_GlyphCache(&d->grayscaleCache);
}

static void setupFontVariants_RasterText_(iText *base, const iFontSpec *spec, int baseId,
                                          float uiSize, float textSize) {
    iRasterText *d = (iRasterText *) base;
    if (spec->flags & override_FontSpecFlag && d->overrideFontId < 0) {
        d->overrideFontId = baseId;
    }
    pushBack_Array(&d->fontPriorityOrder, &(iPrioMapItem){ spec->priority, baseId });
    for (enum iFontStyle style = 0; style < max_FontStyle; style++) {
        for (enum iFontSize sizeId = 0; sizeId < max_FontSize; sizeId++) {
            init_Font(at_Array(&d->fonts, FONT_ID(baseId, style, sizeId)),
                      spec,
                      spec->styles[style],
                      sizeId,
                      (sizeId < contentRegular_FontSize ? uiSize : textSize) *
                          scale_FontSize(sizeId));
        }
    }
}

static iBool hasVariant_RasterText_(iText *base, const iFontSpec *spec) {
    const iRasterText *d = (const iRasterText *) base;
    for (size_t i = 0; i < size_Array(&d->fonts); i += maxVariants_Fonts) {
        if (((const iRasterFont *) constAt_Array(&d->fonts, i))->font.spec == spec) return iTrue;
    }
    return iFalse;
}

static int allocateSlot_RasterText_(iText *base) {
    iRasterText *d = (iRasterText *) base;
    const int fontId = (int) size_Array(&d->fonts);
    resize_Array(&d->fonts, fontId + maxVariants_Fonts);
    return fontId;
}

void initFonts_RasterText(iRasterText *d) {
    resize_Array(&d->fonts, auxiliary_FontId);
    initFonts_Text(&d->base,
                   &d->fontPriorityOrder,
                   &d->overrideFontId,
                   &d->monoFallback,
                   &(iFontInitCallbacks){
                       .setupSpec = setupFontVariants_RasterText_,
                       .hasSpec   = hasVariant_RasterText_,
                       .alloc     = allocateSlot_RasterText_,
                   });
#if !defined (NDEBUG)
    printf("[Text] %zu font variants ready\n", size_Array(&d->fonts));
#endif
}

void deinitFonts_RasterText(iRasterText *d) {
    iForEach(Array, i, &d->fonts) {
        deinit_Font(i.value);
    }
    clear_Array(&d->fonts);
    clear_Array(&d->fontPriorityOrder);
    d->overrideFontId = -1;
}

/*----------------------------------------------------------------------------------------------*/
/* Font variant init/deinit */

void init_Font(iRasterFont *d, const iFontSpec *fontSpec, const iFontFile *fontFile,
               enum iFontSize sizeId, float height) {
    const int   scaleType  = scaleType_FontSpec(sizeId);
    const float glyphScale = fontSpec->glyphScale[scaleType];
    d->font.spec   = fontSpec;
    d->font.file   = fontFile;
    d->font.height = (int) (height * fontSpec->heightScale[scaleType]);
    d->xScale = d->yScale = scaleForPixelHeight_FontFile(fontFile, d->font.height) * glyphScale;
    if (isMonospaced_RasterFont(d)) {
        /* Monospaced fonts must align 1:1 with the pixel grid to avoid seams
           between adjacent box-drawing characters. */
        const float advance = (float) fontFile->emAdvance * d->xScale;
        if (advance > 4) {
            d->xScale *= floorf(advance) / advance;
        }
    }
    d->emAdvance     = fontFile->emAdvance * d->xScale;
    d->font.baseline = fontFile->ascent * d->yScale;
    d->vertOffset    = d->font.height * (1.0f - glyphScale) / 2 *
                       fontSpec->vertOffsetScale[scaleType];
    d->table = NULL;
}

void deinit_Font(iRasterFont *d) {
    delete_GlyphTable(d->table);
}

/*----------------------------------------------------------------------------------------------*/
/* Character font fallback */

uint32_t glyphIndex_Font_(iRasterFont *d, iChar ch) {
    const size_t entry = ch - 32;
    if (!d->table) {
        d->table = new_GlyphTable();
    }
    iGlyphTable *table = d->table;
    if (entry < iElemCount(table->indexTable)) {
        if (table->indexTable[entry] == ~0u) {
            table->indexTable[entry] = findGlyphIndex_FontFile(d->font.file, ch);
        }
        return table->indexTable[entry];
    }
    return findGlyphIndex_FontFile(d->font.file, ch);
}

iRasterFont *characterFont_Font_(iRasterFont *d, iChar ch, uint32_t *glyphIndex) {
    if (isVariationSelector_Char(ch)) {
        return d;
    }
    const enum iFontStyle styleId      = styleId_Text_(d);
    const enum iFontSize  sizeId       = sizeId_Text_(d);
    const iBool           isMonospaced = isMonospaced_RasterFont(d);
    iRasterText *tx    = currentRaster_Text_();
    iRasterFont *overrideFont = NULL;
    if (ch != 0x20 && tx->overrideFontId >= 0) {
        overrideFont = (iRasterFont *) font_Text(FONT_ID(tx->overrideFontId, styleId, sizeId));
        if (overrideFont != d && (*glyphIndex = glyphIndex_Font_(overrideFont, ch)) != 0) {
            return overrideFont;
        }
    }
    if ((*glyphIndex = glyphIndex_Font_(d, ch)) != 0) {
        return d;
    }
    for (int preferMonospaced = isMonospaced ? 1 : 0; preferMonospaced >= 0; preferMonospaced--) {
        iConstForEach(Array, i, &tx->fontPriorityOrder) {
            iRasterFont *font = (iRasterFont *) font_Text(
                FONT_ID(((const iPrioMapItem *) i.value)->fontIndex, styleId, sizeId));
            if (font == d || font == overrideFont) continue;
            if (preferMonospaced && !isMonospaced_RasterFont(font)) continue;
            if (!font->font.file || !font->font.file->data) continue;
            if ((*glyphIndex = glyphIndex_Font_(font, ch)) != 0) {
                return font;
            }
        }
    }
    if (!*glyphIndex) {
        fprintf(stderr, "failed to find %08x (%lc)\n", ch, (int) ch); fflush(stderr);
        tx->missingGlyphs = iTrue;
        iBool gotIt = iFalse;
        for (size_t i = 0; i < iElemCount(tx->missingChars); i++) {
            if (tx->missingChars[i] == ch) { gotIt = iTrue; break; }
        }
        if (!gotIt) {
            memmove(tx->missingChars + 1, tx->missingChars,
                    sizeof(tx->missingChars) - sizeof(tx->missingChars[0]));
            tx->missingChars[0] = ch;
        }
    }
    return d;
}

iGlyph *glyph_Font_(iRasterFont *d, iChar ch) {
    uint32_t glyphIndex = 0;
    iRasterFont *font = characterFont_Font_(d, ch, &glyphIndex);
    return glyphByIndex_Font_(font, glyphIndex);
}

iChar nextChar_(const char **chPos, const char *end) {
    if (*chPos == end) return 0;
    iChar ch;
    int len = decodeBytes_MultibyteChar(*chPos, end, &ch);
    if (len <= 0) { (*chPos)++; return 0; }
    (*chPos) += len;
    return ch;
}

iBaseFont *characterFont_BaseFont(iBaseFont *d, iChar ch) {
    uint32_t    glyphIndex = 0;
    iRasterFont *result    = characterFont_Font_((iRasterFont *) d, ch, &glyphIndex);
    return glyphIndex ? (iBaseFont *) result : NULL;
}

/*----------------------------------------------------------------------------------------------*/
/* GlyphBuffer */

#if defined (LAGRANGE_ENABLE_HARFBUZZ)

void init_GlyphBuffer_(iGlyphBuffer *d, iRasterFont *font, const iChar *logicalText) {
    d->hb          = hb_buffer_create();
    d->font        = font;
    d->logicalText = logicalText;
    d->glyphInfo   = NULL;
    d->glyphPos    = NULL;
    d->glyphCount  = 0;
    d->script      = 0;
}

void deinit_GlyphBuffer_(iGlyphBuffer *d) {
    hb_buffer_destroy(d->hb);
}

void shape_GlyphBuffer_(iGlyphBuffer *d) {
    if (!d->glyphInfo) {
        hb_shape(hbFont_FontFile(d->font->font.file), d->hb, NULL, 0);
        d->glyphInfo = hb_buffer_get_glyph_infos(d->hb, &d->glyphCount);
        d->glyphPos  = hb_buffer_get_glyph_positions(d->hb, &d->glyphCount);
    }
}

float advance_GlyphBuffer_(const iGlyphBuffer *d, iRangei wrapPosRange) {
    float x = 0.0f;
    for (unsigned int i = 0; i < d->glyphCount; i++) {
        const int logPos = d->glyphInfo[i].cluster;
        if (logPos < wrapPosRange.start || logPos >= wrapPosRange.end) {
            continue;
        }
        x += d->font->xScale * d->glyphPos[i].x_advance;
        if (d->logicalText[logPos] == '\t') {
            /* Tab stop: snap to next multiple of tabWidth em-advances. */
            const float stop = prefs_App()->tabWidth * d->font->emAdvance;
            x = floorf(x / stop) * stop + stop;
        }
    }
    return x;
}

void evenMonospaceAdvances_GlyphBuffer_(iGlyphBuffer *d, iRasterFont *baseFont) {
    const float monoAdvance = baseFont->emAdvance;
    for (unsigned int i = 0; i < d->glyphCount; ++i) {
        const hb_glyph_info_t *info = d->glyphInfo + i;
        if (d->glyphPos[i].x_advance > 0 && d->font != baseFont) {
            const iChar ch = d->logicalText[info->cluster];
            if (ch == 0x20 || isPictograph_Char(ch) || isEmoji_Char(ch) ||
                (ch >= 0x1fb00 && ch <= 0x1fbff /* legacy computing */)) {
                const float dw = d->font->xScale * d->glyphPos[i].x_advance -
                                 (isEmoji_Char(ch) ? 2 : 1) * monoAdvance;
                d->glyphPos[i].x_offset  -= dw / 2 / d->font->xScale - 1;
                d->glyphPos[i].x_advance -= dw     / d->font->xScale - 1;
            }
        }
    }
}

void alignOtherFontsVertically_GlyphBuffer_(iGlyphBuffer *d, iRasterFont *baseFont) {
    if (d->font->font.height > baseFont->font.height) {
        const int offset = (d->font->font.height - baseFont->font.height) / 2;
        for (unsigned int i = 0; i < d->glyphCount; ++i) {
            d->glyphPos[i].y_offset += offset / d->font->yScale;
        }
    }
}

static float justificationWeight_(iChar c) {
    if (c == '.' || c == '!' || c == '?' || c == ';') {
        return 2.0f;
    }
    return 1.0f;
}

void justify_GlyphBuffer_(iGlyphBuffer *buffers, size_t numBuffers,
                          iRangei wrapPosRange, float *wrapAdvance,
                          int available, iBool isLast) {
    iGlyphBuffer *begin           = buffers;
    iGlyphBuffer *end             = buffers + numBuffers;
    float         outerSpace      = available - *wrapAdvance;
    float         totalInnerSpace = 0.0f;
    float         numSpaces       = 0;
    int           numAdvancing    = 0;
    const float   maxExpansion    = 0.14f;
    if (isLast || outerSpace <= 0) {
        return;
    }
#define CHECK_LOGPOS() \
    if (logPos < wrapPosRange.start) continue; \
    if (logPos >= wrapPosRange.end) break
    for (iGlyphBuffer *buf = begin; buf != end; buf++) {
        for (unsigned int i = 0; i < buf->glyphCount; i++) {
            hb_glyph_info_t     *info   = &buf->glyphInfo[i];
            hb_glyph_position_t *pos    = &buf->glyphPos[i];
            const int            logPos = info->cluster;
            CHECK_LOGPOS();
            if (pos->x_advance > 0) {
                numAdvancing++;
            }
            if (buf->logicalText[logPos] == 0x20) {
                totalInnerSpace += pos->x_advance * buf->font->xScale;
                numSpaces += justificationWeight_(buf->logicalText[iMax(0, logPos - 1)]);
            }
        }
    }
    if (numSpaces >= 2 && totalInnerSpace > 0) {
        outerSpace = iMin(outerSpace, *wrapAdvance * maxExpansion);
        float adv = 0.0f;
        for (iGlyphBuffer *buf = begin; buf != end; buf++) {
            const float xScale = buf->font->xScale;
            for (unsigned int i = 0; i < buf->glyphCount; i++) {
                hb_glyph_info_t     *info   = &buf->glyphInfo[i];
                hb_glyph_position_t *pos    = &buf->glyphPos[i];
                const int            logPos = info->cluster;
                CHECK_LOGPOS();
                if (buf->logicalText[logPos] == 0x20) {
                    const float weight = justificationWeight_(buf->logicalText[iMax(0, logPos - 1)]);
                    pos->x_advance =
                        (weight * (totalInnerSpace + outerSpace) / numSpaces) / xScale;
                }
                adv += pos->x_advance * xScale;
            }
        }
        *wrapAdvance = adv;
    }
    if (numAdvancing > 1 && *wrapAdvance < available - 1.0f) {
        const float expandable = *wrapAdvance;
        outerSpace = available - expandable;
        for (iGlyphBuffer *buf = begin; buf != end; buf++) {
            if (buf->script) continue;
            const float xScale = buf->font->xScale;
            for (unsigned int i = 0; i < buf->glyphCount; i++) {
                hb_glyph_info_t     *info   = &buf->glyphInfo[i];
                hb_glyph_position_t *pos    = &buf->glyphPos[i];
                const int            logPos = info->cluster;
                CHECK_LOGPOS();
                if (pos->x_advance > 0) {
                    pos->x_advance += (outerSpace / (numAdvancing - 1)) / xScale;
                }
            }
        }
        *wrapAdvance = available;
    }
#undef CHECK_LOGPOS
}

#endif /* LAGRANGE_ENABLE_HARFBUZZ — GlyphBuffer */

#if defined (LAGRANGE_ENABLE_HARFBUZZ)

/*----------------------------------------------------------------------------------------------*/
/* FontRun */

static const hb_script_t hbScripts_[max_Script] = {
    0,
    HB_SCRIPT_ARABIC,
    HB_SCRIPT_BENGALI,
    HB_SCRIPT_DEVANAGARI,
    HB_SCRIPT_HAN,
    HB_SCRIPT_HIRAGANA,
    HB_SCRIPT_KATAKANA,
    HB_SCRIPT_ORIYA,
    HB_SCRIPT_TAMIL,
};

void init_FontRun(iFontRun *d, const iFontRunArgs *args, const iRangecc text, uint32_t crc) {
    d->textCrc32 = crc;
    d->args      = *args;
    init_AttributedText(&d->attrText,
                        text,
                        args->maxLen,
                        args->font,
                        args->color,
                        args->baseDir,
                        args->baseFont,
                        args->baseFgColorId,
                        args->overrideChar);
    const iChar *logicalText = constData_Array(&d->attrText.logical);
    const iChar *visualText  = constData_Array(&d->attrText.visual);
    const int *  logToVis    = constData_Array(&d->attrText.logicalToVisual);
    const int *  visToLog    = constData_Array(&d->attrText.visualToLogical);
    const size_t runCount    = size_Array(&d->attrText.runs);
    init_Array(&d->buffers, sizeof(iGlyphBuffer));
    resize_Array(&d->buffers, runCount);
#if defined (LAGRANGE_ENABLE_HARFBUZZ)
    iConstForEach(Array, i, &d->attrText.runs) {
        const iAttributedRun *run = i.value;
        iGlyphBuffer *buf = at_Array(&d->buffers, index_ArrayConstIterator(&i));
        init_GlyphBuffer_(buf, (iRasterFont *) run->font, logicalText);
        int v[2] = { logToVis[run->logical.start], logToVis[run->logical.end - 1] };
        if (v[0] > v[1]) {
            iSwap(int, v[0], v[1]);
        }
        for (int vis = v[0]; vis <= v[1]; vis++) {
            hb_buffer_add(buf->hb, visualText[vis], visToLog[vis]);
        }
        hb_buffer_set_content_type(buf->hb, HB_BUFFER_CONTENT_TYPE_UNICODE);
        hb_buffer_set_direction(buf->hb, HB_DIRECTION_LTR);
        const hb_script_t script = hbScripts_[run->flags.script];
        if (script) {
            buf->script = script;
            hb_buffer_set_script(buf->hb, script);
        }
        shape_GlyphBuffer_(buf);
    }
    if (isMonospaced_RasterFont(args->font)) {
        for (size_t ri = 0; ri < runCount; ri++) {
            evenMonospaceAdvances_GlyphBuffer_(at_Array(&d->buffers, ri), args->font);
        }
    }
    for (size_t ri = 0; ri < runCount; ri++) {
        alignOtherFontsVertically_GlyphBuffer_(at_Array(&d->buffers, ri), args->font);
    }
#else
    iUnused(logicalText, visualText, logToVis, visToLog, runCount);
#endif
}

void deinit_FontRun(iFontRun *d) {
#if defined (LAGRANGE_ENABLE_HARFBUZZ)
    iForEach(Array, b, &d->buffers) {
        deinit_GlyphBuffer_(b.value);
    }
#endif
    deinit_Array(&d->buffers);
    deinit_AttributedText(&d->attrText);
}

iDefineTypeConstructionArgs(FontRun,
                            (const iFontRunArgs *args, const iRangecc text, uint32_t crc),
                            args, text, crc)

static unsigned fontRunCacheHits_  = 0;
static unsigned fontRunCacheTotal_ = 0;

iFontRun *makeOrFindCachedFontRun_(iRasterText *d, const iFontRunArgs *runArgs,
                                   const iRangecc text, iBool *wasFound_out) {
    fontRunCacheTotal_++;
    const uint32_t crc = iCrc32(text.start, size_Range(&text));
    iForIndices(i, d->cachedFontRuns) {
        if (d->cachedFontRuns[i] &&
            d->cachedFontRuns[i]->textCrc32 == crc &&
            equal_FontRunArgs(runArgs, &d->cachedFontRuns[i]->args)) {
            d->cachedFontRuns[i]->attrText.source = text;
            fontRunCacheHits_++;
            *wasFound_out = iTrue;
            return d->cachedFontRuns[i];
        }
    }
    *wasFound_out = iFalse;
    delete_FontRun(d->cachedFontRuns[iElemCount(d->cachedFontRuns) - 1]);
    memmove(d->cachedFontRuns + 1,
            d->cachedFontRuns,
            sizeof(d->cachedFontRuns) - sizeof(d->cachedFontRuns[0]));
    d->cachedFontRuns[0] = new_FontRun(runArgs, text, crc);
    return d->cachedFontRuns[0];
}

#endif /* LAGRANGE_ENABLE_HARFBUZZ — FontRun */

/*----------------------------------------------------------------------------------------------*/
/* Glyph caching and cache_Text (used by both HarfBuzz and simple backends) */

iGlyph *glyphByIndex_Font_(iRasterFont *d, uint32_t glyphIndex) {
    if (!d->table) {
        d->table = new_GlyphTable();
    }
    iGlyph *glyph = (iGlyph *) value_Hash(&d->table->glyphs, glyphIndex);
    if (!glyph) {
        iRasterText *tx   = currentRaster_Text_();
        const int    maxH = maxGlyphHeight_Text_(&tx->base);
        const iBool  grayscaleFull = tx->grayscaleCache.bottom >
                                     tx->grayscaleCache.size.y - maxH;
        const iBool  colorFull = tx->colorCache.texture &&
                                 tx->colorCache.bottom >
                                 tx->colorCache.size.y - maxH;
        if (grayscaleFull || colorFull) {
#if !defined (NDEBUG)
            printf("[Text] %s cache is full, clearing all caches!\n",
                   grayscaleFull ? "grayscale" : "color"); fflush(stdout);
#endif
            resetGlyphCaches_();
        }
        glyph = new_Glyph(glyphIndex);
        glyph->font = d;
        if (isColorGlyph_(d->font.file, glyphIndex)) {
            /* Color glyph: skip grayscale allocation entirely.
               Only the advance is needed now; colorRect is filled by cacheGlyphs_Font_. */
            glyph->flags  |= isColor_GlyphFlag;
            glyph->advance = d->xScale * glyphAdvance_FontFile(d->font.file, glyphIndex);
        }
        else {
            for (int offsetIndex = 0; offsetIndex < numOffsetSteps_Glyph_; offsetIndex++) {
                allocate_Font_(d, glyph, offsetIndex);
            }
        }
        insert_Hash(&d->table->glyphs, &glyph->node);
    }
    return glyph;
}

/*----------------------------------------------------------------------------------------------*/
/* Glyph caching (grayscale atlas; color emoji handled per-backend via rasterizeForCache) */

iDeclareType(RasterGlyph)

struct Impl_RasterGlyph {
    iGlyph *glyph;
    int     hoff;
    iRect   rect;
};

static void flushGlyphsToCache_(iGlyphCache *cache, iArray *rasters,
                                  SDL_Surface *stagingBuf, SDL_Texture **oldTarget_inout,
                                  iBool *targetChanged_inout) {
    if (isEmpty_Array(rasters)) return;
    SDL_Renderer *render = current_Text()->render;
    SDL_Texture  *bufTex = SDL_CreateTextureFromSurface(render, stagingBuf);
    SDL_SetTextureBlendMode(bufTex, SDL_BLENDMODE_NONE);
    if (!*targetChanged_inout) {
        *targetChanged_inout = iTrue;
        *oldTarget_inout     = SDL_GetRenderTarget(render);
        SDL_SetRenderTarget(render, cache->texture);
    }
    iConstForEach(Array, i, rasters) {
        const iRasterGlyph *rg     = i.value;
        const iRect        *glRect = &rg->glyph->rect[rg->hoff];
        SDL_RenderCopy(render, bufTex,
                       (const SDL_Rect *) &rg->rect,
                       (const SDL_Rect *) glRect);
        setRasterized_Glyph_(rg->glyph, rg->hoff);
    }
    SDL_DestroyTexture(bufTex);
    clear_Array(rasters);
}

void cacheGlyphs_Font_(iRasterFont *d, const uint32_t *glyphIndices, size_t numGlyphIndices) {
    iGlyphCache *cache   = &currentRaster_Text_()->grayscaleCache;
    SDL_Surface *buf     = NULL;
    const iInt2  bufSize = init_I2(iMin(1024, d->font.height * iMin(5 * numGlyphIndices, 20)),
                                   d->font.height * 4 / 3);
    int          bufX    = 0;
    iArray      *rasters = NULL;
    SDL_Texture *oldTarget       = NULL;
    iBool        isTargetChanged = iFalse;
    SDL_Palette *palette = prefs_App()->fontSmoothing
                               ? currentRaster_Text_()->grayscale
                               : currentRaster_Text_()->blackAndWhite;
    iAssert(isExposed_Window(get_Window()));
    size_t index = 0;
    while (index < numGlyphIndices) {
        for (; index < numGlyphIndices; index++) {
            const uint32_t glyphIndex = glyphIndices[index];
            const int      lastBottom = cache->bottom;
            iGlyph *glyph = glyphByIndex_Font_(d, glyphIndex);
            if (cache->bottom < lastBottom) {
                /* Cache was reset due to overflow: restart. */
                bufX = 0;
                if (rasters) clear_Array(rasters);
                index = 0;
                break;
            }
            if ((glyph->flags & isColor_GlyphFlag) &&
                !isEmpty_Rect(glyph->colorRect)) continue; /* already uploaded to color atlas */
            if (isFullyRasterized_Glyph_(glyph)) continue;
            if (!buf) {
                rasters = new_Array(sizeof(iRasterGlyph));
                buf = SDL_CreateRGBSurfaceWithFormat(0, bufSize.x, bufSize.y,
                                                     LAGRANGE_RASTER_DEPTH,
                                                     LAGRANGE_RASTER_FORMAT);
                SDL_SetSurfaceBlendMode(buf, SDL_BLENDMODE_NONE);
                SDL_SetSurfacePalette(buf, palette);
            }
            SDL_Surface *surfaces[4] = { NULL, NULL, NULL, NULL };
            if (rasterizeForCache_Font_(glyph->font, glyph, surfaces, palette)) {
                continue; /* color glyph handled */
            }
            iBool outOfSpace = iFalse;
            iForIndices(i, surfaces) {
                if (surfaces[i]) {
                    const int w = iMin(surfaces[i]->w, bufSize.x);
                    const int h = surfaces[i]->h;
                    if (bufX + w <= bufSize.x) {
                        SDL_BlitSurface(surfaces[i], NULL, buf,
                                        &(SDL_Rect){ bufX, 0, w, h });
                        pushBack_Array(rasters,
                                       &(iRasterGlyph){ glyph, i, init_Rect(bufX, 0, w, h) });
                        bufX += w;
                    }
                    else {
                        outOfSpace = iTrue;
                        break;
                    }
                }
            }
            iForIndices(i, surfaces) {
                if (surfaces[i]) {
                    if (surfaces[i]->flags & SDL_PREALLOC) free(surfaces[i]->pixels);
                    SDL_FreeSurface(surfaces[i]);
                }
            }
            if (outOfSpace) break;
        }
        flushGlyphsToCache_(cache, rasters, buf, &oldTarget, &isTargetChanged);
        if (buf && rasters && isEmpty_Array(rasters)) bufX = 0;
    }
    if (rasters) delete_Array(rasters);
    if (buf) SDL_FreeSurface(buf);
    if (isTargetChanged) SDL_SetRenderTarget(current_Text()->render, oldTarget);
}

void cacheSingleGlyph_Font_(iRasterFont *d, uint32_t glyphIndex) {
    cacheGlyphs_Font_(d, &glyphIndex, 1);
}

void cache_Text(int fontId, iRangecc text) {
    iRasterFont *d = (iRasterFont *) font_Text(fontId);
    iArray       glyphIndices;
    iAttributedText attrText;
    init_Array(&glyphIndices, sizeof(uint32_t));
    init_AttributedText(&attrText, text, 0, d, none_ColorId, 0, d, none_ColorId, 0);
    const iChar *logicalText = constData_Array(&attrText.logical);
    iConstForEach(Array, i, &attrText.runs) {
        const iAttributedRun *run = i.value;
        if (run->flags.isLineBreak) continue;
        for (int pos = run->logical.start; pos < run->logical.end; pos++) {
            const iChar ch = logicalText[pos];
            if (!isSpace_Char(ch) && !isControl_Char(ch)) {
                const uint32_t gi = glyphIndex_Font_(d, ch);
                if (gi) pushBack_Array(&glyphIndices, &gi);
            }
        }
    }
    deinit_AttributedText(&attrText);
    cacheGlyphs_Font_(d, constData_Array(&glyphIndices), size_Array(&glyphIndices));
    deinit_Array(&glyphIndices);
}

/*----------------------------------------------------------------------------------------------*/
/* nextTabStop_Font_ is called by both run_Font_ (HarfBuzz) and text_simple.c (no HarfBuzz). */

float nextTabStop_Font_(const iRasterFont *d, float x) {
    const float stop = prefs_App()->tabWidth * d->emAdvance;
    return floorf(x / stop) * stop + stop;
}

#if defined (LAGRANGE_ENABLE_HARFBUZZ)

void process_RunLayer_(iRunLayer *d, int layerIndex) {
    iRasterText           *tx         = currentRaster_Text_();
    const iAttributedText *attrText   = &d->fontRun->attrText;
    const iArray          *buffers    = &d->fontRun->buffers;
    const iChar           *logicalText = constData_Array(&attrText->logical);
    for (size_t logRunIndex = 0; logRunIndex < size_Array(d->runOrder); logRunIndex++) {
        const size_t          runIndex = constValue_Array(d->runOrder, logRunIndex, size_t);
        const iAttributedRun *run      = constAt_Array(&attrText->runs, runIndex);
        if (run->flags.isLineBreak) {
            d->xCursor = 0.0f;
            d->yCursor += d->font->font.height;
            continue;
        }
        const iColor fgClr = fgColor_AttributedRun(run);
        const iColor bgClr = bgColor_AttributedRun(run);
        iBool isBgFilled = iFalse;
        if (~d->mode & permanentColorFlag_RunMode) {
            isBgFilled = (bgClr.a != 0) || (d->mode & fillBackground_RunMode);
        }
        const iGlyphBuffer *buf = constAt_Array(buffers, runIndex);
        iAssert(run->font == (iBaseFont *) buf->font);
        for (unsigned int i = 0; i < buf->glyphCount; i++) {
            const hb_glyph_info_t *info    = &buf->glyphInfo[i];
            const hb_codepoint_t   glyphId = info->codepoint;
            const int              logPos  = info->cluster;
            if (logPos < d->wrapPosRange.start || logPos >= d->wrapPosRange.end) {
                continue;
            }
            iRasterFont  *runFont  = (iRasterFont *) run->font;
            const float   xOffset  = runFont->xScale * buf->glyphPos[i].x_offset;
            float         yOffset  = runFont->yScale * buf->glyphPos[i].y_offset;
            const float   xAdvance = runFont->xScale * buf->glyphPos[i].x_advance;
            const float   yAdvance = runFont->yScale * buf->glyphPos[i].y_advance;
            const iGlyph *glyph    = glyphByIndex_Font_(runFont, glyphId);
            const iChar   ch       = logicalText[logPos];
            if (ch == '\t') {
                d->xCursor = nextTabStop_Font_(d->font, d->xCursor) - xAdvance;
            }
            const float xf       = d->xCursor + xOffset;
            float       subpixel = xf - (int) xf;
            if (subpixel < 0.0f) subpixel = 1.0f + subpixel;
            const int hoff = enableHalfPixelGlyphs_Text
                                 ? (int) (subpixel / offsetStep_Glyph_()) : 0;
            if (ch == 0x3001 || ch == 0x3002) { /* Ideographic Comma and Full Stop */
                if (yOffset == 0.0f) {
                    yOffset = glyph->d[hoff].y + glyph->rect[hoff].size.y +
                              glyph->d[hoff].y / 4;
                }
            }
            const iBool isColor = (glyph->flags & isColor_GlyphFlag) != 0;
            SDL_Rect dst;
            if (isColor) {
                dst = (SDL_Rect){
                    d->orig.x + (int)(d->xCursor + xOffset),
                    d->orig.y + (int)(d->yCursor - yOffset) + d->font->font.baseline,
                    glyph->colorRect.size.x, glyph->colorRect.size.y
                };
            }
            else {
                dst = (SDL_Rect){
                    d->orig.x + (int)(d->xCursor + xOffset) + glyph->d[hoff].x,
                    d->orig.y + (int)(d->yCursor - yOffset) + ((iRasterFont *) glyph->font)->font.baseline +
                        glyph->d[hoff].y,
                    glyph->rect[hoff].size.x, glyph->rect[hoff].size.y
                };
            }
            /* Align baselines of different fonts. */
            if (run->font != attrText->baseFont &&
                ~run->font->spec->flags & auxiliary_FontSpecFlag) {
                const int bl1 = ((iRasterFont *) attrText->baseFont)->font.baseline +
                                ((iRasterFont *) attrText->baseFont)->vertOffset;
                const int bl2 = runFont->font.baseline + runFont->vertOffset;
                dst.y += bl1 - bl2;
            }
            /* Update the bounding box. */
            if (layerIndex == background_RunLayerType) {
                if (d->mode & visualFlag_RunMode) {
                    if (isEmpty_Rect(d->bounds)) {
                        d->bounds = init_Rect(dst.x, dst.y, dst.w, dst.h);
                    }
                    else {
                        d->bounds = union_Rect(d->bounds, init_Rect(dst.x, dst.y, dst.w, dst.h));
                    }
                }
                else {
                    d->bounds.size.x = iMax(d->bounds.size.x, dst.x + dst.w - d->orig.x);
                    d->bounds.size.y = iMax(d->bounds.size.y,
                                            d->yCursor + ((iRasterFont *) glyph->font)->font.height);
                }
            }
            const iBool isSpace = (logicalText[logPos] == 0x20);
            if (d->mode & draw_RunMode && (isBgFilled || !isSpace)) {
                dst.x += origin_Paint.x;
                dst.y += origin_Paint.y;
                if (layerIndex == background_RunLayerType && isBgFilled) {
                    if (bgClr.a) {
                        SDL_SetRenderDrawColor(current_Text()->render,
                                               bgClr.r, bgClr.g, bgClr.b, 255);
                        const SDL_Rect bgRect = {
                            origin_Paint.x + d->orig.x + (int) d->xCursor,
                            origin_Paint.y + d->orig.y + (int) d->yCursor,
                            (int) ceilf(subpixel + xAdvance),
                            d->font->font.height,
                        };
                        SDL_RenderFillRect(current_Text()->render, &bgRect);
                    }
                    else if (d->mode & fillBackground_RunMode) {
                        SDL_SetRenderDrawColor(current_Text()->render,
                                               fgClr.r, fgClr.g, fgClr.b, 0);
                        SDL_RenderFillRect(current_Text()->render, &dst);
                    }
                }
                if (layerIndex == foreground_RunLayerType && !isSpace) {
                    if (isColor ? isEmpty_Rect(glyph->colorRect)
                                : !isRasterized_Glyph_(glyph, hoff)) {
                        cacheSingleGlyph_Font_(runFont, glyphId);
                        glyph = glyphByIndex_Font_(runFont, glyphId);
                        iAssert(isColor ? !isEmpty_Rect(glyph->colorRect)
                                        : isRasterized_Glyph_(glyph, hoff));
                    }
                    SDL_Rect src;
                    if (isColor) {
                        memcpy(&src, &glyph->colorRect, sizeof(SDL_Rect));
                        SDL_RenderCopy(current_Text()->render, tx->colorCache.texture, &src, &dst);
                    }
                    else {
                        if (~d->mode & permanentColorFlag_RunMode) {
                            SDL_SetTextureColorMod(tx->grayscaleCache.texture,
                                                   fgClr.r, fgClr.g, fgClr.b);
                        }
                        memcpy(&src, &glyph->rect[hoff], sizeof(SDL_Rect));
                        SDL_RenderCopy(current_Text()->render, tx->grayscaleCache.texture,
                                       &src, &dst);
                    }
                }
            }
            d->xCursor += xAdvance;
            d->yCursor += yAdvance;
            if (i + 1 < buf->glyphCount) {
                d->xCursor += horizKern_Font_(runFont, glyphId,
                                              buf->glyphInfo[i + 1].codepoint);
            }
            d->xCursorMax = iMax(d->xCursorMax, d->xCursor);
        }
    }
}

static void run_Font_(iRasterFont *d, const iRunArgs *args) {
    const int   mode         = args->mode;
    const iInt2 orig         = args->pos;
    iRect       bounds       = { orig, init_I2(0, d->font.height) };
    float       xCursor      = 0.0f;
    float       yCursor      = 0.0f;
    float       xCursorMax   = 0.0f;
    const iBool isMonospaced = isMonospaced_RasterFont(d);
    iWrapText  *wrap         = args->wrap;
    iFontRun   *fontRun;
    iBool       didFindCachedFontRun = iFalse;
    if (mode & draw_RunMode) {
        const iColor clr = get_Color(args->color);
        SDL_SetTextureColorMod(currentRaster_Text_()->grayscaleCache.texture,
                               clr.r, clr.g, clr.b);
    }
    iAssert(args->text.end >= args->text.start);
    fontRun = makeOrFindCachedFontRun_(
        currentRaster_Text_(),
        &(iFontRunArgs){ args->maxLen,
                         d,
                         args->color,
                         args->baseDir,
                         current_Text()->baseFontId >= 0
                             ? (iRasterFont *) font_Text(current_Text()->baseFontId)
                             : d,
                         current_Text()->baseFgColorId,
                         wrap ? wrap->overrideChar : 0 },
        args->text,
        &didFindCachedFontRun);
    const iAttributedText *attrText    = &fontRun->attrText;
    const size_t           runCount    = size_Array(&attrText->runs);
    const iChar           *logicalText = constData_Array(&attrText->logical);
    if (wrap) {
        wrap->baseDir           = attrText->isBaseRTL ? -1 : +1;
        iAssert(equalRange_Rangecc(wrap->text, args->text));
        wrap->wrapRange_        = args->text;
        wrap->hitAdvance_out    = zero_I2();
        wrap->hitChar_out       = NULL;
        wrap->hitGlyphNormX_out = 0.0f;
    }
    iBool        willAbortDueToWrap = iFalse;
    const size_t textLen            = size_Array(&attrText->logical);
    iRanges      wrapRuns           = { 0, runCount };
    iRangei      wrapPosRange       = { 0, textLen };
    int          wrapResumePos      = textLen;
    size_t       wrapResumeRunIndex = runCount;
    iTextAttrib  attrib             = { .fgColorId = args->color,
                                        .bgColorId = none_ColorId,
                                        .isBaseRTL = attrText->isBaseRTL };
    iTextAttrib  wrapAttrib         = attrib;
    iTextAttrib  lastAttrib         = attrib;
    const int    layoutBound        = (wrap ? wrap->maxWidth : args->layoutBound);
    iBool        isFirst            = iTrue;
    const iBool  checkHitPoint      = wrap && !isEqual_I2(wrap->hitPoint, zero_I2());
    const iBool  checkHitChar       = wrap && wrap->hitChar;
    iBool        wasCharHit         = iFalse;
    size_t       numWrapLines       = 0;
    while (!isEmpty_Range(&wrapRuns)) {
        if (isFirst) {
            isFirst = iFalse;
        }
        else {
            xCursor = 0;
            yCursor += d->font.height;
        }
        float wrapAdvance = 0.0f;
        if (wrap) {
            float  breakAdvance  = -1.0f;
            size_t breakRunIndex = iInvalidPos;
            iAssert(wrapPosRange.end == textLen);
            int safeBreakPos = -1;
            for (size_t runIndex = wrapRuns.start; runIndex < wrapRuns.end; runIndex++) {
                const iAttributedRun *run = constAt_Array(&attrText->runs, runIndex);
                if (run->flags.isLineBreak) {
                    if (checkHitChar &&
                        wrap->hitChar == sourcePtr_AttributedText(attrText, run->logical.start)) {
                        wrap->hitAdvance_out = init_I2(wrapAdvance, yCursor);
                    }
                    wrapPosRange.end   = run->logical.start;
                    wrapResumePos      = run->logical.end;
                    wrapRuns.end       = runIndex;
                    wrapResumeRunIndex = runIndex + 1;
                    break;
                }
                wrapResumeRunIndex = runCount;
                wrapResumePos      = textLen;
                iGlyphBuffer *buf  = at_Array(&fontRun->buffers, runIndex);
                iAssert(run->font == (iAnyFont *) buf->font);
                iChar prevCh[2] = { 0, 0 };
                lastAttrib = run->attrib;
                for (unsigned int ir = 0; ir < buf->glyphCount; ir++) {
                    const int i = (run->attrib.isRTL ? buf->glyphCount - ir - 1 : ir);
                    const hb_glyph_info_t *info    = &buf->glyphInfo[i];
                    const hb_codepoint_t   glyphId = info->codepoint;
                    const int              logPos  = info->cluster;
                    if (logPos < wrapPosRange.start || logPos >= wrapPosRange.end) continue;
                    iRasterFont  *runFont  = (iRasterFont *) run->font;
                    const iGlyph *glyph    = glyphByIndex_Font_(runFont, glyphId);
                    const float   xOffset  = runFont->xScale * buf->glyphPos[i].x_offset;
                    const float   xAdvance = fabsf(runFont->xScale * buf->glyphPos[i].x_advance);
                    const iChar   ch       = logicalText[logPos];
                    const enum iWrapTextMode wrapMode = isCJK_Script(run->flags.script)
                                                            ? anyCharacter_WrapTextMode
                                                            : args->wrap->mode;
                    if (wrapMode == word_WrapTextMode) {
                        if (((prevCh[0] == '-' || prevCh[0] == '/' || prevCh[0] == '\\' ||
                              prevCh[0] == '?' || prevCh[0] == '!' || prevCh[0] == '&' ||
                              prevCh[0] == '+' || prevCh[0] == '_' || prevCh[0] == '@') &&
                             !isPunct_Char(ch)) ||
                            (isAlpha_Char(prevCh[1]) && prevCh[0] == '.' && isAlpha_Char(ch))) {
                            safeBreakPos  = logPos;
                            breakAdvance  = wrapAdvance;
                            breakRunIndex = runIndex;
                        }
                        else if (isSpace_Char(ch)) {
                            safeBreakPos  = logPos;
                            breakAdvance  = wrapAdvance;
                            breakRunIndex = runIndex;
                        }
                        prevCh[1] = prevCh[0];
                        prevCh[0] = ch;
                    }
                    else {
                        safeBreakPos  = logPos;
                        breakAdvance  = wrapAdvance;
                        breakRunIndex = runIndex;
                        wrapAttrib    = run->attrib;
                    }
                    if (ch == '\t') {
                        wrapAdvance = nextTabStop_Font_(d, wrapAdvance) - xAdvance;
                    }
                    if (wrap->maxWidth > 0 &&
                        wrapAdvance + xOffset + glyph->d[0].x + glyph->rect[0].size.x >
                        args->wrap->maxWidth) {
                        if (safeBreakPos > wrapPosRange.start) {
                            wrapPosRange.end = safeBreakPos;
                        }
                        else {
                            if (wrapMode == word_WrapTextMode &&
                                run->logical.start > wrapPosRange.start) {
                                wrapPosRange.end   = run->logical.start;
                                wrapResumePos      = run->logical.start;
                                wrapRuns.end       = runIndex + 1;
                                wrapResumeRunIndex = runIndex;
                                break;
                            }
                            if (logPos == wrapPosRange.start) {
                                wrapPosRange.end = logPos + 1;
                                breakAdvance     = wrapAdvance + xAdvance;
                            }
                            else {
                                wrapPosRange.end = logPos;
                                breakAdvance     = wrapAdvance;
                            }
                            breakRunIndex = runIndex;
                        }
                        wrapResumePos = wrapPosRange.end;
                        if (wrapMode != anyCharacter_WrapTextMode) {
                            while (wrapResumePos < textLen &&
                                   isSpace_Char(logicalText[wrapResumePos])) {
                                wrapResumePos++;
                            }
                        }
                        wrapRuns.end       = breakRunIndex + 1;
                        wrapResumeRunIndex = breakRunIndex;
                        wrapAdvance        = breakAdvance;
                        break;
                    }
                    wrapAdvance += xAdvance;
                    if (i + 1 < buf->glyphCount) {
                        wrapAdvance += horizKern_Font_(buf->font, glyphId,
                                                        buf->glyphInfo[i + 1].codepoint);
                    }
                }
            }
        }
        else {
            for (size_t i = wrapRuns.start; i < wrapRuns.end; i++) {
                wrapAdvance += advance_GlyphBuffer_(buffer_FontRun(fontRun, i), wrapPosRange);
            }
        }
        if (args->justify && !didFindCachedFontRun && layoutBound && !isMonospaced) {
            justify_GlyphBuffer_(at_Array(&fontRun->buffers, wrapRuns.start),
                                 size_Range(&wrapRuns),
                                 wrapPosRange,
                                 &wrapAdvance,
                                 layoutBound,
                                 wrapRuns.start > 0 && wrapRuns.end == runCount);
        }
        if (checkHitPoint || checkHitChar) {
            iAssert(wrap);
            const iBool isHitPointOnThisLine =
                checkHitPoint && wrap->hitPoint.y >= orig.y + yCursor &&
                wrap->hitPoint.y < orig.y + yCursor + d->font.height;
            float hitAdvance = 0.0f;
            for (size_t i = wrapRuns.start; i < wrapRuns.end; i++) {
                const iGlyphBuffer *buf = buffer_FontRun(fontRun, i);
                for (size_t j = 0; j < buf->glyphCount; j++) {
                    const int logPos = buf->glyphInfo[j].cluster;
                    if (logPos < wrapPosRange.start) continue;
                    if (logPos >= wrapPosRange.end) break;
                    const float xAdvance = buf->glyphPos[j].x_advance * buf->font->xScale;
                    if (checkHitChar && !wasCharHit) {
                        const char *sourceLoc = sourcePtr_AttributedText(attrText, logPos);
                        if (sourceLoc <= wrap->hitChar) {
                            wrap->hitAdvance_out = init_I2(hitAdvance, yCursor);
                        }
                        if (sourceLoc >= wrap->hitChar) {
                            wasCharHit = iTrue;
                        }
                    }
                    if (isHitPointOnThisLine) {
                        if (wrap->hitPoint.x >= orig.x + hitAdvance &&
                            wrap->hitPoint.x < orig.x + hitAdvance + xAdvance) {
                            wrap->hitChar_out       = sourcePtr_AttributedText(attrText, logPos);
                            wrap->hitGlyphNormX_out = (wrap->hitPoint.x - wrapAdvance) / xAdvance;
                        }
                    }
                    hitAdvance += xAdvance;
                }
            }
            if (checkHitChar && !wasCharHit) {
                wrap->hitAdvance_out = init_I2(hitAdvance, yCursor);
            }
            if (isHitPointOnThisLine && !wrap->hitChar_out) {
                if (wrap->hitPoint.x < orig.x) {
                    const iGlyphBuffer *buf = buffer_FontRun(fontRun, wrapRuns.start);
                    if (buf->glyphCount > 0) {
                        wrap->hitChar_out = sourcePtr_AttributedText(attrText,
                                                buf->glyphInfo[0].cluster);
                        wrap->hitGlyphNormX_out = 0.0f;
                    }
                }
                else {
                    if (wrapResumePos == textLen) {
                        wrap->hitChar_out = sourcePtr_AttributedText(attrText, wrapResumePos);
                    }
                    else {
                        const char *hit = sourcePtr_AttributedText(attrText,
                                                                   iMax(0, wrapResumePos - 1));
                        while (hit > args->text.start) {
                            if (!isSpace_Char(hit[-1])) break;
                            hit--;
                        }
                        wrap->hitChar_out = hit;
                    }
                    wrap->hitGlyphNormX_out = 0.0f;
                }
            }
        }
        iArray runOrder;
        init_Array(&runOrder, sizeof(size_t));
        {
            size_t oppositeInsertIndex = iInvalidPos;
            for (size_t runIndex = wrapRuns.start; runIndex < wrapRuns.end; runIndex++) {
                const iAttributedRun *run = at_Array(&fontRun->attrText.runs, runIndex);
                if (!attrText->isBaseRTL) {
                    if (run->attrib.isRTL) {
                        if (oppositeInsertIndex == iInvalidPos) {
                            oppositeInsertIndex = size_Array(&runOrder);
                        }
                        insert_Array(&runOrder, oppositeInsertIndex, &runIndex);
                    }
                    else {
                        pushBack_Array(&runOrder, &runIndex);
                        oppositeInsertIndex = iInvalidPos;
                    }
                }
                else {
                    if (!run->attrib.isRTL) {
                        if (oppositeInsertIndex == iInvalidPos) oppositeInsertIndex = 0;
                        insert_Array(&runOrder, oppositeInsertIndex++, &runIndex);
                    }
                    else {
                        pushFront_Array(&runOrder, &runIndex);
                        oppositeInsertIndex = iInvalidPos;
                    }
                }
            }
        }
        iAssert(size_Array(&runOrder) == size_Range(&wrapRuns));
        int   origin         = 0;
        iBool isRightAligned = attrText->isBaseRTL;
        if (isRightAligned && layoutBound > 0) {
            origin = layoutBound - wrapAdvance;
        }
        if (wrap && wrap->wrapFunc &&
            !notify_WrapText(args->wrap,
                             sourcePtr_AttributedText(attrText, wrapResumePos),
                             wrapAttrib, origin, iRound(wrapAdvance))) {
            willAbortDueToWrap = iTrue;
        }
        numWrapLines++;
        if (wrap && wrap->maxLines && numWrapLines == wrap->maxLines) {
            willAbortDueToWrap = iTrue;
        }
        wrapAttrib = lastAttrib;
        iRunLayer layer = {
            .font         = d,
            .mode         = mode,
            .orig         = orig,
            .bounds       = bounds,
            .fontRun      = fontRun,
            .runOrder     = &runOrder,
            .wrapPosRange = wrapPosRange,
            .xCursorMax   = xCursorMax,
            .yCursor      = yCursor,
        };
        for (int layerIndex = 0; layerIndex < 2; layerIndex++) {
            if (~mode & draw_RunMode && layerIndex == foreground_RunLayerType) continue;
            layer.xCursor = origin;
            layer.yCursor = yCursor;
            process_RunLayer_(&layer, layerIndex);
        }
        bounds     = layer.bounds;
        xCursor    = layer.xCursor;
        xCursorMax = layer.xCursorMax;
        yCursor    = layer.yCursor;
        deinit_Array(&runOrder);
        if (willAbortDueToWrap) break;
        wrapRuns.start     = wrapResumeRunIndex;
        wrapRuns.end       = runCount;
        wrapPosRange.start = wrapResumePos;
        wrapPosRange.end   = textLen;
    }
    if (endsWith_Rangecc(args->text, "\n")) {
        xCursor = 0;
        yCursor += d->font.height;
    }
    if (args->metrics_out) {
        args->metrics_out->advance = init_I2(xCursor, yCursor);
        args->metrics_out->bounds  = bounds;
    }
}

#else /* !LAGRANGE_ENABLE_HARFBUZZ */

typedef iRasterFont iFont; /* text_simple.c uses iFont */
#   if defined (LAGRANGE_ENABLE_STB_TRUETYPE)
#       include "text_stb.h" /* provides stbData_FontFile for STB kerning in text_simple.c */
#   endif
#   define run_Font_ runSimple_Font_
#   include "text_simple.c"

#endif /* LAGRANGE_ENABLE_HARFBUZZ */

void run_Font(iBaseFont *font, const iRunArgs *args) {
    run_Font_((iRasterFont *) font, args);
}
