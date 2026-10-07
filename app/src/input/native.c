/* Copyright 2020-2026 Jaakko Keränen <jaakko.keranen@iki.fi>

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

/* Editing with a system-provided text control. The widget only draws the static text
   while the control is not active. */

#include "impl.h"

#include "ui/command.h"
#include "ui/root.h"
#include <lagrange/snippets.h>

#if defined (iPlatformAndroidMobile)
#   include "platform/android.h"
#endif

void initVariant_InputWidget_(iInputWidget *d) {
    d->sysCtrl = NULL;
    init_String(&d->text);
    d->pendingSelectionRange = (iRangei){ -1, -1 };
}

void deinitVariant_InputWidget_(iInputWidget *d) {
    delete_SystemTextInput(d->sysCtrl);
    deinit_String(&d->text);
}

iString *newText_InputWidget_(const iInputWidget *d) {
    return copy_String(&d->text);
}

iString *newVisibleText_InputWidget_(const iInputWidget *d) {
    return copy_String(&d->text);
}

iBool isEmpty_InputWidget_(const iInputWidget *d) {
    return isEmpty_String(&d->text);
}

iBool isAllSelected_InputWidget_(const iInputWidget *d) {
    iUnused(d);
    return iFalse; /* Query the native widget? */
}

void replaceText_InputWidget_(iInputWidget *d, const iString *text, iBool isUndoable) {
    iUnused(isUndoable);
    set_String(&d->text, text);
    if (d->sysCtrl) {
        setText_SystemTextInput(d->sysCtrl, text, iTrue);
    }
    else {
        updateAllLinesAndResizeHeight_InputWidget_(d); /* need to know the new height */
    }
}

int visLineOffsetY_InputWidget_(const iInputWidget *d) {
    iUnused(d);
    return 0; /* offset for the buffered text */
}

void updateVisible_InputWidget_(iInputWidget *d) {
    iUnused(d);
    /* TODO: Anything to do? */
}

void updateAllLinesAndResizeHeight_InputWidget_(iInputWidget *d) {
    if (width_Widget(d) >= minWidth_InputWidget_) {
        /* Rewrap the buffered text and resize accordingly. */
        iWrapText wt = wrapParams_InputWidget_(d);
        wt.text = range_String(&d->text);
        /* TODO: Set max lines limit for WrapText. */
        const int height = measure_WrapText(&wt, d->font).bounds.size.y;
        /* We use this to store the number wrapped lines for determining widget height. */
        d->visWrapLines.start = 0;
        d->visWrapLines.end = iMax(d->minWrapLines,
                                   iMin(d->maxWrapLines, height / lineHeight_Text(d->font)));
        updateMetrics_InputWidget_(d);
    }
}

int contentHeight_InputWidget_(const iInputWidget *d) {
    const int lineHeight = lineHeight_Text(d->font);
    const int minHeight = d->minWrapLines * lineHeight;
    const int maxHeight = d->maxWrapLines * lineHeight;
    if (d->sysCtrl) {
        const int preferred = (preferredHeight_SystemTextInput(d->sysCtrl) + 2 * gap_UI) / lineHeight;
        return iClamp(preferred * lineHeight, minHeight, maxHeight);
    }
    if (d->buffered && ~d->inFlags & needUpdateBuffer_InputWidgetFlag) {
        return iClamp(d->buffered->size.y, minHeight, maxHeight);
    }
    return (int) size_Range(&d->visWrapLines) * lineHeight;
}

iBool isHeightKnown_InputWidget_(const iInputWidget *d) {
    /* The native control may not know the appropriate height yet. */
    return !d->sysCtrl || preferredHeight_SystemTextInput(d->sysCtrl) != 0;
}

void updateTextInputRect_InputWidget_(const iInputWidget *d) {
    if (d->sysCtrl) {
        setRect_SystemTextInput(d->sysCtrl, contentBounds_InputWidget_(d));
    }
}

static void updateAfterVisualOffsetChange_InputWidget_(iInputWidget *d, iRoot *root) {
    iAssert(as_Widget(d)->root == root);
    iUnused(root);
    if (d->sysCtrl) {
        setRect_SystemTextInput(d->sysCtrl, contentBounds_InputWidget_(d));
    }
}

static void systemInputChanged_InputWidget_(iSystemTextInput *sysCtrl, void *widget) {
    iInputWidget *d = widget;
    const iString *sysText = text_SystemTextInput(sysCtrl);
    if (!equal_String(&d->text, sysText)) {
        set_String(&d->text, sysText);
        restartBackupTimer_InputWidget_(d);
        contentsWereChanged_InputWidget_(d);
    }
    updateMetrics_InputWidget_(d);
}

void selectAll_InputWidget(iInputWidget *d) {
    if (d->sysCtrl) {
        selectAll_SystemTextInput(d->sysCtrl);
    }
}

void deselect_InputWidget(iInputWidget *d) {
    iUnused(d);
    /* TODO? */
}

void moveCursorHome_InputWidget(iInputWidget *d) {
    if (d->sysCtrl) {
        // TODO: Is there a way to move the native cursor to the start?
    }
}

void beginEditing_InputWidget_(iInputWidget *d, iBool allowSelectAll) {
    iUnused(allowSelectAll);
    iWidget *w = as_Widget(d);
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
#if defined (iPlatformAndroidMobile)
    if (d->pendingSelectionRange.start >= 0) {
        setSelection_SystemTextInput(d->sysCtrl, d->pendingSelectionRange);
        d->pendingSelectionRange = (iRangei){ -1, -1 };
    }
#endif
    setTextChangedFunc_SystemTextInput(d->sysCtrl, systemInputChanged_InputWidget_, d);
    iConnect(Root, w->root, visualOffsetsChanged, d, updateAfterVisualOffsetChange_InputWidget_);
    updateTextInputRect_InputWidget_(d);
    updateMetrics_InputWidget_(d);
    refresh_Widget(d); /* ensure buffered panels hide the static text */
}

void endEditing_InputWidget_(iInputWidget *d, iBool accept) {
    iWidget *w = as_Widget(d);
    if (d->sysCtrl) {
        iDisconnect(Root, w->root, visualOffsetsChanged, d, updateAfterVisualOffsetChange_InputWidget_);
        if (accept) {
            set_String(&d->text, text_SystemTextInput(d->sysCtrl));
            if (d->inFlags & isUrl_InputWidgetFlag) {
                /* User probably didn't intend to have spaces in the start/end of a URL. */
                trim_String(&d->text);
            }
        }
        else {
            set_String(&d->text, &d->oldText);
        }
        delete_SystemTextInput(d->sysCtrl);
        d->sysCtrl = NULL;
    }
}

enum iEventResult processCommand_InputWidget_(iInputWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (isResize_UserEvent(ev)) {
        if (d->sysCtrl) {
            updateAfterVisualOffsetChange_InputWidget_(d, w->root);
        }
    }
    else if (isCommand_UserEvent(ev, "input.paste")) {
        const char *cmd = command_UserEvent(ev);
#if defined (iPlatformAndroidMobile)
        /* FIXME: Should paste to the most recently active input widget.
           The only place where snippets can be pasted on mobile is the upload dialog's
           text editor. In other fields, the system clipboard will have to be manually
           accessed. */
        if (!cmp_String(id_Widget(w), "upload.text") || !cmp_String(id_Widget(w), "input")) {
            const iString *content = get_Snippets(collect_String(suffix_Command(
                    cmd, "snippet")));
            /* On Android, we don't have system menus so activating a dropdown menu will
               unfocus the native text input field (otherwise the field over be drawn over
               the menu). We'll do the insertion here and reactivate focus on the menu. */
            iRangei selRange = lastInputSelectionRange_Android();
            iString *modified = copy_String(&d->text);
            truncate_String(modified, selRange.start);
            append_String(modified, content);
            append_String(modified, collect_String(mid_String(&d->text, selRange.end, iInvalidSize)));
            set_String(&d->text, modified);
            delete_String(modified);
            postCommand_Widget(w, "focus.set id:%s", cstr_String(id_Widget(w)));
            d->pendingSelectionRange = (iRangei){
                selRange.start,
                selRange.start + length_String(content)
            };
            return true_EventResult;
        }
#else
        if (isEditing_InputWidget_(d) && d->sysCtrl) {
            insert_SystemTextInput(d->sysCtrl, get_Snippets(collect_String(suffix_Command(
                    cmd, "snippet"))));
            return true_EventResult;
        }
#endif
        return false_EventResult;
    }
    return ignored_EventResult;
}

enum iEventResult processInputEvent_InputWidget_(iInputWidget *d, const SDL_Event *ev) {
    iUnused(d, ev);
    return ignored_EventResult;
}

enum iEventResult processPointerEvents_InputWidget_(iInputWidget *d, const SDL_Event *ev) {
    iUnused(d, ev);
    return ignored_EventResult;
}

enum iEventResult processTouchEvents_InputWidget_(iInputWidget *d, const SDL_Event *ev) {
    /* Just a tap to activate the system-provided text input control. */
    switch (processEvent_Click(&d->click, ev)) {
        case none_ClickResult:
            break;
        case started_ClickResult:
            setFocus_Widget(as_Widget(d));
            return true_EventResult;
        default:
            return true_EventResult;
    }
    return ignored_EventResult;
}

enum iEventResult processKeyDown_InputWidget_(iInputWidget *d, const SDL_Event *ev) {
    iUnused(d);
    switch (ev->key.key) {
        /* The native input handles cursor movements. */
        case SDLK_HOME:
        case SDLK_END:
        case SDLK_LEFT:
        case SDLK_RIGHT:
        case SDLK_UP:
        case SDLK_DOWN:
        case SDLK_PAGEUP:
        case SDLK_PAGEDOWN:
            return true_EventResult;
    }
    return ignored_EventResult;
}

void drawContent_InputWidget_(const iInputWidget *d, iPaint *p, iRect bounds,
                              iRect contentBounds, int fg) {
    iUnused(p, bounds, fg);
    drawStatic_InputWidget_(d, contentBounds, visLineOffsetY_InputWidget_(d));
}
