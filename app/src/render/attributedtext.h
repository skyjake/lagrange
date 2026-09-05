/* Copyright 2020-2026 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "font.h"

iDeclareType(AttributedRun)

struct Impl_AttributedRun {
    iRangei     logical; /* UTF-32 codepoint indices in the logical-order text */
    iTextAttrib attrib;
    iBaseFont  *font;
    iColor      fgColor_; /* any RGB color; A > 0 */
    iColor      bgColor_; /* any RGB color; A > 0 */
    struct {
        uint8_t isLineBreak : 1;
        uint8_t script      : 7; /* if script detected */
    } flags;
};

iColor      fgColor_AttributedRun   (const iAttributedRun *);
iColor      bgColor_AttributedRun   (const iAttributedRun *);

/*-----------------------------------------------------------------------------------------------*/

iDeclareType(AttributedText)
iDeclareTypeConstructionArgs(AttributedText, iRangecc text, size_t maxLen, iAnyFont *font,
                             int colorId, int baseDir, iAnyFont *baseFont, int baseFgColorId,
                             iChar overrideChar)

struct Impl_AttributedText {
    iRangecc source; /* original source text */
    size_t   maxLen;
    iBaseFont *font;
    int      fgColorId;
    iBaseFont *baseFont;
    int      baseFgColorId;
    iBool    isBaseRTL;
    iArray   runs;
    iArray   logical;         /* UTF-32 text in logical order (mixed directions; matches source) */
    iArray   visual;          /* UTF-32 text in visual order (LTR) */
    iArray   logicalToVisual; /* map visual index to logical index */
    iArray   visualToLogical;
    iArray   logicalToSourceOffset; /* map logical character to UTF-8 offset in the source text */
    char *   bidiLevels;
};

const char *sourcePtr_AttributedText(const iAttributedText *, int logicalPos);
