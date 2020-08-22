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

#pragma once

#include <the_Foundation/rect.h>
#include <the_Foundation/string.h>

#include <SDL_render.h>

enum iFontId {
    default_FontId,
    defaultMedium_FontId,
    defaultMonospace_FontId,
    regular_FontId,
    monospace_FontId,
    monospaceSmall_FontId,
    medium_FontId,
    italic_FontId,
    bold_FontId,
    mediumBold_FontId,
    largeBold_FontId,
    hugeBold_FontId,
    largeLight_FontId,
    /* symbol fonts */
    defaultSymbols_FontId,
    defaultMediumSymbols_FontId,
    symbols_FontId,
    mediumSymbols_FontId,
    largeSymbols_FontId,
    hugeSymbols_FontId,
    smallSymbols_FontId,
    /* emoji fonts */
    defaultEmoji_FontId,
    defaultMediumEmoji_FontId,
    emoji_FontId,
    mediumEmoji_FontId,
    largeEmoji_FontId,
    hugeEmoji_FontId,
    smallEmoji_FontId,
    max_FontId,
    /* Meta: */
    fromSymbolsToEmojiOffset_FontId = 7,
    /* UI fonts: */
    uiLabel_FontId        = default_FontId,
    uiShortcuts_FontId    = default_FontId,
    uiInput_FontId        = defaultMonospace_FontId,
    uiContent_FontId      = defaultMedium_FontId,
    /* Document fonts: */
    paragraph_FontId         = regular_FontId,
    firstParagraph_FontId    = medium_FontId,
    preformatted_FontId      = monospace_FontId,
    preformattedSmall_FontId = monospaceSmall_FontId,
    quote_FontId             = italic_FontId,
    heading1_FontId          = hugeBold_FontId,
    heading2_FontId          = largeBold_FontId,
    heading3_FontId          = medium_FontId,
    banner_FontId            = largeLight_FontId,
};

#define variationSelectorEmoji_Char     ((iChar) 0xfe0f)

extern int gap_Text; /* affected by content font size */

void    init_Text           (SDL_Renderer *);
void    deinit_Text         (void);

void    setContentFontSize_Text (float fontSizeFactor); /* affects all except `default*` fonts */
void    resetFonts_Text     (void);

int     lineHeight_Text     (int fontId);
iInt2   measure_Text        (int fontId, const char *text);
iInt2   measureRange_Text   (int fontId, iRangecc text);
iRect   visualBounds_Text   (int fontId, iRangecc text);
iInt2   advance_Text        (int fontId, const char *text);
iInt2   advanceN_Text       (int fontId, const char *text, size_t n); /* `n` in characters */
iInt2   advanceRange_Text   (int fontId, iRangecc text);

iInt2   tryAdvance_Text         (int fontId, iRangecc text, int width, const char **endPos);
iInt2   tryAdvanceNoWrap_Text   (int fontId, iRangecc text, int width, const char **endPos);

enum iAlignment {
    left_Alignment,
    center_Alignment,
    right_Alignment,
};

void    draw_Text           (int fontId, iInt2 pos, int color, const char *text, ...);
void    drawAlign_Text      (int fontId, iInt2 pos, int color, enum iAlignment align, const char *text, ...);
void    drawCentered_Text   (int fontId, iRect rect, iBool alignVisual, int color, const char *text, ...);
void    drawString_Text     (int fontId, iInt2 pos, int color, const iString *text);
void    drawRange_Text      (int fontId, iInt2 pos, int color, iRangecc text);

SDL_Texture *   glyphCache_Text     (void);

enum iTextBlockMode { quadrants_TextBlockMode, shading_TextBlockMode };

iString *   renderBlockChars_Text   (const iBlock *fontData, int height, enum iTextBlockMode,
                                     const iString *text);

/*-----------------------------------------------------------------------------------------------*/

iDeclareType(TextBuf)
iDeclareTypeConstructionArgs(TextBuf, int font, const char *text)

struct Impl_TextBuf {
    SDL_Texture *texture;
    iInt2        size;
};

void    draw_TextBuf        (const iTextBuf *, iInt2 pos, int color);
