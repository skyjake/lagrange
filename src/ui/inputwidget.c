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
#include "paint.h"
#include "util.h"
#include "keys.h"
#include "app.h"

#include <the_Foundation/array.h>
#include <SDL_clipboard.h>
#include <SDL_timer.h>

#if defined (iPlatformApple)
#   include "macos.h"
#endif

static const int    refreshInterval_InputWidget_ = 256;
static const size_t maxUndo_InputWidget_         = 64;

static void enableEditorKeysInMenus_(iBool enable) {
#if defined (iPlatformApple)
    enableMenuItemsByKey_MacOS(SDLK_LEFT, KMOD_PRIMARY, enable);
    enableMenuItemsByKey_MacOS(SDLK_RIGHT, KMOD_PRIMARY, enable);
#else
    iUnused(enable);
#endif
}

iDeclareType(InputUndo)

struct Impl_InputUndo {
    iArray text;
    size_t cursor;
};

static void init_InputUndo_(iInputUndo *d, const iArray *text, size_t cursor) {
    initCopy_Array(&d->text, text);
    d->cursor = cursor;
}

static void deinit_InputUndo_(iInputUndo *d) {
    deinit_Array(&d->text);
}

enum iInputWidgetFlag {
    isSensitive_InputWidgetFlag      = iBit(1),
    enterPressed_InputWidgetFlag     = iBit(2),
    selectAllOnFocus_InputWidgetFlag = iBit(3),
    notifyEdits_InputWidgetFlag      = iBit(4),
    eatEscape_InputWidgetFlag        = iBit(5),
    isMarking_InputWidgetFlag        = iBit(6),
};

struct Impl_InputWidget {
    iWidget         widget;
    enum iInputMode mode;
    int             inFlags;
    size_t          maxLen;
    iArray          text;    /* iChar[] */
    iArray          oldText; /* iChar[] */
    iString         hint;
    size_t          cursor;
    size_t          lastCursor;
    iRanges         mark;
    iArray          undoStack;
    int             font;
    iClick          click;
    int             cursorVis;
    uint32_t        timer;
    iTextBuf *      buffered;
};

iDefineObjectConstructionArgs(InputWidget, (size_t maxLen), maxLen)

static void clearUndo_InputWidget_(iInputWidget *d) {
    iForEach(Array, i, &d->undoStack) {
        deinit_InputUndo_(i.value);
    }
    clear_Array(&d->undoStack);
}

static void showCursor_InputWidget_(iInputWidget *d) {
    d->cursorVis = 2;
}

void init_InputWidget(iInputWidget *d, size_t maxLen) {
    iWidget *w = &d->widget;
    init_Widget(w);
    setFlags_Widget(w, focusable_WidgetFlag | hover_WidgetFlag, iTrue);
    init_Array(&d->text, sizeof(iChar));
    init_Array(&d->oldText, sizeof(iChar));
    init_String(&d->hint);
    init_Array(&d->undoStack, sizeof(iInputUndo));
    d->font             = uiInput_FontId | alwaysVariableFlag_FontId;
    d->cursor           = 0;
    d->lastCursor       = 0;
    d->inFlags          = eatEscape_InputWidgetFlag;
    iZap(d->mark);
    setMaxLen_InputWidget(d, maxLen);
    /* Caller must arrange the width, but the height is fixed. */
    w->rect.size.y = lineHeight_Text(default_FontId) + 2 * gap_UI;
    setFlags_Widget(w, fixedHeight_WidgetFlag, iTrue);
    init_Click(&d->click, d, SDL_BUTTON_LEFT);
    d->timer = 0;
    d->cursorVis = 0;
    d->buffered = NULL;
}

void deinit_InputWidget(iInputWidget *d) {
    if (isSelected_Widget(d)) {
        enableEditorKeysInMenus_(iTrue);
    }
    delete_TextBuf(d->buffered);
    clearUndo_InputWidget_(d);
    deinit_Array(&d->undoStack);
    if (d->timer) {
        SDL_RemoveTimer(d->timer);
    }
    deinit_String(&d->hint);
    deinit_Array(&d->oldText);
    deinit_Array(&d->text);
}

static void pushUndo_InputWidget_(iInputWidget *d) {
    iInputUndo undo;
    init_InputUndo_(&undo, &d->text, d->cursor);
    pushBack_Array(&d->undoStack, &undo);
    if (size_Array(&d->undoStack) > maxUndo_InputWidget_) {
        deinit_InputUndo_(front_Array(&d->undoStack));
        popFront_Array(&d->undoStack);
    }
}

static iBool popUndo_InputWidget_(iInputWidget *d) {
    if (!isEmpty_Array(&d->undoStack)) {
        iInputUndo *undo = back_Array(&d->undoStack);
        setCopy_Array(&d->text, &undo->text);
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

void setSensitive_InputWidget(iInputWidget *d, iBool isSensitive) {
    iChangeFlags(d->inFlags, isSensitive_InputWidgetFlag, isSensitive);
}

const iString *text_InputWidget(const iInputWidget *d) {
    return collect_String(newUnicodeN_String(constData_Array(&d->text), size_Array(&d->text)));
}

void setMaxLen_InputWidget(iInputWidget *d, size_t maxLen) {
    d->maxLen = maxLen;
    d->mode   = (maxLen == 0 ? insert_InputMode : overwrite_InputMode);
    resize_Array(&d->text, maxLen);
    if (maxLen) {
        /* Set a fixed size. */
        iBlock *content = new_Block(maxLen);
        fill_Block(content, 'M');
        setSize_Widget(
            as_Widget(d),
            add_I2(measure_Text(d->font, cstr_Block(content)), init_I2(6 * gap_UI, 2 * gap_UI)));
        delete_Block(content);
    }
}

void setHint_InputWidget(iInputWidget *d, const char *hintText) {
    setCStr_String(&d->hint, hintText);
}

static const iChar sensitiveChar_ = 0x25cf; /* black circle */

static iString *visText_InputWidget_(const iInputWidget *d) {
    iString *text;
    if (~d->inFlags & isSensitive_InputWidgetFlag) {
        text = newUnicodeN_String(constData_Array(&d->text), size_Array(&d->text));
    }
    else {
        text = new_String();
        for (size_t i = 0; i < size_Array(&d->text); ++i) {
            appendChar_String(text, sensitiveChar_);
        }
    }
    return text;
}

static void invalidateBuffered_InputWidget_(iInputWidget *d) {
    if (d->buffered) {
        delete_TextBuf(d->buffered);
        d->buffered = NULL;
    }
}

static void updateBuffered_InputWidget_(iInputWidget *d) {
    invalidateBuffered_InputWidget_(d);
    iString *visText = visText_InputWidget_(d);
    d->buffered = new_TextBuf(d->font, cstr_String(visText));
    delete_String(visText);
}

void setText_InputWidget(iInputWidget *d, const iString *text) {
    clearUndo_InputWidget_(d);
    clear_Array(&d->text);
    iConstForEach(String, i, text) {
        pushBack_Array(&d->text, &i.value);
    }
    if (isFocused_Widget(d)) {
        d->cursor = size_Array(&d->text);
        selectAll_InputWidget(d);
    }
    else {
        d->cursor = iMin(d->cursor, size_Array(&d->text));
        iZap(d->mark);
    }
    if (!isFocused_Widget(d)) {
        updateBuffered_InputWidget_(d);
    }
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

void selectAll_InputWidget(iInputWidget *d) {
    d->mark = (iRanges){ 0, size_Array(&d->text) };
    refresh_Widget(as_Widget(d));
}

void begin_InputWidget(iInputWidget *d) {
    iWidget *w = as_Widget(d);
    if (flags_Widget(w) & selected_WidgetFlag) {
        /* Already active. */
        return;
    }
    invalidateBuffered_InputWidget_(d);
    setFlags_Widget(w, hidden_WidgetFlag | disabled_WidgetFlag, iFalse);
    setCopy_Array(&d->oldText, &d->text);
    if (d->mode == overwrite_InputMode) {
        d->cursor = 0;
    }
    else {
        d->cursor = iMin(size_Array(&d->text), d->maxLen - 1);
    }
    SDL_StartTextInput();
    setFlags_Widget(w, selected_WidgetFlag, iTrue);
    showCursor_InputWidget_(d);
    refresh_Widget(w);
    d->timer = SDL_AddTimer(refreshInterval_InputWidget_, cursorTimer_, d);
    d->inFlags &= ~enterPressed_InputWidgetFlag;
    if (d->inFlags & selectAllOnFocus_InputWidgetFlag) {
        d->mark = (iRanges){ 0, size_Array(&d->text) };
    }
    else {
        iZap(d->mark);
    }
    enableEditorKeysInMenus_(iFalse);
}

void end_InputWidget(iInputWidget *d, iBool accept) {
    iWidget *w = as_Widget(d);
    if (~flags_Widget(w) & selected_WidgetFlag) {
        /* Was not active. */
        return;
    }
    enableEditorKeysInMenus_(iTrue);
    if (!accept) {
        setCopy_Array(&d->text, &d->oldText);
    }
    updateBuffered_InputWidget_(d);
    SDL_RemoveTimer(d->timer);
    d->timer = 0;
    SDL_StopTextInput();
    setFlags_Widget(w, selected_WidgetFlag, iFalse);
    const char *id = cstr_String(id_Widget(as_Widget(d)));
    if (!*id) id = "_";
    refresh_Widget(w);
    postCommand_Widget(w,
                       "input.ended id:%s enter:%d arg:%d",
                       id,
                       d->inFlags & enterPressed_InputWidgetFlag ? 1 : 0,
                       accept ? 1 : 0);
}

static void insertChar_InputWidget_(iInputWidget *d, iChar chr) {
    if (d->mode == insert_InputMode) {
        insert_Array(&d->text, d->cursor, &chr);
        d->cursor++;
    }
    else if (d->maxLen == 0 || d->cursor < d->maxLen) {
        if (d->cursor >= size_Array(&d->text)) {
            resize_Array(&d->text, d->cursor + 1);
        }
        set_Array(&d->text, d->cursor++, &chr);
        if (d->maxLen && d->cursor == d->maxLen) {
            setFocus_Widget(NULL);
        }
    }
    showCursor_InputWidget_(d);
    refresh_Widget(as_Widget(d));
}

iLocalDef size_t cursorMax_InputWidget_(const iInputWidget *d) {
    return iMin(size_Array(&d->text), d->maxLen - 1);
}

iLocalDef iBool isMarking_(void) {
    return (SDL_GetModState() & KMOD_SHIFT) != 0;
}

void setCursor_InputWidget(iInputWidget *d, size_t pos) {
    showCursor_InputWidget_(d);
    if (isEmpty_Array(&d->text)) {
        d->cursor = 0;
    }
    else {
        d->cursor = iClamp(pos, 0, cursorMax_InputWidget_(d));
    }
    /* Update selection. */
    if (isMarking_()) {
        if (isEmpty_Range(&d->mark)) {
            d->mark.start = d->lastCursor;
            d->mark.end   = d->cursor;
        }
        else {
            d->mark.end = d->cursor;
        }
    }
    else {
        iZap(d->mark);
    }
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
    m.start   = iMin(m.start, size_Array(&d->text));
    m.end     = iMin(m.end, size_Array(&d->text));
    return m;
}

static void contentsWereChanged_InputWidget_(iInputWidget *d) {
    if (d->inFlags & notifyEdits_InputWidgetFlag) {
        postCommand_Widget(d, "input.edited id:%s", cstr_String(id_Widget(constAs_Widget(d))));
    }
}

static iBool deleteMarked_InputWidget_(iInputWidget *d) {
    const iRanges m = mark_InputWidget_(d);
    if (!isEmpty_Range(&m)) {
        removeRange_Array(&d->text, m);
        setCursor_InputWidget(d, m.start);
        iZap(d->mark);
        return iTrue;
    }
    return iFalse;
}

static iBool isWordChar_InputWidget_(const iInputWidget *d, size_t pos) {
    const iChar ch = pos < size_Array(&d->text) ? constValue_Array(&d->text, pos, iChar) : ' ';
    return isAlphaNumeric_Char(ch);
}

iLocalDef iBool movePos_InputWidget_(const iInputWidget *d, size_t *pos, int dir) {
    if (dir < 0) {
        if (*pos > 0) (*pos)--; else return iFalse;
    }
    else {
        if (*pos < cursorMax_InputWidget_(d)) (*pos)++; else return iFalse;
    }
    return iTrue;
}

static size_t skipWord_InputWidget_(const iInputWidget *d, size_t pos, int dir) {
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

iLocalDef iInt2 padding_(void) {
    return init_I2(gap_UI / 2, gap_UI / 2);
}

static iInt2 textOrigin_InputWidget_(const iInputWidget *d, const char *visText) {
    const iWidget *w         = constAs_Widget(d);
    iRect          bounds    = adjusted_Rect(bounds_Widget(w), padding_(), neg_I2(padding_()));
    const iInt2    emSize    = advance_Text(d->font, "M");
    const int      textWidth = advance_Text(d->font, visText).x;
    const int      cursorX   = advanceN_Text(d->font, visText, d->cursor).x;
    int            xOff      = 0;
    shrink_Rect(&bounds, init_I2(gap_UI * (flags_Widget(w) & tight_WidgetFlag ? 1 : 2), 0));
    if (d->maxLen == 0) {
        if (textWidth > width_Rect(bounds) - emSize.x) {
            xOff = width_Rect(bounds) - emSize.x - textWidth;
        }
        if (cursorX + xOff < width_Rect(bounds) / 2) {
            xOff = width_Rect(bounds) / 2 - cursorX;
        }
        xOff = iMin(xOff, 0);
    }
    const int yOff = (height_Rect(bounds) - lineHeight_Text(d->font)) / 2;
    return add_I2(topLeft_Rect(bounds), init_I2(xOff, yOff));
}

static size_t coordIndex_InputWidget_(const iInputWidget *d, iInt2 coord) {
    iString *visText = visText_InputWidget_(d);
    iInt2    pos     = sub_I2(coord, textOrigin_InputWidget_(d, cstr_String(visText)));
    size_t   index   = 0;
    if (pos.x > 0) {
        const char *endPos;
        tryAdvanceNoWrap_Text(d->font, range_String(visText), pos.x, &endPos);
        if (endPos == constEnd_String(visText)) {
            index = cursorMax_InputWidget_(d);
        }
        else {
            /* Need to know the actual character index. */
            /* TODO: tryAdvance could tell us this directly with an extra return value */
            iConstForEach(String, i, visText) {
                if (i.pos >= endPos) break;
                index++;
            }
        }
    }
    delete_String(visText);
    return index;
}

static iBool copy_InputWidget_(iInputWidget *d, iBool doCut) {
    if (!isEmpty_Range(&d->mark)) {
        const iRanges m = mark_InputWidget_(d);
        SDL_SetClipboardText(cstrCollect_String(
            newUnicodeN_String(constAt_Array(&d->text, m.start), size_Range(&m))));
        if (doCut) {
            pushUndo_InputWidget_(d);
            deleteMarked_InputWidget_(d);
            contentsWereChanged_InputWidget_(d);
        }
        return iTrue;
    }
    return iFalse;
}

static iBool processEvent_InputWidget_(iInputWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (isCommand_Widget(w, ev, "focus.gained")) {
        begin_InputWidget(d);
        return iFalse;
    }
    else if (isCommand_Widget(w, ev, "focus.lost")) {
        end_InputWidget(d, iTrue);
        return iFalse;
    }
    else if (isCommand_UserEvent(ev, "theme.changed")) {
        if (d->buffered) {
            updateBuffered_InputWidget_(d);
        }
        return iFalse;
    }
    else if (isFocused_Widget(d) && isCommand_UserEvent(ev, "copy")) {
        copy_InputWidget_(d, iFalse);
        return iTrue;
    }
    if (ev->type == SDL_MOUSEMOTION && isHover_Widget(d)) {
        setCursor_Window(get_Window(), SDL_SYSTEM_CURSOR_IBEAM);
    }
    switch (processEvent_Click(&d->click, ev)) {
        case none_ClickResult:
            break;
        case started_ClickResult:
            setFocus_Widget(w);
            setCursor_InputWidget(d, coordIndex_InputWidget_(d, pos_Click(&d->click)));
            iZap(d->mark);
            d->inFlags &= ~isMarking_InputWidgetFlag;
            return iTrue;
        case double_ClickResult:
            selectAll_InputWidget(d);
            d->inFlags &= ~isMarking_InputWidgetFlag;
            return iTrue;
        case aborted_ClickResult:
            d->inFlags &= ~isMarking_InputWidgetFlag;
            return iTrue;
        case drag_ClickResult:
            showCursor_InputWidget_(d);
            d->cursor = coordIndex_InputWidget_(d, pos_Click(&d->click));
            if (~d->inFlags & isMarking_InputWidgetFlag) {
                d->inFlags |= isMarking_InputWidgetFlag;
                d->mark.start = d->cursor;
            }
            d->mark.end = d->cursor;
            refresh_Widget(w);
            return iTrue;
        case finished_ClickResult:
            return iTrue;
    }
    if (ev->type == SDL_KEYUP && isFocused_Widget(w)) {
        return iTrue;
    }
    const size_t curMax = cursorMax_InputWidget_(d);
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
                    if (SDL_HasClipboardText()) {
                        pushUndo_InputWidget_(d);
                        deleteMarked_InputWidget_(d);
                        char *text = SDL_GetClipboardText();
                        iString *paste = collect_String(newCStr_String(text));
                        SDL_free(text);
                        iConstForEach(String, i, paste) {
                            insertChar_InputWidget_(d, i.value);
                        }
                        contentsWereChanged_InputWidget_(d);
                    }
                    return iTrue;
                case 'z':
                    if (popUndo_InputWidget_(d)) {
                        refresh_Widget(w);
                        contentsWereChanged_InputWidget_(d);
                    }
                    return iTrue;
            }
        }
        d->lastCursor = d->cursor;
        switch (key) {
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                d->inFlags |= enterPressed_InputWidgetFlag;
                setFocus_Widget(NULL);
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
                    d->mark.start = d->cursor;
                    d->mark.end   = skipWord_InputWidget_(d, d->cursor, -1);
                    deleteMarked_InputWidget_(d);
                    contentsWereChanged_InputWidget_(d);
                }
                else if (d->cursor > 0) {
                    pushUndo_InputWidget_(d);
                    remove_Array(&d->text, --d->cursor);
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
            case SDLK_HOME:
            case SDLK_END:
                setCursor_InputWidget(d, key == SDLK_HOME ? 0 : curMax);
                refresh_Widget(w);
                return iTrue;
            case SDLK_a:
#if defined (iPlatformApple)
                if (mods == KMOD_PRIMARY) {
                    d->mark.start = 0;
                    d->mark.end   = curMax;
                    d->cursor     = curMax;
                    showCursor_InputWidget_(d);
                    refresh_Widget(w);
                    return iTrue;
                }
#endif
                /* fall through for Emacs-style Home/End */
            case SDLK_e:
                if (mods == KMOD_CTRL || mods == (KMOD_CTRL | KMOD_SHIFT)) {
                    setCursor_InputWidget(d, key == 'a' ? 0 : curMax);
                    refresh_Widget(w);
                    return iTrue;
                }
                break;
            case SDLK_LEFT:
            case SDLK_RIGHT: {
                const int dir = (key == SDLK_LEFT ? -1 : +1);
                if (mods & byLine_KeyModifier) {
                    setCursor_InputWidget(d, dir < 0 ? 0 : curMax);
                }
                else if (mods & byWord_KeyModifier) {
                    setCursor_InputWidget(d, skipWord_InputWidget_(d, d->cursor, dir));
                }
                else if (!isMarking_() && !isEmpty_Range(&d->mark)) {
                    const iRanges m = mark_InputWidget_(d);
                    setCursor_InputWidget(d, dir < 0 ? m.start : m.end);
                    iZap(d->mark);
                }
                else if ((dir < 0 && d->cursor > 0) || (dir > 0 && d->cursor < curMax)) {
                    setCursor_InputWidget(d, d->cursor + dir);
                }
                refresh_Widget(w);
                return iTrue;
            }
            case SDLK_TAB:
            case SDLK_DOWN: /* for moving to lookup from url entry */
                /* Allow focus switching. */
                return processEvent_Widget(as_Widget(d), ev);
        }
        if (mods & (KMOD_PRIMARY | KMOD_SECONDARY)) {
            return iFalse;
        }
        return iTrue;
    }
    else if (ev->type == SDL_TEXTINPUT && isFocused_Widget(w)) {
        pushUndo_InputWidget_(d);
        deleteMarked_InputWidget_(d);
        const iString *uni = collectNewCStr_String(ev->text.text);
        iConstForEach(String, i, uni) {
            insertChar_InputWidget_(d, i.value);
        }
        contentsWereChanged_InputWidget_(d);
        return iTrue;
    }
    return processEvent_Widget(w, ev);
}

static iBool isWhite_(const iString *str) {
    iConstForEach(String, i, str) {
        if (!isSpace_Char(i.value)) {
            return iFalse;
        }
    }
    return iTrue;
}

static void draw_InputWidget_(const iInputWidget *d) {
    const iWidget *w         = constAs_Widget(d);
    iRect          bounds    = adjusted_Rect(bounds_Widget(w), padding_(), neg_I2(padding_()));
    iBool          isHint    = iFalse;
    const iBool    isFocused = isFocused_Widget(w);
    const iBool    isHover   = isHover_Widget(w) &&
                               contains_Widget(w, mouseCoord_Window(get_Window()));
    iPaint p;
    init_Paint(&p);
    iString *text = visText_InputWidget_(d);
    if (isWhite_(text) && !isEmpty_String(&d->hint)) {
        set_String(text, &d->hint);
        isHint = iTrue;
    }
    fillRect_Paint(
        &p, bounds, isFocused ? uiInputBackgroundFocused_ColorId : uiInputBackground_ColorId);
    drawRectThickness_Paint(&p,
                            adjusted_Rect(bounds, neg_I2(one_I2()), zero_I2()),
                            isFocused ? gap_UI / 4 : 1,
                            isFocused ? uiInputFrameFocused_ColorId
                                      : isHover ? uiInputFrameHover_ColorId : uiInputFrame_ColorId);
    setClip_Paint(&p, bounds);
    const iInt2 textOrigin = textOrigin_InputWidget_(d, cstr_String(text));
    if (isFocused && !isEmpty_Range(&d->mark)) {
        /* Draw the selected range. */
        const int m1 = advanceN_Text(d->font, cstr_String(text), d->mark.start).x;
        const int m2 = advanceN_Text(d->font, cstr_String(text), d->mark.end).x;
        fillRect_Paint(&p,
                       (iRect){ addX_I2(textOrigin, iMin(m1, m2)),
                                init_I2(iAbs(m2 - m1), lineHeight_Text(d->font)) },
                       uiMarked_ColorId);
    }
    if (d->buffered && !isFocused && !isHint) {
        /* Most input widgets will use this, since only one is focused at a time. */
        draw_TextBuf(d->buffered, textOrigin, uiInputText_ColorId);
    }
    else {
        draw_Text(d->font,
                  textOrigin,
                  isHint ? uiAnnotation_ColorId
                         : isFocused && !isEmpty_Array(&d->text) ? uiInputTextFocused_ColorId
                                                                 : uiInputText_ColorId,
                  "%s",
                  cstr_String(text));
    }
    unsetClip_Paint(&p);
    /* Cursor blinking. */
    if (isFocused && d->cursorVis) {
        iString cur;
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
        /* The `gap_UI` offsets below are a hack. They are used because for some reason the
           cursor rect and the glyph inside don't quite position like during `run_Text_()`. */
        const iInt2 prefixSize = advanceN_Text(d->font, cstr_String(text), d->cursor);
        const iInt2 curPos     = addX_I2(textOrigin, prefixSize.x);
        const iRect curRect    = { curPos, addX_I2(advance_Text(d->font, cstr_String(&cur)), iMin(2, gap_UI / 4)) };
        fillRect_Paint(&p, curRect, uiInputCursor_ColorId);
        draw_Text(d->font, addX_I2(curPos, iMin(1, gap_UI / 8)), uiInputCursorText_ColorId, "%s", cstr_String(&cur));
        deinit_String(&cur);
    }
    delete_String(text);
    drawChildren_Widget(w);
}

iBeginDefineSubclass(InputWidget, Widget)
    .processEvent = (iAny *) processEvent_InputWidget_,
    .draw         = (iAny *) draw_InputWidget_,
iEndDefineSubclass(InputWidget)
