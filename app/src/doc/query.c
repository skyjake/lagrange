/* Copyright 2026 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "query.h"

#include "../app.h"
#include "../defs.h"
#include "../gmutil.h"
#include "../render/paint.h"
#include "documentview.h"
#include "persistentstate.h"
#include "ui/command.h"
#include "ui/documentwidget.h"
#include "ui/inputwidget.h"
#include "ui/keys.h"
#include "ui/labelwidget.h"
#include "ui/menu.h"
#include "ui/util.h"
#include "ui/widget.h"

#include <lagrange/gmrequest.h>
#include <lagrange/sitespec.h>

iDefineTypeConstruction(Query)

static iWidget *ownerWidget_(const iQuery *d) {
    return as_Widget(d->owner);
}

static iDocumentView *ownerView_(const iQuery *d) {
    return view_DocumentWidget(d->owner);
}

static iGmDocument *ownerDoc_(const iQuery *d) {
    return ownerView_(d)->doc;
}

static iString *makeQueryUrl_(const iString *queryUrl, const iString *userEnteredText) {
    iString *url = copy_String(queryUrl);
    /* Remove the existing query string. */
    const size_t qPos = indexOfCStr_String(url, "?");
    if (qPos != iInvalidPos) {
        remove_Block(&url->chars, qPos, iInvalidSize);
    }
    appendCStr_String(url, "?");
    iString *cleaned = copy_String(userEnteredText);
    if (deviceType_App() != desktop_AppDeviceType) {
        trimEnd_String(cleaned); /* autocorrect may insert an extra space */
        if (isEmpty_String(cleaned)) {
            set_String(cleaned, userEnteredText); /* user wanted just spaces? */
        }
    }
    append_String(url, collect_String(urlEncode_String(cleaned)));
    delete_String(cleaned);
    return url;
}

static void inputQueryValidator_(iInputWidget *input, void *context) {
    iDocumentWidget *d = context;
    iString *url = makeQueryUrl_(state_DocumentWidget(d)->url, text_InputWidget(input));
    iWidget *dlg = parent_Widget(input);
    iLabelWidget *counter = findChild_Widget(dlg, "valueinput.counter");
    iAssert(counter);
    int avail = 1024 - (int) size_String(url);
    setFlags_Widget(findChild_Widget(dlg, "default"), disabled_WidgetFlag, avail < 0);
    setEnterKeyEnabled_InputWidget(input, avail >= 0);
    int len = length_String(text_InputWidget(input));
    if (len > 1024) {
        iString *trunc = copy_String(text_InputWidget(input));
        truncate_String(trunc, 1024);
        setText_InputWidget(input, trunc);
        delete_String(trunc);
    }
    setTextCStr_LabelWidget(counter, format_CStr("%d", avail)); /* Gemini URL maxlen */
    setTextColor_LabelWidget(counter,
                             avail < 0   ? uiTextCaution_ColorId :
                             avail < 128 ? uiTextStrong_ColorId
                                         : uiTextDim_ColorId);
    delete_String(url);
    arrange_Widget(findChild_Widget(dlg, "dialogbuttons"));
}

static void makePastePrecedingLineMenuItem_(iMenuItem *item_out, const iWidget *buttons,
                                            const char *precedingLine) {
    const iBinding *bind = findCommand_Keys("input.precedingline");
    *item_out = (iMenuItem){
        "${menu.input.precedingline}",
         bind->key,
         bind->mods,
         format_CStr("!valueinput.set ptr:%p text:%s", buttons, precedingLine)
    };
}

static const iArray *updateInputPromptMenuItems_(iWidget *menu) {
    const char     *context       = cstr_String(&menu->data);
    const iWidget  *buttons       = pointerLabel_Command(context, "buttons");
    const iLabelWidget *prompt    = findChild_Widget(parent_Widget(buttons), "valueinput.prompt");
    const iString  *url           = string_Command(context, "url");
    const char     *precedingLine = suffixPtr_Command(context, "preceding");
    /* Compose new menu items. */
    iArray *items = collectNew_Array(sizeof(iMenuItem));
    iMenuItem pasteItem;
    makePastePrecedingLineMenuItem_(&pasteItem, buttons, precedingLine);
    pushBack_Array(items, &pasteItem);
    pushBack_Array(items,
                   &(iMenuItem) { "${menu.input.pasteprompt}",
                                  0,
                                  0,
                                  format_CStr("!valueinput.set ptr:%p text:%s",
                                              buttons,
                                              cstr_String(text_LabelWidget(prompt))) });
    pushBack_Array(items, &(iMenuItem){ "${menu.paste.snippet}", 0, 0, "submenu id:snippetmenu" });
    /* An inline prompt is always docked under its link, so only a modal sheet needs this. */
    const iBool isSheetPrompt = (flags_Widget(parent_Widget(buttons)) & mouseModal_WidgetFlag) != 0;
    if (isDesktop_Platform() && isSheetPrompt) {
        /* Location of the prompt. */
        const iBool isBottom = prefs_App()->promptPosition == bottom_InputPromptPosition;
        pushBackN_Array(
            items,
            (iMenuItem[]) {
                { "---" },
                { isBottom ? "${menu.input.showtop}" : "${menu.input.showbottom}",
                  0,
                  0,
                  format_CStr("!valueinput.togglebottom ptr:%p", buttons) } },
            2);
    }
    pushBackN_Array(
        items,
        (iMenuItem[]){
            { "---" },
            { !isPromptUrl_SiteSpec(url) ? "${menu.input.setprompt}" : "${menu.input.unsetprompt}",
              0,
              0,
              format_CStr("!prompturl.toggle url:%s", cstr_String(url)) } },
        2);
    /* Recently submitted input texts can be restored. */ {
        const iStringArray *recentInput = recentlySubmittedInput_App();
        if (!isEmpty_StringArray(recentInput)) {
            pushBack_Array(items, &(iMenuItem){ "---" });
            pushBack_Array(items, &(iMenuItem){
                isMobile_Platform() ? "---${ST:menu.input.restore}" : "```${menu.input.restore}"
            });
            iReverseConstForEach(StringArray, i, recentInput) {
                iString *label = collect_String(copy_String(i.value));
                replace_String(label, "\n\n", " ");
                replace_String(label, "\n", " ");
                trim_String(label);
                const size_t maxLen = 45;
                if (length_String(label) > maxLen) {
                    truncate_String(label, maxLen);
                    trim_String(label);
                    appendCStr_String(label, "...");
                }
                pushBack_Array(items,
                               &(iMenuItem){ cstr_String(label),
                                             0,
                                             0,
                                             format_CStr("!valueinput.set ptr:%p text:%s",
                                                         buttons,
                                                         cstr_String(i.value)) });
            }
            pushBackN_Array(
                items,
                (iMenuItem[]) { { "---" }, { "${menu.input.clear}", 0, 0, "!recentinput.clear" } },
                2);
        }
    }
    return items;
}

static void setupDialog_Query_(iQuery *d, iWidget *dlg, const iString *url,
                                      iBool isSensitive) {
    iWidget *buttons = findChild_Widget(dlg, "dialogbuttons");
    iLabelWidget *lineBreak = NULL;
    if (!isSensitive) {
        /* The line break and URL length counters are positioned differently on mobile.
           There is no line breaks in sensitive input. */
        if (deviceType_App() == desktop_AppDeviceType) {
            iString *keyStr = collectNew_String();
            toString_Sym(SDLK_RETURN,
                         lineBreakKeyMod_ReturnKeyBehavior(prefs_App()->returnKey),
                         keyStr);
            lineBreak = new_LabelWidget(
                format_CStr("${dlg.input.linebreak}" uiTextAction_ColorEscape "  %s",
                            cstr_String(keyStr)),
                NULL);
            insertChildAfter_Widget(buttons, iClob(lineBreak), 0);
        }
        if (lineBreak) {
            setFlags_Widget(as_Widget(lineBreak), frameless_WidgetFlag, iTrue);
            setTextColor_LabelWidget(lineBreak, uiTextDim_ColorId);
        }
    }
    iWidget *counter = (iWidget *) new_LabelWidget("", NULL);
    setId_Widget(counter, "valueinput.counter");
    setFlags_Widget(counter, frameless_WidgetFlag | resizeToParentHeight_WidgetFlag, iTrue);
    if (deviceType_App() == desktop_AppDeviceType) {
        addChildPos_Widget(buttons, iClob(counter), front_WidgetAddPos);
    }
    else {
        insertChildAfter_Widget(buttons, iClob(counter), 1);
    }
    if (lineBreak && deviceType_App() != desktop_AppDeviceType) {
        addChildPos_Widget(buttons, iClob(lineBreak), front_WidgetAddPos);
    }
    /* Shortcut for the Paste Preceding Line. The menu is dynamic so it won't listen
       for the keys as usual. */ {
        iMenuItem pasteItem;
        makePastePrecedingLineMenuItem_(
            &pasteItem, buttons, cstr_String(linePrecedingLink_DocumentWidget(d->owner)));
        addAction_Widget(dlg, pasteItem.key, pasteItem.kmods, pasteItem.command);
    }
    /* Menu for additional actions, past entries. */ {
        iLabelWidget *ellipsisButton =
            makeMenuButton_LabelWidget(midEllipsis_Icon, NULL, 0);
        iWidget *menu = findChild_Widget(as_Widget(ellipsisButton), "menu");
        /* When opening, update the items to reflect the site-specific settings. */
        setMenuUpdateItemsFunc_Widget(menu, updateInputPromptMenuItems_);
        set_String(&menu->data,
                   collectNewFormat_String("context buttons:%p url:%s preceding:%s",
                                           buttons,
                                           cstr_String(canonicalUrl_String(url)),
                                           cstr_String(linePrecedingLink_DocumentWidget(
                                               d->owner))));
        if (deviceType_App() == desktop_AppDeviceType) {
            addChildPos_Widget(buttons, iClob(ellipsisButton), front_WidgetAddPos);
        }
        else {
            insertChildAfterFlags_Widget(buttons, iClob(ellipsisButton), 0,
                                         frameless_WidgetFlag | noBackground_WidgetFlag);
            setFont_LabelWidget(ellipsisButton, font_LabelWidget((iLabelWidget *) lastChild_Widget(buttons)));
            setTextColor_LabelWidget(ellipsisButton, uiTextAction_ColorId);
        }
    }
    iInputWidget *input = findChild_Widget(dlg, "input");
    setValidator_InputWidget(input, inputQueryValidator_, d->owner);
    setBackupFileName_InputWidget(input, "inputbackup");
    setSelectAllOnFocus_InputWidget(input, iTrue);
    setSensitiveContent_InputWidget(input, isSensitive);
    setArrowFocusNavigable_InputWidget(input, iFalse);
}

static iWidget *makeBar_Query_(iQuery *d, iGmLinkId linkId, const iString *url,
                                      iBool isSensitive, const char *promptLabel) {
    iUrl parts;
    init_Url(&parts, url);
    const iString *acceptCommand =
        collectNewFormat_String("!document.input.submit doc:%p link:%u", d->owner, linkId);
    iWidget *dlg = makeEmbeddedValueInput_Widget(
        ownerWidget_(d),
        NULL,
        promptLabel ? promptLabel
                    : format_CStr(cstr_Lang("dlg.input.prompt"), cstr_Rangecc(parts.path)),
        uiTextAction_ColorEscape "${dlg.input.send}",
        cstr_String(acceptCommand),
        NULL, 0);
    /* Width must be correct before setupInputPromptDialog_() restores any backup text below,
       since that re-wraps (and measures height) immediately at the input's current width. */
    dlg->rect.size.x = documentWidth_DocumentView(ownerView_(d));
    arrange_Widget(dlg);
    setupDialog_Query_(d, dlg, url, isSensitive);
    arrange_Widget(dlg); /* pick up any height change from restored backup content */
    setId_Widget(dlg, format_CStr("inputprompt%u", linkId)); /* required for lookup */
    return dlg;
}

static iWidget *recreate_Query_(iQuery *d, iGmLinkId linkId) {
    iMediaId mediaId = findLinkInputPrompt_Media(media_GmDocument(ownerDoc_(d)), linkId);
    if (!mediaId.type) {
        return NULL;
    }
    iBool isSensitive;
    const iString *label, *baseUrl;
    int heightPx;
    inputPromptInfo_Media(media_GmDocument(ownerDoc_(d)), mediaId, &isSensitive, &label, &baseUrl, &heightPx);
    iWidget *bar = makeBar_Query_(
        d, linkId, baseUrl, isSensitive, label ? cstr_String(label) : NULL);
    postCommand_Widget(bar, "valueinput.resized"); /* reconcile with the (possibly stale) reserved height */
    return bar;
}

static void drawClipped_Query_(const iPtrArray *bars, const iRect *clipBounds) {
    /* Inline prompts are drawn clipped so they don't escape the document area during swipes. */
    iPaint p;
    init_Paint(&p);
    setClip_Paint(&p, *clipBounds);
    iConstForEach(PtrArray, i, bars) {
        const iWidget *bar = i.ptr;
        class_Widget(bar)->draw(bar);
    }
    unsetClip_Paint(&p);
}

/*----------------------------------------------------------------------------------------------*/

void init_Query(iQuery *d) {
    d->owner = NULL;
    init_PtrArray(&d->outgoing);
    init_PtrArray(&d->covered);
}

void deinit_Query(iQuery *d) {
    deinit_PtrArray(&d->covered);
    deinit_PtrArray(&d->outgoing);
}

void setOwner_Query(iQuery *d, iDocumentWidget *owner) {
    d->owner = owner;
}

iWidget *findBar_Query(const iQuery *d, iGmLinkId linkId) {
    return findChild_Widget(ownerWidget_(d), format_CStr("inputprompt%u", linkId));
}

void destroyAll_Query(iQuery *d) {
    iForEach(ObjectList, i, children_Widget(ownerWidget_(d))) {
        iWidget *child = i.object;
        if (startsWith_String(id_Widget(child), "inputprompt") &&
            ~child->flags2 & deferredDraw_WidgetFlag2) { /* skip a currently covered bar */
            destroy_Widget(child);
        }
    }
}

void deferOutgoing_Query(iQuery *d) {
    /* Escape the "inputpromptN" id lookup and take these out of the normal draw pass. */
    iAssert(isEmpty_PtrArray(&d->outgoing));
    iConstForEach(PtrArray, m, &swipeView_DocumentWidget(d->owner)->visibleMedia) {
        const iGmRun *run = m.ptr;
        if (run->mediaType != inputPrompt_MediaType) {
            continue;
        }
        iWidget *bar = findBar_Query(d, run->linkId);
        if (!bar) {
            continue;
        }
        setId_Widget(bar, format_CStr("outgoinginputprompt%u", run->linkId));
        setFlags_Widget(bar, hidden_WidgetFlag | horizontalOffset_WidgetFlag, iTrue);
        iChangeFlags(bar->flags2, deferredDraw_WidgetFlag2, iTrue);
        if (focus_Widget() == bar || hasParent_Widget(focus_Widget(), bar)) {
            setFocus_Widget(NULL);
        }
        pushBack_PtrArray(&d->outgoing, bar);
    }
    repositionOutgoing_Query(d); /* position them before the first draw */
}

iWidget *create_Query(iQuery *d, iGmLinkId linkId, const iString *url,
                             iBool isSensitive, const iString *promptLabel) {
    /* Creates the bar and records it (and its height) in the document's media cache, so a
       subsequent layout doesn't fall back to a placeholder height. */
    iGmDocument *doc = ownerDoc_(d);
    setInputPrompt_Media(media_GmDocument(doc), linkId, isSensitive, promptLabel, url);
    iWidget *bar = makeBar_Query_(
        d, linkId, url, isSensitive, promptLabel ? cstr_String(promptLabel) : NULL);
    setInputPromptHeight_Media(
        media_GmDocument(doc), findLinkInputPrompt_Media(media_GmDocument(doc), linkId),
        height_Widget(bar));
    return bar;
}

void refreshAfterChange_Query(iQuery *d) {
    redoLayout_GmDocument(ownerDoc_(d));
    updateVisible_DocumentView(ownerView_(d));
    invalidate_DocumentWidget(d->owner);
    refresh_Widget(ownerWidget_(d));
}

void destroy_Query(iQuery *d, iGmLinkId linkId) {
    iWidget *bar = findBar_Query(d, linkId);
    if (bar) {
        destroy_Widget(bar);
    }
    clearInputPrompt_Media(media_GmDocument(ownerDoc_(d)), linkId);
    refreshAfterChange_Query(d);
}

void setEnabled_Query(iQuery *d, iGmLinkId linkId, iBool enabled) {
    iWidget *bar = findBar_Query(d, linkId);
    if (bar) {
        setTreeFlags_Widget(bar, disabled_WidgetFlag, !enabled);
        refresh_Widget(bar);
    }
}

void reenableAll_Query(iQuery *d) {
    iForEach(ObjectList, i, children_Widget(ownerWidget_(d))) {
        iWidget *child = i.object;
        if (startsWith_String(id_Widget(child), "inputprompt")) {
            setTreeFlags_Widget(child, disabled_WidgetFlag, iFalse);
            refresh_Widget(child);
        }
    }
}

void ensureVisible_Query(iQuery *d, iGmLinkId linkId) {
    /* Only called on creation/resize so this doesn't fight the user's own scrolling. */
    const iGmRun *run = findInputPromptRun_GmDocument(ownerDoc_(d), linkId);
    if (!run) {
        return;
    }
    const int     top    = top_Rect(run->bounds);
    const int     bottom = bottom_Rect(run->bounds);
    const iRangei vis    = visibleRange_DocumentView(ownerView_(d));
    const int     margin = gap_UI;
    int offset = 0;
    if (bottom - top > size_Range(&vis)) {
        /* Taller than the viewport: prioritize the top over the buttons row. */
        offset = (top - margin) - vis.start;
    }
    else if (bottom + margin > vis.end) {
        offset = (bottom + margin) - vis.end;
    }
    else if (top - margin < vis.start) {
        offset = (top - margin) - vis.start;
    }
    if (offset) {
        smoothScroll_DocumentView(ownerView_(d), offset, 300);
    }
}

void show_Query(iQuery *d, iGmLinkId linkId, const iString *baseUrl,
                       const iGmResponse *resp, enum iGmStatusCode statusCode) {
    if (findBar_Query(d, linkId)) {
        destroy_Query(d, linkId);
    }
    iWidget *bar = create_Query(
        d, linkId, baseUrl, statusCode == sensitiveInput_GmStatusCode,
        isEmpty_String(&resp->meta) ? NULL : &resp->meta);
    refreshAfterChange_Query(d);
    if (document_App() == d->owner) {
        /* Only steal focus if this tab is the one being viewed. */
        setFocus_Widget(findChild_Widget(bar, "input"));
        ensureVisible_Query(d, linkId);
    }
}

void repositionOutgoing_Query(iQuery *d) {
    const int offset = swipeOffsetOfView_DocumentWidget(d->owner, swipeView_DocumentWidget(d->owner));
    iForEach(PtrArray, i, &d->outgoing) {
        setVisualOffset_Widget(i.ptr, offset, 0, 0);
    }
}

void reposition_Query(iQuery *d, iDocumentView *view) {
    /* Must run after render_GmDocument() repopulates visibleMedia for this frame. */
    iWidget *w = ownerWidget_(d);
    const int swipeOffset = swipeOffsetOfView_DocumentWidget(d->owner, view);
    /* Is `view` currently covered, or rubber-banding without a swipeView? */
    const iBool isCovered =
        view == ownerView_(d) &&
        ((swipeView_DocumentWidget(d->owner) && isSwipeOverlay_DocumentWidget(d->owner)) ||
         (!swipeView_DocumentWidget(d->owner) && swipeOffset != 0));
    if (view == ownerView_(d)) {
        clear_PtrArray(&d->covered);
    }
    /* Only the bars actually in the viewport are flagged visible. Also clear a stale
       covered/deferred state, or a bar dropped from `visibleMedia` would never get
       drawn again. */
    iForEach(ObjectList, i, children_Widget(w)) {
        iWidget *child = i.object;
        if (startsWith_String(id_Widget(child), "inputprompt")) {
            setFlags_Widget(child, hidden_WidgetFlag, iTrue);
            iChangeFlags(child->flags2, deferredDraw_WidgetFlag2, iFalse);
        }
    }
    iConstForEach(PtrArray, m, &view->visibleMedia) {
        const iGmRun *run = m.ptr;
        if (run->mediaType != inputPrompt_MediaType) {
            continue;
        }
        iWidget *bar = findBar_Query(d, run->linkId);
        if (!bar) {
            /* The widget doesn't survive a document swap, but its state does in Media
               (cf. inline images restored from a cached/history document). */
            bar = recreate_Query_(d, run->linkId);
            if (!bar) {
                continue;
            }
        }
        const iRect rect = runRect_DocumentView(view, run); /* in window coords */
        bar->rect.pos    = windowToLocal_Widget(bar, rect.pos);
        bar->rect.pos.x += swipeOffset;
        bar->rect.size.x = rect.size.x;
        arrange_Widget(bar);
        if (isCovered) {
            setFlags_Widget(bar, hidden_WidgetFlag, iTrue);
            iChangeFlags(bar->flags2, deferredDraw_WidgetFlag2, iTrue);
            pushBack_PtrArray(&d->covered, bar);
        }
        else {
            setFlags_Widget(bar, hidden_WidgetFlag, iFalse);
            iChangeFlags(bar->flags2, deferredDraw_WidgetFlag2, iFalse);
        }
    }
    /* A hidden prompt would eat scroll keys if left focused. */
    if (focus_Widget() && isFinished_SmoothScroll(&view->scrollY)) {
        iWidget *bar = focus_Widget();
        while (bar && !startsWith_String(id_Widget(bar), "inputprompt")) {
            bar = bar->parent;
        }
        if (bar && bar->flags & hidden_WidgetFlag) {
            setFocus_Widget(NULL);
        }
    }
}

void drawOutgoing_Query(const iQuery *d, const iRect *clipBounds) {
    drawClipped_Query_(&d->outgoing, clipBounds);
}

void drawCovered_Query(const iQuery *d, const iRect *clipBounds) {
    drawClipped_Query_(&d->covered, clipBounds);
}

void showForPromptUrls_Query(iQuery *d) {
    /* Links marked "Assume This URL Requires Input" show their prompt immediately, not just
       after a click, avoiding a layout jump. Only applies when prompts can be inline. */
    if (deviceType_App() == desktop_AppDeviceType &&
        prefs_App()->promptPosition == inline_InputPromptPosition) {
        iGmDocument *doc = ownerDoc_(d);
        iBool     didAny      = iFalse;
        iGmLinkId firstLinkId = 0;
        for (size_t linkId = 1; ; linkId++) {
            const iString *linkUrl = linkUrl_GmDocument(doc, linkId);
            if (!linkUrl) break;
            const iString *absUrl = absoluteUrl_String(url_DocumentWidget(d->owner), linkUrl);
            if (!isPromptUrl_SiteSpec(absUrl)) {
                continue;
            }
            iUrl url;
            init_Url(&url, absUrl);
            if (!isEmpty_Range(&url.query) ||
                findLinkInputPrompt_Media(media_GmDocument(doc), linkId).type) {
                continue; /* already has a query, or already created (e.g. restored from cache) */
            }
            create_Query(d, (iGmLinkId) linkId, absUrl, iFalse, NULL);
            if (!firstLinkId) {
                firstLinkId = (iGmLinkId) linkId;
            }
            didAny = iTrue;
        }
        if (didAny) {
            refreshAfterChange_Query(d);
            /* Focus the first one only if it's in view -- otherwise it's as disorienting as
               the layout jump this feature avoids. */
            const iGmRun *run = findInputPromptRun_GmDocument(ownerDoc_(d), firstLinkId);
            if (run) {
                const iRangei vis = visibleRange_DocumentView(ownerView_(d));
                if (top_Rect(run->bounds) >= vis.start && bottom_Rect(run->bounds) <= vis.end) {
                    iWidget *bar = findBar_Query(d, firstLinkId);
                    if (bar) {
                        setFocus_Widget(findChild_Widget(bar, "input"));
                    }
                }
            }
        }
    }
}

void resetAfterSwipe_Query(iQuery *d) {
    /* The outgoing bars are done sliding; actually get rid of them now. */
    iForEach(PtrArray, o, &d->outgoing) {
        destroy_Widget(o.ptr);
    }
    clear_PtrArray(&d->outgoing);
    iForEach(PtrArray, c, &d->covered) {
        iWidget *bar = c.ptr;
        setFlags_Widget(bar, hidden_WidgetFlag, iFalse);
        iChangeFlags(bar->flags2, deferredDraw_WidgetFlag2, iFalse);
    }
    clear_PtrArray(&d->covered);
}

iWidget *makeModal_Query(iQuery *d, const iString *url, iBool isSensitive,
                                const char *promptLabel, const char *acceptCommand) {
    iUrl parts;
    init_Url(&parts, url);
    iWidget *dlg = makeValueInput_Widget(
        ownerWidget_(d),
        NULL,
        format_CStr(uiHeading_ColorEscape "%s", cstr_Rangecc(parts.host)),
        promptLabel ? promptLabel
                    : format_CStr(cstr_Lang("dlg.input.prompt"), cstr_Rangecc(parts.path)),
        uiTextAction_ColorEscape "${dlg.input.send}",
        acceptCommand);
    setupDialog_Query_(d, dlg, url, isSensitive);
    return dlg;
}

iBool handleCommand_Query(iQuery *d, const char *cmd) {
    if (equal_Command(cmd, "document.input.submit") && document_Command(cmd) == d->owner) {
        const iString *url = state_DocumentWidget(d->owner)->url;
        if (hasLabel_Command(cmd, "link")) {
            /* Use the stored base URL (document URL may change). */
            const iGmLinkId linkId = argU32Label_Command(cmd, "link");
            iBool isSensitive;
            const iString *label, *baseUrl;
            int heightPx;
            inputPromptInfo_Media(media_GmDocument(ownerDoc_(d)),
                                  findLinkInputPrompt_Media(media_GmDocument(ownerDoc_(d)), linkId),
                                  &isSensitive, &label, &baseUrl, &heightPx);
            if (baseUrl) {
                url = baseUrl;
            }
        }
        else if (hasLabel_Command(cmd, "prompturl")) {
            url = string_Command(cmd, "prompturl");
        }
        const iString *userEnteredText = collect_String(suffix_Command(cmd, "value"));
        saveSubmittedInput_App(userEnteredText);
        const char *queryUrl = cstrCollect_String(makeQueryUrl_(url, userEnteredText));
        if (hasLabel_Command(cmd, "link")) {
            postCommandf_Root(ownerWidget_(d)->root, "open url:%s", queryUrl);
            /* Don't dismiss the inline prompt yet. The request may be cancelled before a
               response arrives, in which case the prompt remain as is for another request
               attempt. */
            setEnabled_Query(d, argU32Label_Command(cmd, "link"), iFalse);
        }
        else {
            postCommandf_Root(ownerWidget_(d)->root, "open redirect:1 url:%s", queryUrl);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "valueinput.cancelled") &&
             equal_Rangecc(range_Command(cmd, "id"), "!document.input.submit") &&
             !hasLabel_Command(cmd, "prompturl") && !hasLabel_Command(cmd, "link") &&
             document_App() == d->owner) {
        postCommand_Root(get_Root(), "navigate.back");
        return iTrue;
    }
    else if (equal_Command(cmd, "valueinput.cancelled") && hasLabel_Command(cmd, "link") &&
             document_Command(cmd) == d->owner) {
        /* No back-navigation: the original page is still visible underneath. */
        destroy_Query(d, argU32Label_Command(cmd, "link"));
        return iTrue;
    }
    else if (equalWidget_Command(cmd, ownerWidget_(d), "valueinput.resized")) {
        iWidget *bar = as_Widget(pointer_Command(cmd));
        if (bar && startsWith_String(id_Widget(bar), "inputprompt")) {
            const iGmLinkId linkId = (iGmLinkId) atoi(cstr_String(id_Widget(bar)) + 11);
            iMediaId mediaId = findLinkInputPrompt_Media(media_GmDocument(ownerDoc_(d)), linkId);
            if (mediaId.type) {
                setInputPromptHeight_Media(media_GmDocument(ownerDoc_(d)), mediaId, height_Widget(bar));
                refreshAfterChange_Query(d);
                if (document_App() == d->owner) {
                    /* The prompt just resized and may now extend past the viewport. */
                    ensureVisible_Query(d, linkId);
                }
            }
        }
        return iFalse; /* let it also reach the bar's own handler for its internal re-arrange */
    }
    else if (equalWidget_Command(cmd, ownerWidget_(d), "focus.gained")) {
        iWidget *bar = pointer_Command(cmd);
        while (bar && !startsWith_String(id_Widget(bar), "inputprompt")) {
            bar = bar->parent;
        }
        if (bar && document_App() == d->owner) {
            /* Focus should never be (partially or fully) outside the viewable area. */
            ensureVisible_Query(d, (iGmLinkId) atoi(cstr_String(id_Widget(bar)) + 11));
        }
        return iFalse;
    }
    return iFalse;
}
