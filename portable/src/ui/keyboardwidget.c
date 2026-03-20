/* Copyright 2025 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "keyboardwidget.h"

#include "app.h"
#include "command.h"
#include "gamepad.h"
#include "inputwidget.h"
#include "paint.h"
#include "root.h"
#include "text.h"
#include "window.h"

#include <SDL_timer.h>
#include <the_Foundation/stringarray.h>
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>

static int initialRepeatDelayMs_ = 350;
static int repeatDelayMs_        = 100;

iDeclareType(KeyPage);
iDeclareType(KeyRow);
iDeclareType(Key);

enum iKeyFlags {
    pressed_KeyFlag     = iBit(1),
    invert_KeyFlag      = iBit(2),
    expand_KeyFlag      = iBit(3),
    spacer_KeyFlag      = iBit(4), /* half-wide */
    oneHalfWide_KeyFlag = iBit(5), /* one-and-half-wide */
    doubleWide_KeyFlag  = iBit(6), /* double-wide */
    bigFont_KeyFlag     = iBit(7),
};

struct Impl_Key {
    int      flags;
    iChar    keySym;
    iString *label;
    int      pageId;
    iRect    rect; /* relative to widget */
};

struct Impl_KeyRow {
    iArray keys;
};

struct Impl_KeyPage {
    int    id;
    int    height;
    size_t maxRowKeys;
    int    onKeyPageId; /* after press, switch page */
    iArray rows;
};

static void init_Key(iKey *d, int flags) {
    iZap(*d);
    d->flags  = flags;
    d->pageId = -1;
}

static void deinit_Key(iKey *d) {
    delete_String(d->label);
}


static void init_KeyRow(iKeyRow *d) {
    init_Array(&d->keys, sizeof(iKey));
}

static void deinit_KeyRow(iKeyRow *d) {
    iForEach(Array, i, &d->keys) {
        deinit_Key(i.value);
    }
    deinit_Array(&d->keys);
}

static void init_KeyPage(iKeyPage *d) {
    init_Array(&d->rows, sizeof(iKeyRow));
    d->height      = 0;
    d->maxRowKeys  = 0;
    d->onKeyPageId = -1;
}

static void deinit_KeyPage(iKeyPage *d) {
    iForEach(Array, i, &d->rows) {
        deinit_KeyRow(i.value);
    }
    deinit_Array(&d->rows);
}

static iKey *find_KeyPage_(iKeyPage *d, int sym) {
    iForEach(Array, r, &d->rows) {
        iKeyRow *row = r.value;
        iForEach(Array, k, &row->keys) {
            iKey *key = k.value;
            if (key->keySym == sym) return key;
        }
    }
    return NULL;
}

static const iKeyRow *row_KeyPage_(const iKeyPage *d, const iKey *key) {
    iConstForEach(Array, r, &d->rows) {
        const iKeyRow *row = r.value;
        iConstForEach(Array, k, &row->keys) {
            if (key == k.value) return row;
        }
    }
    return NULL;
}

/*-----------------------------------------------------------------------------------------------*/

struct Impl_KeyboardWidget {
    iWidget      widget;
    iKeyPage    *visPage;
    const iKey  *pressedKey;
    const iKey  *hoverKey;
    SDL_TimerID  repeatTimer;
    SDL_Event    repeatEvent;
    int          height; /* depends on screen dimensions, key layout */
    iArray       pages;
    iStringArray *availableConfigs; /* pairs of "id: name" */
    iString      currentConfigName;
    iString      currentConfigLabel;
    iBool        needLayout;
    iBool        repeatWasTriggered;
    enum iFontId font;
    enum iFontId bigFont;
};

iDefineObjectConstruction(KeyboardWidget);

static const char *defaultKeyboardConfig_ =
    "@us-english: U.S. English\n"

    "page: lowercase\n"
    "row: 1 2 3 4 5 6 7 8 9 0\n"
    "row: q w e r t y u i o p\n"
    "row: a s d f g h j k l\n"
    "row: {+@uppercase " shift_Icon "} z x c v b n m {+0x8 " delete_Icon "}\n"
    "row: {++@symbols #+=} {@emoji \U0001f642} {-0x20} {++0xd " return_Icon "}\n"

    "page: uppercase\n"
    "row: ! \" # $ % ^ & * ( )\n"
    "row: Q W E R T Y U I O P\n"
    "row: A S D F G H J K L\n"
    "row: {!+@lowercase " shift_Icon "} Z X C V B N M {+0x8 " delete_Icon "}\n"
    "row: {++@symbols #+=} {@emoji \U0001f642} {-0x20} {++0xd " return_Icon "}\n"
    "onkey: lowercase\n"

    "page: symbols\n"
    "row: [ ] # + % ^ = * {{ }\n"
    "row: - / : ; ( ) $ & @ •\n"
    "row: _ \\ | ~ < > € £ ¥\n"
    "row: {-} . , ? ! ' ` \" {+0x8 " delete_Icon "}\n"
    "row: {++@lowercase abc} {-0x20} {++0xd " return_Icon "}\n"

    "page: emoji\n"
    u8"row: 🙂 😊 😃 😂 😅 🙁 {0x8 " delete_Icon "}\n"
    "row: {++@lowercase abc} {-0x20} {++0xd " return_Icon "}\n";

static uint32_t keyRepeater_KeyboardWidget_(uint32_t interval, void *param) {
    iKeyboardWidget *w = param;
    SDL_PushEvent((SDL_Event *) &w->repeatEvent);
    return repeatDelayMs_;
}

static void stopRepeat_KeyboardWidget_(iKeyboardWidget *d) {
    if (d->repeatTimer) {
        SDL_RemoveTimer(d->repeatTimer);
        d->repeatTimer = 0;
    }
}

static void startRepeat_KeyboardWidget_(iKeyboardWidget *d, const SDL_Event *initialEvent) {
    d->repeatEvent = *initialEvent;
    d->repeatEvent.button.x /= window_Widget(d)->pixelRatio;
    d->repeatEvent.button.y /= window_Widget(d)->pixelRatio;
    d->repeatWasTriggered = iFalse;
    stopRepeat_KeyboardWidget_(d);
    d->repeatTimer = SDL_AddTimer(initialRepeatDelayMs_, keyRepeater_KeyboardWidget_, d);
}

static void clear_KeyboardWidget_(iKeyboardWidget *d) {
    iForEach(Array, i, &d->pages) {
        deinit_KeyPage(i.value);
    }
    clear_Array(&d->pages);
    d->visPage    = NULL;
    d->pressedKey = NULL;
    d->hoverKey   = NULL;
}

static int pageId_(iStringArray *names, iRangecc name) {
    iConstForEach(StringArray, i, names) {
        if (equalRange_Rangecc(name, range_String(i.value))) {
            return index_StringArrayConstIterator(&i);
        }
    }
    pushBackRange_StringArray(names, name);
    return (int) size_StringArray(names) - 1;
}

static void doLayout_KeyboardWidget(iKeyboardWidget *d) {
    const int maxHeight  = size_Root(as_Widget(d)->root).y / 2;
    const int maxRows    = 5;
    const int pageWidth  = width_Widget(d);
    const int pageHeight = maxHeight;
    iForEach(Array, i, &d->pages) {
        iKeyPage *page      = i.value;
        const int keyWidth  = pageWidth / (int) page->maxRowKeys;
        const int keyHeight = pageHeight / (int) size_Array(&page->rows);
        int       y         = 0;
        iForEach(Array, j, &page->rows) {
            iKeyRow *row = j.value;
            /* Determine key widths and the total width of the row. */
            int rowWidth     = 0;
            int numExpanding = 0;
            iForEach(Array, j, &row->keys) {
                iKey *key = j.value;
                if (key->label && length_String(key->label) == 1) {
                    key->flags |= bigFont_KeyFlag;
                }
                key->rect.size.y  = keyHeight;
                int         width = 0;
                const float mul   = (key->flags & doubleWide_KeyFlag    ? 2.0f
                                     : key->flags & oneHalfWide_KeyFlag ? 1.5f
                                                                        : 1.0f);
                if (key->flags & expand_KeyFlag) {
                    numExpanding++;
                }
                else if (key->flags & spacer_KeyFlag) {
                    width = keyWidth / 2;
                }
                else if (key->label) {
                    const int labelWidth =
                        measureRange_Text(d->font, range_String(key->label)).advance.x;
                    width = iMax(labelWidth, (int) (mul * keyWidth));
                }
                else {
                    width = (int) (mul * keyWidth);
                }
                key->rect.size.x = width;
                rowWidth += width;
            }
            rowWidth = iMin(rowWidth, pageWidth);
            if (numExpanding) {
                iForEach(Array, k, &row->keys) {
                    iKey *key = k.value;
                    if (key->flags & expand_KeyFlag) {
                        key->rect.size.x = (pageWidth - rowWidth) / numExpanding;
                    }
                }
                rowWidth = pageWidth;
            }
            /* Position the keys sequentially. */
            int x = (pageWidth - rowWidth) / 2;
            for (init_ArrayIterator(&j, &row->keys); j.value; next_ArrayIterator(&j)) {
                iKey *key     = j.value;
                key->rect.pos = init_I2(x, y);
                x += width_Rect(key->rect);
            }
            y += keyHeight;
        }
        page->height = y;
    }
    d->needLayout = iFalse;
}

static iBool parseConfig_KeyboardWidget_(iKeyboardWidget *d, const char *source,
                                         const iString *name) {
    iStringArray *pageNames = new_StringArray();
    iRangecc      lineSeg   = iNullRange;
    iKeyPage     *loadPage  = NULL;
    iBool         skipping  = iTrue;
    iBool         wasLoaded = iFalse;
    while (nextSplit_Rangecc(range_CStr(source), "\n", &lineSeg)) {
        iRangecc line = lineSeg;
        trim_Rangecc(&line);
        if (isEmpty_Range(&line) || line.start[0] == '#') {
            continue;
        }
        if (line.start[0] == '@') {
            /* Starts of a new layout. */
            skipping = iTrue;
            loadPage = NULL;
            const char *sep = strchr(line.start + 1, ':');
            if (contains_Range(&line, sep)) {
                const iRangecc confName  = { line.start + 1, sep };
                iRangecc       confLabel = { sep + 1, line.end };
                if (name && !cmp_Rangecc(confName, cstr_String(name))) {
                    /* This is the one that was requested to be loaded. */
                    set_String(&d->currentConfigName, name);
                    trim_Rangecc(&confLabel);
                    setRange_String(&d->currentConfigLabel, confLabel);
                    clear_KeyboardWidget_(d);
                    skipping = iFalse;
                    wasLoaded = iTrue;
                    d->needLayout = iTrue;
                }
                else {
                    if (!name) {
                        pushBackRange_StringArray(d->availableConfigs,
                                                  (iRangecc) { line.start + 1, line.end });
                    }
                }
            }
            continue;
        }
        if (skipping) {
            continue;
        }
        if (startsWith_Rangecc(line, "page:")) {
            iRangecc name = { line.start + 5, line.end };
            trimStart_Rangecc(&name);
            const int pageId = pageId_(pageNames, name);
            while (pageId >= size_Array(&d->pages)) {
                iKeyPage pad;
                init_KeyPage(&pad);
                pushBack_Array(&d->pages, &pad);
            }
            loadPage = at_Array(&d->pages, pageId);
        }
        else if (loadPage && startsWith_Rangecc(line, "onkey:")) {
            iRangecc name = { line.start + 6, line.end };
            trim_Rangecc(&name);
            loadPage->onKeyPageId = pageId_(pageNames, name);
        }
        else if (loadPage && startsWith_Rangecc(line, "row:")) {
            size_t numRowKeys = 0; /* excluding spacers */
            /* Add a new row. */
            iKeyRow row;
            init_KeyRow(&row);
            line.start += 4;
            trimStart_Rangecc(&line);
            while (!isEmpty_Range(&line)) {
                char c = line.start[0];
                if (c == '{' && size_Range(&line) >= 2) {
                    if (line.start[1] == '{') {
                        line.start++; /* double brace is used as escape */
                    }
                    else {
                        /* Special reference or symbol. */
                        iRangecc symbol = { line.start + 1, line.start + 1 };
                        while (symbol.end < line.end && *symbol.end != '}') {
                            symbol.end++;
                        }
                        trim_Rangecc(&symbol);
                        if (isEmpty_Range(&symbol)) {
                            /* A simple spacer key. */
                            iKey key;
                            init_Key(&key, spacer_KeyFlag);
                            pushBack_Array(&row.keys, &key);
                        }
                        else {
                            int flags = 0;
                            for (; symbol.start < symbol.end; symbol.start++) {
                                if (*symbol.start == '!') {
                                    flags |= invert_KeyFlag;
                                }
                                else if (*symbol.start == '-') {
                                    flags |= expand_KeyFlag;
                                }
                                else if (*symbol.start == '+') {
                                    if (~flags & oneHalfWide_KeyFlag) {
                                        flags |= oneHalfWide_KeyFlag;
                                    }
                                    else {
                                        flags |= doubleWide_KeyFlag;
                                    }
                                    flags &= ~expand_KeyFlag;
                                }
                                else
                                    break;
                            }
                            iKey key;
                            init_Key(&key, flags);
                            if (startsWith_Rangecc(symbol, "0x")) {
                                /* Numeric character. */
                                char *end;
                                key.keySym   = strtoul(symbol.start + 2, &end, 16);
                                symbol.start = end;
                            }
                            else if (startsWith_Rangecc(symbol, "@")) {
                                /* Page reference. */
                                symbol.start++;
                                trim_Rangecc(&symbol);
                                iRangecc name = { symbol.start, symbol.start };
                                while (name.end < symbol.end && !isSpace_Char(*name.end)) {
                                    name.end++;
                                }
                                key.pageId   = pageId_(pageNames, name);
                                symbol.start = name.end;
                            }
                            trimStart_Rangecc(&symbol);
                            /* Code point is followed by the label string. */
                            if (!isEmpty_Range(&symbol)) {
                                key.label = newRange_String(symbol);
                            }
                            if (!key.label && !key.keySym) {
                                key.flags |= spacer_KeyFlag;
                            }
                            pushBack_Array(&row.keys, &key);
                            numRowKeys++;
                        }
                        line.start = symbol.end + 1;
                        trimStart_Rangecc(&line);
                        continue;
                    }
                }
                iRangecc label = { line.start, line.start };
                while (label.end < line.end && !isSpace_Char(*label.end)) {
                    label.end++;
                }
                iKey key;
                init_Key(&key, 0);
                iAssert(!isEmpty_Range(&label));
                // printf("%zu: label key: '%s'\n", numRowKeys, cstr_Rangecc(label));
                key.label = newRange_String(label);
                pushBack_Array(&row.keys, &key);
                numRowKeys++;
                /* Move onto the next label. */
                line.start = label.end;
                trimStart_Rangecc(&line);
            }
            pushBack_Array(&loadPage->rows, &row);
            loadPage->maxRowKeys = iMax(loadPage->maxRowKeys, numRowKeys);
        }
    }
    iRelease(pageNames);
    if (wasLoaded) {
        d->visPage = front_Array(&d->pages);
    }
    return iTrue;
}

static void updateHeight_KeyboardWidget_(iKeyboardWidget *d) {
    iWidget *w = as_Widget(d);
    iAssert(d->visPage);
    if (d->needLayout) {
        doLayout_KeyboardWidget(d);
    }
    d->height = d->visPage->height;
    setFixedSize_Widget(w, init_I2(-1, d->height));
    if (!isVisible_Widget(d)) {
        setVisualOffset_Widget(w, d->height, 0, 0);
    }
    arrange_Widget(w);
    refresh_Widget(w);
}

static void showOrHide_KeyboardWidget_(iKeyboardWidget *d, iBool show) {
    iWidget *w = &d->widget;
    if (show && !isVisible_Widget(d)) {
        setVisualOffset_Widget(w, 0, 400, easeOut_AnimFlag | softer_AnimFlag);
        setFlags_Widget(w, hidden_WidgetFlag, iFalse);
    }
    else if (!show && isVisible_Widget(d)) {
        setVisualOffset_Widget(w, d->height, 400, easeOut_AnimFlag | softer_AnimFlag);
        setFlags_Widget(w, hidden_WidgetFlag, iTrue);
    }
    setKeyboardHeight_MainWindow(as_MainWindow(window_Widget(w)), show ? d->height : 0);
}

static void draw_KeyboardWidget_(const iKeyboardWidget *d) {
    const iWidget *w      = &d->widget;
    const iRect    bounds = bounds_Widget(w);
    draw_Widget(w);
    iPaint p;
    init_Paint(&p);
    iConstForEach(Array, i, &d->visPage->rows) {
        const iKeyRow *row = i.value;
        iConstForEach(Array, k, &row->keys) {
            const iKey *key = k.value;
            const iRect rect =
                shrunk_Rect(moved_Rect(key->rect, bounds.pos), mulf_I2(gap2_UI, 0.75f));
            if (key->flags & spacer_KeyFlag) {
                continue;
            }
            const iBool isPressed = (key == d->pressedKey);
            const iBool isDown    = isPressed | ((key->flags & invert_KeyFlag) != 0);
            fillRect_Paint(&p,
                           rect,
                           isDown               ? uiBackgroundSelected_ColorId
                           : key == d->hoverKey ? uiBackgroundUnfocusedSelection_ColorId
                                                : uiBackground_ColorId);
            if (isDown) {
                fillRect_Paint(
                    &p, (iRect) { rect.pos, init_I2(width_Rect(rect), gap_UI) }, uiEmboss2_ColorId);
            }
            if (key == d->hoverKey) {
                drawRectThickness_Paint(
                    &p, expanded_Rect(rect, init1_I2(gap_UI)), gap_UI / 2, uiEmbossHover1_ColorId);
            }
            int c1 = uiEmboss1_ColorId, c2 = uiEmboss2_ColorId;
            if (isDown) iSwap(int, c1, c2);
            drawEmbossedFrame_Paint(&p, rect, c1, c2, iFalse, iFalse);
            if (key->label) {
                drawCenteredRange_Text(key->flags & bigFont_KeyFlag ? d->bigFont : d->font,
                                       moved_Rect(rect, init_I2(0, isDown ? gap_UI : 0)),
                                       iFalse,
                                       key->keySym || key->pageId >= 0 ? uiTextAction_ColorId
                                                                       : uiTextStrong_ColorId,
                                       range_String(key->label));
            }
        }
    }
}

static iKey *hitKey_KeyboardWidget_(const iKeyboardWidget *d, iInt2 coord /* local */) {
    iForEach(Array, i, &d->visPage->rows) {
        iKeyRow *row = i.value;
        iForEach(Array, k, &row->keys) {
            iKey *key = k.value;
            if (key->flags & spacer_KeyFlag) {
                continue;
            }
            if (coord.y >= bottom_Rect(key->rect)) {
                break; /* not on this row, but could be below */
            }
            if (coord.y < top_Rect(key->rect)) {
                return NULL; /* sequentially laid out, no point in checking further */
            }
            if (coord.x >= left_Rect(key->rect) && coord.x < right_Rect(key->rect)) {
                return key;
            }
        }
    }
    return NULL;
}

static iKeyRow *hitRow_KeyboardWidget_(const iKeyboardWidget *d, iInt2 coord /* local */) {
    iForEach(Array, i, &d->visPage->rows) {
        iKeyRow    *row   = i.value;
        const iKey *first = constData_Array(&row->keys);
        if (coord.y >= top_Rect(first->rect) && coord.y < bottom_Rect(first->rect)) {
            return row;
        }
    }
    return NULL;
}

static void setHover_KeyboardWidget_(iKeyboardWidget *d, iKey *key) {
    if (key != d->hoverKey) {
        d->hoverKey = key;
        refresh_Widget(d);
    }
}

static void trigger_KeywordWidget_(const iKeyboardWidget *d, const iKey *key) {
    const iWidget *w = constAs_Widget(d);
    if (key->keySym && key->keySym != SDLK_SPACE) {
        emulateKeyPress_Window(window_Widget(d), key->keySym, 0);
    }
    else if (key->label || key->keySym == SDLK_SPACE) {
        SDL_TextInputEvent input = {
            .type     = SDL_TEXTINPUT,
            .windowID = id_Window(window_Widget(d)),
        };
        if (key->label) {
            strcpy(input.text, cstr_String(key->label));
        }
        else {
            strcpy(input.text, " ");
        }
        SDL_PushEvent((SDL_Event *) &input);
    }
}

static iBool processEvent_KeyboardWidget_(iKeyboardWidget *d, const SDL_Event *event) {
    iWidget *w = as_Widget(d);
    if (isCommand_SDLEvent(event)) {
        const char *cmd = command_UserEvent(event);
        if (equal_Command(cmd, "focus.gained") || equal_Command(cmd, "focus.lost")) {
            showOrHide_KeyboardWidget(
                d, focus_Widget() && isInstance_Object(focus_Widget(), &Class_InputWidget));
            return iFalse;
        }
        else if (equal_Command(cmd, "keyboard.hide")) {
            showOrHide_KeyboardWidget(d, iFalse);
            return iTrue;
        }
    }
    else if ((event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP) &&
             event->button.button == SDL_BUTTON_RIGHT &&
             contains_Widget(w, pointerCoord_Gamepad(gamepad_App()))) {
        iKey *bspKey = find_KeyPage_(d->visPage, SDLK_BACKSPACE);
        if (!bspKey) return iTrue;
        if (event->type == SDL_MOUSEBUTTONDOWN && bspKey->flags & invert_KeyFlag) {
            emulateKeyPress_Window(window_Widget(d), SDLK_BACKSPACE, 0);
            d->repeatWasTriggered = iTrue;
        }
        else if (event->type == SDL_MOUSEBUTTONUP) {
            stopRepeat_KeyboardWidget_(d);
            if (!d->repeatWasTriggered) {
                emulateKeyPress_Window(window_Widget(d), SDLK_BACKSPACE, 0);
            }
        }
        if (bspKey) {
            if (~bspKey->flags & invert_KeyFlag) startRepeat_KeyboardWidget_(d, event);
            iChangeFlags(bspKey->flags, invert_KeyFlag, event->type == SDL_MOUSEBUTTONDOWN);
            refresh_Widget(d);
        }
        return iTrue;
    }
    else if ((event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP) &&
             event->button.button == SDL_BUTTON_LEFT) {
        const iInt2 relPos = sub_I2(mouseCoord_SDLEvent(event), w->rect.pos);
        const iKey *key    = hitKey_KeyboardWidget_(d, relPos);
        if (key && event->type == SDL_MOUSEBUTTONDOWN) {
            if (d->pressedKey != key) {
                d->pressedKey = key;
                if (key->pageId < 0) {
                    startRepeat_KeyboardWidget_(d, event);
                }
                refresh_Widget(d);
            }
            else {
                trigger_KeywordWidget_(d, d->pressedKey); /* repeating */
                d->repeatWasTriggered = iTrue;
            }
        }
        else if (d->pressedKey && event->type == SDL_MOUSEBUTTONUP) {
            stopRepeat_KeyboardWidget_(d);
            key = d->pressedKey;
            /* Automatic page switch after keypress. */
            if (d->visPage->onKeyPageId >= 0) {
                d->visPage  = at_Array(&d->pages, d->visPage->onKeyPageId);
                d->hoverKey = hitKey_KeyboardWidget_(d, relPos);
            }
            if (key->pageId >= 0) {
                d->visPage  = at_Array(&d->pages, key->pageId);
                d->hoverKey = hitKey_KeyboardWidget_(d, relPos);
            }
            else if (!d->repeatWasTriggered) {
                trigger_KeywordWidget_(d, key);
            }
            d->pressedKey = NULL;
            refresh_Widget(d);
        }
        return contains_Widget(w, mouseCoord_SDLEvent(event));
    }
    else if (event->type == SDL_MOUSEMOTION) {
        if (contains_Widget(w, mouseCoord_SDLEvent(event))) {
            setHover_KeyboardWidget_(
                d, hitKey_KeyboardWidget_(d, sub_I2(mouseCoord_SDLEvent(event), w->rect.pos)));
        }
        else {
            setHover_KeyboardWidget_(d, NULL);
        }
        return iTrue;
    }
    return processEvent_Widget(&d->widget, event);
}

static void parseAllConfigs_KeyboardWidget_(iKeyboardWidget *d, const iString *loadName) {
    parseConfig_KeyboardWidget_(d, defaultKeyboardConfig_, loadName);
    /* Look for other keyboard configs in the user directory. */
    iForEach(DirFileInfo, info, iClob(new_DirFileInfo(dataDir_App()))) {
        const iString *path = path_FileInfo(info.value);
        if (endsWith_String(path, ".lkb")) {
            iFile *f = new_File(path);
            if (open_File(f, readOnly_FileMode)) {
                iString *source = readString_File(f);
                parseConfig_KeyboardWidget_(d, cstr_String(source), loadName);
                delete_String(source);
            }
            iRelease(f);
        }
    }
}

static void findAllLayouts_KeyboardWidget_(iKeyboardWidget *d) {
    clear_StringArray(d->availableConfigs);
    parseAllConfigs_KeyboardWidget_(d, NULL);
}

static void loadConfig_KeyboardWidget_(iKeyboardWidget *d, const iString *name) {
    parseAllConfigs_KeyboardWidget_(d, name);
}

/*-----------------------------------------------------------------------------------------------*/

void init_KeyboardWidget(iKeyboardWidget *d) {
    iWidget *w = &d->widget;
    init_Widget(w);
    d->repeatTimer = 0;
    d->height      = 0;
    d->font        = uiContent_FontId;
    d->bigFont     = uiLabelLarge_FontId;
    d->needLayout  = iFalse;
    init_String(&d->currentConfigName);
    init_String(&d->currentConfigLabel);
    d->availableConfigs = new_StringArray();
    setBackgroundColor_Widget(w, uiBackgroundSidebar_ColorId);
    setFlags_Widget(w,
                    hidden_WidgetFlag | fixedHeight_WidgetFlag | resizeToParentWidth_WidgetFlag |
                        moveToParentBottomEdge_WidgetFlag | keepOnTop_WidgetFlag |
                        noFadeBackground_WidgetFlag | borderTop_WidgetFlag,
                    iTrue);
    w->flags2 |= mustStayOnTop_WidgetFlag2; /* really, keep it on top */
    init_Array(&d->pages, sizeof(iKeyPage));
    setDrawBufferEnabled_Widget(w, iTrue);
    findAllLayouts_KeyboardWidget_(d);
}

void deinit_KeyboardWidget(iKeyboardWidget *d) {
    iRelease(d->availableConfigs);
    deinit_String(&d->currentConfigLabel);
    deinit_String(&d->currentConfigName);
    if (d->repeatTimer) {
        SDL_RemoveTimer(d->repeatTimer);
    }
}

void showOrHide_KeyboardWidget(iKeyboardWidget *d, iBool show) {
    iWidget *w = &d->widget;
    if (show) {
        const iString *kbd = &prefs_App()->strings[keyboardLayout_PrefsString];
        if (cmpString_String(&d->currentConfigName, kbd)) {
            loadConfig_KeyboardWidget_(d, kbd);
        }
        updateHeight_KeyboardWidget_(d);
    }
    showOrHide_KeyboardWidget_(d, show);
}

void cyclePage_KeyboardWidget(iKeyboardWidget *d, int dir) {
    int index  = d->visPage - (const iKeyPage *) constData_Array(&d->pages);
    index      = iWrap(index + dir, 0, size_Array(&d->pages));
    d->visPage = at_Array(&d->pages, index);
    d->hoverKey =
        hitKey_KeyboardWidget_(d,
                               sub_I2(mouseCoord_Window(window_Widget(d), mouseId_Gamepad),
                                      topLeft_Rect(bounds_Widget(&d->widget))));
    d->pressedKey = NULL;
    refresh_Widget(d);
}

iRect keyRectAtX_KeyboardWidget(const iKeyboardWidget *d, int x, size_t rowIndex, int rowOffset) {
    const iWidget *w       = &d->widget;
    const iRect    bounds  = bounds_Widget(w);
    const size_t   numRows = size_Array(&d->visPage->rows);
    if (rowIndex == iInvalidPos) {
        const iInt2 relPos =
            sub_I2(mouseCoord_Window(window_Widget(w), mouseId_Gamepad), topLeft_Rect(bounds));
        const iKeyRow *pRow = hitRow_KeyboardWidget_(d, relPos);
        if (pRow) {
            rowIndex = pRow - (const iKeyRow *) constData_Array(&d->visPage->rows);
        }
        else {
            rowIndex = (relPos.y >= height_Rect(bounds) / 2 ? numRows - 1 : 0);
        }
    }
    rowIndex += rowOffset;
    if (rowIndex >= numRows) {
        return zero_Rect();
    }
    const iKeyRow *row       = constAt_Array(&d->visPage->rows, rowIndex);
    const iBool    isEvenRow = (rowIndex & 1) != 0;
    iConstForEach(Array, k, &row->keys) {
        const iKey *key = k.value;
        if (key->flags & spacer_KeyFlag) continue;
        const iBool isFirst = index_ArrayConstIterator(&k) == 0;
        const iBool isLast  = index_ArrayConstIterator(&k) == size_Array(&row->keys) - 1;
        const int   left    = isFirst ? 0 : left_Rect(key->rect);
        const int   right   = isLast ? width_Rect(bounds) : right_Rect(key->rect);
        /* Alternate boundaries on rows to avoid stepping sideways when moving up/down. */
        if ((isEvenRow && (x >= left && x < right) || (!isEvenRow && (x > left && x <= right)))) {
            return moved_Rect(key->rect, topLeft_Rect(bounds));
        }
    }
    /* No suitable key was there, so just use the row's Y coord at the requested X. */
    const iKey *first = constData_Array(&row->keys);
    return init_Rect(x, top_Rect(bounds) + mid_Rect(first->rect).y, 0, 0);
}

iBool moveHover_KeyboardWidget(iKeyboardWidget *d, enum iDirection dir) {
    if (!d->hoverKey) {
        return iFalse;
    }
    iWidget       *w        = &d->widget;
    const iKeyRow *hoverRow = row_KeyPage_(d->visPage, d->hoverKey);
    const iKey    *first    = constData_Array(&hoverRow->keys);
    const iKey    *last     = constEnd_Array(&hoverRow->keys);
    switch (dir) {
        case left_Direction:
            for (const iKey *k = d->hoverKey - 1; k >= first; k--) {
                if (~k->flags & spacer_KeyFlag) {
                    d->hoverKey = k;
                    break;
                }
            }
            break;
        case right_Direction:
            for (const iKey *k = d->hoverKey + 1; k < last; k++) {
                if (~k->flags & spacer_KeyFlag) {
                    d->hoverKey = k;
                    break;
                }
            }
            break;
        default:
            break;
    }
    movePointer_Gamepad(
        gamepad_App(),
        add_I2(addY_I2(mid_Rect(d->hoverKey->rect), height_Rect(d->hoverKey->rect) / 4),
               topLeft_Rect(bounds_Widget(w))),
        100);
    refresh_Widget(d);
    return iTrue;
}

iBeginDefineSubclass(KeyboardWidget, Widget).draw  = (iAny *) draw_KeyboardWidget_,
                                     .processEvent = (iAny *) processEvent_KeyboardWidget_,
                                     iEndDefineSubclass(KeyboardWidget)
