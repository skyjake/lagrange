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

/* Fully custom text editor. The primary source of complexity is the handling of wrapped
   text content. */

#include "impl.h"

#include "app.h"
#include "gmutil.h"
#include "periodic.h"
#include "ui/command.h"
#include "ui/keys.h"
#include "ui/touch.h"
#include "ui/window.h"
#include <lagrange/prefs.h>
#include <lagrange/snippets.h>

#include <the_Foundation/ptrset.h>
#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_timer.h>

#if defined (iPlatformAppleDesktop)
#   include "platform/macos.h"
#endif

static const char *sensitive_ = "●";

static void enableEditorKeysInMenus_(iBool enable) {
#if defined (iPlatformAppleDesktop)
    enableMenuItemsByKey_MacOS(SDLK_LEFT,  KMOD_PRIMARY, enable);
    enableMenuItemsByKey_MacOS(SDLK_RIGHT, KMOD_PRIMARY, enable);
    enableMenuItemsByKey_MacOS(SDLK_UP,    KMOD_PRIMARY, enable);
    enableMenuItemsByKey_MacOS(SDLK_DOWN,  KMOD_PRIMARY, enable);
    enableMenuItemsByKey_MacOS(SDLK_UP,    KMOD_PRIMARY | SDL_KMOD_SHIFT, enable);
    enableMenuItemsByKey_MacOS(SDLK_DOWN,  KMOD_PRIMARY | SDL_KMOD_SHIFT, enable);
#else
    iUnused(enable);
#endif
}

static iPtrSet *activeInputWidgets_(void) {
    static iPtrSet *set_;
    if (!set_) {
        set_ = new_PtrSet();
    }
    return set_;
}

static void deactivateInputMode_InputWidget_(iInputWidget *d) {
    /* We can only end the text input mode if no active input widgets remain. */
    remove_PtrSet(activeInputWidgets_(), d);
    if (isEmpty_PtrSet(activeInputWidgets_())) {
        setTextInputActive_App(iFalse);
        enableEditorKeysInMenus_(iTrue);
    }
}

iLocalDef iBool isBlinkingCursor_(void) {
    /* Terminal will blink if appropriate. */
    return prefs_App()->blinkingCursor && !isTerminal_Platform();
}

static void startOrStopCursorTimer_InputWidget_(iInputWidget *d, int doStart) {
    if (!isBlinkingCursor_() && doStart == 1) {
        doStart = iFalse;
    }
    if (doStart) {
        add_Periodic(periodic_App(), d, "input.blink");
    }
    else if (!doStart) {
        remove_Periodic(periodic_App(), d);
    }
}

iInputLayout layout_InputWidget_(const iInputWidget *d) {
    return (iInputLayout){
        .wrap      = wrapParams_InputWidget_(d),
        .font      = d->font,
        .minWidth  = minWidth_InputWidget_,
        .insertion = preeditRange_InputWidget_(d),
    };
}

static void relayout_InputWidget_(iInputWidget *d, iRangei lines) {
    const iInputLayout layout = layout_InputWidget_(d);
    relayout_EditBuf(&d->buf, &layout, lines);
}

static iInt2 relativeCoord_InputWidget_(const iInputWidget *d, iInt2 pos) {
    const iInputLayout layout = layout_InputWidget_(d);
    return relativeCoord_EditBuf(&d->buf, &layout, pos);
}

static iRangei visibleLineRange_InputWidget_(const iInputWidget *d) {
    return visibleLines_EditBuf(&d->buf, d->visWrapLines);
}

int visLineOffsetY_InputWidget_(const iInputWidget *d) {
    const iInputLine *line = findLineByWrapY_EditBuf(&d->buf, d->visWrapLines.start);
    return (line->wrapLines.start - d->visWrapLines.start) * lineHeight_Text(d->font) -
           d->wheelAccum;
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
        wc.y += lineHeight_Text(d->font) * numWrapLines_InputLine(line_EditBuf(&d->buf, i));
    }
    addv_I2(&wc, relativeCoord_InputWidget_(d, pos));
    *isInsideBounds = contains_Rect(bounds, wc);
    return wc;
}

void updateVisible_InputWidget_(iInputWidget *d) {
    if (width_Widget(d) == 0) {
        return; /* Nothing to do yet. */
    }
    const int totalWraps = numWrapLines_EditBuf(&d->buf);
    const int visWraps = iClamp(totalWraps, d->minWrapLines, d->maxWrapLines);
    /* Resize the height of the editor. */
    d->visWrapLines.end = d->visWrapLines.start + visWraps;
    /* Determine which wraps are currently visible. */
    d->buf.cursor.y = iMin(d->buf.cursor.y, size_Array(&d->buf.lines) - 1);
    const iInputLine *curLine = line_EditBuf(&d->buf, d->buf.cursor.y);
    const int cursorY = curLine->wrapLines.start +
        relativeCoord_InputWidget_(d, d->buf.cursor).y / lineHeight_Text(d->font);
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
    if (!isFocused_Widget(d) && d->maxWrapLines == 1) {
        d->visWrapLines.start = 0;
        d->visWrapLines.end = 1;
    }
}

void showCursor_InputWidget_(iInputWidget *d) {
    d->cursorVis = 2;
    updateVisible_InputWidget_(d);
}

void initVariant_InputWidget_(iInputWidget *d) {
    init_EditBuf(&d->buf);
    d->prevCursor   = zero_I2();
    d->lastTapTime  = 0;
    d->tapCount     = 0;
    d->cursorVis    = 0;
    initPreedit_InputWidget_(d);
}

void deinitVariant_InputWidget_(iInputWidget *d) {
    deinitPreedit_InputWidget_(d);
    startOrStopCursorTimer_InputWidget_(d, iFalse);
    deactivateInputMode_InputWidget_(d);
    deinit_EditBuf(&d->buf);
}

iString *newText_InputWidget_(const iInputWidget *d) {
    iString *text = new_String();
    text_EditBuf(&d->buf, text);
    return text;
}

iString *newVisibleText_InputWidget_(const iInputWidget *d) {
    iString *visText = new_String();
    const iRangei visRange = visibleLineRange_InputWidget_(d);
    for (int i = visRange.start; i < visRange.end; i++) {
        append_String(visText, &line_EditBuf(&d->buf, i)->text);
    }
    return visText;
}

iBool isEmpty_InputWidget_(const iInputWidget *d) {
    return isEmpty_EditBuf(&d->buf);
}

iBool isAllSelected_InputWidget_(const iInputWidget *d) {
    return isAllMarked_EditBuf(&d->buf);
}

void replaceText_InputWidget_(iInputWidget *d, const iString *text, iBool isUndoable) {
    if (isUndoable) {
        pushUndo_EditBuf(&d->buf);
    }
    else {
        clearUndo_EditBuf(&d->buf);
    }
    setText_EditBuf(&d->buf, text);
    iAssert(!isEmpty_Array(&d->buf.lines));
    relayout_InputWidget_(d, (iRangei){ 0, size_Array(&d->buf.lines) });
    d->buf.cursor = cursorMax_EditBuf(&d->buf);
    if (!isFocused_Widget(d)) {
        iZap(d->buf.mark);
    }
}

void updateAllLinesAndResizeHeight_InputWidget_(iInputWidget *d) {
    const int oldWraps = numWrapLines_EditBuf(&d->buf);
    relayout_InputWidget_(d, (iRangei){ 0, size_Array(&d->buf.lines) });
    updateVisible_InputWidget_(d);
    if (oldWraps != numWrapLines_EditBuf(&d->buf)) {
        updateMetrics_InputWidget_(d);
    }
}

int contentHeight_InputWidget_(const iInputWidget *d) {
    return (int) size_Range(&d->visWrapLines) * lineHeight_Text(d->font);
}

iBool isHeightKnown_InputWidget_(const iInputWidget *d) {
    iUnused(d);
    return iTrue;
}

void updateTextInputRect_InputWidget_(const iInputWidget *d) {
#if !defined (SDL_SEAL_CURSES)
    /* Tell SDL where the text cursor is so the OS can position IME candidate
       windows (e.g., the Korean Hanja selection panel) near the insertion point.
       Coordinates must be in points, not pixels, hence the pixelRatio division. */
    iBool inBounds;
    iInt2 wc = cursorToWindowCoord_InputWidget_(d, d->buf.cursor, &inBounds);
    const int lh = lineHeight_Text(d->font);
    wc.x += preeditAdvance_InputWidget_(d);
    const float pr = get_Window()->pixelRatio;
    SDL_SetTextInputArea(get_Window()->win, &(SDL_Rect){
        (int)(wc.x / pr), (int)(wc.y / pr), 1, (int)(lh / pr)
    }, 0);
#else
    iUnused(d);
#endif
}

static iBool popUndo_InputWidget_(iInputWidget *d) {
    if (popUndo_EditBuf(&d->buf)) {
        updateAllLinesAndResizeHeight_InputWidget_(d);
        return iTrue;
    }
    return iFalse;
}

static void textOfLinesWasChanged_InputWidget_(iInputWidget *d, iRangei lineRange) {
    relayout_InputWidget_(d, lineRange);
    updateVisible_InputWidget_(d);
    updateMetrics_InputWidget_(d);
    restartBackupTimer_InputWidget_(d);
}

void insertRange_InputWidget_(iInputWidget *d, iRangecc range) {
    textOfLinesWasChanged_InputWidget_(d, insert_EditBuf(&d->buf, range, d->mode, d->maxLen));
    showCursor_InputWidget_(d);
    refresh_Widget(as_Widget(d));
}

static void insertChar_InputWidget_(iInputWidget *d, iChar chr) {
    iMultibyteChar mb;
    init_MultibyteChar(&mb, chr);
    insertRange_InputWidget_(d, range_CStr(mb.bytes));
}

iLocalDef iBool isMarking_(void) {
    return (modState_Keys() & SDL_KMOD_SHIFT) != 0;
}

static void setCursor_InputWidget_(iInputWidget *d, iInt2 pos) {
    iAssert(!isEmpty_Array(&d->buf.lines));
    pos.x = iClamp(pos.x, 0, endX_EditBuf(&d->buf, pos.y));
    d->buf.cursor = pos;
    /* Update selection. */
    if (isMarking_()) {
        if (isEmpty_Range(&d->buf.mark)) {
            d->buf.mark.start = cursorToIndex_EditBuf(&d->buf, d->prevCursor);
            d->buf.mark.end   = cursorToIndex_EditBuf(&d->buf, d->buf.cursor);
        }
        else {
            d->buf.mark.end = cursorToIndex_EditBuf(&d->buf, d->buf.cursor);
        }
    }
    else {
        iZap(d->buf.mark);
    }
    showCursor_InputWidget_(d);
}

void moveCursorHome_InputWidget(iInputWidget *d) {
    setCursor_InputWidget_(d, zero_I2());
}

static iBool moveCursorByLine_InputWidget_(iInputWidget *d, int dir, int horiz) {
    const iInputLine *line     = cursorLine_EditBuf(&d->buf);
    iInt2             relCoord = relativeCoord_InputWidget_(d, d->buf.cursor);
    int               relLine  = relCoord.y / lineHeight_Text(d->font);
    if ((dir < 0 && relLine > 0) || (dir > 0 && relLine < numWrapLines_InputLine(line) - 1)) {
        relCoord.y += dir * lineHeight_Text(d->font);
    }
    else if (dir < 0 && d->buf.cursor.y > 0) {
        d->buf.cursor.y--;
        line = cursorLine_EditBuf(&d->buf);
        relCoord.y = lineHeight_Text(d->font) * (numWrapLines_InputLine(line) - 1);
    }
    else if (dir > 0 && d->buf.cursor.y < size_Array(&d->buf.lines) - 1) {
        d->buf.cursor.y++;
        relCoord.y = 0;
    }
    else if (dir == 0 && horiz != 0) {
        relCoord.x = (horiz < 0 ? 0 : width_Widget(d));
    }
    else {
        return iFalse;
    }
    const iInputLayout layout = layout_InputWidget_(d);
    iWrapText wt = wrap_EditBuf(&d->buf, &layout, d->buf.cursor.y);
    if (isEqual_I2(relCoord, zero_I2())) {
        /* (0, 0) disables the hit test, but this is trivial to figure out. */
        wt.hitChar_out = wt.text.start;
    }
    else {
        wt.hitPoint = addY_I2(relCoord, 1 * aspect_UI);
        measure_WrapText(&wt, d->font);
    }
    if (wt.hitChar_out) {
        d->buf.cursor.x = wt.hitChar_out - wt.text.start;
    }
    else {
        d->buf.cursor.x = endX_EditBuf(&d->buf, d->buf.cursor.y);
    }
    if (wt.hitGlyphNormX_out > 0.5f && d->buf.cursor.x < endX_EditBuf(&d->buf, d->buf.cursor.y)) {
        iChar ch;
        int n = decodeBytes_MultibyteChar(wt.text.start + d->buf.cursor.x, wt.text.end, &ch);
        if (ch != '\n' && n > 0) {
            d->buf.cursor.x += n;
        }
    }
    setCursor_InputWidget_(d, d->buf.cursor); /* mark, show */
    return iTrue;
}

static void deleteIndexRange_InputWidget_(iInputWidget *d, iRanges deleted) {
    restartBackupTimer_InputWidget_(d);
    const int firstModified = delete_EditBuf(&d->buf, deleted);
    if (firstModified >= 0) {
        /* Rewrap the lines that may have been cut in half. */
        relayout_InputWidget_(d, (iRangei){
            firstModified, iMin(firstModified + 2, (int) size_Array(&d->buf.lines)) });
    }
    updateVisible_InputWidget_(d);
    updateMetrics_InputWidget_(d);
}

iBool deleteMarked_InputWidget_(iInputWidget *d) {
    const iRanges m = mark_EditBuf(&d->buf);
    if (!isEmpty_Range(&m)) {
        deleteIndexRange_InputWidget_(d, m);
        setCursor_InputWidget_(d, indexToCursor_EditBuf(&d->buf, m.start));
        iZap(d->buf.mark); /* setCursor thinks we're marking when Shift is down */
        return iTrue;
    }
    return iFalse;
}

static iInt2 coordCursor_InputWidget_(const iInputWidget *d, iInt2 coord) {
    const iRect bounds   = contentBounds_InputWidget_(d);
    const iInt2 relCoord = sub_I2(coord, addY_I2(topLeft_Rect(bounds),
                                                 visLineOffsetY_InputWidget_(d)));
    if (relCoord.y < 0) {
        return zero_I2();
    }
    iWrapText wrapText = wrapParams_InputWidget_(d);
    wrapText.hitPoint = relCoord;
    const iRangei visLines = visibleLineRange_InputWidget_(d);
    for (size_t y = visLines.start; y < visLines.end; y++) {
        wrapText.text = range_String(&line_EditBuf(&d->buf, y)->text);
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
            return init_I2(iMin(pos - wrapText.text.start, endX_EditBuf(&d->buf, y)), y);
        }
        wrapText.hitPoint.y -= tm.advance.y;
    }
    return cursorMax_EditBuf(&d->buf);
}

static iBool copy_InputWidget_(iInputWidget *d, iBool doCut) {
    if (!isEmpty_Range(&d->buf.mark)) {
        const iRanges m   = mark_EditBuf(&d->buf);
        iString *     str = collectNew_String();
        textRange_EditBuf(&d->buf, m, str);
        SDL_SetClipboardText(
            cstr_String(d->inFlags & isUrl_InputWidgetFlag ? canonicalUrl_String(str) : str));
        if (doCut) {
            pushUndo_EditBuf(&d->buf);
            deleteMarked_InputWidget_(d);
            contentsWereChanged_InputWidget_(d);
        }
        return iTrue;
    }
    return iFalse;
}

static void paste_InputWidget_(iInputWidget *d) {
    if (SDL_HasClipboardText()) {
        pushUndo_EditBuf(&d->buf);
        deleteMarked_InputWidget_(d);
        char *   text  = SDL_GetClipboardText();
        iString *paste = collect_String(newCStr_String(text));
        replace_String(paste, "\r", ""); // we expect line breaks to be just LF, no CRs
        /* Url decoding. */
        if (d->inFlags & isUrl_InputWidgetFlag) {
            trim_String(paste);
            if (prefs_App()->decodeUserVisibleURLs) {
                paste = collect_String(urlDecodeExclude_String(paste, URL_DECODE_EXCLUDE_CHARS));
                replace_String(paste, "\n", "%0A");
                replace_String(paste, "\t", "%09");
            }
            else {
                paste = collect_String(urlEncodeExclude_String(paste, URL_ENCODE_EXCLUDE_CHARS));
            }
        }
        SDL_free(text);
        insertRange_InputWidget_(d, range_String(paste));
        contentsWereChanged_InputWidget_(d);
    }
}

static void cursorLineWasChanged_InputWidget_(iInputWidget *d) {
    textOfLinesWasChanged_InputWidget_(d, (iRangei){ d->buf.cursor.y, d->buf.cursor.y + 1 });
}

void selectAll_InputWidget(iInputWidget *d) {
    markAll_EditBuf(&d->buf);
    refresh_Widget(as_Widget(d));
}

void deselect_InputWidget(iInputWidget *d) {
    iZap(d->buf.mark);
    refresh_Widget(as_Widget(d));
}

void beginEditing_InputWidget_(iInputWidget *d, iBool allowSelectAll) {
    iWidget *w = as_Widget(d);
    text_EditBuf(&d->buf, &d->oldText);
    if (d->mode == overwrite_InputMode) {
        d->buf.cursor = zero_I2();
    }
    else {
        d->buf.cursor.y = iMin(d->buf.cursor.y, size_Array(&d->buf.lines) - 1);
        d->buf.cursor.x = iMin(d->buf.cursor.x, cursorLine_EditBuf(&d->buf)->range.end);
    }
    insert_PtrSet(activeInputWidgets_(), d);
    setTextInputActive_App(iTrue);
    enableEditorKeysInMenus_(iFalse);
    showCursor_InputWidget_(d);
    refresh_Widget(w);
    startOrStopCursorTimer_InputWidget_(d, iTrue);
    if (allowSelectAll && d->inFlags & selectAllOnFocus_InputWidgetFlag) {
        markAll_EditBuf(&d->buf);
        d->buf.cursor = cursorMax_EditBuf(&d->buf);
    }
    else if (~d->inFlags & isMarking_InputWidgetFlag) {
        iZap(d->buf.mark);
    }
    updateTextInputRect_InputWidget_(d);
    updateVisible_InputWidget_(d);
    window_Widget(w)->keyPriority = w;
}

void endEditing_InputWidget_(iInputWidget *d, iBool accept) {
    iWidget *w = as_Widget(d);
    if (!accept) {
        /* Overwrite the edited lines. */
        setText_EditBuf(&d->buf, &d->oldText);
        clearPreedit_InputWidget_(d);
    }
    else {
        commitPreedit_InputWidget_(d);
    }
    d->inFlags &= ~isMarking_InputWidgetFlag;
    deactivateInputMode_InputWidget_(d);
    startOrStopCursorTimer_InputWidget_(d, iFalse);
    window_Widget(w)->keyPriority = NULL;
}

static void markWordAtCursor_InputWidget_(iInputWidget *d) {
    d->buf.mark.start = d->buf.mark.end = cursorToIndex_EditBuf(&d->buf, d->buf.cursor);
    extendRange_EditBuf(&d->buf, &d->buf.mark.start, -1);
    extendRange_EditBuf(&d->buf, &d->buf.mark.end, +1);
    d->initialMark = d->buf.mark;
}

static void showClipMenu_InputWidget_(const iInputWidget *d, iInt2 coord) {
    iWidget *clipMenu = findChild_Widget(root_Widget(constAs_Widget(d)), "clipmenu");
    if (!clipMenu) return; /* all main/extra windows have a clipmenu, but popups do not */
    if (isVisible_Widget(clipMenu)) {
        closeMenu_Widget(clipMenu);
    }
    else {
        setMenuItemDisabled_Widget(
            clipMenu, "input.paste enter:1", cmp_String(id_Widget(constAs_Widget(d)), "url"));
        openMenuFlags_Widget(clipMenu, coord, iFalse);
    }
}

enum iEventResult processPointerEvents_InputWidget_(iInputWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (ev->type == SDL_EVENT_MOUSE_MOTION && (isHover_Widget(d) || flags_Widget(w) & keepOnTop_WidgetFlag)) {
        const iInt2 coord = init_I2(ev->motion.x, ev->motion.y);
        const iInt2 inner = windowToInner_Widget(w, coord);
        setCursor_Window(get_Window(),
                         inner.x >= 2 * gap_UI + d->leftPadding &&
                         inner.x < width_Widget(w) - d->rightPadding
                             ? SDL_SYSTEM_CURSOR_TEXT
                             : SDL_SYSTEM_CURSOR_DEFAULT);
    }
    const iInt2 buttonPos = init_I2(ev->button.x, ev->button.y);
    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev->button.button == SDL_BUTTON_RIGHT &&
        contains_Widget(w, buttonPos) /* quick test without traversal */ &&
        hitChild_Widget(w, buttonPos) == w /* don't hit child buttons */) {
        /* Show the copy/paste context menu. */
        setFocus_Widget(w);
        showClipMenu_InputWidget_(d, mouseCoord_Window(get_Window(), ev->button.which));
        return iTrue;
    }
    switch (processEvent_Click(&d->click, ev)) {
        case none_ClickResult:
            break;
        case started_ClickResult: {
            setFocus_Widget(w);
            commitPreedit_InputWidget_(d);
            const iInt2 oldCursor = d->buf.cursor;
            setCursor_InputWidget_(d, coordCursor_InputWidget_(d, pos_Click(&d->click)));
            if (keyMods_Sym(modState_Keys()) == SDL_KMOD_SHIFT) {
                d->buf.mark = d->initialMark = (iRanges){
                    cursorToIndex_EditBuf(&d->buf, oldCursor),
                    cursorToIndex_EditBuf(&d->buf, d->buf.cursor)
                };
                d->inFlags |= isMarking_InputWidgetFlag;
            }
            else {
                iZap(d->buf.mark);
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
            d->buf.cursor = coordCursor_InputWidget_(d, pos_Click(&d->click));
            showCursor_InputWidget_(d);
            if (~d->inFlags & isMarking_InputWidgetFlag) {
                d->inFlags |= isMarking_InputWidgetFlag;
                d->buf.mark.start = cursorToIndex_EditBuf(&d->buf, d->buf.cursor);
            }
            d->buf.mark.end = cursorToIndex_EditBuf(&d->buf, d->buf.cursor);
            if (d->inFlags & markWords_InputWidgetFlag) {
                const iBool isFwd = d->buf.mark.end >= d->buf.mark.start;
                extendRange_EditBuf(&d->buf, &d->buf.mark.end, isFwd ? +1 : -1);
                d->buf.mark.start = isFwd ? d->initialMark.start : d->initialMark.end;
            }
            refresh_Widget(w);
            return true_EventResult;
        case finished_ClickResult:
            d->inFlags &= ~isMarking_InputWidgetFlag;
            return true_EventResult;
    }
    if (ev->type == SDL_EVENT_MOUSE_MOTION && flags_Widget(w) & keepOnTop_WidgetFlag) {
        const iInt2 coord = init_I2(ev->motion.x, ev->motion.y);
        if (contains_Click(&d->click, coord)) {
            return true_EventResult;
        }
    }
    return ignored_EventResult;
}

static iInt2 touchCoordCursor_InputWidget_(const iInputWidget *d, iInt2 coord) {
    /* Clamp to the bounds so the cursor doesn't wrap at the ends. */
    iRect bounds = shrunk_Rect(contentBounds_InputWidget_(d), one_I2());
    bounds.size.y = iMini(numWrapLines_EditBuf(&d->buf), d->maxWrapLines) * lineHeight_Text(d->font) - 2;
    return coordCursor_InputWidget_(d, min_I2(bottomRight_Rect(bounds),
                                              max_I2(coord, topLeft_Rect(bounds))));
}

static iBool isInsideMark_InputWidget_(const iInputWidget *d, size_t pos) {
    const iRanges mark = mark_EditBuf(&d->buf);
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

enum iEventResult processTouchEvents_InputWidget_(iInputWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
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
    if (isFocused_Widget(w)) {
        if (ev->type == SDL_EVENT_USER && ev->user.code == widgetTapBegins_UserEventCode) {
            d->lastTapTime = d->tapStartTime;
            d->tapStartTime = SDL_GetTicks();
            const int tapDist = dist_I2(latestPosition_Touch(), d->lastTapPos);
            d->lastTapPos = latestPosition_Touch();
            if (d->tapStartTime - d->lastTapTime < 400 && tapDist < gap_UI * 4) {
                d->tapCount++;
            }
            else {
                d->tapCount = 0;
            }
            if (!isEmpty_Range(&d->buf.mark)) {
                const int dist[2] = {
                    distanceToPos_InputWidget_(d, latestPosition_Touch(),
                                               indexToCursor_EditBuf(&d->buf, d->buf.mark.start)),
                    distanceToPos_InputWidget_(d, latestPosition_Touch(),
                                               indexToCursor_EditBuf(&d->buf, d->buf.mark.end))
                };
                if (dist[0] < dist[1]) {
                    d->inFlags |= dragMarkerStart_InputWidgetFlag;
                }
                else {
                    d->inFlags |= dragMarkerEnd_InputWidgetFlag;
                }
                d->inFlags |= isMarking_InputWidgetFlag;
                setFlags_Widget(w, touchDrag_WidgetFlag, iTrue);
            }
            else {
                const int dist = distanceToPos_InputWidget_(d, latestPosition_Touch(), d->buf.cursor);
                if (dist < gap_UI * 10) {
                    setFlags_Widget(w, touchDrag_WidgetFlag, iTrue);
                    d->inFlags |= dragCursor_InputWidgetFlag;
                }
            }
            return true_EventResult;
        }
    }
    if ((ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev->type == SDL_EVENT_MOUSE_BUTTON_UP) &&
        ev->button.button == SDL_BUTTON_RIGHT && contains_Widget(w, latestPosition_Touch())) {
        if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            setFocus_Widget(w);
            d->inFlags |= isMarking_InputWidgetFlag;
            d->buf.cursor = touchCoordCursor_InputWidget_(d, latestPosition_Touch());
            markWordAtCursor_InputWidget_(d);
            d->buf.cursor = indexToCursor_EditBuf(&d->buf, d->buf.mark.end);
            refresh_Widget(d);
        }
        return true_EventResult;
    }
    switch (processEvent_Click(&d->click, ev)) {
        case none_ClickResult:
             break;
        case started_ClickResult:
            return true_EventResult;
        case drag_ClickResult:
            if (d->inFlags & dragCursor_InputWidgetFlag) {
                iZap(d->buf.mark);
                d->buf.cursor = touchCoordCursor_InputWidget_(d, pos_Click(&d->click));
                showCursor_InputWidget_(d);
                refresh_Widget(w);
            }
            else if (d->inFlags & dragMarkerStart_InputWidgetFlag) {
                d->buf.mark.start = cursorToIndex_EditBuf(&d->buf, touchCoordCursor_InputWidget_(d, pos_Click(&d->click)));
                refresh_Widget(w);
            }
            else if (d->inFlags & dragMarkerEnd_InputWidgetFlag) {
                d->buf.mark.end = cursorToIndex_EditBuf(&d->buf, touchCoordCursor_InputWidget_(d, pos_Click(&d->click)));
                refresh_Widget(w);
            }
            return true_EventResult;
        case finished_ClickResult:
        case aborted_ClickResult: {
            if (!isFocused_Widget(w)) {
                setFocus_Widget(w);
                d->lastTapPos = latestPosition_Touch();
                d->tapStartTime = SDL_GetTicks();
                d->tapCount = 0;
                d->buf.cursor = touchCoordCursor_InputWidget_(d, pos_Click(&d->click));
                showCursor_InputWidget_(d);
            }
            else if (!isEmpty_Range(&d->buf.mark) && !isMoved_Click(&d->click)) {
                if (isInsideMark_InputWidget_(d, cursorToIndex_EditBuf(&d->buf, touchCoordCursor_InputWidget_(d, latestPosition_Touch())))) {
                    showClipMenu_InputWidget_(d, latestPosition_Touch());
                }
                else {
                    iZap(d->buf.mark);
                    d->buf.cursor = touchCoordCursor_InputWidget_(d, pos_Click(&d->click));
                }
            }
            else if (SDL_GetTicks() - d->lastTapTime > 1000 &&
                     d->tapCount == 0 && isEmpty_Range(&d->buf.mark) && !isMoved_Click(&d->click) &&
                     distanceToPos_InputWidget_(d, latestPosition_Touch(), d->buf.cursor) < gap_UI * 5) {
                showClipMenu_InputWidget_(d, latestPosition_Touch());
            }
            else {
                if (~d->inFlags & isMarking_InputWidgetFlag) {
                    iZap(d->buf.mark);
                    d->buf.cursor = touchCoordCursor_InputWidget_(d, pos_Click(&d->click));
                }
            }
            if (d->inFlags & (dragCursor_InputWidgetFlag | dragMarkerStart_InputWidgetFlag |
                              dragMarkerEnd_InputWidgetFlag)) {
                d->inFlags &= ~(dragCursor_InputWidgetFlag |
                                dragMarkerStart_InputWidgetFlag |
                                dragMarkerEnd_InputWidgetFlag);
                setFlags_Widget(w, touchDrag_WidgetFlag, iFalse);
            }
            d->inFlags &= ~isMarking_InputWidgetFlag;
            showCursor_InputWidget_(d);
            refresh_Widget(w);
            return true_EventResult;
        }
    }
    return ignored_EventResult;
}

static void clampWheelAccum_InputWidget_(iInputWidget *d, int wheel) {
    if (wheel > 0 && d->visWrapLines.start == 0) {
        d->wheelAccum = 0;
        refresh_Widget(d);
    }
    else if (wheel < 0 && d->visWrapLines.end >= lastLine_EditBuf(&d->buf)->wrapLines.end) {
        d->wheelAccum = 0;
        refresh_Widget(d);
    }
}

static iBool isSelectAllEvent_InputWidget_(const SDL_KeyboardEvent *ev) {
    /* Note: If this were a binding, it would have to conditional on an InputWidget being focused. */
    if (!ev->down) {
        return iFalse;
    }
    const int key  = ev->key;
    const int mods = keyMods_Sym(ev->mod);
#if defined (iPlatformTerminal)
    return key == SDLK_A && mods == SDL_KMOD_ALT;
#else
    return key == SDLK_A && mods == KMOD_PRIMARY;
#endif
}

enum iEventResult processCommand_InputWidget_(iInputWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (isCommand_UserEvent(ev, "input.blink")) {
        /* Sent by Periodic. */
        if (d->cursorVis > 1) {
            /* After moving the cursor, it stops blinking for a while. */
            d->cursorVis--;
        }
        else {
            d->cursorVis ^= 1;
        }
        refresh_Widget(d);
        return true_EventResult;
    }
    else if (isCommand_UserEvent(ev, "prefs.blink.changed")) {
        if (isEditing_InputWidget_(d) && arg_Command(command_UserEvent(ev))) {
            startOrStopCursorTimer_InputWidget_(d, 2);
        }
        return false_EventResult;
    }
    else if (isEditing_InputWidget_(d) && (isCommand_UserEvent(ev, "window.focus.lost") ||
                                           isCommand_UserEvent(ev, "window.focus.gained"))) {
        /* Ignore events happening in other windows. */
        if (arg_Command(command_UserEvent(ev)) == id_Window(window_Widget(w))) {
            if (isCommand_UserEvent(ev, "window.focus.lost")) {
                commitPreedit_InputWidget_(d); /* don't lose in-progress composition */
            }
            startOrStopCursorTimer_InputWidget_(d, isCommand_UserEvent(ev, "window.focus.gained"));
            d->cursorVis = 1;
            refresh_Widget(d);
        }
        return false_EventResult;
    }
    else if ((isCommand_UserEvent(ev, "copy") || isCommand_UserEvent(ev, "input.copy")) &&
             isEditing_InputWidget_(d)) {
        copy_InputWidget_(d, argLabel_Command(command_UserEvent(ev), "cut"));
        return true_EventResult;
    }
    else if (isCommand_UserEvent(ev, "input.delete") && isEditing_InputWidget_(d)) {
        pushUndo_EditBuf(&d->buf);
        if (deleteMarked_InputWidget_(d)) {
            contentsWereChanged_InputWidget_(d);
        }
        return true_EventResult;
    }
    else if (isCommand_UserEvent(ev, "input.paste") && isEditing_InputWidget_(d)) {
        const char *cmd = command_UserEvent(ev);
        if (hasLabel_Command(cmd, "snippet")) {
            pushUndo_EditBuf(&d->buf);
            deleteMarked_InputWidget_(d);
            insertRange_InputWidget_(
                d, range_String(get_Snippets(collect_String(suffix_Command(cmd, "snippet")))));
            contentsWereChanged_InputWidget_(d);
            return true_EventResult;
        }
        paste_InputWidget_(d);
        if (argLabel_Command(command_UserEvent(ev), "enter")) {
            d->inFlags |= enterPressed_InputWidgetFlag;
            setFocus_Widget(NULL);
        }
        return true_EventResult;
    }
    else if (isCommand_UserEvent(ev, "input.undo") && isEditing_InputWidget_(d)) {
        if (popUndo_InputWidget_(d)) {
            refresh_Widget(w);
            contentsWereChanged_InputWidget_(d);
        }
        return true_EventResult;
    }
    else if (isCommand_UserEvent(ev, "text.insert")) {
        pushUndo_EditBuf(&d->buf);
        deleteMarked_InputWidget_(d);
        insertChar_InputWidget_(d, arg_Command(command_UserEvent(ev)));
        contentsWereChanged_InputWidget_(d);
        return true_EventResult;
    }
    return ignored_EventResult;
}

enum iEventResult processInputEvent_InputWidget_(iInputWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (ev->type == SDL_EVENT_MOUSE_WHEEL && contains_Widget(w, coord_MouseWheelEvent(&ev->wheel))) {
        if (numWrapLines_EditBuf(&d->buf) <= size_Range(&d->visWrapLines)) {
            return false_EventResult;
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
            lineDelta = iMin(lineDelta, lastLine_EditBuf(&d->buf)->wrapLines.end - d->visWrapLines.end);
            if (!lineDelta) d->wheelAccum = 0;
        }
        if (lineDelta) {
            d->wheelAccum         -= lineDelta * lineHeight;
            d->visWrapLines.start += lineDelta;
            d->visWrapLines.end   += lineDelta;
            clampWheelAccum_InputWidget_(d, ev->wheel.y);
            d->inFlags |= needUpdateBuffer_InputWidgetFlag;
            refresh_Widget(d);
        }
        return true_EventResult;
    }
    if (ev->type == SDL_EVENT_TEXT_INPUT && isFocused_Widget(w)) {
        /* TEXTINPUT: a normal (e.g. non-CJK) keypress or the IME has finalized
           text to commit. */
        if ((modState_Keys() & (SDL_KMOD_CTRL | SDL_KMOD_ALT)) == SDL_KMOD_CTRL) {
            /* Note: AltGr on Windows is reported as Ctrl+Alt. */
            return true_EventResult;
        }
        if (isLinux_Platform() && keyMods_Sym(modState_Keys()) == SDL_KMOD_CTRL) {
            return true_EventResult;
        }
        if (!hasPreedit_InputWidget_(d)) {
            /* Not finishing an IME composition, so this needs its own undo entry.
               (Compositions already pushed undo when they started.) */
            pushUndo_EditBuf(&d->buf);
        }
        clearPreedit_InputWidget_(d);
        deleteMarked_InputWidget_(d);
        insertRange_InputWidget_(d, range_CStr(ev->text.text));
        contentsWereChanged_InputWidget_(d);
        updateTextInputRect_InputWidget_(d);
        return true_EventResult;
    }
    if (processTextEditing_InputWidget_(d, ev)) {
        return true_EventResult;
    }
    return ignored_EventResult;
}

enum iEventResult processKeyDown_InputWidget_(iInputWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    /* While an IME composition is active, SDL sends KEYDOWN before the IME
       processes the key. Consume unmodified and Option keys so the widget
       doesn't also act on them (e.g., Option+Enter for Hanja selection).
       Cmd/Ctrl combos are let through as app shortcuts. */
    if (hasPreedit_InputWidget_(d) &&
        !(ev->key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) &&
        ev->key.key != SDLK_ESCAPE &&
        ev->key.key != SDLK_TAB) {
        return true_EventResult;
    }
    const int   key       = ev->key.key;
    const int   mods      = keyMods_Sym(ev->key.mod);
    const iInt2 curMax    = cursorMax_EditBuf(&d->buf);
    const iInt2 lineFirst = init_I2(0, d->buf.cursor.y);
    const iInt2 lineLast  = init_I2(endX_EditBuf(&d->buf, d->buf.cursor.y), d->buf.cursor.y);
    if (mods == KMOD_UNDO) {
        switch (key) {
            case 'c':
            case 'x':
                copy_InputWidget_(d, key == 'x');
                return true_EventResult;
            case 'v':
                paste_InputWidget_(d);
                return true_EventResult;
            case 'z':
                if (popUndo_InputWidget_(d)) {
                    refresh_Widget(w);
                    contentsWereChanged_InputWidget_(d);
                }
                return true_EventResult;
        }
    }
    if (isApple_Platform() && (mods == KMOD_PRIMARY || mods == (KMOD_PRIMARY | SDL_KMOD_SHIFT))) {
        switch (key) {
            case SDLK_UP:
            case SDLK_DOWN:
                setCursor_InputWidget_(d, key == SDLK_UP ? zero_I2() : curMax);
                refresh_Widget(d);
                return true_EventResult;
        }
    }
    d->prevCursor = d->buf.cursor;
    if (isSelectAllEvent_InputWidget_(&ev->key)) {
        selectAll_InputWidget(d);
        d->buf.mark.start = 0;
        d->buf.mark.end   = cursorToIndex_EditBuf(&d->buf, curMax);
        d->buf.cursor     = curMax;
        showCursor_InputWidget_(d);
        refresh_Widget(w);
        return true_EventResult;
    }
    switch (key) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (isAllowedToInsertNewline_InputWidget_(d)) {
                if (checkLineBreakMods_InputWidget_(d, mods)) {
                    pushUndo_EditBuf(&d->buf);
                    deleteMarked_InputWidget_(d);
                    insertChar_InputWidget_(d, '\n');
                    contentsWereChanged_InputWidget_(d);
                    return true_EventResult;
                }
            }
            break;
        case SDLK_INSERT:
            if (mods == SDL_KMOD_SHIFT) {
                paste_InputWidget_(d);
            }
            return true_EventResult;
        case SDLK_BACKSPACE:
            if (!isEmpty_Range(&d->buf.mark)) {
                pushUndo_EditBuf(&d->buf);
                deleteMarked_InputWidget_(d);
                contentsWereChanged_InputWidget_(d);
            }
            else if (mods & byWord_KeyModifier) {
                pushUndo_EditBuf(&d->buf);
                d->buf.mark.start = cursorToIndex_EditBuf(&d->buf, d->buf.cursor);
                d->buf.mark.end   = cursorToIndex_EditBuf(&d->buf, skipWord_EditBuf(&d->buf, d->buf.cursor, -1));
                deleteMarked_InputWidget_(d);
                contentsWereChanged_InputWidget_(d);
            }
            else if (!isEqual_I2(d->buf.cursor, zero_I2())) {
                pushUndo_EditBuf(&d->buf);
                d->buf.mark.end = cursorToIndex_EditBuf(&d->buf, d->buf.cursor);
                movePos_EditBuf(&d->buf, &d->buf.cursor, -1);
                d->buf.mark.start = cursorToIndex_EditBuf(&d->buf, d->buf.cursor);
                deleteMarked_InputWidget_(d);
                contentsWereChanged_InputWidget_(d);
            }
            else if (isEqual_I2(d->buf.cursor, zero_I2()) && d->maxLen == 1) {
                pushUndo_EditBuf(&d->buf);
                clear_String(&cursorLine_EditBuf(&d->buf)->text);
                cursorLineWasChanged_InputWidget_(d);
                contentsWereChanged_InputWidget_(d);
            }
            showCursor_InputWidget_(d);
            refresh_Widget(w);
            return true_EventResult;
        case SDLK_D:
            if (mods != SDL_KMOD_CTRL) break;
        case SDLK_DELETE:
            if (!isEmpty_Range(&d->buf.mark)) {
                pushUndo_EditBuf(&d->buf);
                deleteMarked_InputWidget_(d);
                contentsWereChanged_InputWidget_(d);
            }
            else if (mods & byWord_KeyModifier) {
                pushUndo_EditBuf(&d->buf);
                d->buf.mark.start = cursorToIndex_EditBuf(&d->buf, d->buf.cursor);
                d->buf.mark.end   = cursorToIndex_EditBuf(&d->buf, skipWord_EditBuf(&d->buf, d->buf.cursor, +1));
                deleteMarked_InputWidget_(d);
                contentsWereChanged_InputWidget_(d);
            }
            else if (!isEqual_I2(d->buf.cursor, curMax)) {
                pushUndo_EditBuf(&d->buf);
                deleteIndexRange_InputWidget_(d, (iRanges){
                    cursorToIndex_EditBuf(&d->buf, d->buf.cursor),
                    cursorToIndex_EditBuf(&d->buf, movedCursor_EditBuf(&d->buf, d->buf.cursor, +1, 0))
                });
                contentsWereChanged_InputWidget_(d);
            }
            showCursor_InputWidget_(d);
            refresh_Widget(w);
            return true_EventResult;
        case SDLK_K:
            if (mods == SDL_KMOD_CTRL) {
                if (!isEmpty_Range(&d->buf.mark)) {
                    pushUndo_EditBuf(&d->buf);
                    deleteMarked_InputWidget_(d);
                    contentsWereChanged_InputWidget_(d);
                }
                else {
                    pushUndo_EditBuf(&d->buf);
                    iInputLine *line = cursorLine_EditBuf(&d->buf);
                    truncate_String(&line->text, d->buf.cursor.x);
                    if (!isLastLine_EditBuf(&d->buf, line)) {
                        appendCStr_String(&line->text, "\n"); /* must have a newline */
                    }
                    cursorLineWasChanged_InputWidget_(d);
                    contentsWereChanged_InputWidget_(d);
                }
                showCursor_InputWidget_(d);
                refresh_Widget(w);
                return true_EventResult;
            }
            break;
        case SDLK_HOME:
        case SDLK_END:
            if (mods == KMOD_PRIMARY || mods == (KMOD_PRIMARY | SDL_KMOD_SHIFT)) {
                setCursor_InputWidget_(d, key == SDLK_HOME ? zero_I2() : curMax);
            }
            else {
                moveCursorByLine_InputWidget_(d, 0, key == SDLK_HOME ? -1 : +1);
            }
            refresh_Widget(w);
            return true_EventResult;
        case SDLK_A:
        case SDLK_E:
            if (mods == SDL_KMOD_CTRL || mods == (SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) {
                if (isTerminal_Platform()) {
                    /* Move to the start/end of the current wrapped line. */
                    moveCursorByLine_InputWidget_(d, 0, key == 'a' ? -1 : +1);
                    refresh_Widget(w);
                    return true_EventResult;
                }
                if (isApple_Platform()) {
                    /* Move to the start/end of the current paragraph. */
                    setCursor_InputWidget_(d, key == 'a' ? lineFirst : lineLast);
                    refresh_Widget(w);
                    return true_EventResult;
                }
            }
            break;
        case SDLK_LEFT:
        case SDLK_RIGHT: {
            const int dir = (key == SDLK_LEFT ? -1 : +1);
            if (mods & byLine_KeyModifier) {
                moveCursorByLine_InputWidget_(d, 0, dir);
            }
            else if (mods & byWord_KeyModifier) {
                setCursor_InputWidget_(d, skipWord_EditBuf(&d->buf, d->buf.cursor, dir));
            }
            else if (!isMarking_() && !isEmpty_Range(&d->buf.mark)) {
                const iRanges m = mark_EditBuf(&d->buf);
                setCursor_InputWidget_(d, indexToCursor_EditBuf(&d->buf, dir < 0 ? m.start : m.end));
                iZap(d->buf.mark);
            }
            else {
                setCursor_InputWidget_(d, movedCursor_EditBuf(&d->buf, d->buf.cursor, dir, 0));
            }
            refresh_Widget(w);
            return true_EventResult;
        }
        case SDLK_TAB:
            if (mods == (SDL_KMOD_ALT | SDL_KMOD_SHIFT)) {
                pushUndo_EditBuf(&d->buf);
                deleteMarked_InputWidget_(d);
                insertChar_InputWidget_(d, '\t');
                contentsWereChanged_InputWidget_(d);
                return true_EventResult;
            }
            /* Allow focus switching. */
            return processEvent_Widget(as_Widget(d), ev) ? true_EventResult : false_EventResult;
        case SDLK_UP:
        case SDLK_DOWN:
            if (moveCursorByLine_InputWidget_(d, key == SDLK_UP ? -1 : +1, 0)) {
                refresh_Widget(d);
                return true_EventResult;
            }
            /* For moving to lookup from url entry. */
            if (processEvent_Widget(as_Widget(d), ev)) {
                return true_EventResult;
            }
            if (d->inFlags & arrowFocusNavigable_InputWidgetFlag) {
                return moveFocusWithArrows_App(ev) ? true_EventResult : false_EventResult;
            }
            return true_EventResult;
        case SDLK_PAGEUP:
        case SDLK_PAGEDOWN:
            for (int count = 0; count < 5; count++) {
                moveCursorByLine_InputWidget_(d, key == SDLK_PAGEUP ? -1 : +1, 0);
            }
            refresh_Widget(d);
            return true_EventResult;
    }
    return ignored_EventResult;
}

/*----------------------------------------------------------------------------------------------*/

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

void drawContent_InputWidget_(const iInputWidget *d, iPaint *p, iRect bounds,
                              iRect contentBounds, int fg) {
    const iWidget *w         = constAs_Widget(d);
    const iBool    isFocused = isFocused_Widget(w);
    iInt2          drawPos   = topLeft_Rect(contentBounds);
    setClip_Paint(p,
                  adjusted_Rect(bounds,
                                init_I2(d->leftPadding, 0),
                                init_I2(-d->rightPadding,
                                        w->flags & extraPadding_WidgetFlag ? -gap_UI / 2 : 0)));
    iWrapText wrapText = {
        .maxWidth     = d->maxLen == 0 ? width_Rect(contentBounds) : unlimitedWidth_InputWidget_,
        .mode         = (d->inFlags & isUrl_InputWidgetFlag ? anyCharacter_WrapTextMode
                                                            : word_WrapTextMode),
        .overrideChar = (d->inFlags & isSensitive_InputWidgetFlag ? sensitiveChar_InputWidget_ : 0),
    };
    const iRangei visLines       = visibleLineRange_InputWidget_(d);
    iRect         markerRects[2] = { zero_Rect(), zero_Rect() };
    const int     visLineOffsetY = visLineOffsetY_InputWidget_(d);
    /* If IME composition is active, create a version of the cursor line with the
       preedit text inserted so draw_WrapText wraps it correctly. */
    const iRangecc preedit        = preeditRange_InputWidget_(d);
    const iBool    hasPreeditLine = isFocused && !isEmpty_Range(&preedit) &&
                                    contains_Range(&visLines, d->buf.cursor.y);
    iString preeditLine;
    init_String(&preeditLine);
    if (hasPreeditLine) {
        set_String(&preeditLine, &line_EditBuf(&d->buf, d->buf.cursor.y)->text);
        insertData_Block(&preeditLine.chars, d->buf.cursor.x, preedit.start, size_Range(&preedit));
    }
    if (!drawStatic_InputWidget_(d, contentBounds, visLineOffsetY)) {
        iAssert(~d->inFlags & isSensitive_InputWidgetFlag || size_Range(&visLines) == 1);
        drawPos.y += visLineOffsetY;
        iMarkPainter marker = {
            .paint = p,
            .d = d,
            .contentBounds = contentBounds,
            .mark = mark_EditBuf(&d->buf),
        };
        wrapText.context = &marker;
        wrapText.wrapFunc = isFocused ? draw_MarkPainter_ : NULL; /* mark is drawn under each line of text */
        for (size_t vis = visLines.start; vis < visLines.end; vis++) {
            const iInputLine *line = line_EditBuf(&d->buf, vis);
            if (hasPreeditLine && (int) vis == d->buf.cursor.y) {
                wrapText.text = range_String(&preeditLine);
            }
            else {
                wrapText.text = range_String(&line->text);
            }
            marker.line   = line;
            marker.pos    = drawPos;
            iInputWidgetHighlight highlight = { .font = d->font, .color = fg };
            if (d->highlighter) {
                highlight = d->highlighter(d, wrapText.text, d->highlighterContext);
            }
            addv_I2(&drawPos,
                    draw_WrapText(&wrapText, highlight.font, drawPos, highlight.color)
                        .advance); /* lines end with \n */
        }
        markerRects[0] = marker.firstMarkRect;
        markerRects[1] = marker.lastMarkRect;
        wrapText.wrapFunc = NULL;
        wrapText.context  = NULL;
    }
    int visWrapsAbove = 0;
    iInt2 cursorCoord = zero_I2();
    if (isFocused && contains_Range(&visLines, d->buf.cursor.y)) {
        for (int i = d->buf.cursor.y - 1; i >= visLines.start; i--) {
            visWrapsAbove += numWrapLines_InputLine(line_EditBuf(&d->buf, i));
        }
        if (hasPreeditLine) {
            /* Compute cursor coord in the preedit-inclusive wrapped layout.
               The cursor sits at the end of the preedit text. */
            iWrapText wt = wrapParams_InputWidget_(d);
            wt.text = range_String(&preeditLine);
            wt.hitChar = wt.text.start + d->buf.cursor.x + size_Range(&preedit);
            measure_WrapText(&wt, d->font);
            cursorCoord = wt.hitAdvance_out;
        }
        else {
            cursorCoord = relativeCoord_InputWidget_(d, d->buf.cursor);
        }
    }
    if (hasPreeditLine) {
        drawPreedit_InputWidget_(d, p,
                                 addY_I2(topLeft_Rect(contentBounds),
                                         visLineOffsetY + visWrapsAbove * lineHeight_Text(d->font)),
                                 &preeditLine,
                                 cursorCoord);
    }
    /* Draw the insertion point. */
    if (isFocused && (d->cursorVis || !isBlinkingCursor_()) &&
        contains_Range(&visLines, d->buf.cursor.y) &&
        (deviceType_App() == desktop_AppDeviceType || isEmpty_Range(&d->buf.mark))) {
        iInt2    curSize;
        iRangecc cursorChar = iNullRange;
        if (d->mode == overwrite_InputMode) {
            /* Block cursor that overlaps a character. */
            cursorChar.start = charPos_EditBuf(&d->buf, d->buf.cursor);
            iChar ch = 0;
            int n = decodeBytes_MultibyteChar(cursorChar.start,
                                              constEnd_String(&line_EditBuf(&d->buf, d->buf.cursor.y)->text),
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
        const iInt2 curPos = add_I2(addY_I2(topLeft_Rect(contentBounds), visLineOffsetY +
                                            visWrapsAbove * lineHeight_Text(d->font)),
                                    addX_I2(addX_I2(cursorCoord,
                                                    hasPreeditLine ? 0 : preeditAdvance_InputWidget_(d)),
                                            (d->mode == insert_InputMode ? -curSize.x / 2 : 0)));
        const iRect curRect  = { curPos, curSize };
#if defined (SDL_SEAL_CURSES)
        /* Tell where to place the terminal cursor. */
        SDL_SetTextInputArea(get_Window()->win, (const SDL_Rect *) &curRect, 0);
#endif
        fillRect_Paint(p, curRect, uiInputCursor_ColorId);
        if (d->mode == overwrite_InputMode) {
            /* The `gap_UI` offset below is a hack. They are used because for some reason the
               cursor rect and the glyph inside don't quite position like during `run_Text_()`. */
            drawRange_Text(d->font,
                           addX_I2(curPos, iMin(1, gap_UI / 8)),
                           uiInputCursorText_ColorId,
                           cursorChar);
        }
    }
    /* Draw the scroll indicator. */ {
        const int lineHeight = lineHeight_Text(d->font);
        iWidgetScrollInfo info;
        contentScrollInfo_Widget(w,
                                 &info,
                                 lineHeight * d->visWrapLines.start,
                                 lineHeight * numWrapLines_EditBuf(&d->buf));
        drawScrollIndicator_Widget(w, &info, uiInputCursor_ColorId, 0.666f);
    }
    unsetClip_Paint(p);
    if (!isEmpty_Rect(markerRects[0])) {
        for (int i = 0; i < 2; ++i) {
            drawPin_Paint(p, markerRects[i], i, uiTextCaution_ColorId);
        }
    }
    deinit_String(&preeditLine);
}
