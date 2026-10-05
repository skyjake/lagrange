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

#include "simpledocumentwidget.h"

#include "app.h"
#include "command.h"
#include "documentwidget.h"
#include "doc/banner.h"
#include "doc/documentview.h"
#include "gmutil.h"
#include "keys.h"
#include "render/text.h"
#include "root.h"
#include "scrollwidget.h"
#include "util.h"
#include "window.h"

struct Impl_SimpleDocumentWidget {
    iWidget        widget;
    iDocumentView *view;
    iBanner       *banner; /* always empty */
    iRangecc       selectMark;
    iRangecc       foundMark;
    iClick         click;
    iScrollWidget *scroll;
};

iDefineObjectConstruction(SimpleDocumentWidget)

static iBool isHoverAllowed_SimpleDocumentWidget_(const iSimpleDocumentWidget *d) {
    return isHover_Widget(constAs_Widget(d));
}

static iScrollWidget *scrollBar_SimpleDocumentWidget_(const iSimpleDocumentWidget *d) {
    return d->scroll;
}

static void refreshWhileScrolling_SimpleDocumentWidget_(iAny *ptr) {
    iSimpleDocumentWidget *d    = ptr;
    iDocumentView         *view = d->view;
    if (flags_Widget(ptr) & destroyPending_WidgetFlag) {
        return;
    }
    updateVisible_DocumentView(view);
    refresh_Widget(d);
    if (view->animWideRunId) {
        for (const iGmRun *r = view->animWideRunRange.start; r != view->animWideRunRange.end; r++) {
            insert_PtrSet(view->invalidRuns, r);
        }
    }
    if (isFinished_Anim(&view->animWideRunOffset)) {
        view->animWideRunId = 0;
    }
    if (!isFinished_SmoothScroll(&view->scrollY) || !isFinished_Anim(&view->animWideRunOffset)) {
        addTicker_App(refreshWhileScrolling_SimpleDocumentWidget_, d);
    }
    else {
        updateHover_DocumentView(view, mouseCoord_Window(get_Window(), 0));
    }
}

static void scrollBegan_SimpleDocumentWidget_(iAnyObject *any, int offset, uint32_t duration) {
    iSimpleDocumentWidget *d = any;
    if (offset) {
        updateVisible_DocumentView(d->view);
        refresh_Widget(d);
        if (duration > 0) {
            addTicker_App(refreshWhileScrolling_SimpleDocumentWidget_, d);
        }
    }
}

static void aboutToScrollView_SimpleDocumentWidget_(iSimpleDocumentWidget *d, int scrollMax) {
    const iRangei visRange = visibleRange_DocumentView(d->view);
    const int     docSize  = pageHeight_DocumentView(d->view);
    setRange_ScrollWidget(d->scroll, (iRangei){ 0, scrollMax });
    setThumb_ScrollWidget(d->scroll,
                          pos_SmoothScroll(&d->view->scrollY),
                          docSize > 0 ? height_Widget(d) * size_Range(&visRange) / docSize : 0);
}

static const iDocumentViewHost viewHost_SimpleDocumentWidget_ = {
    .scrollBar             = (iAny *) scrollBar_SimpleDocumentWidget_,
    .isHoverAllowed        = (iAny *) isHoverAllowed_SimpleDocumentWidget_,
    .isPrerenderingAllowed = (iAny *) isVisible_Widget,
    .refreshWhileScrolling = refreshWhileScrolling_SimpleDocumentWidget_,
    .scrollBegan           = scrollBegan_SimpleDocumentWidget_,
    .aboutToScrollView     = (iAny *) aboutToScrollView_SimpleDocumentWidget_,
};

void init_SimpleDocumentWidget(iSimpleDocumentWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setFlags_Widget(w, hover_WidgetFlag, iTrue);
    d->banner = new_Banner();
    d->view   = new_DocumentView();
    d->selectMark       = iNullRange;
    d->foundMark        = iNullRange;
    d->view->banner     = d->banner;
    d->view->selectMark = &d->selectMark;
    d->view->foundMark  = &d->foundMark;
    d->view->pageMargin = 1;
    d->view->sideMargin = 2;
    setOwner_DocumentView(d->view, d, &viewHost_SimpleDocumentWidget_);
    init_Click(&d->click, d, SDL_BUTTON_LEFT);
    addChild_Widget(w, iClob(d->scroll = new_ScrollWidget()));
    setThumbColor_ScrollWidget(d->scroll, uiTextDim_ColorId);
}

void deinit_SimpleDocumentWidget(iSimpleDocumentWidget *d) {
    delete_DocumentView(d->view);
    delete_Banner(d->banner);
}

iGmDocument *document_SimpleDocumentWidget(iSimpleDocumentWidget *d) {
    return d->view->doc;
}

static void pinPrefs_SimpleDocumentWidget_(iGmDocument *doc) {
    /* Not affected by any scoped prefs. */
    iDocPrefs pinned;
    initCopy_DocPrefs(&pinned, defaults_DocPrefs());
    pinned.pageView.value          = iFalse;
    pinned.lineWidth.value         = 1000; /* fills the widget */
    pinned.sideIcon.value          = iFalse;
    pinned.bigFirstParagraph.value = iFalse;
    pinned.centerShortDocs.value   = iFalse;
    setDocPrefs_GmDocument(doc, &pinned);
    deinit_DocPrefs(&pinned);
}

static void documentChanged_SimpleDocumentWidget_(iSimpleDocumentWidget *d) {
    documentRunsInvalidated_DocumentView(d->view);
    resetScroll_DocumentView(d->view);
    allocVisBuffer_DocumentView(d->view);
    invalidate_DocumentView(d->view);
    updateVisible_DocumentView(d->view);
    refresh_Widget(d);
}

void setSource_SimpleDocumentWidget(iSimpleDocumentWidget *d, const iString *url,
                                    const iString *source) {
    iGmDocument *doc = new_GmDocument();
    setMode_GmDocument(doc, uiTheme_GmDocumentMode);
    pinPrefs_SimpleDocumentWidget_(doc);
    setUrl_GmDocument(doc, url);
    const iGmDocumentMetrics metrics = metrics_DocumentView(d->view);
    iRelease(d->view->doc);
    d->view->doc = doc;
    setSource_GmDocument(doc, source, &metrics, final_GmDocumentUpdate);
    documentChanged_SimpleDocumentWidget_(d);
}

void clear_SimpleDocumentWidget(iSimpleDocumentWidget *d) {
    setSource_SimpleDocumentWidget(d, collectNew_String(), collectNew_String());
}

void scrollToLink_SimpleDocumentWidget(iSimpleDocumentWidget *d, const iString *url,
                                       iBool isSmooth) {
    const iGmDocument *doc      = d->view->doc;
    const iString     *canonUrl = canonicalUrl_String(url);
    const iGmRunRange  runs     = runRange_GmDocument(doc);
    for (const iGmRun *run = runs.start; run != runs.end; run++) {
        if (run->linkId && ~run->flags & decoration_GmRunFlag &&
            equal_String(linkUrl_GmDocument(doc, run->linkId), canonUrl)) {
            if (isSmooth) {
                smoothScrollTo_DocumentView(d->view, mid_Rect(run->visBounds).y, iTrue, 400);
            }
            else {
                scrollTo_DocumentView(d->view, mid_Rect(run->visBounds).y, iTrue);
                refresh_Widget(d);
            }
            return;
        }
    }
}

void redoLayout_SimpleDocumentWidget(iSimpleDocumentWidget *d) {
    redoLayout_GmDocument(d->view->doc);
    documentRunsInvalidated_DocumentView(d->view);
    invalidate_DocumentView(d->view);
    updateVisible_DocumentView(d->view);
    refresh_Widget(d);
}

static void updateWidth_SimpleDocumentWidget_(iSimpleDocumentWidget *d) {
    if (width_Widget(d) <= 0) {
        return;
    }
    if (updateWidth_DocumentView(d->view)) {
        invalidate_DocumentView(d->view);
    }
    allocVisBuffer_DocumentView(d->view);
    updateVisible_DocumentView(d->view);
    refresh_Widget(d);
}

static void openLink_SimpleDocumentWidget_(iSimpleDocumentWidget *d, iGmLinkId linkId) {
    const iString *url = linkUrl_GmDocument(d->view->doc, linkId);
    if (url) {
        postCommandf_Root(as_Widget(d)->root, "open url:%s", cstr_String(url));
    }
}

static iBool processEvent_SimpleDocumentWidget_(iSimpleDocumentWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (isMetricsChange_UserEvent(ev)) {
        updateWidth_SimpleDocumentWidget_(d);
    }
    else if (isCommand_UserEvent(ev, "document.openurls.changed")) {
        /* The current page is highlighted. */
        if (updateOpenURLs_GmDocument(d->view->doc)) {
            invalidate_DocumentView(d->view);
            refresh_Widget(w);
        }
    }
    else if (isCommand_Widget(w, ev, "scroll.moved")) {
        init_Anim(&d->view->scrollY.pos, arg_Command(command_UserEvent(ev)));
        updateVisible_DocumentView(d->view);
        refresh_Widget(w);
        return iTrue;
    }
    else if (isCommand_UserEvent(ev, "theme.changed")) {
        invalidatePalette_GmDocument(d->view->doc);
        redoLayout_SimpleDocumentWidget(d);
    }
    if (ev->type == SDL_EVENT_MOUSE_WHEEL && isHover_Widget(w)) {
        /* Wide preformatted blocks scroll horizontally. */
        const iInt2 mouse = coord_MouseWheelEvent(&ev->wheel);
        if (isPerPixel_MouseWheelEvent(&ev->wheel)) {
            stop_Anim(&d->view->scrollY.pos);
            immediateScroll_DocumentView(d->view, -ev->wheel.y);
            scrollWideBlock_DocumentView(d->view, mouse, -ev->wheel.x, 0, NULL);
        }
        else {
            const int lineHeight = lineHeight_Text(uiContent_FontId);
            if (ev->wheel.x) {
                scrollWideBlock_DocumentView(d->view, mouse, -3 * ev->wheel.x * lineHeight, 167, NULL);
            }
            if (ev->wheel.y) {
                smoothScroll_DocumentView(d->view,
                                          -3 * ev->wheel.y * lineHeight,
                                          prefs_App()->smoothScrolling ? 200 : 0);
            }
        }
        return iTrue;
    }
    if (ev->type == SDL_EVENT_MOUSE_MOTION) {
        updateHover_DocumentView(d->view, init_I2(ev->motion.x, ev->motion.y));
    }
    switch (processEvent_Click(&d->click, ev)) {
        case started_ClickResult:
            return iTrue;
        case finished_ClickResult:
            if (d->view->hoverLink && contains_Widget(w, pos_Click(&d->click))) {
                openLink_SimpleDocumentWidget_(d, d->view->hoverLink->linkId);
            }
            return iTrue;
        case drag_ClickResult:
        case aborted_ClickResult:
            return iTrue;
        default:
            break;
    }
    return processEvent_Widget(w, ev);
}

static void draw_SimpleDocumentWidget_(const iSimpleDocumentWidget *d) {
    const iWidget *w = constAs_Widget(d);
    if (width_Rect(bounds_Widget(w)) <= 0) {
        return;
    }
    draw_DocumentView(d->view, 0);
    /* The global palette belongs to the root's current document. */
    const iDocumentWidget *front = document_Root(w->root);
    if (front) {
        makePaletteGlobal_GmDocument(document_DocumentWidget(front));
    }
    drawChildren_Widget(w);
}

static void sizeChanged_SimpleDocumentWidget_(iSimpleDocumentWidget *d) {
    updateWidth_SimpleDocumentWidget_(d);
}

iBeginDefineSubclass(SimpleDocumentWidget, Widget)
    .processEvent = (iAny *) processEvent_SimpleDocumentWidget_,
    .draw         = (iAny *) draw_SimpleDocumentWidget_,
    .sizeChanged  = (iAny *) sizeChanged_SimpleDocumentWidget_,
iEndDefineSubclass(SimpleDocumentWidget)
