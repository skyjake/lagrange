/* Copyright 2026 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "gmdocument.h"

iDeclareType(DocumentStyle)
iDeclareType(DocumentStyleParams)

enum iDocumentStyleFlag {
    centered_DocumentStyleFlag         = iBit(1),
    keepBottomMargin_DocumentStyleFlag = iBit(2), /* applies even after blank lines */
};

struct Impl_DocumentStyleParams {
    const iDocPrefs *prefs;
    int              width;         /* layout width */
    int              outsideMargin; /* distance from layout edge to canvas edge */
    iBool            isMonospace;
    iBool            isGopher;
    iBool            isPaged;
    iBool            isPlainText;
    iBool            isDarkBackground;
    iBool            isUiTheme;     /* UI fonts */
    iBool            isBookCover;
};

/* Indents and margins are in units of gap_Text, vertical margins in body text lines. */
struct Impl_DocumentStyle {
    int   fonts[max_GmLineType];
    int   colors[max_GmLineType];
    int   flags[max_GmLineType];
    float indents[max_GmLineType];
    float rightMargins[max_GmLineType];
    float topMargins[max_GmLineType];
    float bottomMargins[max_GmLineType];
    float linkGroupMargin;  /* between links separated by a blank line */
    float verticalPad;      /* above and below the document */
    float ruleIndent;
    iBool hasLede;          /* first paragraph emphasized */
    int   ledeFont;
    int   ledeColor;
    int   maxLedeLines;
    iBool isFullWidthImages;
};

void    init_DocumentStyle  (iDocumentStyle *, const iDocumentStyleParams *params);
