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
#include "app.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "../stb_truetype.h"

#include <the_Foundation/array.h>
#include <the_Foundation/file.h>
#include <the_Foundation/hash.h>
#include <the_Foundation/math.h>
#include <the_Foundation/stringlist.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/path.h>
#include <the_Foundation/vec2.h>

#include <SDL_surface.h>
#include <SDL_hints.h>
#include <stdarg.h>

iDeclareType(Font)
iDeclareType(Glyph)
iDeclareTypeConstructionArgs(Glyph, iChar ch)

static const float contentScale_Text_ = 1.3f;

int gap_Text;                           /* cf. gap_UI in metrics.h */
int enableHalfPixelGlyphs_Text = iTrue; /* debug setting */
int enableKerning_Text         = iTrue; /* looking up kern pairs is slow */

static iBool enableRaster_Text_ = iTrue;
static int   numPendingRasterization_Text_ = 0;

enum iGlyphFlag {
    rasterized0_GlyphFlag = iBit(1),    /* zero offset */
    rasterized1_GlyphFlag = iBit(2),    /* half-pixel offset */
};

struct Impl_Glyph {
    iHashNode node;
    int flags;
    uint32_t glyphIndex;
    const iFont *font; /* may come from symbols/emoji */
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
    enum iFontId   symbolsFont;  /* font to use for symbols */
    enum iFontId   japaneseFont; /* font to use for Japanese glyphs */
    enum iFontId   koreanFont;   /* font to use for Korean glyphs */
    uint32_t       indexTable[128 - 32];
};

static iFont *font_Text_(enum iFontId id);

static void init_Font(iFont *d, const iBlock *data, int height, float scale,
                      enum iFontId symbolsFont, iBool isMonospaced) {
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
    d->vertOffset   = height * (1.0f - scale) / 2;
    d->baseline     = ascent * d->yScale;
    d->symbolsFont  = symbolsFont;
    d->japaneseFont = regularJapanese_FontId;
    d->koreanFont   = regularKorean_FontId;
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

static iText text_;

static void initFonts_Text_(iText *d) {
    const float textSize = fontSize_UI * d->contentFontSize;
    const float monoSize = textSize * 0.71f;
    const float smallMonoSize = monoSize * 0.8f;
    const iBlock *regularFont  = &fontNunitoRegular_Embedded;
    const iBlock *italicFont   = &fontNunitoLightItalic_Embedded;
    const iBlock *h12Font      = &fontNunitoExtraBold_Embedded;
    const iBlock *h3Font       = &fontNunitoRegular_Embedded;
    const iBlock *lightFont    = &fontNunitoExtraLight_Embedded;
    float         scaling      = 1.0f; /* glyph scaling (<=1.0), for increasing line spacing */
    float         lightScaling = 1.0f;
    float         h123Scaling  = 1.0f; /* glyph scaling (<=1.0), for increasing line spacing */
    if (d->contentFont == firaSans_TextFont) {
        regularFont = &fontFiraSansRegular_Embedded;
        lightFont   = &fontFiraSansLight_Embedded;
        italicFont  = &fontFiraSansItalic_Embedded;
        scaling     = lightScaling = 0.85f;
    }
    else if (d->contentFont == tinos_TextFont) {
        regularFont = &fontTinosRegular_Embedded;
        lightFont   = &fontLiterataExtraLightopsz18_Embedded;
        italicFont  = &fontTinosItalic_Embedded;
        scaling      = 0.85f;
    }
    else if (d->contentFont == literata_TextFont) {
        regularFont = &fontLiterataRegularopsz14_Embedded;
        italicFont  = &fontLiterataLightItalicopsz10_Embedded;
        lightFont   = &fontLiterataExtraLightopsz18_Embedded;
    }
    else if (d->contentFont == sourceSansPro_TextFont) {
        regularFont = &fontSourceSansProRegular_Embedded;
        italicFont  = &fontFiraSansItalic_Embedded;
        lightFont   = &fontFiraSansLight_Embedded;
        lightScaling = 0.85f;
    }
    else if (d->contentFont == iosevka_TextFont) {
        regularFont = &fontIosevkaTermExtended_Embedded;
        italicFont  = &fontIosevkaTermExtended_Embedded;
        lightFont   = &fontIosevkaTermExtended_Embedded;
        scaling     = lightScaling = 0.866f;
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
    else if (d->headingFont == sourceSansPro_TextFont) {
        h12Font = &fontSourceSansProBold_Embedded;
        h3Font = &fontSourceSansProRegular_Embedded;
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
        int symbolsFont;
    } fontData[max_FontId] = {
        { &fontSourceSansProRegular_Embedded, uiSize,               1.0f, defaultSymbols_FontId },
        { &fontSourceSansProBold_Embedded,    uiSize,               1.0f, defaultSymbols_FontId },
        { &fontSourceSansProRegular_Embedded, uiSize * 1.125f,      1.0f, defaultMediumSymbols_FontId },
        { &fontSourceSansProBold_Embedded,    uiSize * 1.125f,      1.0f, defaultMediumSymbols_FontId },
        { &fontSourceSansProRegular_Embedded, uiSize * 1.333f,      1.0f, defaultBigSymbols_FontId },
        { &fontSourceSansProBold_Embedded,    uiSize * 1.333f,      1.0f, defaultBigSymbols_FontId },
        { &fontSourceSansProRegular_Embedded, uiSize * 1.666f,      1.0f, defaultLargeSymbols_FontId },
        { &fontSourceSansProBold_Embedded,    uiSize * 1.666f,      1.0f, defaultLargeSymbols_FontId },
        { &fontIosevkaTermExtended_Embedded,  uiSize * 0.866f,      1.0f, defaultSymbols_FontId },
        { &fontSourceSansProRegular_Embedded, textSize,             scaling, symbols_FontId },
        /* content fonts */
        { regularFont,                        textSize,             scaling,      symbols_FontId },
        { &fontIosevkaTermExtended_Embedded,  monoSize,             1.0f,         monospaceSymbols_FontId },
        { &fontIosevkaTermExtended_Embedded,  smallMonoSize,        1.0f,         monospaceSmallSymbols_FontId },
        { regularFont,                        textSize * 1.200f,    scaling,      mediumSymbols_FontId },
        { h3Font,                             textSize * 1.333f,    h123Scaling,  bigSymbols_FontId },
        { italicFont,                         textSize,             scaling,      symbols_FontId },
        { h12Font,                            textSize * 1.666f,    h123Scaling,  largeSymbols_FontId },
        { h12Font,                            textSize * 2.000f,    h123Scaling,  hugeSymbols_FontId },
        { lightFont,                          textSize * 1.666f,    lightScaling, largeSymbols_FontId },
        /* monospace content fonts */
        { &fontIosevkaTermExtended_Embedded,  textSize,             0.866f, symbols_FontId },
        /* symbol fonts */
        { &fontSymbola_Embedded,              uiSize,               1.0f, defaultSymbols_FontId },
        { &fontSymbola_Embedded,              uiSize * 1.125f,      1.0f, defaultMediumSymbols_FontId },
        { &fontSymbola_Embedded,              uiSize * 1.333f,      1.0f, defaultBigSymbols_FontId },
        { &fontSymbola_Embedded,              uiSize * 1.666f,      1.0f, defaultLargeSymbols_FontId },
        { &fontSymbola_Embedded,              textSize,             1.0f, symbols_FontId },
        { &fontSymbola_Embedded,              textSize * 1.200f,    1.0f, mediumSymbols_FontId },
        { &fontSymbola_Embedded,              textSize * 1.333f,    1.0f, bigSymbols_FontId },
        { &fontSymbola_Embedded,              textSize * 1.666f,    1.0f, largeSymbols_FontId },
        { &fontSymbola_Embedded,              textSize * 2.000f,    1.0f, hugeSymbols_FontId },
        { &fontSymbola_Embedded,              monoSize,             1.0f, monospaceSymbols_FontId },
        { &fontSymbola_Embedded,              smallMonoSize,        1.0f, monospaceSmallSymbols_FontId },
        /* emoji fonts */
        { &fontNotoEmojiRegular_Embedded,     uiSize,               1.0f, defaultSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     uiSize * 1.125f,      1.0f, defaultMediumSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     uiSize * 1.333f,      1.0f, defaultBigSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     uiSize * 1.666f,      1.0f, defaultLargeSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     textSize,             1.0f, symbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     textSize * 1.200f,    1.0f, mediumSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     textSize * 1.333f,    1.0f, bigSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     textSize * 1.666f,    1.0f, largeSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     textSize * 2.000f,    1.0f, hugeSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     monoSize,             1.0f, monospaceSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     smallMonoSize,        1.0f, monospaceSmallSymbols_FontId },
        /* japanese fonts */
        { &fontNotoSansJPRegular_Embedded,    uiSize,               1.0f, defaultSymbols_FontId },
        { &fontNotoSansJPRegular_Embedded,    smallMonoSize,        1.0f, monospaceSmallSymbols_FontId },
        { &fontNotoSansJPRegular_Embedded,    monoSize,             1.0f, monospaceSymbols_FontId },
        { &fontNotoSansJPRegular_Embedded,    textSize,             1.0f, symbols_FontId },
        { &fontNotoSansJPRegular_Embedded,    textSize * 1.200f,    1.0f, mediumSymbols_FontId },
        { &fontNotoSansJPRegular_Embedded,    textSize * 1.333f,    1.0f, bigSymbols_FontId },
        { &fontNotoSansJPRegular_Embedded,    textSize * 1.666f,    1.0f, largeSymbols_FontId },
        { &fontNotoSansJPRegular_Embedded,    textSize * 2.000f,    1.0f, hugeSymbols_FontId },
        /* korean fonts */
        { &fontNanumGothicRegular_Embedded,   uiSize,               1.0f, defaultSymbols_FontId },
        { &fontNanumGothicRegular_Embedded,   smallMonoSize,        1.0f, monospaceSmallSymbols_FontId },
        { &fontNanumGothicRegular_Embedded,   monoSize,             1.0f, monospaceSymbols_FontId },
        { &fontNanumGothicRegular_Embedded,   textSize,             1.0f, symbols_FontId },
        { &fontNanumGothicRegular_Embedded,   textSize * 1.200f,    1.0f, mediumSymbols_FontId },
        { &fontNanumGothicRegular_Embedded,   textSize * 1.333f,    1.0f, bigSymbols_FontId },
        { &fontNanumGothicRegular_Embedded,   textSize * 1.666f,    1.0f, largeSymbols_FontId },
        { &fontNanumGothicRegular_Embedded,   textSize * 2.000f,    1.0f, hugeSymbols_FontId },
    };
    iForIndices(i, fontData) {
        iFont *font = &d->fonts[i];
        init_Font(font,
                  fontData[i].ttf,
                  fontData[i].size,
                  fontData[i].scaling,
                  fontData[i].symbolsFont,
                  fontData[i].ttf == &fontIosevkaTermExtended_Embedded);
        if (i == default_FontId || i == defaultMedium_FontId) {
            font->manualKernOnly = iTrue;
        }
    }
    /* Japanese script. */ {
        /* Everything defaults to the regular sized japanese font, so these are just
           the other sizes. */
        font_Text_(default_FontId)->japaneseFont          = defaultJapanese_FontId;
        font_Text_(defaultMedium_FontId)->japaneseFont    = defaultJapanese_FontId;
        font_Text_(defaultBig_FontId)->japaneseFont       = defaultJapanese_FontId;
        font_Text_(defaultLarge_FontId)->japaneseFont     = defaultJapanese_FontId;
        font_Text_(defaultMonospace_FontId)->japaneseFont = defaultJapanese_FontId;
        font_Text_(monospaceSmall_FontId)->japaneseFont   = monospaceSmallJapanese_FontId;
        font_Text_(monospace_FontId)->japaneseFont        = monospaceJapanese_FontId;
        font_Text_(medium_FontId)->japaneseFont           = mediumJapanese_FontId;
        font_Text_(big_FontId)->japaneseFont              = bigJapanese_FontId;
        font_Text_(largeBold_FontId)->japaneseFont        = largeJapanese_FontId;
        font_Text_(largeLight_FontId)->japaneseFont       = largeJapanese_FontId;
        font_Text_(hugeBold_FontId)->japaneseFont         = hugeJapanese_FontId;
    }
    /* Korean script. */ {
        font_Text_(default_FontId)->koreanFont          = defaultKorean_FontId;
        font_Text_(defaultMedium_FontId)->koreanFont    = defaultKorean_FontId;
        font_Text_(defaultBig_FontId)->koreanFont       = defaultKorean_FontId;
        font_Text_(defaultLarge_FontId)->koreanFont     = defaultKorean_FontId;
        font_Text_(defaultMonospace_FontId)->koreanFont = defaultKorean_FontId;
        font_Text_(monospaceSmall_FontId)->koreanFont   = monospaceSmallKorean_FontId;
        font_Text_(monospace_FontId)->koreanFont        = monospaceKorean_FontId;
        font_Text_(medium_FontId)->koreanFont           = mediumKorean_FontId;
        font_Text_(big_FontId)->koreanFont              = bigKorean_FontId;
        font_Text_(largeBold_FontId)->koreanFont        = largeKorean_FontId;
        font_Text_(largeLight_FontId)->koreanFont       = largeKorean_FontId;
        font_Text_(hugeBold_FontId)->koreanFont         = hugeKorean_FontId;
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
    numPendingRasterization_Text_ = 0;
}

static void deinitCache_Text_(iText *d) {
    deinit_Array(&d->cacheRows);
    SDL_DestroyTexture(d->cache);
}

void init_Text(SDL_Renderer *render) {
    iText *d = &text_;
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

int numPendingGlyphs_Text(void) {
    return numPendingRasterization_Text_;
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
    SDL_SetSurfacePalette(surface8, text_.grayscale);
    SDL_PixelFormat *fmt = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);
    SDL_Surface *surface = SDL_ConvertSurface(surface8, fmt, 0);
    SDL_FreeFormat(fmt);
    SDL_FreeSurface(surface8);
    stbtt_FreeBitmap(bmp, NULL);
    return surface;
}

iLocalDef SDL_Rect sdlRect_(const iRect rect) {
    return (SDL_Rect){ rect.pos.x, rect.pos.y, rect.size.x, rect.size.y };
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

static iBool cache_Font_(iFont *d, iGlyph *glyph, int hoff) {
    iText *       txt     = &text_;
    SDL_Renderer *render  = txt->render;
    SDL_Texture * tex     = NULL;
    SDL_Surface * surface = NULL;
    iRect *       glRect  = &glyph->rect[hoff];
    /* Rasterize the glyph using stbtt. */
    iAssert(!isRasterized_Glyph_(glyph, hoff));
    surface = rasterizeGlyph_Font_(d, glyph->glyphIndex, hoff * 0.5f);
    tex = SDL_CreateTextureFromSurface(render, surface);
    iAssert(isEqual_I2(glRect->size, init_I2(surface->w, surface->h)));
    if (tex) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
        const SDL_Rect dstRect = sdlRect_(*glRect);
        SDL_RenderCopy(render, tex, &(SDL_Rect){ 0, 0, dstRect.w, dstRect.h }, &dstRect);
        SDL_DestroyTexture(tex);
        setRasterized_Glyph_(glyph, hoff);
    }
    if (surface) {
        SDL_FreeSurface(surface);
    }
    return isRasterized_Glyph_(glyph, hoff);
}

iLocalDef iFont *characterFont_Font_(iFont *d, iChar ch, uint32_t *glyphIndex) {
    if ((*glyphIndex = glyphIndex_Font_(d, ch)) != 0) {
        return d;
    }
    /* Not defined in current font, try Noto Emoji (for selected characters). */
    if ((ch >= 0x1f300 && ch < 0x1f600) || (ch >= 0x1f680 && ch <= 0x1f6c5)) {
        iFont *emoji = font_Text_(d->symbolsFont + fromSymbolsToEmojiOffset_FontId);
        if (emoji != d && (*glyphIndex = glyphIndex_Font_(emoji, ch)) != 0) {
            return emoji;
        }
    }
    /* Could be Korean. */
    if (ch >= 0x3000) {
        iFont *korean = font_Text_(d->koreanFont);
        if (korean != d && (*glyphIndex = glyphIndex_Font_(korean, ch)) != 0) {
            return korean;
        }
    }
    /* Japanese perhaps? */
    if (ch > 0x3040) {
        iFont *japanese = font_Text_(d->japaneseFont);
        if (japanese != d && (*glyphIndex = glyphIndex_Font_(japanese, ch)) != 0) {
            return japanese;
        }
    }
#if defined (iPlatformApple)
    /* White up arrow is used for the Shift key on macOS. Symbola's glyph is not a great
       match to the other text, so use the UI font instead. */
    if ((ch == 0x2318 || ch == 0x21e7) && d == font_Text_(regular_FontId)) {
        *glyphIndex = glyphIndex_Font_(d = font_Text_(defaultContentSized_FontId), ch);
        return d;
    }
#endif
    /* Fall back to Symbola for anything else. */
    iFont *font = font_Text_(d->symbolsFont);
    *glyphIndex = glyphIndex_Font_(font, ch);
//    if (!*glyphIndex) {
//        fprintf(stderr, "failed to find %08x (%lc)\n", ch, ch); fflush(stderr);
//    }
    return font;
}

static const iGlyph *glyph_Font_(iFont *d, iChar ch) {
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
        numPendingRasterization_Text_ += 2;
    }
    if (enableRaster_Text_ && !isFullyRasterized_Glyph_(glyph)) {
        SDL_Texture *oldTarget = SDL_GetRenderTarget(text_.render);
        SDL_SetRenderTarget(text_.render, text_.cache);
        if (!isRasterized_Glyph_(glyph, 0)) {
            if (cache_Font_(font, glyph, 0)) {
                numPendingRasterization_Text_--;
                iAssert(numPendingRasterization_Text_ >= 0);
            }
        }
        if (!isRasterized_Glyph_(glyph, 1)) {
            if (cache_Font_(font, glyph, 1)) { /* half-pixel offset */
                numPendingRasterization_Text_--;
                iAssert(numPendingRasterization_Text_ >= 0);
            }
        }
        SDL_SetRenderTarget(text_.render, oldTarget);
    }
    return glyph;
}

enum iRunMode {
    measure_RunMode    = 0,
    draw_RunMode       = 1,
    modeMask_RunMode   = 0x00ff,
    flagsMask_RunMode  = 0xff00,
    noWrapFlag_RunMode = iBit(9),
    visualFlag_RunMode = iBit(10), /* actual visible bounding box of the glyph, e.g., for icons */
    permanentColorFlag_RunMode      = iBit(11),
    alwaysVariableWidthFlag_RunMode = iBit(12),
};

static iChar nextChar_(const char **chPos, const char *end) {
    if (*chPos == end) {
        return 0;
    }
    iChar ch;
    int len = decodeBytes_MultibyteChar(*chPos, end - *chPos, &ch);
    if (len <= 0) {
        (*chPos)++; /* skip it */
        return 0;
    }
    (*chPos) += len;
    return ch;
}

static enum iFontId fontId_Text_(const iFont *font) {
    return (enum iFontId) (font - text_.fonts);
}

iLocalDef iBool isWrapBoundary_(iChar prevC, iChar c) {
    /* Line wrapping boundaries are determined by looking at a character and the
       last character processed. We want to wrap at natural word boundaries where
       possible, so normally we wrap at a space followed a non-space character. As
       an exception, we also wrap after punctuation used to break up words, so we
       can wrap text like foo/bar/baz-abc-def.xyz at any puncation boundaries,
       without wrapping on other punctuation used for expressive purposes like
       emoticons :-) */
    if (c == '.' && (prevC == '(' || prevC == '[' || prevC == '.')) {
        /* Start of a [...], perhaps? */
        return iFalse;
    }
    if (isSpace_Char(prevC)) {
        return iFalse;
    }
    if (c == '/' || c == '-' || c == ',' || c == ';' || c == ':' || c == '.' || c == 0xad) {
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
    if (args->continueFrom_out) {
        *args->continueFrom_out = args->text.end;
    }
    iChar prevCh = 0;
    const iBool isMonospaced = d->isMonospaced && !(mode & alwaysVariableWidthFlag_RunMode);
    if (isMonospaced) {
        monoAdvance = glyph_Font_(d, 'M')->advance;
    }
    /* Global flag that allows glyph rasterization. */
    enableRaster_Text_ = !isMeasuring_(mode);
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
            if (ch == '\n') {
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
            if (ch == '\r') {
                iChar esc = nextChar_(&chPos, args->text.end);
                int colorNum = args->color;
                if (esc != 0x24) { /* ASCII Cancel */
                    colorNum = esc - asciiBase_ColorEscape;
                }
                else if (esc == '\r') { /* Extended range. */
                    esc = nextChar_(&chPos, args->text.end) + asciiExtended_ColorEscape;
                    colorNum = esc - asciiBase_ColorEscape;
                }
                if (mode & draw_RunMode && ~mode & permanentColorFlag_RunMode) {
                    const iColor clr = get_Color(colorNum);
                    SDL_SetTextureColorMod(text_.cache, clr.r, clr.g, clr.b);
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
        const int hoff = enableHalfPixelGlyphs_Text ? (xpos - x1 > 0.5f ? 1 : 0) : 0;
        int x2 = x1 + glyph->rect[hoff].size.x;
        /* Out of the allotted space? */
        if (args->xposLimit > 0 && x2 > args->xposLimit) {
            if (args->continueFrom_out) {
                if (lastWordEnd != args->text.start) {
                    *args->continueFrom_out = lastWordEnd;
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
        const float advance = (useMonoAdvance ? monoAdvance : glyph->advance);
        if (!isMeasuring_(mode)) {
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
            SDL_RenderCopy(text_.render, text_.cache, &src, &dst);
        }
        xpos += advance;
        if (!isSpace_Char(ch)) {
            xposExtend += isEmoji ? glyph->advance : advance;
        }
        xposExtend = iMax(xposExtend, xpos);
        xposMax    = iMax(xposMax, xposExtend);
        if (args->continueFrom_out && ((mode & noWrapFlag_RunMode) || isWrapBoundary_(prevCh, ch))) {
            lastWordEnd = chPos;
        }
#if defined (LAGRANGE_ENABLE_KERNING)
        /* Check the next character. */
        if (!isMonospaced && glyph->font == d) {
            /* TODO: No need to decode the next char twice; check this on the next iteration. */
            const char *peek = chPos;
            const iChar next = nextChar_(&peek, args->text.end);
            if (enableKerning_Text && !d->manualKernOnly && next) {
                xpos += d->xScale * stbtt_GetGlyphKernAdvance(&d->font, glyph->glyphIndex, next);
            }
        }
#endif
        prevCh = ch;
        if (--maxLen == 0) {
            break;
        }
    }
    if (args->runAdvance_out) {
        *args->runAdvance_out = xposMax - orig.x;
    }
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
                                 &(iRunArgs){ .mode = measure_RunMode | runFlagsFromId_(fontId),
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

static void drawBounded_Text_(int fontId, iInt2 pos, int xposBound, int color, iRangecc text) {
    iText *d = &text_;
    const iColor clr = get_Color(color & mask_ColorId);
    SDL_SetTextureColorMod(d->cache, clr.r, clr.g, clr.b);
    run_Font_(font_Text_(fontId),
              &(iRunArgs){ .mode = draw_RunMode |
                                   (color & permanent_ColorId ? permanentColorFlag_RunMode : 0) |
                                   runFlagsFromId_(fontId),
                           .text            = text,
                           .pos             = pos,
                           .xposLayoutBound = xposBound,
                           .color           = color });
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
        pos.y += adv.y;
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
    const iRangecc text       = range_Block(&chars);
    iRect          textBounds = alignVisual ? visualBounds_Text(fontId, text)
                                   : (iRect){ zero_I2(), advanceRange_Text(fontId, text) };
    textBounds.pos = sub_I2(mid_Rect(rect), mid_Rect(textBounds));
    textBounds.pos.x = iMax(textBounds.pos.x, left_Rect(rect)); /* keep left edge visible */
    draw_Text_(fontId, textBounds.pos, color, text);
    deinit_Block(&chars);
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

iDefineTypeConstructionArgs(TextBuf, (int font, const char *text), font, text)

void init_TextBuf(iTextBuf *d, int font, const char *text) {
    SDL_Renderer *render = text_.render;
    d->size    = advance_Text(font, text);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    d->texture = SDL_CreateTexture(render,
                                   SDL_PIXELFORMAT_RGBA4444,
                                   SDL_TEXTUREACCESS_STATIC | SDL_TEXTUREACCESS_TARGET,
                                   d->size.x,
                                   d->size.y);
    SDL_Texture *oldTarget = SDL_GetRenderTarget(render);
    SDL_SetRenderTarget(render, d->texture);
    SDL_SetTextureBlendMode(text_.cache, SDL_BLENDMODE_NONE); /* blended when TextBuf is drawn */
    SDL_SetRenderDrawColor(text_.render, 255, 255, 255, 0);
    SDL_RenderClear(text_.render);
    draw_Text_(font, zero_I2(), white_ColorId, range_CStr(text));
    SDL_SetTextureBlendMode(text_.cache, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(render, oldTarget);
    SDL_SetTextureBlendMode(d->texture, SDL_BLENDMODE_BLEND);
}

void deinit_TextBuf(iTextBuf *d) {
    SDL_DestroyTexture(d->texture);
}

void draw_TextBuf(const iTextBuf *d, iInt2 pos, int color) {
    const iColor clr = get_Color(color);
    SDL_SetTextureColorMod(d->texture, clr.r, clr.g, clr.b);
    SDL_RenderCopy(text_.render,
                   d->texture,
                   &(SDL_Rect){ 0, 0, d->size.x, d->size.y },
                   &(SDL_Rect){ pos.x, pos.y, d->size.x, d->size.y });
}
