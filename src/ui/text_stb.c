/* Copyright 2020-2022 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "text.h"
#include "color.h"
#include "metrics.h"
#include "resources.h"
#include "window.h"
#include "paint.h"
#include "app.h"

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
#   include <fribidi/fribidi.h>
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

iDeclareType(Font)
iDeclareType(Glyph)
iDeclareTypeConstructionArgs(Glyph, iChar ch)

enum iGlyphFlag {
    rasterized0_GlyphFlag = iBit(1),    /* zero offset */
    rasterized1_GlyphFlag = iBit(2),    /* quarter pixel offset */
    rasterized2_GlyphFlag = iBit(3),    /* half-pixel offset */
    rasterized3_GlyphFlag = iBit(4),    /* three quarters offset */
};

int   enableHalfPixelGlyphs_Text    = iTrue; /* debug setting */
int   enableKerning_Text            = iTrue; /* note: looking up kern pairs is slow */

static int numOffsetSteps_Glyph_    = 4;   /* subpixel offsets for glyphs */
static int rasterizedAll_GlyphFlag_ = 0xf; /* updated with numOffsetSteps_Glyph */

iLocalDef float offsetStep_Glyph_(void) {
    return 1.0f / (float) numOffsetSteps_Glyph_;
}

static int makeRasterizedAll_GlyphFlag_(int n) {
    int flag = rasterized0_GlyphFlag;
    if (n > 1) {
        flag |= rasterized1_GlyphFlag;
    }
    if (n > 2) {
        flag |= rasterized2_GlyphFlag;
    }
    if (n > 3) {
        flag |= rasterized3_GlyphFlag;
    }
    return flag;
}

struct Impl_Glyph {
    iHashNode node;
    int       flags;
    iFont    *font;    /* may come from symbols/emoji */
    float     advance; /* scaled */
    iRect     rect[4]; /* zero and half pixel offset */
    iInt2     d[4];
};

void init_Glyph(iGlyph *d, uint32_t glyphIndex) {
    d->node.key   = glyphIndex;
    d->flags      = 0;
    d->font       = NULL;
    d->advance    = 0.0f;
    iZap(d->rect);
    iZap(d->d);
}

void deinit_Glyph(iGlyph *d) {
    iUnused(d);
}

static uint32_t index_Glyph_(const iGlyph *d) {
    return d->node.key;
}

iLocalDef iBool isRasterized_Glyph_(const iGlyph *d, int hoff) {
    return (d->flags & (rasterized0_GlyphFlag << hoff)) != 0;
}

iLocalDef iBool isFullyRasterized_Glyph_(const iGlyph *d) {
    return (d->flags & rasterizedAll_GlyphFlag_) == rasterizedAll_GlyphFlag_;
}

iLocalDef void setRasterized_Glyph_(iGlyph *d, int hoff) {
    d->flags |= rasterized0_GlyphFlag << hoff;
}

iDefineTypeConstructionArgs(Glyph, (iChar ch), ch)

/*-----------------------------------------------------------------------------------------------*/

static iGlyph *glyph_Font_(iFont *d, iChar ch);

iDeclareType(GlyphTable)

struct Impl_GlyphTable {
    iHash          glyphs; /* key is glyph index in the font */
    /* TODO: `glyphs` does not need to be a Hash.
       We could lazily allocate an array with glyphCount elements instead. */
    uint32_t       indexTable[128 - 32]; /* quick ASCII lookup */
};

static void clearGlyphs_GlyphTable_(iGlyphTable *d) {
    if (d) {
        iForEach(Hash, i, &d->glyphs) {
            delete_Glyph((iGlyph *) i.value);
        }
        clear_Hash(&d->glyphs);
    }
}

static void init_GlyphTable(iGlyphTable *d) {
    init_Hash(&d->glyphs);
    memset(d->indexTable, 0xff, sizeof(d->indexTable));
}

static void deinit_GlyphTable(iGlyphTable *d) {
    clearGlyphs_GlyphTable_(d);
    deinit_Hash(&d->glyphs);
}

iDefineTypeConstruction(GlyphTable)

/*-----------------------------------------------------------------------------------------------*/

struct Impl_Font {
    iBaseFont    font;
    int          baseline;
    int          vertOffset; /* offset due to glyph scaling */
    float        xScale, yScale;
    float        emAdvance;
    iGlyphTable *table;
};

static void init_Font(iFont *d, const iFontSpec *fontSpec, const iFontFile *fontFile,
                      enum iFontSize sizeId, float height) {
    const int scaleType = scaleType_FontSpec(sizeId);
    d->font.spec = fontSpec;
    d->font.file = fontFile;
    d->font.height = (int) (height * fontSpec->heightScale[scaleType]);
    const float glyphScale = fontSpec->glyphScale[scaleType];
    d->xScale = d->yScale = scaleForPixelHeight_FontFile(fontFile, d->font.height) * glyphScale;
    if (isMonospaced_Font(d)) {
        /* It is important that monospaced fonts align 1:1 with the pixel grid so that
           box-drawing characters don't have partially occupied edge pixels, leading to seams
           between adjacent glyphs. */
        const float advance = (float) fontFile->emAdvance * d->xScale;
        if (advance > 4) { /* not too tiny */
            d->xScale *= floorf(advance) / advance;
        }
    }
    d->emAdvance  = fontFile->emAdvance * d->xScale;
    d->baseline   = fontFile->ascent * d->yScale;
    d->vertOffset = d->font.height * (1.0f - glyphScale) / 2 * fontSpec->vertOffsetScale[scaleType];
    d->table = NULL;
}

static void deinit_Font(iFont *d) {
    delete_GlyphTable(d->table);
}

static uint32_t glyphIndex_Font_(iFont *d, iChar ch) {
    /* TODO: Add a small cache of ~5 most recently found indices. */
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

/*----------------------------------------------------------------------------------------------*/

iDeclareType(CacheRow)

struct Impl_CacheRow {
    int   height;
    iInt2 pos;
};

iDeclareType(PrioMapItem)
struct Impl_PrioMapItem {
    int      priority;
    uint32_t fontIndex;
};

static int cmp_PrioMapItem_(const void *a, const void *b) {
    const iPrioMapItem *i = a, *j = b;
    return -iCmp(i->priority, j->priority);
}

iDeclareType(FontRunArgs)
iDeclareType(FontRun)
iDeclareTypeConstructionArgs(FontRun, const iFontRunArgs *args, const iRangecc text, uint32_t crc)

iDeclareType(StbText)
iDeclareTypeConstructionArgs(StbText, SDL_Renderer *render, float documentFontSizeFactor)

struct Impl_StbText {
    iText          base;
    iArray         fonts; /* fonts currently selected for use (incl. all styles/sizes) */
    int            overrideFontId; /* always checked for glyphs first, regardless of which font is used */
    iArray         fontPriorityOrder;
    SDL_Texture *  cache;
    iInt2          cacheSize;
    int            cacheRowAllocStep;
    int            cacheBottom;
    iArray         cacheRows;
    SDL_Palette *  grayscale;
    SDL_Palette *  blackAndWhite; /* unsmoothed glyph palette */
    iBool          missingGlyphs;  /* true if a glyph couldn't be found */
    iChar          missingChars[20]; /* rotating buffer of the latest missing characters */
    iFontRun *     cachedFontRuns[16]; /* recently generated HarfBuzz glyph buffers */
};

iLocalDef iStbText *current_StbText_(void) {
    return (iStbText *) current_Text();
}

iLocalDef iFont *font_Text_(enum iFontId id) {
    iAssert(current_StbText_());
    return at_Array(&current_StbText_()->fonts, id & mask_FontId);
}

static void setupFontVariants_StbText_(iStbText *d, const iFontSpec *spec, int baseId) {
    const float uiSize   = fontSize_UI * (isMobile_Platform() ? 1.1f : 1.0f);
    const float textSize = fontSize_UI * d->base.contentFontSize;
    if (spec->flags & override_FontSpecFlag && d->overrideFontId < 0) {
        /* This is the highest priority override font. */
        d->overrideFontId = baseId;
    }
    iAssert(current_StbText_() == d);
    pushBack_Array(&d->fontPriorityOrder, &(iPrioMapItem){ spec->priority, baseId });
    for (enum iFontStyle style = 0; style < max_FontStyle; style++) {
        for (enum iFontSize sizeId = 0; sizeId < max_FontSize; sizeId++) {
            init_Font(font_Text_(FONT_ID(baseId, style, sizeId)),
                      spec,
                      spec->styles[style],
                      sizeId,
                      (sizeId < contentRegular_FontSize ? uiSize : textSize) *
                          scale_FontSize(sizeId));
        }
    }
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

iLocalDef enum iFontSize sizeId_Text_(const iFont *d) {
    return fontId_Text_(d) % max_FontSize;
}

iLocalDef enum iFontStyle styleId_Text_(const iFont *d) {
    return style_FontId(fontId_Text_(d));
}

static const iFontSpec *tryFindSpec_(enum iPrefsString ps, const char *fallback) {
    const iFontSpec *spec = findSpec_Fonts(cstr_String(&prefs_App()->strings[ps]));
    return spec ? spec : findSpec_Fonts(fallback);
}

static void initFonts_StbText_(iStbText *d) {
    /* The `fonts` array has precomputed scaling factors and other parameters in all sizes
       and styles for each available font. Indices to `fonts` act as font runtime IDs. */
    /* First the mandatory fonts. */
    d->overrideFontId = -1;
    clear_Array(&d->fontPriorityOrder);
    resize_Array(&d->fonts, auxiliary_FontId); /* room for the built-ins */
    setupFontVariants_StbText_(d, tryFindSpec_(uiFont_PrefsString, "default"), default_FontId);
    setupFontVariants_StbText_(d, tryFindSpec_(monospaceFont_PrefsString, "iosevka"), monospace_FontId);
    setupFontVariants_StbText_(d, tryFindSpec_(headingFont_PrefsString, "default"), documentHeading_FontId);
    setupFontVariants_StbText_(d, tryFindSpec_(bodyFont_PrefsString, "default"), documentBody_FontId);
    setupFontVariants_StbText_(d, tryFindSpec_(monospaceDocumentFont_PrefsString, "iosevka-body"), documentMonospace_FontId);
    /* Check if there are auxiliary fonts available and set those up, too. */
    iConstForEach(PtrArray, s, listSpecsByPriority_Fonts()) {
        const iFontSpec *spec = s.ptr;
//        printf("spec '%s': prio=%d\n", cstr_String(&spec->name), spec->priority);
        if (spec->flags & (auxiliary_FontSpecFlag | user_FontSpecFlag)) {
            const int fontId = size_Array(&d->fonts);
            resize_Array(&d->fonts, fontId + maxVariants_Fonts);
            setupFontVariants_StbText_(d, spec, fontId);
        }
    }
    sort_Array(&d->fontPriorityOrder, cmp_PrioMapItem_);
#if !defined (NDEBUG)
    printf("[Text] %zu font variants ready\n", size_Array(&d->fonts));
#endif
    gap_Text = iRound(gap_UI * d->base.contentFontSize);
}

static void deinitFonts_StbText_(iStbText *d) {
    iForEach(Array, i, &d->fonts) {
        deinit_Font(i.value);
    }
    clear_Array(&d->fonts);
}

static int maxGlyphHeight_Text_(const iText *d) {
    /* Huge size is 2 * contentFontSize. */
    return 4 * d->contentFontSize * fontSize_UI;
}

static void initCache_StbText_(iStbText *d) {
    init_Array(&d->cacheRows, sizeof(iCacheRow));
    const int textSize = d->base.contentFontSize * fontSize_UI;
    iAssert(textSize > 0);
    numOffsetSteps_Glyph_   = get_Window()->pixelRatio < 2.0f   ? 4
                              : get_Window()->pixelRatio < 2.5f ? 3
                                                                : 2;
    rasterizedAll_GlyphFlag_ = makeRasterizedAll_GlyphFlag_(numOffsetSteps_Glyph_);
#if !defined(NDEBUG)
    printf("[Text] subpixel offsets: %d\n", numOffsetSteps_Glyph_);
#endif
    const iInt2 cacheDims = init_I2(8 * numOffsetSteps_Glyph_, 40);
    d->cacheSize          = mul_I2(cacheDims, init1_I2(iMax(textSize, fontSize_UI)));
    SDL_RendererInfo renderInfo;
    SDL_GetRendererInfo(d->base.render, &renderInfo);
    if (renderInfo.max_texture_height > 0 && d->cacheSize.y > renderInfo.max_texture_height) {
        d->cacheSize.y = renderInfo.max_texture_height;
        d->cacheSize.x = renderInfo.max_texture_width;
    }
    d->cacheRowAllocStep = iMax(2, textSize / 6);
    /* Allocate initial (empty) rows. These will be assigned actual locations in the cache
       once at least one glyph is stored. */
    for (int h = d->cacheRowAllocStep;
         h <= 5 * textSize + d->cacheRowAllocStep;
         h += d->cacheRowAllocStep) {
        pushBack_Array(&d->cacheRows, &(iCacheRow){ .height = 0 });
    }
    d->cacheBottom = 0;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    d->cache = SDL_CreateTexture(d->base.render,
                                 SDL_PIXELFORMAT_RGBA4444,
                                 SDL_TEXTUREACCESS_STATIC | SDL_TEXTUREACCESS_TARGET,
                                 d->cacheSize.x,
                                 d->cacheSize.y);
    SDL_SetTextureBlendMode(d->cache, SDL_BLENDMODE_BLEND);
}

static void deinitCache_StbText_(iStbText *d) {
    deinit_Array(&d->cacheRows);
    SDL_DestroyTexture(d->cache);
}

void init_StbText(iStbText *d, SDL_Renderer *render, float documentFontSizeFactor) {
    init_Text(&d->base, render, documentFontSizeFactor);
    iText *oldActive = current_Text();
    setCurrent_Text(&d->base);
    init_Array(&d->fonts, sizeof(iFont));
    init_Array(&d->fontPriorityOrder, sizeof(iPrioMapItem));
    d->missingGlyphs   = iFalse;
    iZap(d->missingChars);
    iZap(d->cachedFontRuns);
    /* A grayscale palette for rasterized glyphs. */ {
        SDL_Color colors[256];
        for (int i = 0; i < 256; ++i) {
            /* TODO: On dark backgrounds, applying a gamma curve of some sort might be helpful. */
            colors[i] = (SDL_Color){ 255, 255, 255, 255 * powf(i / 255.0f, 1.0f) + 0.5f };
        }
        d->grayscale = SDL_AllocPalette(256);
        SDL_SetPaletteColors(d->grayscale, colors, 0, 256);
    }
    /* Black-and-white palette for unsmoothed glyphs. */ {
        SDL_Color colors[256];
        for (int i = 0; i < 256; ++i) {
            colors[i] = (SDL_Color){ 255, 255, 255, i < 100 ? 0 : 255 };
        }
        d->blackAndWhite = SDL_AllocPalette(256);
        SDL_SetPaletteColors(d->blackAndWhite, colors, 0, 256);
    }
    initCache_StbText_(d);
    initFonts_StbText_(d);
    setCurrent_Text(oldActive);
}

void deinit_StbText(iStbText *d) {
#if defined (LAGRANGE_ENABLE_HARFBUZZ)
    iForIndices(i, d->cachedFontRuns) {
        delete_FontRun(d->cachedFontRuns[i]);
    }
#endif
    SDL_FreePalette(d->blackAndWhite);
    SDL_FreePalette(d->grayscale);
    deinitFonts_StbText_(d);
    deinitCache_StbText_(d);
    deinit_Array(&d->fontPriorityOrder);
    deinit_Array(&d->fonts);
    deinit_Text(&d->base);
}

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
    SDL_SetTextureAlphaMod(current_StbText_()->cache, iClamp(opacity, 0.0f, 1.0f) * 255 + 0.5f);
}

static void resetCache_StbText_(iStbText *d) {
    deinitCache_StbText_(d);
    iForEach(Array, i, &d->fonts) {
        clearGlyphs_GlyphTable_(((iFont *) i.value)->table);
    }
    initCache_StbText_(d);
}

void resetFonts_Text(iText *d) {
    iText *oldActive = current_Text();
    iStbText *s = (iStbText *) d;
    setCurrent_Text(d); /* some routines rely on the global `activeText_` pointer */
    deinitFonts_StbText_(s);
    deinitCache_StbText_(s);
    initCache_StbText_(s);
    initFonts_StbText_(s);
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

iLocalDef iCacheRow *cacheRow_StbText_(iStbText *d, int height) {
    return at_Array(&d->cacheRows, (height - 1) / d->cacheRowAllocStep);
}

static iInt2 assignCachePos_Text_(iStbText *d, iInt2 size) {
    iCacheRow *cur = cacheRow_StbText_(d, size.y);
    if (cur->height == 0) {
        /* Begin a new row height. */
        cur->height = (1 + (size.y - 1) / d->cacheRowAllocStep) * d->cacheRowAllocStep;
        cur->pos.y = d->cacheBottom;
        d->cacheBottom = cur->pos.y + cur->height;
    }
    iAssert(cur->height >= size.y);
    /* TODO: Automatically enlarge the cache if running out of space?
       Maybe make it paged, but beware of texture swapping too often inside a text string. */
    if (cur->pos.x + size.x > d->cacheSize.x) {
        /* Does not fit on this row, advance to a new location in the cache. */
        cur->pos.y = d->cacheBottom;
        cur->pos.x = 0;
        d->cacheBottom += cur->height;
        iAssert(d->cacheBottom <= d->cacheSize.y);
    }
    const iInt2 assigned = cur->pos;
    cur->pos.x += size.x;
    return assigned;
}

static void allocate_Font_(iFont *d, iGlyph *glyph, int hoff) {
    iRect *glRect = &glyph->rect[hoff];
    int    x0, y0, x1, y1;
    measureGlyph_FontFile(d->font.file, index_Glyph_(glyph), d->xScale, d->yScale,
                          hoff * offsetStep_Glyph_(),
                          &x0, &y0, &x1, &y1);
    glRect->size = init_I2(x1 - x0, y1 - y0);
    /* Determine placement in the glyph cache texture, advancing in rows. */
    glRect->pos    = assignCachePos_Text_(current_StbText_(), glRect->size);
    glyph->d[hoff] = init_I2(x0, y0);
    glyph->d[hoff].y += d->vertOffset;
    if (hoff == 0) { /* hoff>=1 uses same metrics as `glyph` */
        glyph->advance = d->xScale * glyphAdvance_FontFile(d->font.file, index_Glyph_(glyph));
    }
}

iLocalDef iFont *characterFont_Font_(iFont *d, iChar ch, uint32_t *glyphIndex) {
    if (isVariationSelector_Char(ch)) {
        return d;
    }
    const enum iFontStyle styleId = styleId_Text_(d);
    const enum iFontSize  sizeId  = sizeId_Text_(d);
    iFont *overrideFont = NULL;
    if (ch != 0x20 && current_StbText_()->overrideFontId >= 0) {
        /* Override font is checked first. */
        overrideFont = font_Text_(FONT_ID(current_StbText_()->overrideFontId, styleId, sizeId));
        if (overrideFont != d && (*glyphIndex = glyphIndex_Font_(overrideFont, ch)) != 0) {
            return overrideFont;
        }
    }
    /* The font's own version of the glyph. */
    if ((*glyphIndex = glyphIndex_Font_(d, ch)) != 0) {
        return d;
    }
    /* As a fallback, check all other available fonts of this size in priority order. */
    iConstForEach(Array, i, &current_StbText_()->fontPriorityOrder) {
        iFont *font = font_Text_(FONT_ID(((const iPrioMapItem *) i.value)->fontIndex,
                                         styleId, sizeId));
        if (font == d || font == overrideFont) {
            continue; /* already checked this one */
        }
        if ((*glyphIndex = glyphIndex_Font_(font, ch)) != 0) {
#if 0
            printf("using '%s' (pr:%d) for %lc (%x) => %d  [missing in '%s']\n",
                   cstr_String(&font->fontSpec->id),
                   font->fontSpec->priority,
                   (int) ch,
                   ch,
                   glyphIndex_Font_(font, ch),
                   cstr_String(&d->fontSpec->id));
#endif
            return font;
        }
    }
    if (!*glyphIndex) {
        fprintf(stderr, "failed to find %08x (%lc)\n", ch, (int) ch); fflush(stderr);
        iStbText *tx = current_StbText_();
        tx->missingGlyphs = iTrue;
        /* Remember a few of the latest missing characters. */
        iBool gotIt = iFalse;
        for (size_t i = 0; i < iElemCount(tx->missingChars); i++) {
            if (tx->missingChars[i] == ch) {
                gotIt = iTrue;
                break;
            }
        }
        if (!gotIt) {
            memmove(tx->missingChars + 1,
                    tx->missingChars,
                    sizeof(tx->missingChars) - sizeof(tx->missingChars[0]));
            tx->missingChars[0] = ch;
        }
    }
    return d;
}

static iGlyph *glyphByIndex_Font_(iFont *d, uint32_t glyphIndex) {
    if (!d->table) {
        d->table = new_GlyphTable();
    }
    iGlyph* glyph = NULL;
    void *  node = value_Hash(&d->table->glyphs, glyphIndex);
    if (node) {
        glyph = node;
    }
    else {
        iStbText *tx = current_StbText_();
        /* If the cache is running out of space, clear it and we'll recache what's needed currently. */
        if (tx->cacheBottom > tx->cacheSize.y - maxGlyphHeight_Text_(&tx->base)) {
#if !defined (NDEBUG)
            printf("[Text] glyph cache is full, clearing!\n"); fflush(stdout);
#endif
            resetCache_StbText_(tx);
        }
        glyph = new_Glyph(glyphIndex);
        glyph->font = d;
        /* New glyphs are always allocated at least. This reserves a position in the cache
           and updates the glyph metrics. */
        for (int offsetIndex = 0; offsetIndex < numOffsetSteps_Glyph_; offsetIndex++) {
            allocate_Font_(d, glyph, offsetIndex);
        }
        insert_Hash(&d->table->glyphs, &glyph->node);
    }
    return glyph;
}

static iGlyph *glyph_Font_(iFont *d, iChar ch) {
    /* The glyph may actually come from a different font; look up the right font. */
    uint32_t glyphIndex = 0;
    iFont *font = characterFont_Font_(d, ch, &glyphIndex);
    return glyphByIndex_Font_(font, glyphIndex);
}

iBaseFont *characterFont_BaseFont(iBaseFont *d, iChar ch) {
    const iGlyph *glyph = glyph_Font_((iFont *) d, ch);
    if (index_Glyph_(glyph)) {
        return (iBaseFont *) glyph->font;
    }
    return NULL;
}

static iChar nextChar_(const char **chPos, const char *end) {
    if (*chPos == end) {
        return 0;
    }
    iChar ch;
    int len = decodeBytes_MultibyteChar(*chPos, end, &ch);
    if (len <= 0) {
        (*chPos)++; /* skip it */
        return 0;
    }
    (*chPos) += len;
    return ch;
}

/*----------------------------------------------------------------------------------------------*/

iDeclareType(RasterGlyph)

struct Impl_RasterGlyph {
    iGlyph *glyph;
    int     hoff;
    iRect   rect;
};

static void cacheGlyphs_Font_(iFont *d, const uint32_t *glyphIndices, size_t numGlyphIndices) {
    /* TODO: Make this an object so it can be used sequentially without reallocating buffers. */
    SDL_Surface *buf     = NULL;
    const iInt2  bufSize = init_I2(iMin(512, d->font.height * iMin(2 * numGlyphIndices, 20)),
                                   d->font.height * 4 / 3);
    int          bufX    = 0;
    iArray *     rasters = NULL;
    SDL_Texture *oldTarget = NULL;
    iBool        isTargetChanged = iFalse;
    iAssert(isExposed_Window(get_Window()));
    /* We'll flush the buffered rasters periodically until everything is cached. */
    size_t index = 0;
    while (index < numGlyphIndices) {
        for (; index < numGlyphIndices; index++) {
            const uint32_t glyphIndex = glyphIndices[index];
            const int lastCacheBottom = current_StbText_()->cacheBottom;
            iGlyph *glyph = glyphByIndex_Font_(d, glyphIndex);
            if (current_StbText_()->cacheBottom < lastCacheBottom) {
                /* The cache was reset due to running out of space. We need to restart from
                   the beginning! */
                bufX = 0;
                if (rasters) {
                    clear_Array(rasters);
                }
                index = 0;
                break;
            }
            if (!isFullyRasterized_Glyph_(glyph)) {
                /* Need to cache this. */
                if (buf == NULL) {
                    rasters = new_Array(sizeof(iRasterGlyph));
                    buf     = SDL_CreateRGBSurfaceWithFormat(
                                0, bufSize.x, bufSize.y,
                                LAGRANGE_RASTER_DEPTH,
                                LAGRANGE_RASTER_FORMAT);
                    SDL_SetSurfaceBlendMode(buf, SDL_BLENDMODE_NONE);
                    SDL_SetSurfacePalette(buf, glyphPalette_());
                }
                SDL_Surface *surfaces[4] = { NULL, NULL, NULL, NULL };
                for (int si = 0; si < numOffsetSteps_Glyph_; si++) {
                    surfaces[si] = !isRasterized_Glyph_(glyph, si)
                                       ? rasterizeGlyph_Font_(glyph->font,
                                                              index_Glyph_(glyph),
                                                              si * offsetStep_Glyph_())
                                       : NULL;
                }
                iBool outOfSpace = iFalse;
                iForIndices(i, surfaces) {
                    if (surfaces[i]) {
                        const int w = surfaces[i]->w;
                        const int h = surfaces[i]->h;
                        if (bufX + w <= bufSize.x) {
                            SDL_BlitSurface(surfaces[i],
                                            NULL,
                                            buf,
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
                iForIndices(i, surfaces) { /* cleanup */
                    if (surfaces[i]) {
                        if (surfaces[i]->flags & SDL_PREALLOC) {
                            free(surfaces[i]->pixels);
                        }
                        SDL_FreeSurface(surfaces[i]);
                    }
                }
                if (outOfSpace) {
                    /* Redo this glyph. `index` does not get incremented. */
                    break;
                }
            }
        }
        /* Finished or the buffer is full, copy the glyphs to the cache texture. */
        if (!isEmpty_Array(rasters)) {
            SDL_Renderer *render = current_Text()->render;
            SDL_Texture *bufTex = SDL_CreateTextureFromSurface(render, buf);
            SDL_SetTextureBlendMode(bufTex, SDL_BLENDMODE_NONE);
            if (!isTargetChanged) {
                isTargetChanged = iTrue;
                oldTarget = SDL_GetRenderTarget(render);
                SDL_SetRenderTarget(render, current_StbText_()->cache);
            }
//            printf("copying %zu rasters from %p\n", size_Array(rasters), bufTex); fflush(stdout);
            iConstForEach(Array, i, rasters) {
                const iRasterGlyph *rg = i.value;
//                iAssert(isEqual_I2(rg->rect.size, rg->glyph->rect[rg->hoff].size));
                const iRect *glRect = &rg->glyph->rect[rg->hoff];
                SDL_RenderCopy(render,
                               bufTex,
                               (const SDL_Rect *) &rg->rect,
                               (const SDL_Rect *) glRect);
                setRasterized_Glyph_(rg->glyph, rg->hoff);
//                printf(" - %u (hoff %d)\n", index_Glyph_(rg->glyph), rg->hoff);
            }
            SDL_DestroyTexture(bufTex);
            /* Resume with an empty buffer. */
            clear_Array(rasters);
            bufX = 0;
        }
    }
    if (rasters) {
        delete_Array(rasters);
    }
    if (buf) {
        SDL_FreeSurface(buf);
    }
    if (isTargetChanged) {
        SDL_SetRenderTarget(current_Text()->render, oldTarget);
    }
}

iLocalDef void cacheSingleGlyph_Font_(iFont *d, uint32_t glyphIndex) {
    cacheGlyphs_Font_(d, &glyphIndex, 1);
}

static void cacheTextGlyphs_Font_(iFont *d, const iRangecc text) {
    iArray glyphIndices;
    init_Array(&glyphIndices, sizeof(uint32_t));
    iAttributedText attrText;
    init_AttributedText(&attrText, text, 0, d, none_ColorId, 0, d, none_ColorId, 0);
    /* We use AttributedText here so the font lookup matches the behavior during text drawing --
       glyphs may be selected from a font that's different than `d`. */
    const iChar *logicalText = constData_Array(&attrText.logical);
    iConstForEach(Array, i, &attrText.runs) {
        const iAttributedRun *run = i.value;
        if (run->flags.isLineBreak) {
            continue;
        }
        for (int pos = run->logical.start; pos < run->logical.end; pos++) {
            const iChar ch = logicalText[pos];
            if (!isSpace_Char(ch) && !isControl_Char(ch)) {
                /* TODO: Use `run->font`; the glyph may be selected from a different font. */
                const uint32_t glyphIndex = glyphIndex_Font_(d, ch);
                if (glyphIndex) {
                    pushBack_Array(&glyphIndices, &glyphIndex);
                }
            }
        }
    }
    deinit_AttributedText(&attrText);
    /* TODO: Cache glyphs from ALL the fonts we encountered above. */
    cacheGlyphs_Font_(d, constData_Array(&glyphIndices), size_Array(&glyphIndices));
    deinit_Array(&glyphIndices);
}

void cache_Text(int fontId, iRangecc text) {
    cacheTextGlyphs_Font_(font_Text_(fontId), text);
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

static float nextTabStop_Font_(const iFont *d, float x) {
    const float stop = prefs_App()->tabWidth * d->emAdvance;
    return floorf(x / stop) * stop + stop;
}

#if defined (LAGRANGE_ENABLE_HARFBUZZ)

iDeclareType(GlyphBuffer)

struct Impl_GlyphBuffer {
    hb_buffer_t *        hb;
    iFont *              font;
    const iChar *        logicalText;
    hb_glyph_info_t *    glyphInfo;
    hb_glyph_position_t *glyphPos;
    unsigned int         glyphCount;
    hb_script_t          script;
};

static void init_GlyphBuffer_(iGlyphBuffer *d, iFont *font, const iChar *logicalText) {
    d->hb          = hb_buffer_create();
    d->font        = font;
    d->logicalText = logicalText;
    d->glyphInfo   = NULL;
    d->glyphPos    = NULL;
    d->glyphCount  = 0;
    d->script      = 0;
}

static void deinit_GlyphBuffer_(iGlyphBuffer *d) {
    hb_buffer_destroy(d->hb);
}

static void shape_GlyphBuffer_(iGlyphBuffer *d) {
    if (!d->glyphInfo) {
        hb_shape(d->font->font.file->hbFont, d->hb, NULL, 0);
        d->glyphInfo = hb_buffer_get_glyph_infos(d->hb, &d->glyphCount);
        d->glyphPos  = hb_buffer_get_glyph_positions(d->hb, &d->glyphCount);
    }
}

static float advance_GlyphBuffer_(const iGlyphBuffer *d, iRangei wrapPosRange) {
    float x = 0.0f;
    for (unsigned int i = 0; i < d->glyphCount; i++) {
        const int logPos = d->glyphInfo[i].cluster;
        if (logPos < wrapPosRange.start || logPos >= wrapPosRange.end) {
            continue;
        }
        x += d->font->xScale * d->glyphPos[i].x_advance;
        if (d->logicalText[logPos] == '\t') {
            x = nextTabStop_Font_(d->font, x);
        }
        if (i + 1 < d->glyphCount) {
            x += horizKern_Font_(d->font,
                                 d->glyphInfo[i].codepoint,
                                 d->glyphInfo[i + 1].codepoint);
        }
    }
    return x;
}

static void evenMonospaceAdvances_GlyphBuffer_(iGlyphBuffer *d, iFont *baseFont) {
    const float monoAdvance = baseFont->emAdvance;
    for (unsigned int i = 0; i < d->glyphCount; ++i) {
        const hb_glyph_info_t *info = d->glyphInfo + i;
        if (d->glyphPos[i].x_advance > 0 && d->font != baseFont) {
            const iChar ch = d->logicalText[info->cluster];
            if (isPictograph_Char(ch) || isEmoji_Char(ch)) {
                const float dw = d->font->xScale * d->glyphPos[i].x_advance - (isEmoji_Char(ch) ? 2 : 1) * monoAdvance;
                d->glyphPos[i].x_offset  -= dw / 2 / d->font->xScale - 1;
                d->glyphPos[i].x_advance -= dw     / d->font->xScale - 1;
            }
        }
    }
}

static void alignOtherFontsVertically_GlyphBuffer_(iGlyphBuffer *d, iFont *baseFont) {
    int offset = 0;
    if (d->font->font.height > baseFont->font.height) {
        /* Doesn't fit on the baseline, so move it up. */
        offset = (d->font->font.height - baseFont->font.height) / 2;
        for (unsigned int i = 0; i < d->glyphCount; ++i) {
            d->glyphPos[i].y_offset += offset / d->font->yScale;
        }
    }
}

iLocalDef float justificationWeight_(iChar c) {
    if (c == '.' || c == '!' || c == '?' ||c == ';') {
        return 2.0f;
    }
    /*
    if (c == ',' || c == ':') {
        return 1.0f;
    }
    */
    return 1.0f;
}

static void justify_GlyphBuffer_(iGlyphBuffer *buffers, size_t numBuffers,
                                 iRangei wrapPosRange,
                                 float *wrapAdvance,
                                 int available, iBool isLast) {
    iGlyphBuffer *begin             = buffers;
    iGlyphBuffer *end               = buffers + numBuffers;
    float         outerSpace        = available - *wrapAdvance;
    float         totalInnerSpace   = 0.0f;
    float         numSpaces         = 0;
    int           numAdvancing      = 0;
    const float   maxSpaceExpansion = 0.14f;
    if (isLast || outerSpace <= 0) {
        return;
    }
    /* TODO: This could use a utility that handles the `wrapPosRange` character span inside
       a span of runs. */
#define CHECK_LOGPOS() \
    if (logPos < wrapPosRange.start) continue; \
    if (logPos >= wrapPosRange.end) break
    /* Find out if there are spaces to expand. */
    for (iGlyphBuffer *buf = begin; buf != end; buf++) {
        for (size_t i = 0; i < buf->glyphCount; i++) {
            hb_glyph_info_t     *info   = &buf->glyphInfo[i];
            hb_glyph_position_t *pos    = &buf->glyphPos[i];
            const int            logPos = info->cluster;
            CHECK_LOGPOS();
            if (pos->x_advance > 0) {
                numAdvancing++;
            }
            if (buf->logicalText[logPos] == 0x20) {
                totalInnerSpace += pos->x_advance * buf->font->xScale;
                float weight = justificationWeight_(buf->logicalText[iMax(0, logPos - 1)]);
                numSpaces += weight;
            }
        }
    }
    if (numSpaces >= 2 && totalInnerSpace > 0) {
        outerSpace = iMin(outerSpace, *wrapAdvance * maxSpaceExpansion);
        float adv = 0.0f;
        for (iGlyphBuffer *buf = begin; buf != end; buf++) {
            const float xScale = buf->font->xScale;
            for (size_t i = 0; i < buf->glyphCount; i++) {
                hb_glyph_info_t     *info   = &buf->glyphInfo[i];
                hb_glyph_position_t *pos    = &buf->glyphPos[i];
                const int            logPos = info->cluster;
                CHECK_LOGPOS();
                if (buf->logicalText[logPos] == 0x20) {
                    float weight = justificationWeight_(buf->logicalText[iMax(0, logPos - 1)]);
                    pos->x_advance =
                        (weight * (totalInnerSpace + outerSpace) / numSpaces) / xScale;
                }
                adv += pos->x_advance * xScale;
            }
        }
        *wrapAdvance = adv;
    }
    /* Finally expand all glyphs a little, if we must. */
    if (numAdvancing > 1 && *wrapAdvance < available - 1.0f) {
        float expandable = *wrapAdvance;
        float outerSpace = available - expandable;
        for (iGlyphBuffer *buf = begin; buf != end; buf++) {
            if (buf->script) continue;
            const float xScale = buf->font->xScale;
            for (size_t i = 0; i < buf->glyphCount; i++) {
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
}

struct Impl_FontRunArgs {
    size_t maxLen;
    iFont *font;
    int    color;
    int    baseDir;
    iFont *baseFont;
    int    baseFgColorId;
    iChar  overrideChar;
};

iLocalDef iBool equal_FontRunArgs(const iFontRunArgs *a, const iFontRunArgs *b) {
    return memcmp(a, b, sizeof(iFontRunArgs)) == 0;
}

struct Impl_FontRun {
    uint32_t        textCrc32;
    iFontRunArgs    args;
    iAttributedText attrText;
    iArray          buffers; /* GlyphBuffers */
};

#if defined (LAGRANGE_ENABLE_HARFBUZZ)
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
#endif

void init_FontRun(iFontRun *d, const iFontRunArgs *args, const iRangecc text, uint32_t crc) {
    d->textCrc32 = crc;
    d->args = *args;
    /* Split the text into a number of attributed runs that specify exactly which
       font is used and other attributes such as color. (HarfBuzz shaping is done
       with one specific font.) */
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
    /* Prepare the HarfBuzz buffers. */
    iConstForEach(Array, i, &d->attrText.runs) {
        const iAttributedRun *run = i.value;
        iGlyphBuffer *buf = at_Array(&d->buffers, index_ArrayConstIterator(&i));
        init_GlyphBuffer_(buf, (iFont *) run->font, logicalText);
        /* Insert the text in visual order (LTR) in the HarfBuzz buffer for shaping.
           First we need to map the logical run to the corresponding visual run. */
        int v[2] = { logToVis[run->logical.start], logToVis[run->logical.end - 1] };
        if (v[0] > v[1]) {
            iSwap(int, v[0], v[1]); /* always LTR */
        }
        for (int vis = v[0]; vis <= v[1]; vis++) {
            hb_buffer_add(buf->hb, visualText[vis], visToLog[vis]);
        }
        hb_buffer_set_content_type(buf->hb, HB_BUFFER_CONTENT_TYPE_UNICODE);
        hb_buffer_set_direction(buf->hb, HB_DIRECTION_LTR); /* visual */
        const hb_script_t script = hbScripts_[run->flags.script];
        if (script) {
            buf->script = script;
            hb_buffer_set_script(buf->hb, script);
        }
        shape_GlyphBuffer_(buf); /* this may take a little while */
    }
    if (isMonospaced_Font(args->font)) {
        /* Fit borrowed glyphs into the expected monospacing. */
        for (size_t runIndex = 0; runIndex < runCount; runIndex++) {
            evenMonospaceAdvances_GlyphBuffer_(at_Array(&d->buffers, runIndex), args->font);
        }
    }
    for (size_t runIndex = 0; runIndex < runCount; runIndex++) {
        alignOtherFontsVertically_GlyphBuffer_(at_Array(&d->buffers, runIndex), args->font);
    }
}

void deinit_FontRun(iFontRun *d) {
    iForEach(Array, b, &d->buffers) {
        deinit_GlyphBuffer_(b.value);
    }
    deinit_Array(&d->buffers);
    deinit_AttributedText(&d->attrText);
}

iLocalDef const iGlyphBuffer *buffer_FontRun(const iFontRun *d, size_t pos) {
    return constAt_Array(&d->buffers, pos);
}

iDefineTypeConstructionArgs(FontRun,
                            (const iFontRunArgs *args, const iRangecc text, uint32_t crc),
                            args, text, crc)

iDeclareType(RunLayer)

struct Impl_RunLayer {
    iFont          *font;
    int             mode;
    iInt2           orig;
    iRect           bounds;
    const iFontRun *fontRun;
    const iArray   *runOrder;
    iRangei         wrapPosRange;
    float           xCursor;
    float           yCursor;
    float           xCursorMax;
};

enum iRunLayerType {
    background_RunLayerType = 0,
    foreground_RunLayerType = 1,
};

void process_RunLayer_(iRunLayer *d, int layerIndex) {
    const iAttributedText *attrText    = &d->fontRun->attrText;
    const iArray          *buffers     = &d->fontRun->buffers;
    const iChar           *logicalText = constData_Array(&attrText->logical);
    /* TODO: Shouldn't the hit tests be done here? */
    for (size_t logRunIndex = 0; logRunIndex < size_Array(d->runOrder); logRunIndex++) {
        const size_t runIndex = constValue_Array(d->runOrder, logRunIndex, size_t);
        const iAttributedRun *run = constAt_Array(&attrText->runs, runIndex);
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
        /* Process all the glyphs. */
        for (unsigned int i = 0; i < buf->glyphCount; i++) {
            const hb_glyph_info_t *info    = &buf->glyphInfo[i];
            const hb_codepoint_t   glyphId = info->codepoint;
            const int              logPos  = info->cluster;
            if (logPos < d->wrapPosRange.start || logPos >= d->wrapPosRange.end) {
                continue; /* can't break because of RTL (?) */
            }
            iFont        *runFont  = (iFont *) run->font;
            const float   xOffset  = runFont->xScale * buf->glyphPos[i].x_offset;
            float         yOffset  = runFont->yScale * buf->glyphPos[i].y_offset;
            const float   xAdvance = runFont->xScale * buf->glyphPos[i].x_advance;
            const float   yAdvance = runFont->yScale * buf->glyphPos[i].y_advance;
            const iGlyph *glyph    = glyphByIndex_Font_(runFont, glyphId);
            const iChar   ch       = logicalText[logPos];
            if (ch == '\t') {
#if 0
                if (mode & draw_RunMode) {
                    /* Tab indicator. */
                    iColor tabColor = get_Color(uiTextAction_ColorId);
                    SDL_SetRenderDrawColor(activeText_->render, tabColor.r, tabColor.g, tabColor.b, 255);
                    const int pad = d->height / 6;
                    SDL_RenderFillRect(activeText_->render, &(SDL_Rect){
                        orig.x + xCursor,
                        orig.y + yCursor + d->height / 2 - pad / 2,
                        pad,
                        pad
                    });
                }
#endif
                d->xCursor = nextTabStop_Font_(d->font, d->xCursor) - xAdvance;
            }
            const float xf = d->xCursor + xOffset;
            float subpixel = xf - (int) xf;
            if (subpixel < 0.0f) {
                subpixel = 1.0f + subpixel;
            }
            const int hoff = enableHalfPixelGlyphs_Text ? (int) (subpixel / offsetStep_Glyph_()) : 0;
            if (ch == 0x3001 || ch == 0x3002) {
                /* Vertical misalignment?? */
                if (yOffset == 0.0f) {
                    /* Move down to baseline. Why doesn't HarfBuzz do this? */
                    yOffset = glyph->d[hoff].y + glyph->rect[hoff].size.y + glyph->d[hoff].y / 4;
                }
            }
            /* Output position for the glyph. */
            SDL_Rect dst = { d->orig.x + d->xCursor + xOffset + glyph->d[hoff].x,
                             d->orig.y + d->yCursor - yOffset + glyph->font->baseline + glyph->d[hoff].y,
                             glyph->rect[hoff].size.x,
                             glyph->rect[hoff].size.y };
            /* Align baselines of different fonts. */
            if (run->font != attrText->baseFont &&
                ~run->font->spec->flags & auxiliary_FontSpecFlag) {
                const int bl1 = ((iFont *) attrText->baseFont)->baseline +
                                ((iFont *) attrText->baseFont)->vertOffset;
                const int bl2 = runFont->baseline + runFont->vertOffset;
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
                    d->bounds.size.y = iMax(d->bounds.size.y, d->yCursor + glyph->font->font.height);
                }
            }
            const iBool isSpace = (logicalText[logPos] == 0x20);
            if (d->mode & draw_RunMode && (isBgFilled || !isSpace)) {
                dst.x += origin_Paint.x;
                dst.y += origin_Paint.y;
                if (layerIndex == background_RunLayerType && isBgFilled) {
                    /* TODO: Backgrounds of all glyphs should be cleared before drawing anything else. */
                    if (bgClr.a) {
                        SDL_SetRenderDrawColor(current_Text()->render, bgClr.r, bgClr.g, bgClr.b, 255);
                        const SDL_Rect bgRect = {
                            origin_Paint.x + d->orig.x + d->xCursor,
                            origin_Paint.y + d->orig.y + d->yCursor,
                            (int) ceilf(subpixel + xAdvance),
                            d->font->font.height,
                        };
                        SDL_RenderFillRect(current_Text()->render, &bgRect);
                    }
                    else if (d->mode & fillBackground_RunMode) {
                        /* Alpha blending looks much better if the RGB components don't change
                           in the partially transparent pixels. */
                        SDL_SetRenderDrawColor(current_Text()->render, fgClr.r, fgClr.g, fgClr.b, 0);
                        SDL_RenderFillRect(current_Text()->render, &dst);
                    }
                }
                if (layerIndex == foreground_RunLayerType && !isSpace) {
                    /* Draw the glyph. */
                    if (!isRasterized_Glyph_(glyph, hoff)) {
                        cacheSingleGlyph_Font_(runFont, glyphId); /* may cause cache reset */
                        glyph = glyphByIndex_Font_(runFont, glyphId);
                        iAssert(isRasterized_Glyph_(glyph, hoff));
                    }
                    if (~d->mode & permanentColorFlag_RunMode) {
                        SDL_SetTextureColorMod(current_StbText_()->cache, fgClr.r, fgClr.g, fgClr.b);
                    }
                    SDL_Rect src;
                    memcpy(&src, &glyph->rect[hoff], sizeof(SDL_Rect));
                    SDL_RenderCopy(current_Text()->render, current_StbText_()->cache, &src, &dst);
                }
#if 0
                /* Show spaces and direction. */
                if (isSpace) {
                    const iColor debug = get_Color(run->flags.isRTL ? yellow_ColorId : red_ColorId);
                    SDL_SetRenderDrawColor(text_.render, debug.r, debug.g, debug.b, 255);
                    dst.w = xAdvance;
                    dst.h = d->height / 2;
                    dst.y -= d->height / 2;
                    SDL_RenderFillRect(text_.render, &dst);
                }
#endif
            }
            d->xCursor += xAdvance;
            d->yCursor += yAdvance;
            /* Additional kerning tweak. It would be better to use HarfBuzz font callbacks,
               but they don't seem to get called? */
            if (i + 1 < buf->glyphCount) {
                d->xCursor += horizKern_Font_(runFont, glyphId, buf->glyphInfo[i + 1].codepoint);
            }
            d->xCursorMax = iMax(d->xCursorMax, d->xCursor);
        }
    }
}

static unsigned fontRunCacheHits_  = 0;
static unsigned fontRunCacheTotal_ = 0;

static iFontRun *makeOrFindCachedFontRun_StbText_(iStbText *d, const iFontRunArgs *runArgs,
                                                  const iRangecc text, iBool *wasFound) {
    fontRunCacheTotal_++;
#if 0
    if (fontRunCacheTotal_ % 100 == 0) {
        printf("FONT RUN CACHE: %d/%d rate:%.1f%%\n",
               fontRunCacheHits_,
               fontRunCacheTotal_,
               (float) fontRunCacheHits_ / (float) fontRunCacheTotal_ * 100);
        fflush(stdout);
    }
#endif
    const uint32_t crc = iCrc32(text.start, size_Range(&text));
    iForIndices(i, d->cachedFontRuns) {
        if (d->cachedFontRuns[i] && d->cachedFontRuns[i]->textCrc32 == crc &&
            equal_FontRunArgs(runArgs, &d->cachedFontRuns[i]->args)) {
            d->cachedFontRuns[i]->attrText.source = text;
            fontRunCacheHits_++;
            *wasFound = iTrue;
            return d->cachedFontRuns[i];
        }
    }
    *wasFound = iFalse;
    delete_FontRun(d->cachedFontRuns[iElemCount(d->cachedFontRuns) - 1]);
    memmove(d->cachedFontRuns + 1,
            d->cachedFontRuns,
            sizeof(d->cachedFontRuns) - sizeof(d->cachedFontRuns[0]));
    d->cachedFontRuns[0] = new_FontRun(runArgs, text, crc);
    return d->cachedFontRuns[0];
}

static void run_Font_(iFont *d, const iRunArgs *args) {
    const int   mode         = args->mode;
    const iInt2 orig         = args->pos;
    iRect       bounds       = { orig, init_I2(0, d->font.height) };
    float       xCursor      = 0.0f;
    float       yCursor      = 0.0f;
    float       xCursorMax   = 0.0f;
    const iBool isMonospaced = isMonospaced_Font(d);
    iWrapText  *wrap         = args->wrap;
    iFontRun   *fontRun;
    iBool       didFindCachedFontRun = iFalse;
    /* Set the default text foreground color. */
    if (mode & draw_RunMode) {
        const iColor clr = get_Color(args->color);
        SDL_SetTextureColorMod(current_StbText_()->cache, clr.r, clr.g, clr.b);
    }
    iAssert(args->text.end >= args->text.start);
    /* We keep a small cache of recently shaped runs because preparing these can be expensive.
       Quite frequently the same text is quickly re-drawn and/or measured (e.g., InputWidget). */
    fontRun = makeOrFindCachedFontRun_StbText_(
        current_StbText_(),
        &(iFontRunArgs){ args->maxLen,
                         d,
                         args->color,
                         args->baseDir,
                         current_Text()->baseFontId >= 0 ? font_Text_(current_Text()->baseFontId) : d,
                         current_Text()->baseFgColorId,
                         wrap ? wrap->overrideChar : 0 },
        args->text,
        &didFindCachedFontRun);
    const iAttributedText *attrText    = &fontRun->attrText;
    const size_t           runCount    = size_Array(&attrText->runs);
    const iChar           *logicalText = constData_Array(&attrText->logical);
    if (wrap) {
        wrap->baseDir = attrText->isBaseRTL ? -1 : +1;
        /* TODO: Duplicated args? */
        iAssert(equalRange_Rangecc(wrap->text, args->text));
        /* Initialize the wrap range. */
        wrap->wrapRange_        = args->text;
        wrap->hitAdvance_out    = zero_I2();
        wrap->hitChar_out       = NULL;
        wrap->hitGlyphNormX_out = 0.0f;
    }
    iBool        willAbortDueToWrap = iFalse;
    const size_t textLen            = size_Array(&attrText->logical);
    iRanges      wrapRuns           = { 0, runCount };
    iRangei      wrapPosRange       = { 0, textLen };
    int          wrapResumePos      = textLen;  /* logical position where next line resumes */
    size_t       wrapResumeRunIndex = runCount; /* index of run where next line resumes */
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
        /* First we need to figure out how much text fits on the current line. */
        if (wrap) {
            float breakAdvance = -1.0f;
            size_t breakRunIndex = iInvalidPos;
            iAssert(wrapPosRange.end == textLen);
            /* Determine ends of wrapRuns and wrapVisRange. */
            int safeBreakPos = -1;
            for (size_t runIndex = wrapRuns.start; runIndex < wrapRuns.end; runIndex++) {
                const iAttributedRun *run = constAt_Array(&attrText->runs, runIndex);
                /* Update the attributes. */
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
                iGlyphBuffer *buf = at_Array(&fontRun->buffers, runIndex);
                iAssert(run->font == (iAnyFont *) buf->font);
                iChar prevCh[2] = { 0, 0 };
                lastAttrib = run->attrib;
//                printf("checking run %zu...\n", runIndex);
                for (unsigned int ir = 0; ir < buf->glyphCount; ir++) {
                    const int i = (run->attrib.isRTL ? buf->glyphCount - ir - 1 : ir);
                    const hb_glyph_info_t *info    = &buf->glyphInfo[i];
                    const hb_codepoint_t   glyphId = info->codepoint;
                    const int              logPos  = info->cluster;
                    if (logPos < wrapPosRange.start || logPos >= wrapPosRange.end) {
                        continue;
                    }
                    iFont *       runFont    = (iFont *) run->font;
                    const iGlyph *glyph      = glyphByIndex_Font_(runFont, glyphId);
//                    const int     glyphFlags = hb_glyph_info_get_glyph_flags(info);
                    const float   xOffset    = runFont->xScale * buf->glyphPos[i].x_offset;
                    const float   xAdvance   = runFont->xScale * buf->glyphPos[i].x_advance;
                    const iChar   ch         = logicalText[logPos];
                    const enum iWrapTextMode wrapMode = isCJK_Script(run->flags.script)
                                                              ? anyCharacter_WrapTextMode
                                                              : args->wrap->mode;
                    iAssert(xAdvance >= 0);
                    if (wrapMode == word_WrapTextMode) {
                        /* When word-wrapping, only consider certain places breakable. */
                        if (((prevCh[0] == '-' || prevCh[0] == '/' || prevCh[0] == '\\' || prevCh[0] == '?' ||
                             prevCh[0] == '!' || prevCh[0] == '&' || prevCh[0] == '+' || prevCh[0] == '_' ||
                             prevCh[0] == '@') &&
                             !isPunct_Char(ch)) ||
                            (isAlpha_Char(prevCh[1]) && prevCh[0] == '.' && isAlpha_Char(ch))) {
                            safeBreakPos = logPos;
                            breakAdvance = wrapAdvance;
                            breakRunIndex = runIndex;
//                            printf("sbp:%d breakAdv_A:%f\n", safeBreakPos, breakAdvance);
    //                        isSoftHyphenBreak = iFalse;
                        }
                        else if (isSpace_Char(ch)) {
                            safeBreakPos = logPos;
                            breakAdvance = wrapAdvance;
                            breakRunIndex = runIndex;
//                            printf("sbp:%d breakAdv_B:%f\n", safeBreakPos, breakAdvance);
    //                        isSoftHyphenBreak = iFalse;
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
                    /* Out of room? */
                    if (wrap->maxWidth > 0 &&
                        wrapAdvance + xOffset + glyph->d[0].x + glyph->rect[0].size.x >
                        args->wrap->maxWidth) {
//                        printf("out of room at lp:%d! safeBreakPos:%d (idx:%zu) breakAdv:%f\n",
//                               logPos, safeBreakPos,
//                               breakRunIndex, breakAdvance);
                        if (safeBreakPos >= 0) {
                            wrapPosRange.end = safeBreakPos;
                        }
                        else {
                            if (wrapMode == word_WrapTextMode && run->logical.start > wrapPosRange.start) {
                                /* Don't have a word break position, so the whole run needs
                                   to be cut. */
                                wrapPosRange.end = run->logical.start;
                                wrapResumePos = run->logical.start;
                                wrapRuns.end = runIndex + 1;
                                wrapResumeRunIndex = runIndex;
                                break;
                            }
                            wrapPosRange.end = logPos;
                            breakAdvance     = wrapAdvance;
                            breakRunIndex    = runIndex;
                        }
                        wrapResumePos = wrapPosRange.end;
                        if (wrapMode != anyCharacter_WrapTextMode) {
                            while (wrapResumePos < textLen && isSpace_Char(logicalText[wrapResumePos])) {
                                wrapResumePos++; /* skip space */
                            }
                        }
                        wrapRuns.end       = breakRunIndex + 1; /* still includes this run */
                        wrapResumeRunIndex = breakRunIndex;     /* ...but continue from the same one */
//                        printf("-> wrapAdv:%f (breakAdv:%f)\n", wrapAdvance, breakAdvance);
                        wrapAdvance        = breakAdvance;
//                        printf("wrapResumePos:%d\n", wrapResumePos);
                        break;
                    }
                    wrapAdvance += xAdvance;
                    /* Additional kerning tweak. It would be better to use HarfBuzz font callbacks,
                       but they don't seem to get called? */
                    if (i + 1 < buf->glyphCount) {
                        wrapAdvance += horizKern_Font_(buf->font,
                                                       glyphId,
                                                       buf->glyphInfo[i + 1].codepoint);
                    }
                }
//                printf("...finished checking run %zu\n", runIndex);
            }
        }
        else {
            /* Not wrapped so everything fits! Calculate total advance without wrapping. */
            for (size_t i = wrapRuns.start; i < wrapRuns.end; i++) {
                wrapAdvance += advance_GlyphBuffer_(buffer_FontRun(fontRun, i), wrapPosRange);
            }
        }
        /* Justification. */
        if (args->justify && !didFindCachedFontRun && layoutBound && !isMonospaced) {
            /* NOTE: May modify a cached FontRun! */
            justify_GlyphBuffer_(at_Array(&fontRun->buffers, wrapRuns.start),
                                 size_Range(&wrapRuns),
                                 wrapPosRange,
                                 &wrapAdvance,
                                 layoutBound,
                                 wrapRuns.start > 0 && wrapRuns.end == runCount /* last wrap? */);
        }
        /* Hit tests. */
        if (checkHitPoint || checkHitChar) {
            iAssert(wrap);
            const iBool isHitPointOnThisLine = (checkHitPoint && wrap->hitPoint.y >= orig.y + yCursor &&
                                                wrap->hitPoint.y < orig.y + yCursor + d->font.height);
            float hitAdvance = 0.0f;
            for (size_t i = wrapRuns.start; i < wrapRuns.end; i++) {
                const iGlyphBuffer *buf = buffer_FontRun(fontRun, i);
                for (size_t j = 0; j < buf->glyphCount; j++) {
                    const int logPos = buf->glyphInfo[j].cluster;
                    CHECK_LOGPOS();
                    const float xAdvance = buf->glyphPos[j].x_advance * buf->font->xScale;
                    if (checkHitChar && !wasCharHit) {
                        const char *sourceLoc = sourcePtr_AttributedText(attrText, logPos);
                        if (sourceLoc <= wrap->hitChar) {
                            wrap->hitAdvance_out = init_I2(hitAdvance, yCursor);
                        }
                        if (sourceLoc >= wrap->hitChar) {
                            wasCharHit = iTrue; /* variation selectors etc. have matching cluster */
                        }
                    }
                    if (isHitPointOnThisLine) {
                        if (wrap->hitPoint.x >= orig.x + hitAdvance &&
                            wrap->hitPoint.x < orig.x + hitAdvance + xAdvance) {
                            wrap->hitChar_out = sourcePtr_AttributedText(attrText, logPos);
                            wrap->hitGlyphNormX_out = (wrap->hitPoint.x - wrapAdvance) / xAdvance;
                        }
                    }
                    hitAdvance += xAdvance;
                }
            }
            if (checkHitChar && !wasCharHit) {
                wrap->hitAdvance_out = init_I2(hitAdvance, yCursor); /* last end of line */
            }
            if (isHitPointOnThisLine && !wrap->hitChar_out) {
                /* Check if the hit point is on the left side of this line. */
                if (wrap->hitPoint.x < orig.x) {
                    const iGlyphBuffer *buf = buffer_FontRun(fontRun, wrapRuns.start);
                    if (buf->glyphCount > 0) {
                        wrap->hitChar_out = sourcePtr_AttributedText(attrText, buf->glyphInfo[0].cluster);
                        wrap->hitGlyphNormX_out = 0.0f;
                    }
                }
                /* Maybe on the right side? */
                else {
                    if (wrapResumePos == textLen) {
                        wrap->hitChar_out = sourcePtr_AttributedText(attrText, wrapResumePos);
                    }
                    else {
                        const char *hit = sourcePtr_AttributedText(attrText, iMax(0, wrapResumePos - 1));
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
        /* Reorder the run indices according to text direction. */ {
            size_t oppositeInsertIndex = iInvalidPos;
            for (size_t runIndex = wrapRuns.start; runIndex < wrapRuns.end; runIndex++) {
                const iAttributedRun *run = at_Array(&fontRun->attrText.runs, runIndex);
                if (!attrText->isBaseRTL) { /* left-to-right */
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
                else { /* right-to-left */
                    if (!run->attrib.isRTL) {
                        if (oppositeInsertIndex == iInvalidPos) {
                            oppositeInsertIndex = 0;
                        }
                        insert_Array(&runOrder, oppositeInsertIndex++, &runIndex);
                    }
                    else {
                        pushFront_Array(&runOrder, &runIndex);
                        oppositeInsertIndex = iInvalidPos;
                    }
                }
            }
#if 0
            printf("Run order: ");
            iConstForEach(Array, ro, &runOrder) {
                const size_t *idx = ro.value;
                printf("%zu {%s}\n", *idx,
                       cstr_Rangecc(sourceRange_AttributedText_(&attrText,                                                                   ((const iAttributedRun *) at_Array(&attrText.runs, *idx))->logical)));
            }
            printf("\n");
#endif

        }
        iAssert(size_Array(&runOrder) == size_Range(&wrapRuns));
        /* Alignment. */
        int origin = 0;
        iBool isRightAligned = attrText->isBaseRTL;
        if (isRightAligned) {
            if (layoutBound > 0) {
                origin = layoutBound - wrapAdvance;
            }
        }
        /* Make a callback for each wrapped line. */
        if (wrap && wrap->wrapFunc &&
            !notify_WrapText(args->wrap,
                             sourcePtr_AttributedText(attrText, wrapResumePos),
                             wrapAttrib,
                             origin,
                             iRound(wrapAdvance))) {
            willAbortDueToWrap = iTrue;
        }
        numWrapLines++;
        if (wrap && wrap->maxLines && numWrapLines == wrap->maxLines) {
            willAbortDueToWrap = iTrue;
        }
        wrapAttrib = lastAttrib;
        /* We have determined a possible wrap position and alignment for the work runs,
           so now we can process the glyphs. However, glyphs may sometimes overlap due to
           kerning, so all backgrounds must be drawn first, as a separate layer, before
           any foreground glyphs. Otherwise, there would be visible clipping. */
        iRunLayer layer = {
            /* TODO: Could use this already above and not duplicate the variables here. */
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
            if (~mode & draw_RunMode && layerIndex == foreground_RunLayerType) {
                continue; /* just one layer for measurements */
            }
            layer.xCursor = origin;
            layer.yCursor = yCursor;
            process_RunLayer_(&layer, layerIndex);
        }
        bounds     = layer.bounds;
        xCursor    = layer.xCursor;
        xCursorMax = layer.xCursorMax;
        yCursor    = layer.yCursor;
        deinit_Array(&runOrder);
        if (willAbortDueToWrap) {
            break;
        }
        wrapRuns.start     = wrapResumeRunIndex;
        wrapRuns.end       = runCount;
        wrapPosRange.start = wrapResumePos;
        wrapPosRange.end   = textLen;
    }
    if (endsWith_Rangecc(args->text, "\n")) {
        /* FIXME: This is a kludge, the wrap loop should handle this case, too. */
        /* The last wrap is an empty newline wrap. */
        xCursor = 0;
        yCursor += d->font.height;
    }
    if (args->metrics_out) {
        args->metrics_out->advance = init_I2(xCursor, yCursor);
        args->metrics_out->bounds = bounds;
    }
}

#else /* !defined (LAGRANGE_ENABLE_HARFBUZZ) */

/* The fallback method: an incomplete solution for simple scripts. */
#   define run_Font_    runSimple_Font_
#   include "text_simple.c"

#endif /* defined (LAGRANGE_ENABLE_HARFBUZZ) */

void run_Font(iBaseFont *font, const iRunArgs *args) {
    return run_Font_((iFont *) font, args);
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
    return current_StbText_()->cache;
}
