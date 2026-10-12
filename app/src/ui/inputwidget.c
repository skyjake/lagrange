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

/* InputWidget supports both fully custom and system-provided text editing. This is the
   common core; the editing variants are under "input/". */

#include "input/impl.h"

#include "app.h"
#include "command.h"
#include "keys.h"
#include <lagrange/lang.h>
#include <lagrange/prefs.h>
#include <lagrange/snippets.h>
#include "window.h"
#include "../gmutil.h"

#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <the_Foundation/regexp.h>
#include <SDL3/SDL_timer.h>

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
        iString *text = newText_InputWidget_(d);
        write_File(f, utf8_String(text));
        delete_String(text);
        d->inFlags &= ~needBackup_InputWidgetFlag;
    }
    iRelease(f);
}

static void eraseBackup_InputWidget_(iInputWidget *d) {
    if (d->backupPath) {
        removePath_CStr(cstr_String(d->backupPath));
        delete_String(d->backupPath);
        d->backupPath = NULL;
    }
}

static uint32_t backupTimeout_InputWidget_(void *context, SDL_TimerID timerID, uint32_t interval) {
    iUnused(timerID, interval);
    iInputWidget *d = context;
    notify_Widget(d, "input.backup");
    return 0; /* does not repeat */
}

void restartBackupTimer_InputWidget_(iInputWidget *d) {
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
    /* Use the window's serial number instead of index so this is unaffected by focus changes. */
    appendFormat_String(d->backupPath, ".win%u.txt", serial_Window(as_Widget(d)->root->window));
    restoreBackup_InputWidget_(d);
}

iRect contentBounds_InputWidget_(const iInputWidget *d) {
    const iWidget *w      = constAs_Widget(d);
    iRect          bounds = adjusted_Rect(bounds_Widget(w),
                                 addX_I2(padding_InputWidget_(), d->leftPadding),
                                 neg_I2(addX_I2(padding_InputWidget_(), d->rightPadding)));
    shrink_Rect(&bounds, init_I2(gap_UI * (flags_Widget(w) & tight_WidgetFlag ? 1 : 2) * aspect_UI, 0));
    bounds.pos.y += padding_InputWidget_().y / 2;
    if (flags_Widget(w) & extraPadding_WidgetFlag) {
        if (hasNativeControl_InputWidget_(d) && !cmp_String(id_Widget(w), "url")) {
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

iWrapText wrapParams_InputWidget_(const iInputWidget *d) {
    return (iWrapText){
        .maxWidth = d->maxLen == 0 ? iMaxi(minWidth_InputWidget_,
                                           width_Rect(contentBounds_InputWidget_(d)))
                                   : unlimitedWidth_InputWidget_,
        .mode =
            (d->inFlags & isUrl_InputWidgetFlag ? anyCharacter_WrapTextMode : word_WrapTextMode),
        .overrideChar = (d->inFlags & isSensitive_InputWidgetFlag ? sensitiveChar_InputWidget_ : 0),
    };
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
        int extraHeight = (flags_Widget(d) & extraPadding_WidgetFlag ? extraPaddingHeight_InputWidget_(d) : 0);
        setFixedSize_Widget(
            as_Widget(d),
            add_I2(measure_Text(d->font, cstr_Block(content)).bounds.size,
                   init_I2(6 * gap_UI + d->leftPadding + d->rightPadding,
                           2 * gap_UI + extraHeight)));
        delete_Block(content);
    }
}

void updateMetrics_InputWidget_(iInputWidget *d) {
    iWidget *w = as_Widget(d);
    updateSizeForFixedLength_InputWidget_(d);
    /* Caller must arrange the width, but the height is set here. */
    if (!isHeightKnown_InputWidget_(d)) {
        return;
    }
    const int oldHeight = height_Rect(w->rect);
    w->rect.size.y = contentHeight_InputWidget_(d) + 3 * padding_InputWidget_().y; /* TODO: Why 3x? */
    if (flags_Widget(w) & extraPadding_WidgetFlag) {
        w->rect.size.y += extraPaddingHeight_InputWidget_(d);
    }
    invalidateBuffered_InputWidget_(d);
    d->inFlags |= needUpdateBuffer_InputWidgetFlag;
    if (height_Rect(w->rect) != oldHeight) {
        notify_Widget(d, "input.resized arg:%d", w->root->pendingArrange + 1);
        updateTextInputRect_InputWidget_(d);
        if (height_Rect(w->rect) > oldHeight && isFocused_Widget(w)) {
            /* Keep the growing bottom edge visible after the rearrangement. */
            postCommand_Widget(w, "input.overflow");
        }
    }
}

void init_InputWidget(iInputWidget *d, size_t maxLen) {
    iWidget *w = &d->widget;
    init_Widget(w);
    w->bgColor = uiInputBackground_ColorId;
    d->validator = NULL;
    d->validatorContext = NULL;
    d->highlighter = NULL;
    d->highlighterContext = NULL;
    setFlags_Widget(w, focusable_WidgetFlag | hover_WidgetFlag, iTrue);
    setFlags_Widget(w, extraPadding_WidgetFlag, isMobile_Platform());
    initVariant_InputWidget_(d);
    init_String(&d->oldText);
    init_String(&d->srcHint);
    init_String(&d->hint);
    d->font            = uiInput_FontId | alwaysVariableFlag_FontId;
    d->leftPadding     = 0;
    d->rightPadding    = 0;
    d->lastUpdateWidth = 0;
    d->inFlags         = eatEscape_InputWidgetFlag | enterKeyEnabled_InputWidgetFlag |
                         lineBreaksEnabled_InputWidgetFlag | useReturnKeyBehavior_InputWidgetFlag |
                         arrowFocusNavigable_InputWidgetFlag;
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
    deinitVariant_InputWidget_(d);
}

iBool isAllowedToInsertNewline_InputWidget_(const iInputWidget *d) {
    return ~d->inFlags & isSensitive_InputWidgetFlag &&
        ~d->inFlags & isUrl_InputWidgetFlag &&
        d->inFlags & lineBreaksEnabled_InputWidgetFlag && d->maxLen == 0;
}

void setFont_InputWidget(iInputWidget *d, int fontId) {
    d->font = fontId;
    d->lastUpdateWidth = 0; /* force a rewrapping */
    updateMetrics_InputWidget_(d);
}

void setMode_InputWidget(iInputWidget *d, enum iInputMode mode) {
    d->mode = mode;
}

static const iString *omitDefaultScheme_(iString *url) {
    if (startsWithCase_String(url, "gemini://")) {
        remove_Block(&url->chars, 0, 7); /* leaving // */
    }
    return url;
}

const iString *text_InputWidget(const iInputWidget *d) {
    iString *text = collect_String(d ? newText_InputWidget_(d) : new_String());
    if (d && d->inFlags & isUrl_InputWidgetFlag) {
        /* Check for `hostname:port` pattern and fit it so it'll be parsed correctly. */
        static iRegExp *simpleHost;
        if (!simpleHost) {
            simpleHost = new_RegExp("^[\\w.-]+:\\d{1,5}$", caseInsensitive_RegExpOption);
        }
        iRegExpMatch m;
        init_RegExpMatch(&m);
        if (matchString_RegExp(simpleHost, text, &m)) {
            prependCStr_String(text, "gemini://");
        }
    }
    return text;
}

const iString *rawText_InputWidget(const iInputWidget *d) {
    return collect_String(d ? newText_InputWidget_(d) : new_String());
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

void setHighlighter_InputWidget(iInputWidget *d, iInputWidgetHighlighterFunc highlighter,
                                void *context) {
    d->highlighter = highlighter;
    d->highlighterContext = context;
    invalidateBuffered_InputWidget_(d);
}

void setLineBreaksEnabled_InputWidget(iInputWidget *d, iBool lineBreaksEnabled) {
    iChangeFlags(d->inFlags, lineBreaksEnabled_InputWidgetFlag, lineBreaksEnabled);
}

void setArrowFocusNavigable_InputWidget(iInputWidget *d, iBool arrowFocusNavigable) {
    iChangeFlags(d->inFlags, arrowFocusNavigable_InputWidgetFlag, arrowFocusNavigable);
}

void setEnterKeyEnabled_InputWidget(iInputWidget *d, iBool enterKeyEnabled) {
    iChangeFlags(d->inFlags, enterKeyEnabled_InputWidgetFlag, enterKeyEnabled);
}

void setOmitDefaultSchemeIfNarrow_InputWidget(iInputWidget *d, iBool omitDefaultSchemeIfNarrow) {
    iChangeFlags(d->inFlags, omitDefaultSchemeIfNarrow_InputWidgetFlag, omitDefaultSchemeIfNarrow);
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
        iString *visText = newVisibleText_InputWidget_(d);
        if (d->inFlags & isUrl_InputWidgetFlag) {
            if (d->inFlags & omitDefaultSchemeIfNarrow_InputWidgetFlag) {
                if (measure_Text(d->font, cstr_String(visText)).advance.x >
                    width_Rect(contentBounds_InputWidget_(d))) {
                    omitDefaultScheme_(visText);
                }
            }
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
        /* TODO: To apply syntax highlighting here, we'd have to draw line by line. */
        iWrapText wt = wrapParams_InputWidget_(d);
        wt.maxLines = d->maxWrapLines;
        wt.text = range_String(visText);
        const int fg = uiInputText_ColorId;
        d->buffered = new_TextBuf(&wt, d->font, fg);
        delete_String(visText);
    }
    d->inFlags &= ~needUpdateBuffer_InputWidgetFlag;
}

void setText_InputWidget(iInputWidget *d, const iString *text) {
    if (!d) return;
    const iBool isAllSelected = isAllSelected_InputWidget_(d);
    setTextUndoable_InputWidget(d, text, iFalse);
    if (isAllSelected) {
        selectAll_InputWidget(d);
    }
}

void setTextUndoable_InputWidget(iInputWidget *d, const iString *text, iBool isUndoable) {
    if (!d) return;
    if (d->inFlags & isUrl_InputWidgetFlag) {
        if (prefs_App()->decodeUserVisibleURLs) {
            text = collect_String(urlDecodeExclude_String(text, URL_DECODE_EXCLUDE_CHARS));
        }
        else {
            /* The user wants URLs encoded, also Punycode the domain. */
            iString *enc = urlEncodeExclude_String(text, URL_ENCODE_EXCLUDE_CHARS);
            /* Prevent address bar spoofing (mentioned as IDN homograph attack in
               https://github.com/skyjake/lagrange/issues/73) */
            punyEncodeUrlHost_String(enc);
            text = enc;
        }
    }
    iString *nfcText = collect_String(copy_String(text));
    normalize_String(nfcText);
    replaceText_InputWidget_(d, nfcText, isUndoable);
    if (!isFocused_Widget(d)) {
        d->inFlags |= needUpdateBuffer_InputWidgetFlag;
    }
    updateVisible_InputWidget_(d);
    updateMetrics_InputWidget_(d);
    if (!hasNativeControl_InputWidget_(d)) {
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

void validate_InputWidget(iInputWidget *d) {
    if (d->validator) {
        d->validator(d, d->validatorContext); /* this may change the contents */
    }
}

static void begin_InputWidget_(iInputWidget *d, iBool allowSelectAll) {
    iWidget *w = as_Widget(d);
    if (isEditing_InputWidget_(d)) {
        /* Already active. */
        return;
    }
    invalidateBuffered_InputWidget_(d);
    setFlags_Widget(w, hidden_WidgetFlag | disabled_WidgetFlag, iFalse);
    setFlags_Widget(w, selected_WidgetFlag, iTrue);
    d->inFlags &= ~enterPressed_InputWidgetFlag;
    beginEditing_InputWidget_(d, allowSelectAll);
}

void begin_InputWidget(iInputWidget *d) {
    begin_InputWidget_(d, iTrue);
}

void end_InputWidget(iInputWidget *d, iBool accept) {
    iWidget *w = as_Widget(d);
    if (!isEditing_InputWidget_(d)) {
        /* Was not active. */
        return;
    }
    endEditing_InputWidget_(d, accept);
    d->inFlags |= needUpdateBuffer_InputWidgetFlag;
    setFlags_Widget(w, selected_WidgetFlag | keepOnTop_WidgetFlag | touchDrag_WidgetFlag, iFalse);
    const char *id = cstr_String(id_Widget(as_Widget(d)));
    if (!*id) id = "_";
    refresh_Widget(w);
    if (d->inFlags & isUrl_InputWidgetFlag) {
        /* Check for bang snippets. */
        const iString *text = text_InputWidget(d);
        if (startsWith_String(text, "!")) {
            iRangecc snip = iNullRange;
            if (nextSplit_Rangecc(range_String(text), " ", &snip)) {
                const iString *content = get_Snippets(collectNewRange_String(snip));
                if (!isEmpty_String(content)) {
                    iString *query = collectNewRange_String((iRangecc){ snip.end, constEnd_String(text) });
                    trim_String(query);
                    set_String(query, collect_String(urlEncode_String(query)));
                    if (!contains_String(content, '?')) {
                        /* substition may already have the beginning of a query */
                        prependCStr_String(query, "?");
                    }
                    prepend_String(query, content);
                    setText_InputWidget(d, query);
                }
            }
        }
    }
    notify_Widget(w,
                  "input.ended id:%s enter:%d arg:%d",
                  id,
                  d->inFlags & enterPressed_InputWidgetFlag ? 1 : 0,
                  accept ? 1 : 0);
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

void contentsWereChanged_InputWidget_(iInputWidget *d) {
    validate_InputWidget(d);
    if (d->inFlags & notifyEdits_InputWidgetFlag) {
        notify_Widget(d, "input.edited id:%s", cstr_String(id_Widget(constAs_Widget(d))));
    }
    if (!hasNativeControl_InputWidget_(d)) {
        refresh_Widget(d);
    }
}

static iRect bounds_InputWidget_(const iInputWidget *d) {
    const iWidget *w = constAs_Widget(d);
    iRect bounds = bounds_Widget(w);
    if (!isFocused_Widget(d)) {
        return bounds;
    }
    /* There may be more visible lines than fits in the widget bounds. */
    bounds.size.y = contentHeight_InputWidget_(d) + 3 * padding_InputWidget_().y;
    if (w->flags & extraPadding_WidgetFlag) {
        bounds.size.y += extraPaddingHeight_InputWidget_(d);
    }
    return bounds;
}

static iBool contains_InputWidget_(const iInputWidget *d, iInt2 coord) {
    return contains_Rect(bounds_InputWidget_(d), coord);
}

iBool checkLineBreakMods_InputWidget_(const iInputWidget *d, int mods) {
    if (d->inFlags & useReturnKeyBehavior_InputWidgetFlag) {
        return mods == lineBreakKeyMod_ReturnKeyBehavior(prefs_App()->returnKey);
    }
    return mods == 0;
}

static iBool checkAcceptMods_InputWidget_(const iInputWidget *d, int mods) {
    if (isMobile_Platform()) {
        return mods == KMOD_PRIMARY; /* non-configurable */
    }
    if (d->inFlags & useReturnKeyBehavior_InputWidgetFlag) {
        return mods == acceptKeyMod_ReturnKeyBehavior(prefs_App()->returnKey);
    }
    return mods == 0;
}

static void overflowScrollToKeepVisible_InputWidget_(iAny *widget) {
    iInputWidget *d = widget;
    iWidget *w = as_Widget(d);
    if (!isFocused_Widget(w)) {
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
    if (deviceType_App() != desktop_AppDeviceType && focus_Widget() == w &&
        isCommand_UserEvent(ev, "menu.opened")) {
        setFocus_Widget(NULL);
        return iFalse;
    }
    if (isCommand_Widget(w, ev, "focus.gained")) {
        if (contentBounds_InputWidget_(d).size.x < minWidth_InputWidget_) {
            setFocus_Widget(NULL);
        }
        else {
            enum iFocusMethod method = arg_Command(command_UserEvent(ev));
            begin_InputWidget_(d, method != arrowKeys_FocusMethod);
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
    enum iEventResult result = processCommand_InputWidget_(d, ev);
    if (result) {
        return result >> 1;
    }
    if (isCommand_UserEvent(ev, "input.selectall") && isEditing_InputWidget_(d)) {
        selectAll_InputWidget(d);
        return iTrue;
    }
    else if (isCommand_UserEvent(ev, "input.deselect") && isEditing_InputWidget_(d)) {
        deselect_InputWidget(d);
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
    }
    result = processInputEvent_InputWidget_(d, ev);
    if (result) {
        return result >> 1;
    }
    /* Click behavior depends on device type. */
    result = (deviceType_App() == desktop_AppDeviceType ? processPointerEvents_InputWidget_(d, ev)
                                                         : processTouchEvents_InputWidget_(d, ev));
    if (result) {
        return result >> 1;
    }
    if (ev->type == SDL_EVENT_KEY_UP && isFocused_Widget(w)) {
        return iTrue;
    }
    if (ev->type == SDL_EVENT_KEY_DOWN && isFocused_Widget(w)) {
        result = processKeyDown_InputWidget_(d, ev);
        if (result) {
            return result >> 1;
        }
        const int key  = ev->key.key;
        const int mods = keyMods_Sym(ev->key.mod);
        switch (key) {
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                if (d->inFlags & enterKeyEnabled_InputWidgetFlag &&
                    (checkAcceptMods_InputWidget_(d, mods) ||
                     (~d->inFlags & lineBreaksEnabled_InputWidgetFlag))) {
                    d->inFlags |= enterPressed_InputWidgetFlag;
                    if (isTerminal_Platform() && cmp_String(id_Widget(w), "url")) {
                        /* In dialogs, Return moves to the next focusable field rather than
                           loosing focus entirely. */
                        setFocus_Widget(findFocusable_Widget(w, forward_WidgetFocusDir));
                    }
                    else {
                        setFocus_Widget(NULL);
                    }
                    return iTrue;
                }
                /* The native input handles the Return key. */
                return LAGRANGE_USE_SYSTEM_TEXT_INPUT != 0;
            case SDLK_ESCAPE:
                end_InputWidget(d, iTrue);
                setFocus_Widget(NULL);
                return (d->inFlags & eatEscape_InputWidgetFlag) != 0;
        }
        if (mods & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) {
            return iFalse;
        }
        return iTrue;
    }
    return processEvent_Widget(w, ev);
}

/* Draws the buffered text or the hint. Returns false if neither is shown. */
iBool drawStatic_InputWidget_(const iInputWidget *d, iRect contentBounds, int visLineOffsetY) {
    const iWidget *w       = constAs_Widget(d);
    const iInt2    drawPos = topLeft_Rect(contentBounds);
    if (d->buffered && !isFocused_Widget(w)) {
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
        return iTrue;
    }
    if (isHintVisible_InputWidget_(d)) {
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
        return iTrue;
    }
    return iFalse;
}

static void draw_InputWidget_(const iInputWidget *d) {
    const iWidget *w          = constAs_Widget(d);
    iRect          bounds     = adjusted_Rect(bounds_InputWidget_(d), padding_InputWidget_(),
                                              neg_I2(padding_InputWidget_()));
    iBool          isHint     = isHintVisible_InputWidget_(d);
    const iBool    isFocused  = isFocused_Widget(w);
    const iBool    isDisabled = isDisabled_Widget(w);
    const iBool    isHover    = deviceType_App() == desktop_AppDeviceType &&
                                isHover_Widget(w) &&
                                contains_InputWidget_(d, mouseCoord_Window(get_Window(), 0));
    if (d->inFlags & needUpdateBuffer_InputWidgetFlag) {
        updateBuffered_InputWidget_(iConstCast(iInputWidget *, d));
    }
    iPaint p;
    init_Paint(&p);
    const iBool isFramed   = !isTerminal_Platform() && ~w->flags & frameless_WidgetFlag;
    const int   bgColor    = isFocused ? uiInputBackgroundFocused_ColorId : w->bgColor;
    const int   frameColor = isDisabled  ? none_ColorId
                             : isFocused ? uiInputFrameFocused_ColorId
                             : isHover   ? uiInputFrameHover_ColorId
                                         : uiInputFrame_ColorId;
    if (!isTerminal_Platform() && ~w->flags2 & noRound_WidgetFlag2) {
        drawRoundRect_Paint(&p,
                            bounds,
                            all_RoundCorner,
                            frame_UI * (isFocused ? gap_UI / 4 : 1),
                            bgColor,
                            isFramed ? frameColor : none_ColorId,
                            isFramed ? frameColor : none_ColorId);
    }
    else {
        fillRect_Paint(&p, bounds, bgColor);
        if (isFramed && frameColor >= 0) {
            drawRectThickness_Paint(&p,
                                    adjusted_Rect(bounds, neg_I2(one_I2()), zero_I2()),
                                    frame_UI * (isFocused ? gap_UI / 4 : 1),
                                    frameColor);
        }
    }
    if (hasNativeControl_InputWidget_(d)) {
        /* The system-provided control is drawing the text. */
        drawChildren_Widget(w);
        return;
    }
    const int fg = isDisabled  ? uiTextDisabled_ColorId
                   : isHint    ? uiAnnotation_ColorId
                   : isFocused ? uiInputTextFocused_ColorId
                               : uiInputText_ColorId;
    drawContent_InputWidget_(d, &p, bounds, contentBounds_InputWidget_(d), fg);
    drawChildren_Widget(w);
}

iBeginDefineSubclass(InputWidget, Widget)
    .processEvent = (iAny *) processEvent_InputWidget_,
    .draw         = (iAny *) draw_InputWidget_,
iEndDefineSubclass(InputWidget)
