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
#include "app.h"

#include <the_Foundation/array.h>
#include <SDL_clipboard.h>
#include <SDL_timer.h>

static const int REFRESH_INTERVAL = 256;

struct Impl_InputWidget {
    iWidget         widget;
    enum iInputMode mode;
    iBool           isSensitive;
    iBool           enterPressed;
    size_t          maxLen;
    iArray          text;    /* iChar[] */
    iArray          oldText; /* iChar[] */
    iString         hint;
    size_t          cursor;
    int             font;
    iClick          click;
    uint32_t        timer;
};

iDefineObjectConstructionArgs(InputWidget, (size_t maxLen), maxLen)

void init_InputWidget(iInputWidget *d, size_t maxLen) {
    iWidget *w = &d->widget;
    init_Widget(w);
    setFlags_Widget(w, focusable_WidgetFlag | hover_WidgetFlag, iTrue);
    init_Array(&d->text, sizeof(iChar));
    init_Array(&d->oldText, sizeof(iChar));
    init_String(&d->hint);
    d->font   = uiInput_FontId;
    d->cursor = 0;
    d->isSensitive = iFalse;
    d->enterPressed = iFalse;
    setMaxLen_InputWidget(d, maxLen);
    /* Caller must arrange the width, but the height is fixed. */
    w->rect.size.y = lineHeight_Text(default_FontId) + 2 * gap_UI;
    setFlags_Widget(w, fixedHeight_WidgetFlag, iTrue);
    init_Click(&d->click, d, SDL_BUTTON_LEFT);
    d->timer = 0;
}

void deinit_InputWidget(iInputWidget *d) {
    if (d->timer) {
        SDL_RemoveTimer(d->timer);
    }
    deinit_String(&d->hint);
    deinit_Array(&d->oldText);
    deinit_Array(&d->text);
}

void setMode_InputWidget(iInputWidget *d, enum iInputMode mode) {
    d->mode = mode;
}

void setSensitive_InputWidget(iInputWidget *d, iBool isSensitive) {
    d->isSensitive = isSensitive;
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

void setHint_InputWidget(iInputWidget *d, const iString *hintText) {
    set_String(&d->hint, hintText);
}

void setText_InputWidget(iInputWidget *d, const iString *text) {
    clear_Array(&d->text);
    iConstForEach(String, i, text) {
        pushBack_Array(&d->text, &i.value);
    }
    refresh_Widget(as_Widget(d));
}

void setTextCStr_InputWidget(iInputWidget *d, const char *cstr) {
    iString *str = newCStr_String(cstr);
    setText_InputWidget(d, str);
    delete_String(str);
}

void setCursor_InputWidget(iInputWidget *d, size_t pos) {
    d->cursor = iMin(pos, size_Array(&d->text));
}

static uint32_t refreshTimer_(uint32_t interval, void *d) {
    refresh_Widget(d);
    return interval;
}

void begin_InputWidget(iInputWidget *d) {
    iWidget *w = as_Widget(d);
    if (flags_Widget(w) & selected_WidgetFlag) {
        /* Already active. */
        return;
    }
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
    refresh_Widget(w);
    d->timer = SDL_AddTimer(REFRESH_INTERVAL, refreshTimer_, d);
    d->enterPressed = iFalse;
}

void end_InputWidget(iInputWidget *d, iBool accept) {
    iWidget *w = as_Widget(d);
    if (~flags_Widget(w) & selected_WidgetFlag) {
        /* Was not active. */
        return;
    }
    if (!accept) {
        setCopy_Array(&d->text, &d->oldText);
    }
    SDL_RemoveTimer(d->timer);
    d->timer = 0;
    SDL_StopTextInput();
    setFlags_Widget(w, selected_WidgetFlag, iFalse);
    const char *id = cstr_String(id_Widget(as_Widget(d)));
    if (!*id) id = "_";
    refresh_Widget(w);
    postCommand_Widget(
        w, "input.ended id:%s enter:%d arg:%d", id, d->enterPressed ? 1 : 0, accept ? 1 : 0);
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
    refresh_Widget(as_Widget(d));
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
    switch (processEvent_Click(&d->click, ev)) {
        case none_ClickResult:
            break;
        case started_ClickResult:
        case drag_ClickResult:
        case double_ClickResult:
        case aborted_ClickResult:
            return iTrue;
        case finished_ClickResult:
            setFocus_Widget(as_Widget(d));
            return iTrue;
    }
    if (ev->type == SDL_KEYUP && isFocused_Widget(w)) {
        return iTrue;
    }
    const size_t curMax = iMin(size_Array(&d->text), d->maxLen - 1);
    if (ev->type == SDL_KEYDOWN && isFocused_Widget(w)) {
        const int key  = ev->key.keysym.sym;
        const int mods = keyMods_Sym(ev->key.keysym.mod);
        if (mods == KMOD_PRIMARY) {
            switch (key) {
                case 'v':
                    if (SDL_HasClipboardText()) {
                        char *text = SDL_GetClipboardText();
                        iString *paste = collect_String(newCStr_String(text));
                        SDL_free(text);
                        iConstForEach(String, i, paste) {
                            insertChar_InputWidget_(d, i.value);
                        }
                    }
                    return iTrue;
            }
        }
        switch (key) {
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                d->enterPressed = iTrue;
                setFocus_Widget(NULL);
                return iTrue;
            case SDLK_ESCAPE:
                end_InputWidget(d, iFalse);
                setFocus_Widget(NULL);
                return iTrue;
            case SDLK_BACKSPACE:
                if (mods & KMOD_ALT) {
                    clear_Array(&d->text);
                    d->cursor = 0;
                }
                else if (d->cursor > 0) {
                    remove_Array(&d->text, --d->cursor);
                }
                refresh_Widget(w);
                return iTrue;
            case SDLK_d:
                if (mods != KMOD_CTRL) break;
            case SDLK_DELETE:
                if (d->cursor < size_Array(&d->text)) {
                    remove_Array(&d->text, d->cursor);
                    refresh_Widget(w);
                }
                return iTrue;
            case SDLK_k:
                if (mods == KMOD_CTRL) {
                    removeN_Array(&d->text, d->cursor, size_Array(&d->text) - d->cursor);
                    refresh_Widget(w);
                    return iTrue;
                }
                break;
            case SDLK_HOME:
            case SDLK_END:
                d->cursor = (key == SDLK_HOME ? 0 : curMax);
                refresh_Widget(w);
                return iTrue;
            case SDLK_a:
            case SDLK_e:
                if (mods == KMOD_CTRL) {
                    d->cursor = (key == 'a' ? 0 : curMax);
                    refresh_Widget(w);
                    return iTrue;
                }
                break;
            case SDLK_LEFT:
                if (mods & KMOD_PRIMARY) {
                    d->cursor = 0;
                }
                else if (d->cursor > 0) {
                    d->cursor--;
                }
                refresh_Widget(w);
                return iTrue;
            case SDLK_RIGHT:
                if (mods & KMOD_PRIMARY) {
                    d->cursor = curMax;
                }
                else if (d->cursor < curMax) {
                    d->cursor++;
                }
                refresh_Widget(w);
                return iTrue;
            case SDLK_TAB:
                /* Allow focus switching. */
                return processEvent_Widget(as_Widget(d), ev);
        }
        if (mods & (KMOD_PRIMARY | KMOD_SECONDARY)) {
            return iFalse;
        }
        return iTrue;
    }
    else if (ev->type == SDL_TEXTINPUT && isFocused_Widget(w)) {
        const iString *uni = collectNewCStr_String(ev->text.text);
        iConstForEach(String, i, uni) {
            insertChar_InputWidget_(d, i.value);
        }
        return iTrue;
    }
    return processEvent_Widget(w, ev);
}

static const iChar sensitiveChar_ = 0x25cf; /* black circle */

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
    const uint32_t time      = frameTime_Window(get_Window());
    const iInt2    padding   = init_I2(gap_UI / 2, gap_UI / 2);
    iRect          bounds    = adjusted_Rect(bounds_Widget(w), padding, neg_I2(padding));
    const iBool    isFocused = isFocused_Widget(w);
    const iBool    isHover   = isHover_Widget(w) &&
                               contains_Widget(w, mouseCoord_Window(get_Window()));
    iBool          isHint    = iFalse;
    iPaint p;
    init_Paint(&p);
    iString text;
    if (!d->isSensitive) {
        initUnicodeN_String(&text, constData_Array(&d->text), size_Array(&d->text));
    }
    else {
        init_String(&text);
        for (size_t i = 0; i < size_Array(&d->text); ++i) {
            appendChar_String(&text, sensitiveChar_);
        }
    }
    if (isWhite_(&text) && !isEmpty_String(&d->hint)) {
        set_String(&text, &d->hint);
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
    shrink_Rect(&bounds, init_I2(gap_UI * (flags_Widget(w) & tight_WidgetFlag ? 1 : 2), 0));
    const iInt2 emSize    = advance_Text(d->font, "M");
    const int   textWidth = advance_Text(d->font, cstr_String(&text)).x;
    const int   cursorX   = advanceN_Text(d->font, cstr_String(&text), d->cursor).x;
    int         xOff      = 0;
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
    draw_Text(d->font,
              add_I2(topLeft_Rect(bounds), init_I2(xOff, yOff)),
              isHint ? uiAnnotation_ColorId
                     : isFocused && !isEmpty_Array(&d->text) ? uiInputTextFocused_ColorId
                                                             : uiInputText_ColorId,
              "%s",
              cstr_String(&text));
    unsetClip_Paint(&p);
    /* Cursor blinking. */
    if (isFocused && (time & 256)) {
        const iInt2 prefixSize = advanceN_Text(d->font, cstr_String(&text), d->cursor);
        const iInt2 curPos = init_I2(xOff + left_Rect(bounds) + prefixSize.x,
                                     yOff + top_Rect(bounds));
        const iRect curRect = { curPos, addX_I2(emSize, 1) };
        iString     cur;
        if (d->cursor < size_Array(&d->text)) {
            if (!d->isSensitive) {
                initUnicodeN_String(&cur, constAt_Array(&d->text, d->cursor), 1);
            }
            else {
                initUnicodeN_String(&cur, &sensitiveChar_, 1);
            }
        }
        else {
            initCStr_String(&cur, " ");
        }
        fillRect_Paint(&p, curRect, uiInputCursor_ColorId);
        draw_Text(d->font, curPos, uiInputCursorText_ColorId, cstr_String(&cur));
        deinit_String(&cur);
    }
    deinit_String(&text);
}

iBeginDefineSubclass(InputWidget, Widget)
    .processEvent = (iAny *) processEvent_InputWidget_,
    .draw         = (iAny *) draw_InputWidget_,
iEndDefineSubclass(InputWidget)
