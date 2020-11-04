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

struct Impl_Glyph {
    iHashNode node;
    uint32_t glyphIndex;
    const iFont *font; /* may come from symbols/emoji */
    iRect rect[2]; /* zero and half pixel offset */
    iInt2 d[2];
    float advance; /* scaled */
};

void init_Glyph(iGlyph *d, iChar ch) {
    d->node.key   = ch;
    d->glyphIndex = 0;
    d->font       = NULL;
    d->rect[0]    = zero_Rect();
    d->rect[1]    = zero_Rect();
    d->advance    = 0.0f;
}

void deinit_Glyph(iGlyph *d) {
    iUnused(d);
}

iChar char_Glyph(const iGlyph *d) {
    return d->node.key;
}

iDefineTypeConstructionArgs(Glyph, (iChar ch), ch)

    /*-----------------------------------------------------------------------------------------------*/

    struct Impl_Font {
    iBlock *       data;
    stbtt_fontinfo font;
    float          scale;
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

static void init_Font(iFont *d, const iBlock *data, int height, float scale, enum iFontId symbolsFont) {
    init_Hash(&d->glyphs);
    d->data = NULL;
    d->height = height;
    iZap(d->font);
    stbtt_InitFont(&d->font, constData_Block(data), 0);
    d->scale = stbtt_ScaleForPixelHeight(&d->font, height) * scale;
    d->vertOffset = height * (1.0f - scale) / 2;
    int ascent;
    stbtt_GetFontVMetrics(&d->font, &ascent, NULL, NULL);
    d->baseline     = (int) ascent * d->scale;
    d->symbolsFont  = symbolsFont;
    d->japaneseFont = regularJapanese_FontId;
    d->koreanFont   = regularKorean_FontId;
    d->isMonospaced = iFalse;
    memset(d->indexTable, 0xff, sizeof(d->indexTable));
}

static void deinit_Font(iFont *d) {
    iForEach(Hash, i, &d->glyphs) {
        delete_Glyph((iGlyph *) i.value);
    }
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
    const float monoSize = fontSize_UI * d->contentFontSize / contentScale_Text_ * 0.866f;
    const iBlock *regularFont = &fontNunitoRegular_Embedded;
    const iBlock *italicFont  = &fontNunitoLightItalic_Embedded;
    const iBlock *h12Font     = &fontNunitoExtraBold_Embedded;
    const iBlock *h3Font      = &fontNunitoRegular_Embedded;
    const iBlock *lightFont   = &fontNunitoExtraLight_Embedded;
    float         scaling     = 1.0f; /* glyph scaling (<=1.0), for increasing line spacing */
    float         h123Scaling = 1.0f; /* glyph scaling (<=1.0), for increasing line spacing */
    if (d->contentFont == firaSans_TextFont) {
        regularFont = &fontFiraSansRegular_Embedded;
        lightFont   = &fontFiraSansLight_Embedded;
        italicFont  = &fontFiraSansItalic_Embedded;
        scaling     = 0.85f;
    }
    else if (d->contentFont == ebGaramond_TextFont) {
        regularFont = &fontEBGaramondRegular_Embedded;
        lightFont   = &fontEBGaramondRegular_Embedded;
        italicFont  = &fontEBGaramondItalic_Embedded;
    }
    else if (d->contentFont == literata_TextFont) {
        regularFont = &fontLiterataRegularopsz14_Embedded;
        italicFont  = &fontLiterataLightItalicopsz10_Embedded;
        lightFont   = &fontLiterataExtraLightopsz18_Embedded;
    }
    if (d->headingFont == firaSans_TextFont) {
        h12Font     = &fontFiraSansBold_Embedded;
        h3Font      = &fontFiraSansRegular_Embedded;
        h123Scaling = 0.85f;
    }
    else if (d->headingFont == ebGaramond_TextFont) {
        h12Font = &fontEBGaramondBold_Embedded;
        h3Font  = &fontEBGaramondRegular_Embedded;
    }
    else if (d->headingFont == literata_TextFont) {
        h12Font = &fontLiterataBoldopsz36_Embedded;
        h3Font  = &fontLiterataRegularopsz14_Embedded;
    }
    const struct {
        const iBlock *ttf;
        int size;
        float scaling;
        int symbolsFont;
    } fontData[max_FontId] = {
        { &fontSourceSansProRegular_Embedded, fontSize_UI,          1.0f, defaultSymbols_FontId },
        { &fontSourceSansProRegular_Embedded, fontSize_UI * 1.125f, 1.0f, defaultMediumSymbols_FontId },
        { &fontSourceSansProRegular_Embedded, fontSize_UI * 1.666f, 1.0f, defaultLargeSymbols_FontId },
        { &fontFiraMonoRegular_Embedded,      fontSize_UI * 0.866f, 1.0f, defaultSymbols_FontId },
        /* content fonts */
        { regularFont,                        textSize,             scaling, symbols_FontId },
        { &fontFiraMonoRegular_Embedded,      monoSize,             1.0f,    monospaceSymbols_FontId },
        { &fontFiraMonoRegular_Embedded,      monoSize * 0.750f,    1.0f,    monospaceSmallSymbols_FontId },
        { regularFont,                        textSize * 1.200f,    scaling, mediumSymbols_FontId },
        { h3Font,                             textSize * 1.333f,    scaling, bigSymbols_FontId },
        { italicFont,                         textSize,             scaling, symbols_FontId },
        { h12Font,                            textSize * 1.666f,    h123Scaling, largeSymbols_FontId },
        { h12Font,                            textSize * 2.000f,    h123Scaling, hugeSymbols_FontId },
        { lightFont,                          textSize * 1.666f,    scaling,     largeSymbols_FontId },
        /* symbol fonts */
        { &fontSymbola_Embedded,              fontSize_UI,          1.0f, defaultSymbols_FontId },
        { &fontSymbola_Embedded,              fontSize_UI * 1.125f, 1.0f, defaultMediumSymbols_FontId },
        { &fontSymbola_Embedded,              fontSize_UI * 1.666f, 1.0f, defaultLargeSymbols_FontId },
        { &fontSymbola_Embedded,              textSize,             1.0f, symbols_FontId },
        { &fontSymbola_Embedded,              textSize * 1.200f,    1.0f, mediumSymbols_FontId },
        { &fontSymbola_Embedded,              textSize * 1.333f,    1.0f, bigSymbols_FontId },
        { &fontSymbola_Embedded,              textSize * 1.666f,    1.0f, largeSymbols_FontId },
        { &fontSymbola_Embedded,              textSize * 2.000f,    1.0f, hugeSymbols_FontId },
        { &fontSymbola_Embedded,              monoSize,             1.0f, monospaceSymbols_FontId },
        { &fontSymbola_Embedded,              monoSize * 0.750f,    1.0f, monospaceSmallSymbols_FontId },
        /* emoji fonts */
        { &fontNotoEmojiRegular_Embedded,     fontSize_UI,          1.0f, defaultSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     fontSize_UI * 1.125f, 1.0f, defaultMediumSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     fontSize_UI * 1.666f, 1.0f, defaultLargeSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     textSize,             1.0f, symbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     textSize * 1.200f,    1.0f, mediumSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     textSize * 1.333f,    1.0f, bigSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     textSize * 1.666f,    1.0f, largeSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     textSize * 2.000f,    1.0f, hugeSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     monoSize,             1.0f, monospaceSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     monoSize * 0.750f,    1.0f, monospaceSmallSymbols_FontId },
        /* japanese fonts */
        { &fontKosugiMaruRegular_Embedded,    fontSize_UI,          1.0f, defaultSymbols_FontId },
        { &fontKosugiMaruRegular_Embedded,    monoSize * 0.750,     1.0f, monospaceSmallSymbols_FontId },
        { &fontKosugiMaruRegular_Embedded,    monoSize,             1.0f, monospaceSymbols_FontId },
        { &fontKosugiMaruRegular_Embedded,    textSize,             1.0f, symbols_FontId },
        { &fontKosugiMaruRegular_Embedded,    textSize * 1.200f,    1.0f, mediumSymbols_FontId },
        { &fontKosugiMaruRegular_Embedded,    textSize * 1.333f,    1.0f, bigSymbols_FontId },
        { &fontKosugiMaruRegular_Embedded,    textSize * 1.666f,    1.0f, largeSymbols_FontId },
        { &fontKosugiMaruRegular_Embedded,    textSize * 2.000f,    1.0f, hugeSymbols_FontId },
        /* korean fonts */
        { &fontNanumGothicRegular_Embedded,   fontSize_UI,          1.0f, defaultSymbols_FontId },
        { &fontNanumGothicRegular_Embedded,   monoSize * 0.750,     1.0f, monospaceSmallSymbols_FontId },
        { &fontNanumGothicRegular_Embedded,   monoSize,             1.0f, monospaceSymbols_FontId },
        { &fontNanumGothicRegular_Embedded,   textSize,             1.0f, symbols_FontId },
        { &fontNanumGothicRegular_Embedded,   textSize * 1.200f,    1.0f, mediumSymbols_FontId },
        { &fontNanumGothicRegular_Embedded,   textSize * 1.333f,    1.0f, bigSymbols_FontId },
        { &fontNanumGothicRegular_Embedded,   textSize * 1.666f,    1.0f, largeSymbols_FontId },
        { &fontNanumGothicRegular_Embedded,   textSize * 2.000f,    1.0f, hugeSymbols_FontId },
    };
    iForIndices(i, fontData) {
        iFont *font = &d->fonts[i];
        init_Font(
            font, fontData[i].ttf, fontData[i].size, fontData[i].scaling, fontData[i].symbolsFont);
        if (fontData[i].ttf == &fontFiraMonoRegular_Embedded) {
            font->isMonospaced = iTrue;
        }
        if (i == default_FontId || i == defaultMedium_FontId) {
            font->manualKernOnly = iTrue;
        }
    }
    /* Japanese script. */ {
        /* Everything defaults to the regular sized japanese font, so these are just
           the other sizes. */
        font_Text_(default_FontId)->japaneseFont          = defaultJapanese_FontId;
        font_Text_(defaultMedium_FontId)->japaneseFont    = defaultJapanese_FontId;
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

static void initCache_Text_(iText *d) {
    init_Array(&d->cacheRows, sizeof(iCacheRow));
    const int textSize = d->contentFontSize * fontSize_UI;
    iAssert(textSize > 0);
    const iInt2 cacheDims = init_I2(16, 80);
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
    for (int h = d->cacheRowAllocStep; h <= 2 * textSize; h += d->cacheRowAllocStep) {
        pushBack_Array(&d->cacheRows, &(iCacheRow){ .height = 0 });
    }
    d->cacheBottom = 0;
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

void init_Text(SDL_Renderer *render) {
    iText *d = &text_;
    d->contentFont     = nunito_TextFont;
    d->headingFont     = nunito_TextFont;
    d->contentFontSize = contentScale_Text_;    
    d->ansiEscape      = new_RegExp("\\[([0-9;]+)m", 0);
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

void resetFonts_Text(void) {
    iText *d = &text_;
    deinitFonts_Text_(d);
    deinitCache_Text_(d);
    initCache_Text_(d);
    initFonts_Text_(d);
}

iLocalDef iFont *font_Text_(enum iFontId id) {
    return &text_.fonts[id];
}

static void freeBmp_(void *ptr) {
    stbtt_FreeBitmap(ptr, NULL);
}

static SDL_Surface *rasterizeGlyph_Font_(const iFont *d, uint32_t glyphIndex, float xShift) {
    int w, h;
    uint8_t *bmp = stbtt_GetGlyphBitmapSubpixel(
        &d->font, d->scale, d->scale, xShift, 0.0f, glyphIndex, &w, &h, 0, 0);
    /* Note: `bmp` must be freed afterwards. */
    collect_Garbage(bmp, freeBmp_);
    SDL_Surface *surface8 =
        SDL_CreateRGBSurfaceWithFormatFrom(bmp, w, h, 8, w, SDL_PIXELFORMAT_INDEX8);
    SDL_SetSurfacePalette(surface8, text_.grayscale);
    SDL_PixelFormat *fmt = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);
    SDL_Surface *surface = SDL_ConvertSurface(surface8, fmt, 0);
    SDL_FreeFormat(fmt);
    SDL_FreeSurface(surface8);
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

static void cache_Font_(iFont *d, iGlyph *glyph, int hoff) {
    iText *txt = &text_;
    SDL_Renderer *render = txt->render;
    SDL_Texture *tex = NULL;
    SDL_Surface *surface = NULL;
    iRect *glRect = &glyph->rect[hoff];
    /* Rasterize the glyph using stbtt. */ {
        surface = rasterizeGlyph_Font_(d, glyph->glyphIndex, hoff * 0.5f);
        if (hoff == 0) {
            int adv;
            const uint32_t gIndex = glyph->glyphIndex;
//            float advScale = d->scale;
//            if (isJapanese_FontId(d - text_.fonts)) {
                /* Treat as monospace. */
//                gIndex = stbtt_FindGlyphIndex(&d->font, 0x5712);
//                advScale *= 2.0f;
//            }
            stbtt_GetGlyphHMetrics(&d->font, gIndex, &adv, NULL);
            glyph->advance = d->scale * adv;
        }
        stbtt_GetGlyphBitmapBoxSubpixel(&d->font,
                                        glyph->glyphIndex,
                                        d->scale,
                                        d->scale,
                                        hoff * 0.5f,
                                        0.0f,
                                        &glyph->d[hoff].x,
                                        &glyph->d[hoff].y,
                                        NULL,
                                        NULL);
        glyph->d[hoff].y += d->vertOffset;
        tex = SDL_CreateTextureFromSurface(render, surface);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
        glRect->size = init_I2(surface->w, surface->h);
    }
    /* Determine placement in the glyph cache texture, advancing in rows. */
    glRect->pos = assignCachePos_Text_(txt, glRect->size);
    const SDL_Rect dstRect = sdlRect_(*glRect);
    SDL_RenderCopy(render, tex, &(SDL_Rect){ 0, 0, dstRect.w, dstRect.h }, &dstRect);
    if (tex) {
        SDL_DestroyTexture(tex);
        iAssert(surface);
        SDL_FreeSurface(surface);
    }
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
    if (ch > 0x3000) {
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
    /* Fall back to Symbola for anything else. */
    iFont *font = font_Text_(d->symbolsFont);
    *glyphIndex = glyphIndex_Font_(font, ch);
//    if (!*glyphIndex) {
//        fprintf(stderr, "failed to find %08x (%lc)\n", ch, ch); fflush(stderr);
//    }
    return font;
}

static const iGlyph *glyph_Font_(iFont *d, iChar ch) {
    /* It may actually come from a different font. */
    uint32_t glyphIndex = 0;
    iFont *font = characterFont_Font_(d, ch, &glyphIndex);
    const void *node = value_Hash(&font->glyphs, ch);
    if (node) {
        return node;
    }
    iGlyph *glyph     = new_Glyph(ch);
    glyph->glyphIndex = glyphIndex;
    glyph->font       = font;
    SDL_Texture *oldTarget = SDL_GetRenderTarget(text_.render);
    SDL_SetRenderTarget(text_.render, text_.cache);
    cache_Font_(font, glyph, 0);
    cache_Font_(font, glyph, 1); /* half-pixel offset */
    SDL_SetRenderTarget(text_.render, oldTarget);
    insert_Hash(&font->glyphs, &glyph->node);
    return glyph;
}

enum iRunMode {
    measure_RunMode,
    measureNoWrap_RunMode,
    measureVisual_RunMode, /* actual visible bounding box of the glyph, e.g., for icons */
    draw_RunMode,
    drawPermanentColor_RunMode
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
    return font - text_.fonts;
}

iLocalDef iBool isWrapBoundary_(iChar prevC, iChar c) {
    /* Line wrapping boundaries are determined by looking at a character and the
       last character processed. We want to wrap at natural word boundaries where
       possible, so normally we wrap at a space followed a non-space character. As
       an exception, we also wrap after punctuation used to break up words, so we
       can wrap text like foo/bar/baz-abc-def.xyz at any puncation boundaries,
       without wrapping on other punctuation used for expressive purposes like
       emoticons :-) */
    if (isSpace_Char(prevC)) {
        return iFalse;
    }
    if (c == '/' || c == '-' || c == ',' || c == ';' || c == ':' || c == '.') {
        return iTrue;
    }
    return isSpace_Char(c);
}

iLocalDef iBool isMeasuring_(enum iRunMode mode) {
    return mode == measure_RunMode || mode == measureNoWrap_RunMode ||
           mode == measureVisual_RunMode;
}

static iRect run_Font_(iFont *d, enum iRunMode mode, iRangecc text, size_t maxLen, iInt2 pos,
                       int xposLimit, const char **continueFrom_out, int *runAdvance_out) {
    iRect bounds = zero_Rect();
    const iInt2 orig = pos;
    float xpos = pos.x;
    float xposMax = xpos;
    float monoAdvance = 0;
    iAssert(xposLimit == 0 || isMeasuring_(mode));
    const char *lastWordEnd = text.start;
    if (continueFrom_out) {
        *continueFrom_out = text.end;
    }
    iChar prevCh = 0;
    if (d->isMonospaced) {
        monoAdvance = glyph_Font_(d, 'M')->advance;
    }
    for (const char *chPos = text.start; chPos != text.end; ) {
        iAssert(chPos < text.end);
        const char *currentPos = chPos;
        if (*chPos == 0x1b) {
            /* ANSI escape. */
            chPos++;
            iRegExpMatch m;
            init_RegExpMatch(&m);
            if (match_RegExp(text_.ansiEscape, chPos, text.end - chPos, &m)) {
                if (mode == draw_RunMode) {
                    /* Change the color. */
                    const iColor clr = ansi_Color(capturedRange_RegExpMatch(&m, 1), tmParagraph_ColorId);
                    SDL_SetTextureColorMod(text_.cache, clr.r, clr.g, clr.b);
                }
                chPos = end_RegExpMatch(&m);
                continue;
            }
        }
        iChar ch = nextChar_(&chPos, text.end);
        if (isVariationSelector_Char(ch)) {
            /* TODO: VS15: Should peek ahead for this and prefer the Emoji font. */
            ch = nextChar_(&chPos, text.end); /* just ignore */
        }
        /* Special instructions. */ {
            if (ch == '\n') {
                xpos = pos.x;
                pos.y += d->height;
                prevCh = ch;
                continue;
            }
            if (ch == '\t') {
                const int tabStopWidth = d->height * 8;
                xpos = pos.x + ((int) ((xpos - pos.x) / tabStopWidth) + 1) * tabStopWidth;
                prevCh = 0;
                continue;
            }
            if (ch == '\r') {
                const iChar esc = nextChar_(&chPos, text.end);
                if (mode == draw_RunMode) {
                    const iColor clr = get_Color(esc - asciiBase_ColorEscape);
                    SDL_SetTextureColorMod(text_.cache, clr.r, clr.g, clr.b);
                }
                prevCh = 0;
                continue;
            }
        }
        const iGlyph *glyph = glyph_Font_(d, ch);
        int x1 = xpos;
        const int hoff = enableHalfPixelGlyphs_Text ? (xpos - x1 > 0.5f ? 1 : 0) : 0;
        int x2 = x1 + glyph->rect[hoff].size.x;
        /* Out of the allotted space? */
        if (xposLimit > 0 && x2 > xposLimit) {
            if (lastWordEnd != text.start) {
                *continueFrom_out = lastWordEnd;
            }
            else {
                *continueFrom_out = currentPos; /* forced break */
            }
            break;
        }
        SDL_Rect dst = { x1 + glyph->d[hoff].x,
                         pos.y + glyph->font->baseline + glyph->d[hoff].y,
                         glyph->rect[hoff].size.x,
                         glyph->rect[hoff].size.y };
        /* Update the bounding box. */
        if (mode == measureVisual_RunMode) {
            if (isEmpty_Rect(bounds)) {
                bounds = init_Rect(dst.x, dst.y, dst.w, dst.h);
            }
            else {
                bounds = union_Rect(bounds, init_Rect(dst.x, dst.y, dst.w, dst.h));
            }
        }
        else {
            bounds.size.x = iMax(bounds.size.x, x2 - orig.x);
            bounds.size.y = iMax(bounds.size.y, pos.y + glyph->font->height - orig.y);
        }
        const iBool useMonoAdvance =
            monoAdvance > 0 && !isJapanese_FontId(fontId_Text_(glyph->font));
        const float advance = (useMonoAdvance ? monoAdvance : glyph->advance);
        if (!isMeasuring_(mode)) {
            if (useMonoAdvance && dst.w > advance) {
                dst.x -= (dst.w - advance) / 2;

            }
            SDL_RenderCopy(text_.render, text_.cache, (const SDL_Rect *) &glyph->rect[hoff], &dst);
        }
        /* Symbols and emojis are NOT monospaced, so must conform when the primary font
           is monospaced. Except with Japanese script, that's larger than the normal monospace. */
        xpos += advance;
        xposMax = iMax(xposMax, xpos);
        if (continueFrom_out && (mode == measureNoWrap_RunMode || isWrapBoundary_(prevCh, ch))) {
            lastWordEnd = chPos;
        }
#if defined (LAGRANGE_ENABLE_KERNING)
        /* Check the next character. */
        if (!d->isMonospaced && glyph->font == d) {
            /* TODO: No need to decode the next char twice; check this on the next iteration. */
            const char *peek = chPos;
            const iChar next = nextChar_(&peek, text.end);
            if (enableKerning_Text && !d->manualKernOnly && next) {
                xpos += d->scale * stbtt_GetGlyphKernAdvance(&d->font, glyph->glyphIndex, next);
            }
        }
#endif
        prevCh = ch;
        if (--maxLen == 0) {
            break;
        }
    }
    if (runAdvance_out) {
        *runAdvance_out = xposMax - orig.x;
    }
    return bounds;
}

int lineHeight_Text(int fontId) {
    return text_.fonts[fontId].height;
}

iInt2 measureRange_Text(int fontId, iRangecc text) {
    if (isEmpty_Range(&text)) {
        return init_I2(0, lineHeight_Text(fontId));
    }
    return run_Font_(&text_.fonts[fontId],
                     measure_RunMode,
                     text,
                     iInvalidSize,
                     zero_I2(),
                     0,
                     NULL,
                     NULL).size;
}

iRect visualBounds_Text(int fontId, iRangecc text) {
    return run_Font_(
        font_Text_(fontId), measureVisual_RunMode, text, iInvalidSize, zero_I2(), 0, NULL, NULL);
}

iInt2 measure_Text(int fontId, const char *text) {
    return measureRange_Text(fontId, range_CStr(text));
}

iInt2 advanceRange_Text(int fontId, iRangecc text) {
    int advance;
    const int height = run_Font_(&text_.fonts[fontId],
                                 measure_RunMode,
                                 text,
                                 iInvalidSize,
                                 zero_I2(),
                                 0,
                                 NULL,
                                 &advance)
                           .size.y;
    return init_I2(advance, height);
}

iInt2 tryAdvance_Text(int fontId, iRangecc text, int width, const char **endPos) {
    int advance;
    const int height = run_Font_(&text_.fonts[fontId],
                                 measure_RunMode,
                                 text,
                                 iInvalidSize,
                                 zero_I2(),
                                 width,
                                 endPos,
                                 &advance)
                           .size.y;
    return init_I2(advance, height);
}

iInt2 tryAdvanceNoWrap_Text(int fontId, iRangecc text, int width, const char **endPos) {
    int advance;
    const int height = run_Font_(&text_.fonts[fontId],
                                 measureNoWrap_RunMode,
                                 text,
                                 iInvalidSize,
                                 zero_I2(),
                                 width,
                                 endPos,
                                 &advance)
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
    run_Font_(
        &text_.fonts[fontId], measure_RunMode, range_CStr(text), n, zero_I2(), 0, NULL, &advance);
    return init_I2(advance, lineHeight_Text(fontId));
}

static void draw_Text_(int fontId, iInt2 pos, int color, iRangecc text) {
    iText *d = &text_;
    const iColor clr = get_Color(color & mask_ColorId);
    SDL_SetTextureColorMod(d->cache, clr.r, clr.g, clr.b);
    run_Font_(&d->fonts[fontId],
              color & permanent_ColorId ? drawPermanentColor_RunMode : draw_RunMode,
              text,
              iInvalidSize,
              pos,
              0,
              NULL,
              NULL);
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

int drawWrapRange_Text(int fontId, iInt2 pos, int maxWidth, int color, iRangecc text) {
    const char *endp;
    while (!isEmpty_Range(&text)) {
        tryAdvance_Text(fontId, text, maxWidth, &endp);
        drawRange_Text(fontId, pos, color, (iRangecc){ text.start, endp });
        text.start = endp;
        pos.y += lineHeight_Text(fontId);
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
        if (i.value == variationSelectorEmoji_Char) {
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
