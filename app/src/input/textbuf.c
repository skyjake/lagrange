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

#include "textbuf.h"

#include "ui/util.h"

static const size_t maxUndo_EditBuf_ = 64;

static void init_InputLine(iInputLine *d) {
    iZap(d->range);
    init_String(&d->text);
    d->wrapLines = (iRangei){ 0, 1 };
}

static void deinit_InputLine(iInputLine *d) {
    deinit_String(&d->text);
}

static void clearInputLines_(iArray *inputLines) {
    iForEach(Array, i, inputLines) {
        deinit_InputLine(i.value);
    }
    clear_Array(inputLines);
}

static void splitToLines_(const iString *text, iArray *inputLines) {
    clearInputLines_(inputLines);
    if (isEmpty_String(text)) {
        iInputLine empty;
        init_InputLine(&empty);
        pushBack_Array(inputLines, &empty);
        return;
    }
    size_t   index = 0;
    iRangecc seg   = iNullRange;
    if (startsWith_String(text, "\n")) { /* empty segment ignored at the start */
        iInputLine empty;
        init_InputLine(&empty);
        setCStr_String(&empty.text, "\n");
        empty.range = (iRanges){ 0, 1 };
        index = 1;
        pushBack_Array(inputLines, &empty);
    }
    while (nextSplit_Rangecc(range_String(text), "\n", &seg)) {
        iInputLine line;
        init_InputLine(&line);
        setRange_String(&line.text, seg);
        appendCStr_String(&line.text, "\n");
        line.range = (iRanges){ index, index + size_String(&line.text) };
        pushBack_Array(inputLines, &line);
        index = line.range.end;
    }
    if (endsWith_String(text, "\n")) { /* empty segment ignored at the end */
        iInputLine empty;
        init_InputLine(&empty);
        iInputLine *last = back_Array(inputLines);
        empty.range.start = empty.range.end = last->range.end;
        pushBack_Array(inputLines, &empty);
    }
    else {
        iInputLine *last = back_Array(inputLines);
        removeEnd_String(&last->text, 1);
        last->range.end--;
    }
    iAssert(((iInputLine *) back_Array(inputLines))->range.end == size_String(text));
}

static void mergeLinesRange_(const iArray *inputLines, iRanges range, iString *merged) {
    clear_String(merged);
    iConstForEach(Array, i, inputLines) {
        const iInputLine *line = i.value;
        const char *text = constBegin_String(&line->text);
        if (line->range.end <= range.start || line->range.start >= range.end) {
            continue; /* outside */
        }
        if (line->range.start >= range.start && line->range.end <= range.end) {
            append_String(merged, &line->text); /* complete */
        }
        else if (range.start <= line->range.start) {
            appendRange_String(merged, (iRangecc){ text, text + range.end - line->range.start });
        }
        else {
            const size_t from = range.start - line->range.start;
            appendRange_String(merged, (iRangecc){ text + from,
                                                   text + iMin(from + size_Range(&range),
                                                               size_Range(&line->range)) });
        }
    }
}

static void mergeLines_(const iArray *inputLines, iString *merged) {
    mergeLinesRange_(inputLines, (iRanges){ 0, iInvalidSize }, merged);
}

/*----------------------------------------------------------------------------------------------*/

iDeclareType(InputUndo)

struct Impl_InputUndo {
    iString text;
    iInt2 cursor;
};

static void init_InputUndo_(iInputUndo *d, const iArray *lines, iInt2 cursor) {
    init_String(&d->text);
    mergeLines_(lines, &d->text);
    d->cursor = cursor;
}

static void deinit_InputUndo_(iInputUndo *d) {
    deinit_String(&d->text);
}

/*----------------------------------------------------------------------------------------------*/

void init_EditBuf(iEditBuf *d) {
    init_Array(&d->lines, sizeof(iInputLine));
    init_Array(&d->undoStack, sizeof(iInputUndo));
    d->cursor = zero_I2();
    iZap(d->mark);
    splitToLines_(&iStringLiteral(""), &d->lines);
}

void deinit_EditBuf(iEditBuf *d) {
    clearInputLines_(&d->lines);
    clearUndo_EditBuf(d);
    deinit_Array(&d->undoStack);
    deinit_Array(&d->lines);
}

void setText_EditBuf(iEditBuf *d, const iString *text) {
    splitToLines_(text, &d->lines);
}

void text_EditBuf(const iEditBuf *d, iString *text_out) {
    mergeLines_(&d->lines, text_out);
}

void textRange_EditBuf(const iEditBuf *d, iRanges range, iString *text_out) {
    mergeLinesRange_(&d->lines, range, text_out);
}

void pushUndo_EditBuf(iEditBuf *d) {
    iInputUndo undo;
    init_InputUndo_(&undo, &d->lines, d->cursor);
    pushBack_Array(&d->undoStack, &undo);
    if (size_Array(&d->undoStack) > maxUndo_EditBuf_) {
        deinit_InputUndo_(front_Array(&d->undoStack));
        popFront_Array(&d->undoStack);
    }
}

iBool popUndo_EditBuf(iEditBuf *d) {
    if (!isEmpty_Array(&d->undoStack)) {
        iInputUndo *undo = back_Array(&d->undoStack);
        splitToLines_(&undo->text, &d->lines);
        d->cursor = undo->cursor;
        deinit_InputUndo_(undo);
        popBack_Array(&d->undoStack);
        iZap(d->mark);
        return iTrue;
    }
    return iFalse;
}

void clearUndo_EditBuf(iEditBuf *d) {
    iForEach(Array, i, &d->undoStack) {
        deinit_InputUndo_(i.value);
    }
    clear_Array(&d->undoStack);
}

const iInputLine *line_EditBuf(const iEditBuf *d, size_t index) {
    iAssert(!isEmpty_Array(&d->lines));
    return constAt_Array(&d->lines, index);
}

const iInputLine *lastLine_EditBuf(const iEditBuf *d) {
    iAssert(!isEmpty_Array(&d->lines));
    return constBack_Array(&d->lines);
}

iInputLine *cursorLine_EditBuf(iEditBuf *d) {
    return at_Array(&d->lines, d->cursor.y);
}

iBool isLastLine_EditBuf(const iEditBuf *d, const iInputLine *line) {
    return (const void *) line == constBack_Array(&d->lines);
}

int numWrapLines_EditBuf(const iEditBuf *d) {
    return lastLine_EditBuf(d)->wrapLines.end;
}

iBool isEmpty_EditBuf(const iEditBuf *d) {
    return size_Array(&d->lines) == 1 && isEmpty_String(&line_EditBuf(d, 0)->text);
}

iBool isAllMarked_EditBuf(const iEditBuf *d) {
    const iRanges all = { 0, lastLine_EditBuf(d)->range.end };
    return d->mark.start == all.start && d->mark.end == all.end;
}

void markAll_EditBuf(iEditBuf *d) {
    d->mark = (iRanges){ 0, lastLine_EditBuf(d)->range.end };
}

iRanges mark_EditBuf(const iEditBuf *d) {
    iRanges m = { iMin(d->mark.start, d->mark.end), iMax(d->mark.start, d->mark.end) };
    const iInputLine *last = lastLine_EditBuf(d);
    m.start   = iMin(m.start, last->range.end);
    m.end     = iMin(m.end, last->range.end);
    return m;
}

static const iString *lineString_EditBuf_(const iEditBuf *d, int y) {
    return &line_EditBuf(d, y)->text;
}

const char *charPos_EditBuf(const iEditBuf *d, iInt2 pos) {
    return cstr_String(lineString_EditBuf_(d, pos.y)) + pos.x;
}

int endX_EditBuf(const iEditBuf *d, int y) {
    /* The last line is not required to have an newline at the end. */
    const iInputLine *line = line_EditBuf(d, y);
    return line->range.end - (isLastLine_EditBuf(d, line) ? 0 : 1) - line->range.start;
}

static iBool isCursorFocusable_Char_(iChar c) {
    return !isDefaultIgnorable_Char(c) &&
           !isVariationSelector_Char(c) &&
           !isFitzpatrickType_Char(c);
}

iChar at_EditBuf(const iEditBuf *d, iInt2 pos) {
    if (pos.y >= 0 && pos.y < size_Array(&d->lines) &&
        pos.x >= 0 && pos.x <= endX_EditBuf(d, pos.y)) {
        iChar ch = 0;
        decodeBytes_MultibyteChar(charPos_EditBuf(d, pos),
                                  constEnd_String(lineString_EditBuf_(d, pos.y)),
                                  &ch);
        return ch;
    }
    return ' ';
}

iInt2 cursorMax_EditBuf(const iEditBuf *d) {
    const int yLast = size_Array(&d->lines) - 1;
    return init_I2(endX_EditBuf(d, yLast), yLast);
}

iInt2 movedCursor_EditBuf(const iEditBuf *d, iInt2 pos, int xDir, int yDir) {
    iChar ch = 0;
    int   n  = 0;
    /* TODO: The cursor should never land on any combining codepoints either. */
    for (;;) {
        if (xDir < 0) {
            if (pos.x == 0) {
                if (pos.y > 0) {
                    pos.x = endX_EditBuf(d, --pos.y);
                }
            }
            else {
                iAssert(pos.x > 0);
                n = decodePrecedingBytes_MultibyteChar(charPos_EditBuf(d, pos),
                                                       cstr_String(lineString_EditBuf_(d, pos.y)),
                                                       &ch);
                pos.x -= n;
                if (!isCursorFocusable_Char_(at_EditBuf(d, pos))) {
                    continue;
                }
            }
        }
        else if (xDir > 0) {
            if (pos.x == endX_EditBuf(d, pos.y)) {
                if (pos.y < size_Array(&d->lines) - 1) {
                    pos.y++;
                    pos.x = 0;
                }
            }
            else {
                n = decodeBytes_MultibyteChar(charPos_EditBuf(d, pos),
                                              constEnd_String(lineString_EditBuf_(d, pos.y)),
                                              &ch);
                pos.x += n;
                if (!isCursorFocusable_Char_(at_EditBuf(d, pos))) {
                    continue;
                }
            }
        }
        break;
    }
    return pos;
}

iBool movePos_EditBuf(const iEditBuf *d, iInt2 *pos, int dir) {
    iInt2 npos = movedCursor_EditBuf(d, *pos, dir, 0);
    if (isEqual_I2(*pos, npos)) {
        return iFalse;
    }
    *pos = npos;
    return iTrue;
}

static iBool isWordChar_EditBuf_(const iEditBuf *d, iInt2 pos) {
    return isAlphaNumeric_Char(at_EditBuf(d, pos));
}

iInt2 skipWord_EditBuf(const iEditBuf *d, iInt2 pos, int dir) {
    const iBool startedAtNonWord = !isWordChar_EditBuf_(d, pos);
    if (!movePos_EditBuf(d, &pos, dir)) {
        return pos;
    }
    /* Skip any non-word characters at start position. */
    while (!isWordChar_EditBuf_(d, pos)) {
        if (!movePos_EditBuf(d, &pos, dir)) {
            return pos;
        }
    }
    if (startedAtNonWord && dir > 0) {
        return pos; /* Found the start of a word. */
    }
    /* Skip the word. */
    while (isWordChar_EditBuf_(d, pos)) {
        if (!movePos_EditBuf(d, &pos, dir)) {
            return pos;
        }
    }
    if (dir > 0) {
        /* Skip to the beginning of the word. */
        while (!isWordChar_EditBuf_(d, pos)) {
            if (!movePos_EditBuf(d, &pos, dir)) {
                return pos;
            }
        }
    }
    else {
        movePos_EditBuf(d, &pos, +1);
    }
    return pos;
}

size_t cursorToIndex_EditBuf(const iEditBuf *d, iInt2 pos) {
    if (pos.y < 0) {
        return 0;
    }
    if (pos.y >= size_Array(&d->lines)) {
        return lastLine_EditBuf(d)->range.end;
    }
    const iInputLine *line = line_EditBuf(d, pos.y);
    pos.x = iClamp(pos.x, 0, endX_EditBuf(d, pos.y));
    return line->range.start + pos.x;
}

iInt2 indexToCursor_EditBuf(const iEditBuf *d, size_t index) {
    /* TODO: The lines are sorted; this could use a binary search. */
    iConstForEach(Array, i, &d->lines) {
        const iInputLine *line = i.value;
        if (contains_Range(&line->range, index)) {
            return init_I2(index - line->range.start, index_ArrayConstIterator(&i));
        }
    }
    return cursorMax_EditBuf(d);
}

void extendRange_EditBuf(const iEditBuf *d, size_t *index, int dir) {
    iInt2 pos = indexToCursor_EditBuf(d, *index);
    if (dir < 0) {
        while (movePos_EditBuf(d, &pos, dir)) {
            if (isSelectionBreaking_Char(at_EditBuf(d, pos))) {
                movePos_EditBuf(d, &pos, +1);
                break;
            }
        }
    }
    if (dir > 0) {
        while (!isSelectionBreaking_Char(at_EditBuf(d, pos)) &&
               movePos_EditBuf(d, &pos, dir)) {
            /* keep going */
        }
    }
    *index = cursorToIndex_EditBuf(d, pos);
}

/*----------------------------------------------------------------------------------------------*/

iWrapText wrap_EditBuf(const iEditBuf *d, const iInputLayout *layout, int y) {
    iWrapText wt = layout->wrap;
    wt.text = range_String(lineString_EditBuf_(d, y));
    return wt;
}

static void updateLine_EditBuf_(iEditBuf *d, const iInputLayout *layout, int y) {
    iInputLine *line = at_Array(&d->lines, y);
    iAssert(endsWith_String(&line->text, "\n") || isLastLine_EditBuf(d, line));
    iWrapText wrapText = wrap_EditBuf(d, layout, y);
    if (wrapText.maxWidth <= layout->minWidth) {
        line->wrapLines.end = line->wrapLines.start + 1;
        return;
    }
    /* Wrapping must account for the insertion at the cursor. */
    iString temp;
    const iBool hasInsertion = (y == d->cursor.y && !isEmpty_Range(&layout->insertion));
    if (hasInsertion) {
        initCopy_String(&temp, &line->text);
        insertData_Block(&temp.chars, d->cursor.x,
                         layout->insertion.start,
                         size_Range(&layout->insertion));
        wrapText.text = range_String(&temp);
    }
    const iTextMetrics tm = measure_WrapText(&wrapText, layout->font);
    line->wrapLines.end = line->wrapLines.start + height_Rect(tm.bounds) / lineHeight_Text(layout->font);
    iAssert(!isEmpty_Range(&line->wrapLines));
    if (hasInsertion) {
        deinit_String(&temp);
    }
}

static void updateLineRangesStartingFrom_EditBuf_(iEditBuf *d, int y) {
    iInputLine *line = at_Array(&d->lines, y);
    line->range.end = line->range.start + size_String(&line->text);
    for (size_t i = y + 1; i < size_Array(&d->lines); i++) {
        iInputLine *next  = at_Array(&d->lines, i);
        next->range.start = line->range.end;
        next->range.end   = next->range.start + size_String(&next->text);
        /* Update wrap line range as well. */
        next->wrapLines = (iRangei){
            line->wrapLines.end,
            line->wrapLines.end + numWrapLines_InputLine(next)
        };
        line = next;
    }
}

void relayout_EditBuf(iEditBuf *d, const iInputLayout *layout, iRangei lines) {
    for (int i = lines.start; i < lines.end; i++) {
        updateLine_EditBuf_(d, layout, i);
    }
    updateLineRangesStartingFrom_EditBuf_(d, lines.start);
}

iInt2 relativeCoord_EditBuf(const iEditBuf *d, const iInputLayout *layout, iInt2 pos) {
    /* Relative to the start of the line on which the position is. */
    iWrapText wt = wrap_EditBuf(d, layout, pos.y);
    wt.hitChar = wt.text.start + pos.x;
    measure_WrapText(&wt, layout->font);
    return wt.hitAdvance_out;
}

const iInputLine *findLineByWrapY_EditBuf(const iEditBuf *d, int wrapY) {
    iConstForEach(Array, i, &d->lines) {
        const iInputLine *line = i.value;
        if (contains_Range(&line->wrapLines, wrapY)) {
            return line;
        }
    }
    iAssert(iFalse); /* wrap y is out of bounds */
    return wrapY < 0 ? constFront_Array(&d->lines) : constBack_Array(&d->lines);
}

iRangei visibleLines_EditBuf(const iEditBuf *d, iRangei visWrapLines) {
    iRangei vis = { -1, -1 };
    /* Determine which lines are in the potentially visible range. */
    for (int i = 0; i < size_Array(&d->lines); i++) {
        const iInputLine *line = constAt_Array(&d->lines, i);
        if (vis.start < 0 && line->wrapLines.end > visWrapLines.start) {
            vis.start = vis.end = i;
        }
        if (line->wrapLines.start < visWrapLines.end) {
            vis.end = i + 1;
        }
        else break;
    }
    iAssert(isEmpty_Range(&vis) || (vis.start >= 0 && vis.end >= vis.start));
    return vis;
}

/*----------------------------------------------------------------------------------------------*/

iRangei insert_EditBuf(iEditBuf *d, iRangecc range, enum iInputMode mode, size_t maxLen) {
    iRangecc nextRange = { range.end, range.end };
    const int firstModified = d->cursor.y;
    for (; !isEmpty_Range(&range); range = nextRange) {
        /* If there's a newline, we'll need to break and begin a new line. */
        const char *newline = iStrStrN(range.start, "\n", size_Range(&range));
        if (newline) {
            nextRange = (iRangecc){ iMin(newline + 1, range.end), range.end };
            range.end = newline;
        }
        iInputLine *line = cursorLine_EditBuf(d);
        if (mode == insert_InputMode) {
            insertData_Block(&line->text.chars, d->cursor.x, range.start, size_Range(&range));
        }
        else {
            iAssert(!newline);
            setSubData_Block(&line->text.chars, d->cursor.x, range.start, size_Range(&range));
        }
        d->cursor.x += size_Range(&range);
        if (!newline) {
            break;
        }
        /* Split current line into a new line. */
        iInputLine split;
        init_InputLine(&split);
        setRange_String(&split.text, (iRangecc){
            cstr_String(&line->text) + d->cursor.x, constEnd_String(&line->text)
        });
        truncate_Block(&line->text.chars, d->cursor.x);
        if (!endsWith_String(&line->text, "\n")) {
            appendCStr_String(&line->text, "\n");
        }
        insert_Array(&d->lines, ++d->cursor.y, &split);
        d->cursor.x = 0;
    }
    if (maxLen > 0) {
        iAssert(size_Array(&d->lines) == 1);
        iAssert(d->cursor.y == 0);
        iInputLine *line = front_Array(&d->lines);
        size_t len = length_String(&line->text);
        if (len > maxLen) {
            removeEnd_String(&line->text, len - maxLen);
            d->cursor.x = endX_EditBuf(d, 0);
        }
    }
    return (iRangei){ firstModified, d->cursor.y + 1 };
}

int delete_EditBuf(iEditBuf *d, iRanges deleted) {
    int firstModified = -1;
    for (int i = size_Array(&d->lines) - 1; i >= 0; i--) {
        iInputLine *line = at_Array(&d->lines, i);
        if (line->range.end <= deleted.start) {
            break;
        }
        if (line->range.start >= deleted.end) {
            continue;
        }
        firstModified = i;
        if (line->range.start >= deleted.start && line->range.end <= deleted.end) {
            clear_String(&line->text);
        }
        else if (deleted.start > line->range.start && deleted.end >= line->range.end) {
            truncate_Block(&line->text.chars, deleted.start - line->range.start);
        }
        else if (deleted.start <= line->range.start && deleted.end <= line->range.end) {
            remove_Block(&line->text.chars, 0, deleted.end - line->range.start);
        }
        else if (deleted.start > line->range.start && deleted.end <= line->range.end) {
            remove_Block(&line->text.chars, deleted.start - line->range.start, size_Range(&deleted));
        }
        else {
            iAssert(iFalse); /* all cases exhausted */
        }
        if (i + 1 < size_Array(&d->lines) && !endsWith_String(&line->text, "\n")) {
            /* Newline deleted, so merge with next line. */
            iInputLine *nextLine = at_Array(&d->lines, i + 1);
            append_String(&line->text, &nextLine->text);
            deinit_InputLine(nextLine);
            remove_Array(&d->lines, i + 1);
        }
    }
    if (isEmpty_Array(&d->lines)) {
        /* Everything was deleted. */
        iInputLine empty;
        init_InputLine(&empty);
        pushBack_Array(&d->lines, &empty);
    }
    iZap(d->mark);
    return firstModified;
}
