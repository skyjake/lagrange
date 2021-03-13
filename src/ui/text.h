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

/* Size names: regular (1x) -> medium (1.2x) -> big (1.33x) -> large (1.67x) -> huge (2x) */

enum iFontId {
    default_FontId,
    defaultBold_FontId,
    defaultMedium_FontId,
    defaultMediumBold_FontId,
    defaultBig_FontId,
    defaultBigBold_FontId,
    defaultLarge_FontId,
    defaultLargeBold_FontId,
    defaultMonospace_FontId,
    defaultContentSized_FontId,
    /* content fonts */
    regular_FontId,
    bold_FontId,
    monospace_FontId,
    monospaceSmall_FontId,
    medium_FontId,
    big_FontId,
    italic_FontId,
    largeBold_FontId,
    hugeBold_FontId,
    largeLight_FontId,
    /* monospace content fonts */
    regularMonospace_FontId,
    /* symbol fonts */
    defaultSymbols_FontId,
    defaultMediumSymbols_FontId,
    defaultBigSymbols_FontId,
    defaultLargeSymbols_FontId,
    symbols_FontId,
    mediumSymbols_FontId,
    bigSymbols_FontId,
    largeSymbols_FontId,
    hugeSymbols_FontId,
    monospaceSymbols_FontId,
    monospaceSmallSymbols_FontId,
    /* emoji fonts */
    defaultEmoji_FontId,
    defaultMediumEmoji_FontId,
    defaultBigEmoji_FontId,
    defaultLargeEmoji_FontId,
    emoji_FontId,
    mediumEmoji_FontId,
    bigEmoji_FontId,
    largeEmoji_FontId,
    hugeEmoji_FontId,
    monospaceEmoji_FontId,
    monospaceSmallEmoji_FontId,
    /* japanese script */
    defaultJapanese_FontId,
    monospaceSmallJapanese_FontId,
    monospaceJapanese_FontId,
    regularJapanese_FontId,
    mediumJapanese_FontId,
    bigJapanese_FontId,
    largeJapanese_FontId,
    hugeJapanese_FontId,
    /* korean script */
    defaultKorean_FontId,
    monospaceSmallKorean_FontId,
    monospaceKorean_FontId,
    regularKorean_FontId,
    mediumKorean_FontId,
    bigKorean_FontId,
    largeKorean_FontId,
    hugeKorean_FontId,
    max_FontId,

    /* Meta: */
    fromSymbolsToEmojiOffset_FontId = 11,
    mask_FontId                     = 0xffff,
    alwaysVariableFlag_FontId       = 0x10000,

    /* UI fonts: */
    uiLabel_FontId          = default_FontId,
    uiLabelBold_FontId      = defaultBold_FontId,
    uiLabelLarge_FontId     = defaultLarge_FontId,
    uiLabelLargeBold_FontId = defaultLargeBold_FontId,
    uiShortcuts_FontId      = default_FontId,
    uiInput_FontId          = defaultMedium_FontId,
    uiContent_FontId        = defaultMedium_FontId,
    uiContentBold_FontId    = defaultMediumBold_FontId,
    uiContentSymbols_FontId = defaultMediumSymbols_FontId,
    /* Document fonts: */
    paragraph_FontId         = regular_FontId,
    firstParagraph_FontId    = medium_FontId,
    preformatted_FontId      = monospace_FontId,
    preformattedSmall_FontId = monospaceSmall_FontId,
    quote_FontId             = italic_FontId,
    heading1_FontId          = hugeBold_FontId,
    heading2_FontId          = largeBold_FontId,
    heading3_FontId          = big_FontId,
    banner_FontId            = largeLight_FontId,
};

iLocalDef iBool isJapanese_FontId(enum iFontId id) {
    return id >= defaultJapanese_FontId && id <= hugeJapanese_FontId;
}
iLocalDef iBool isVariationSelector_Char(iChar c) {
    return (c >= 0xfe00 && c <= 0xfe0f) || (c >= 0xe0100 && c <= 0xe0121);
}
iLocalDef iBool isFitzpatrickType_Char(iChar c) {
    return c >= 0x1f3fb && c <= 0x1f3ff;
}
iLocalDef iBool isDefaultIgnorable_Char(iChar c) {
    return c == 0x115f || (c >= 0x200b && c <= 0x200e) || c == 0x2060 || c == 0x2061 ||
           c == 0xfeff;
}
iLocalDef iBool isEmoji_Char(iChar c) {
    return (c >= 0x1f300 && c < 0x1f700) || (c >= 0x1f7e0 && c <= 0x1f7eb) ||
           (c >= 0x1f900 && c <= 0x1f9ff) || (c >= 0x1fa70 && c <= 0x1faff);
}
iLocalDef iBool isDingbats_Char(iChar c) {
    return c >= 0x2702 && c <= 0x27b0;
}
iLocalDef iBool isPictograph_Char(iChar c) {
    return (c == 0x21a9) ||
           (c == 0x2218 || c == 0x2219) ||
           (c >= 0x2300 && c <= 0x27bf) ||
           (c >= 0x1f680 && c <= 0x1f6c0);
}

#define emojiVariationSelector_Char     ((iChar) 0xfe0f)

enum iTextFont {
    nunito_TextFont,
    firaSans_TextFont,
    literata_TextFont,
    tinos_TextFont,
    sourceSansPro_TextFont,
    iosevka_TextFont,
};

extern int gap_Text; /* affected by content font size */

void    init_Text               (SDL_Renderer *);
void    deinit_Text             (void);

void    setContentFont_Text     (enum iTextFont font);
void    setHeadingFont_Text     (enum iTextFont font);
void    setContentFontSize_Text (float fontSizeFactor); /* affects all except `default*` fonts */
void    resetFonts_Text         (void);

int     lineHeight_Text         (int fontId);
iInt2   measure_Text            (int fontId, const char *text);
iInt2   measureRange_Text       (int fontId, iRangecc text);
iRect   visualBounds_Text       (int fontId, iRangecc text);
iInt2   advance_Text            (int fontId, const char *text);
iInt2   advanceN_Text           (int fontId, const char *text, size_t n); /* `n` in characters */
iInt2   advanceRange_Text       (int fontId, iRangecc text);
iInt2   advanceWrapRange_Text   (int fontId, int maxWidth, iRangecc text);

iInt2   tryAdvance_Text         (int fontId, iRangecc text, int width, const char **endPos);
iInt2   tryAdvanceNoWrap_Text   (int fontId, iRangecc text, int width, const char **endPos);

enum iAlignment {
    left_Alignment,
    center_Alignment,
    right_Alignment,
};

void    setOpacity_Text     (float opacity);

void    draw_Text           (int fontId, iInt2 pos, int color, const char *text, ...);
void    drawAlign_Text      (int fontId, iInt2 pos, int color, enum iAlignment align, const char *text, ...);
void    drawCentered_Text   (int fontId, iRect rect, iBool alignVisual, int color, const char *text, ...);
void    drawString_Text     (int fontId, iInt2 pos, int color, const iString *text);
void    drawRange_Text      (int fontId, iInt2 pos, int color, iRangecc text);
void    drawBoundRange_Text (int fontId, iInt2 pos, int boundWidth, int color, iRangecc text); /* bound does not wrap */
int     drawWrapRange_Text  (int fontId, iInt2 pos, int maxWidth, int color, iRangecc text); /* returns new Y */

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
