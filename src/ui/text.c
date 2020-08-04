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
#include <the_Foundation/regexp.h>
#include <the_Foundation/path.h>
#include <the_Foundation/vec2.h>

#include <SDL_surface.h>
#include <SDL_hints.h>
#include <stdarg.h>

iDeclareType(Font)
iDeclareType(Glyph)
iDeclareTypeConstructionArgs(Glyph, iChar ch)

int gap_Text;                           /* cf. gap_UI in metrics.h */
int enableHalfPixelGlyphs_Text = iTrue; /* debug setting */

struct Impl_Glyph {
    iHashNode node;
    const iFont *font; /* may come from symbols/emoji */
    iRect rect[2]; /* zero and half pixel offset */
    iInt2 d[2];
    float advance; /* scaled */
};

void init_Glyph(iGlyph *d, iChar ch) {
    d->node.key = ch;
    d->font = NULL;
    d->rect[0] = zero_Rect();
    d->rect[1] = zero_Rect();
    d->advance = 0.0f;
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
    int            height;
    int            baseline;
    iHash          glyphs;
    iBool          enableKerning;
    enum iFontId   symbolsFont; /* font to use for symbols */
};

static iFont *font_Text_(enum iFontId id);

static void init_Font(iFont *d, const iBlock *data, int height, enum iFontId symbolsFont) {
    init_Hash(&d->glyphs);
    d->data = NULL;
    d->height = height;
    iZap(d->font);
    stbtt_InitFont(&d->font, constData_Block(data), 0);
    d->scale = stbtt_ScaleForPixelHeight(&d->font, height);
    int ascent;
    stbtt_GetFontVMetrics(&d->font, &ascent, NULL, NULL);
    d->baseline = (int) ascent * d->scale;
    d->symbolsFont = symbolsFont;
    d->enableKerning = iTrue;
}

static void deinit_Font(iFont *d) {
    iForEach(Hash, i, &d->glyphs) {
        delete_Glyph((iGlyph *) i.value);
    }
    deinit_Hash(&d->glyphs);
    delete_Block(d->data);
}

iDeclareType(Text)
iDeclareType(CacheRow)

struct Impl_CacheRow {
    int   height;
    iInt2 pos;
};

struct Impl_Text {
    float         contentFontSize;
    iFont         fonts[max_FontId];
    SDL_Renderer *render;
    SDL_Texture * cache;
    iInt2         cacheSize;
    int           cacheRowAllocStep;
    int           cacheBottom;
    iArray        cacheRows;
    SDL_Palette * grayscale;
    iRegExp *     ansiEscape;
};

static iText text_;

static void initFonts_Text_(iText *d) {
    const float textSize = fontSize_UI * d->contentFontSize;
    const struct {
        const iBlock *ttf;
        int size;
        int symbolsFont;
    } fontData[max_FontId] = {
        { &fontSourceSansProRegular_Embedded, fontSize_UI,          defaultSymbols_FontId },
        { &fontFiraMonoRegular_Embedded,      fontSize_UI * 0.866f, defaultSymbols_FontId },
        { &fontFiraSansRegular_Embedded,      textSize,             symbols_FontId },
        { &fontFiraMonoRegular_Embedded,      textSize * 0.866f,    smallSymbols_FontId },
        { &fontFiraMonoRegular_Embedded,      textSize * 0.666f,    smallSymbols_FontId },
        { &fontFiraSansRegular_Embedded,      textSize * 1.333f,    mediumSymbols_FontId },
        { &fontFiraSansItalic_Embedded,       textSize,             symbols_FontId },
        { &fontFiraSansBold_Embedded,         textSize,             symbols_FontId },
        { &fontFiraSansBold_Embedded,         textSize * 1.333f,    mediumSymbols_FontId },
        { &fontFiraSansBold_Embedded,         textSize * 1.666f,    largeSymbols_FontId },
        { &fontFiraSansBold_Embedded,         textSize * 2.000f,    hugeSymbols_FontId },
        { &fontSymbola_Embedded,              fontSize_UI,          defaultSymbols_FontId },
        { &fontSymbola_Embedded,              textSize,             symbols_FontId },
        { &fontSymbola_Embedded,              textSize * 1.333f,    mediumSymbols_FontId },
        { &fontSymbola_Embedded,              textSize * 1.666f,    largeSymbols_FontId },
        { &fontSymbola_Embedded,              textSize * 2.000f,    hugeSymbols_FontId },
        { &fontSymbola_Embedded,              textSize * 0.866f,    smallSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     fontSize_UI,          defaultSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     textSize,             symbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     textSize * 1.333f,    mediumSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     textSize * 1.666f,    largeSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     textSize * 2.000f,    hugeSymbols_FontId },
        { &fontNotoEmojiRegular_Embedded,     textSize * 0.866f,    smallSymbols_FontId },
    };
    iForIndices(i, fontData) {
        iFont *font = &d->fonts[i];
        init_Font(font, fontData[i].ttf, fontData[i].size, fontData[i].symbolsFont);
        if (fontData[i].ttf == &fontFiraMonoRegular_Embedded) {
            font->enableKerning = iFalse;
        }
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
    d->cacheRowAllocStep = iMax(2, textSize / 6);
    /* Allocate initial (empty) rows. These will be assigned actual locations in the cache
       once at least one glyph is stored. */
    for (int h = d->cacheRowAllocStep; h <= 2 * textSize; h += d->cacheRowAllocStep) {
        pushBack_Array(&d->cacheRows, &(iCacheRow){ .height = 0 });
    }
    d->cacheBottom = 0;
    d->cache = SDL_CreateTexture(d->render,
                                 SDL_PIXELFORMAT_RGBA8888,
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
    d->contentFontSize = 1.0f;
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

void setContentFontSize_Text(float fontSizeFactor) {
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

static SDL_Surface *rasterizeGlyph_Font_(const iFont *d, iChar ch, float xShift) {
    int w, h;
    uint8_t *bmp = stbtt_GetCodepointBitmapSubpixel(
        &d->font, d->scale, d->scale, xShift, 0.0f, ch, &w, &h, 0, 0);
    /* Note: `bmp` must be freed afterwards. */
    SDL_Surface *surface =
        SDL_CreateRGBSurfaceWithFormatFrom(bmp, w, h, 8, w, SDL_PIXELFORMAT_INDEX8);
    SDL_SetSurfacePalette(surface, text_.grayscale);
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
    const iChar ch = char_Glyph(glyph);
    iRect *glRect = &glyph->rect[hoff];
    /* Rasterize the glyph using stbtt. */ {
        surface = rasterizeGlyph_Font_(d, ch, hoff * 0.5f);
        if (hoff == 0) {
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&d->font, ch, &adv, &lsb);
            glyph->advance = d->scale * adv;
        }
        stbtt_GetCodepointBitmapBoxSubpixel(&d->font,
                                            ch,
                                            d->scale,
                                            d->scale,
                                            hoff * 0.5f,
                                            0.0f,
                                            &glyph->d[hoff].x,
                                            &glyph->d[hoff].y,
                                            NULL,
                                            NULL);
        tex = SDL_CreateTextureFromSurface(render, surface);
        glRect->size = init_I2(surface->w, surface->h);
    }
    /* Determine placement in the glyph cache texture, advancing in rows. */
    glRect->pos = assignCachePos_Text_(txt, glRect->size);
    SDL_SetRenderTarget(render, txt->cache);
    const SDL_Rect dstRect = sdlRect_(*glRect);
    SDL_RenderCopy(render, tex, &(SDL_Rect){ 0, 0, dstRect.w, dstRect.h }, &dstRect);
    SDL_SetRenderTarget(render, NULL);
    if (tex) {
        SDL_DestroyTexture(tex);
        iAssert(surface);
        stbtt_FreeBitmap(surface->pixels, NULL);
        SDL_FreeSurface(surface);
    }
}

iLocalDef iFont *characterFont_Font_(iFont *d, iChar ch) {    
    if (stbtt_FindGlyphIndex(&d->font, ch) != 0) {
        return d;
    }
    /* Not defined in current font, try Noto Emoji (for selected characters). */
    if ((ch >= 0x1f300 && ch < 0x1f600) || (ch >= 0x1f680 && ch <= 0x1f6c5)) {
        iFont *emoji = font_Text_(d->symbolsFont + fromSymbolsToEmojiOffset_FontId);
        if (emoji != d && stbtt_FindGlyphIndex(&emoji->font, ch)) {
            return emoji;
        }
    }
    /* Fall back to Symbola for anything else. */
    return font_Text_(d->symbolsFont);
}

static const iGlyph *glyph_Font_(iFont *d, iChar ch) {
    /* It may actually come from a different font. */
    iFont *font = characterFont_Font_(d, ch);
    const void *node = value_Hash(&font->glyphs, ch);
    if (node) {
        return node;
    }
    iGlyph *glyph = new_Glyph(ch);
    glyph->font = font;
    cache_Font_(font, glyph, 0);
    cache_Font_(font, glyph, 1); /* half-pixel offset */
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

iLocalDef iBool isWrapBoundary_(iChar a, iChar b) {
    if (b == '/' || b == '-' || b == ',' || b == ';' || b == ':') {
        return iTrue;
    }
    return !isSpace_Char(a) && isSpace_Char(b);
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
    iAssert(xposLimit == 0 || isMeasuring_(mode));
    const char *lastWordEnd = text.start;
    if (continueFrom_out) {
        *continueFrom_out = text.end;
    }
    iChar prevCh = 0;
    for (const char *chPos = text.start; chPos != text.end; ) {
        iAssert(chPos < text.end);
        const char *currentPos = chPos;
        if (*chPos == 0x1b) {
            /* ANSI escape. */
            chPos++;
            iRegExpMatch m;
            if (match_RegExp(text_.ansiEscape, chPos, text.end - chPos, &m)) {
                if (mode == draw_RunMode) {
                    /* Change the color. */
                    const iColor clr = ansi_Color(capturedRange_RegExpMatch(&m, 1), gray75_ColorId);
                    SDL_SetTextureColorMod(text_.cache, clr.r, clr.g, clr.b);
                }
                chPos = end_RegExpMatch(&m);
                continue;
            }
        }
        iChar ch = nextChar_(&chPos, text.end);
        /* Special instructions. */ {
            if (ch == '\n') {
                xpos = pos.x;
                pos.y += d->height;
                prevCh = ch;
                continue;
            }
            if (ch == '\r') {
                const iChar esc = nextChar_(&chPos, text.end);
                if (mode == draw_RunMode) {
                    const iColor clr = get_Color(esc - '0');
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
        const SDL_Rect dst = { x1 + glyph->d[hoff].x,
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
        if (!isMeasuring_(mode)) {
            SDL_RenderCopy(text_.render, text_.cache, (const SDL_Rect *) &glyph->rect[hoff], &dst);
        }
        xpos += glyph->advance;
        xposMax = iMax(xposMax, xpos);
        if (mode == measureNoWrap_RunMode || isWrapBoundary_(prevCh, ch)) {
            lastWordEnd = chPos;
        }
        /* Check the next character. */
        if (d->enableKerning && glyph->font == d) {
            /* TODO: No need to decode the next char twice; check this on the next iteration. */
            const char *peek = chPos;
            const iChar next = nextChar_(&peek, text.end);
            if (ch == '/' && next == '/') {
                /* Manual kerning for double-slash. */
                xpos -= glyph->rect[hoff].size.x * 0.5f;
            }
            else if (next) {
                xpos += d->scale * stbtt_GetCodepointKernAdvance(&d->font, ch, next);
            }
        }
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

/*-----------------------------------------------------------------------------------------------*/

iDefineTypeConstructionArgs(TextBuf, (int font, const char *text), font, text)

void init_TextBuf(iTextBuf *d, int font, const char *text) {
    SDL_Renderer *render = text_.render;
    d->size    = advance_Text(font, text);
    d->texture = SDL_CreateTexture(render,
                                   SDL_PIXELFORMAT_RGBA8888,
                                   SDL_TEXTUREACCESS_STATIC | SDL_TEXTUREACCESS_TARGET,
                                   d->size.x,
                                   d->size.y);
    SDL_SetTextureBlendMode(d->texture, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(render, d->texture);
    draw_Text_(font, zero_I2(), white_ColorId, range_CStr(text));
    SDL_SetRenderTarget(render, NULL);
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
