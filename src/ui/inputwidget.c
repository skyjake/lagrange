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

/* InputWidget supports both fully custom and system-provided text editing.
   The primary source of complexity is the handling of wrapped text content
   in the custom text editor. */

/* TODO: Refactor this so that the native text input widget has a common base
   class with the fully-custom input widget. Currently this implementation is
   too convoluted, with both variants intermingled. */

#include "inputwidget.h"
#include "command.h"
#include "paint.h"
#include "util.h"
#include "keys.h"
#include "prefs.h"
#include "lang.h"
#include "touch.h"
#include "app.h"

#include <the_Foundation/array.h>
#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <SDL_clipboard.h>
#include <SDL_timer.h>
#include <SDL_version.h>

#if defined (iPlatformAppleDesktop)
#   include "macos.h"
#endif

#if defined (iPlatformAppleMobile) || defined (iPlatformAndroidMobile)
#   include "mobile.h"
#   define LAGRANGE_USE_SYSTEM_TEXT_INPUT 1 /* System-provided UI control almost handles everything. */
#else
#   define LAGRANGE_USE_SYSTEM_TEXT_INPUT 0
iDeclareType(SystemTextInput)
#endif

static const int    refreshInterval_InputWidget_ = 512;
static const size_t maxUndo_InputWidget_         = 64;
static const int    unlimitedWidth_InputWidget_  = 1000000; /* TODO: WrapText disables some functionality if maxWidth==0 */

static const iChar  sensitiveChar_ = 0x25cf;   /* black circle */
static const char * sensitive_     = "\u25cf";

#define minWidth_InputWidget_   (3 * gap_UI)

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

static void updateMetrics_InputWidget_(iInputWidget *);

/*----------------------------------------------------------------------------------------------*/
#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT

iDeclareType(InputLine)

struct Impl_InputLine {
    iString text;
    iRanges range;      /* byte offset inside the entire content; for marking */
    iRangei wrapLines;  /* range of visual wrapped lines */
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

#endif /* USE_SYSTEM_TEXT_INPUT */

enum iInputWidgetFlag {
    isSensitive_InputWidgetFlag          = iBit(1),
    isUrl_InputWidgetFlag                = iBit(2), /* affected by decoding preference */
    enterPressed_InputWidgetFlag         = iBit(3),
    selectAllOnFocus_InputWidgetFlag     = iBit(4),
    notifyEdits_InputWidgetFlag          = iBit(5),
    eatEscape_InputWidgetFlag            = iBit(6),
    isMarking_InputWidgetFlag            = iBit(7),
    markWords_InputWidgetFlag            = iBit(8),
    needUpdateBuffer_InputWidgetFlag     = iBit(9),
    enterKeyEnabled_InputWidgetFlag      = iBit(10),
    lineBreaksEnabled_InputWidgetFlag    = iBit(11),
    needBackup_InputWidgetFlag           = iBit(12),
    useReturnKeyBehavior_InputWidgetFlag = iBit(13),
    //touchBehavior_InputWidgetFlag        = iBit(14), /* different behavior depending on interaction method */
    dragCursor_InputWidgetFlag           = iBit(14),
    dragMarkerStart_InputWidgetFlag      = iBit(15),
    dragMarkerEnd_InputWidgetFlag        = iBit(16),
};

/*----------------------------------------------------------------------------------------------*/

struct Impl_InputWidget {
    iWidget         widget;
    enum iInputMode mode;
    int             font;
    int             inFlags;
    size_t          maxLen;  /* characters */
    iString         srcHint;
    iString         hint;
    int             leftPadding; /* additional padding between frame and content */
    int             rightPadding;
    int             minWrapLines, maxWrapLines; /* min/max number of visible lines allowed */
    iRangei         visWrapLines; /* which wrap lines are current visible */
    iClick          click;
    int             wheelAccum;
    iTextBuf *      buffered; /* pre-rendered static text */
    iInputWidgetValidatorFunc validator;
    void *          validatorContext;
    iString *       backupPath;
    int             backupTimer;
    iString         oldText; /* for restoring if edits cancelled */
    int             lastUpdateWidth;
    uint32_t        lastOverflowScrollTime; /* scrolling to show focused widget */
    iSystemTextInput *sysCtrl;
#if LAGRANGE_USE_SYSTEM_TEXT_INPUT
    iString         text;
#else
    iArray          lines;        /* iInputLine[] */
    iInt2           cursor;       /* cursor position: x = byte offset, y = line index */
    iInt2           prevCursor;   /* previous cursor position */
    iRanges         mark;         /* TODO: would likely simplify things to use two Int2's for marking; no conversions needed */
    iRanges         initialMark;
    iArray          undoStack;
    uint32_t        tapStartTime;
    uint32_t        lastTapTime;
    iInt2           lastTapPos;
    int             tapCount;
    int             cursorVis;
    uint32_t        timer;
#endif
};

iDefineObjectConstructionArgs(InputWidget, (size_t maxLen), maxLen)

static int extraPaddingHeight_InputWidget_(const iInputWidget *d) {
    if ((isPortraitPhone_App() || deviceType_App() == tablet_AppDeviceType) &&
        !cmp_String(id_Widget(&d->widget), "url")) {
        /* Make the tap target more generous. */
        return 2.5f * gap_UI;
    }
    return 1.25f * gap_UI;
}

static void restoreBackup_InputWidget_(iInputWidget *d) {
    if (!d->backupPath) return;
    iFile *f = new_File(d->backupPath);
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        setText_InputWidget(d, collect_String(readString_File(f)));
    }
    iRelease(f);
}

static void saveBackup_InputWidget_(iInputWidget *d) {
    if (!d->backupPath) return;
    iFile *f = new_File(d->backupPath);
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
#if LAGRANGE_USE_SYSTEM_TEXT_INPUT
        write_File(f, utf8_String(&d->text));
#else
        iConstForEach(Array, i, &d->lines) {
            const iInputLine *line = i.value;
            write_File(f, utf8_String(&line->text));
        }
#   if !defined (NDEBUG)
        iConstForEach(Array, j, &d->lines) {
            iAssert(endsWith_String(&((const iInputLine *) j.value)->text, "\n") ||
                    index_ArrayConstIterator(&j) == size_Array(&d->lines) - 1);
        }
#   endif
#endif
        d->inFlags &= ~needBackup_InputWidgetFlag;
    }
    iRelease(f);
}

static void eraseBackup_InputWidget_(iInputWidget *d) {
    if (d->backupPath) {
        remove(cstr_String(d->backupPath));
        delete_String(d->backupPath);
        d->backupPath = NULL;
    }
}

static uint32_t backupTimeout_InputWidget_(uint32_t interval, void *context) {
    iInputWidget *d = context;
    postCommand_Widget(d, "input.backup");
    return 0; /* does not repeat */
}

static void restartBackupTimer_InputWidget_(iInputWidget *d) {
    if (d->backupPath) {
        d->inFlags |= needBackup_InputWidgetFlag;
        if (d->backupTimer) {
            SDL_RemoveTimer(d->backupTimer);
        }
        d->backupTimer = SDL_AddTimer(2500, backupTimeout_InputWidget_, d);
    }
}

void setBackupFileName_InputWidget(iInputWidget *d, const char *fileName) {
    if (fileName == NULL) {
        if (d->backupTimer) {
            SDL_RemoveTimer(d->backupTimer);
            d->backupTimer = 0;
        }
        eraseBackup_InputWidget_(d);
        if (d->backupPath) {
            delete_String(d->backupPath);
            d->backupPath = NULL;
        }
        return;
    }
    if (!d->backupPath) {
        d->backupPath = copy_String(dataDir_App());
    }
    append_Path(d->backupPath, collectNewCStr_String(fileName));
    const size_t windowIndex = windowIndex_Root(as_Widget(d)->root);
    /* Each window has its own separate backup. */
    if (windowIndex > 0) {
        appendFormat_String(d->backupPath, ".%zu", windowIndex);
    }
    appendCStr_String(d->backupPath, ".txt");
    restoreBackup_InputWidget_(d);
}

iLocalDef iInt2 padding_(void) {
    return init_I2(gap_UI / 2, gap_UI / 2);
}

#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT

static void clearUndo_InputWidget_(iInputWidget *d) {
    iForEach(Array, i, &d->undoStack) {
        deinit_InputUndo_(i.value);
    }
    clear_Array(&d->undoStack);
}

static const iInputLine *line_InputWidget_(const iInputWidget *d, size_t index) {
    iAssert(!isEmpty_Array(&d->lines));
    return constAt_Array(&d->lines, index);
}

#endif /* !LAGRANGE_USE_SYSTEM_TEXT_INPUT */

static iRect contentBounds_InputWidget_(const iInputWidget *d) {
    const iWidget *w      = constAs_Widget(d);
    iRect          bounds = adjusted_Rect(bounds_Widget(w),
                                 addX_I2(padding_(), d->leftPadding),
                                 neg_I2(addX_I2(padding_(), d->rightPadding)));
    shrink_Rect(&bounds, init_I2(gap_UI * (flags_Widget(w) & tight_WidgetFlag ? 1 : 2), 0));
    bounds.pos.y += padding_().y / 2;
    if (flags_Widget(w) & extraPadding_WidgetFlag) {
        if (d->sysCtrl && !cmp_String(id_Widget(w), "url")) {
            /* TODO: This is super hacky: the native UI control would be offset incorrectly.
               These paddings/offsets are getting a bit ridiculous, should rethink the whole thing.
               Use the Widget paddings! */
            bounds.pos.y += 1.25f * gap_UI / 2;
        }
        else {
            bounds.pos.y += extraPaddingHeight_InputWidget_(d) / 2;
        }
    }
    return bounds;
}

static iWrapText wrap_InputWidget_(const iInputWidget *d, int y) {
#if LAGRANGE_USE_SYSTEM_TEXT_INPUT
    iUnused(y); /* full text is wrapped always */
    iRangecc text = range_String(&d->text);
#else
    iRangecc text = range_String(&line_InputWidget_(d, y)->text);
#endif
    return (iWrapText){
        .text = text,
        .maxWidth = d->maxLen == 0 ? iMaxi(minWidth_InputWidget_,
                                           width_Rect(contentBounds_InputWidget_(d)))
                                   : unlimitedWidth_InputWidget_,
        .mode =
            (d->inFlags & isUrl_InputWidgetFlag ? anyCharacter_WrapTextMode : word_WrapTextMode),
        .overrideChar = (d->inFlags & isSensitive_InputWidgetFlag ? sensitiveChar_ : 0),
    };
}

#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT

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

static iBool isCursorFocusable_Char_(iChar c) {
    return !isDefaultIgnorable_Char(c) &&
           !isVariationSelector_Char(c) &&
           !isFitzpatrickType_Char(c);
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

static iInt2 movedCursor_InputWidget_(const iInputWidget *d, iInt2 pos, int xDir, int yDir) {
    iChar ch = 0;
    int   n  = 0;
    /* TODO: The cursor should never land on any combining codepoints either. */
    for (;;) {
        if (xDir < 0) {
            if (pos.x == 0) {
                if (pos.y > 0) {
                    pos.x = endX_InputWidget_(d, --pos.y);
                }
            }
            else {
                iAssert(pos.x > 0);
                n = decodePrecedingBytes_MultibyteChar(charPos_InputWidget_(d, pos),
                                                       cstr_String(lineString_InputWidget_(d, pos.y)),
                                                       &ch);
                pos.x -= n;
                if (!isCursorFocusable_Char_(at_InputWidget_(d, pos))) {
                    continue;
                }
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
                n = decodeBytes_MultibyteChar(charPos_InputWidget_(d, pos),
                                              constEnd_String(lineString_InputWidget_(d, pos.y)),
                                              &ch);
                pos.x += n;
                if (!isCursorFocusable_Char_(at_InputWidget_(d, pos))) {
                    continue;
                }
            }
        }
        break;
    }
    return pos;
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
    return (line->wrapLines.start - d->visWrapLines.start) * lineHeight_Text(d->font) -
           d->wheelAccum;
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
    iAssert(isEmpty_Range(&vis) || (vis.start >= 0 && vis.end >= vis.start));
    return vis;
}

static iInt2 relativeCoordOnLine_InputWidget_(const iInputWidget *d, iInt2 pos) {
    /* Relative to the start of the line on which the position is. */
    iWrapText wt = wrap_InputWidget_(d, pos.y);
    wt.hitChar = wt.text.start + pos.x;
    measure_WrapText(&wt, d->font);
    return wt.hitAdvance_out;
}

static iInt2 cursorToWindowCoord_InputWidget_(const iInputWidget *d, iInt2 pos, iBool *isInsideBounds) {
    /* Maps a cursor XY position to a window coordinate. */
    const iRect bounds = contentBounds_InputWidget_(d);
    iInt2 wc = addY_I2(topLeft_Rect(bounds), visLineOffsetY_InputWidget_(d));
    iRangei visLines = visibleLineRange_InputWidget_(d);
    if (!contains_Range(&visLines, pos.y)) {
        /* This line is not visible. */
        *isInsideBounds = iFalse;
        return zero_I2();
    }
    for (int i = visLines.start; i < pos.y; i++) {
        wc.y += lineHeight_Text(d->font) * numWrapLines_InputLine_(line_InputWidget_(d, i));
    }
    const iInputLine *line = line_InputWidget_(d, pos.y);
    addv_I2(&wc, relativeCoordOnLine_InputWidget_(d, pos));
    *isInsideBounds = contains_Rect(bounds, wc);
    return wc;
}

static iInt2 relativeCursorCoord_InputWidget_(const iInputWidget *d) {
    return relativeCoordOnLine_InputWidget_(d, d->cursor);
}

static void updateVisible_InputWidget_(iInputWidget *d) {
    if (width_Widget(d) == 0) {
        return; /* Nothing to do yet. */
    }
    const int totalWraps = numWrapLines_InputWidget_(d);
    const int visWraps = iClamp(totalWraps, d->minWrapLines, d->maxWrapLines);
    /* Resize the height of the editor. */
    d->visWrapLines.end = d->visWrapLines.start + visWraps;
    /* Determine which wraps are currently visible. */
    d->cursor.y = iMin(d->cursor.y, size_Array(&d->lines) - 1);
    const iInputLine *curLine = constAt_Array(&d->lines, d->cursor.y);
    const int cursorY = curLine->wrapLines.start +
        relativeCursorCoord_InputWidget_(d).y / lineHeight_Text(d->font);
    /* Scroll to cursor. */
    int delta = 0;
    if (d->visWrapLines.end < cursorY + 1) {
        delta = cursorY + 1 - d->visWrapLines.end;
    }
    else if (cursorY < d->visWrapLines.start) {
        delta = cursorY - d->visWrapLines.start;
    }
    if (d->visWrapLines.end + delta > totalWraps) {
        /* Don't scroll past the bottom. */
        delta = totalWraps - d->visWrapLines.end;
    }
    if (d->visWrapLines.start + delta < 0) {
        /* Don't ever scroll above the top. */
        delta = -d->visWrapLines.start;
    }
    d->visWrapLines.start += delta;
    d->visWrapLines.end   += delta;
//    iAssert(contains_Range(&d->visWrapLines, cursorY));
    if (!isFocused_Widget(d) && d->maxWrapLines == 1) {
        d->visWrapLines.start = 0;
        d->visWrapLines.end = 1;
    }
//    printf("[InputWidget %p] total:%d viswrp:%d cur:%d vis:%d..%d\n",
//           d, totalWraps, visWraps, d->cursor.y, d->visWrapLines.start, d->visWrapLines.end);
//    fflush(stdout);
}

static void showCursor_InputWidget_(iInputWidget *d) {
    d->cursorVis = 2;
    updateVisible_InputWidget_(d);
}

#else /* if LAGRANGE_USE_SYSTEM_TEXT_INPUT */

static int visLineOffsetY_InputWidget_(const iInputWidget *d) {
    return 0; /* offset for the buffered text */
}

static void updateVisible_InputWidget_(iInputWidget *d) {
    iUnused(d);
    /* TODO: Anything to do? */
}

#endif

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
        int extraHeight = (flags_Widget(as_Widget(d)) & extraPadding_WidgetFlag ? extraPaddingHeight_InputWidget_(d) : 0);
        setFixedSize_Widget(
            as_Widget(d),
            add_I2(measure_Text(d->font, cstr_Block(content)).bounds.size,
                   init_I2(6 * gap_UI + d->leftPadding + d->rightPadding,
                           2 * gap_UI + extraHeight)));
        delete_Block(content);
    }
}

static iString *text_InputWidget_(const iInputWidget *d) {
#if LAGRANGE_USE_SYSTEM_TEXT_INPUT
    return copy_String(&d->text);
#else
    iString *text = new_String();
    mergeLines_(&d->lines, text);
    return text;
#endif
}

#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT
static size_t length_InputWidget_(const iInputWidget *d) {
    /* Note: `d->length` is kept up to date, so don't call this normally. */
    size_t len = 0;
    iConstForEach(Array, i, &d->lines) {
        const iInputLine *line = i.value;
        len += length_String(&line->text);
    }
    return len;
}

static void updateLine_InputWidget_(iInputWidget *d, iInputLine *line) {
    iAssert(endsWith_String(&line->text, "\n") || isLastLine_InputWidget_(d, line));
    iWrapText wrapText = wrap_InputWidget_(d, indexOf_Array(&d->lines, line));
    if (wrapText.maxWidth <= minWidth_InputWidget_) {
        line->wrapLines.end = line->wrapLines.start + 1;
        return;
    }
    const iTextMetrics tm = measure_WrapText(&wrapText, d->font);
    line->wrapLines.end = line->wrapLines.start + height_Rect(tm.bounds) / lineHeight_Text(d->font);
    iAssert(!isEmpty_Range(&line->wrapLines));
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

static void updateAllLinesAndResizeHeight_InputWidget_(iInputWidget *d) {
    const int oldWraps = numWrapLines_InputWidget_(d);
    iForEach(Array, i, &d->lines) {
        updateLine_InputWidget_(d, i.value); /* count number of visible lines */
    }
    updateLineRangesStartingFrom_InputWidget_(d, 0);
    updateVisible_InputWidget_(d);
    if (oldWraps != numWrapLines_InputWidget_(d)) {
        updateMetrics_InputWidget_(d);
    }
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

iLocalDef iBool isBlinkingCursor_(void) {
    /* Terminal will blink if appropriate. */
    return prefs_App()->blinkingCursor && !isTerminal_Platform();
}

static void startOrStopCursorTimer_InputWidget_(iInputWidget *d, int doStart) {
    if (!isBlinkingCursor_() && doStart == 1) {
        doStart = iFalse;
    }
    if (doStart && !d->timer) {
        d->timer = SDL_AddTimer(refreshInterval_InputWidget_, cursorTimer_, d);
    }
    else if (!doStart && d->timer) {
        SDL_RemoveTimer(d->timer);
        d->timer = 0;
    }
}

#else /* using a system-provided text control */

static void updateAllLinesAndResizeHeight_InputWidget_(iInputWidget *d) {
    if (width_Widget(d) >= minWidth_InputWidget_) {
        /* Rewrap the buffered text and resize accordingly. */
        iWrapText wt = wrap_InputWidget_(d, 0);
        /* TODO: Set max lines limit for WrapText. */
        const int height = measure_WrapText(&wt, d->font).bounds.size.y;
        /* We use this to store the number wrapped lines for determining widget height. */
        d->visWrapLines.start = 0;
        d->visWrapLines.end = iMax(d->minWrapLines,
                                   iMin(d->maxWrapLines, height / lineHeight_Text(d->font)));
        updateMetrics_InputWidget_(d);
    }
}

#endif

static int contentHeight_InputWidget_(const iInputWidget *d) {
    const int lineHeight = lineHeight_Text(d->font);
#if LAGRANGE_USE_SYSTEM_TEXT_INPUT
    const int minHeight = d->minWrapLines * lineHeight;
    const int maxHeight = d->maxWrapLines * lineHeight;
    if (d->sysCtrl) {
        const int preferred = (preferredHeight_SystemTextInput(d->sysCtrl) + 2 * gap_UI) / lineHeight;
        return iClamp(preferred * lineHeight, minHeight, maxHeight);
    }
    if (d->buffered && ~d->inFlags & needUpdateBuffer_InputWidgetFlag) {
        return iClamp(d->buffered->size.y, minHeight, maxHeight);
    }
#endif
    return (int) size_Range(&d->visWrapLines) * lineHeight;
}

static void updateTextInputRect_InputWidget_(const iInputWidget *d) {
#if LAGRANGE_USE_SYSTEM_TEXT_INPUT
    if (d->sysCtrl) {
        setRect_SystemTextInput(d->sysCtrl, contentBounds_InputWidget_(d));
    }
#endif
#if !defined (iPlatformAppleMobile) && !defined (iPlatformAndroidMobile) && !defined (SDL_SEAL_CURSES)
    const iRect bounds = bounds_Widget(constAs_Widget(d));
    SDL_SetTextInputRect(&(SDL_Rect){ bounds.pos.x, bounds.pos.y, bounds.size.x, bounds.size.y });
#endif
}

static void updateMetrics_InputWidget_(iInputWidget *d) {
    iWidget *w = as_Widget(d);
    updateSizeForFixedLength_InputWidget_(d);
    /* Caller must arrange the width, but the height is set here. */
#if LAGRANGE_USE_SYSTEM_TEXT_INPUT
    if (d->sysCtrl && preferredHeight_SystemTextInput(d->sysCtrl) == 0) {
        /* Nothing to update, the native control doesn't know the appropriate height yet. */
        return;
    }
#endif
    const int oldHeight = height_Rect(w->rect);
    w->rect.size.y = contentHeight_InputWidget_(d) + 3 * padding_().y; /* TODO: Why 3x? */
    if (flags_Widget(w) & extraPadding_WidgetFlag) {
        w->rect.size.y += extraPaddingHeight_InputWidget_(d);
    }
    invalidateBuffered_InputWidget_(d);
    if (height_Rect(w->rect) != oldHeight) {
        postCommand_Widget(d, "input.resized arg:%d", w->root->pendingArrange + 1);
        updateTextInputRect_InputWidget_(d);
    }
}

void init_InputWidget(iInputWidget *d, size_t maxLen) {
    iWidget *w = &d->widget;
    init_Widget(w);
    d->validator = NULL;
    d->validatorContext = NULL;
    setFlags_Widget(w, focusable_WidgetFlag | hover_WidgetFlag, iTrue);
    setFlags_Widget(w, extraPadding_WidgetFlag, isMobile_Platform());
#if LAGRANGE_USE_SYSTEM_TEXT_INPUT
    init_String(&d->text);
#else
    init_Array(&d->lines, sizeof(iInputLine));
    init_Array(&d->undoStack, sizeof(iInputUndo));
    d->cursor       = zero_I2();
    d->prevCursor   = zero_I2();
    d->lastTapTime  = 0;
    d->tapCount     = 0;
    d->timer        = 0;
    d->cursorVis    = 0;
    iZap(d->mark);
    splitToLines_(&iStringLiteral(""), &d->lines);
#endif
    init_String(&d->oldText);
    init_String(&d->srcHint);
    init_String(&d->hint);
    d->font         = uiInput_FontId | alwaysVariableFlag_FontId;
    d->leftPadding  = 0;
    d->rightPadding = 0;
    d->lastUpdateWidth = 0;
    d->inFlags         = eatEscape_InputWidgetFlag | enterKeyEnabled_InputWidgetFlag |
                         lineBreaksEnabled_InputWidgetFlag | useReturnKeyBehavior_InputWidgetFlag;
    //    if (deviceType_App() != desktop_AppDeviceType) {
    //        d->inFlags |= enterKeyInsertsLineFeed_InputWidgetFlag;
    //    }
    setMaxLen_InputWidget(d, maxLen);
    d->visWrapLines.start = 0;
    d->visWrapLines.end = 1;
    d->maxWrapLines = maxLen > 0 ? 1 : 20; /* TODO: Choose maximum dynamically? */
    d->minWrapLines = 1;
    setFlags_Widget(w, fixedHeight_WidgetFlag, iTrue); /* resizes its own height */
    init_Click(&d->click, d, SDL_BUTTON_LEFT);
    d->wheelAccum = 0;
    d->buffered = NULL;
    d->backupPath = NULL;
    d->backupTimer = 0;
    d->sysCtrl = NULL;
    updateMetrics_InputWidget_(d);
}

void deinit_InputWidget(iInputWidget *d) {
    if (d->backupTimer) {
        SDL_RemoveTimer(d->backupTimer);
    }
    if (d->inFlags & needBackup_InputWidgetFlag) {
        saveBackup_InputWidget_(d);
    }
    delete_String(d->backupPath);
    d->backupPath = NULL;
    delete_TextBuf(d->buffered);
    deinit_String(&d->srcHint);
    deinit_String(&d->hint);
    deinit_String(&d->oldText);
#if LAGRANGE_USE_SYSTEM_TEXT_INPUT
    delete_SystemTextInput(d->sysCtrl);
    deinit_String(&d->text);
#else
    startOrStopCursorTimer_InputWidget_(d, iFalse);
    clearInputLines_(&d->lines);
    if (isSelected_Widget(d)) {
        SDL_StopTextInput();
        enableEditorKeysInMenus_(iTrue);
    }
    clearUndo_InputWidget_(d);
    deinit_Array(&d->undoStack);
    deinit_Array(&d->lines);
#endif
}

static iBool isAllowedToInsertNewline_InputWidget_(const iInputWidget *d) {
    return ~d->inFlags & isSensitive_InputWidgetFlag &&
        ~d->inFlags & isUrl_InputWidgetFlag &&
        d->inFlags & lineBreaksEnabled_InputWidgetFlag && d->maxLen == 0;
}

#if LAGRANGE_USE_SYSTEM_TEXT_INPUT
static void updateAfterVisualOffsetChange_InputWidget_(iInputWidget *d, iRoot *root) {
    iAssert(as_Widget(d)->root == root);
    iUnused(root);
    if (d->sysCtrl) {
        setRect_SystemTextInput(d->sysCtrl, contentBounds_InputWidget_(d));
    }
}
#endif

void setFont_InputWidget(iInputWidget *d, int fontId) {
    d->font = fontId;
    updateMetrics_InputWidget_(d);
}

#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT
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
        splitToLines_(&undo->text, &d->lines);
        d->cursor = undo->cursor;
        deinit_InputUndo_(undo);
        popBack_Array(&d->undoStack);
        iZap(d->mark);
        updateAllLinesAndResizeHeight_InputWidget_(d);
        return iTrue;
    }
    return iFalse;
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

static size_t cursorToIndex_InputWidget_(const iInputWidget *d, iInt2 pos) {
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
    /* TODO: The lines are sorted; this could use a binary search. */
    iConstForEach(Array, i, &d->lines) {
        const iInputLine *line = i.value;
        if (contains_Range(&line->range, index)) {
            return init_I2(index - line->range.start, index_ArrayConstIterator(&i));
        }
    }
    return cursorMax_InputWidget_(d);
}
#endif

void setMode_InputWidget(iInputWidget *d, enum iInputMode mode) {
    d->mode = mode;
}

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

const iString *text_InputWidget(const iInputWidget *d) {
    if (d) {
        iString *text = collect_String(text_InputWidget_(d));
        if (d->inFlags & isUrl_InputWidgetFlag) {
            /* Add the "gemini" scheme back if one is omitted. */
            restoreDefaultScheme_(text);
        }
        return text;
    }
    return collectNew_String();
}

int font_InputWidget(const iInputWidget *d) {
    return d->font;
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
    maxLines = iMax(minLines, maxLines);
    if (d->minWrapLines != minLines || d->maxWrapLines != maxLines) {
        d->minWrapLines = minLines;
        d->maxWrapLines = maxLines;
        updateVisible_InputWidget_(d);
        updateMetrics_InputWidget_(d);
    }
}

int minLines_InputWidget(const iInputWidget *d) {
    return d->minWrapLines;
}
int maxLines_InputWidget(const iInputWidget *d) {
    return d->maxWrapLines;
}

void setValidator_InputWidget(iInputWidget *d, iInputWidgetValidatorFunc validator, void *context) {
    d->validator = validator;
    d->validatorContext = context;
}

void setLineBreaksEnabled_InputWidget(iInputWidget *d, iBool lineBreaksEnabled) {
    iChangeFlags(d->inFlags, lineBreaksEnabled_InputWidgetFlag, lineBreaksEnabled);
}

void setEnterKeyEnabled_InputWidget(iInputWidget *d, iBool enterKeyEnabled) {
    iChangeFlags(d->inFlags, enterKeyEnabled_InputWidgetFlag, enterKeyEnabled);
}

void setUseReturnKeyBehavior_InputWidget(iInputWidget *d, iBool useReturnKeyBehavior) {
    iChangeFlags(d->inFlags, useReturnKeyBehavior_InputWidgetFlag, useReturnKeyBehavior);
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

iLocalDef iBool isEmpty_InputWidget_(const iInputWidget *d) {
#if LAGRANGE_USE_SYSTEM_TEXT_INPUT
    return isEmpty_String(&d->text);
#else
    return size_Array(&d->lines) == 1 && isEmpty_String(&line_InputWidget_(d, 0)->text);
#endif
}

static iBool isHintVisible_InputWidget_(const iInputWidget *d) {
    return !isEmpty_String(&d->hint) && isEmpty_InputWidget_(d);
}

static void updateBuffered_InputWidget_(iInputWidget *d) {
    invalidateBuffered_InputWidget_(d);
    if (isHintVisible_InputWidget_(d)) {
        d->buffered = newRange_TextBuf(d->font, uiAnnotation_ColorId, range_String(&d->hint));
    }
    else {
        /* Draw all the potentially visible lines to a buffer. */
#if LAGRANGE_USE_SYSTEM_TEXT_INPUT
        iString *visText = copy_String(&d->text);
#else
        iString *visText = new_String();
        const iRangei visRange = visibleLineRange_InputWidget_(d);
        for (int i = visRange.start; i < visRange.end; i++) {
            append_String(visText, &line_InputWidget_(d, i)->text);
        }
#endif
        if (d->inFlags & isUrl_InputWidgetFlag) {
            /* Highlight the host name. */
            iUrl parts;
            init_Url(&parts, visText);
            if (!isEmpty_Range(&parts.host)) {
                const char *cstr = cstr_String(visText);
                insertData_Block(&visText->chars,
                                 parts.host.end - cstr,
                                 restore_ColorEscape,
                                 strlen(restore_ColorEscape));
                insertData_Block(&visText->chars,
                                 parts.host.start - cstr,
                                 uiTextStrong_ColorEscape,
                                 strlen(uiTextStrong_ColorEscape));
            }
        }
        iWrapText wt = wrap_InputWidget_(d, 0);
        wt.maxLines = d->maxWrapLines;
        wt.text = range_String(visText);
        const int fg = uiInputText_ColorId;
        d->buffered = new_TextBuf(&wt, d->font, fg);
        delete_String(visText);
    }
    d->inFlags &= ~needUpdateBuffer_InputWidgetFlag;
}

void setText_InputWidget(iInputWidget *d, const iString *text) {
    setTextUndoable_InputWidget(d, text, iFalse);
}

static iBool isNarrow_InputWidget_(const iInputWidget *d) {
    return width_Rect(contentBounds_InputWidget_(d)) < 100 * gap_UI * aspect_UI;
}

void setTextUndoable_InputWidget(iInputWidget *d, const iString *text, iBool isUndoable) {
    if (!d) return;
#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT
    if (isUndoable) {
        pushUndo_InputWidget_(d);
    }
#endif
    if (d->inFlags & isUrl_InputWidgetFlag) {
        if (prefs_App()->decodeUserVisibleURLs) {
            iString *enc = collect_String(copy_String(text));
            urlDecodePath_String(enc);
            text = enc;
        }
        else {
            /* The user wants URLs encoded, also Punycode the domain. */
            iString *enc = collect_String(copy_String(text));
            urlEncodePath_String(enc);
            /* Prevent address bar spoofing (mentioned as IDN homograph attack in
               https://github.com/skyjake/lagrange/issues/73) */
            punyEncodeUrlHost_String(enc);
            text = enc;
        }
        /* Omit the default (Gemini) scheme if there isn't much space. */
        if (isNarrow_InputWidget_(d)) {
            text = omitDefaultScheme_(collect_String(copy_String(text)));
        }
    }
    iString *nfcText = collect_String(copy_String(text));
    normalize_String(nfcText);
#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT
    if (!isUndoable) {
        clearUndo_InputWidget_(d);
    }
    splitToLines_(nfcText, &d->lines);
    iAssert(!isEmpty_Array(&d->lines));
    iForEach(Array, i, &d->lines) {
        updateLine_InputWidget_(d, i.value); /* count number of visible lines */
    }
    updateLineRangesStartingFrom_InputWidget_(d, 0);
    d->cursor = cursorMax_InputWidget_(d);
    if (!isFocused_Widget(d)) {
        iZap(d->mark);
    }
#else
    set_String(&d->text, nfcText);
    if (d->sysCtrl) {
        setText_SystemTextInput(d->sysCtrl, nfcText, iTrue);
    }
    else {
        updateAllLinesAndResizeHeight_InputWidget_(d); /* need to know the new height */
    }
#endif
    if (!isFocused_Widget(d)) {
        d->inFlags |= needUpdateBuffer_InputWidgetFlag;
    }
    updateVisible_InputWidget_(d);
    updateMetrics_InputWidget_(d);
    if (!d->sysCtrl) {
        refresh_Widget(as_Widget(d));
    }
}

void setTextCStr_InputWidget(iInputWidget *d, const char *cstr) {
    iString *str = newCStr_String(cstr);
    setText_InputWidget(d, str);
    delete_String(str);
}

void setTextUndoableCStr_InputWidget(iInputWidget *d, const char *cstr, iBool isUndoable) {
    iString *str = newCStr_String(cstr);
    setTextUndoable_InputWidget(d, str, isUndoable);
    delete_String(str);
}

void selectAll_InputWidget(iInputWidget *d) {
#if LAGRANGE_USE_SYSTEM_TEXT_INPUT
    if (d->sysCtrl) {
        selectAll_SystemTextInput(d->sysCtrl);
    }
#else
    d->mark = (iRanges){ 0, lastLine_InputWidget_(d)->range.end };
    refresh_Widget(as_Widget(d));
#endif
}

void deselect_InputWidget(iInputWidget *d) {
#if LAGRANGE_USE_SYSTEM_TEXT_INPUT
    /* TODO? */
#else
    iZap(d->mark);
    refresh_Widget(as_Widget(d));
#endif
}

void validate_InputWidget(iInputWidget *d) {
    if (d->validator) {
        d->validator(d, d->validatorContext); /* this may change the contents */
    }    
}

iLocalDef iBool isEditing_InputWidget_(const iInputWidget *d) {
    return (flags_Widget(constAs_Widget(d)) & selected_WidgetFlag) != 0;
}

static void contentsWereChanged_InputWidget_(iInputWidget *);

#if LAGRANGE_USE_SYSTEM_TEXT_INPUT
void systemInputChanged_InputWidget_(iSystemTextInput *sysCtrl, void *widget) {
    iInputWidget *d = widget;
    const iString *sysText = text_SystemTextInput(sysCtrl);
    if (!equal_String(&d->text, sysText)) {
        set_String(&d->text, sysText);
        restartBackupTimer_InputWidget_(d);
        contentsWereChanged_InputWidget_(d);
    }
    updateMetrics_InputWidget_(d);
}
#endif

void begin_InputWidget(iInputWidget *d) {
    iWidget *w = as_Widget(d);
    if (isEditing_InputWidget_(d)) {
        /* Already active. */
        return;
    }
    invalidateBuffered_InputWidget_(d);
    setFlags_Widget(w, hidden_WidgetFlag | disabled_WidgetFlag, iFalse);
    setFlags_Widget(w, selected_WidgetFlag, iTrue);
    d->inFlags &= ~enterPressed_InputWidgetFlag;
#if LAGRANGE_USE_SYSTEM_TEXT_INPUT
    set_String(&d->oldText, &d->text);
    d->sysCtrl = new_SystemTextInput(
        contentBounds_InputWidget_(d),
        (d->maxWrapLines > 1 ? multiLine_SystemTextInputFlags : 0) |
            (d->inFlags & isUrl_InputWidgetFlag ? (disableAutocorrect_SystemTextInputFlag |
                                                   disableAutocapitalize_SystemTextInputFlag)
                                                : 0) |
            /* widget-specific tweaks (hacks) */
            (!cmp_String(id_Widget(w), "url") ? returnGo_SystemTextInputFlags : 0) |
            (!cmp_String(id_Widget(w), "upload.text") ? extraPadding_SystemTextInputFlag : 0) |
            (flags_Widget(w) & alignRight_WidgetFlag ? alignRight_SystemTextInputFlag : 0) |
            (isAllowedToInsertNewline_InputWidget_(d) ? insertNewlines_SystemTextInputFlag : 0) |
            (d->inFlags & selectAllOnFocus_InputWidgetFlag ? selectAll_SystemTextInputFlags : 0));
    setFont_SystemTextInput(d->sysCtrl, d->font);
    setText_SystemTextInput(d->sysCtrl, &d->oldText, iFalse);
    setTextChangedFunc_SystemTextInput(d->sysCtrl, systemInputChanged_InputWidget_, d);
    iConnect(Root, w->root, visualOffsetsChanged, d, updateAfterVisualOffsetChange_InputWidget_);
    updateTextInputRect_InputWidget_(d);
    updateMetrics_InputWidget_(d);
    refresh_Widget(d); /* ensure buffered panels hide the static text */
#else
    mergeLines_(&d->lines, &d->oldText);
    if (d->mode == overwrite_InputMode) {
        d->cursor = zero_I2();
    }
    else {
        d->cursor.y = iMin(d->cursor.y, size_Array(&d->lines) - 1);
        d->cursor.x = iMin(d->cursor.x, cursorLine_InputWidget_(d)->range.end);
    }
    SDL_StartTextInput();
    showCursor_InputWidget_(d);
    refresh_Widget(w);
    startOrStopCursorTimer_InputWidget_(d, iTrue);
    if (d->inFlags & selectAllOnFocus_InputWidgetFlag) {
        d->mark = (iRanges){ 0, lastLine_InputWidget_(d)->range.end };
        d->cursor = cursorMax_InputWidget_(d);
    }
    else if (~d->inFlags & isMarking_InputWidgetFlag) {
        iZap(d->mark);
    }
    enableEditorKeysInMenus_(iFalse);
    updateTextInputRect_InputWidget_(d);
    updateVisible_InputWidget_(d);
#endif
}

void end_InputWidget(iInputWidget *d, iBool accept) {
    iWidget *w = as_Widget(d);
    if (!isEditing_InputWidget_(d)) {
        /* Was not active. */
        return;
    }
#if LAGRANGE_USE_SYSTEM_TEXT_INPUT
    if (d->sysCtrl) {
        iDisconnect(Root, w->root, visualOffsetsChanged, d, updateAfterVisualOffsetChange_InputWidget_);
        if (accept) {
            set_String(&d->text, text_SystemTextInput(d->sysCtrl));
        }
        else {
            set_String(&d->text, &d->oldText);
        }
        delete_SystemTextInput(d->sysCtrl);
        d->sysCtrl = NULL;
    }
#else
    if (!accept) {
        /* Overwrite the edited lines. */
        splitToLines_(&d->oldText, &d->lines);
    }
    SDL_StopTextInput();
    enableEditorKeysInMenus_(iTrue);
    d->inFlags &= ~isMarking_InputWidgetFlag;
    startOrStopCursorTimer_InputWidget_(d, iFalse);
#endif
    d->inFlags |= needUpdateBuffer_InputWidgetFlag;
    setFlags_Widget(w, selected_WidgetFlag | keepOnTop_WidgetFlag | touchDrag_WidgetFlag, iFalse);
    const char *id = cstr_String(id_Widget(as_Widget(d)));
    if (!*id) id = "_";
    refresh_Widget(w);
    postCommand_Widget(w,
                       "input.ended id:%s enter:%d arg:%d",
                       id,
                       d->inFlags & enterPressed_InputWidgetFlag ? 1 : 0,
                       accept ? 1 : 0);
}

#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT
static void textOfLinesWasChanged_InputWidget_(iInputWidget *d, iRangei lineRange) {
    for (int i = lineRange.start; i < lineRange.end; i++) {
        updateLine_InputWidget_(d, at_Array(&d->lines, i));
    }
    updateLineRangesStartingFrom_InputWidget_(d, lineRange.start);
    updateVisible_InputWidget_(d);
    updateMetrics_InputWidget_(d);
    restartBackupTimer_InputWidget_(d);
}

static void insertRange_InputWidget_(iInputWidget *d, iRangecc range) {
    iRangecc nextRange = { range.end, range.end };
    const int firstModified = d->cursor.y;
    for (; !isEmpty_Range(&range); range = nextRange) {
        /* If there's a newline, we'll need to break and begin a new line. */
        const char *newline = iStrStrN(range.start, "\n", size_Range(&range));
        if (newline) {
            nextRange = (iRangecc){ iMin(newline + 1, range.end), range.end };
            range.end = newline;
        }
        iInputLine *line = cursorLine_InputWidget_(d);
        if (d->mode == insert_InputMode) {
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
        truncate_String(&line->text, d->cursor.x);
        if (!endsWith_String(&line->text, "\n")) {
            appendCStr_String(&line->text, "\n");
        }
        insert_Array(&d->lines, ++d->cursor.y, &split);
        d->cursor.x = 0;
    }
    if (d->maxLen > 0) {
        iAssert(size_Array(&d->lines) == 1);
        iAssert(d->cursor.y == 0);
        iInputLine *line = front_Array(&d->lines);
        size_t len = length_String(&line->text);
        if (len > d->maxLen) {
            removeEnd_String(&line->text, len - d->maxLen);
            d->cursor.x = endX_InputWidget_(d, 0);
        }
    }
    textOfLinesWasChanged_InputWidget_(d, (iRangei){ firstModified, d->cursor.y + 1 });
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
    iChar ch = at_InputWidget_(d, pos);
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

static iBool moveCursorByLine_InputWidget_(iInputWidget *d, int dir, int horiz) {
    const iInputLine *line     = cursorLine_InputWidget_(d);
    iInt2             relCoord = relativeCursorCoord_InputWidget_(d);
    int               relLine  = relCoord.y / lineHeight_Text(d->font);
    if ((dir < 0 && relLine > 0) || (dir > 0 && relLine < numWrapLines_InputLine_(line) - 1)) {
        relCoord.y += dir * lineHeight_Text(d->font);
    }
    else if (dir < 0 && d->cursor.y > 0) {
        d->cursor.y--;
        line = cursorLine_InputWidget_(d);
        relCoord.y = lineHeight_Text(d->font) * (numWrapLines_InputLine_(line) - 1);
    }
    else if (dir > 0 && d->cursor.y < size_Array(&d->lines) - 1) {
        d->cursor.y++;
        relCoord.y = 0;
    }
    else if (dir == 0 && horiz != 0) {
        relCoord.x = (horiz < 0 ? 0 : width_Widget(d));
    }
    else {
        return iFalse;
    }
    iWrapText wt = wrap_InputWidget_(d, d->cursor.y);
    if (isEqual_I2(relCoord, zero_I2())) {
        /* (0, 0) disables the hit test, but this is trivial to figure out. */
        wt.hitChar_out = wt.text.start;        
    }
    else {
        wt.hitPoint = addY_I2(relCoord, 1 * aspect_UI); 
        measure_WrapText(&wt, d->font);
    }
    if (wt.hitChar_out) {
        d->cursor.x = wt.hitChar_out - wt.text.start;
    }
    else {
        d->cursor.x = endX_InputWidget_(d, d->cursor.y);
    }
    if (wt.hitGlyphNormX_out > 0.5f && d->cursor.x < endX_InputWidget_(d, d->cursor.y)) {
        iChar ch;
        int n = decodeBytes_MultibyteChar(wt.text.start + d->cursor.x, wt.text.end, &ch);
        if (ch != '\n' && n > 0) {
            d->cursor.x += n;
        }
    }
    setCursor_InputWidget(d, d->cursor); /* mark, show */
    return iTrue;
}

static iRanges mark_InputWidget_(const iInputWidget *d) {
    iRanges m = { iMin(d->mark.start, d->mark.end), iMax(d->mark.start, d->mark.end) };
    const iInputLine *last = lastLine_InputWidget_(d);
    m.start   = iMin(m.start, last->range.end);
    m.end     = iMin(m.end, last->range.end);
    return m;
}

static void deleteIndexRange_InputWidget_(iInputWidget *d, iRanges deleted) {
    size_t firstModified = iInvalidPos;
    restartBackupTimer_InputWidget_(d);
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
        iZap(d->mark); /* setCursor thinks we're marking when Shift is down */
        return iTrue;
    }
    return iFalse;
}

static iBool isWordChar_InputWidget_(const iInputWidget *d, iInt2 pos) {
    return isAlphaNumeric_Char(at_InputWidget_(d, pos));
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

static iInt2 coordCursor_InputWidget_(const iInputWidget *d, iInt2 coord) {
    const iRect bounds   = contentBounds_InputWidget_(d);
    const iInt2 relCoord = sub_I2(coord, addY_I2(topLeft_Rect(bounds),
                                                 visLineOffsetY_InputWidget_(d)));
    if (relCoord.y < 0) {
        return zero_I2();
    }
//    if (relCoord.y >= height_Rect(bounds)) {
//        printf("relCoord > bounds.h\n"); fflush(stdout);
//        return cursorMax_InputWidget_(d);
//    }
    iWrapText wrapText = {
        .maxWidth = d->maxLen == 0 ? iMaxi(minWidth_InputWidget_, width_Rect(bounds)) : unlimitedWidth_InputWidget_,
        .mode = (d->inFlags & isUrl_InputWidgetFlag ? anyCharacter_WrapTextMode : word_WrapTextMode),
        .hitPoint = relCoord,
        .overrideChar = (d->inFlags & isSensitive_InputWidgetFlag ? sensitiveChar_ : 0),
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
        const iRanges m   = mark_InputWidget_(d);
        iString *     str = collectNew_String();
        mergeLinesRange_(&d->lines, m, str);
        if (d->inFlags & isUrl_InputWidgetFlag) {
            restoreDefaultScheme_(str);
        }
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
                paste = collect_String(urlDecodeExclude_String(paste, URL_RESERVED_CHARS));
                replace_String(paste, "\n", "%0A");
                replace_String(paste, "\t", "%09");
            }
            else {
                urlEncodePath_String(paste);
            }
        }
        SDL_free(text);
        insertRange_InputWidget_(d, range_String(paste));
        contentsWereChanged_InputWidget_(d);
    }
}

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

static void lineTextWasChanged_InputWidget_(iInputWidget *d, iInputLine *line) {
    const int y = indexOf_Array(&d->lines, line);
    textOfLinesWasChanged_InputWidget_(d, (iRangei){ y, y + 1 });
}
#endif

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

static void contentsWereChanged_InputWidget_(iInputWidget *d) {
    validate_InputWidget(d);
    if (d->inFlags & notifyEdits_InputWidgetFlag) {
        postCommand_Widget(d, "input.edited id:%s", cstr_String(id_Widget(constAs_Widget(d))));
    }
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
        bounds.size.y += extraPaddingHeight_InputWidget_(d);
    }
    return bounds;
}

static iBool contains_InputWidget_(const iInputWidget *d, iInt2 coord) {
    return contains_Rect(bounds_InputWidget_(d), coord);
}

static iBool isArrowUpDownConsumed_InputWidget_(const iInputWidget *d) {
    return d->maxWrapLines > 1;
}

static iBool checkLineBreakMods_InputWidget_(const iInputWidget *d, int mods) {
    if (d->inFlags & useReturnKeyBehavior_InputWidgetFlag) {
        return mods == lineBreakKeyMod_ReturnKeyBehavior(prefs_App()->returnKey);
    }
    return mods == 0;
}

static iBool checkAcceptMods_InputWidget_(const iInputWidget *d, int mods) {
    if (d->inFlags & useReturnKeyBehavior_InputWidgetFlag) {
        return mods == acceptKeyMod_ReturnKeyBehavior(prefs_App()->returnKey);
    }
    return mods == 0;
}

enum iEventResult {
    ignored_EventResult = 0, /* event was not processed */
    false_EventResult   = 1, /* event was processed but other widgets can still process it, too*/
    true_EventResult    = 2, /* event was processed and should not be passed on */
};

#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT
static void markWordAtCursor_InputWidget_(iInputWidget *d) {
    d->mark.start = d->mark.end = cursorToIndex_InputWidget_(d, d->cursor);
    extendRange_InputWidget_(d, &d->mark.start, -1);
    extendRange_InputWidget_(d, &d->mark.end, +1);
    d->initialMark = d->mark;
}

static void showClipMenu_InputWidget_(const iInputWidget *d, iInt2 coord) {
    iWidget *clipMenu = findWidget_App("clipmenu");
    if (isVisible_Widget(clipMenu)) {
        closeMenu_Widget(clipMenu);
    }
    else {
        setMenuItemDisabled_Widget(
            clipMenu, "input.paste enter:1", cmp_String(id_Widget(constAs_Widget(d)), "url"));
        openMenuFlags_Widget(clipMenu, coord, iFalse);
    }
}
#endif

static enum iEventResult processPointerEvents_InputWidget_(iInputWidget *d, const SDL_Event *ev) {
#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT
    iWidget *w = as_Widget(d);
    if (ev->type == SDL_MOUSEMOTION && (isHover_Widget(d) || flags_Widget(w) & keepOnTop_WidgetFlag)) {
        const iInt2 coord = init_I2(ev->motion.x, ev->motion.y);
        const iInt2 inner = windowToInner_Widget(w, coord);
        setCursor_Window(get_Window(),
                         inner.x >= 2 * gap_UI + d->leftPadding &&
                         inner.x < width_Widget(w) - d->rightPadding
                             ? SDL_SYSTEM_CURSOR_IBEAM
                             : SDL_SYSTEM_CURSOR_ARROW);
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_RIGHT &&
        contains_Widget(w, init_I2(ev->button.x, ev->button.y))) {
        setFocus_Widget(w);
        showClipMenu_InputWidget_(d, mouseCoord_Window(get_Window(), ev->button.which));
        return iTrue;
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
                    markWordAtCursor_InputWidget_(d);
                    refresh_Widget(w);
                }
                if (d->click.count == 3) {
                    selectAll_InputWidget(d);
                }
            }
            refresh_Widget(d);
            return true_EventResult;
        }
        case aborted_ClickResult:
            d->inFlags &= ~isMarking_InputWidgetFlag;
            return true_EventResult;
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
            return true_EventResult;
        case finished_ClickResult:
            d->inFlags &= ~isMarking_InputWidgetFlag;
            return true_EventResult;
    }
    if (ev->type == SDL_MOUSEMOTION && flags_Widget(w) & keepOnTop_WidgetFlag) {
        const iInt2 coord = init_I2(ev->motion.x, ev->motion.y);
        if (contains_Click(&d->click, coord)) {
            return true_EventResult;
        }
    }
#endif
    return ignored_EventResult;
}

#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT
static iInt2 touchCoordCursor_InputWidget_(const iInputWidget *d, iInt2 coord) {
    /* Clamp to the bounds so the cursor doesn't wrap at the ends. */
    iRect bounds = shrunk_Rect(contentBounds_InputWidget_(d), one_I2());
    bounds.size.y = iMini(numWrapLines_InputWidget_(d), d->maxWrapLines) * lineHeight_Text(d->font) - 2;
    return coordCursor_InputWidget_(d, min_I2(bottomRight_Rect(bounds),
                                              max_I2(coord, topLeft_Rect(bounds))));
}

static iBool isInsideMark_InputWidget_(const iInputWidget *d, size_t pos) {
    const iRanges mark = mark_InputWidget_(d);
    return contains_Range(&mark, pos);
}

static int distanceToPos_InputWidget_(const iInputWidget *d, iInt2 uiCoord, iInt2 textPos) {
    iBool isInside;
    const iInt2 winCoord = cursorToWindowCoord_InputWidget_(d, textPos, &isInside);
    if (!isInside) {
        return INT_MAX;
    }
    return dist_I2(addY_I2(winCoord, lineHeight_Text(d->font) / 2), uiCoord);
}
#endif

static enum iEventResult processTouchEvents_InputWidget_(iInputWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT
    /*
     + first tap to focus & select all/place cursor
     + focused tap to place cursor
     - drag cursor to move it
     - double-click to select a word
     - drag to move selection handles
     - long-press for context menu: copy, paste, delete, select all, deselect
     - double-click and hold to select words
     - triple-click to select all
     - drag/wheel elsewhere to scroll (contents or overflow), no change in focus
     */
//    if (ev->type != SDL_MOUSEBUTTONUP && ev->type != SDL_MOUSEBUTTONDOWN &&
//        ev->type != SDL_MOUSEWHEEL && ev->type != SDL_MOUSEMOTION &&
//        !(ev->type == SDL_USEREVENT && ev->user.code == widgetTapBegins_UserEventCode) &&
//        !(ev->type == SDL_USEREVENT && ev->user.code == widgetTouchEnds_UserEventCode)) {
//        return ignored_EventResult;
//    }
    if (isFocused_Widget(w)) {
        if (ev->type == SDL_USEREVENT && ev->user.code == widgetTapBegins_UserEventCode) {
            d->lastTapTime = d->tapStartTime;
            d->tapStartTime = SDL_GetTicks();
            const int tapDist = dist_I2(latestPosition_Touch(), d->lastTapPos);
            d->lastTapPos = latestPosition_Touch();
//            printf("[%p] tap start time: %u (%u) %d\n", w, d->tapStartTime, d->tapStartTime - d->lastTapTime, tapDist);
            if (d->tapStartTime - d->lastTapTime < 400 && tapDist < gap_UI * 4) {
                d->tapCount++;
//                printf("[%p] >> tap count: %d\n", w, d->tapCount);
            }
            else {
                d->tapCount = 0;
            }
            if (!isEmpty_Range(&d->mark)) {
                const int dist[2] = {
                    distanceToPos_InputWidget_(d, latestPosition_Touch(),
                                               indexToCursor_InputWidget_(d, d->mark.start)),
                    distanceToPos_InputWidget_(d, latestPosition_Touch(),
                                               indexToCursor_InputWidget_(d, d->mark.end))
                };
                if (dist[0] < dist[1]) {
//                    printf("[%p] begin marker start drag\n", w);
                    d->inFlags |= dragMarkerStart_InputWidgetFlag;
                }
                else {
//                    printf("[%p] begin marker end drag\n", w);
                    d->inFlags |= dragMarkerEnd_InputWidgetFlag;
                }
                d->inFlags |= isMarking_InputWidgetFlag;
                setFlags_Widget(w, touchDrag_WidgetFlag, iTrue);
            }
            else {
                const int dist = distanceToPos_InputWidget_(d, latestPosition_Touch(), d->cursor);
//                printf("[%p] tap dist: %d\n", w, dist);
                if (dist < gap_UI * 10) {
//                    printf("[%p] begin cursor drag\n", w);
                    setFlags_Widget(w, touchDrag_WidgetFlag, iTrue);
                    d->inFlags |= dragCursor_InputWidgetFlag;
//                d->inFlags |= touchBehavior_InputWidgetFlag;
//                setMouseGrab_Widget(w);
//                return iTrue;
                }
            }
//            if (~d->inFlags & selectAllOnFocus_InputWidgetFlag) {
//                d->cursor = coordCursor_InputWidget_(d, pos_Click(&d->click));
//                showCursor_InputWidget_(d);
//            }
            return true_EventResult;
        }
    }
#if 0
    else if (isFocused_Widget(w)) {
        if (ev->type == SDL_MOUSEMOTION) {
            if (~d->inFlags & touchBehavior_InputWidgetFlag) {
                const iInt2 curPos = relativeCursorCoord_InputWidget_(d);
                const iInt2 relClick = sub_I2(pos_Click(&d->click),
                                              topLeft_Rect(contentBounds_InputWidget_(d)));
                if (dist_I2(curPos, relClick) < gap_UI * 8) {
//                    printf("tap on cursor!\n");
                    setFlags_Widget(w, touchDrag_WidgetFlag, iTrue);
                    d->inFlags |= touchBehavior_InputWidgetFlag;
//                    printf("[Input] begin cursor drag\n");
                    setMouseGrab_Widget(w);
                    return iTrue;
                }
            }
            else if (ev->motion.x > 0 && ev->motion.y > 0) {
//                printf("[Input] cursor being dragged\n");
                iRect bounds = shrunk_Rect(contentBounds_InputWidget_(d), one_I2());
                bounds.size.y = iMini(numWrapLines_InputWidget_(d), d->maxWrapLines) * lineHeight_Text(d->font) - 2;
                iInt2 mpos = init_I2(ev->motion.x, ev->motion.y);
                mpos = min_I2(bottomRight_Rect(bounds), max_I2(mpos, topLeft_Rect(bounds)));
                d->cursor = coordCursor_InputWidget_(d, mpos);
                showCursor_InputWidget_(d);
                refresh_Widget(w);
                return iTrue;
            }
        }
        if (d->inFlags & touchBehavior_InputWidgetFlag) {
            if (ev->type == SDL_MOUSEBUTTONUP ||
                (ev->type == SDL_USEREVENT && ev->user.code == widgetTouchEnds_UserEventCode)) {
                d->inFlags &= ~touchBehavior_InputWidgetFlag;
                setFlags_Widget(w, touchDrag_WidgetFlag, iFalse);
                setMouseGrab_Widget(NULL);
//                printf("[Input] touch ends\n");
                return iFalse;
            }
        }
    }
#endif
#if 1
    if ((ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP) &&
        ev->button.button == SDL_BUTTON_RIGHT && contains_Widget(w, latestPosition_Touch())) {
        if (ev->type == SDL_MOUSEBUTTONDOWN) {
            /*if (isFocused_Widget(w)) {
                d->inFlags |= isMarking_InputWidgetFlag;
                d->cursor = touchCoordCursor_InputWidget_(d, latestPosition_Touch());
                markWordAtCursor_InputWidget_(d);
                refresh_Widget(d);
                return true_EventResult;
            }*/
            setFocus_Widget(w);
            d->inFlags |= isMarking_InputWidgetFlag;
            d->cursor = touchCoordCursor_InputWidget_(d, latestPosition_Touch());
            markWordAtCursor_InputWidget_(d);
            d->cursor = indexToCursor_InputWidget_(d, d->mark.end);
            refresh_Widget(d);
        }
        return true_EventResult;
    }
    switch (processEvent_Click(&d->click, ev)) {
        case none_ClickResult:
             break;
        case started_ClickResult: {
//            printf("[%p] started\n", w);
            /*
            const iInt2 curPos = relativeCursorCoord_InputWidget_(d);
            const iInt2 relClick = sub_I2(pos_Click(&d->click),
                                          topLeft_Rect(contentBounds_InputWidget_(d)));
            if (dist_I2(curPos, relClick) < gap_UI * 8) {
                printf("tap on cursor!\n");
                setFlags_Widget(w, touchDrag_WidgetFlag, iTrue);
            }
            else {
                printf("tap elsewhere\n");
            }*/
            return true_EventResult;
        }
        case drag_ClickResult:
//            printf("[%p] drag %d,%d\n", w, pos_Click(&d->click).x, pos_Click(&d->click).y);
            if (d->inFlags & dragCursor_InputWidgetFlag) {
                iZap(d->mark);
                d->cursor = touchCoordCursor_InputWidget_(d, pos_Click(&d->click));
                showCursor_InputWidget_(d);
                refresh_Widget(w);
            }
            else if (d->inFlags & dragMarkerStart_InputWidgetFlag) {
                d->mark.start = cursorToIndex_InputWidget_(d, touchCoordCursor_InputWidget_(d, pos_Click(&d->click)));
                refresh_Widget(w);
            }
            else if (d->inFlags & dragMarkerEnd_InputWidgetFlag) {
                d->mark.end = cursorToIndex_InputWidget_(d, touchCoordCursor_InputWidget_(d, pos_Click(&d->click)));
                refresh_Widget(w);
            }
            return true_EventResult;
  //          printf("[%p] aborted\n", w);
//            d->inFlags &= ~touchBehavior_InputWidgetFlag;
//            setFlags_Widget(w, touchDrag_WidgetFlag, iFalse);
//            return true_EventResult;
        case finished_ClickResult:
        case aborted_ClickResult: {
//            printf("[%p] ended\n", w);
            uint32_t tapElapsed = SDL_GetTicks() - d->tapStartTime;
//            printf("tapElapsed: %u\n", tapElapsed);
            if (!isFocused_Widget(w)) {
                setFocus_Widget(w);
                d->lastTapPos = latestPosition_Touch();
                d->tapStartTime = SDL_GetTicks();
                d->tapCount = 0;
                d->cursor = touchCoordCursor_InputWidget_(d, pos_Click(&d->click));
                showCursor_InputWidget_(d);
            }
            else if (!isEmpty_Range(&d->mark) && !isMoved_Click(&d->click)) {
                if (isInsideMark_InputWidget_(d, cursorToIndex_InputWidget_(d, touchCoordCursor_InputWidget_(d, latestPosition_Touch())))) {
                    showClipMenu_InputWidget_(d, latestPosition_Touch());
                }
                else {
                    iZap(d->mark);
                    d->cursor = touchCoordCursor_InputWidget_(d, pos_Click(&d->click));
                }
            }
            else if (SDL_GetTicks() - d->lastTapTime > 1000 &&
                     d->tapCount == 0 && isEmpty_Range(&d->mark) && !isMoved_Click(&d->click) &&
                     distanceToPos_InputWidget_(d, latestPosition_Touch(), d->cursor) < gap_UI * 5) {
                showClipMenu_InputWidget_(d, latestPosition_Touch());
            }
            else {
                if (~d->inFlags & isMarking_InputWidgetFlag) {
                    iZap(d->mark);
                    d->cursor = touchCoordCursor_InputWidget_(d, pos_Click(&d->click));
                }
            }
            if (d->inFlags & (dragCursor_InputWidgetFlag | dragMarkerStart_InputWidgetFlag |
                              dragMarkerEnd_InputWidgetFlag)) {
//                printf("[%p] finished cursor/marker drag\n", w);
                d->inFlags &= ~(dragCursor_InputWidgetFlag |
                                dragMarkerStart_InputWidgetFlag |
                                dragMarkerEnd_InputWidgetFlag);
                setFlags_Widget(w, touchDrag_WidgetFlag, iFalse);
            }
            d->inFlags &= ~isMarking_InputWidgetFlag;
            showCursor_InputWidget_(d);
            refresh_Widget(w);
#if 0
            d->inFlags &= ~touchBehavior_InputWidgetFlag;
            if (flags_Widget(w) & touchDrag_WidgetFlag) {
                setFlags_Widget(w, touchDrag_WidgetFlag, iFalse);
                return true_EventResult;
            }
            if (!isMoved_Click(&d->click)) {
                if (!isFocused_Widget(w)) {
                    setFocus_Widget(w);
                    if (~d->inFlags & selectAllOnFocus_InputWidgetFlag) {
                        d->cursor = coordCursor_InputWidget_(d, pos_Click(&d->click));
                        showCursor_InputWidget_(d);
                    }
                }
                else {
                    iZap(d->mark);
                    d->cursor = coordCursor_InputWidget_(d, pos_Click(&d->click));
                    showCursor_InputWidget_(d);
                }
            }
#endif
            return true_EventResult;
        }
    }
#endif
//    if ((ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP) &&
//        contains_Widget(w, init_I2(ev->button.x, ev->button.y))) {
//        /* Eat all mouse clicks on the widget. */
//        return true_EventResult;
//    }
#else
    /* Just a tap to activate the system-provided text input control. */
    switch (processEvent_Click(&d->click, ev)) {
        case none_ClickResult:
            break;
        case started_ClickResult:
            setFocus_Widget(w);
            return true_EventResult;
        default:
            return true_EventResult;
    }
#endif
    return ignored_EventResult;
}

#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT
static void clampWheelAccum_InputWidget_(iInputWidget *d, int wheel) {
    if (wheel > 0 && d->visWrapLines.start == 0) {
        d->wheelAccum = 0;
        refresh_Widget(d);
    }
    else if (wheel < 0 && d->visWrapLines.end >= lastLine_InputWidget_(d)->wrapLines.end) {
        d->wheelAccum = 0;
        refresh_Widget(d);
    }    
}
#endif

static void overflowScrollToKeepVisible_InputWidget_(iAny *widget) {
    iInputWidget *d = widget;
    iWidget *w = as_Widget(d);
    if (!isFocused_Widget(w)) { //} || isAffectedByVisualOffset_Widget(w)) {
        return;
    }
    iRect rect    = boundsWithoutVisualOffset_Widget(w);
    iRect visible = visibleRect_Root(w->root);
    const uint32_t nowTime = SDL_GetTicks();
    const double elapsed = (nowTime - d->lastOverflowScrollTime) / 1000.0;
    int dist = bottom_Rect(rect) + gap_UI - bottom_Rect(visible);
    const int step = iRound(10 * dist * elapsed);
    if (step > 0) {
        iWidget *scrollable = findOverflowScrollable_Widget(w);
        if (scrollable) {
            scrollOverflow_Widget(scrollable, -iClamp(step, 1, dist));
            d->lastOverflowScrollTime = nowTime;
        }
    }
    if (dist > 0) {
        addTicker_App(overflowScrollToKeepVisible_InputWidget_, widget);
    }
}

static iBool isSelectAllEvent_InputWidget_(const SDL_KeyboardEvent *ev) {
    /* Note: If this were a binding, it would have to conditional on an InputWidget being focused. */
    if (ev->state != SDL_PRESSED) {
        return iFalse;
    }
    const int key  = ev->keysym.sym;
    const int mods = keyMods_Sym(ev->keysym.mod);
#if defined (iPlatformTerminal)
    return key == SDLK_a && mods == KMOD_ALT;
#else
    return key == SDLK_a && mods == KMOD_PRIMARY;
#endif    
}

static iBool processEvent_InputWidget_(iInputWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    /* Resize according to width immediately. */
    if (d->lastUpdateWidth != w->rect.size.x) {
        d->inFlags |= needUpdateBuffer_InputWidgetFlag;
        if (contentBounds_InputWidget_(d).size.x < minWidth_InputWidget_) {
            setFocus_Widget(NULL);
            return iFalse;
        }
        if (d->inFlags & isUrl_InputWidgetFlag) {
            /* Restore/omit the default scheme if necessary. */
            setText_InputWidget(d, text_InputWidget(d));
        }
        updateAllLinesAndResizeHeight_InputWidget_(d);
        d->lastUpdateWidth = w->rect.size.x;
    }
#if LAGRANGE_USE_SYSTEM_TEXT_INPUT
    if (isResize_UserEvent(ev)) {
        if (d->sysCtrl) {
            updateAfterVisualOffsetChange_InputWidget_(d, w->root);
        }
    }
#endif
    if (deviceType_App() != desktop_AppDeviceType && isCommand_UserEvent(ev, "menu.opened")) {
        setFocus_Widget(NULL);
        return iFalse;
    }
    if (isCommand_Widget(w, ev, "focus.gained")) {
        if (contentBounds_InputWidget_(d).size.x < minWidth_InputWidget_) {
            setFocus_Widget(NULL);
        }
        else {
            begin_InputWidget(d);
        }
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
#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT
    else if (isCommand_UserEvent(ev, "prefs.blink.changed")) {
        if (isEditing_InputWidget_(d) && arg_Command(command_UserEvent(ev))) {
            startOrStopCursorTimer_InputWidget_(d, 2);
        }
        return iFalse;
    }
    else if (isEditing_InputWidget_(d) && (isCommand_UserEvent(ev, "window.focus.lost") ||
                                           isCommand_UserEvent(ev, "window.focus.gained"))) {
        startOrStopCursorTimer_InputWidget_(d, isCommand_UserEvent(ev, "window.focus.gained"));
        d->cursorVis = 1;
        refresh_Widget(d);
        return iFalse;
    }
    else if ((isCommand_UserEvent(ev, "copy") || isCommand_UserEvent(ev, "input.copy")) &&
             isEditing_InputWidget_(d)) {
        copy_InputWidget_(d, argLabel_Command(command_UserEvent(ev), "cut"));
        return iTrue;
    }
//    else if (isFocused_Widget(d) && isCommand_UserEvent(ev, "copy")) {
//        copy_InputWidget_(d, iFalse);
//        return iTrue;
//    }
    else if (isCommand_UserEvent(ev, "input.paste") && isEditing_InputWidget_(d)) {
        paste_InputWidget_(d);
        if (argLabel_Command(command_UserEvent(ev), "enter")) {
            d->inFlags |= enterPressed_InputWidgetFlag;
            setFocus_Widget(NULL);            
        }
        return iTrue;
    }
    else if (isCommand_UserEvent(ev, "input.undo") && isEditing_InputWidget_(d)) {
        if (popUndo_InputWidget_(d)) {
            refresh_Widget(w);
            contentsWereChanged_InputWidget_(d);
        }
        return iTrue;
    }
    else if (isCommand_UserEvent(ev, "text.insert")) {
        pushUndo_InputWidget_(d);
        deleteMarked_InputWidget_(d);
        insertChar_InputWidget_(d, arg_Command(command_UserEvent(ev)));
        contentsWereChanged_InputWidget_(d);
        return iTrue;
    }
#endif
    else if (isCommand_UserEvent(ev, "input.selectall") && isEditing_InputWidget_(d)) {
        selectAll_InputWidget(d);
        return iTrue;
    }
    else if (isCommand_UserEvent(ev, "theme.changed")) {
        if (d->buffered) {
            d->inFlags |= needUpdateBuffer_InputWidgetFlag;
        }
        return iFalse;
    }
    else if (isCommand_UserEvent(ev, "keyboard.changed")) {
        const iBool isKeyboardVisible = (arg_Command(command_UserEvent(ev)) != 0);
        /* Scroll to keep widget visible when keyboard appears. */
        if (isFocused_Widget(d) && findOverflowScrollable_Widget(parent_Widget(d))) {
            if (isKeyboardVisible) {
                d->lastOverflowScrollTime = SDL_GetTicks();
                overflowScrollToKeepVisible_InputWidget_(d);
            }
            else {
                setFocus_Widget(NULL); /* stop editing */
            }
        }
        return iFalse;
    }
    else if (isCommand_UserEvent(ev, "input.overflow")) {
        if (isFocused_Widget(d)) {
            d->lastOverflowScrollTime = SDL_GetTicks();
            overflowScrollToKeepVisible_InputWidget_(d);
        }
        return iFalse;
    }
    else if (isCommand_Widget(w, ev, "input.backup")) {
        if (d->inFlags & needBackup_InputWidgetFlag) {
            saveBackup_InputWidget_(d);
        }
        return iTrue;
    }
    else if (isMetricsChange_UserEvent(ev)) {
        updateMetrics_InputWidget_(d);
     //   updateLinesAndResize_InputWidget_(d);
    }
#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT
    if (ev->type == SDL_MOUSEWHEEL && contains_Widget(w, coord_MouseWheelEvent(&ev->wheel))) {
        if (numWrapLines_InputWidget_(d) <= size_Range(&d->visWrapLines)) {
            return ignored_EventResult;
        }
        const int lineHeight = lineHeight_Text(d->font);
        if (isPerPixel_MouseWheelEvent(&ev->wheel)) {
            d->wheelAccum -= ev->wheel.y;
            refresh_Widget(d);
        }
        else {
            d->wheelAccum -= ev->wheel.y * 3 * lineHeight;
        }
        clampWheelAccum_InputWidget_(d, ev->wheel.y);
        int lineDelta = d->wheelAccum / lineHeight;
        if (lineDelta < 0) {
            lineDelta = iMax(lineDelta, -d->visWrapLines.start);
            if (!lineDelta) d->wheelAccum = 0;
        }
        else if (lineDelta > 0) {
            lineDelta = iMin(lineDelta, lastLine_InputWidget_(d)->wrapLines.end - d->visWrapLines.end);
            if (!lineDelta) d->wheelAccum = 0;
        }
        if (lineDelta) {
            d->wheelAccum         -= lineDelta * lineHeight;
            d->visWrapLines.start += lineDelta;
            d->visWrapLines.end   += lineDelta;
            clampWheelAccum_InputWidget_(d, ev->wheel.y);
            d->inFlags |= needUpdateBuffer_InputWidgetFlag;
            refresh_Widget(d);
            return true_EventResult;
        }
        return false_EventResult;
    }
    if (ev->type == SDL_TEXTINPUT && isFocused_Widget(w)) {
        pushUndo_InputWidget_(d);
        deleteMarked_InputWidget_(d);
        insertRange_InputWidget_(d, range_CStr(ev->text.text));
        contentsWereChanged_InputWidget_(d);
        return iTrue;
    }
    const iInt2 curMax    = cursorMax_InputWidget_(d);
    const iInt2 lineFirst = init_I2(0, d->cursor.y);
    const iInt2 lineLast  = init_I2(endX_InputWidget_(d, d->cursor.y), d->cursor.y);
#endif
    /* Click behavior depends on device type. */ {
        const int mbResult = (deviceType_App() == desktop_AppDeviceType
                              ? processPointerEvents_InputWidget_(d, ev)
                              : processTouchEvents_InputWidget_(d, ev));
        if (mbResult) {
            return mbResult >> 1;
        }
    }
    if (ev->type == SDL_KEYUP && isFocused_Widget(w)) {
        return iTrue;
    }
    if (ev->type == SDL_KEYDOWN && isFocused_Widget(w)) {
        const int key  = ev->key.keysym.sym;
        const int mods = keyMods_Sym(ev->key.keysym.mod);
#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT
        if (mods == KMOD_UNDO) {
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
#  if defined (iPlatformApple)
        if (mods == KMOD_PRIMARY || mods == (KMOD_PRIMARY | KMOD_SHIFT)) {
            switch (key) {
                case SDLK_UP:
                case SDLK_DOWN:
                    setCursor_InputWidget(d, key == SDLK_UP ? zero_I2() : curMax);
                    refresh_Widget(d);
                    return iTrue;
            }
        }
#  endif
        d->prevCursor = d->cursor;
        if (isSelectAllEvent_InputWidget_(&ev->key)) {
            selectAll_InputWidget(d);
            d->mark.start = 0;
            d->mark.end   = cursorToIndex_InputWidget_(d, curMax);
            d->cursor     = curMax;
            showCursor_InputWidget_(d);
            refresh_Widget(w);
            return iTrue;            
        }
#endif /* !LAGRANGE_USE_SYSTEM_TEXT_INPUT */
        switch (key) {
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT
                if (isAllowedToInsertNewline_InputWidget_(d)) {
                    if (checkLineBreakMods_InputWidget_(d, mods)) {
                        pushUndo_InputWidget_(d);
                        deleteMarked_InputWidget_(d);
                        insertChar_InputWidget_(d, '\n');
                        contentsWereChanged_InputWidget_(d);
                        return iTrue;
                    }
                }
#endif
                if (d->inFlags & enterKeyEnabled_InputWidgetFlag &&
                    (checkAcceptMods_InputWidget_(d, mods) ||
                     (~d->inFlags & lineBreaksEnabled_InputWidgetFlag))) {
                    d->inFlags |= enterPressed_InputWidgetFlag;
                    setFocus_Widget(NULL);
                    return iTrue;
                }
                return iFalse;
            case SDLK_ESCAPE:
                end_InputWidget(d, iTrue);
                setFocus_Widget(NULL);
                return (d->inFlags & eatEscape_InputWidgetFlag) != 0;
#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT
            case SDLK_INSERT:
                if (mods == KMOD_SHIFT) {
                    paste_InputWidget_(d);
                }
                return iTrue;
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
                    d->mark.start = cursorToIndex_InputWidget_(d, d->cursor);
                    d->mark.end   = cursorToIndex_InputWidget_(d, skipWord_InputWidget_(d, d->cursor, +1));
                    deleteMarked_InputWidget_(d);
                    contentsWereChanged_InputWidget_(d);
                }
                else if (!isEqual_I2(d->cursor, curMax)) {
                    pushUndo_InputWidget_(d);
                    deleteIndexRange_InputWidget_(d, (iRanges){
                        cursorToIndex_InputWidget_(d, d->cursor),
                        cursorToIndex_InputWidget_(d, movedCursor_InputWidget_(d, d->cursor, +1, 0))
                    });
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
                        iInputLine *line = cursorLine_InputWidget_(d);
                        truncate_String(&line->text, d->cursor.x);
                        if (!isLastLine_InputWidget_(d, line)) {
                            appendCStr_String(&line->text, "\n"); /* must have a newline */
                        }
                        lineTextWasChanged_InputWidget_(d, line);
                        contentsWereChanged_InputWidget_(d);
                    }
                    showCursor_InputWidget_(d);
                    refresh_Widget(w);
                    return iTrue;
                }
                break;
            case SDLK_HOME:
            case SDLK_END:
                if (mods == KMOD_PRIMARY || mods == (KMOD_PRIMARY | KMOD_SHIFT)) {
                    setCursor_InputWidget(d, key == SDLK_HOME ? zero_I2() : curMax);
                }
                else {
                    moveCursorByLine_InputWidget_(d, 0, key == SDLK_HOME ? -1 : +1);
                }
                refresh_Widget(w);
                return iTrue;
            case SDLK_a:
            case SDLK_e:
                if (mods == KMOD_CTRL || mods == (KMOD_CTRL | KMOD_SHIFT)) {
#  if defined (iPlatformTerminal)
                    /* Move to the start/end of the current wrapped line. */
                    moveCursorByLine_InputWidget_(d, 0, key == 'a' ? -1 : +1);
                    refresh_Widget(w);
                    return iTrue;
#  endif
#  if defined (iPlatformApple)
                    /* Move to the start/end of the current paragraph. */
                    setCursor_InputWidget(d, key == 'a' ? lineFirst : lineLast);
                    refresh_Widget(w);
                    return iTrue;
#  endif
                }
                break;
            case SDLK_LEFT:
            case SDLK_RIGHT: {
                const int dir = (key == SDLK_LEFT ? -1 : +1);
                if (mods & byLine_KeyModifier) {
                    moveCursorByLine_InputWidget_(d, 0, dir);
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
                if (mods == (KMOD_ALT | KMOD_SHIFT)) {
                    pushUndo_InputWidget_(d);
                    deleteMarked_InputWidget_(d);
                    insertChar_InputWidget_(d, '\t');
                    contentsWereChanged_InputWidget_(d);
                    return iTrue;
                }
                /* Allow focus switching. */
                return processEvent_Widget(as_Widget(d), ev);
            case SDLK_UP:
            case SDLK_DOWN:
                if (moveCursorByLine_InputWidget_(d, key == SDLK_UP ? -1 : +1, 0)) {
                    refresh_Widget(d);
                    return iTrue;
                }
                if (isArrowUpDownConsumed_InputWidget_(d)) {
                    return iTrue;
                }
                /* For moving to lookup from url entry. */
                return processEvent_Widget(as_Widget(d), ev);
            case SDLK_PAGEUP:
            case SDLK_PAGEDOWN:
                for (int count = 0; count < 5; count++) {
                    moveCursorByLine_InputWidget_(d, key == SDLK_PAGEUP ? -1 : +1, 0);
                }
                refresh_Widget(d);
                return iTrue;
#endif /* !LAGRANGE_USE_SYSTEM_TEXT_INPUT */
        }
        if (mods & (KMOD_GUI | KMOD_CTRL)) {
            return iFalse;
        }
        return iTrue;
    }
    return processEvent_Widget(w, ev);
}

#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT
iDeclareType(MarkPainter)

struct Impl_MarkPainter {
    iPaint *            paint;
    const iInputWidget *d;
    iRect               contentBounds;
    const iInputLine *  line;
    iInt2               pos;
    iRanges             mark;
    iRect               firstMarkRect;
    iRect               lastMarkRect;
};

static iBool draw_MarkPainter_(iWrapText *wrapText, iRangecc wrappedText, iTextAttrib attrib,
                               int origin, int advance) {
    iMarkPainter *mp = wrapText->context;
    const iRanges mark = mp->mark;
    if (isEmpty_Range(&mark)) {
        return iTrue; /* nothing marked */
    }
    int fontId = mp->d->font;
    /* TODO: Apply attrib on the font */
    const char *cstr = cstr_String(&mp->line->text);
    const iRanges lineRange = {
        wrappedText.start - cstr + mp->line->range.start,
        wrappedText.end   - cstr + mp->line->range.start
    };
    const int lineHeight = lineHeight_Text(mp->d->font);
    if (mark.end <= lineRange.start || mark.start >= lineRange.end) {
        mp->pos.y += lineHeight;
        return iTrue; /* outside of mark */
    }
    iRect rect = { addX_I2(mp->pos, origin), init_I2(advance, lineHeight) };
    if (mark.end < lineRange.end) {
        /* Calculate where the mark ends. */
        const iRangecc markedPrefix = {
            wrappedText.start,
            wrappedText.start + mark.end - lineRange.start
        };
        rect.size.x = measureRange_Text(fontId, markedPrefix).advance.x;
    }
    if (mark.start > lineRange.start) {
        /* Calculate where the mark starts. */
        const iRangecc unmarkedPrefix = {
            wrappedText.start,
            wrappedText.start + mark.start - lineRange.start
        };
        adjustEdges_Rect(&rect, 0, 0, 0, measureRange_Text(fontId, unmarkedPrefix).advance.x);
    }
    rect.size.x = iMax(gap_UI / 3, rect.size.x);
    mp->pos.y += lineHeight;
    fillRect_Paint(mp->paint, rect, uiMarked_ColorId | opaque_ColorId);
    if (deviceType_App() != desktop_AppDeviceType) {
        if (isEmpty_Rect(mp->firstMarkRect)) mp->firstMarkRect = rect;
        mp->lastMarkRect = rect;
    }
    return iTrue;
}
#endif

static void draw_InputWidget_(const iInputWidget *d) {
    const iWidget *w         = constAs_Widget(d);
    iRect          bounds    = adjusted_Rect(bounds_InputWidget_(d), padding_(), neg_I2(padding_()));
    iBool          isHint    = isHintVisible_InputWidget_(d);
    const iBool    isFocused = isFocused_Widget(w);    
    const iBool    isHover   = deviceType_App() == desktop_AppDeviceType &&
                               isHover_Widget(w) &&
                               contains_InputWidget_(d, mouseCoord_Window(get_Window(), 0));
    if (d->inFlags & needUpdateBuffer_InputWidgetFlag) {
        updateBuffered_InputWidget_(iConstCast(iInputWidget *, d));
    }
    iPaint p;
    init_Paint(&p);
    /* `lines` is already up to date and ready for drawing. */
    fillRect_Paint(
        &p, bounds, isFocused ? uiInputBackgroundFocused_ColorId : uiInputBackground_ColorId);
    if (!isTerminal_Platform()) {
        drawRectThickness_Paint(&p,
                                adjusted_Rect(bounds, neg_I2(one_I2()), zero_I2()),
                                isFocused ? gap_UI / 4 : 1,
                                isFocused ? uiInputFrameFocused_ColorId
                                : isHover ? uiInputFrameHover_ColorId : uiInputFrame_ColorId);
    }
    if (d->sysCtrl) {
        /* The system-provided control is drawing the text. */
        drawChildren_Widget(w);
        return;
    }
    const iRect contentBounds = contentBounds_InputWidget_(d);
    iInt2       drawPos       = topLeft_Rect(contentBounds);
    const int   fg            = isHint      ? uiAnnotation_ColorId
                                : isFocused ? uiInputTextFocused_ColorId
                                            : uiInputText_ColorId;
#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT
    setClip_Paint(&p,
                  adjusted_Rect(bounds,
                                init_I2(d->leftPadding, 0),
                                init_I2(-d->rightPadding,
                                        w->flags & extraPadding_WidgetFlag ? -gap_UI / 2 : 0)));
    iWrapText wrapText = {
        .maxWidth     = d->maxLen == 0 ? width_Rect(contentBounds) : unlimitedWidth_InputWidget_,
        .mode         = (d->inFlags & isUrl_InputWidgetFlag ? anyCharacter_WrapTextMode
                                                            : word_WrapTextMode),
        .overrideChar = (d->inFlags & isSensitive_InputWidgetFlag ? sensitiveChar_ : 0),
    };
    const iRangei visLines       = visibleLineRange_InputWidget_(d);
    iRect         markerRects[2] = { zero_Rect(), zero_Rect() };
#endif
    const int     visLineOffsetY = visLineOffsetY_InputWidget_(d);
    /* If buffered, just draw the buffered copy. */
    if (d->buffered && !isFocused) {
        /* Most input widgets will use this, since only one is focused at a time. */
        if (flags_Widget(w) & alignRight_WidgetFlag) {
            draw_TextBuf(
                d->buffered,
                addY_I2(init_I2(right_Rect(contentBounds) - d->buffered->size.x, drawPos.y),
                        visLineOffsetY),
                white_ColorId);
        }
        else {        
            draw_TextBuf(d->buffered, addY_I2(drawPos, visLineOffsetY), white_ColorId);
        }
    }
    else if (isHint) {
        if (flags_Widget(w) & alignRight_WidgetFlag) {
            drawAlign_Text(d->font,
                           init_I2(right_Rect(contentBounds), drawPos.y),
                           uiInputCursor_ColorId,
                           right_Alignment,
                           "%s",
                           cstr_String(&d->hint));
        }
        else {
            drawRange_Text(d->font, drawPos, uiInputCursor_ColorId, range_String(&d->hint));
        }
    }
#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT
    else {
        iAssert(~d->inFlags & isSensitive_InputWidgetFlag || size_Range(&visLines) == 1);
        drawPos.y += visLineOffsetY;
        iMarkPainter marker = {
            .paint = &p,
            .d = d,
            .contentBounds = contentBounds,
            .mark = mark_InputWidget_(d),
        };
        wrapText.context = &marker;
        wrapText.wrapFunc = isFocused ? draw_MarkPainter_ : NULL; /* mark is drawn under each line of text */
        for (size_t vis = visLines.start; vis < visLines.end; vis++) {
            const iInputLine *line = constAt_Array(&d->lines, vis);
            wrapText.text = range_String(&line->text);
            marker.line   = line;
            marker.pos    = drawPos;
            addv_I2(&drawPos, draw_WrapText(&wrapText, d->font, drawPos, fg).advance); /* lines end with \n */
        }
        markerRects[0] = marker.firstMarkRect;
        markerRects[1] = marker.lastMarkRect;
        wrapText.wrapFunc = NULL;
        wrapText.context  = NULL;
    }
    /* Draw the insertion point. */
    if (isFocused && (d->cursorVis || !isBlinkingCursor_()) &&
        contains_Range(&visLines, d->cursor.y) &&
        (deviceType_App() == desktop_AppDeviceType || isEmpty_Range(&d->mark))) {
        iInt2    curSize;
        iRangecc cursorChar    = iNullRange;
        int      visWrapsAbove = 0;
        for (int i = d->cursor.y - 1; i >= visLines.start; i--) {
            const iInputLine *line = constAt_Array(&d->lines, i);
            visWrapsAbove += numWrapLines_InputLine_(line);
        }
        if (d->mode == overwrite_InputMode) {
            /* Block cursor that overlaps a character. */
            cursorChar.start = charPos_InputWidget_(d, d->cursor);
            iChar ch = 0;
            int n = decodeBytes_MultibyteChar(cursorChar.start,
                                              constEnd_String(&constCursorLine_InputWidget_(d)->text),
                                              &ch);
            cursorChar.end = cursorChar.start + iMax(n, 0);
            if (ch) {
                if (d->inFlags & isSensitive_InputWidgetFlag) {
                    cursorChar = range_CStr(sensitive_);
                }
            }
            else {
                cursorChar = range_CStr(" ");
            }
            curSize = addX_I2(measureRange_Text(d->font, ch ? cursorChar : range_CStr("0")).bounds.size,
                              iMin(2, gap_UI / 4));
        }
        else {
            /* Bar cursor. */
            curSize = init_I2(gap_UI / 2, lineHeight_Text(d->font));
        }
        const iInt2 advance = relativeCursorCoord_InputWidget_(d);
        const iInt2 curPos = add_I2(addY_I2(topLeft_Rect(contentBounds), visLineOffsetY +
                                            visWrapsAbove * lineHeight_Text(d->font)),
                                    addX_I2(advance,
                                            (d->mode == insert_InputMode ? -curSize.x / 2 : 0)));
        const iRect curRect  = { curPos, curSize };
#if defined (SDL_SEAL_CURSES)
        /* Tell where to place the terminal cursor. */
        SDL_SetTextInputRect((const SDL_Rect *) &curRect);  
#endif
        fillRect_Paint(&p, curRect, uiInputCursor_ColorId);
        if (d->mode == overwrite_InputMode) {
            /* The `gap_UI` offset below is a hack. They are used because for some reason the
               cursor rect and the glyph inside don't quite position like during `run_Text_()`. */
            drawRange_Text(d->font,
                           addX_I2(curPos, iMin(1, gap_UI / 8)),
                           uiInputCursorText_ColorId,
                           cursorChar);
        }
    }
    unsetClip_Paint(&p);
    if (!isEmpty_Rect(markerRects[0])) {
        for (int i = 0; i < 2; ++i) {
            drawPin_Paint(&p, markerRects[i], i, uiTextCaution_ColorId);
        }
    }
#endif
    drawChildren_Widget(w);
}

iBeginDefineSubclass(InputWidget, Widget)
    .processEvent = (iAny *) processEvent_InputWidget_,
    .draw         = (iAny *) draw_InputWidget_,
iEndDefineSubclass(InputWidget)
