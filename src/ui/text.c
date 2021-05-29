/* Copyright 2020 Jaakko Keränen <jaakko.keranen@iki.fi>

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
#include "color.h"
#include "metrics.h"
#include "embedded.h"
#include "window.h"
#include "app.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "../stb_truetype.h"

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
#include <SDL_hints.h>
#include <SDL_version.h>
#include <stdarg.h>

#if SDL_VERSION_ATLEAST(2, 0, 10)
#   define LAGRANGE_RASTER_DEPTH    8
#   define LAGRANGE_RASTER_FORMAT   SDL_PIXELFORMAT_INDEX8
#else
#   define LAGRANGE_RASTER_DEPTH    32
#   define LAGRANGE_RASTER_FORMAT   SDL_PIXELFORMAT_RGBA8888
#endif

iDeclareType(Font)
iDeclareType(Glyph)
iDeclareTypeConstructionArgs(Glyph, iChar ch)

static const float contentScale_Text_ = 1.3f;

int gap_Text;                           /* cf. gap_UI in metrics.h */
int enableHalfPixelGlyphs_Text = iTrue; /* debug setting */
int enableKerning_Text         = iTrue; /* looking up kern pairs is slow */

enum iGlyphFlag {
    rasterized0_GlyphFlag = iBit(1),    /* zero offset */
    rasterized1_GlyphFlag = iBit(2),    /* half-pixel offset */
};

struct Impl_Glyph {
    iHashNode node;
    int flags;
    uint32_t glyphIndex;
    iFont *font; /* may come from symbols/emoji */
    iRect rect[2]; /* zero and half pixel offset */
    iInt2 d[2];
    float advance; /* scaled */
};

void init_Glyph(iGlyph *d, iChar ch) {
    d->node.key   = ch;
    d->flags      = 0;
    d->glyphIndex = 0;
    d->font       = NULL;
    d->rect[0]    = zero_Rect();
    d->rect[1]    = zero_Rect();
    d->advance    = 0.0f;
}

void deinit_Glyph(iGlyph *d) {
    iUnused(d);
}

static iChar codepoint_Glyph_(const iGlyph *d) {
    return d->node.key;
}

iLocalDef iBool isRasterized_Glyph_(const iGlyph *d, int hoff) {
    return (d->flags & (rasterized0_GlyphFlag << hoff)) != 0;
}

iLocalDef iBool isFullyRasterized_Glyph_(const iGlyph *d) {
    return (d->flags & (rasterized0_GlyphFlag | rasterized1_GlyphFlag)) ==
           (rasterized0_GlyphFlag | rasterized1_GlyphFlag);
}

iLocalDef void setRasterized_Glyph_(iGlyph *d, int hoff) {
    d->flags |= rasterized0_GlyphFlag << hoff;
}

iDefineTypeConstructionArgs(Glyph, (iChar ch), ch)

/*-----------------------------------------------------------------------------------------------*/

struct Impl_Font {
    iBlock *       data;
    stbtt_fontinfo font;
    float          xScale, yScale;
    int            vertOffset; /* offset due to scaling */
    int            height;
    int            baseline;
    iHash          glyphs;
    iBool          isMonospaced;
    iBool          manualKernOnly;
    enum iFontSize sizeId;  /* used to look up different fonts of matching size */
    uint32_t       indexTable[128 - 32]; /* quick ASCII lookup */
};

static iFont *font_Text_(enum iFontId id);

static void init_Font(iFont *d, const iBlock *data, int height, float scale,
                      enum iFontSize sizeId, iBool isMonospaced) {
    init_Hash(&d->glyphs);
    d->data = NULL;
    d->isMonospaced = isMonospaced;
    d->height = height;
    iZap(d->font);
    stbtt_InitFont(&d->font, constData_Block(data), 0);
    int ascent, descent;
    stbtt_GetFontVMetrics(&d->font, &ascent, &descent, NULL);
    d->xScale = d->yScale = stbtt_ScaleForPixelHeight(&d->font, height) * scale;
    if (d->isMonospaced) {
        /* It is important that monospaced fonts align 1:1 with the pixel grid so that
           box-drawing characters don't have partially occupied edge pixels, leading to seams
           between adjacent glyphs. */
        int adv;
        stbtt_GetCodepointHMetrics(&d->font, 'M', &adv, NULL);
        const float advance = (float) adv * d->xScale;
        if (advance > 4) { /* not too tiny */
            d->xScale *= floorf(advance) / advance;
        }
    }
    d->baseline   = ascent * d->yScale;
    d->vertOffset = height * (1.0f - scale) / 2;
    /* Custom tweaks. */
    if (data == &fontNotoSansSymbolsRegular_Embedded ||
        data == &fontNotoSansSymbols2Regular_Embedded) {
        d->vertOffset /= 2; 
    }
    else if (data == &fontNotoEmojiRegular_Embedded) {
        //d->vertOffset -= height / 30;
    }
    d->sizeId = sizeId;
    memset(d->indexTable, 0xff, sizeof(d->indexTable));
}

static void clearGlyphs_Font_(iFont *d) {
    iForEach(Hash, i, &d->glyphs) {
        delete_Glyph((iGlyph *) i.value);
    }
    clear_Hash(&d->glyphs);
}

static void deinit_Font(iFont *d) {
    clearGlyphs_Font_(d);
    deinit_Hash(&d->glyphs);
    delete_Block(d->data);
}

static uint32_t glyphIndex_Font_(iFont *d, iChar ch) {
    /* TODO: Add a small cache of ~5 most recently found indices. */
    const size_t entry = ch - 32;
    if (entry < iElemCount(d->indexTable)) {
        if (d->indexTable[entry] == ~0u) {
            d->indexTable[entry] = stbtt_FindGlyphIndex(&d->font, ch);
        }
        return d->indexTable[entry];
    }
    return stbtt_FindGlyphIndex(&d->font, ch);
}

/*----------------------------------------------------------------------------------------------*/

iDeclareType(Text)
iDeclareType(CacheRow)

struct Impl_CacheRow {
    int   height;
    iInt2 pos;
};

struct Impl_Text {
    enum iTextFont contentFont;
    enum iTextFont headingFont;
    float          contentFontSize;
    iFont          fonts[max_FontId];
    SDL_Renderer * render;
    SDL_Texture *  cache;
    iInt2          cacheSize;
    int            cacheRowAllocStep;
    int            cacheBottom;
    iArray         cacheRows;
    SDL_Palette *  grayscale;
    iRegExp *      ansiEscape;
};

static iText   text_;
static iBlock *userFont_;

static void initFonts_Text_(iText *d) {
    const float textSize = fontSize_UI * d->contentFontSize;
    const float monoSize = textSize * 0.71f;
    const float smallMonoSize = monoSize * 0.8f;
    const iBlock *regularFont  = &fontNunitoRegular_Embedded;
    const iBlock *boldFont     = &fontNunitoBold_Embedded;
    const iBlock *italicFont   = &fontNunitoLightItalic_Embedded;
    const iBlock *h12Font      = &fontNunitoExtraBold_Embedded;
    const iBlock *h3Font       = &fontNunitoRegular_Embedded;
    const iBlock *lightFont    = &fontNunitoExtraLight_Embedded;
    float         scaling      = 1.0f; /* glyph scaling (<=1.0), for increasing line spacing */
    float         italicScaling= 1.0f;
    float         lightScaling = 1.0f;
    float         h123Scaling  = 1.0f; /* glyph scaling (<=1.0), for increasing line spacing */
    if (d->contentFont == firaSans_TextFont) {
        regularFont = &fontFiraSansRegular_Embedded;
        boldFont    = &fontFiraSansSemiBold_Embedded;
        lightFont   = &fontFiraSansLight_Embedded;
        italicFont  = &fontFiraSansItalic_Embedded;
        scaling     = italicScaling = lightScaling = 0.85f;
    }
    else if (d->contentFont == tinos_TextFont) {
        regularFont = &fontTinosRegular_Embedded;
        boldFont    = &fontTinosBold_Embedded;
        lightFont   = &fontLiterataExtraLightopsz18_Embedded;
        italicFont  = &fontTinosItalic_Embedded;
        scaling      = italicScaling = 0.85f;
    }
    else if (d->contentFont == literata_TextFont) {
        regularFont = &fontLiterataRegularopsz14_Embedded;
        boldFont    = &fontLiterataBoldopsz36_Embedded;
        italicFont  = &fontLiterataLightItalicopsz10_Embedded;
        lightFont   = &fontLiterataExtraLightopsz18_Embedded;
    }
    else if (d->contentFont == sourceSans3_TextFont) {
        regularFont = &fontSourceSans3Regular_Embedded;
        boldFont    = &fontSourceSans3Semibold_Embedded;
        italicFont  = &fontSourceSans3It_Embedded;
        lightFont   = &fontSourceSans3ExtraLight_Embedded;
    }
    else if (d->contentFont == iosevka_TextFont) {
        regularFont = &fontIosevkaTermExtended_Embedded;
        boldFont    = &fontIosevkaTermExtended_Embedded;
        italicFont  = &fontIosevkaTermExtended_Embedded;
        lightFont   = &fontIosevkaTermExtended_Embedded;
        scaling     = italicScaling = lightScaling = 0.866f;
    }
    if (d->headingFont == firaSans_TextFont) {
        h12Font     = &fontFiraSansBold_Embedded;
        h3Font      = &fontFiraSansRegular_Embedded;
        h123Scaling = 0.85f;
    }
    else if (d->headingFont == tinos_TextFont) {
        h12Font = &fontTinosBold_Embedded;
        h3Font  = &fontTinosRegular_Embedded;
        h123Scaling = 0.85f;
    }
    else if (d->headingFont == literata_TextFont) {
        h12Font = &fontLiterataBoldopsz36_Embedded;
        h3Font  = &fontLiterataRegularopsz14_Embedded;
    }
    else if (d->headingFont == sourceSans3_TextFont) {
        h12Font = &fontSourceSans3Bold_Embedded;
        h3Font = &fontSourceSans3Regular_Embedded;
    }
    else if (d->headingFont == iosevka_TextFont) {
        h12Font = &fontIosevkaTermExtended_Embedded;
        h3Font  = &fontIosevkaTermExtended_Embedded;
    }
#if defined (iPlatformAppleMobile)
    const float uiSize = fontSize_UI * 1.1f;
#else
    const float uiSize = fontSize_UI;
#endif
    const struct {
        const iBlock *ttf;
        int size;
        float scaling;
        enum iFontSize sizeId;
        /* UI sizes: 1.0, 1.125, 1.333, 1.666 */
        /* Content sizes: smallmono, mono, 1.0, 1.2, 1.333, 1.666, 2.0 */
    } fontData[max_FontId] = {
        /* UI fonts: normal weight */
        { &fontSourceSans3Regular_Embedded, uiSize,               1.0f, uiNormal_FontSize },
        { &fontSourceSans3Regular_Embedded, uiSize * 1.125f,      1.0f, uiMedium_FontSize },
        { &fontSourceSans3Regular_Embedded, uiSize * 1.333f,      1.0f, uiBig_FontSize },
        { &fontSourceSans3Regular_Embedded, uiSize * 1.666f,      1.0f, uiLarge_FontSize },
        /* UI fonts: bold weight */
        { &fontSourceSans3Bold_Embedded,    uiSize,               1.0f, uiNormal_FontSize },
        { &fontSourceSans3Bold_Embedded,    uiSize * 1.125f,      1.0f, uiMedium_FontSize },
        { &fontSourceSans3Bold_Embedded,    uiSize * 1.333f,      1.0f, uiBig_FontSize },
        { &fontSourceSans3Bold_Embedded,    uiSize * 1.666f,      1.0f, uiLarge_FontSize },
        /* content fonts */
        { regularFont,                        textSize,             scaling,      contentRegular_FontSize },
        { boldFont,                           textSize,             scaling,      contentRegular_FontSize },
        { italicFont,                         textSize,             italicScaling,contentRegular_FontSize },
        { regularFont,                        textSize * 1.200f,    scaling,      contentMedium_FontSize },
        { h3Font,                             textSize * 1.333f,    h123Scaling,  contentBig_FontSize },
        { h12Font,                            textSize * 1.666f,    h123Scaling,  contentLarge_FontSize },
        { lightFont,                          textSize * 1.666f,    lightScaling, contentLarge_FontSize },
        { h12Font,                            textSize * 2.000f,    h123Scaling,  contentHuge_FontSize },
        { &fontIosevkaTermExtended_Embedded,  smallMonoSize,        1.0f,         contentMonoSmall_FontSize },
        { &fontIosevkaTermExtended_Embedded,  monoSize,             1.0f,         contentMono_FontSize },
        /* extra content fonts */
        { &fontSourceSans3Regular_Embedded,   textSize,             scaling, contentRegular_FontSize },
        { &fontSourceSans3Regular_Embedded,   textSize * 0.80f,     scaling, contentRegular_FontSize },
        /* symbols and scripts */
#define DEFINE_FONT_SET(data, glyphScale) \
        { (data), uiSize,            glyphScale, uiNormal_FontSize }, \
        { (data), uiSize * 1.125f,   glyphScale, uiMedium_FontSize }, \
        { (data), uiSize * 1.333f,   glyphScale, uiBig_FontSize }, \
        { (data), uiSize * 1.666f,   glyphScale, uiLarge_FontSize }, \
        { (data), textSize,          glyphScale, contentRegular_FontSize }, \
        { (data), textSize * 1.200f, glyphScale, contentMedium_FontSize }, \
        { (data), textSize * 1.333f, glyphScale, contentBig_FontSize }, \
        { (data), textSize * 1.666f, glyphScale, contentLarge_FontSize }, \
        { (data), textSize * 2.000f, glyphScale, contentHuge_FontSize }, \
        { (data), smallMonoSize,     glyphScale, contentMonoSmall_FontSize }, \
        { (data), monoSize,          glyphScale, contentMono_FontSize }
        DEFINE_FONT_SET(userFont_ ? userFont_ : &fontIosevkaTermExtended_Embedded, 1.0f),
        DEFINE_FONT_SET(&fontIosevkaTermExtended_Embedded, 0.866f),
        DEFINE_FONT_SET(&fontNotoSansSymbolsRegular_Embedded, 1.45f),
        DEFINE_FONT_SET(&fontNotoSansSymbols2Regular_Embedded, 1.45f),
        DEFINE_FONT_SET(&fontSmolEmojiRegular_Embedded, 1.0f),
        DEFINE_FONT_SET(&fontNotoEmojiRegular_Embedded, 1.10f),
        DEFINE_FONT_SET(&fontNotoSansJPRegular_Embedded, 1.0f),
        DEFINE_FONT_SET(&fontNotoSansSCRegular_Embedded, 1.0f),
        DEFINE_FONT_SET(&fontNanumGothicRegular_Embedded, 1.0f), /* TODO: should use Noto Sans here, too */
        DEFINE_FONT_SET(&fontNotoSansArabicUIRegular_Embedded, 1.0f),
    };
    iForIndices(i, fontData) {
        iFont *font = &d->fonts[i];
        init_Font(font,
                  fontData[i].ttf,
                  fontData[i].size,
                  fontData[i].scaling,
                  fontData[i].sizeId,
                  fontData[i].ttf == &fontIosevkaTermExtended_Embedded);
        if (i == default_FontId || i == defaultMedium_FontId) {
            font->manualKernOnly = iTrue;
        }
    }
    gap_Text = iRound(gap_UI * d->contentFontSize);
}

static void deinitFonts_Text_(iText *d) {
    iForIndices(i, d->fonts) {
        deinit_Font(&d->fonts[i]);
    }
}

static int maxGlyphHeight_Text_(const iText *d) {
    return 2 * d->contentFontSize * fontSize_UI;
}

static void initCache_Text_(iText *d) {
    init_Array(&d->cacheRows, sizeof(iCacheRow));
    const int textSize = d->contentFontSize * fontSize_UI;
    iAssert(textSize > 0);
    const iInt2 cacheDims = init_I2(16, 40);
    d->cacheSize = mul_I2(cacheDims, init1_I2(iMax(textSize, fontSize_UI)));
    SDL_RendererInfo renderInfo;
    SDL_GetRendererInfo(d->render, &renderInfo);
    if (renderInfo.max_texture_height > 0 && d->cacheSize.y > renderInfo.max_texture_height) {
        d->cacheSize.y = renderInfo.max_texture_height;
        d->cacheSize.x = renderInfo.max_texture_width;
    }
    d->cacheRowAllocStep = iMax(2, textSize / 6);
    /* Allocate initial (empty) rows. These will be assigned actual locations in the cache
       once at least one glyph is stored. */
    for (int h = d->cacheRowAllocStep; h <= 2 * textSize + d->cacheRowAllocStep; h += d->cacheRowAllocStep) {
        pushBack_Array(&d->cacheRows, &(iCacheRow){ .height = 0 });
    }
    d->cacheBottom = 0;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    d->cache = SDL_CreateTexture(d->render,
                                 SDL_PIXELFORMAT_RGBA4444,
                                 SDL_TEXTUREACCESS_STATIC | SDL_TEXTUREACCESS_TARGET,
                                 d->cacheSize.x,
                                 d->cacheSize.y);
    SDL_SetTextureBlendMode(d->cache, SDL_BLENDMODE_BLEND);
}

static void deinitCache_Text_(iText *d) {
    deinit_Array(&d->cacheRows);
    SDL_DestroyTexture(d->cache);
}

void loadUserFonts_Text(void) {
    if (userFont_) {
        delete_Block(userFont_);
        userFont_ = NULL;
    }
    /* Load the system font. */
    const iPrefs *prefs = prefs_App();
    if (!isEmpty_String(&prefs->symbolFontPath)) {
        iFile *f = new_File(&prefs->symbolFontPath);
        if (open_File(f, readOnly_FileMode)) {
            userFont_ = readAll_File(f);
        }
        else {
            fprintf(stderr, "[Text] failed to open: %s\n", cstr_String(&prefs->symbolFontPath));
        }
        iRelease(f);
    }
}

void init_Text(SDL_Renderer *render) {
    iText *d = &text_;
    loadUserFonts_Text();
    d->contentFont     = nunito_TextFont;
    d->headingFont     = nunito_TextFont;
    d->contentFontSize = contentScale_Text_;
    d->ansiEscape      = new_RegExp("[[()]([0-9;AB]*)m", 0);
    d->render          = render;
    /* A grayscale palette for rasterized glyphs. */ {
        SDL_Color colors[256];
        for (int i = 0; i < 256; ++i) {
            colors[i] = (SDL_Color){ 255, 255, 255, i };
        }
        d->grayscale = SDL_AllocPalette(256);
        SDL_SetPaletteColors(d->grayscale, colors, 0, 256);
    }
    initCache_Text_(d);
    initFonts_Text_(d);
}

void deinit_Text(void) {
    iText *d = &text_;
    SDL_FreePalette(d->grayscale);
    deinitFonts_Text_(d);
    deinitCache_Text_(d);
    d->render = NULL;
    iRelease(d->ansiEscape);
}

void setOpacity_Text(float opacity) {
    SDL_SetTextureAlphaMod(text_.cache, iClamp(opacity, 0.0f, 1.0f) * 255 + 0.5f);
}

void setContentFont_Text(enum iTextFont font) {
    if (text_.contentFont != font) {
        text_.contentFont = font;
        resetFonts_Text();
    }
}

void setHeadingFont_Text(enum iTextFont font) {
    if (text_.headingFont != font) {
        text_.headingFont = font;
        resetFonts_Text();
    }
}

void setContentFontSize_Text(float fontSizeFactor) {
    fontSizeFactor *= contentScale_Text_;
    iAssert(fontSizeFactor > 0);
    if (iAbs(text_.contentFontSize - fontSizeFactor) > 0.001f) {
        text_.contentFontSize = fontSizeFactor;
        resetFonts_Text();
    }
}

static void resetCache_Text_(iText *d) {
    deinitCache_Text_(d);
    for (int i = 0; i < max_FontId; i++) {
        clearGlyphs_Font_(&d->fonts[i]);
    }
    initCache_Text_(d);
}

void resetFonts_Text(void) {
    iText *d = &text_;
    deinitFonts_Text_(d);
    deinitCache_Text_(d);
    initCache_Text_(d);
    initFonts_Text_(d);
}

iLocalDef iFont *font_Text_(enum iFontId id) {
    return &text_.fonts[id & mask_FontId];
}

static SDL_Surface *rasterizeGlyph_Font_(const iFont *d, uint32_t glyphIndex, float xShift) {
    int w, h;
    uint8_t *bmp = stbtt_GetGlyphBitmapSubpixel(
        &d->font, d->xScale, d->yScale, xShift, 0.0f, glyphIndex, &w, &h, 0, 0);
    SDL_Surface *surface8 =
        SDL_CreateRGBSurfaceWithFormatFrom(bmp, w, h, 8, w, SDL_PIXELFORMAT_INDEX8);
    SDL_SetSurfaceBlendMode(surface8, SDL_BLENDMODE_NONE);
    SDL_SetSurfacePalette(surface8, text_.grayscale);
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

iLocalDef iCacheRow *cacheRow_Text_(iText *d, int height) {
    return at_Array(&d->cacheRows, (height - 1) / d->cacheRowAllocStep);
}

static iInt2 assignCachePos_Text_(iText *d, iInt2 size) {
    iCacheRow *cur = cacheRow_Text_(d, size.y);
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
    stbtt_GetGlyphBitmapBoxSubpixel(
        &d->font, glyph->glyphIndex, d->xScale, d->yScale, hoff * 0.5f, 0.0f, &x0, &y0, &x1, &y1);
    glRect->size = init_I2(x1 - x0, y1 - y0);
    /* Determine placement in the glyph cache texture, advancing in rows. */
    glRect->pos    = assignCachePos_Text_(&text_, glRect->size);
    glyph->d[hoff] = init_I2(x0, y0);
    glyph->d[hoff].y += d->vertOffset;
    if (hoff == 0) { /* hoff==1 uses same metrics as `glyph` */
        int adv;
        const uint32_t gIndex = glyph->glyphIndex;
        stbtt_GetGlyphHMetrics(&d->font, gIndex, &adv, NULL);
        glyph->advance = d->xScale * adv;
    }
}

iLocalDef iFont *characterFont_Font_(iFont *d, iChar ch, uint32_t *glyphIndex) {
    if (isVariationSelector_Char(ch)) {
        return d;
    }
    /* Smol Emoji overrides all other fonts. */
    if (ch != 0x20) {
        iFont *smol = font_Text_(smolEmoji_FontId + d->sizeId);
        if (smol != d && (*glyphIndex = glyphIndex_Font_(smol, ch)) != 0) {
            return smol;
        }
    }
    /* Manual exceptions. */ {
        if (ch >= 0x2190 && ch <= 0x2193 /* arrows */) {
            d = font_Text_(iosevka_FontId + d->sizeId);
            *glyphIndex = glyphIndex_Font_(d, ch);
            return d;
        }
    }
    if ((*glyphIndex = glyphIndex_Font_(d, ch)) != 0) {
        return d;
    }
    const int fallbacks[] = {
        notoEmoji_FontId,
        symbols2_FontId,
        symbols_FontId
    };
    /* First fallback is Smol Emoji. */
    iForIndices(i, fallbacks) {
        iFont *fallback = font_Text_(fallbacks[i] + d->sizeId);
        if (fallback != d && (*glyphIndex = glyphIndex_Font_(fallback, ch)) != 0) {
            return fallback;
        }
    }
    /* Try Simplified Chinese. */
    if (ch >= 0x2e80) {
        iFont *sc = font_Text_(chineseSimplified_FontId + d->sizeId);
        if (sc != d && (*glyphIndex = glyphIndex_Font_(sc, ch)) != 0) {
            return sc;
        }
    }
    /* Could be Korean. */
    if (ch >= 0x3000) {
        iFont *korean = font_Text_(korean_FontId + d->sizeId);
        if (korean != d && (*glyphIndex = glyphIndex_Font_(korean, ch)) != 0) {
            return korean;
        }
    }
    /* Japanese perhaps? */
    if (ch > 0x3040) {
        iFont *japanese = font_Text_(japanese_FontId + d->sizeId);
        if (japanese != d && (*glyphIndex = glyphIndex_Font_(japanese, ch)) != 0) {
            return japanese;
        }
    }
    /* Maybe Arabic. */
    if (ch >= 0x600) {
        iFont *arabic = font_Text_(arabic_FontId + d->sizeId);
        if (arabic != d && (*glyphIndex = glyphIndex_Font_(arabic, ch)) != 0) {
            return arabic;
        }
    }
#if defined (iPlatformApple)
    /* White up arrow is used for the Shift key on macOS. Symbola's glyph is not a great
       match to the other text, so use the UI font instead. */
    if ((ch == 0x2318 || ch == 0x21e7) && d == font_Text_(regular_FontId)) {
        *glyphIndex = glyphIndex_Font_(d = font_Text_(defaultContentRegular_FontId), ch);
        return d;
    }
#endif
    /* User's symbols font. */ {
        iFont *sys = font_Text_(userSymbols_FontId + d->sizeId);
        if (sys != d && (*glyphIndex = glyphIndex_Font_(sys, ch)) != 0) {
            return sys;
        }
    }
    /* Final fallback. */
    iFont *font = font_Text_(iosevka_FontId + d->sizeId);
    if (d != font) {
        *glyphIndex = glyphIndex_Font_(font, ch);
    }
    if (!*glyphIndex) {
        fprintf(stderr, "failed to find %08x (%lc)\n", ch, (int)ch); fflush(stderr);
    }
    return d;
}

static iGlyph *glyph_Font_(iFont *d, iChar ch) {
    iGlyph * glyph;
    uint32_t glyphIndex = 0;
    /* The glyph may actually come from a different font; look up the right font. */
    iFont *font = characterFont_Font_(d, ch, &glyphIndex);
    void * node = value_Hash(&font->glyphs, ch);
    if (node) {
        glyph = node;
    }
    else {
        /* If the cache is running out of space, clear it and we'll recache what's needed currently. */
        if (text_.cacheBottom > text_.cacheSize.y - maxGlyphHeight_Text_(&text_)) {
#if !defined (NDEBUG)
            printf("[Text] glyph cache is full, clearing!\n"); fflush(stdout);
#endif
            resetCache_Text_(&text_);
        }
        glyph             = new_Glyph(ch);
        glyph->glyphIndex = glyphIndex;
        glyph->font       = font;
        /* New glyphs are always allocated at least. This reserves a position in the cache
           and updates the glyph metrics. */
        allocate_Font_(font, glyph, 0);
        allocate_Font_(font, glyph, 1);
        insert_Hash(&font->glyphs, &glyph->node);
    }
    return glyph;
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

iDeclareType(RasterGlyph)

struct Impl_RasterGlyph {
    iGlyph *glyph;
    int     hoff;
    iRect   rect;
};

void cacheTextGlyphs_Font_(iFont *d, const iRangecc text) {
    const char * chPos   = text.start;
    SDL_Surface *buf     = NULL;
    const iInt2  bufSize = init_I2(iMin(512, d->height * iMin(2 * size_Range(&text), 20)),
                                   d->height * 4 / 3);
    int          bufX    = 0;
    iArray *     rasters = NULL;
    SDL_Texture *oldTarget = NULL;
    iBool        isTargetChanged = iFalse;
    iAssert(isExposed_Window(get_Window()));
    /* We'll flush the buffered rasters periodically until everything is cached. */
    while (chPos < text.end) {
        while (chPos < text.end) {
            const char *lastPos = chPos;
            const iChar ch = nextChar_(&chPos, text.end);
            if (ch == 0 || isSpace_Char(ch) || isDefaultIgnorable_Char(ch) ||
                isFitzpatrickType_Char(ch)) {
                continue;
            }
            const int lastCacheBottom = text_.cacheBottom;
            iGlyph *glyph = glyph_Font_(d, ch);
            if (text_.cacheBottom < lastCacheBottom) {
                /* The cache was reset due to running out of space. We need to restart from
                   the beginning! */
                chPos = text.start;
                bufX = 0;
                if (rasters) {
                    clear_Array(rasters);
                }
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
                    SDL_SetSurfacePalette(buf, text_.grayscale);
                }
                SDL_Surface *surfaces[2] = {
                    !isRasterized_Glyph_(glyph, 0) ?
                            rasterizeGlyph_Font_(glyph->font, glyph->glyphIndex, 0) : NULL,
                    !isRasterized_Glyph_(glyph, 1) ?
                            rasterizeGlyph_Font_(glyph->font, glyph->glyphIndex, 0.5f) : NULL
                };
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
                iForIndices(i, surfaces) {
                    if (surfaces[i]) {
                        if (surfaces[i]->flags & SDL_PREALLOC) {
                            free(surfaces[i]->pixels);
                        }
                        SDL_FreeSurface(surfaces[i]);
                    }
                }
                if (outOfSpace) {
                    chPos = lastPos;
                    break;
                }
            }
        }
        /* Finished or the buffer is full, copy the glyphs to the cache texture. */
        if (!isEmpty_Array(rasters)) {
            SDL_Texture *bufTex = SDL_CreateTextureFromSurface(text_.render, buf);
            SDL_SetTextureBlendMode(bufTex, SDL_BLENDMODE_NONE);
            if (!isTargetChanged) {
                isTargetChanged = iTrue;
                oldTarget = SDL_GetRenderTarget(text_.render);
                SDL_SetRenderTarget(text_.render, text_.cache);
            }
//            printf("copying %zu rasters from %p\n", size_Array(rasters), bufTex); fflush(stdout);
            iConstForEach(Array, i, rasters) {
                const iRasterGlyph *rg = i.value;
//                iAssert(isEqual_I2(rg->rect.size, rg->glyph->rect[rg->hoff].size));
                const iRect *glRect = &rg->glyph->rect[rg->hoff];
                SDL_RenderCopy(text_.render,
                               bufTex,
                               (const SDL_Rect *) &rg->rect,
                               (const SDL_Rect *) glRect);
                setRasterized_Glyph_(rg->glyph, rg->hoff);
//                printf(" - %u\n", rg->glyph->glyphIndex);
            }
            SDL_DestroyTexture(bufTex);
            /* Resume with an empty buffer. */
            clear_Array(rasters);
            bufX = 0;
        }
        else {
            iAssert(chPos >= text.end);
        }
    }
    if (rasters) {
        delete_Array(rasters);
    }
    if (buf) {
        SDL_FreeSurface(buf);
    }
    if (isTargetChanged) {
        SDL_SetRenderTarget(text_.render, oldTarget);
    }
}

enum iRunMode {
    measure_RunMode                 = 0,
    draw_RunMode                    = 1,
    modeMask_RunMode                = 0x00ff,
    flagsMask_RunMode               = 0xff00,
    noWrapFlag_RunMode              = iBit(9),
    visualFlag_RunMode              = iBit(10), /* actual visible bounding box of the glyph,
                                                   e.g., for icons */
    permanentColorFlag_RunMode      = iBit(11),
    alwaysVariableWidthFlag_RunMode = iBit(12),
    fillBackground_RunMode          = iBit(13),
    stopAtNewline_RunMode           = iBit(14), /* don't advance past \n, consider it a wrap pos */
};

static enum iFontId fontId_Text_(const iFont *font) {
    return (enum iFontId) (font - text_.fonts);
}

iLocalDef iBool isWrapPunct_(iChar c) {
    /* Punctuation that participates in word-wrapping. */
    return (c == '/' || c == '\\' || c == '=' || c == '-' || c == ',' || c == ';' || c == '.' || c == ':' || c == 0xad);
}

iLocalDef iBool isClosingBracket_(iChar c) {
    return (c == ')' || c == ']' || c == '}' || c == '>');
}

//iLocalDef iBool isBracket_(iChar c) {
//    return (c == '(' || c == '[' || c == '{' || c == '<' || isClosingBracket_(c));
//}

iLocalDef iBool isWrapBoundary_(iChar prevC, iChar c) {
    /* Line wrapping boundaries are determined by looking at a character and the
       last character processed. We want to wrap at natural word boundaries where
       possible, so normally we wrap at a space followed a non-space character. As
       an exception, we also wrap after punctuation used to break up words, so we
       can wrap text like foo/bar/baz-abc-def.xyz at any puncation boundaries,
       without wrapping on other punctuation used for expressive purposes like
       emoticons :-) */
    if (isClosingBracket_(prevC) && !isWrapPunct_(c)) {
        return iTrue;
    }
    if (isSpace_Char(prevC)) {
        return iFalse;
    }
    if ((prevC == '/' || prevC == '\\' || prevC == '-' || prevC == '_' || prevC == '+') &&
        !isWrapPunct_(c)) {
        return iTrue;
    }
    return isSpace_Char(c);
}

iLocalDef iBool isMeasuring_(enum iRunMode mode) {
    return (mode & modeMask_RunMode) == measure_RunMode;
}

iDeclareType(RunArgs)

struct Impl_RunArgs {
    enum iRunMode mode;
    iRangecc      text;
    size_t        maxLen; /* max characters to process */
    iInt2         pos;
    int           xposLimit;       /* hard limit for wrapping */
    int           xposLayoutBound; /* visible bound for layout purposes; does not affect wrapping */
    int           color;
    const char ** continueFrom_out;
    int *         runAdvance_out;
};

static iRect run_Font_(iFont *d, const iRunArgs *args) {
    iRect       bounds      = zero_Rect();
    const iInt2 orig        = args->pos;
    float       xpos        = orig.x;
    float       xposMax     = xpos;
    float       monoAdvance = 0;
    int         ypos        = orig.y;
    size_t      maxLen      = args->maxLen ? args->maxLen : iInvalidSize;
    float       xposExtend  = orig.x; /* allows wide glyphs to use more space; restored by whitespace */
    const enum iRunMode mode        = args->mode;
    const char *        lastWordEnd = args->text.start;
    iAssert(args->xposLimit == 0 || isMeasuring_(mode));
    iAssert(args->text.end >= args->text.start);
    if (args->continueFrom_out) {
        *args->continueFrom_out = args->text.end;
    }
    iChar prevCh = 0;
    const iBool isMonospaced = d->isMonospaced && !(mode & alwaysVariableWidthFlag_RunMode);
    if (isMonospaced) {
        monoAdvance = glyph_Font_(d, 'M')->advance;
    }
    if (args->mode & fillBackground_RunMode) {
        const iColor initial = get_Color(args->color);
        SDL_SetRenderDrawColor(text_.render, initial.r, initial.g, initial.b, 0);
    }
    /* Text rendering is not very straightforward! Let's dive in... */
    for (const char *chPos = args->text.start; chPos != args->text.end; ) {
        iAssert(chPos < args->text.end);
        const char *currentPos = chPos;
        if (*chPos == 0x1b) { /* ANSI escape. */
            chPos++;
            iRegExpMatch m;
            init_RegExpMatch(&m);
            if (match_RegExp(text_.ansiEscape, chPos, args->text.end - chPos, &m)) {
                if (mode & draw_RunMode && ~mode & permanentColorFlag_RunMode) {
                    /* Change the color. */
                    const iColor clr =
                        ansiForeground_Color(capturedRange_RegExpMatch(&m, 1), tmParagraph_ColorId);
                    SDL_SetTextureColorMod(text_.cache, clr.r, clr.g, clr.b);
                    if (args->mode & fillBackground_RunMode) {
                        SDL_SetRenderDrawColor(text_.render, clr.r, clr.g, clr.b, 0);
                    }
                }
                chPos = end_RegExpMatch(&m);
                continue;
            }
        }
        iChar ch = nextChar_(&chPos, args->text.end);
        iBool isEmoji = isEmoji_Char(ch);
        if (ch == 0x200d) { /* zero-width joiner */
            /* We don't have the composited Emojis. */
            if (isEmoji_Char(prevCh)) {
                /* skip */
                nextChar_(&chPos, args->text.end);
                ch = nextChar_(&chPos, args->text.end);
            }
        }
        if (isVariationSelector_Char(ch)) {
            ch = nextChar_(&chPos, args->text.end); /* skip it */
        }
        /* Special instructions. */ {
            if (ch == 0xad) { /* soft hyphen */
                lastWordEnd = chPos;
                if (isMeasuring_(mode)) {
                    if (args->xposLimit > 0) {
                        const char *postHyphen = chPos;
                        iChar       nextCh     = nextChar_(&postHyphen, args->text.end);
                        if ((int) xpos + glyph_Font_(d, ch)->rect[0].size.x +
                            glyph_Font_(d, nextCh)->rect[0].size.x > args->xposLimit) {
                            /* Wraps after hyphen, should show it. */
                        }
                        else continue;
                    }
                    else continue;
                }
                else {
                    /* Only show it at the end. */
                    if (chPos != args->text.end) {
                        continue;
                    }
                }
            }
            /* TODO: Check out if `uc_wordbreak_property()` from libunistring can be used here. */
            if (ch == '\n') {
                if (args->xposLimit > 0 && mode & stopAtNewline_RunMode) {
                    /* Stop the line here, this is a hard warp. */
                    if (args->continueFrom_out) {
                        *args->continueFrom_out = chPos;
                    }
                    break;
                }
                xpos = xposExtend = orig.x;
                ypos += d->height;
                prevCh = ch;
                continue;
            }
            if (ch == '\t') {
                const int tabStopWidth = d->height * 10;
                const int halfWidth    = (iMax(args->xposLimit, args->xposLayoutBound) - orig.x) / 2;
                const int xRel         = xpos - orig.x;
                /* First stop is always to half width. */
                if (halfWidth > 0 && xRel < halfWidth) {
                    xpos = orig.x + halfWidth;
                }
                else if (halfWidth > 0 && xRel < halfWidth * 3 / 2) {
                    xpos = orig.x + halfWidth * 3 / 2;
                }
                else {
                    xpos = orig.x + ((xRel / tabStopWidth) + 1) * tabStopWidth;
                }
                xposExtend = iMax(xposExtend, xpos);
                prevCh = 0;
                continue;
            }
            if (ch == '\r') { /* color change */
                iChar esc = nextChar_(&chPos, args->text.end);
                int colorNum = args->color;
                if (esc == '\r') { /* Extended range. */
                    esc = nextChar_(&chPos, args->text.end) + asciiExtended_ColorEscape;
                    colorNum = esc - asciiBase_ColorEscape;
                }
                else if (esc != 0x24) { /* ASCII Cancel */
                    colorNum = esc - asciiBase_ColorEscape;
                }
                if (mode & draw_RunMode && ~mode & permanentColorFlag_RunMode) {
                    const iColor clr = get_Color(colorNum);
                    SDL_SetTextureColorMod(text_.cache, clr.r, clr.g, clr.b);
                    if (args->mode & fillBackground_RunMode) {
                        SDL_SetRenderDrawColor(text_.render, clr.r, clr.g, clr.b, 0);
                    }
                }
                prevCh = 0;
                continue;
            }
            if (isDefaultIgnorable_Char(ch) || isFitzpatrickType_Char(ch)) {
                continue;
            }
        }
        const iGlyph *glyph = glyph_Font_(d, ch);
        int x1 = iMax(xpos, xposExtend);
        /* Which half of the pixel the glyph falls on? */
        const int hoff = enableHalfPixelGlyphs_Text ? (xpos - x1 > 0.5f ? 1 : 0) : 0;
        if (mode & draw_RunMode && ch != 0x20 && ch != 0 && !isRasterized_Glyph_(glyph, hoff)) {
            /* Need to pause here and make sure all glyphs have been cached in the text. */
//            printf("[Text] missing from cache: %lc (%x)\n", (int) ch, ch);
            cacheTextGlyphs_Font_(d, args->text);
            glyph = glyph_Font_(d, ch); /* cache may have been reset */
        }
        int x2 = x1 + glyph->rect[hoff].size.x;
        /* Out of the allotted space on the line? */
        if (args->xposLimit > 0 && x2 > args->xposLimit) {
            if (args->continueFrom_out) {
                if (lastWordEnd != args->text.start && ~mode & noWrapFlag_RunMode) {
                    *args->continueFrom_out = skipSpace_CStr(lastWordEnd);
                    *args->continueFrom_out = iMin(*args->continueFrom_out,
                                                   args->text.end);
                }
                else {
                    *args->continueFrom_out = currentPos; /* forced break */
                }
            }
            break;
        }
        const int yLineMax = ypos + d->height;
        SDL_Rect dst = { x1 + glyph->d[hoff].x,
                         ypos + glyph->font->baseline + glyph->d[hoff].y,
                         glyph->rect[hoff].size.x,
                         glyph->rect[hoff].size.y };
        if (glyph->font != d) {
            if (glyph->font->height > d->height) {
                /* Center-align vertically so the baseline isn't totally offset. */
                dst.y -= (glyph->font->height - d->height) / 2;
            }
        }
        /* Update the bounding box. */
        if (mode & visualFlag_RunMode) {
            if (isEmpty_Rect(bounds)) {
                bounds = init_Rect(dst.x, dst.y, dst.w, dst.h);
            }
            else {
                bounds = union_Rect(bounds, init_Rect(dst.x, dst.y, dst.w, dst.h));
            }
        }
        else {
            bounds.size.x = iMax(bounds.size.x, x2 - orig.x);
            bounds.size.y = iMax(bounds.size.y, ypos + glyph->font->height - orig.y);
        }
        /* Symbols and emojis are NOT monospaced, so must conform when the primary font
           is monospaced. Except with Japanese script, that's larger than the normal monospace. */
        const iBool useMonoAdvance =
            monoAdvance > 0 && !isJapanese_FontId(fontId_Text_(glyph->font));
        const float advance = (useMonoAdvance && glyph->advance > 0 ? monoAdvance : glyph->advance);
        if (!isMeasuring_(mode) && ch != 0x20 /* don't bother rendering spaces */) {
            if (useMonoAdvance && dst.w > advance && glyph->font != d && !isEmoji) {
                /* Glyphs from a different font may need recentering to look better. */
                dst.x -= (dst.w - advance) / 2;
            }
            SDL_Rect src;
            memcpy(&src, &glyph->rect[hoff], sizeof(SDL_Rect));
            /* Clip the glyphs to the font's height. This is useful when the font's line spacing
               has been reduced or when the glyph is from a different font. */
            if (dst.y + dst.h > yLineMax) {
                const int over = dst.y + dst.h - yLineMax;
                src.h -= over;
                dst.h -= over;
            }
            if (dst.y < ypos) {
                const int over = ypos - dst.y;
                dst.y += over;
                dst.h -= over;
                src.y += over;
                src.h -= over;
            }
            if (args->mode & fillBackground_RunMode) {
                /* Alpha blending looks much better if the RGB components don't change in
                   the partially transparent pixels. */
                SDL_RenderFillRect(text_.render, &dst);
            }
            SDL_RenderCopy(text_.render, text_.cache, &src, &dst);
        }
        xpos += advance;
        if (!isSpace_Char(ch)) {
            xposExtend += isEmoji ? glyph->advance : advance;
        }
#if defined (LAGRANGE_ENABLE_KERNING)
        /* Check the next character. */
        if (!isMonospaced && glyph->font == d) {
            /* TODO: No need to decode the next char twice; check this on the next iteration. */
            const char *peek = chPos;
            const iChar next = nextChar_(&peek, args->text.end);
            if (enableKerning_Text && !d->manualKernOnly && next) {
                const uint32_t nextGlyphIndex = glyphIndex_Font_(glyph->font, next);
                const int kern = stbtt_GetGlyphKernAdvance(
                    &glyph->font->font, glyph->glyphIndex, nextGlyphIndex);
                if (kern) {
//                    printf("%lc(%u) -> %lc(%u): kern %d (%f)\n", ch, glyph->glyphIndex, next,
//                           nextGlyphIndex,
//                           kern, d->xScale * kern);
                    xpos       += d->xScale * kern;
                    xposExtend += d->xScale * kern;
                }
            }
        }
#endif
        xposExtend = iMax(xposExtend, xpos);
        xposMax    = iMax(xposMax, xposExtend);
        if (args->continueFrom_out && ((mode & noWrapFlag_RunMode) || isWrapBoundary_(prevCh, ch))) {
            lastWordEnd = currentPos; /* mark word wrap position */
        }
        prevCh = ch;
        if (--maxLen == 0) {
            break;
        }
    }
    if (args->runAdvance_out) {
        *args->runAdvance_out = xposMax - orig.x;
    }
    fflush(stdout);
    return bounds;
}

int lineHeight_Text(int fontId) {
    return font_Text_(fontId)->height;
}

iInt2 measureRange_Text(int fontId, iRangecc text) {
    if (isEmpty_Range(&text)) {
        return init_I2(0, lineHeight_Text(fontId));
    }
    return run_Font_(font_Text_(fontId), &(iRunArgs){ .mode = measure_RunMode, .text = text }).size;
}

iRect visualBounds_Text(int fontId, iRangecc text) {
    return run_Font_(font_Text_(fontId),
                     &(iRunArgs){
                         .mode = measure_RunMode | visualFlag_RunMode,
                         .text = text,
                     });
}

iInt2 measure_Text(int fontId, const char *text) {
    return measureRange_Text(fontId, range_CStr(text));
}

void cache_Text(int fontId, iRangecc text) {
    cacheTextGlyphs_Font_(font_Text_(fontId), text);
}

static int runFlagsFromId_(enum iFontId fontId) {
    int runFlags = 0;
    if (fontId & alwaysVariableFlag_FontId) {
        runFlags |= alwaysVariableWidthFlag_RunMode;
    }
    return runFlags;
}

iInt2 advanceRange_Text(int fontId, iRangecc text) {
    int advance;
    const int height = run_Font_(font_Text_(fontId),
                                 &(iRunArgs){ .mode = measure_RunMode | runFlagsFromId_(fontId),
                                              .text = text,
                                              .runAdvance_out = &advance })
                           .size.y;
    return init_I2(advance, height);
}

iInt2 tryAdvance_Text(int fontId, iRangecc text, int width, const char **endPos) {
    int advance;
    const int height = run_Font_(font_Text_(fontId),
                                 &(iRunArgs){ .mode = measure_RunMode | stopAtNewline_RunMode |
                                                      runFlagsFromId_(fontId),
                                              .text = text,
                                              .xposLimit        = width,
                                              .continueFrom_out = endPos,
                                              .runAdvance_out   = &advance })
                           .size.y;
    return init_I2(advance, height);
}

iInt2 tryAdvanceNoWrap_Text(int fontId, iRangecc text, int width, const char **endPos) {
    int advance;
    const int height = run_Font_(font_Text_(fontId),
                                 &(iRunArgs){ .mode = measure_RunMode | noWrapFlag_RunMode |
                                                      stopAtNewline_RunMode |
                                                      runFlagsFromId_(fontId),
                                              .text             = text,
                                              .xposLimit        = width,
                                              .continueFrom_out = endPos,
                                              .runAdvance_out   = &advance })
                           .size.y;
    return init_I2(advance, height);
}

iInt2 advance_Text(int fontId, const char *text) {
    return advanceRange_Text(fontId, range_CStr(text));
}

iInt2 advanceN_Text(int fontId, const char *text, size_t n) {
    if (n == 0) {
        return init_I2(0, lineHeight_Text(fontId));
    }
    int advance;
    run_Font_(font_Text_(fontId),
              &(iRunArgs){ .mode           = measure_RunMode | runFlagsFromId_(fontId),
                           .text           = range_CStr(text),
                           .maxLen         = n,
                           .runAdvance_out = &advance });
    return init_I2(advance, lineHeight_Text(fontId));
}

static void drawBoundedN_Text_(int fontId, iInt2 pos, int xposBound, int color, iRangecc text, size_t maxLen) {
    iText *d    = &text_;
    iFont *font = font_Text_(fontId);
    const iColor clr = get_Color(color & mask_ColorId);
    SDL_SetTextureColorMod(d->cache, clr.r, clr.g, clr.b);
    run_Font_(font,
              &(iRunArgs){ .mode = draw_RunMode |
                                   (color & permanent_ColorId ? permanentColorFlag_RunMode : 0) |
                                   (color & fillBackground_ColorId ? fillBackground_RunMode : 0) |
                                   runFlagsFromId_(fontId),
                           .text            = text,
                           .maxLen          = maxLen,                           
                           .pos             = pos,
                           .xposLayoutBound = xposBound,
                           .color           = color & mask_ColorId });
}

static void drawBounded_Text_(int fontId, iInt2 pos, int xposBound, int color, iRangecc text) {
    drawBoundedN_Text_(fontId, pos, xposBound, color, text, 0);
}

static void draw_Text_(int fontId, iInt2 pos, int color, iRangecc text) {
    drawBounded_Text_(fontId, pos, 0, color, text);
}

void drawAlign_Text(int fontId, iInt2 pos, int color, enum iAlignment align, const char *text, ...) {
    iBlock chars;
    init_Block(&chars, 0); {
        va_list args;
        va_start(args, text);
        vprintf_Block(&chars, text, args);
        va_end(args);
    }
    if (align == center_Alignment) {
        pos.x -= measure_Text(fontId, cstr_Block(&chars)).x / 2;
    }
    else if (align == right_Alignment) {
        pos.x -= measure_Text(fontId, cstr_Block(&chars)).x;
    }
    draw_Text_(fontId, pos, color, range_Block(&chars));
    deinit_Block(&chars);
}

void draw_Text(int fontId, iInt2 pos, int color, const char *text, ...) {
    iBlock chars;
    init_Block(&chars, 0); {
        va_list args;
        va_start(args, text);
        vprintf_Block(&chars, text, args);
        va_end(args);
    }
    draw_Text_(fontId, pos, color, range_Block(&chars));
    deinit_Block(&chars);
}

void drawString_Text(int fontId, iInt2 pos, int color, const iString *text) {
    draw_Text_(fontId, pos, color, range_String(text));
}

void drawRange_Text(int fontId, iInt2 pos, int color, iRangecc text) {
    draw_Text_(fontId, pos, color, text);
}

void drawRangeN_Text(int fontId, iInt2 pos, int color, iRangecc text, size_t maxChars) {
    drawBoundedN_Text_(fontId, pos, 0, color, text, maxChars);
}

iInt2 advanceWrapRange_Text(int fontId, int maxWidth, iRangecc text) {
    iInt2 size = zero_I2();
    const char *endp;
    while (!isEmpty_Range(&text)) {
        iInt2 line = tryAdvance_Text(fontId, text, maxWidth, &endp);
        text.start = endp;
        size.x = iMax(size.x, line.x);
        size.y += line.y;
    }
    return size;
}

void drawBoundRange_Text(int fontId, iInt2 pos, int boundWidth, int color, iRangecc text) {
    /* This function is used together with text that has already been wrapped, so we'll know
       the bound width but don't have to re-wrap the text. */
    drawBounded_Text_(fontId, pos, pos.x + boundWidth, color, text);
}

int drawWrapRange_Text(int fontId, iInt2 pos, int maxWidth, int color, iRangecc text) {
    const char *endp;
    while (!isEmpty_Range(&text)) {
        const iInt2 adv = tryAdvance_Text(fontId, text, maxWidth, &endp);
        drawRange_Text(fontId, pos, color, (iRangecc){ text.start, endp });
        text.start = endp;
        pos.y += iMax(adv.y, lineHeight_Text(fontId));
    }
    return pos.y;
}

void drawCentered_Text(int fontId, iRect rect, iBool alignVisual, int color, const char *format, ...) {
    iBlock chars;
    init_Block(&chars, 0); {
        va_list args;
        va_start(args, format);
        vprintf_Block(&chars, format, args);
        va_end(args);
    }
    drawCenteredRange_Text(fontId, rect, alignVisual, color, range_Block(&chars));
    deinit_Block(&chars);
}

void drawCenteredRange_Text(int fontId, iRect rect, iBool alignVisual, int color, iRangecc text) {
    iRect textBounds = alignVisual ? visualBounds_Text(fontId, text)
                                   : (iRect){ zero_I2(), advanceRange_Text(fontId, text) };
    textBounds.pos = sub_I2(mid_Rect(rect), mid_Rect(textBounds));
    textBounds.pos.x = iMax(textBounds.pos.x, left_Rect(rect)); /* keep left edge visible */
    draw_Text_(fontId, textBounds.pos, color, text);
}

SDL_Texture *glyphCache_Text(void) {
    return text_.cache;
}

static void freeBitmap_(void *ptr) {
    stbtt_FreeBitmap(ptr, NULL);
}

iString *renderBlockChars_Text(const iBlock *fontData, int height, enum iTextBlockMode mode,
                               const iString *text) {
    iBeginCollect();
    stbtt_fontinfo font;
    iZap(font);
    stbtt_InitFont(&font, constData_Block(fontData), 0);
    int ascent;
    stbtt_GetFontVMetrics(&font, &ascent, NULL, NULL);
    iDeclareType(CharBuf);
    struct Impl_CharBuf {
        uint8_t *pixels;
        iInt2 size;
        int dy;
        int advance;
    };
    iArray *    chars     = collectNew_Array(sizeof(iCharBuf));
    int         pxRatio   = (mode == quadrants_TextBlockMode ? 2 : 1);
    int         pxHeight  = height * pxRatio;
    const float scale     = stbtt_ScaleForPixelHeight(&font, pxHeight);
    const float xScale    = scale * 2; /* character aspect ratio */
    const int   baseline  = ascent * scale;
    int         width     = 0;
    size_t      strRemain = length_String(text);
    iConstForEach(String, i, text) {
        if (!strRemain) break;
        if (isVariationSelector_Char(i.value) || isDefaultIgnorable_Char(i.value)) {
            strRemain--;
            continue;
        }
        iCharBuf buf;
        buf.pixels = stbtt_GetCodepointBitmap(
            &font, xScale, scale, i.value, &buf.size.x, &buf.size.y, 0, &buf.dy);
        stbtt_GetCodepointHMetrics(&font, i.value, &buf.advance, NULL);
        buf.advance *= xScale;
        if (!isSpace_Char(i.value)) {
            if (mode == quadrants_TextBlockMode) {
                buf.advance = (buf.size.x - 1) / 2 * 2 + 2;
            }
            else {
                buf.advance = buf.size.x + 1;
            }
        }
        pushBack_Array(chars, &buf);
        collect_Garbage(buf.pixels, freeBitmap_);
        width += buf.advance;
        strRemain--;
    }
    const size_t len = (mode == quadrants_TextBlockMode ? height * ((width + 1) / 2 + 1)
                                                        : (height * (width + 1)));
    iChar *outBuf = iCollectMem(malloc(sizeof(iChar) * len));
    for (size_t i = 0; i < len; ++i) {
        outBuf[i] = 0x20;
    }
    iChar *outPos = outBuf;
    for (int y = 0; y < pxHeight; y += pxRatio) {
        const iCharBuf *ch = constData_Array(chars);
        int lx = 0;
        for (int x = 0; x < width; x += pxRatio, lx += pxRatio) {
            if (lx >= ch->advance) {
                ch++;
                lx = 0;
            }
            const int ly = y - baseline - ch->dy;
            if (mode == quadrants_TextBlockMode) {
                #define checkPixel_(offx, offy) \
                    (lx + offx < ch->size.x && ly + offy < ch->size.y && ly + offy >= 0 ? \
                        ch->pixels[(lx + offx) + (ly + offy) * ch->size.x] > 155 \
                        : iFalse)
                const int mask = (checkPixel_(0, 0) ? 1 : 0) |
                                 (checkPixel_(1, 0) ? 2 : 0) |
                                 (checkPixel_(0, 1) ? 4 : 0) |
                                 (checkPixel_(1, 1) ? 8 : 0);
                #undef checkPixel_
                static const iChar blocks[16] = { 0x0020, 0x2598, 0x259D, 0x2580, 0x2596, 0x258C,
                                                  0x259E, 0x259B, 0x2597, 0x259A, 0x2590, 0x259C,
                                                  0x2584, 0x2599, 0x259F, 0x2588 };
                *outPos++ = blocks[mask];
            }
            else {
                static const iChar shades[5] = { 0x0020, 0x2591, 0x2592, 0x2593, 0x2588 };
                *outPos++ = shades[lx < ch->size.x && ly < ch->size.y && ly >= 0 ?
                                   ch->pixels[lx + ly * ch->size.x] * 5 / 256 : 0];
            }
        }
        *outPos++ = '\n';
    }
    /* We could compose the lines separately, but we'd still need to convert them to Strings
       individually to trim them. */
    iStringList *lines = split_String(collect_String(newUnicodeN_String(outBuf, len)), "\n");
    while (!isEmpty_StringList(lines) &&
           isEmpty_String(collect_String(trimmed_String(at_StringList(lines, 0))))) {
        popFront_StringList(lines);
    }
    while (!isEmpty_StringList(lines) && isEmpty_String(collect_String(trimmed_String(
                                             at_StringList(lines, size_StringList(lines) - 1))))) {
        popBack_StringList(lines);
    }
    iEndCollect();
    return joinCStr_StringList(iClob(lines), "\n");
}

/*-----------------------------------------------------------------------------------------------*/

iDefineTypeConstructionArgs(TextBuf, (int font, int color, const char *text), font, color, text)

static void initWrap_TextBuf_(iTextBuf *d, int font, int color, int maxWidth, iBool doWrap, const char *text) {
    SDL_Renderer *render = text_.render;
    if (maxWidth == 0) {
        d->size = advance_Text(font, text);
    }
    else {
        d->size = zero_I2();
        iRangecc content = range_CStr(text);
        while (!isEmpty_Range(&content)) {
            const iInt2 size = (doWrap ? tryAdvance_Text(font, content, maxWidth, &content.start)
                                 : tryAdvanceNoWrap_Text(font, content, maxWidth, &content.start));
            d->size.x = iMax(d->size.x, size.x);
            d->size.y += iMax(size.y, lineHeight_Text(font));
        }
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if (d->size.x * d->size.y) {
        d->texture = SDL_CreateTexture(render,
                                       SDL_PIXELFORMAT_RGBA4444,
                                       SDL_TEXTUREACCESS_STATIC | SDL_TEXTUREACCESS_TARGET,
                                       d->size.x,
                                       d->size.y);
    }
    else {
        d->texture = NULL;
    }
    if (d->texture) {
        SDL_Texture *oldTarget = SDL_GetRenderTarget(render);
        SDL_SetRenderTarget(render, d->texture);
        SDL_SetTextureBlendMode(text_.cache, SDL_BLENDMODE_NONE); /* blended when TextBuf is drawn */
        SDL_SetRenderDrawColor(text_.render, 0, 0, 0, 0);
        SDL_RenderClear(text_.render);
        const int fg    = color | fillBackground_ColorId;
        iRangecc  range = range_CStr(text);
        if (maxWidth == 0) {
            draw_Text_(font, zero_I2(), fg, range);
        }
        else if (doWrap) {
            drawWrapRange_Text(font, zero_I2(), maxWidth, fg, range);
        }
        else {
            iInt2 pos = zero_I2();
            while (!isEmpty_Range(&range)) {
                const char *endp;
                tryAdvanceNoWrap_Text(font, range, maxWidth, &endp);
                draw_Text_(font, pos, fg, (iRangecc){ range.start, endp });
                range.start = endp;
                pos.y += lineHeight_Text(font);
            }
        }
        SDL_SetTextureBlendMode(text_.cache, SDL_BLENDMODE_BLEND);
        SDL_SetRenderTarget(render, oldTarget);
        SDL_SetTextureBlendMode(d->texture, SDL_BLENDMODE_BLEND);
    }
}

void init_TextBuf(iTextBuf *d, int font, int color, const char *text) {
    initWrap_TextBuf_(d, font, color, 0, iFalse, text);
}

void deinit_TextBuf(iTextBuf *d) {
    SDL_DestroyTexture(d->texture);
}

iTextBuf *newBound_TextBuf(int font, int color, int boundWidth, const char *text) {
    iTextBuf *d = iMalloc(TextBuf);
    initWrap_TextBuf_(d, font, color, boundWidth, iFalse, text);
    return d;
}

iTextBuf *newWrap_TextBuf(int font, int color, int wrapWidth, const char *text) {
    iTextBuf *d = iMalloc(TextBuf);
    initWrap_TextBuf_(d, font, color, wrapWidth, iTrue, text);
    return d;
}

void draw_TextBuf(const iTextBuf *d, iInt2 pos, int color) {
    const iColor clr = get_Color(color);
    SDL_SetTextureColorMod(d->texture, clr.r, clr.g, clr.b);
    SDL_RenderCopy(text_.render,
                   d->texture,
                   &(SDL_Rect){ 0, 0, d->size.x, d->size.y },
                   &(SDL_Rect){ pos.x, pos.y, d->size.x, d->size.y });
}
