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
#include <the_Foundation/vec2.h>
#include <SDL_render.h>

#include "font.h"

iDeclareType(RegExp)

#define emojiVariationSelector_Char     ((iChar) 0xfe0f)

extern int gap_Text; /* affected by content font size */
static const float contentScale_Text = 1.3f;

iDeclareType(Text)

struct Impl_Text {
    SDL_Renderer *render;
    float         contentFontSize;
    iRegExp      *ansiEscape;
    int           ansiFlags;
    int           baseFontId; /* base attributes (for restoring via escapes) */
    int           baseFgColorId;
};

iRegExp *makeAnsiEscapePattern_Text(iBool includeEscChar);

iText * new_Text                (SDL_Renderer *render);
void    delete_Text             (iText *);

void    init_Text               (iText *, SDL_Renderer *);
void    deinit_Text             (iText *);

void    setCurrent_Text         (iText *);
iText * current_Text            (void);

void    setDocumentFontSize_Text(iText *, float fontSizeFactor); /* affects all except `default*` fonts */
void    resetFonts_Text         (iText *);
void    resetFontCache_Text     (iText *);

enum iAnsiFlag {
    allowFg_AnsiFlag        = iBit(1),
    allowBg_AnsiFlag        = iBit(2),
    allowFontStyle_AnsiFlag = iBit(3),
    allowAll_AnsiFlag       = 0x7,
};

void    setOpacity_Text         (float opacity);
void    setBaseAttributes_Text  (int fontId, int fgColorId); /* current "normal" text attributes */
void    setAnsiFlags_Text       (int ansiFlags);
int     ansiFlags_Text          (void);

iChar   missing_Text            (size_t index);
void    resetMissing_Text       (iText *);
iBool   checkMissing_Text       (void); /* returns the flag, and clears it */
SDL_Texture *glyphCache_Text    (void);

/*----------------------------------------------------------------------------------------------*/

int     lineHeight_Text         (int fontId);
iRect   visualBounds_Text       (int fontId, iRangecc text);
int     fontWithSize_Text       (int fontId, enum iFontSize sizeId);
int     fontWithStyle_Text      (int fontId, enum iFontStyle styleId);
int     fontWithFamily_Text     (int fontId, enum iFontId familyId);

iTextMetrics    measureRange_Text       (int fontId, iRangecc text);
iTextMetrics    measureWrapRange_Text   (int fontId, int maxWidth, iRangecc text);
iTextMetrics    measureN_Text           (int fontId, const char *text, size_t n); /* `n` in characters */

iLocalDef iTextMetrics measure_Text(int fontId, const char *text) {
    return measureRange_Text(fontId, range_CStr(text));
}

iInt2   tryAdvance_Text         (int fontId, iRangecc text, int width, const char **endPos);
iInt2   tryAdvanceNoWrap_Text   (int fontId, iRangecc text, int width, const char **endPos);

enum iAlignment {
    left_Alignment,
    center_Alignment,
    right_Alignment,
};

void    cache_Text              (int fontId, iRangecc text); /* pre-render glyphs */

void    draw_Text               (int fontId, iInt2 pos, int color, const char *text, ...);
void    drawAlign_Text          (int fontId, iInt2 pos, int color, enum iAlignment align, const char *text, ...);
void    drawCentered_Text       (int fontId, iRect rect, iBool alignVisual, int color, const char *text, ...);
void    drawCenteredRange_Text  (int fontId, iRect rect, iBool alignVisual, int color, iRangecc text);
void    drawCenteredOutline_Text(int fontId, iRect rect, iBool alignVisual, int outlineColor,
                                 int fillColor, const char *text, ...);
void    drawString_Text         (int fontId, iInt2 pos, int color, const iString *text);
void    drawRange_Text          (int fontId, iInt2 pos, int color, iRangecc text);
void    drawRangeN_Text         (int fontId, iInt2 pos, int color, iRangecc text, size_t maxLen);
void    drawOutline_Text        (int fontId, iInt2 pos, int outlineColor, int fillColor, iRangecc text);
void    drawBoundRange_Text     (int fontId, iInt2 pos, int boundWidth, iBool justify, int color, iRangecc text); /* bound does not wrap */
int     drawWrapRange_Text      (int fontId, iInt2 pos, int maxWidth, int color, iRangecc text); /* returns new Y */

/*-----------------------------------------------------------------------------------------------*/

iDeclareType(TextBuf)
iDeclareTypeConstructionArgs(TextBuf, iWrapText *wrap, int fontId, int color)
    
struct Impl_TextBuf {
    SDL_Texture *texture;
    iInt2        size;
};

iTextBuf *  newRange_TextBuf(int font, int color, iRangecc text);
void        draw_TextBuf    (const iTextBuf *, iInt2 pos, int color);
