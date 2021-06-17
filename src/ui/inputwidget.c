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
    isUrl_InputWidgetFlag            = iBit(2), /* affected by decoding preference */
    enterPressed_InputWidgetFlag     = iBit(3),
    selectAllOnFocus_InputWidgetFlag = iBit(4),
    notifyEdits_InputWidgetFlag      = iBit(5),
    eatEscape_InputWidgetFlag        = iBit(6),
    isMarking_InputWidgetFlag        = iBit(7),
    markWords_InputWidgetFlag        = iBit(8),
    needUpdateBuffer_InputWidgetFlag = iBit(9),
    enterKeyEnabled_InputWidgetFlag  = iBit(10),
};

/*----------------------------------------------------------------------------------------------*/

iDeclareType(InputLine)
    
struct Impl_InputLine {
    size_t offset; /* character position from the beginning */
    size_t len;    /* length as characters */
    iString text; /* UTF-8 */
};

static void init_InputLine(iInputLine *d) {
    d->offset = 0;
    init_String(&d->text);
}

static void deinit_InputLine(iInputLine *d) {
    deinit_String(&d->text);
}

iDefineTypeConstruction(InputLine)
    
/*----------------------------------------------------------------------------------------------*/
    
struct Impl_InputWidget {
    iWidget         widget;
    enum iInputMode mode;
    int             inFlags;
    size_t          maxLen;
    size_t          maxLayoutLines;
    iArray          text;    /* iChar[] */
    iArray          oldText; /* iChar[] */
    iArray          lines;
    int             lastUpdateWidth;
    iString         hint;
    iString         srcHint;
    int             leftPadding;
    int             rightPadding;
    size_t          cursor; /* offset from beginning */
    size_t          lastCursor;
    size_t          cursorLine;
    int             verticalMoveX;
    iRanges         mark;
    iRanges         initialMark;
    iArray          undoStack;
    int             font;
    iClick          click;
    int             cursorVis;
    uint32_t        timer;
    iTextBuf *      buffered;
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
    const iWidget *w         = constAs_Widget(d);
    //    const iRect widgetBounds = bounds_Widget(w);
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

static void showCursor_InputWidget_(iInputWidget *d) {
    d->cursorVis = 2;
    updateCursorLine_InputWidget_(d);
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
            add_I2(measure_Text(d->font, cstr_Block(content)),
                   init_I2(6 * gap_UI + d->leftPadding + d->rightPadding,
                           2 * gap_UI + extraHeight)));
        delete_Block(content);
    }
}

static const iChar sensitiveChar_ = 0x25cf; /* black circle */

static iString *utf32toUtf8_InputWidget_(const iInputWidget *d) {
    return newUnicodeN_String(constData_Array(&d->text), size_Array(&d->text));
}

static iString *visText_InputWidget_(const iInputWidget *d) {
    iString *text;
    if (~d->inFlags & isSensitive_InputWidgetFlag) {
        text = utf32toUtf8_InputWidget_(d);
    }
    else {
        text = new_String();
        for (size_t i = 0; i < size_Array(&d->text); ++i) {
            appendChar_String(text, sensitiveChar_);
        }
    }
    return text;
}

static void clearLines_InputWidget_(iInputWidget *d) {
    iForEach(Array, i, &d->lines) {
        deinit_InputLine(i.value);
    }
    clear_Array(&d->lines);
}

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

static int contentHeight_InputWidget_(const iInputWidget *d, iBool forLayout) {
    size_t numLines = iMax(1, size_Array(&d->lines));
    if (forLayout) {
        numLines = iMin(numLines, d->maxLayoutLines);
    }
    return (int) numLines * lineHeight_Text(d->font);
}

static void updateMetrics_InputWidget_(iInputWidget *d) {
    iWidget *w = as_Widget(d);
    updateSizeForFixedLength_InputWidget_(d);
    /* Caller must arrange the width, but the height is fixed. */
    w->rect.size.y = contentHeight_InputWidget_(d, iTrue) + 3.0f * padding_().y; /* TODO: Why 3x? */
    if (flags_Widget(w) & extraPadding_WidgetFlag) {
        w->rect.size.y += extraPaddingHeight_;
    }
    invalidateBuffered_InputWidget_(d);
    postCommand_Widget(d, "input.resized");
}

static void updateLinesAndResize_InputWidget_(iInputWidget *d) {
    const size_t oldCount = size_Array(&d->lines);
    updateLines_InputWidget_(d);
    if (oldCount != size_Array(&d->lines)) {
        d->click.minHeight = contentHeight_InputWidget_(d, iFalse);
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
    init_Array(&d->text, sizeof(iChar));
    init_Array(&d->oldText, sizeof(iChar));
    init_Array(&d->lines, sizeof(iInputLine));
    init_String(&d->hint);
    init_String(&d->srcHint);
    init_Array(&d->undoStack, sizeof(iInputUndo));
    d->font         = uiInput_FontId | alwaysVariableFlag_FontId;
    d->leftPadding  = 0;
    d->rightPadding = 0;
    d->cursor       = 0;
    d->lastCursor   = 0;
    d->cursorLine   = 0;
    d->lastUpdateWidth = 0;
    d->verticalMoveX = -1; /* TODO: Use this. */
    d->inFlags      = eatEscape_InputWidgetFlag | enterKeyEnabled_InputWidgetFlag;
    iZap(d->mark);
    setMaxLen_InputWidget(d, maxLen);
    d->maxLayoutLines = iInvalidSize;
    setFlags_Widget(w, fixedHeight_WidgetFlag, iTrue);
    init_Click(&d->click, d, SDL_BUTTON_LEFT);
    d->timer = 0;
    d->cursorVis = 0;
    d->buffered = NULL;
    updateLines_InputWidget_(d);
    updateMetrics_InputWidget_(d);
}

void deinit_InputWidget(iInputWidget *d) {
    clearLines_InputWidget_(d);
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
    deinit_Array(&d->lines);
    deinit_Array(&d->oldText);
    deinit_Array(&d->text);
}

void setFont_InputWidget(iInputWidget *d, int fontId) {
    d->font = fontId;
    updateMetrics_InputWidget_(d);
}

static const iInputLine *line_InputWidget_(const iInputWidget *d, size_t index) {
    iAssert(!isEmpty_Array(&d->lines));
    return constAt_Array(&d->lines, index);
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
        iString *text = collect_String(utf32toUtf8_InputWidget_(d));
        if (d->inFlags & isUrl_InputWidgetFlag) {
            /* Add the "gemini" scheme back if one is omitted. */
            restoreDefaultScheme_(text);
        }
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

void setMaxLayoutLines_InputWidget(iInputWidget *d, size_t maxLayoutLines) {
    d->maxLayoutLines = maxLayoutLines;
    updateMetrics_InputWidget_(d);
}

void setValidator_InputWidget(iInputWidget *d, iInputWidgetValidatorFunc validator, void *context) {
    d->validator = validator;
    d->validatorContext = context;
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
    return !isEmpty_String(&d->hint) && size_Array(&d->lines) == 1 &&
           isEmpty_String(&line_InputWidget_(d, 0)->text);
}

static void updateBuffered_InputWidget_(iInputWidget *d) {
    invalidateBuffered_InputWidget_(d);
    if (isHintVisible_InputWidget_(d)) {
        d->buffered = new_TextBuf(d->font, uiAnnotation_ColorId, cstr_String(&d->hint));                
    }
    else {
        iString *bufText = NULL;
#if 0
        if (d->inFlags & isUrl_InputWidgetFlag && as_Widget(d)->root == win->keyRoot) {
            /* TODO: Move this omitting to `updateLines_`? */
            /* Highlight the host name. */
            iUrl parts;
            const iString *text = collect_String(utf32toUtf8_InputWidget_(d));
            init_Url(&parts, text);
            if (!isEmpty_Range(&parts.host)) {
                bufText = new_String();
                appendRange_String(bufText, (iRangecc){ constBegin_String(text), parts.host.start });
                appendCStr_String(bufText, uiTextStrong_ColorEscape);
                appendRange_String(bufText, parts.host);
                appendCStr_String(bufText, restore_ColorEscape);
                appendRange_String(bufText, (iRangecc){ parts.host.end, constEnd_String(text) });
            }
        }
#endif
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
    d->inFlags &= ~needUpdateBuffer_InputWidgetFlag;
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
        /* Omit the default (Gemini) scheme if there isn't much space. */
        if (isNarrow_Root(as_Widget(d)->root)) {
            text = omitDefaultScheme_(collect_String(copy_String(text)));
        }
    }
    clearUndo_InputWidget_(d);
    clear_Array(&d->text);
    iString *nfcText = collect_String(copy_String(text));
    normalize_String(nfcText);
    iConstForEach(String, i, nfcText) {
        pushBack_Array(&d->text, &i.value);
    }
    if (isFocused_Widget(d)) {
        d->cursor = size_Array(&d->text);
//        selectAll_InputWidget(d);
    }
    else {
        d->cursor = iMin(d->cursor, size_Array(&d->text));
        iZap(d->mark);
    }
    if (!isFocused_Widget(d)) {
        d->inFlags |= needUpdateBuffer_InputWidgetFlag;
    }
    updateLinesAndResize_InputWidget_(d);
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
    setCopy_Array(&d->oldText, &d->text);
    if (d->mode == overwrite_InputMode) {
        d->cursor = 0;
    }
    else {
        d->cursor = iMin(size_Array(&d->text), d->maxLen - 1);
    }
    updateCursorLine_InputWidget_(d);
    SDL_StartTextInput();
    setFlags_Widget(w, selected_WidgetFlag, iTrue);
    if (d->maxLayoutLines != iInvalidSize) {
        /* This will extend beyond the arranged region. */
        setFlags_Widget(w, keepOnTop_WidgetFlag, iTrue);
    }
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
    if (!isEditing_InputWidget_(d)) {
        /* Was not active. */
        return;
    }
    enableEditorKeysInMenus_(iTrue);
    if (!accept) {
        setCopy_Array(&d->text, &d->oldText);
    }
    d->inFlags |= needUpdateBuffer_InputWidgetFlag;
    SDL_RemoveTimer(d->timer);
    d->timer = 0;
    SDL_StopTextInput();
    setFlags_Widget(w, selected_WidgetFlag | keepOnTop_WidgetFlag, iFalse);
    const char *id = cstr_String(id_Widget(as_Widget(d)));
    if (!*id) id = "_";
    updateLinesAndResize_InputWidget_(d);
    refresh_Widget(w);
    postCommand_Widget(w,
                       "input.ended id:%s enter:%d arg:%d",
                       id,
                       d->inFlags & enterPressed_InputWidgetFlag ? 1 : 0,
                       accept ? 1 : 0);
}

static void insertChar_InputWidget_(iInputWidget *d, iChar chr) {
    iWidget *w = as_Widget(d);
    if (d->mode == insert_InputMode) {
        insert_Array(&d->text, d->cursor, &chr);
        d->cursor++;
    }
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
    showCursor_InputWidget_(d);
    refresh_Widget(as_Widget(d));
}

iLocalDef size_t cursorMax_InputWidget_(const iInputWidget *d) {
    return iMin(size_Array(&d->text), d->maxLen - 1);
}

iLocalDef iBool isMarking_(void) {
    return (modState_Keys() & KMOD_SHIFT) != 0;
}

void setCursor_InputWidget(iInputWidget *d, size_t pos) {
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
    showCursor_InputWidget_(d);
}

iLocalDef iBool isLastLine_InputWidget_(const iInputWidget *d, const iInputLine *line) {
    return (const void *) line == constAt_Array(&d->lines, size_Array(&d->lines) - 1);
}

static size_t indexForRelativeX_InputWidget_(const iInputWidget *d, int x, const iInputLine *line) {
    if (x <= 0) {
        return line->offset;
    }
    const char *endPos;
    tryAdvanceNoWrap_Text(d->font, range_String(&line->text), x, &endPos);
    size_t index = line->offset;
    if (endPos == constEnd_String(&line->text)) {
        index += line->len;
    }
    else {
        /* Need to know the actual character index. */
        /* TODO: tryAdvance could tell us this directly with an extra return value */
        iConstForEach(String, i, &line->text) {
            if (i.pos >= endPos) break;
            index++;
        }
    }
    if (!isLastLine_InputWidget_(d, line) && index == line->offset + line->len) {
        index = iMax(index - 1, line->offset);
    }
    return index;
}

static iBool moveCursorByLine_InputWidget_(iInputWidget *d, int dir) {
    const iInputLine *line = line_InputWidget_(d, d->cursorLine);
    int xPos = advanceN_Text(d->font, cstr_String(&line->text), d->cursor - line->offset).x;
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
    m.start   = iMin(m.start, size_Array(&d->text));
    m.end     = iMin(m.end, size_Array(&d->text));
    return m;
}

static void contentsWereChanged_InputWidget_(iInputWidget *d) {
    if (d->validator) {
        d->validator(d, d->validatorContext); /* this may change the contents */
    }
    updateLinesAndResize_InputWidget_(d);
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

#if 0
static iInt2 textOrigin_InputWidget_(const iInputWidget *d) { //}, const char *visText) {
//    const iWidget *w         = constAs_Widget(d);
    iRect          bounds    = contentBounds_InputWidget_(d);/* adjusted_Rect(bounds_Widget(w),
                                 addX_I2(padding_(), d->leftPadding),
                                 neg_I2(addX_I2(padding_(), d->rightPadding)));*/
//    const iInt2    emSize    = advance_Text(d->font, "M");
//    const int      textWidth = advance_Text(d->font, visText).x;
//    const int      cursorX   = advanceN_Text(d->font, visText, d->cursor).x;
//    int            xOff      = 0;
//    shrink_Rect(&bounds, init_I2(gap_UI * (flags_Widget(w) & tight_WidgetFlag ? 1 : 2), 0));
/*    if (d->maxLen == 0) {
        if (textWidth > width_Rect(bounds) - emSize.x) {
            xOff = width_Rect(bounds) - emSize.x - textWidth;
        }
        if (cursorX + xOff < width_Rect(bounds) / 2) {
            xOff = width_Rect(bounds) / 2 - cursorX;
        }
        xOff = iMin(xOff, 0);
    }*/
//    const int yOff = 0.3f * lineHeight_Text(d->font); // (height_Rect(bounds) - lineHeight_Text(d->font)) / 2;
//    return addY_I2(topLeft_Rect(bounds), yOff);
    
}
#endif

static size_t coordIndex_InputWidget_(const iInputWidget *d, iInt2 coord) {
    const iInt2 pos = sub_I2(coord, contentBounds_InputWidget_(d).pos);
    const size_t lineNumber = iMin(iMax(0, pos.y) / lineHeight_Text(d->font),
                                   (int) size_Array(&d->lines) - 1);
    const iInputLine *line = line_InputWidget_(d, lineNumber);
    const char *endPos;
    tryAdvanceNoWrap_Text(d->font, range_String(&line->text), pos.x, &endPos);
    return indexForRelativeX_InputWidget_(d, pos.x, line);
}

static iBool copy_InputWidget_(iInputWidget *d, iBool doCut) {
    if (!isEmpty_Range(&d->mark)) {
        const iRanges m = mark_InputWidget_(d);
        iString *str = collect_String(newUnicodeN_String(constAt_Array(&d->text, m.start),
                                                         size_Range(&m)));
        SDL_SetClipboardText(
            cstr_String(d->inFlags & isUrl_InputWidgetFlag ? withSpacesEncoded_String(str) : str));
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

static iChar at_InputWidget_(const iInputWidget *d, size_t pos) {
    return *(const iChar *) constAt_Array(&d->text, pos);
}

static iRanges lineRange_InputWidget_(const iInputWidget *d) {
    if (isEmpty_Array(&d->lines)) {
        return (iRanges){ 0, 0 };
    }
    const iInputLine *line = line_InputWidget_(d, d->cursorLine);
    return (iRanges){ line->offset, line->offset + line->len };
}

static void extendRange_InputWidget_(iInputWidget *d, size_t *pos, int dir) {
    const size_t textLen = size_Array(&d->text);
    if (dir < 0 && *pos > 0) {
        for ((*pos)--; *pos > 0; (*pos)--) {
            if (isSelectionBreaking_Char(at_InputWidget_(d, *pos))) {
                (*pos)++;
                break;
            }
        }
    }
    if (dir > 0) {
        for (; *pos < textLen && !isSelectionBreaking_Char(at_InputWidget_(d, *pos)); (*pos)++) {
            /* continue */
        }
    }
}

static iRect bounds_InputWidget_(const iInputWidget *d) {
    const iWidget *w = constAs_Widget(d);
    iRect bounds = bounds_Widget(w);
    if (!isFocused_Widget(d)) {
        return bounds;
    }
    bounds.size.y = contentHeight_InputWidget_(d, iFalse) + 3 * padding_().y;
    if (w->flags & extraPadding_WidgetFlag) {
        bounds.size.y += extraPaddingHeight_;
    }
    return bounds;
}

static iBool contains_InputWidget_(const iInputWidget *d, iInt2 coord) {
    return contains_Rect(bounds_InputWidget_(d), coord);
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
        updateLinesAndResize_InputWidget_(d);
    }
    else if (isResize_UserEvent(ev) || d->lastUpdateWidth != w->rect.size.x) {
        d->inFlags |= needUpdateBuffer_InputWidgetFlag;
        if (d->inFlags & isUrl_InputWidgetFlag) {
            /* Restore/omit the default scheme if necessary. */
            setText_InputWidget(d, text_InputWidget(d));
        }
        updateLinesAndResize_InputWidget_(d);
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
            const size_t oldCursor = d->cursor;
            setCursor_InputWidget(d, coordIndex_InputWidget_(d, pos_Click(&d->click)));
            if (keyMods_Sym(modState_Keys()) == KMOD_SHIFT) {
                d->mark = d->initialMark = (iRanges){ oldCursor, d->cursor };
                d->inFlags |= isMarking_InputWidgetFlag;
            }
            else {
                iZap(d->mark);
                iZap(d->initialMark);
                d->inFlags &= ~(isMarking_InputWidgetFlag | markWords_InputWidgetFlag);
                if (d->click.count == 2) {
                    d->inFlags |= isMarking_InputWidgetFlag | markWords_InputWidgetFlag;
                    d->mark.start = d->mark.end = d->cursor;
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
            d->cursor = coordIndex_InputWidget_(d, pos_Click(&d->click));
            showCursor_InputWidget_(d);
            if (~d->inFlags & isMarking_InputWidgetFlag) {
                d->inFlags |= isMarking_InputWidgetFlag;
                d->mark.start = d->cursor;
            }
            d->mark.end = d->cursor;
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
    const size_t  curMax    = cursorMax_InputWidget_(d);
    const iRanges lineRange = lineRange_InputWidget_(d);
    const size_t  lineFirst = lineRange.start;
    const size_t  lineLast  = lineRange.end == curMax ? curMax : iMax(lineRange.start, lineRange.end - 1);
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
                    setCursor_InputWidget(d, key == SDLK_UP ? 0 : curMax);
                    refresh_Widget(d);
                    return iTrue;
            }
        }
#endif        
        d->lastCursor = d->cursor;
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
                                           deviceType_App() != desktop_AppDeviceType)) {
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
                else if (d->cursor == 0 && d->maxLen == 1) {
                    pushUndo_InputWidget_(d);
                    clear_Array(&d->text);
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
                if (mods == KMOD_PRIMARY || mods == (KMOD_PRIMARY | KMOD_SHIFT)) {
                    setCursor_InputWidget(d, key == SDLK_HOME ? 0 : curMax);
                }
                else {
                    setCursor_InputWidget(d, key == SDLK_HOME ? lineFirst : lineLast);
                }
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
        const iString *uni = collectNewCStr_String(ev->text.text);
        iConstForEach(String, i, uni) {
            insertChar_InputWidget_(d, i.value);
        }
        contentsWereChanged_InputWidget_(d);
        return iTrue;
    }
    return processEvent_Widget(w, ev);
}

#if 0
static iBool isWhite_(const iString *str) {
    iConstForEach(String, i, str) {
        if (!isSpace_Char(i.value)) {
            return iFalse;
        }
    }
    return iTrue;
}
#endif

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
    const int   fg         = isHint                                  ? uiAnnotation_ColorId
                             : isFocused && !isEmpty_Array(&d->text) ? uiInputTextFocused_ColorId
                                                                     : uiInputText_ColorId;
    /* If buffered, just draw the buffered copy. */
    if (d->buffered && !isFocused) { //&& !isFocused/* && !isHint*/) {
        /* Most input widgets will use this, since only one is focused at a time. */
        draw_TextBuf(d->buffered, topLeft_Rect(contentBounds), white_ColorId);
    }
    else if (isHint) {
        drawRange_Text(d->font, topLeft_Rect(contentBounds), uiAnnotation_ColorId,
                       range_String(&d->hint));
    }
    else {
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
                    const int m1 = advanceN_Text(d->font,
                                                 cstr_String(&line->text),
                                                 iMax(lineRange.start, mark.start) - line->offset)
                                       .x;
                    const int m2 = advanceN_Text(d->font,
                                                 cstr_String(&line->text),
                                                 iMin(lineRange.end, mark.end) - line->offset)
                                       .x;
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
    }
    unsetClip_Paint(&p);
    /* Cursor blinking. */
    if (isFocused && d->cursorVis) {
        iString cur;
        iInt2 curSize;
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
            curSize = addX_I2(advance_Text(d->font, cstr_String(&cur)), iMin(2, gap_UI / 4));
        }
        else {
            /* Bar cursor. */
            curSize = init_I2(gap_UI / 2, lineHeight_Text(d->font));
        }
        const iInputLine *curLine = line_InputWidget_(d, d->cursorLine);
        const iString *   text    = &curLine->text;
        /* The `gap_UI` offsets below are a hack. They are used because for some reason the
           cursor rect and the glyph inside don't quite position like during `run_Text_()`. */
        const iInt2 prefixSize = advanceN_Text(d->font, cstr_String(text), d->cursor - curLine->offset);
        const iInt2 curPos     = addX_I2(addY_I2(contentBounds.pos, lineHeight_Text(d->font) * d->cursorLine),
                                         prefixSize.x +
                                         (d->mode == insert_InputMode ? -curSize.x / 2 : 0));
        const iRect curRect    = { curPos, curSize };
        fillRect_Paint(&p, curRect, uiInputCursor_ColorId);
        if (d->mode == overwrite_InputMode) {
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
