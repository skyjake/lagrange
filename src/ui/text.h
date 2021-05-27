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

/* Content sizes: regular (1x) -> medium (1.2x) -> big (1.33x) -> large (1.67x) -> huge (2x) */

enum iFontSize {
    uiNormal_FontSize, /* 1.000 */
    uiMedium_FontSize, /* 1.125 */
    uiBig_FontSize,    /* 1.333 */
    uiLarge_FontSize,  /* 1.666 */
    contentRegular_FontSize,
    contentMedium_FontSize,
    contentBig_FontSize,
    contentLarge_FontSize,
    contentHuge_FontSize,
    contentMonoSmall_FontSize,
    contentMono_FontSize,
    max_FontSize,
};

enum iFontId {
    /* UI fonts: normal weight (1x, 1.125x, 1.33x, 1.67x) */
    default_FontId = 0,
    defaultMedium_FontId,
    defaultBig_FontId,
    defaultLarge_FontId,
    /* UI fonts: bold weight */
    defaultBold_FontId,
    defaultMediumBold_FontId,
    defaultBigBold_FontId,
    defaultLargeBold_FontId,
    /* content fonts */
    regular_FontId,
    bold_FontId,
    italic_FontId,
    medium_FontId,
    big_FontId,
    largeBold_FontId,
    largeLight_FontId,
    hugeBold_FontId,
    monospaceSmall_FontId,
    monospace_FontId,
    /* extra content fonts */
    defaultContentSized_FontId, /* UI font but sized to regular_FontId */
    /* symbols and scripts */
    userSymbols_FontId,
    iosevka_FontId           = userSymbols_FontId + max_FontSize,
    symbols_FontId           = iosevka_FontId + max_FontSize,
    symbols2_FontId          = symbols_FontId + max_FontSize,
    smolEmoji_FontId         = symbols2_FontId + max_FontSize,
    notoEmoji_FontId         = smolEmoji_FontId + max_FontSize,
    japanese_FontId          = notoEmoji_FontId + max_FontSize,
    chineseSimplified_FontId = japanese_FontId + max_FontSize,
    korean_FontId            = chineseSimplified_FontId + max_FontSize,
    arabic_FontId            = korean_FontId + max_FontSize,
    max_FontId               = arabic_FontId + max_FontSize,

    /* Meta: */
    mask_FontId               = 0xffff,
    alwaysVariableFlag_FontId = 0x10000,

    /* UI fonts: */
    uiLabel_FontId          = default_FontId,
    uiLabelBold_FontId      = defaultBold_FontId,
    uiLabelLarge_FontId     = defaultLarge_FontId,
    uiLabelLargeBold_FontId = defaultLargeBold_FontId,
    uiShortcuts_FontId      = default_FontId,
    uiInput_FontId          = defaultMedium_FontId,
    uiContent_FontId        = defaultMedium_FontId,
    uiContentBold_FontId    = defaultMediumBold_FontId,
    uiContentSymbols_FontId = symbols_FontId + uiMedium_FontSize,    
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
    regularMonospace_FontId  = iosevka_FontId + contentRegular_FontSize    
};

iLocalDef iBool isJapanese_FontId(enum iFontId id) {
    return id >= japanese_FontId && id < japanese_FontId + max_FontSize;
}

#define emojiVariationSelector_Char     ((iChar) 0xfe0f)

enum iTextFont {
    nunito_TextFont,
    firaSans_TextFont,
    literata_TextFont,
    tinos_TextFont,
    sourceSans3_TextFont,
    iosevka_TextFont,
};

extern int gap_Text; /* affected by content font size */

void    init_Text               (SDL_Renderer *);
void    deinit_Text             (void);

void    loadUserFonts_Text      (void); /* based on Prefs */

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

void    cache_Text          (int fontId, iRangecc text); /* pre-render glyphs */

void    draw_Text               (int fontId, iInt2 pos, int color, const char *text, ...);
void    drawAlign_Text          (int fontId, iInt2 pos, int color, enum iAlignment align, const char *text, ...);
void    drawCentered_Text       (int fontId, iRect rect, iBool alignVisual, int color, const char *text, ...);
void    drawCenteredRange_Text  (int fontId, iRect rect, iBool alignVisual, int color, iRangecc text);
void    drawString_Text         (int fontId, iInt2 pos, int color, const iString *text);
void    drawRange_Text          (int fontId, iInt2 pos, int color, iRangecc text);
void    drawRangeN_Text         (int fontId, iInt2 pos, int color, iRangecc text, size_t maxLen);
void    drawBoundRange_Text     (int fontId, iInt2 pos, int boundWidth, int color, iRangecc text); /* bound does not wrap */
int     drawWrapRange_Text      (int fontId, iInt2 pos, int maxWidth, int color, iRangecc text); /* returns new Y */

SDL_Texture *   glyphCache_Text     (void);

enum iTextBlockMode { quadrants_TextBlockMode, shading_TextBlockMode };

iString *   renderBlockChars_Text   (const iBlock *fontData, int height, enum iTextBlockMode,
                                     const iString *text);

/*-----------------------------------------------------------------------------------------------*/

iDeclareType(TextBuf)
iDeclareTypeConstructionArgs(TextBuf, int font, int color, const char *text)
    
struct Impl_TextBuf {
    SDL_Texture *texture;
    iInt2        size;
};

iTextBuf *  newBound_TextBuf(int font, int color, int boundWidth, const char *text); /* does not word wrap */
iTextBuf *  newWrap_TextBuf (int font, int color, int wrapWidth, const char *text);

void        draw_TextBuf    (const iTextBuf *, iInt2 pos, int color);
