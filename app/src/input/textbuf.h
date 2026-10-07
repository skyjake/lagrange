/* Copyright 2020-2026 Jaakko Keränen <jaakko.keranen@iki.fi>
   Copyright 2026 Sidney Cammeresi <sac@cheesecake.org>

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

#include "render/text.h"
#include "ui/inputwidget.h"

#include <the_Foundation/array.h>
#include <the_Foundation/range.h>
#include <the_Foundation/string.h>
#include <the_Foundation/vec2.h>

iDeclareType(EditBuf)
iDeclareType(InputLayout)
iDeclareType(InputLine)

/* All lines except the last one end in a newline. */
struct Impl_InputLine {
    iString text;
    iRanges range;      /* byte offset inside the entire content; for marking */
    iRangei wrapLines;  /* range of visual wrapped lines */
};

iLocalDef int numWrapLines_InputLine(const iInputLine *d) {
    return size_Range(&d->wrapLines);
}

struct Impl_InputLayout {
    iWrapText wrap;      /* `text` is set separately for each line */
    int       font;
    int       minWidth;  /* lines are not wrapped if `maxWidth` is at most this */
    iRangecc  insertion; /* shown at the cursor but not part of the content (IME preedit) */
};

struct Impl_EditBuf {
    iArray  lines;      /* iInputLine[] */
    iInt2   cursor;     /* x = byte offset, y = line index */
    iRanges mark;       /* byte offsets inside the entire content */
    iArray  undoStack;
};

void    init_EditBuf          (iEditBuf *);
void    deinit_EditBuf        (iEditBuf *);

void    setText_EditBuf       (iEditBuf *, const iString *text); /* cursor is not moved */
void    text_EditBuf          (const iEditBuf *, iString *text_out);
void    textRange_EditBuf     (const iEditBuf *, iRanges range, iString *text_out);

void    pushUndo_EditBuf      (iEditBuf *);
iBool   popUndo_EditBuf       (iEditBuf *); /* lines need relayout afterwards */
void    clearUndo_EditBuf     (iEditBuf *);

const iInputLine *  line_EditBuf          (const iEditBuf *, size_t index);
const iInputLine *  lastLine_EditBuf      (const iEditBuf *);
iInputLine *        cursorLine_EditBuf    (iEditBuf *);
iBool               isLastLine_EditBuf    (const iEditBuf *, const iInputLine *line);
int                 numWrapLines_EditBuf  (const iEditBuf *);
iBool               isEmpty_EditBuf       (const iEditBuf *);

iBool   isAllMarked_EditBuf   (const iEditBuf *);
void    markAll_EditBuf       (iEditBuf *);
iRanges mark_EditBuf          (const iEditBuf *); /* normalized */

int         endX_EditBuf          (const iEditBuf *, int y);
const char *charPos_EditBuf       (const iEditBuf *, iInt2 pos);
iChar       at_EditBuf            (const iEditBuf *, iInt2 pos);
iInt2       cursorMax_EditBuf     (const iEditBuf *);
iInt2       movedCursor_EditBuf   (const iEditBuf *, iInt2 pos, int xDir, int yDir);
iBool       movePos_EditBuf       (const iEditBuf *, iInt2 *pos, int dir);
iInt2       skipWord_EditBuf      (const iEditBuf *, iInt2 pos, int dir);
size_t      cursorToIndex_EditBuf (const iEditBuf *, iInt2 pos);
iInt2       indexToCursor_EditBuf (const iEditBuf *, size_t index);
void        extendRange_EditBuf   (const iEditBuf *, size_t *index, int dir);

iWrapText           wrap_EditBuf          (const iEditBuf *, const iInputLayout *, int y);
void                relayout_EditBuf      (iEditBuf *, const iInputLayout *, iRangei lines);
iInt2               relativeCoord_EditBuf (const iEditBuf *, const iInputLayout *, iInt2 pos);
const iInputLine *  findLineByWrapY_EditBuf(const iEditBuf *, int wrapY);
iRangei             visibleLines_EditBuf  (const iEditBuf *, iRangei visWrapLines);

/* These return the modified lines, which need relayout. */
iRangei insert_EditBuf        (iEditBuf *, iRangecc text, enum iInputMode mode, size_t maxLen);
int     delete_EditBuf        (iEditBuf *, iRanges range); /* first modified line, or -1 */
