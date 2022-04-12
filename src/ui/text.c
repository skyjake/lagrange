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

#include "text.h"
#include "color.h"
#include "paint.h"

#include <the_Foundation/regexp.h>
#include <SDL_hints.h>

#if defined (LAGRANGE_ENABLE_HARFBUZZ)
#   include <hb.h>
#endif

#if defined (LAGRANGE_ENABLE_FRIBIDI)
#   include <fribidi/fribidi.h>
#endif

static iText *current_Text_;

int   gap_Text;                           /* cf. gap_UI in metrics.h */

void init_Text(iText *d, SDL_Renderer *render) {
    d->render          = render;
    d->contentFontSize = contentScale_Text;
    d->ansiEscape      = makeAnsiEscapePattern_Text(iFalse /* no ESC prefix */);
    d->baseFontId      = -1;
    d->baseFgColorId   = -1;
}

void deinit_Text(iText *d) {
    d->render = NULL;
    iRelease(d->ansiEscape);
}

void setCurrent_Text(iText *d) {
    current_Text_ = d;
}

iText *current_Text(void) {
    return current_Text_;
}

void setDocumentFontSize_Text(iText *d, float fontSizeFactor) {
    fontSizeFactor *= contentScale_Text;
    iAssert(fontSizeFactor > 0);
    if (iAbs(d->contentFontSize - fontSizeFactor) > 0.001f) {
        d->contentFontSize = fontSizeFactor;
        resetFonts_Text(d);
    }
}

void setBaseAttributes_Text(int fontId, int fgColorId) {
    iText *d = current_Text_;
    d->baseFontId    = fontId;
    d->baseFgColorId = fgColorId;
}

void setAnsiFlags_Text(int ansiFlags) {
    current_Text_->ansiFlags = ansiFlags;
}

int ansiFlags_Text(void) {
    return current_Text_->ansiFlags;
}

iRegExp *makeAnsiEscapePattern_Text(iBool includeEscChar) {
    const char *pattern = "\x1b[[()][?]?([0-9;AB]*?)([ABCDEFGHJKSTfhilmn])";
    if (!includeEscChar) {
        pattern++;
    }
    return new_RegExp(pattern, 0);
}

/*----------------------------------------------------------------------------------------------*/

int lineHeight_Text(int fontId) {
    return font_Text(fontId)->height;
}

iTextMetrics measureRange_Text(int fontId, iRangecc text) {
    if (isEmpty_Range(&text)) {
        return (iTextMetrics){ init_Rect(0, 0, 0, lineHeight_Text(fontId)), zero_I2() };
    }
    iTextMetrics tm;
    tm.bounds = run_Font(font_Text(fontId), &(iRunArgs){
        .mode = measure_RunMode,
        .text = text,
        .cursorAdvance_out = &tm.advance
    });
    return tm;
}

iRect visualBounds_Text(int fontId, iRangecc text) {
    return run_Font(font_Text(fontId),
                     &(iRunArgs){
                         .mode = measure_RunMode | visualFlag_RunMode,
                         .text = text,
                     });
}

int runFlags_FontId(enum iFontId fontId) {
    int runFlags = 0;
    if (fontId & alwaysVariableFlag_FontId) {
        runFlags |= alwaysVariableWidthFlag_RunMode;
    }
    return runFlags;
}

static iBool cbAdvanceOneLine_(iWrapText *d, iRangecc range, iTextAttrib attrib, int origin,
                               int advance) {
    iUnused(attrib, origin, advance);
    *((const char **) d->context) = range.end;
    return iFalse; /* just one line */
}

iInt2 tryAdvance_Text(int fontId, iRangecc text, int width, const char **endPos) {
    *endPos = text.end;
    iWrapText wrap = { .mode     = word_WrapTextMode,
                       .text     = text,
                       .maxWidth = width,
                       .wrapFunc = cbAdvanceOneLine_,
                       .context  = endPos };
    /* The return value is expected to be the horizontal/vertical bounds. */
    return measure_WrapText(&wrap, fontId).bounds.size;
}

iInt2 tryAdvanceNoWrap_Text(int fontId, iRangecc text, int width, const char **endPos) {
    if (width && width <= 1) {
        *endPos = text.start;
        return zero_I2();
    }
    *endPos = text.end;
    /* "NoWrap" means words aren't wrapped; the line is broken at nearest character. */
    iWrapText wrap = { .mode     = anyCharacter_WrapTextMode,
                       .text     = text,
                       .maxWidth = width,
                       .wrapFunc = cbAdvanceOneLine_,
                       .context  = endPos };
    iTextMetrics tm = measure_WrapText(&wrap, fontId);
    return init_I2(maxWidth_TextMetrics(tm), tm.bounds.size.y);
}

iTextMetrics measureN_Text(int fontId, const char *text, size_t n) {
    if (n == 0) {
        return (iTextMetrics){ init_Rect(0, 0, 0, lineHeight_Text(fontId)),
                               zero_I2() };
    }
    iTextMetrics tm;
    tm.bounds = run_Font(font_Text(fontId),
              &(iRunArgs){ .mode              = measure_RunMode | runFlags_FontId(fontId),
                           .text              = range_CStr(text),
                           .maxLen            = n,
                           .cursorAdvance_out = &tm.advance });
    return tm;
}

static void drawBoundedN_Text_(int fontId, iInt2 pos, int boundWidth, iBool justify,
                               int color, iRangecc text, size_t maxLen) {
    run_Font(font_Text(fontId),
              &(iRunArgs){ .mode = draw_RunMode |
                                   (color & permanent_ColorId ? permanentColorFlag_RunMode : 0) |
                                   (color & fillBackground_ColorId ? fillBackground_RunMode : 0) |
                                   (color & underline_ColorId ? underline_RunMode : 0) |
                                   runFlags_FontId(fontId),
                           .text        = text,
                           .maxLen      = maxLen,
                           .pos         = pos,
                           .layoutBound = iAbs(boundWidth),
                           .justify     = justify,
                           .color       = color & mask_ColorId,
                           .baseDir     = iSign(boundWidth) });
}

static void drawBounded_Text_(int fontId, iInt2 pos, int boundWidth, iBool justify, int color, iRangecc text) {
    drawBoundedN_Text_(fontId, pos, boundWidth, justify, color, text, 0);
}

static void draw_Text_(int fontId, iInt2 pos, int color, iRangecc text) {
    drawBounded_Text_(fontId, pos, 0, iFalse, color, text);
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
        pos.x -= measure_Text(fontId, cstr_Block(&chars)).bounds.size.x / 2;
    }
    else if (align == right_Alignment) {
        pos.x -= measure_Text(fontId, cstr_Block(&chars)).bounds.size.x;
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
    drawBoundedN_Text_(fontId, pos, 0, iFalse, color, text, maxChars);
}

void drawOutline_Text(int fontId, iInt2 pos, int outlineColor, int fillColor, iRangecc text) {
#if !defined (iPlatformTerminal)
    for (int off = 0; off < 4; ++off) {
        drawRange_Text(fontId,
                       add_I2(pos, init_I2(off % 2 == 0 ? -1 : 1, off / 2 == 0 ? -1 : 1)),
                       outlineColor,
                       text);
    }
    if (fillColor != none_ColorId) {
        drawRange_Text(fontId, pos, fillColor, text);
    }
#else
    drawRange_Text(fontId, pos, fillColor | fillBackground_ColorId, text);
#endif    
}

iTextMetrics measureWrapRange_Text(int fontId, int maxWidth, iRangecc text) {
    iWrapText wrap = { .text = text, .maxWidth = maxWidth, .mode = word_WrapTextMode };
    return measure_WrapText(&wrap, fontId);
}

void drawBoundRange_Text(int fontId, iInt2 pos, int boundWidth, iBool justify, int color, iRangecc text) {
    /* This function is used together with text that has already been wrapped, so we'll know
       the bound width but don't have to re-wrap the text. */
    drawBounded_Text_(fontId, pos, boundWidth, justify, color, text);
}

int drawWrapRange_Text(int fontId, iInt2 pos, int maxWidth, int color, iRangecc text) {
    /* TODO: Use WrapText here, too */
    const char *endp;
    while (!isEmpty_Range(&text)) {
        iInt2 adv = tryAdvance_Text(fontId, text, maxWidth, &endp);
        drawRange_Text(fontId, pos, color, (iRangecc){ text.start, endp });
        if (text.start == endp) {
            adv = tryAdvanceNoWrap_Text(fontId, text, maxWidth, &endp);
            if (text.start == endp) {
                break; /* let's not get stuck */
            }
        }
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

void drawCenteredOutline_Text(int fontId, iRect rect, iBool alignVisual, int outlineColor,
                              int fillColor, const char *format, ...) {
    iBlock chars;
    init_Block(&chars, 0); {
        va_list args;
        va_start(args, format);
        vprintf_Block(&chars, format, args);
        va_end(args);
    }
    if (outlineColor != none_ColorId) {
        for (int off = 0; off < 4; ++off) {
            drawCenteredRange_Text(
                fontId,
                moved_Rect(rect, init_I2(off % 2 == 0 ? -1 : 1, off / 2 == 0 ? -1 : 1)),
                alignVisual,
                outlineColor,
                range_Block(&chars));
        }
    }
    if (fillColor != none_ColorId) {
        drawCenteredRange_Text(fontId, rect, alignVisual, fillColor, range_Block(&chars));
    }
    deinit_Block(&chars);
}

void drawCenteredRange_Text(int fontId, iRect rect, iBool alignVisual, int color, iRangecc text) {
    iRect textBounds = alignVisual ? visualBounds_Text(fontId, text)
                                   : measureRange_Text(fontId, text).bounds;
    textBounds.pos = sub_I2(mid_Rect(rect), mid_Rect(textBounds));
    textBounds.pos.x = iMax(textBounds.pos.x, left_Rect(rect)); /* keep left edge visible */
    draw_Text_(fontId, textBounds.pos, color, text);
}

/*-----------------------------------------------------------------------------------------------*/

iDefineTypeConstructionArgs(TextBuf, (iWrapText *wrapText, int font, int color), wrapText, font, color)

void init_TextBuf(iTextBuf *d, iWrapText *wrapText, int font, int color) {
    SDL_Renderer *render = current_Text()->render;
    d->size = measure_WrapText(wrapText, font).bounds.size;
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
        const iInt2 oldOrigin = origin_Paint;
        origin_Paint = zero_I2();
        setBaseAttributes_Text(font, color);
        SDL_SetRenderTarget(render, d->texture);
        SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(render, 255, 255, 255, 0);
        SDL_RenderClear(render);
        draw_WrapText(wrapText, font, zero_I2(), color | fillBackground_ColorId);
        SDL_SetRenderTarget(render, oldTarget);
        origin_Paint = oldOrigin;
        SDL_SetTextureBlendMode(d->texture, SDL_BLENDMODE_BLEND);
        setBaseAttributes_Text(-1, -1);
    }
}

void deinit_TextBuf(iTextBuf *d) {
    SDL_DestroyTexture(d->texture);
}

iTextBuf *newRange_TextBuf(int font, int color, iRangecc text) {
    return new_TextBuf(&(iWrapText){ .text = text }, font, color);
}

void draw_TextBuf(const iTextBuf *d, iInt2 pos, int color) {
    addv_I2(&pos, origin_Paint);
    const iColor clr = get_Color(color);
    SDL_SetTextureColorMod(d->texture, clr.r, clr.g, clr.b);
    SDL_RenderCopy(current_Text()->render,
                   d->texture,
                   &(SDL_Rect){ 0, 0, d->size.x, d->size.y },
                   &(SDL_Rect){ pos.x, pos.y, d->size.x, d->size.y });
}
