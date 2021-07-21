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

#include "inputwidget.h"
#include "command.h"
#include "paint.h"
#include "util.h"
#include "keys.h"
#include "prefs.h"
#include "lang.h"
#include "app.h"

#include <the_Foundation/array.h>
#include <SDL_clipboard.h>
#include <SDL_timer.h>

#if defined (iPlatformAppleDesktop)
#   include "macos.h"
#endif

static const int    refreshInterval_InputWidget_ = 256;
static const size_t maxUndo_InputWidget_         = 64;

static void enableEditorKeysInMenus_(iBool enable) {
#if defined (iPlatformAppleDesktop)
    enableMenuItemsByKey_MacOS(SDLK_LEFT,  KMOD_PRIMARY, enable);
    enableMenuItemsByKey_MacOS(SDLK_RIGHT, KMOD_PRIMARY, enable);
    enableMenuItemsByKey_MacOS(SDLK_UP,    KMOD_PRIMARY, enable);
    enableMenuItemsByKey_MacOS(SDLK_DOWN,  KMOD_PRIMARY, enable);
    enableMenuItemsByKey_MacOS(SDLK_UP,    KMOD_PRIMARY | KMOD_SHIFT, enable);
    enableMenuItemsByKey_MacOS(SDLK_DOWN,  KMOD_PRIMARY | KMOD_SHIFT, enable);
#else
    iUnused(enable);
#endif
}

/*----------------------------------------------------------------------------------------------*/

iDeclareType(InputLine)
        
struct Impl_InputLine {
    iString text; /* UTF-8 */
    iRanges range; /* byte offset inside the entire content; for marking */
    iRangei wrapLines; /* range of visual wrapped lines */
};

static void init_InputLine(iInputLine *d) {
    iZap(d->range);
    init_String(&d->text);
    d->wrapLines = (iRangei){ 0, 1 };
}

iLocalDef int numWrapLines_InputLine_(const iInputLine *d) {
    return size_Range(&d->wrapLines);
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
        iInputLine line;
        init_InputLine(&line);
        pushBack_Array(inputLines, &line);
        return;
    }
    size_t   index = 0;
    iRangecc seg   = iNullRange;
    while (nextSplit_Rangecc(range_String(text), "\n", &seg)) {
        iInputLine line;
        init_InputLine(&line);
        setRange_String(&line.text, seg);
        appendCStr_String(&line.text, "\n");
        line.range = (iRanges){ index, index + size_String(&line.text) };
        pushBack_Array(inputLines, &line);
        index = line.range.end;
    }
    if (!endsWith_String(text, "\n")) {
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
        else if (line->range.start < range.start) {
            appendRange_String(merged, (iRangecc){ text, text + range.end - line->range.start });
        }
        else {
            appendRange_String(merged, (iRangecc){ text + range.start - line->range.start,
                                                   text + size_Range(&line->range) });
        }
    }
}

static void mergeLines_(const iArray *inputLines, iString *merged) {
    mergeLinesRange_(inputLines, (iRanges){ 0, iInvalidSize }, merged);
}

iDefineTypeConstruction(InputLine)
    
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

enum iInputWidgetFlag {
    isSensitive_InputWidgetFlag      = iBit(1),
    isUrl_InputWidgetFlag            = iBit(2), /* affected by decoding preference */
    enterPressed_InputWidgetFlag     = iBit(3),
    selectAllOnFocus_InputWidgetFlag = iBit(4),
    notifyEdits_InputWidgetFlag      = iBit(5),
    eatEscape_InputWidgetFlag        = iBit(6),
    isMarking_InputWidgetFlag        = iBit(7),
    markWords_InputWidgetFlag        = iBit(8),
    needUpdateBuffer_InputWidgetFlag = iBit(9),
    enterKeyEnabled_InputWidgetFlag  = iBit(10),
    enterKeyInsertsLineFeed_InputWidgetFlag = iBit(11),
};

/*----------------------------------------------------------------------------------------------*/

struct Impl_InputWidget {
    iWidget         widget;
    enum iInputMode mode;
    int             inFlags;
    size_t          maxLen; /* characters */
//    size_t          maxLayoutLines;
//    iArray          text;    /* iChar[] */
//    iArray          oldText; /* iChar[] */
    size_t          length; /* current length in characters */
    iArray          lines;    /* iInputLine[] */
    //iArray          oldLines; /* iInputLine[], for restoring if edits cancelled */
    iString         oldText; /* for restoring if edits cancelled */
    int             lastUpdateWidth;
    iString         srcHint;
    iString         hint;
    int             leftPadding;
    int             rightPadding;
//    size_t          cursor; /* offset from beginning */
//    size_t          lastCursor;
//    size_t          cursorLine;
    iInt2           cursor; /* cursor position: x = byte offset, y = line index */
    iInt2           prevCursor; /* previous cursor position */
//    int             verticalMoveX;
    iRangei         visWrapLines; /* which wrap lines are current visible */
//    int             visLineOffsetY; /* vertical offset of first visible line (pixels) */
    int             minWrapLines, maxWrapLines; /* min/max number of visible lines allowed */
//    int             scrollY; /* wrap lines */
    iRanges         mark;
    iRanges         initialMark;
    iArray          undoStack;
    int             font;
    iClick          click;
    int             cursorVis;
    uint32_t        timer;
    iTextBuf *      buffered; /* pre-rendered static text */
    iInputWidgetValidatorFunc validator;
    void *          validatorContext;
};

iDefineObjectConstructionArgs(InputWidget, (size_t maxLen), maxLen)
  
static void clearUndo_InputWidget_(iInputWidget *d) {
    iForEach(Array, i, &d->undoStack) {
        deinit_InputUndo_(i.value);
    }
    clear_Array(&d->undoStack);
}

iLocalDef iInt2 padding_(void) {
    return init_I2(gap_UI / 2, gap_UI / 2);
}

#define extraPaddingHeight_ (1.25f * gap_UI)

static iRect contentBounds_InputWidget_(const iInputWidget *d) {
    const iWidget *w      = constAs_Widget(d);
    iRect          bounds = adjusted_Rect(bounds_Widget(w),
                                 addX_I2(padding_(), d->leftPadding),
                                 neg_I2(addX_I2(padding_(), d->rightPadding)));
    shrink_Rect(&bounds, init_I2(gap_UI * (flags_Widget(w) & tight_WidgetFlag ? 1 : 2), 0));
    bounds.pos.y += padding_().y / 2;
    if (flags_Widget(w) & extraPadding_WidgetFlag) {
        bounds.pos.y += extraPaddingHeight_ / 2;
    }
    return bounds;
}

#if 0
static void updateCursorLine_InputWidget_(iInputWidget *d) {
    iWidget *w = as_Widget(d);
    d->cursorLine = 0;
    iConstForEach(Array, i, &d->lines) {
        const iInputLine *line = i.value;
        if (line->offset > d->cursor) {
            break;
        }
        d->cursorLine = index_ArrayConstIterator(&i);
    }
    /* May need to scroll to keep the cursor visible. */
    iWidget *flow = findOverflowScrollable_Widget(w);
    if (flow) {
        const iRect rootRect = { rect_Root(w->root).pos, visibleSize_Root(w->root) };
        int         yCursor  = contentBounds_InputWidget_(d).pos.y +
                               lineHeight_Text(d->font) * (int) d->cursorLine;
        const int margin = lineHeight_Text(d->font) * 3;
        if (yCursor < top_Rect(rootRect) + margin) {
            scrollOverflow_Widget(flow, top_Rect(rootRect) + margin - yCursor);
        }
        else if (yCursor > bottom_Rect(rootRect) - margin * 3 / 2) {
            scrollOverflow_Widget(flow, bottom_Rect(rootRect) - margin * 3 / 2 - yCursor);
        }
    }
}
#endif

iLocalDef iBool isLastLine_InputWidget_(const iInputWidget *d, const iInputLine *line) {
    return (const void *) line == constBack_Array(&d->lines);
}

iLocalDef const iInputLine *lastLine_InputWidget_(const iInputWidget *d) {
    iAssert(!isEmpty_Array(&d->lines));
    return constBack_Array(&d->lines);
}

static int numWrapLines_InputWidget_(const iInputWidget *d) {
    return lastLine_InputWidget_(d)->wrapLines.end;
}

static const iInputLine *line_InputWidget_(const iInputWidget *d, size_t index) {
    iAssert(!isEmpty_Array(&d->lines));
    return constAt_Array(&d->lines, index);
}

static const iString *lineString_InputWidget_(const iInputWidget *d, int y) {
    return &line_InputWidget_(d, y)->text;
}

static const char *charPos_InputWidget_(const iInputWidget *d, iInt2 pos) {
    return cstr_String(lineString_InputWidget_(d, pos.y)) + pos.x;
}

static int endX_InputWidget_(const iInputWidget *d, int y) {
    /* The last line is not required to have an newline at the end. */
    const iInputLine *line = line_InputWidget_(d, y);
    return line->range.end - (isLastLine_InputWidget_(d, line) ? 0 : 1) - line->range.start;
}

static iRangecc rangeSize_String(const iString *d, size_t size) {
    return (iRangecc){
        constBegin_String(d),
        constBegin_String(d) + iMin(size, size_String(d))
    };
}

static const iInputLine *findLineByWrapY_InputWidget_(const iInputWidget *d, int wrapY) {
    iConstForEach(Array, i, &d->lines) {
        const iInputLine *line = i.value;
        if (contains_Range(&line->wrapLines, wrapY)) {
            return line;
        }
    }
    iAssert(iFalse); /* wrap y is out of bounds */
    return wrapY < 0 ? constFront_Array(&d->lines) : constBack_Array(&d->lines);
}

static int visLineOffsetY_InputWidget_(const iInputWidget *d) {
    const iInputLine *line = findLineByWrapY_InputWidget_(d, d->visWrapLines.start);
    return (line->wrapLines.start - d->visWrapLines.start) * lineHeight_Text(d->font);
}

static void updateVisible_InputWidget_(iInputWidget *d) {
    const int totalWraps = numWrapLines_InputWidget_(d);
    const int visWraps = iClamp(totalWraps, d->minWrapLines, d->maxWrapLines);
    /* Resize the height of the editor. */
    d->visWrapLines.end = d->visWrapLines.start + visWraps;
    /* Determine which wraps are currently visible. */
    const iInputLine *curLine = constAt_Array(&d->lines, d->cursor.y);
    const int cursorY = curLine->wrapLines.start +
        measureRange_Text(d->font, rangeSize_String(&curLine->text, d->cursor.x))
        .advance.y / lineHeight_Text(d->font);
    
    //int scrollY = d->visLineOffsetY / lineHeight_Text(d->font);
    /* Scroll to cursor. */
    int delta = 0;
    if (d->visWrapLines.end < cursorY + 1) {
        delta = cursorY + 1 - d->visWrapLines.end;
    }
    else if (cursorY < d->visWrapLines.start) {
        delta = cursorY - d->visWrapLines.start;
    }
    d->visWrapLines.start += delta;
    d->visWrapLines.end   += delta;
    iAssert(contains_Range(&d->visWrapLines, cursorY));
//    iAssert(d->visWrapLines.start >= 0);
//    iAssert(d->visWrapLines.end <= totalWraps);
    
//    d->visLineOffsetY = scrollY * lineHeight_Text(d->font);
#if 0
//    const int numContentLines = numContentLines_InputWidget_(d);
    /* Expand or shrink the number of visible lines depending on how much content is available. */
    //d->visLines.end = d->visLines.start + iClamp(numContentLines, d->minVisLines, d->maxVisLines);
    const int maxVisLines = iMin(d->maxVisLines, size_Array(&d->lines));
    d->visLines.end = d->visLines.start;
    
    if (d->visLines.end > numContentLines) {
        const int avail = d->visLines.start;
        int offset = iMin(avail, d->visLines.end - numContentLines);
        d->visLines.start -= offset;
        d->visLines.end -= offset;
    }
    /* Keep the cursor visible. */
    if (d->cursor.y < d->visLines.start) {
        scrollVisLines_InputWidget_(d, d->cursor.y - d->visLines.start);
    }
    if (d->cursor.y > d->visLines.end - 1) {
        scrollVisLines_InputWidget_(d, d->cursor.y - d->visLines.end - 1);
    }
#endif
}

static void showCursor_InputWidget_(iInputWidget *d) {
    d->cursorVis = 2;
//    updateCursorLine_InputWidget_(d);
    updateVisible_InputWidget_(d);
}

static void invalidateBuffered_InputWidget_(iInputWidget *d) {
    if (d->buffered) {
        delete_TextBuf(d->buffered);
        d->buffered = NULL;
    }
}

static void updateSizeForFixedLength_InputWidget_(iInputWidget *d) {
    if (d->maxLen) {
        /* Set a fixed size based on maximum possible width of the text. */
        iBlock *content = new_Block(d->maxLen);
        fill_Block(content, 'M');
        int extraHeight = (flags_Widget(as_Widget(d)) & extraPadding_WidgetFlag ? extraPaddingHeight_ : 0);
        setFixedSize_Widget(
            as_Widget(d),
            add_I2(measure_Text(d->font, cstr_Block(content)).bounds.size,
                   init_I2(6 * gap_UI + d->leftPadding + d->rightPadding,
                           2 * gap_UI + extraHeight)));
        delete_Block(content);
    }
}

static iString *text_InputWidget_(const iInputWidget *d) {
    //return newUnicodeN_String(constData_Array(&d->text), size_Array(&d->text));
    iString *text = new_String();
    mergeLines_(&d->lines, text);
    return text;
}

static size_t length_InputWidget_(const iInputWidget *d) {
    /* Note: `d->length` is kept up to date, so don't call this normally. */
    size_t len = 0;
    iConstForEach(Array, i, &d->lines) {
        const iInputLine *line = i.value;
        len += length_String(&line->text);
    }
    return len;
}

static iString *visText_InputWidget_(const iInputWidget *d) {
    static const iChar sensitiveChar_ = 0x25cf; /* black circle */
    iString *text;
    if (~d->inFlags & isSensitive_InputWidgetFlag) {
        text = text_InputWidget_(d);
    }
    else {
        text = new_String();
        iAssert(d->length == length_InputWidget_(d));
        for (size_t i = 0; i < d->length; i++) {
            appendChar_String(text, sensitiveChar_);
        }
    }
    return text;
}

#if 0
static void clearLines_InputWidget_(iInputWidget *d) {
    iForEach(Array, i, &d->lines) {
        deinit_InputLine(i.value);
    }
    clear_Array(&d->lines);
}
#endif

#if 0
static void updateLines_InputWidget_(iInputWidget *d) {
    d->lastUpdateWidth = d->widget.rect.size.x;
    clearLines_InputWidget_(d);
    if (d->maxLen) {
        /* Everything on a single line. */
        iInputLine line;
        init_InputLine(&line);
        iString *u8 = visText_InputWidget_(d);
        set_String(&line.text, u8);
        line.len = length_String(u8);
        delete_String(u8);
        pushBack_Array(&d->lines, &line);
        updateCursorLine_InputWidget_(d);
        return;
    }
    /* Word-wrapped lines. */
    iString *u8 = visText_InputWidget_(d);
    size_t charPos = 0;
    iRangecc content = range_String(u8);
    const int wrapWidth = contentBounds_InputWidget_(d).size.x;
    while (wrapWidth > 0 && content.end != content.start) {
        const char *endPos;
        if (d->inFlags & isUrl_InputWidgetFlag) {
            tryAdvanceNoWrap_Text(d->font, content, wrapWidth, &endPos);
        }
        else {
            tryAdvance_Text(d->font, content, wrapWidth, &endPos);
        }
        const iRangecc part = (iRangecc){ content.start, endPos };
        iInputLine line;
        init_InputLine(&line);
        setRange_String(&line.text, part);
        line.offset = charPos;
        line.len    = length_String(&line.text);
        pushBack_Array(&d->lines, &line);
        charPos += line.len;
        content.start = endPos;        
    }
    if (isEmpty_Array(&d->lines) || endsWith_String(u8, "\n")) {
        /* Always at least one empty line. */
        iInputLine line;
        init_InputLine(&line);
        line.offset = charPos;
        pushBack_Array(&d->lines, &line);
    }
    else {
        iAssert(charPos == length_String(u8));
    }
    delete_String(u8);        
    updateCursorLine_InputWidget_(d);
}
#endif

static int contentHeight_InputWidget_(const iInputWidget *d) {
    return size_Range(&d->visWrapLines) * lineHeight_Text(d->font);
}

#if 0
static int contentHeight_InputWidget_(const iInputWidget *d, iBool forLayout) {
    int numLines = d->curVisLines;
    if (forLayout) {
        numLines = iMin(numLines, d->numLayoutLines);
    }
    return numLines * lineHeight_Text(d->font);
}
#endif

static void updateMetrics_InputWidget_(iInputWidget *d) {
    iWidget *w = as_Widget(d);
    updateSizeForFixedLength_InputWidget_(d);
    /* Caller must arrange the width, but the height is set here. */
    const int oldHeight = height_Rect(w->rect);
    w->rect.size.y = contentHeight_InputWidget_(d) + 3.0f * padding_().y; /* TODO: Why 3x? */
    if (flags_Widget(w) & extraPadding_WidgetFlag) {
        w->rect.size.y += extraPaddingHeight_;
    }
    invalidateBuffered_InputWidget_(d);
    if (height_Rect(w->rect) != oldHeight) {
        postCommand_Widget(d, "input.resized");
    }
}

static void updateLine_InputWidget_(iInputWidget *d, iInputLine *line) {
    iAssert(endsWith_String(&line->text, "\n") || isLastLine_InputWidget_(d, line));
    iWrapText wrapText = {
        .text     = range_String(&line->text),
        .maxWidth = width_Rect(contentBounds_InputWidget_(d)),
        .mode     = (d->inFlags & isUrl_InputWidgetFlag ? anyCharacter_WrapTextMode
                                                        : word_WrapTextMode),
    };
    if (wrapText.maxWidth <= 0) {
        line->wrapLines.end = line->wrapLines.start + 1;
        return;
    }
    const iTextMetrics tm = measure_WrapText(&wrapText, d->font);
    line->wrapLines.end = line->wrapLines.start + height_Rect(tm.bounds) / lineHeight_Text(d->font);
}

static void updateAllLinesAndResizeHeight_InputWidget_(iInputWidget *d) {
    const int oldWraps = numWrapLines_InputWidget_(d);
    iForEach(Array, i, &d->lines) {
        updateLine_InputWidget_(d, i.value); /* count number of visible lines */
    }
    if (oldWraps != numWrapLines_InputWidget_(d)) {
//        d->click.minHeight = contentHeight_InputWidget_(d, iFalse);
        updateMetrics_InputWidget_(d);
    }
}

void init_InputWidget(iInputWidget *d, size_t maxLen) {
    iWidget *w = &d->widget;
    init_Widget(w);
    d->validator = NULL;
    d->validatorContext = NULL;
    setFlags_Widget(w, focusable_WidgetFlag | hover_WidgetFlag | touchDrag_WidgetFlag, iTrue);
#if defined (iPlatformMobile)
    setFlags_Widget(w, extraPadding_WidgetFlag, iTrue);
#endif
    init_Array(&d->lines, sizeof(iInputLine));
    //init_Array(&d->oldText, sizeof(iChar));
    init_String(&d->oldText);
    init_Array(&d->lines, sizeof(iInputLine));
    init_String(&d->srcHint);
    init_String(&d->hint);
    init_Array(&d->undoStack, sizeof(iInputUndo));
    d->font         = uiInput_FontId | alwaysVariableFlag_FontId;
    d->leftPadding  = 0;
    d->rightPadding = 0;
    d->cursor       = zero_I2();
    d->prevCursor   = zero_I2();
    //d->lastCursor   = 0;
    //d->cursorLine   = 0;
    d->lastUpdateWidth = 0;
    //d->verticalMoveX   = -1; /* TODO: Use this. */
    d->inFlags         = eatEscape_InputWidgetFlag | enterKeyEnabled_InputWidgetFlag;
    if (deviceType_App() != desktop_AppDeviceType) {
        d->inFlags |= enterKeyInsertsLineFeed_InputWidgetFlag;
    }
    iZap(d->mark);
    setMaxLen_InputWidget(d, maxLen);
    d->visWrapLines.start = 0;
    d->visWrapLines.end = 1;
//    d->visLineOffsetY = 0;
    d->maxWrapLines = maxLen > 0 ? 1 : 20; /* TODO: Choose maximum dynamically? */
    d->minWrapLines = 1;
    splitToLines_(&iStringLiteral(""), &d->lines);
    //d->maxLayoutLines = iInvalidSize;
    setFlags_Widget(w, fixedHeight_WidgetFlag, iTrue); /* resizes its own height */
    init_Click(&d->click, d, SDL_BUTTON_LEFT);
    d->timer = 0;
    d->cursorVis = 0;
    d->buffered = NULL;
    //updateLines_InputWidget_(d);
    updateMetrics_InputWidget_(d);
}

void deinit_InputWidget(iInputWidget *d) {
    clearInputLines_(&d->lines);
    if (isSelected_Widget(d)) {
        SDL_StopTextInput();
        enableEditorKeysInMenus_(iTrue);
    }
    delete_TextBuf(d->buffered);
    clearUndo_InputWidget_(d);
    deinit_Array(&d->undoStack);
    if (d->timer) {
        SDL_RemoveTimer(d->timer);
    }
    deinit_String(&d->srcHint);
    deinit_String(&d->hint);
    deinit_String(&d->oldText);
    deinit_Array(&d->lines);
}

void setFont_InputWidget(iInputWidget *d, int fontId) {
    d->font = fontId;
    updateMetrics_InputWidget_(d);
}

static void pushUndo_InputWidget_(iInputWidget *d) {
    iInputUndo undo;
    init_InputUndo_(&undo, &d->lines, d->cursor);
    pushBack_Array(&d->undoStack, &undo);
    if (size_Array(&d->undoStack) > maxUndo_InputWidget_) {
        deinit_InputUndo_(front_Array(&d->undoStack));
        popFront_Array(&d->undoStack);
    }
}

static iBool popUndo_InputWidget_(iInputWidget *d) {
    if (!isEmpty_Array(&d->undoStack)) {
        iInputUndo *undo = back_Array(&d->undoStack);
        //setCopy_Array(&d->text, &undo->text);
        splitToLines_(&undo->text, &d->lines);
        d->cursor = undo->cursor;
        deinit_InputUndo_(undo);
        popBack_Array(&d->undoStack);
        iZap(d->mark);
        return iTrue;
    }
    return iFalse;
}

void setMode_InputWidget(iInputWidget *d, enum iInputMode mode) {
    d->mode = mode;
}

#if 0
static void restoreDefaultScheme_(iString *url) {
    iUrl parts;
    init_Url(&parts, url);
    if (isEmpty_Range(&parts.scheme) && startsWith_String(url, "//")) {
        prependCStr_String(url, "gemini:");
    }
}

static const iString *omitDefaultScheme_(iString *url) {
    if (startsWithCase_String(url, "gemini://")) {
        remove_Block(&url->chars, 0, 7);
    }
    return url;
}
#endif

const iString *text_InputWidget(const iInputWidget *d) {
    if (d) {
        iString *text = collect_String(text_InputWidget_(d));
#if 0
        if (d->inFlags & isUrl_InputWidgetFlag) {
            /* Add the "gemini" scheme back if one is omitted. */
            restoreDefaultScheme_(text);
        }
#endif
        return text;
    }
    return collectNew_String();
}

iInputWidgetContentPadding contentPadding_InputWidget(const iInputWidget *d) {
    return (iInputWidgetContentPadding){ d->leftPadding, d->rightPadding };
}

void setMaxLen_InputWidget(iInputWidget *d, size_t maxLen) {
    d->maxLen = maxLen;
    d->mode   = (maxLen == 0 ? insert_InputMode : overwrite_InputMode);
    updateSizeForFixedLength_InputWidget_(d);
}

void setLineLimits_InputWidget(iInputWidget *d, int minLines, int maxLines) {
    d->minWrapLines = minLines;
    d->maxWrapLines = maxLines;
    updateVisible_InputWidget_(d);
    updateMetrics_InputWidget_(d);
}

void setValidator_InputWidget(iInputWidget *d, iInputWidgetValidatorFunc validator, void *context) {
    d->validator = validator;
    d->validatorContext = context;
}

void setEnterInsertsLF_InputWidget(iInputWidget *d, iBool enterInsertsLF) {
    iChangeFlags(d->inFlags, enterKeyInsertsLineFeed_InputWidgetFlag, enterInsertsLF);
}

void setEnterKeyEnabled_InputWidget(iInputWidget *d, iBool enterKeyEnabled) {
    iChangeFlags(d->inFlags, enterKeyEnabled_InputWidgetFlag, enterKeyEnabled);
}

void setHint_InputWidget(iInputWidget *d, const char *hintText) {
    /* Keep original for retranslations. */
    setCStr_String(&d->srcHint, hintText);
    set_String(&d->hint, &d->srcHint);
    translate_Lang(&d->hint);
}

void setContentPadding_InputWidget(iInputWidget *d, int left, int right) {
    if (left >= 0) {
        d->leftPadding = left;
    }
    if (right >= 0) {
        d->rightPadding = right;
    }
    updateSizeForFixedLength_InputWidget_(d);
    refresh_Widget(d);
}

static iBool isHintVisible_InputWidget_(const iInputWidget *d) {
    return !isEmpty_String(&d->hint) && isEmpty_Array(&d->lines);/* size_Array(&d->lines) <= 1 &&
           isEmpty_String(&line_InputWidget_(d, 0)->text);*/
}

static void updateBuffered_InputWidget_(iInputWidget *d) {
    invalidateBuffered_InputWidget_(d);
#if 0
    if (isHintVisible_InputWidget_(d)) {
        d->buffered = new_TextBuf(d->font, uiAnnotation_ColorId, cstr_String(&d->hint));                
    }
    else {
        iString *bufText = NULL;
        if (!bufText) {
            bufText = visText_InputWidget_(d);
        }
        const int   maxWidth = contentBounds_InputWidget_(d).size.x;
        const int   fg       = uiInputText_ColorId;
        const char *text     = cstr_String(bufText);
        d->buffered =
            (d->inFlags & isUrl_InputWidgetFlag ? newBound_TextBuf(d->font, fg, maxWidth, text)
                                                : newWrap_TextBuf (d->font, fg, maxWidth, text));
        delete_String(bufText);
    }
#endif
    d->inFlags &= ~needUpdateBuffer_InputWidgetFlag;
}

iLocalDef iInputLine *cursorLine_InputWidget_(iInputWidget *d) {
    return at_Array(&d->lines, d->cursor.y);
}

iLocalDef const iInputLine *constCursorLine_InputWidget_(const iInputWidget *d) {
    return constAt_Array(&d->lines, d->cursor.y);
}

iLocalDef iInt2 cursorMax_InputWidget_(const iInputWidget *d) {
    const int yLast = size_Array(&d->lines) - 1;
    return init_I2(endX_InputWidget_(d, yLast), yLast);
}

void setText_InputWidget(iInputWidget *d, const iString *text) {
    if (!d) return;
    if (d->inFlags & isUrl_InputWidgetFlag) {
        /* If user wants URLs encoded, also Punycode the domain. */
        if (!prefs_App()->decodeUserVisibleURLs) {
            iString *enc = collect_String(copy_String(text));
            /* Prevent address bar spoofing (mentioned as IDN homograph attack in
               https://github.com/skyjake/lagrange/issues/73) */
            punyEncodeUrlHost_String(enc);
            text = enc;
        }
#if 0
        /* Omit the default (Gemini) scheme if there isn't much space. */
        if (isNarrow_Root(as_Widget(d)->root)) {
            text = omitDefaultScheme_(collect_String(copy_String(text)));
        }
#endif
    }
    clearUndo_InputWidget_(d);
    //clear_Array(&d->text);
    iString *nfcText = collect_String(copy_String(text));
    normalize_String(nfcText);
//    iConstForEach(String, i, nfcText) {
//        pushBack_Array(&d->text, &i.value);
//    }
    splitToLines_(nfcText, &d->lines);
    iAssert(!isEmpty_Array(&d->lines));
    iForEach(Array, i, &d->lines) {
        updateLine_InputWidget_(d, i.value); /* count number of visible lines */
    }
    if (isFocused_Widget(d)) {
        d->cursor = cursorMax_InputWidget_(d);
    }
    else {
        d->cursor.y = iMin(d->cursor.y, (int) size_Array(&d->lines) - 1);
        d->cursor.x = iMin(d->cursor.x, size_String(&cursorLine_InputWidget_(d)->text));
        iZap(d->mark);
    }
    if (!isFocused_Widget(d)) {
        d->inFlags |= needUpdateBuffer_InputWidgetFlag;
    }
//    updateLinesAndResize_InputWidget_(d);
    updateVisible_InputWidget_(d);
    refresh_Widget(as_Widget(d));
}

void setTextCStr_InputWidget(iInputWidget *d, const char *cstr) {
    iString *str = newCStr_String(cstr);
    setText_InputWidget(d, str);
    delete_String(str);
}

static uint32_t cursorTimer_(uint32_t interval, void *w) {
    iInputWidget *d = w;
    if (d->cursorVis > 1) {
        d->cursorVis--;
    }
    else {
        d->cursorVis ^= 1;
    }
    refresh_Widget(w);
    return interval;
}

static size_t cursorToIndex_InputWidget_(const iInputWidget *d, iInt2 pos) {
    /* TODO: Lazy linear position updates. */
    if (pos.y < 0) {
        return 0;
    }
    if (pos.y >= size_Array(&d->lines)) {
        return lastLine_InputWidget_(d)->range.end;
    }
    const iInputLine *line = line_InputWidget_(d, pos.y);
    pos.x = iClamp(pos.x, 0, endX_InputWidget_(d, pos.y));
    return line->range.start + pos.x;
}

static iInt2 indexToCursor_InputWidget_(const iInputWidget *d, size_t index) {
    /* TODO: Lazy linear position updates. */
    /* TODO: The lines are sorted; this could use a binary search. */
    iConstForEach(Array, i, &d->lines) {
        const iInputLine *line = i.value;
        if (contains_Range(&line->range, index)) {
            return init_I2(index - line->range.start, index_ArrayConstIterator(&i));
        }
    }
    return cursorMax_InputWidget_(d);
}

void selectAll_InputWidget(iInputWidget *d) {
    d->mark = (iRanges){ 0, lastLine_InputWidget_(d)->range.end };
    refresh_Widget(as_Widget(d));
}

iLocalDef iBool isEditing_InputWidget_(const iInputWidget *d) {
    return (flags_Widget(constAs_Widget(d)) & selected_WidgetFlag) != 0;
}

void begin_InputWidget(iInputWidget *d) {
    iWidget *w = as_Widget(d);
    if (isEditing_InputWidget_(d)) {
        /* Already active. */
        return;
    }
    invalidateBuffered_InputWidget_(d);
    setFlags_Widget(w, hidden_WidgetFlag | disabled_WidgetFlag, iFalse);
    //setCopy_Array(&d->oldText, &d->text);
    mergeLines_(&d->lines, &d->oldText);
    if (d->mode == overwrite_InputMode) {
        d->cursor = zero_I2();
    }
    else {
        d->cursor.y = iMin(d->cursor.y, size_Array(&d->lines) - 1);
        d->cursor.x = iMin(d->cursor.x, cursorLine_InputWidget_(d)->range.end);
    }
//    updateCursorLine_InputWidget_(d);
    SDL_StartTextInput();
    setFlags_Widget(w, selected_WidgetFlag, iTrue);
#if 0
    if (d->maxLayoutLines != iInvalidSize) {
        /* This will extend beyond the arranged region. */
        setFlags_Widget(w, keepOnTop_WidgetFlag, iTrue);
    }
#endif
    showCursor_InputWidget_(d);
    refresh_Widget(w);
    d->timer = SDL_AddTimer(refreshInterval_InputWidget_, cursorTimer_, d);
    d->inFlags &= ~enterPressed_InputWidgetFlag;
    if (d->inFlags & selectAllOnFocus_InputWidgetFlag) {
        d->mark = (iRanges){ 0, lastLine_InputWidget_(d)->range.end };
        d->cursor = cursorMax_InputWidget_(d);
    }
    else {
        iZap(d->mark);
    }
    enableEditorKeysInMenus_(iFalse);
}

void end_InputWidget(iInputWidget *d, iBool accept) {
    iWidget *w = as_Widget(d);
    if (!isEditing_InputWidget_(d)) {
        /* Was not active. */
        return;
    }
    enableEditorKeysInMenus_(iTrue);
    if (!accept) {
        //setCopy_Array(&d->text, &d->oldText);
        /* Overwrite the edited lines. */
        splitToLines_(&d->oldText, &d->lines);
    }
    d->inFlags |= needUpdateBuffer_InputWidgetFlag;
    SDL_RemoveTimer(d->timer);
    d->timer = 0;
    SDL_StopTextInput();
    setFlags_Widget(w, selected_WidgetFlag | keepOnTop_WidgetFlag, iFalse);
    const char *id = cstr_String(id_Widget(as_Widget(d)));
    if (!*id) id = "_";
//    updateLinesAndResize_InputWidget_(d);
    refresh_Widget(w);
    postCommand_Widget(w,
                       "input.ended id:%s enter:%d arg:%d",
                       id,
                       d->inFlags & enterPressed_InputWidgetFlag ? 1 : 0,
                       accept ? 1 : 0);
}

static void updateLineRangesStartingFrom_InputWidget_(iInputWidget *d, int y) {
    iInputLine *line = at_Array(&d->lines, y);
    line->range.end = line->range.start + size_String(&line->text);
    for (size_t i = y + 1; i < size_Array(&d->lines); i++) {
        iInputLine *next  = at_Array(&d->lines, i);
        next->range.start = line->range.end;
        next->range.end   = next->range.start + size_String(&next->text);
        /* Update wrap line range as well. */
        next->wrapLines = (iRangei){
            line->wrapLines.end,
            line->wrapLines.end + numWrapLines_InputLine_(next)
        };
        line = next;
    }
}

static void textOfLinesWasChanged_InputWidget_(iInputWidget *d, iRangei lineRange) {
    for (int i = lineRange.start; i < lineRange.end; i++) {
        updateLine_InputWidget_(d, at_Array(&d->lines, i));
    }
    updateLineRangesStartingFrom_InputWidget_(d, lineRange.start);
    updateVisible_InputWidget_(d);
    updateMetrics_InputWidget_(d);
}

static void insertRange_InputWidget_(iInputWidget *d, iRangecc range) {
    iWidget *w = as_Widget(d);
    iRangecc nextRange = { range.end, range.end };
    const int firstModified = d->cursor.y;
    for (;; range = nextRange) {
        /* If there's a newline, we'll need to break and begin a new line. */
        const char *newline = iStrStrN(range.start, "\n", size_Range(&range));
        if (newline) {
            nextRange = (iRangecc){ newline + 1, range.end };
            range.end = newline;
        }
        iInputLine *line = cursorLine_InputWidget_(d);
        if (d->mode == insert_InputMode) {
            insertData_Block(&line->text.chars, d->cursor.x, range.start, size_Range(&range));
            d->cursor.x += size_Range(&range);
        }
        if (!newline) {
            break;
        }
        /* Split current line into a new line. */
        iInputLine split;
        init_InputLine(&split);
        setRange_String(&split.text, (iRangecc){
            cstr_String(&line->text) + d->cursor.x, constEnd_String(&line->text)
        });
        truncate_String(&line->text, d->cursor.x);
        appendCStr_String(&line->text, "\n");
        insert_Array(&d->lines, ++d->cursor.y, &split);
        d->cursor.x = 0;
    }
    textOfLinesWasChanged_InputWidget_(d, (iRangei){ firstModified, d->cursor.y + 1 });
#if 0
    else if (d->maxLen == 0 || d->cursor < d->maxLen) {
        if (d->cursor >= size_Array(&d->text)) {
            resize_Array(&d->text, d->cursor + 1);
        }
        set_Array(&d->text, d->cursor++, &chr);
        if (d->maxLen > 1 && d->cursor == d->maxLen) {
            iWidget *nextFocus = findFocusable_Widget(w, forward_WidgetFocusDir);
            setFocus_Widget(nextFocus == w ? NULL : nextFocus);
        }
        else if (d->maxLen == 1) {
            d->cursor = 0;
        }
    }
#endif
    showCursor_InputWidget_(d);
    refresh_Widget(as_Widget(d));    
}

static void insertChar_InputWidget_(iInputWidget *d, iChar chr) {
    iMultibyteChar mb;
    init_MultibyteChar(&mb, chr);
    insertRange_InputWidget_(d, range_CStr(mb.bytes));
}

iLocalDef iBool isMarking_(void) {
    return (modState_Keys() & KMOD_SHIFT) != 0;
}

void setCursor_InputWidget(iInputWidget *d, iInt2 pos) {
    iAssert(!isEmpty_Array(&d->lines));
    pos.x = iClamp(pos.x, 0, endX_InputWidget_(d, pos.y));
    d->cursor = pos;
    /* Update selection. */
    if (isMarking_()) {
        if (isEmpty_Range(&d->mark)) {
            d->mark.start = cursorToIndex_InputWidget_(d, d->prevCursor);
            d->mark.end   = cursorToIndex_InputWidget_(d, d->cursor);
        }
        else {
            d->mark.end = cursorToIndex_InputWidget_(d, d->cursor);
        }
    }
    else {
        iZap(d->mark);
    }
    showCursor_InputWidget_(d);
}

#if 0
static size_t indexForRelativeX_InputWidget_(const iInputWidget *d, int x, const iInputLine *line) {
    size_t index = line->range.start;
    if (x <= 0) {
        return index;
    }
    const char *endPos;
    tryAdvanceNoWrap_Text(d->font, range_String(&line->text), x, &endPos);
    if (endPos == constEnd_String(&line->text)) {
        index = line->range.end - 1;
    }
    else {
#if 0
        /* Need to know the actual character index. */
        /* TODO: tryAdvance could tell us this directly with an extra return value */
        iConstForEach(String, i, &line->text) {
            if (i.pos >= endPos) break;
            index++;
        }
#endif
        index = line->range.start + (endPos - constBegin_String(&line->text));
    }
#if 0
    if (!isLastLine_InputWidget_(d, line) && index == line->offset + line->len) {
        index = iMax(index - 1, line->offset);
    }
#endif
    return iMax(index, line->range.start);
}
#endif

iDeclareType(LineMover)

struct Impl_LineMover {
    iInputWidget *d;
    int dir;
    int x;
};

static iBool findXBreaks_LineMover_(iWrapText *wrap, iRangecc wrappedText,
                                    int origin, int advance, iBool isBaseRTL) {
    iUnused(isBaseRTL);
    
}

static iBool moveCursorByLine_InputWidget_(iInputWidget *d, int dir) {
    const iInputLine *line = cursorLine_InputWidget_(d);
    iRangecc text = range_String(&line->text);
    iWrapText wrapText = {
        .text     = rangeSize_String(&line->text, d->cursor.x),
        .maxWidth = width_Rect(contentBounds_InputWidget_(d)),
        .mode     = (d->inFlags & isUrl_InputWidgetFlag ? anyCharacter_WrapTextMode
                                                        : word_WrapTextMode),
    };
    iInt2 relCoord = measure_WrapText(&wrapText, d->font).advance;
    const int cursorWidth = measureN_Text(d->font, charPos_InputWidget_(d, d->cursor), 1).advance.x;
    int relLine = relCoord.y / lineHeight_Text(d->font);
    if ((dir < 0 && relLine > 0) || (dir > 0 && relLine < numWrapLines_InputLine_(line) - 1)) {
        /* We can just change the cursor X value, but we'll have to check where the cursor lands. */
        relCoord.y += dir * lineHeight_Text(d->font);
    }
    else if (dir < 0 && d->cursor.y > 0) {
        d->cursor.y--;
        line = cursorLine_InputWidget_(d);
        relCoord.y = lineHeight_Text(d->font) * (numWrapLines_InputLine_(line) - 1);
    }
    else if (dir > 0 && d->cursor.y < size_Array(&d->lines) - 1) {
        d->cursor.y++;
        line = cursorLine_InputWidget_(d);
        relCoord.y = 0;
    }
    else {
        return iFalse;
    }
    wrapText.text     = range_String(&cursorLine_InputWidget_(d)->text);
    wrapText.hitPoint = addY_I2(relCoord, 1); //arelCddX_I2(relCoord, cursorWidth),
    measure_WrapText(&wrapText, d->font);
    if (wrapText.hitChar_out) {
        d->cursor.x = wrapText.hitChar_out - wrapText.text.start;
    }
    else {
        d->cursor.x = endX_InputWidget_(d, d->cursor.y);
    }
    /*
    if (wrapText.hitGlyphNormX_out > 0.5f && d->cursor.x < endX_InputWidget_(d, d->cursor.y)) {
        iChar ch;
        int n = decodeBytes_MultibyteChar(wrapText.text.start + d->cursor.x, wrapText.text.end, &ch);
        if (n > 0) {
            d->cursor.x += n;
        }
    }*/
    //showCursor_InputWidget_(d);
    setCursor_InputWidget(d, d->cursor);
    return iTrue;
    
#if 0
    const iInputLine *line = line_InputWidget_(d, d->cursorLine);
    int xPos1 = maxWidth_TextMetrics(measureN_Text(d->font,
                             cstr_String(&line->text),
                             d->cursor - line->offset));
    int xPos2 = maxWidth_TextMetrics(measureN_Text(d->font,
                              cstr_String(&line->text),
                              iMin(d->cursor + 1 - line->offset, line->len)));
    const int xPos = (xPos1 + xPos2) / 2;
    size_t newCursor = iInvalidPos;
    const size_t numLines = size_Array(&d->lines);
    if (dir < 0 && d->cursorLine > 0) {
        newCursor = indexForRelativeX_InputWidget_(d, xPos, --line);
    }
    else if (dir > 0 && d->cursorLine < numLines - 1) {
        newCursor = indexForRelativeX_InputWidget_(d, xPos, ++line);
    }
    if (newCursor != iInvalidPos) {
        setCursor_InputWidget(d, newCursor);
        return iTrue;
    }
#endif
    return iFalse;
}

void setSensitiveContent_InputWidget(iInputWidget *d, iBool isSensitive) {
    iChangeFlags(d->inFlags, isSensitive_InputWidgetFlag, isSensitive);
}

void setUrlContent_InputWidget(iInputWidget *d, iBool isUrl) {
    iChangeFlags(d->inFlags, isUrl_InputWidgetFlag, isUrl);
    d->inFlags |= needUpdateBuffer_InputWidgetFlag;
}

void setSelectAllOnFocus_InputWidget(iInputWidget *d, iBool selectAllOnFocus) {
    iChangeFlags(d->inFlags, selectAllOnFocus_InputWidgetFlag, selectAllOnFocus);
}

void setNotifyEdits_InputWidget(iInputWidget *d, iBool notifyEdits) {
    iChangeFlags(d->inFlags, notifyEdits_InputWidgetFlag, notifyEdits);
}

void setEatEscape_InputWidget(iInputWidget *d, iBool eatEscape) {
    iChangeFlags(d->inFlags, eatEscape_InputWidgetFlag, eatEscape);
}

static iRanges mark_InputWidget_(const iInputWidget *d) {
    iRanges m = { iMin(d->mark.start, d->mark.end), iMax(d->mark.start, d->mark.end) };
    const iInputLine *last = lastLine_InputWidget_(d);
    m.start   = iMin(m.start, last->range.end);
    m.end     = iMin(m.end, last->range.end);
    return m;
}

static void contentsWereChanged_InputWidget_(iInputWidget *d) {
    if (d->validator) {
        d->validator(d, d->validatorContext); /* this may change the contents */
    }
//    updateLinesAndResize_InputWidget_(d);
    if (d->inFlags & notifyEdits_InputWidgetFlag) {
        postCommand_Widget(d, "input.edited id:%s", cstr_String(id_Widget(constAs_Widget(d))));
    }
}

static void deleteIndexRange_InputWidget_(iInputWidget *d, iRanges deleted) {
    size_t firstModified = iInvalidPos;
    for (size_t i = 0; i < size_Array(&d->lines); i++) {
        iInputLine *line = at_Array(&d->lines, i);
        if (line->range.end < deleted.start) {
            continue;
        }
        if (line->range.start >= deleted.end) {
            break;
        }
        if (firstModified == iInvalidPos) {
            firstModified = i;
        }
        if (line->range.start >= deleted.start && line->range.end <= deleted.end) {
            /* Delete the entire line. */
            deinit_InputLine(line);
            remove_Array(&d->lines, i--);
            continue;
        }
        else if (deleted.start > line->range.start && deleted.end >= line->range.end) {
            truncate_Block(&line->text.chars, deleted.start - line->range.start);
        }
        else if (deleted.start <= line->range.start && deleted.end < line->range.end) {
            remove_Block(&line->text.chars, 0, deleted.end - line->range.start);
        }
        else if (deleted.start > line->range.start && deleted.end < line->range.end) {
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
    /* Update lines. */
    if (firstModified != iInvalidPos) {
        /* Rewrap the lines that may have been cut in half. */
        updateLine_InputWidget_(d, at_Array(&d->lines, firstModified));
        if (firstModified + 1 < size_Array(&d->lines)) {
            updateLine_InputWidget_(d, at_Array(&d->lines, firstModified + 1));
        }
        updateLineRangesStartingFrom_InputWidget_(d, firstModified);
    }
    updateVisible_InputWidget_(d);
    updateMetrics_InputWidget_(d);
}

static iBool deleteMarked_InputWidget_(iInputWidget *d) {
    const iRanges m = mark_InputWidget_(d);
    if (!isEmpty_Range(&m)) {
        deleteIndexRange_InputWidget_(d, m);
        setCursor_InputWidget(d, indexToCursor_InputWidget_(d, m.start));
        return iTrue;
    }
    return iFalse;
}

static iChar at_InputWidget_(const iInputWidget *d, iInt2 pos) {
    if (pos.y >= 0 && pos.y < size_Array(&d->lines) &&
        pos.x >= 0 && pos.x <= endX_InputWidget_(d, pos.y)) {
        iChar ch = 0;
        decodeBytes_MultibyteChar(charPos_InputWidget_(d, pos),
                                  constEnd_String(lineString_InputWidget_(d, pos.y)),
                                  &ch);
        return ch;
    }
    return ' ';
}

static iBool isWordChar_InputWidget_(const iInputWidget *d, iInt2 pos) {
    return isAlphaNumeric_Char(at_InputWidget_(d, pos));
}

static iInt2 movedCursor_InputWidget_(const iInputWidget *d, iInt2 pos, int xDir, int yDir) {
    iChar ch;
    if (xDir < 0) {
        if (pos.x == 0) {
            if (pos.y > 0) {
                pos.x = endX_InputWidget_(d, --pos.y);
            }
        }
        else {
            iAssert(pos.x > 0);
            int n = decodePrecedingBytes_MultibyteChar(charPos_InputWidget_(d, pos),
                                                       cstr_String(lineString_InputWidget_(d, pos.y)),
                                                       &ch);
            pos.x -= n;
        }
    }
    else if (xDir > 0) {
        if (pos.x == endX_InputWidget_(d, pos.y)) {
            if (pos.y < size_Array(&d->lines) - 1) {
                pos.y++;
                pos.x = 0;
            }
        }
        else {
            int n = decodeBytes_MultibyteChar(charPos_InputWidget_(d, pos),
                                              constEnd_String(lineString_InputWidget_(d, pos.y)),
                                              &ch);
            pos.x += n;
        }
    }
    return pos;
}

iLocalDef iBool movePos_InputWidget_(const iInputWidget *d, iInt2 *pos, int dir) {
    iInt2 npos = movedCursor_InputWidget_(d, *pos, dir, 0);
    if (isEqual_I2(*pos, npos)) {
        return iFalse;
    }
    *pos = npos;
    return iTrue;
}

static iInt2 skipWord_InputWidget_(const iInputWidget *d, iInt2 pos, int dir) {
    const iBool startedAtNonWord = !isWordChar_InputWidget_(d, pos);
    if (!movePos_InputWidget_(d, &pos, dir)) {
        return pos;
    }
    /* Skip any non-word characters at start position. */
    while (!isWordChar_InputWidget_(d, pos)) {
        if (!movePos_InputWidget_(d, &pos, dir)) {
            return pos;
        }
    }
    if (startedAtNonWord && dir > 0) {
        return pos; /* Found the start of a word. */
    }
    /* Skip the word. */
    while (isWordChar_InputWidget_(d, pos)) {
        if (!movePos_InputWidget_(d, &pos, dir)) {
            return pos;
        }
    }
    if (dir > 0) {
        /* Skip to the beginning of the word. */
        while (!isWordChar_InputWidget_(d, pos)) {
            if (!movePos_InputWidget_(d, &pos, dir)) {
                return pos;
            }
        }
    }
    else {
        movePos_InputWidget_(d, &pos, +1);
    }
    return pos;
}

static iRangei visibleLineRange_InputWidget_(const iInputWidget *d) {
    iRangei vis = { -1, -1 };
    /* Determine which lines are in the potentially visible range. */
    for (int i = 0; i < size_Array(&d->lines); i++) {
        const iInputLine *line = constAt_Array(&d->lines, i);
        if (vis.start < 0 && line->wrapLines.end > d->visWrapLines.start) {
            vis.start = vis.end = i;
        }
        if (line->wrapLines.start < d->visWrapLines.end) {
            vis.end = i + 1;
        }
        else break;
    }
    return vis;
}

static iInt2 coordCursor_InputWidget_(const iInputWidget *d, iInt2 coord) {
    const iRect bounds   = contentBounds_InputWidget_(d);
    const iInt2 relCoord = sub_I2(coord, addY_I2(topLeft_Rect(bounds),
                                                 visLineOffsetY_InputWidget_(d)));
    if (relCoord.y < 0) {
        return zero_I2();
    }
    if (relCoord.y >= height_Rect(bounds)) {
        return cursorMax_InputWidget_(d);
    }
    iWrapText wrapText = {
        .maxWidth = width_Rect(bounds),
        .mode = (d->inFlags & isUrl_InputWidgetFlag ? anyCharacter_WrapTextMode : word_WrapTextMode),
        .hitPoint = relCoord,
    };
    const iRangei visLines = visibleLineRange_InputWidget_(d);
    for (size_t y = visLines.start; y < visLines.end; y++) {
        wrapText.text = range_String(lineString_InputWidget_(d, y));
        const iTextMetrics tm = measure_WrapText(&wrapText, d->font);
        if (wrapText.hitChar_out) {
            const char *pos = wrapText.hitChar_out;
            /* Cursor is between characters, so jump to next character if halfway there. */
            if (wrapText.hitGlyphNormX_out > 0.5f) {
                iChar ch;
                int n = decodeBytes_MultibyteChar(pos, wrapText.text.end, &ch);
                if (ch != '\n' && n > 0) {
                    pos += n;
                }
            }
            return init_I2(iMin(pos - wrapText.text.start, endX_InputWidget_(d, y)), y);
        }
        wrapText.hitPoint.y -= tm.advance.y;
    }
    return cursorMax_InputWidget_(d);
}

static iBool copy_InputWidget_(iInputWidget *d, iBool doCut) {
    if (!isEmpty_Range(&d->mark)) {
        const iRanges m = mark_InputWidget_(d);
//        iString *str = collect_String(newUnicodeN_String(constAt_Array(&d->text, m.start),
//                                                         size_Range(&m)));
        iString *str = collectNew_String();
        mergeLinesRange_(&d->lines, m, str);
        SDL_SetClipboardText(
            cstr_String(d->inFlags & isUrl_InputWidgetFlag ? canonicalUrl_String(str) : str));
        if (doCut) {
            pushUndo_InputWidget_(d);
            deleteMarked_InputWidget_(d);
            contentsWereChanged_InputWidget_(d);
        }
        return iTrue;
    }
    return iFalse;
}

static void paste_InputWidget_(iInputWidget *d) {
    if (SDL_HasClipboardText()) {
        pushUndo_InputWidget_(d);
        deleteMarked_InputWidget_(d);
        char *   text  = SDL_GetClipboardText();
        iString *paste = collect_String(newCStr_String(text));
        /* Url decoding. */
        if (d->inFlags & isUrl_InputWidgetFlag) {
            if (prefs_App()->decodeUserVisibleURLs) {
                paste = collect_String(urlDecode_String(paste));
            }
            else {
                urlEncodePath_String(paste);
            }
        }
        SDL_free(text);
        iConstForEach(String, i, paste) { insertChar_InputWidget_(d, i.value); }
        contentsWereChanged_InputWidget_(d);
    }
}

#if 0
static iRanges lineRange_InputWidget_(const iInputWidget *d) {
    if (isEmpty_Array(&d->lines)) {
        return (iRanges){ 0, 0 };
    }
    const iInputLine *line = line_InputWidget_(d, d->cursorLine);
    return (iRanges){ line->offset, line->offset + line->len };
}
#endif

static void extendRange_InputWidget_(iInputWidget *d, size_t *index, int dir) {
    iInt2 pos = indexToCursor_InputWidget_(d, *index);
    if (dir < 0) {
        while (movePos_InputWidget_(d, &pos, dir)) {
            if (isSelectionBreaking_Char(at_InputWidget_(d, pos))) {
                movePos_InputWidget_(d, &pos, +1);
                break;
            }
        }
    }
    if (dir > 0) {
        while (!isSelectionBreaking_Char(at_InputWidget_(d, pos)) &&
               movePos_InputWidget_(d, &pos, dir)) {
            /* keep going */
        }
    }
    *index = cursorToIndex_InputWidget_(d, pos);
}

static iRect bounds_InputWidget_(const iInputWidget *d) {
    const iWidget *w = constAs_Widget(d);
    iRect bounds = bounds_Widget(w);
    if (!isFocused_Widget(d)) {
        return bounds;
    }
    /* There may be more visible lines than fits in the widget bounds. */
    bounds.size.y = contentHeight_InputWidget_(d) + 3 * padding_().y;
    if (w->flags & extraPadding_WidgetFlag) {
        bounds.size.y += extraPaddingHeight_;
    }
    return bounds;
}

static iBool contains_InputWidget_(const iInputWidget *d, iInt2 coord) {
    return contains_Rect(bounds_InputWidget_(d), coord);
}

static void lineTextWasChanged_InputWidget_(iInputWidget *d, iInputLine *line) {
    const int y = indexOf_Array(&d->lines, line);
    textOfLinesWasChanged_InputWidget_(d, (iRangei){ y, y + 1 });
}

static iBool processEvent_InputWidget_(iInputWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (isCommand_Widget(w, ev, "focus.gained")) {
        begin_InputWidget(d);
        return iFalse;
    }
    else if (isCommand_UserEvent(ev, "keyroot.changed")) {
        d->inFlags |= needUpdateBuffer_InputWidgetFlag;
    }
    else if (isCommand_UserEvent(ev, "lang.changed")) {
        set_String(&d->hint, &d->srcHint);
        translate_Lang(&d->hint);
        return iFalse;
    }
    else if (isCommand_Widget(w, ev, "focus.lost")) {
        end_InputWidget(d, iTrue);
        return iFalse;
    }
    else if ((isCommand_UserEvent(ev, "copy") || isCommand_UserEvent(ev, "input.copy")) &&
             isEditing_InputWidget_(d)) {
        copy_InputWidget_(d, argLabel_Command(command_UserEvent(ev), "cut"));
        return iTrue;
    }
    else if (isCommand_UserEvent(ev, "input.paste") && isEditing_InputWidget_(d)) {
        paste_InputWidget_(d);
        return iTrue;
    }
    else if (isCommand_UserEvent(ev, "theme.changed")) {
        if (d->buffered) {
            d->inFlags |= needUpdateBuffer_InputWidgetFlag;
        }
        return iFalse;
    }
    else if (isCommand_UserEvent(ev, "keyboard.changed")) {
        if (isFocused_Widget(d) && arg_Command(command_UserEvent(ev))) {
            iRect rect = bounds_Widget(w);
            rect.pos.y -= value_Anim(&get_Window()->rootOffset);
            const iInt2 visRoot = visibleSize_Root(w->root);
            if (bottom_Rect(rect) > visRoot.y) {
                setValue_Anim(&get_Window()->rootOffset, -(bottom_Rect(rect) - visRoot.y), 250);
            }
        }
        return iFalse;
    }
    else if (isCommand_UserEvent(ev, "text.insert")) {
        pushUndo_InputWidget_(d);
        deleteMarked_InputWidget_(d);
        insertChar_InputWidget_(d, arg_Command(command_UserEvent(ev)));
        contentsWereChanged_InputWidget_(d);
        return iTrue;
    }
    else if (isMetricsChange_UserEvent(ev)) {
        updateMetrics_InputWidget_(d);
     //   updateLinesAndResize_InputWidget_(d);
    }
    else if (isResize_UserEvent(ev) || d->lastUpdateWidth != w->rect.size.x) {
        d->inFlags |= needUpdateBuffer_InputWidgetFlag;
#if 0
        if (d->inFlags & isUrl_InputWidgetFlag) {
            /* Restore/omit the default scheme if necessary. */
            setText_InputWidget(d, text_InputWidget(d));
        }
#endif
//        updateLinesAndResize_InputWidget_(d);
    }
    else if (isFocused_Widget(d) && isCommand_UserEvent(ev, "copy")) {
        copy_InputWidget_(d, iFalse);
        return iTrue;
    }
    if (ev->type == SDL_MOUSEMOTION && (isHover_Widget(d) || flags_Widget(w) & keepOnTop_WidgetFlag)) {
        const iInt2 coord = init_I2(ev->motion.x, ev->motion.y);
        const iInt2 inner = windowToInner_Widget(w, coord);
        setCursor_Window(get_Window(),
                         inner.x >= 2 * gap_UI + d->leftPadding &&
                         inner.x < width_Widget(w) - d->rightPadding
                             ? SDL_SYSTEM_CURSOR_IBEAM
                             : SDL_SYSTEM_CURSOR_ARROW);
    }
    switch (processEvent_Click(&d->click, ev)) {
        case none_ClickResult:
            break;
        case started_ClickResult: {
            setFocus_Widget(w);
            const iInt2 oldCursor = d->cursor;
            setCursor_InputWidget(d, coordCursor_InputWidget_(d, pos_Click(&d->click)));
            if (keyMods_Sym(modState_Keys()) == KMOD_SHIFT) {
                d->mark = d->initialMark = (iRanges){
                    cursorToIndex_InputWidget_(d, oldCursor),
                    cursorToIndex_InputWidget_(d, d->cursor)
                };
                d->inFlags |= isMarking_InputWidgetFlag;
            }
            else {
                iZap(d->mark);
                iZap(d->initialMark);
                d->inFlags &= ~(isMarking_InputWidgetFlag | markWords_InputWidgetFlag);
                if (d->click.count == 2) {
                    d->inFlags |= isMarking_InputWidgetFlag | markWords_InputWidgetFlag;
                    d->mark.start = d->mark.end = cursorToIndex_InputWidget_(d, d->cursor);
                    extendRange_InputWidget_(d, &d->mark.start, -1);
                    extendRange_InputWidget_(d, &d->mark.end, +1);
                    d->initialMark = d->mark;
                    refresh_Widget(w);
                }
                if (d->click.count == 3) {
                    selectAll_InputWidget(d);
                }
            }
            return iTrue;
        }
        case aborted_ClickResult:
            d->inFlags &= ~isMarking_InputWidgetFlag;
            return iTrue;
        case drag_ClickResult:
            d->cursor = coordCursor_InputWidget_(d, pos_Click(&d->click));
            showCursor_InputWidget_(d);
            if (~d->inFlags & isMarking_InputWidgetFlag) {
                d->inFlags |= isMarking_InputWidgetFlag;
                d->mark.start = cursorToIndex_InputWidget_(d, d->cursor);
            }
            d->mark.end = cursorToIndex_InputWidget_(d, d->cursor);
            if (d->inFlags & markWords_InputWidgetFlag) {
                const iBool isFwd = d->mark.end >= d->mark.start;
                extendRange_InputWidget_(d, &d->mark.end, isFwd ? +1 : -1);
                d->mark.start = isFwd ? d->initialMark.start : d->initialMark.end;
            }
            refresh_Widget(w);
            return iTrue;
        case finished_ClickResult:
            d->inFlags &= ~isMarking_InputWidgetFlag;
            return iTrue;
    }
    if (ev->type == SDL_MOUSEMOTION && flags_Widget(w) & keepOnTop_WidgetFlag) {
        const iInt2 coord = init_I2(ev->motion.x, ev->motion.y);
        if (contains_Click(&d->click, coord)) {
            return iTrue;
        }
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_RIGHT &&
        contains_Widget(w, init_I2(ev->button.x, ev->button.y))) {
        iWidget *clipMenu = findWidget_App("clipmenu");
        if (isVisible_Widget(clipMenu)) {
            closeMenu_Widget(clipMenu);
        }
        else {
            openMenuFlags_Widget(clipMenu, mouseCoord_Window(get_Window()), iFalse);
        }
        return iTrue;
    }
    if (ev->type == SDL_KEYUP && isFocused_Widget(w)) {
        return iTrue;
    }
    const iInt2 curMax    = cursorMax_InputWidget_(d);
    const iInt2 lineFirst = init_I2(0, d->cursor.y);
    const iInt2 lineLast  = init_I2(endX_InputWidget_(d, d->cursor.y), d->cursor.y);
    if (ev->type == SDL_KEYDOWN && isFocused_Widget(w)) {
        const int key  = ev->key.keysym.sym;
        const int mods = keyMods_Sym(ev->key.keysym.mod);
        if (mods == KMOD_PRIMARY) {
            switch (key) {
                case 'c':
                case 'x':
                    copy_InputWidget_(d, key == 'x');
                    return iTrue;
                case 'v':
                    paste_InputWidget_(d);
                    return iTrue;
                case 'z':
                    if (popUndo_InputWidget_(d)) {
                        refresh_Widget(w);
                        contentsWereChanged_InputWidget_(d);
                    }
                    return iTrue;
            }
        }
#if defined (iPlatformApple)
        if (mods == KMOD_PRIMARY || mods == (KMOD_PRIMARY | KMOD_SHIFT)) {
            switch (key) {
                case SDLK_UP:
                case SDLK_DOWN:
                    setCursor_InputWidget(d, key == SDLK_UP ? zero_I2() : curMax);
                    refresh_Widget(d);
                    return iTrue;
            }
        }
#endif
        d->prevCursor = d->cursor;
        switch (key) {
            case SDLK_INSERT:
                if (mods == KMOD_SHIFT) {
                    paste_InputWidget_(d);
                }
                return iTrue;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                if (mods == KMOD_SHIFT || (d->maxLen == 0 &&
                                           ~d->inFlags & isUrl_InputWidgetFlag &&
                                           d->inFlags & enterKeyInsertsLineFeed_InputWidgetFlag)) {
                    pushUndo_InputWidget_(d);
                    deleteMarked_InputWidget_(d);
                    insertChar_InputWidget_(d, '\n');
                    contentsWereChanged_InputWidget_(d);
                    return iTrue;
                }
                if (d->inFlags & enterKeyEnabled_InputWidgetFlag) {
                    d->inFlags |= enterPressed_InputWidgetFlag;
                    setFocus_Widget(NULL);
                }
                return iTrue;
            case SDLK_ESCAPE:
                end_InputWidget(d, iFalse);
                setFocus_Widget(NULL);
                return (d->inFlags & eatEscape_InputWidgetFlag) != 0;
            case SDLK_BACKSPACE:
                if (!isEmpty_Range(&d->mark)) {
                    pushUndo_InputWidget_(d);
                    deleteMarked_InputWidget_(d);
                    contentsWereChanged_InputWidget_(d);
                }
                else if (mods & byWord_KeyModifier) {
                    pushUndo_InputWidget_(d);
                    d->mark.start = cursorToIndex_InputWidget_(d, d->cursor);
                    d->mark.end   = cursorToIndex_InputWidget_(d, skipWord_InputWidget_(d, d->cursor, -1));
                    deleteMarked_InputWidget_(d);
                    contentsWereChanged_InputWidget_(d);
                }
                else if (!isEqual_I2(d->cursor, zero_I2())) {
                    pushUndo_InputWidget_(d);
                    d->mark.end = cursorToIndex_InputWidget_(d, d->cursor);
                    movePos_InputWidget_(d, &d->cursor, -1);
                    d->mark.start = cursorToIndex_InputWidget_(d, d->cursor);
                    deleteMarked_InputWidget_(d);
                    contentsWereChanged_InputWidget_(d);
                }
                else if (isEqual_I2(d->cursor, zero_I2()) && d->maxLen == 1) {
                    pushUndo_InputWidget_(d);
                    iInputLine *line = cursorLine_InputWidget_(d);
                    clear_String(&line->text);
                    lineTextWasChanged_InputWidget_(d, line);
                    contentsWereChanged_InputWidget_(d);
                }
                showCursor_InputWidget_(d);
                refresh_Widget(w);
                return iTrue;
#if 0
            case SDLK_d:
                if (mods != KMOD_CTRL) break;
            case SDLK_DELETE:
                if (!isEmpty_Range(&d->mark)) {
                    pushUndo_InputWidget_(d);
                    deleteMarked_InputWidget_(d);
                    contentsWereChanged_InputWidget_(d);
                }
                else if (mods & byWord_KeyModifier) {
                    pushUndo_InputWidget_(d);
                    d->mark.start = d->cursor;
                    d->mark.end   = skipWord_InputWidget_(d, d->cursor, +1);
                    deleteMarked_InputWidget_(d);
                    contentsWereChanged_InputWidget_(d);
                }
                else if (d->cursor < size_Array(&d->text)) {
                    pushUndo_InputWidget_(d);
                    remove_Array(&d->text, d->cursor);
                    contentsWereChanged_InputWidget_(d);
                }
                showCursor_InputWidget_(d);
                refresh_Widget(w);
                return iTrue;
            case SDLK_k:
                if (mods == KMOD_CTRL) {
                    if (!isEmpty_Range(&d->mark)) {
                        pushUndo_InputWidget_(d);
                        deleteMarked_InputWidget_(d);
                        contentsWereChanged_InputWidget_(d);
                    }
                    else {
                        pushUndo_InputWidget_(d);
                        removeN_Array(&d->text, d->cursor, size_Array(&d->text) - d->cursor);
                        contentsWereChanged_InputWidget_(d);
                    }
                    showCursor_InputWidget_(d);
                    refresh_Widget(w);
                    return iTrue;
                }
                break;
#endif
            case SDLK_HOME:
            case SDLK_END:
                if (mods == KMOD_PRIMARY || mods == (KMOD_PRIMARY | KMOD_SHIFT)) {
                    setCursor_InputWidget(d, key == SDLK_HOME ? zero_I2() : curMax);
                }
                else {
                    setCursor_InputWidget(d, key == SDLK_HOME ? lineFirst : lineLast);
                }
                refresh_Widget(w);
                return iTrue;
            case SDLK_a:
#if defined (iPlatformApple)
                if (mods == KMOD_PRIMARY) {
                    selectAll_InputWidget(d);
                    d->mark.start = 0;
                    d->mark.end   = cursorToIndex_InputWidget_(d, curMax);
                    d->cursor     = curMax;
                    showCursor_InputWidget_(d);
                    refresh_Widget(w);
                    return iTrue;
                }
#endif
                /* fall through for Emacs-style Home/End */
            case SDLK_e:
                if (mods == KMOD_CTRL || mods == (KMOD_CTRL | KMOD_SHIFT)) {
                    setCursor_InputWidget(d, key == 'a' ? lineFirst : lineLast);
                    refresh_Widget(w);
                    return iTrue;
                }
                break;
            case SDLK_LEFT:
            case SDLK_RIGHT: {
                const int dir = (key == SDLK_LEFT ? -1 : +1);
                if (mods & byLine_KeyModifier) {
                    setCursor_InputWidget(d, dir < 0 ? lineFirst : lineLast);
                }
                else if (mods & byWord_KeyModifier) {
                    setCursor_InputWidget(d, skipWord_InputWidget_(d, d->cursor, dir));
                }
                else if (!isMarking_() && !isEmpty_Range(&d->mark)) {
                    const iRanges m = mark_InputWidget_(d);
                    setCursor_InputWidget(d, indexToCursor_InputWidget_(d, dir < 0 ? m.start : m.end));
                    iZap(d->mark);
                }
                else {
                    setCursor_InputWidget(d, movedCursor_InputWidget_(d, d->cursor, dir, 0));
                }
                refresh_Widget(w);
                return iTrue;
            }
            case SDLK_TAB:
                /* Allow focus switching. */
                return processEvent_Widget(as_Widget(d), ev);
            case SDLK_UP:
            case SDLK_DOWN:
                if (moveCursorByLine_InputWidget_(d, key == SDLK_UP ? -1 : +1)) {
                    refresh_Widget(d);
                    return iTrue;
                }
                /* For moving to lookup from url entry. */
                return processEvent_Widget(as_Widget(d), ev);
            case SDLK_PAGEUP:
            case SDLK_PAGEDOWN:
                for (int count = 0; count < 5; count++) {
                    moveCursorByLine_InputWidget_(d, key == SDLK_PAGEUP ? -1 : +1);
                }
                refresh_Widget(d);
                return iTrue;
        }
        if (mods & (KMOD_PRIMARY | KMOD_SECONDARY)) {
            return iFalse;
        }
        return iTrue;
    }
    else if (ev->type == SDL_TEXTINPUT && isFocused_Widget(w)) {
        pushUndo_InputWidget_(d);
        deleteMarked_InputWidget_(d);
        const int firstInsertLine = d->cursor.y;
        insertRange_InputWidget_(d, range_CStr(ev->text.text));
        contentsWereChanged_InputWidget_(d);
        return iTrue;
    }
    return processEvent_Widget(w, ev);
}

iDeclareType(MarkPainter)

struct Impl_MarkPainter {
    iPaint *paint;
    const iInputWidget *d;
    iRect contentBounds;
    const iInputLine *line;
    iInt2 pos;
    iRanges mark;
};

static iBool draw_MarkPainter_(iWrapText *wrapText, iRangecc wrappedText, int origin, int advance,
                               iBool isBaseRTL) {
    iUnused(isBaseRTL);
    iMarkPainter *mp = wrapText->context;
    const iRanges mark = mp->mark;
    if (isEmpty_Range(&mark)) {
        return iTrue; /* nothing marked */
    }
    const char *cstr = cstr_String(&mp->line->text);
    const iRanges lineRange = {
        wrappedText.start - cstr + mp->line->range.start,
        wrappedText.end   - cstr + mp->line->range.start
    };
    if (mark.end <= lineRange.start || mark.start >= lineRange.end) {
        mp->pos.y += lineHeight_Text(mp->d->font);
        return iTrue; /* outside of mark */
    }
    iRect rect = { addX_I2(mp->pos, origin), init_I2(advance, lineHeight_Text(mp->d->font)) };
    if (mark.end < lineRange.end) {
        /* Calculate where the mark ends. */
        const iRangecc markedPrefix = { //cstr, cstr + mark.end - lineRange.start };
            wrappedText.start,
            wrappedText.start + mark.end - lineRange.start
        };
        rect.size.x = measureRange_Text(mp->d->font, markedPrefix).advance.x;
    }
    if (mark.start > lineRange.start) {
        /* Calculate where the mark starts. */
        const iRangecc unmarkedPrefix = { //cstr, cstr + mark.start - lineRange.start
            wrappedText.start,
            wrappedText.start + mark.start - lineRange.start
        };
        adjustEdges_Rect(&rect, 0, 0, 0, measureRange_Text(mp->d->font, unmarkedPrefix).advance.x);
    }
    rect.size.x = iMax(gap_UI / 3, rect.size.x);
    mp->pos.y += lineHeight_Text(mp->d->font);
    fillRect_Paint(mp->paint, rect, uiMarked_ColorId);
    return iTrue;
}

static void draw_InputWidget_(const iInputWidget *d) {
    const iWidget *w         = constAs_Widget(d);
    iRect          bounds    = adjusted_Rect(bounds_InputWidget_(d), padding_(), neg_I2(padding_()));
    iBool          isHint    = isHintVisible_InputWidget_(d);
    const iBool    isFocused = isFocused_Widget(w);
    const iBool    isHover   = isHover_Widget(w) &&
                               contains_InputWidget_(d, mouseCoord_Window(get_Window()));
    if (d->inFlags & needUpdateBuffer_InputWidgetFlag) {
        updateBuffered_InputWidget_(iConstCast(iInputWidget *, d));
    }
    iPaint p;
    init_Paint(&p);
    /* `lines` is already up to date and ready for drawing. */    
    fillRect_Paint(
        &p, bounds, isFocused ? uiInputBackgroundFocused_ColorId : uiInputBackground_ColorId);
    drawRectThickness_Paint(&p,
                            adjusted_Rect(bounds, neg_I2(one_I2()), zero_I2()),
                            isFocused ? gap_UI / 4 : 1,
                            isFocused ? uiInputFrameFocused_ColorId
                                      : isHover ? uiInputFrameHover_ColorId : uiInputFrame_ColorId);
    setClip_Paint(&p, adjusted_Rect(bounds, init_I2(d->leftPadding, 0),
                                    init_I2(-d->rightPadding, w->flags & extraPadding_WidgetFlag ? -gap_UI / 2 : 0)));
    const iRect contentBounds = contentBounds_InputWidget_(d);
    iInt2       drawPos    = topLeft_Rect(contentBounds);
    const int   fg         = isHint                                   ? uiAnnotation_ColorId
                             : isFocused /*&& !isEmpty_Array(&d->lines)*/ ? uiInputTextFocused_ColorId
                                                                      : uiInputText_ColorId;
    iWrapText wrapText = {
        .maxWidth = width_Rect(contentBounds),
        .mode     = (d->inFlags & isUrl_InputWidgetFlag ? anyCharacter_WrapTextMode : word_WrapTextMode)
    };
    const iRangei visLines = visibleLineRange_InputWidget_(d);
    const int visLineOffsetY = visLineOffsetY_InputWidget_(d);
    /* If buffered, just draw the buffered copy. */
    if (d->buffered && !isFocused) {
        /* Most input widgets will use this, since only one is focused at a time. */
        draw_TextBuf(d->buffered, drawPos, white_ColorId);
    }
    else if (isHint) {
        drawRange_Text(d->font, drawPos, uiAnnotation_ColorId, range_String(&d->hint));
    }
    else {
        /* TODO: Make a function out of this. */
        drawPos.y += visLineOffsetY;
        iMarkPainter marker = {
            .paint = &p,
            .d = d,
            .contentBounds = contentBounds,
            .mark = mark_InputWidget_(d)
        };
        for (size_t vis = visLines.start; vis < visLines.end; vis++) {
            const iInputLine *line = constAt_Array(&d->lines, vis);
            wrapText.text     = range_String(&line->text);
            wrapText.wrapFunc = isFocused ? draw_MarkPainter_ : NULL; /* mark is drawn under each line of text */
            wrapText.context  = &marker;
            marker.line       = line;
            marker.pos        = drawPos;
            addv_I2(&drawPos, draw_WrapText(&wrapText, d->font, drawPos, fg).advance); /* lines end with \n */
        }
        wrapText.wrapFunc = NULL;
        wrapText.context  = NULL;
#if 0
        iConstForEach(Array, i, &d->lines) {
            const iInputLine *line      = i.value;
            const iBool       isLast    = index_ArrayConstIterator(&i) == size_Array(&d->lines) - 1;
            const iInputLine *nextLine  = isLast ? NULL : (line + 1);
            const iRanges     lineRange = { line->offset,
                                            nextLine ? nextLine->offset : size_Array(&d->text) };
            if (isFocused && !isEmpty_Range(&d->mark)) {
                /* Draw the selected range. */
                const iRanges mark = mark_InputWidget_(d);
                if (mark.start < lineRange.end && mark.end > lineRange.start) {
                    const int m1 = maxWidth_TextMetrics(measureN_Text(d->font,
                                                 cstr_String(&line->text),
                                                 iMax(lineRange.start, mark.start) - line->offset));
                    const int m2 = maxWidth_TextMetrics(measureN_Text(d->font,
                                                 cstr_String(&line->text),
                                                 iMin(lineRange.end, mark.end) - line->offset));
                    fillRect_Paint(&p,
                                   (iRect){ addX_I2(drawPos, iMin(m1, m2)),
                                            init_I2(iMax(gap_UI / 3, iAbs(m2 - m1)),
                                                    lineHeight_Text(d->font)) },
                                   uiMarked_ColorId);
                }
            }
            drawRange_Text(d->font, drawPos, fg, range_String(&line->text));
            drawPos.y += lineHeight_Text(d->font);
        }
#endif
    }
    unsetClip_Paint(&p);
    /* Draw the insertion point. */
    if (isFocused && d->cursorVis) {
        iString cur;
        iInt2 curSize;
        int visWrapsAbove = 0;
        for (int i = d->cursor.y - 1; i >= visLines.start; i--) {
            const iInputLine *line = constAt_Array(&d->lines, i);
            visWrapsAbove += numWrapLines_InputLine_(line);
        }
#if 0
        if (d->mode == overwrite_InputMode) {
            /* Block cursor that overlaps a character. */
            if (d->cursor < size_Array(&d->text)) {
                if (~d->inFlags & isSensitive_InputWidgetFlag) {
                    initUnicodeN_String(&cur, constAt_Array(&d->text, d->cursor), 1);
                }
                else {
                    initUnicodeN_String(&cur, &sensitiveChar_, 1);
                }
            }
            else {
                initCStr_String(&cur, " ");
            }
            curSize = addX_I2(measure_Text(d->font, cstr_String(&cur)).bounds.size,
                              iMin(2, gap_UI / 4));
        }
        else {
#endif
            /* Bar cursor. */
            curSize = init_I2(gap_UI / 2, lineHeight_Text(d->font));
//      }
        const iInputLine *curLine = constCursorLine_InputWidget_(d);
        const iString *   text    = &curLine->text;
        /* The bounds include visible characters, while advance includes whitespace as well.
           Normally only the advance is needed, but if the cursor is at a newline, the advance
           will have reset back to zero. */
        wrapText.text    = range_String(text);
        wrapText.hitChar = wrapText.text.start + d->cursor.x;
        //const int prefixSize = maxWidth_TextMetrics(
        measure_WrapText(&wrapText, d->font);
        const iInt2 advance = wrapText.hitAdvance_out;
        //        const iInt2 curPos   = addX_I2(addY_I2(contentBounds.pos, lineHeight_Text(d->font)
        //        * d->cursorLine),
        //                                       prefixSize +
        //                                       (d->mode == insert_InputMode ? -curSize.x / 2 :
        //                                       0));
        //printf("%d -> tm.advance: %d, %d\n", d->cursor.x, tm.advance.x, tm.advance.y);
        const iInt2 curPos = add_I2(addY_I2(topLeft_Rect(contentBounds), visLineOffsetY +
                                            visWrapsAbove * lineHeight_Text(d->font)),
                                    addX_I2(advance,
                                            (d->mode == insert_InputMode ? -curSize.x / 2 : 0)));
        const iRect curRect  = { curPos, curSize };
        fillRect_Paint(&p, curRect, uiInputCursor_ColorId);
        if (d->mode == overwrite_InputMode) {
            /* The `gap_UI` offsets below are a hack. They are used because for some reason the
               cursor rect and the glyph inside don't quite position like during `run_Text_()`. */
            draw_Text(d->font,
                      addX_I2(curPos, iMin(1, gap_UI / 8)),
                      uiInputCursorText_ColorId,
                      "%s",
                      cstr_String(&cur));
            deinit_String(&cur);
        }
    }
    drawChildren_Widget(w);
}

iBeginDefineSubclass(InputWidget, Widget)
    .processEvent = (iAny *) processEvent_InputWidget_,
    .draw         = (iAny *) draw_InputWidget_,
iEndDefineSubclass(InputWidget)
