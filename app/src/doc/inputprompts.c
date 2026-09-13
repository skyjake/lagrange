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

#include "inputprompts.h"

#include "../app.h"
#include "../defs.h"
#include "../gmutil.h"
#include "../render/paint.h"
#include "documentview.h"
#include "ui/documentwidget.h"
#include "ui/inputwidget.h"
#include "ui/util.h"
#include "ui/widget.h"

#include <lagrange/gmrequest.h>
#include <lagrange/sitespec.h>

iDefineTypeConstruction(InputPrompts)

static iWidget *ownerWidget_(const iInputPrompts *d) {
    return as_Widget(d->owner);
}

static iDocumentView *ownerView_(const iInputPrompts *d) {
    return view_DocumentWidget(d->owner);
}

static iGmDocument *ownerDoc_(const iInputPrompts *d) {
    return ownerView_(d)->doc;
}

static iWidget *makeBar_InputPrompts_(iInputPrompts *d, iGmLinkId linkId, const iString *url,
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
    setupPromptDialog_DocumentWidget(d->owner, dlg, url, isSensitive);
    arrange_Widget(dlg); /* pick up any height change from restored backup content */
    setId_Widget(dlg, format_CStr("inputprompt%u", linkId)); /* required for lookup */
    return dlg;
}

static iWidget *recreate_InputPrompts_(iInputPrompts *d, iGmLinkId linkId) {
    iMediaId mediaId = findLinkInputPrompt_Media(media_GmDocument(ownerDoc_(d)), linkId);
    if (!mediaId.type) {
        return NULL;
    }
    iBool isSensitive;
    const iString *label, *baseUrl;
    int heightPx;
    inputPromptInfo_Media(media_GmDocument(ownerDoc_(d)), mediaId, &isSensitive, &label, &baseUrl, &heightPx);
    iWidget *bar = makeBar_InputPrompts_(
        d, linkId, baseUrl, isSensitive, label ? cstr_String(label) : NULL);
    postCommand_Widget(bar, "valueinput.resized"); /* reconcile with the (possibly stale) reserved height */
    return bar;
}

static void drawClipped_InputPrompts_(const iPtrArray *bars, const iRect *clipBounds) {
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

void init_InputPrompts(iInputPrompts *d) {
    d->owner = NULL;
    init_PtrArray(&d->outgoing);
    init_PtrArray(&d->covered);
}

void deinit_InputPrompts(iInputPrompts *d) {
    deinit_PtrArray(&d->covered);
    deinit_PtrArray(&d->outgoing);
}

void setOwner_InputPrompts(iInputPrompts *d, iDocumentWidget *owner) {
    d->owner = owner;
}

iWidget *findBar_InputPrompts(const iInputPrompts *d, iGmLinkId linkId) {
    return findChild_Widget(ownerWidget_(d), format_CStr("inputprompt%u", linkId));
}

void destroyAll_InputPrompts(iInputPrompts *d) {
    iForEach(ObjectList, i, children_Widget(ownerWidget_(d))) {
        iWidget *child = i.object;
        if (startsWith_String(id_Widget(child), "inputprompt") &&
            ~child->flags2 & deferredDraw_WidgetFlag2) { /* skip a currently covered bar */
            destroy_Widget(child);
        }
    }
}

void deferOutgoing_InputPrompts(iInputPrompts *d) {
    /* Escape the "inputpromptN" id lookup and take these out of the normal draw pass. */
    iAssert(isEmpty_PtrArray(&d->outgoing));
    iConstForEach(PtrArray, m, &swipeView_DocumentWidget(d->owner)->visibleMedia) {
        const iGmRun *run = m.ptr;
        if (run->mediaType != inputPrompt_MediaType) {
            continue;
        }
        iWidget *bar = findBar_InputPrompts(d, run->linkId);
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
    repositionOutgoing_InputPrompts(d); /* position them before the first draw */
}

iWidget *create_InputPrompts(iInputPrompts *d, iGmLinkId linkId, const iString *url,
                             iBool isSensitive, const iString *promptLabel) {
    /* Creates the bar and records it (and its height) in the document's media cache, so a
       subsequent layout doesn't fall back to a placeholder height. */
    iGmDocument *doc = ownerDoc_(d);
    setInputPrompt_Media(media_GmDocument(doc), linkId, isSensitive, promptLabel, url);
    iWidget *bar = makeBar_InputPrompts_(
        d, linkId, url, isSensitive, promptLabel ? cstr_String(promptLabel) : NULL);
    setInputPromptHeight_Media(
        media_GmDocument(doc), findLinkInputPrompt_Media(media_GmDocument(doc), linkId),
        height_Widget(bar));
    return bar;
}

void refreshAfterChange_InputPrompts(iInputPrompts *d) {
    redoLayout_GmDocument(ownerDoc_(d));
    updateVisible_DocumentView(ownerView_(d));
    invalidate_DocumentWidget(d->owner);
    refresh_Widget(ownerWidget_(d));
}

void destroy_InputPrompts(iInputPrompts *d, iGmLinkId linkId) {
    iWidget *bar = findBar_InputPrompts(d, linkId);
    if (bar) {
        destroy_Widget(bar);
    }
    clearInputPrompt_Media(media_GmDocument(ownerDoc_(d)), linkId);
    refreshAfterChange_InputPrompts(d);
}

void setEnabled_InputPrompts(iInputPrompts *d, iGmLinkId linkId, iBool enabled) {
    iWidget *bar = findBar_InputPrompts(d, linkId);
    if (bar) {
        setTreeFlags_Widget(bar, disabled_WidgetFlag, !enabled);
        refresh_Widget(bar);
    }
}

void reenableAll_InputPrompts(iInputPrompts *d) {
    iForEach(ObjectList, i, children_Widget(ownerWidget_(d))) {
        iWidget *child = i.object;
        if (startsWith_String(id_Widget(child), "inputprompt")) {
            setTreeFlags_Widget(child, disabled_WidgetFlag, iFalse);
            refresh_Widget(child);
        }
    }
}

void ensureVisible_InputPrompts(iInputPrompts *d, iGmLinkId linkId) {
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

void show_InputPrompts(iInputPrompts *d, iGmLinkId linkId, const iString *baseUrl,
                       const iGmResponse *resp, enum iGmStatusCode statusCode) {
    if (findBar_InputPrompts(d, linkId)) {
        destroy_InputPrompts(d, linkId);
    }
    iWidget *bar = create_InputPrompts(
        d, linkId, baseUrl, statusCode == sensitiveInput_GmStatusCode,
        isEmpty_String(&resp->meta) ? NULL : &resp->meta);
    refreshAfterChange_InputPrompts(d);
    if (document_App() == d->owner) {
        /* Only steal focus if this tab is the one being viewed. */
        setFocus_Widget(findChild_Widget(bar, "input"));
        ensureVisible_InputPrompts(d, linkId);
    }
}

void repositionOutgoing_InputPrompts(iInputPrompts *d) {
    const int offset = swipeOffsetOfView_DocumentWidget(d->owner, swipeView_DocumentWidget(d->owner));
    iForEach(PtrArray, i, &d->outgoing) {
        setVisualOffset_Widget(i.ptr, offset, 0, 0);
    }
}

void reposition_InputPrompts(iInputPrompts *d, iDocumentView *view) {
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
        iWidget *bar = findBar_InputPrompts(d, run->linkId);
        if (!bar) {
            /* The widget doesn't survive a document swap, but its state does in Media
               (cf. inline images restored from a cached/history document). */
            bar = recreate_InputPrompts_(d, run->linkId);
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

void drawOutgoing_InputPrompts(const iInputPrompts *d, const iRect *clipBounds) {
    drawClipped_InputPrompts_(&d->outgoing, clipBounds);
}

void drawCovered_InputPrompts(const iInputPrompts *d, const iRect *clipBounds) {
    drawClipped_InputPrompts_(&d->covered, clipBounds);
}

void showForPromptUrls_InputPrompts(iInputPrompts *d) {
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
            create_InputPrompts(d, (iGmLinkId) linkId, absUrl, iFalse, NULL);
            if (!firstLinkId) {
                firstLinkId = (iGmLinkId) linkId;
            }
            didAny = iTrue;
        }
        if (didAny) {
            refreshAfterChange_InputPrompts(d);
            /* Focus the first one only if it's in view -- otherwise it's as disorienting as
               the layout jump this feature avoids. */
            const iGmRun *run = findInputPromptRun_GmDocument(ownerDoc_(d), firstLinkId);
            if (run) {
                const iRangei vis = visibleRange_DocumentView(ownerView_(d));
                if (top_Rect(run->bounds) >= vis.start && bottom_Rect(run->bounds) <= vis.end) {
                    iWidget *bar = findBar_InputPrompts(d, firstLinkId);
                    if (bar) {
                        setFocus_Widget(findChild_Widget(bar, "input"));
                    }
                }
            }
        }
    }
}

void resetAfterSwipe_InputPrompts(iInputPrompts *d) {
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
