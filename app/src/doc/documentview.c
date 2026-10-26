/* Copyright 2023 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "impl.h"
#include "pagedview.h"

#include "app.h"
#include "banner.h"
#include "defs.h"
#include "media/media.h"
#include "render/text.h"
#include "ui/root.h"
#include "ui/util.h"
#include "ui/window.h"

#include <the_Foundation/ptrarray.h>
#include <SDL3/SDL_render.h>

/*----------------------------------------------------------------------------------------------*/

static void init_DrawBufs(iDrawBufs *d) {
    d->flags = 0;
    d->sideIconBuf = NULL;
    iZap(d->curlBufs);
    d->timestampBuf = NULL;
    d->lastRenderTime = 0;
}

static void deinit_DrawBufs(iDrawBufs *d) {
    delete_TextBuf(d->timestampBuf);
    if (d->sideIconBuf) {
        SDL_DestroyTexture(d->sideIconBuf);
    }
    iForIndices(i, d->curlBufs) {
        if (d->curlBufs[i]) {
            SDL_DestroyTexture(d->curlBufs[i]);
        }
    }
}

iDefineTypeConstruction(DrawBufs)

/*----------------------------------------------------------------------------------------------*/

static void visBufInvalidated_(iVisBuf *d, size_t index) {
    iVisBufMeta *meta = d->buffers[index].user;
    iZap(meta->runsDrawn);
}

/*----------------------------------------------------------------------------------------------*/

iDefineTypeConstruction(DocumentView)

void init_DocumentView(iDocumentView *d) {
    d->owner         = NULL;
    d->host          = NULL;
    d->doc           = new_GmDocument();
    d->invalidRuns   = new_PtrSet();
    d->drawBufs      = new_DrawBufs();
    d->pageMargin    = 5;
    d->sideMargin    = 0;
    d->hoverPre      = NULL;
    d->hoverAltPre   = NULL;
    d->hoverLink     = NULL;
    d->hoverKeyLink  = NULL;
    d->animWideRunId = 0;
    init_Anim(&d->animWideRunOffset, 0);
    iZap(d->renderRuns);
    iZap(d->visibleRuns);
    d->visBuf = new_VisBuf(); {
        d->visBufMeta = malloc(sizeof(iVisBufMeta) * maxPages_VisBuf);
        /* Additional metadata for each buffer. */
        d->visBuf->bufferInvalidated = visBufInvalidated_;
        for (size_t i = 0; i < maxPages_VisBuf; i++) {
            d->visBuf->buffers[i].user = d->visBufMeta + i;
        }
    }
    init_Anim(&d->sideOpacity, 0);
    init_Anim(&d->altTextOpacity, 0);
    init_PtrArray(&d->visibleLinks);
    init_PtrArray(&d->visiblePre);
    init_PtrArray(&d->visibleWideRuns);
    init_Array(&d->wideRunOffsets, sizeof(int));
    init_PtrArray(&d->visibleMedia);
    d->userHasScrolled = iFalse;
    iZap(d->paged);
    d->paged.anchor = iInvalidPos;
    init_Anim(&d->paged.turnOffset, 0);
}

void deinit_DocumentView(iDocumentView *d) {
    removeTicker_App(prerender_DocumentView, d);
    removeTicker_App(animatePageTurn_DocumentView, d);
    delete_DrawBufs(d->drawBufs);
    delete_VisBuf(d->visBuf);
    free(d->visBufMeta);
    delete_PtrSet(d->invalidRuns);
    deinit_Array(&d->wideRunOffsets);
    deinit_PtrArray(&d->visibleMedia);
    deinit_PtrArray(&d->visibleWideRuns);
    deinit_PtrArray(&d->visiblePre);
    deinit_PtrArray(&d->visibleLinks);
    iReleasePtr(&d->doc);
}

/*----------------------------------------------------------------------------------------------*/
/* Calls to the host widget. */

iWidget *hostFooterButtons_DocumentView(const iDocumentView *d) {
    return d->host && d->host->footerButtons ? d->host->footerButtons(d->owner) : NULL;
}

static int hostFooterHeight_DocumentView_(const iDocumentView *d) {
    return d->host && d->host->footerHeight ? d->host->footerHeight(d->owner) : 0;
}

static int hostPhoneToolbarHeight_DocumentView_(const iDocumentView *d) {
    return d->host && d->host->phoneToolbarHeight ? d->host->phoneToolbarHeight(d->owner) : 0;
}

int hostFooterClearance_DocumentView(const iDocumentView *d) {
    return d->host && d->host->footerClearance ? d->host->footerClearance(d->owner) : 0;
}

int hostPhoneBarsHeight_DocumentView(const iDocumentView *d) {
    return hostPhoneToolbarHeight_DocumentView_(d) +
           (d->host && d->host->phoneBottomNavbarHeight ? d->host->phoneBottomNavbarHeight(d->owner)
                                                        : 0);
}

iScrollWidget *hostScrollBar_DocumentView(const iDocumentView *d) {
    return d->host && d->host->scrollBar ? d->host->scrollBar(d->owner) : NULL;
}

static iBool hostIsHoverAllowed_DocumentView_(const iDocumentView *d) {
    return d->host && d->host->isHoverAllowed ? d->host->isHoverAllowed(d->owner) : iTrue;
}

static iBool hostNoHoverWhileScrolling_DocumentView_(const iDocumentView *d) {
    return d->host && d->host->noHoverWhileScrolling ? d->host->noHoverWhileScrolling(d->owner)
                                                     : iFalse;
}

static enum iWheelSwipeState hostWheelSwipeState_DocumentView_(const iDocumentView *d) {
    return d->host && d->host->wheelSwipeState ? d->host->wheelSwipeState(d->owner)
                                               : none_WheelSwipeState;
}

static void hostAnimate_DocumentView_(iDocumentView *d) {
    if (d->host && d->host->animate) {
        d->host->animate(d->owner);
    }
    else {
        refresh_Widget(d->owner);
    }
}

static void hostRefreshWhileScrolling_DocumentView_(iDocumentView *d) {
    if (d->host && d->host->refreshWhileScrolling) {
        addTicker_App(d->host->refreshWhileScrolling, d->owner);
    }
    else {
        refresh_Widget(d->owner);
    }
}

static void hostAboutToScrollView_DocumentView_(iDocumentView *d, int scrollMax) {
    if (d->host && d->host->aboutToScrollView) {
        d->host->aboutToScrollView(d->owner, scrollMax);
    }
}

static void hostDidScrollView_DocumentView_(iDocumentView *d) {
    if (d->host && d->host->didScrollView) {
        d->host->didScrollView(d->owner);
    }
}

static void hostDocumentRunsInvalidated_DocumentView_(iDocumentView *d) {
    if (d->host && d->host->documentRunsInvalidated) {
        d->host->documentRunsInvalidated(d->owner);
    }
    else {
        documentRunsInvalidated_DocumentView(d);
    }
}

static void hostRepositionInlinePrompts_DocumentView_(iDocumentView *d) {
    if (d->host && d->host->repositionInlinePrompts) {
        d->host->repositionInlinePrompts(d->owner, d);
    }
}

static void hostUpdateHoverLinkInfo_DocumentView_(iDocumentView *d, uint16_t linkId) {
    if (d->host && d->host->updateHoverLinkInfo) {
        d->host->updateHoverLinkInfo(d->owner, d, linkId);
    }
}

void hostPagesMoved_DocumentView(iDocumentView *d) {
    if (d->host && d->host->pagesMoved) {
        d->host->pagesMoved(d->owner);
    }
}

void hostPageTurnBegins_DocumentView(iDocumentView *d) {
    if (d->host && d->host->pageTurnBegins) {
        d->host->pageTurnBegins(d->owner);
    }
}

/*----------------------------------------------------------------------------------------------*/

void setOwner_DocumentView(iDocumentView *d, iAnyObject *owner, const iDocumentViewHost *host) {
    d->owner = owner;
    d->host  = host;
    d->userHasScrolled = iFalse;
    init_SmoothScroll(&d->scrollY, owner, host ? host->scrollBegan : NULL);
    if (deviceType_App() != desktop_AppDeviceType) {
        d->scrollY.flags |= pullDownAction_SmoothScrollFlag; /* pull to refresh */
    }
}

int runOffset_DocumentView(const iDocumentView *d, const iGmRun *run) {
    if (preId_GmRun(run) && run->flags & wide_GmRunFlag) {
        if (d->animWideRunId == preId_GmRun(run)) {
            return -value_Anim(&d->animWideRunOffset);
        }
        const size_t numOffsets = size_Array(&d->wideRunOffsets);
        const int *offsets = constData_Array(&d->wideRunOffsets);
        if (preId_GmRun(run) <= numOffsets) {
            return -offsets[preId_GmRun(run) - 1];
        }
    }
    return 0;
}

static int *wideRunOffset_DocumentView_(iDocumentView *d, uint16_t preId) {
    if (size_Array(&d->wideRunOffsets) < preId) {
        resize_Array(&d->wideRunOffsets, preId);
    }
    return at_Array(&d->wideRunOffsets, preId - 1);
}

void resetWideRuns_DocumentView(iDocumentView *d) {
    clear_Array(&d->wideRunOffsets);
    for (size_t i = 0; i < numPre_GmDocument(d->doc); i++) {
        const uint16_t    preId = i + 1;
        const iGmPreMeta *meta  = preMeta_GmDocument(d->doc, preId);
        if (meta->initialOffset) {
            *wideRunOffset_DocumentView_(d, preId) = meta->initialOffset;
        }
    }
    d->animWideRunId = 0;
    init_Anim(&d->animWideRunOffset, 0);
    iZap(d->animWideRunRange);
}

void invalidateAndResetWideRunsWithNonzeroOffset_DocumentView(iDocumentView *d) {
    iConstForEach(PtrArray, i, &d->visibleWideRuns) {
        const iGmRun *run = i.ptr;
        if (runOffset_DocumentView(d, run)) {
            insert_PtrSet(d->invalidRuns, run);
        }
    }
    resetWideRuns_DocumentView(d);
}

int maxDocumentWidth_DocumentView(const iDocumentView *d) {
    const iWidget *w        = constAs_Widget(d->owner);
    const iRect    bounds   = bounds_Widget(w);
    const int      minWidth = 50 * gap_UI * aspect_UI; /* lines must fit a word at least */
    const float    adjust =
        iClamp((float) bounds.size.x / gap_UI / 11 - 12,
               isDesktop_Platform() && !isTerminal_Platform() ? 2.0f /* no contraction */ : -1.0f,
               10.0f); /* adapt to width */
    return iMax(minWidth, bounds.size.x - gap_UI * (d->pageMargin + d->sideMargin + adjust) * 2);
}

const iDocPrefs *docPrefs_DocumentView(const iDocumentView *d) {
    return d->doc ? docPrefs_GmDocument(d->doc) : activeDocPrefs_App();
}

int documentWidth_DocumentView(const iDocumentView *d) {
    if (wantsPaged_DocumentView(d)) {
        return pagedDocumentWidth_DocumentView(d);
    }
    int prefsWidth = docPrefs_DocumentView(d)->lineWidth.value;
    if (isTerminal_Platform()) {
        prefsWidth /= aspect_UI * 0.8f;
    }
    /* The zoom level follows the fonts in use. */
    return iMini(fontSize_UI * prefsWidth * activeDocPrefs_App()->zoomPercent.value / 100,
                 maxDocumentWidth_DocumentView(d));
}

static iBool updatePagedLayout_DocumentView_(iDocumentView *d, iBool force);

iGmDocumentMetrics metrics_DocumentView(const iDocumentView *d) {
    iGmDocumentMetrics metrics = {
        .width           = documentWidth_DocumentView(d),
        .canvasWidth     = width_Widget(d->owner),
        .maxContentWidth = maxDocumentWidth_DocumentView(d),
    };
    if (wantsPaged_DocumentView(d)) {
        pagedMetrics_DocumentView(d, &metrics);
        if (!isEmpty_Banner(d->banner)) {
            metrics.firstPageInset = height_Banner(d->banner) + documentTopPad_DocumentView(d);
        }
    }
    return metrics;
}

int documentTopPad_DocumentView(const iDocumentView *d) {
    /* Amount of space between banner and top of the document. */
    return isEmpty_Banner(d->banner) ? 0 : lineHeight_Text(paragraph_FontId);
}

static int documentTopMargin_DocumentView_(const iDocumentView *d) {
    return (isEmpty_Banner(d->banner) ? d->pageMargin * gap_UI : height_Banner(d->banner)) +
           documentTopPad_DocumentView(d);
}

int pageHeight_DocumentView(const iDocumentView *d) {
    return height_Banner(d->banner) + documentTopPad_DocumentView(d) + size_GmDocument(d->doc).y;
}

iRect documentBounds_DocumentView(const iDocumentView *d) {
    if (isPaged_DocumentView(d)) {
        /* Document area of the first page of the spread. */
        const iRect viewport = pageViewport_DocumentView(d);
        return (iRect){ init_I2(columnOrigin_DocumentView(d, d->paged.column).x, top_Rect(viewport)),
                        init_I2(size_GmDocument(d->doc).x, height_Rect(viewport)) };
    }
    const iRect bounds = bounds_Widget(constAs_Widget(d->owner));
    const int   margin = gap_UI * d->pageMargin;
    iRect       rect;
    iBool       wasCentered = iFalse;
    rect.size.x = documentWidth_DocumentView(d);
    /* Content may not be always wrappable, so let it extend to window width if needed. */
    if (docPrefs_DocumentView(d)->expandToLongLines.value &&
        contentWidth_GmDocument(d->doc) > size_GmDocument(d->doc).x &&
        (/* TODO: compare to maxContentWidth! */
         /* Do we allow content to expand? On regular Gemtext pages, expanding too much
            may produce an asymmetric layout: consider a long page with one wide
            preformatted block somewhere in the middle; until that block is seen, the
            page layout looks broken if it's shifted too much to the left. */
         viewFormat_GmDocument(d->doc) == plainText_SourceFormat ||
         isGopherMenu_GmDocument(d->doc) ||
         contentWidth_GmDocument(d->doc) < size_GmDocument(d->doc).x * 1.333f)) { /* < ⅓ increase */
        rect.size.x = iMini(iMax(rect.size.x, contentWidth_GmDocument(d->doc)),
                            maxDocumentWidth_DocumentView(d));
    }
    rect.pos.x  = mid_Rect(bounds).x - rect.size.x / 2;
    rect.pos.y  = top_Rect(bounds) + margin;
    rect.size.y = height_Rect(bounds) - margin;
    const iWidget *footerButtons = hostFooterButtons_DocumentView(d);
    /* TODO: Further separation of View and Widget: configure header and footer heights
       without involving the widget here. */
    if (d->flags & centerVertically_DocumentViewFlag) {
        const int docSize = documentTopMargin_DocumentView_(d) + size_GmDocument(d->doc).y;
        if (size_GmDocument(d->doc).y == 0) {
            /* Document is empty; maybe just showing an error banner. */
            rect.pos.y = top_Rect(bounds) + height_Rect(bounds) / 2 -
                         documentTopPad_DocumentView(d) - height_Banner(d->banner) / 2;
            rect.size.y = 0;
            wasCentered = iTrue;
        }
        else if (docSize + height_Widget(hostFooterButtons_DocumentView(d)) < rect.size.y) {
            /* Center vertically when the document is short. */
            const int relMidY   = (height_Rect(bounds) -
                                   height_Widget(footerButtons) -
                                   hostPhoneToolbarHeight_DocumentView_(d)) / 2;
            const int visHeight = size_GmDocument(d->doc).y +
                                  height_Widget(footerButtons);
            const int offset    = -height_Banner(d->banner) -
                                  documentTopPad_DocumentView(d) +
                                  height_Widget(footerButtons);
            rect.pos.y  = top_Rect(bounds) + iMaxi(0, relMidY - visHeight / 2 + offset);
            rect.size.y = size_GmDocument(d->doc).y + documentTopMargin_DocumentView_(d);
            wasCentered = iTrue;
        }
    }
    if (!wasCentered) {
        /* The banner overtakes the top margin. */
        if (!isEmpty_Banner(d->banner)) {
            rect.pos.y -= margin;
        }
        else {
            rect.size.y -= margin;
        }
    }
    return rect;
}

int viewPos_DocumentView(const iDocumentView *d) {
    return height_Banner(d->banner) + documentTopPad_DocumentView(d) -
           pos_SmoothScroll(&d->scrollY);
}

static iInt2 documentPos_DocumentView_(const iDocumentView *d, iInt2 pos) {
    if (isPaged_DocumentView(d)) {
        return pagedDocumentPos_DocumentView(d, pos);
    }
    return addY_I2(sub_I2(pos, topLeft_Rect(documentBounds_DocumentView(d))),
                   -viewPos_DocumentView(d));
}

iRangei visibleRange_DocumentView(const iDocumentView *d) {
    if (isPaged_DocumentView(d)) {
        const iRangei cols       = visibleColumns_DocumentView(d);
        const int     pageHeight = pageHeight_GmDocument(d->doc);
        return (iRangei){ cols.start * pageHeight, cols.end * pageHeight };
    }
    int top = pos_SmoothScroll(&d->scrollY) - height_Banner(d->banner) -
              documentTopPad_DocumentView(d);
    if (isEmpty_Banner(d->banner)) {
        /* Top padding is not collapsed. */
        top -= d->pageMargin * gap_UI;
    }
    return (iRangei){ top, top + height_Rect(bounds_Widget(constAs_Widget(d->owner))) };
}

static void addVisible_DocumentView_(void *context, const iGmRun *run) {
    iDocumentView *d = context;
    if (~run->flags & decoration_GmRunFlag && !run->mediaId) {
        if (!d->visibleRuns.start) {
            d->visibleRuns.start = run;
        }
        d->visibleRuns.end = run;
    }
    if (preId_GmRun(run)) {
        pushBack_PtrArray(&d->visiblePre, run);
        if (run->flags & wide_GmRunFlag) {
            pushBack_PtrArray(&d->visibleWideRuns, run);
        }
    }
    /* Image runs are static so they're drawn as part of the content. */
    if (isMedia_GmRun(run) && run->mediaType != image_MediaType) {
        iAssert(run->mediaId);
        pushBack_PtrArray(&d->visibleMedia, run);
    }
    if (run->linkId) {
        pushBack_PtrArray(&d->visibleLinks, run);
    }
}

const iGmRun *lastVisibleLink_DocumentView(const iDocumentView *d) {
    iReverseConstForEach(PtrArray, i, &d->visibleLinks) {
        const iGmRun *run = i.ptr;
        if (run->flags & decoration_GmRunFlag && run->linkId) {
            return run;
        }
    }
    return NULL;
}

static int scrollMax_DocumentView_(const iDocumentView *d) {
    if (isPaged_DocumentView(d)) {
        return 0; /* pages are turned instead */
    }
    const iWidget *w = constAs_Widget(d->owner);
    int sm = pageHeight_DocumentView(d) +
             (isEmpty_Banner(d->banner) ? 2 : 1) * d->pageMargin * gap_UI + /* top and bottom margins */
             hostFooterHeight_DocumentView_(d) - height_Rect(bounds_Widget(w));
    return iMax(0, sm);
}

float normScrollPos_DocumentView(const iDocumentView *d) {
    if (isPaged_DocumentView(d)) {
        return sourcePosition_DocumentView(d);
    }
    const int height = pageHeight_DocumentView(d);
    if (height > 0) {
        float pos = pos_SmoothScroll(&d->scrollY) / (float) height;
        return iMax(pos, 0.0f);
    }
    return 0;
}

void invalidateLink_DocumentView(iDocumentView *d, iGmLinkId id) {
    /* A link has multiple runs associated with it. */
    iConstForEach(PtrArray, i, &d->visibleLinks) {
        const iGmRun *run = i.ptr;
        if (run->linkId == id) {
            insert_PtrSet(d->invalidRuns, run);
        }
    }
}

void invalidateVisibleLinks_DocumentView(iDocumentView *d) {
    iConstForEach(PtrArray, i, &d->visibleLinks) {
        const iGmRun *run = i.ptr;
        if (run->linkId) {
            insert_PtrSet(d->invalidRuns, run);
        }
    }
}

void updateHoverLinkInfo_DocumentView(iDocumentView *d) {
    hostUpdateHoverLinkInfo_DocumentView_(d, d->hoverLink ? d->hoverLink->linkId : 0);
}

static void unhover_DocumentView_(iDocumentView *d) {
    if (d->hoverLink) {
        invalidateLink_DocumentView(d, d->hoverLink->linkId);
        d->hoverLink = NULL;
        updateHoverLinkInfo_DocumentView(d);
    }
    d->hoverKeyLink = NULL;
}

void updateHover_DocumentView(iDocumentView *d, iInt2 mouse) {
    const iWidget *w            = constAs_Widget(d->owner);
    const iRect    docBounds    = documentBounds_DocumentView(d);
    const iGmRun * oldHoverLink = d->hoverLink;
    d->hoverPre          = NULL;
    d->hoverLink         = NULL;
    const iInt2 hoverPos = documentPos_DocumentView_(d, mouse);
    const iGmRun *selectableRun = NULL;
    if (hostIsHoverAllowed_DocumentView_(d)) {
        /* Look for any selectable text run. */
        for (const iGmRun *v = d->visibleRuns.start; v && v != d->visibleRuns.end; v++) {
            if (~v->flags & decoration_GmRunFlag && !isEmpty_Range(&v->text) &&
                contains_Rect(v->bounds, hoverPos)) {
                selectableRun = v;
                break;
            }
        }
        iConstForEach(PtrArray, i, &d->visibleLinks) {
            const iGmRun *run = i.ptr;
            const iGmRun *precedingRun = precedingRun_GmDocument(d->doc, run);
            iRect linkBounds = run->bounds;
            /* Include the link icon in the interactable region. */
            if (precedingRun && precedingRun->flags & decoration_GmRunFlag &&
                precedingRun->flags & startOfLine_GmRunFlag &&
                precedingRun->linkId == run->linkId) {
                linkBounds = union_Rect(linkBounds, precedingRun->visBounds);
            }
            /* Click targets are slightly expanded so there are no gaps between links. */
            if (contains_Rect(expanded_Rect(linkBounds, init1_I2(gap_Text / 2)), hoverPos)) {
                d->hoverLink = run;
                break;
            }
        }
    }
    if (d->hoverLink != oldHoverLink) {
        if (oldHoverLink) {
            invalidateLink_DocumentView(d, oldHoverLink->linkId);
        }
        if (d->hoverLink) {
            invalidateLink_DocumentView(d, d->hoverLink->linkId);
        }
        updateHoverLinkInfo_DocumentView(d);
        refresh_Widget(w);
    }
    /* Hovering over preformatted blocks. */
    if (hostIsHoverAllowed_DocumentView_(d) && contains_Widget(w, mouse)) {
        iConstForEach(PtrArray, j, &d->visiblePre) {
            const iGmRun *run = j.ptr;
            if (contains_Rangei(ySpan_Rect(run->bounds), hoverPos.y) &&
                (run->flags & wide_GmRunFlag ||
                 contains_Rangei((iRangei){ 0, width_Rect(docBounds) }, hoverPos.x))) {
                d->hoverPre    = run;
                d->hoverAltPre = run;
                break;
            }
        }
    }
    if (!d->hoverPre) {
        setValueSpeed_Anim(&d->altTextOpacity, 0.0f, 1.5f);
        if (!isFinished_Anim(&d->altTextOpacity)) {
            hostAnimate_DocumentView_(d);
        }
    }
    else if (d->hoverPre &&
             preHasAltText_GmDocument(d->doc, preId_GmRun(d->hoverPre)) &&
             !hostNoHoverWhileScrolling_DocumentView_(d)) {
        setValueSpeed_Anim(&d->altTextOpacity, 1.0f, 1.5f);
        if (!isFinished_Anim(&d->altTextOpacity)) {
            hostAnimate_DocumentView_(d);
        }
    }
    const iScrollWidget *scrollBar = hostScrollBar_DocumentView(d);
    if (isHover_Widget(w) && !(scrollBar && contains_Widget(constAs_Widget(scrollBar), mouse))) {
        const iBool isClickablePre =
            d->hoverPre && (!isPaged_DocumentView(d) ||
                            preIsFolded_GmDocument(d->doc, preId_GmRun(d->hoverPre)));
        setCursor_Window(get_Window(),
                         d->hoverLink || isClickablePre ? SDL_SYSTEM_CURSOR_POINTER
                         : selectableRun                ? SDL_SYSTEM_CURSOR_TEXT
                                                        : SDL_SYSTEM_CURSOR_DEFAULT);
        if (d->hoverLink &&
            linkFlags_GmDocument(d->doc, d->hoverLink->linkId) & permanent_GmLinkFlag) {
            setCursor_Window(get_Window(), SDL_SYSTEM_CURSOR_DEFAULT); /* not dismissable */
        }
    }
}

void updateSideOpacity_DocumentView(iDocumentView *d, iBool isAnimated) {
    float opacity = 0.0f;
    if (!isPaged_DocumentView(d) && !isEmpty_Banner(d->banner) &&
        height_Banner(d->banner) < pos_SmoothScroll(&d->scrollY)) {
        opacity = 1.0f;
    }
    setValue_Anim(&d->sideOpacity, opacity, isAnimated ? (opacity < 0.5f ? 100 : 200) : 0);
    hostAnimate_DocumentView_(d);
}

iRangecc currentHeading_DocumentView(const iDocumentView *d) {
    iRangecc heading = iNullRange;
    if (d->visibleRuns.start) {
        iConstForEach(Array, i, headings_GmDocument(d->doc)) {
            const iGmHeading *head = i.value;
            if (head->level == 0) {
                if (head->text.start <= d->visibleRuns.start->text.start) {
                    heading = head->text;
                }
                if (d->visibleRuns.end && head->text.start > d->visibleRuns.end->text.start) {
                    break;
                }
            }
        }
    }
    return heading;
}

int updateScrollMax_DocumentView(iDocumentView *d) {
    arrange_Widget(hostFooterButtons_DocumentView(d)); /* scrollMax depends on footer height */
    const int scrollMax = scrollMax_DocumentView_(d);
    setMax_SmoothScroll(&d->scrollY, scrollMax);
    return scrollMax;
}

void updateVisible_DocumentView(iDocumentView *d) {
    if (isPaged_DocumentView(d) && updatePagedLayout_DocumentView_(d, iFalse)) {
        return; /* e.g., banner height changed */
    }
    const int scrollMax = updateScrollMax_DocumentView(d);
    hostAboutToScrollView_DocumentView_(d, scrollMax); /* TODO: A widget may have many views. */
    clear_PtrArray(&d->visibleLinks);
    clear_PtrArray(&d->visibleWideRuns);
    clear_PtrArray(&d->visiblePre);
    clear_PtrArray(&d->visibleMedia);
    const iRangei  visRange   = visibleRange_DocumentView(d);
    const iRangecc oldHeading = currentHeading_DocumentView(d);
    /* Scan for visible runs. */ {
        iZap(d->visibleRuns);
        render_GmDocument(d->doc, visRange, addVisible_DocumentView_, d);
    }
    /* Must come after the scan: this may reposition inline prompts using `visibleMedia`. */
    unhover_DocumentView_(d);
    hostRepositionInlinePrompts_DocumentView_(d); /* uses the visibleMedia scan above */
    const iRangecc newHeading = currentHeading_DocumentView(d);
    if (memcmp(&oldHeading, &newHeading, sizeof(oldHeading))) {
        d->drawBufs->flags |= updateSideBuf_DrawBufsFlag;
    }
    updateHover_DocumentView(d, mouseCoord_Window(get_Window(), 0));
    updateSideOpacity_DocumentView(d, iTrue);
    hostDidScrollView_DocumentView_(d);
}

void updateDrawBufs_DocumentView(iDocumentView *d, int drawBufsFlags) {
    d->drawBufs->flags |= drawBufsFlags;
}

void swap_DocumentView(iDocumentView *d, iDocumentView *swapBuffersWith) {
    /* TODO: This must go! Views should not be swapped between widgets! */
    d->scrollY        = swapBuffersWith->scrollY;
    d->scrollY.widget = as_Widget(d->owner);
    iSwap(iVisBuf *,     d->visBuf,     swapBuffersWith->visBuf);
    iSwap(iVisBufMeta *, d->visBufMeta, swapBuffersWith->visBufMeta);
    iSwap(iDrawBufs *,   d->drawBufs,   swapBuffersWith->drawBufs);
    updateVisible_DocumentView(d);
    updateVisible_DocumentView(swapBuffersWith);
}

void invalidate_DocumentView(iDocumentView *d) {
    invalidate_VisBuf(d->visBuf);
    clear_PtrSet(d->invalidRuns);
}

void documentRunsInvalidated_DocumentView(iDocumentView *d) {
    /* Note: Don't call this only, the owner widget keeps pointers, too. */
    d->hoverPre     = NULL;
    d->hoverAltPre  = NULL;
    d->hoverLink    = NULL;
    d->hoverKeyLink = NULL;
    clear_PtrArray(&d->visibleMedia);
    iZap(d->visibleRuns);
    iZap(d->renderRuns);
}

static void rebaseRuns_PtrArray_(iPtrArray *runs, const iGmRebase *rebase) {
    iForEach(PtrArray, i, runs) {
        const iGmRun *run = i.ptr;
        applyRun_GmRebase(rebase, &run);
        set_PtrArray(runs, index_PtrArrayIterator(&i), (void *) run);
    }
}

void documentAppended_DocumentView(iDocumentView *d, const iGmRebase *rebase) {
    /* Runs were added at the end, and the existing ones may have moved in memory. */
    applyRunRange_GmRebase(rebase, &d->visibleRuns);
    applyRunRange_GmRebase(rebase, &d->renderRuns);
    applyRunRange_GmRebase(rebase, &d->animWideRunRange);
    applyRun_GmRebase(rebase, &d->hoverPre);
    applyRun_GmRebase(rebase, &d->hoverAltPre);
    applyRun_GmRebase(rebase, &d->hoverLink);
    applyRun_GmRebase(rebase, &d->hoverKeyLink);
    rebaseRuns_PtrArray_(&d->visibleLinks, rebase);
    rebaseRuns_PtrArray_(&d->visiblePre, rebase);
    rebaseRuns_PtrArray_(&d->visibleMedia, rebase);
    rebaseRuns_PtrArray_(&d->visibleWideRuns, rebase);
    iForEach(PtrSet, r, d->invalidRuns) {
        applyRun_GmRebase(rebase, (const iGmRun **) r.value); /* order is unchanged */
    }
    const iGmRunRange runs     = runRange_GmDocument(d->doc);
    const size_t      oldCount = ((uintptr_t) rebase->oldRuns.end -
                                  (uintptr_t) rebase->oldRuns.start) / sizeof(iGmRun);
    iVisBuf *visBuf = d->visBuf;
    for (size_t i = 0; i < visBuf->numBuffers; i++) {
        applyRunRange_GmRebase(rebase, &((iVisBufMeta *) visBuf->buffers[i].user)->runsDrawn);
    }
    if (rebase->dy || rebase->numInserted) {
        /* The old runs moved along with the scroll position. The inserted runs are above
           them, where nothing has been drawn. */
        const int dy  = rebase->dy;
        const int top = oldCount ? top_Rect(rebase->newRuns->visBounds) : INT_MIN;
        visBuf->vis.start += dy;
        visBuf->vis.end   += dy;
        for (size_t i = 0; i < visBuf->numBuffers; i++) {
            iVisBufTexture *buf  = &visBuf->buffers[i];
            iVisBufMeta    *meta = buf->user;
            buf->origin += dy;
            if (!isEmpty_Rangei(buf->validRange)) {
                buf->validRange.start = iMax(buf->validRange.start + dy, top);
                buf->validRange.end  += dy;
                if (isEmpty_Rangei(buf->validRange)) {
                    buf->validRange = (iRangei){ 0, 0 };
                    iZap(meta->runsDrawn);
                }
            }
        }
    }
    if (oldCount >= (size_t) (runs.end - rebase->newRuns)) {
        return;
    }
    const iGmRun *firstNew = rebase->newRuns + oldCount;
    int newTop = INT_MAX;
    for (const iGmRun *run = firstNew; run < runs.end; run++) {
        newTop = iMin(newTop, top_Rect(run->visBounds));
    }
    /* Continue drawing buffers into the area of the new runs. */
    for (size_t i = 0; i < visBuf->numBuffers; i++) {
        iVisBufTexture *buf  = &visBuf->buffers[i];
        iVisBufMeta    *meta = buf->user;
        if (isEmpty_Rangei(buf->validRange) || buf->validRange.end <= newTop) {
            continue;
        }
        if (!meta->runsDrawn.start || buf->validRange.start >= newTop) {
            buf->validRange = (iRangei){ 0, 0 };
            iZap(meta->runsDrawn);
            continue;
        }
        if (!meta->runsDrawn.end) {
            meta->runsDrawn.end = firstNew;
        }
        buf->validRange.end = newTop;
    }
}

void moveScroll_DocumentView(iDocumentView *d, int dy) {
    /* The document moved, so the view follows without animating. */
    d->scrollY.pos.from += dy;
    d->scrollY.pos.to   += dy;
}

void resetScroll_DocumentView(iDocumentView *d) {
    reset_SmoothScroll(&d->scrollY);
    resetPage_DocumentView(d);
    d->userHasScrolled = iFalse;
    init_Anim(&d->sideOpacity, 0);
    init_Anim(&d->altTextOpacity, 0);
    resetWideRuns_DocumentView(d);
}

iBool updateWidth_DocumentView(iDocumentView *d) {
    const iGmDocumentMetrics metrics = metrics_DocumentView(d);
    if (updateMetrics_GmDocument(d->doc, &metrics)) {
        documentRunsInvalidated_DocumentView(d); /* GmRuns reallocated */
        return iTrue;
    }
    return iFalse;
}

void clampScroll_DocumentView(iDocumentView *d) {
    move_SmoothScroll(&d->scrollY, 0);
}

void immediateScroll_DocumentView(iDocumentView *d, int offset) {
    move_SmoothScroll(&d->scrollY, offset);
    d->userHasScrolled = iTrue;
}

void smoothScroll_DocumentView(iDocumentView *d, int offset, int duration) {
    moveSpan_SmoothScroll(&d->scrollY, offset, duration);
    d->userHasScrolled = iTrue;
}

static int scrollPos_DocumentView_(iDocumentView *d, int documentY, iBool centered) {
    if (!isEmpty_Banner(d->banner)) {
        documentY += height_Banner(d->banner) + documentTopPad_DocumentView(d);
    }
    else {
        documentY += documentTopPad_DocumentView(d) + d->pageMargin * gap_UI;
    }
    /* Not into the overscroll area. */
    return iClamp(documentY - (centered ? documentBounds_DocumentView(d).size.y / 2
                                        : lineHeight_Text(paragraph_FontId)),
                  0,
                  updateScrollMax_DocumentView(d));
}

void scrollTo_DocumentView(iDocumentView *d, int documentY, iBool centered) {
    if (isPaged_DocumentView(d)) {
        showDocumentY_DocumentView(d, documentY);
        return;
    }
    init_Anim(&d->scrollY.pos, scrollPos_DocumentView_(d, documentY, centered));
    clampScroll_DocumentView(d);
    updateVisible_DocumentView(d);
}

void smoothScrollTo_DocumentView(iDocumentView *d, int documentY, iBool centered, int duration) {
    if (isPaged_DocumentView(d)) {
        showDocumentY_DocumentView(d, documentY);
        return;
    }
    smoothScroll_DocumentView(d,
                              scrollPos_DocumentView_(d, documentY, centered) -
                                  iRound(targetValue_Anim(&d->scrollY.pos)),
                              duration);
}

void scrollToHeading_DocumentView(iDocumentView *d, const char *heading) {
    /* Try an exact match first and then try finding a prefix. */
    for (int pass = 0; pass < 2; pass++) {
        iConstForEach(Array, h, headings_GmDocument(d->doc)) {
            const iGmHeading *head = h.value;
            if ((pass == 0 && equalCase_Rangecc     (head->text, heading)) ||
                (pass == 1 && startsWithCase_Rangecc(head->text, heading))) {
                postCommandf_Root(as_Widget(d->owner)->root, "document.goto loc:%p",
                                  head->text.start);
                return;
            }
        }
    }
}

iBool isWideBlockScrollable_DocumentView(const iDocumentView *d, const iRect docBounds,
                                         const iGmRun *run) {
    const iGmPreMeta *meta       = preMeta_GmDocument(d->doc, preId_GmRun(run));
    const iGmRunRange range      = meta->runRange;
    int               maxWidth   = width_Rect(meta->pixelRect);
    const iRect       pageBounds = shrunk_Rect(bounds_Widget(as_Widget(d->owner)),
                                               init1_I2(d->pageMargin * gap_UI));
    return left_Rect(docBounds) + run->bounds.pos.x + meta->initialOffset + maxWidth >
           right_Rect(pageBounds);
}

iBool scrollWideBlock_DocumentView(iDocumentView *d, iInt2 mousePos, int delta, int duration,
                                   iBool *isAtEnd_out) {
    if (delta == 0 || hostWheelSwipeState_DocumentView_(d) == direct_WheelSwipeState) {
        return iFalse;
    }
    const int docWidth = documentWidth_DocumentView(d);
    const iRect docBounds = documentBounds_DocumentView(d);
    const iInt2 docPos = documentPos_DocumentView_(d, mousePos);
    if (isAtEnd_out) {
        *isAtEnd_out = iFalse;
    }
    iConstForEach(PtrArray, i, &d->visibleWideRuns) {
        const iGmRun *run = i.ptr;
        if (contains_Rangei(ySpan_Rect(run->bounds), docPos.y)) {
            /* We can scroll this run. First find out how much is allowed. */
            const iGmPreMeta *meta = preMeta_GmDocument(d->doc, preId_GmRun(run));
            const iGmRunRange range = meta->runRange;
            int maxWidth = width_Rect(meta->pixelRect);
            if (!isWideBlockScrollable_DocumentView(d, docBounds, run)) {
                return iFalse;
            }
            const int maxOffset = maxWidth + run->bounds.pos.x - docWidth;
            int *offset = wideRunOffset_DocumentView_(d, preId_GmRun(run));
            const int oldOffset = *offset;
            *offset = iClamp(*offset + delta, 0, maxOffset);
            /* Make sure the whole block gets redraw. */
            if (oldOffset != *offset) {
                for (const iGmRun *r = range.start; r != range.end; r++) {
                    insert_PtrSet(d->invalidRuns, r);
                }
                refresh_Widget(d->owner);
                *d->selectMark = iNullRange;
                *d->foundMark  = iNullRange;
                if (duration) {
                    if (d->animWideRunId != preId_GmRun(run) || isFinished_Anim(&d->animWideRunOffset)) {
                        d->animWideRunId = preId_GmRun(run);
                        init_Anim(&d->animWideRunOffset, oldOffset);
                    }
                    setValueEased_Anim(&d->animWideRunOffset, *offset, duration);
                    d->animWideRunRange = range;
                    hostRefreshWhileScrolling_DocumentView_(d);
                }
                else {
                    d->animWideRunId = 0;
                    init_Anim(&d->animWideRunOffset, 0);
                }
            }
            else {
                /* Offset didn't change. We could consider allowing swipe navigation to occur
                   by returning iFalse here, but perhaps only if the original starting
                   offset of the wide block was at the far end already .*/
                if (isAtEnd_out) {
                    *isAtEnd_out = iTrue;
                }
            }
            return iTrue;
        }
    }
    return iFalse;
}

iRangecc sourceLoc_DocumentView(const iDocumentView *d, iInt2 pos) {
    return findLoc_GmDocument(d->doc, documentPos_DocumentView_(d, pos));
}

iDeclareType(MiddleRunParams)

struct Impl_MiddleRunParams {
    int midY;
    const iGmRun *closest;
    int distance;
};

static void find_MiddleRunParams_(void *params, const iGmRun *run) {
    iMiddleRunParams *d = params;
    if (isEmpty_Rect(run->bounds)) {
        return;
    }
    const int distance = iAbs(mid_Rect(run->bounds).y - d->midY);
    if (!d->closest || distance < d->distance) {
        d->closest  = run;
        d->distance = distance;
    }
}

static const iGmRun *middleRun_DocumentView_(const iDocumentView *d) {
    iRangei visRange = visibleRange_DocumentView(d);
    iMiddleRunParams params = { (visRange.start + visRange.end) / 2, NULL, 0 };
    render_GmDocument(d->doc, visRange, find_MiddleRunParams_, &params);
    return params.closest;
}

void allocDrawnVisBuffer_DocumentView(const iDocumentView *d) {
    if (isPaged_DocumentView(d)) {
        allocPagedVisBuffer_DocumentView(d);
    }
    else {
        alloc_VisBuf(d->visBuf, bounds_Widget(constAs_Widget(d->owner)).size, 1);
    }
}

void allocVisBuffer_DocumentView(const iDocumentView *d) {
    if (isVisible_Widget(d->owner)) {
        allocDrawnVisBuffer_DocumentView(d);
    }
    else {
        dealloc_VisBuf(d->visBuf);
    }
}

size_t visibleLinkOrdinal_DocumentView(const iDocumentView *d, iGmLinkId linkId) {
    size_t ord = 0;
    const iRangei visRange = visibleRange_DocumentView(d);
    /* Don't give an ordinal to partially visible links. */
    const int ordinalPad = !isTerminal_Platform() ? -lineHeight_Text(paragraph_FontId) / 10 : 0;
    iConstForEach(PtrArray, i, &d->visibleLinks) {
        const iGmRun *run = i.ptr;
        if (top_Rect(run->visBounds) >= visRange.start + ordinalPad) {
            if (run->flags & decoration_GmRunFlag && run->linkId) {
                if (run->linkId == linkId) return ord;
                ord++;
            }
        }
    }
    return iInvalidPos;
}

static iBool updatePagedLayout_DocumentView_(iDocumentView *d, iBool force) {
    /* Pagination changes, so the position follows the source location at the top of the
       visible area. */
    setWidth_Banner(d->banner, documentWidth_DocumentView(d)); /* affects the first page */
    const iGmDocumentMetrics metrics = metrics_DocumentView(d);
    if (!force && equal_GmDocumentMetrics(&metrics, requestedMetrics_GmDocument(d->doc)) &&
        !isLayoutOutdated_GmDocument(d->doc)) {
        return iFalse;
    }
    const char    *runLoc = d->visibleRuns.start ? d->visibleRuns.start->text.start : NULL;
    const iString *source = source_GmDocument(d->doc);
    /* The reading position stays the same until the user turns the page. Otherwise, each
       relayout would move it to the beginning of a spread. */
    const size_t anchor = anchor_DocumentView(d);
    setMetrics_GmDocument(d->doc, &metrics);
    hostDocumentRunsInvalidated_DocumentView_(d);
    resetPage_DocumentView(d);
    init_Anim(&d->scrollY.pos, 0);
    if (isPaged_DocumentView(d)) {
        const iGmRun *run = anchor < size_String(source)
                                ? findRunAtLoc_GmDocument(d->doc, cstr_String(source) + anchor)
                                : NULL;
        if (run) {
            showDocumentY_DocumentView(d, top_Rect(run->visBounds));
            d->paged.anchor = anchor;
        }
        updateVisible_DocumentView(d);
        return iTrue;
    }
    const iGmRun *run = runLoc ? findRunAtLoc_GmDocument(d->doc, runLoc) : NULL;
    if (run) {
        scrollTo_DocumentView(d, top_Rect(run->visBounds) + lineHeight_Text(paragraph_FontId), iFalse);
    }
    return iTrue;
}

static iBool isSourceRun_DocumentView_(const iDocumentView *d, const iGmRun *run) {
    const iGmRunRange runs = runRange_GmDocument(d->doc);
    const iString    *src  = source_GmDocument(d->doc);
    return run && run >= runs.start && run < runs.end && ~run->flags & decoration_GmRunFlag &&
           run->text.start >= constBegin_String(src) && run->text.start < constEnd_String(src);
}

iDeclareType(VisibleSourceRun)

struct Impl_VisibleSourceRun {
    const iDocumentView *view;
    const iGmRun        *run;
};

static void find_VisibleSourceRun_(void *context, const iGmRun *run) {
    iVisibleSourceRun *d = context;
    if (!d->run && isSourceRun_DocumentView_(d->view, run)) {
        d->run = run;
    }
}

static const iGmRun *firstVisibleSourceRun_DocumentView_(const iDocumentView *d) {
    iVisibleSourceRun params = { d, NULL };
    render_GmDocument(d->doc, visibleRange_DocumentView(d), find_VisibleSourceRun_, &params);
    return params.run;
}

static void setLayoutAnchor_DocumentView_(iDocumentView *d, const iGmRun *keep, iBool keepCenter) {
    /* The relayout begins at the top of the visible area, or above it if the content may
       shrink. The kept run stays where it is. */
    const iGmRunRange runs     = runRange_GmDocument(d->doc);
    const iRangei     visRange = visibleRange_DocumentView(d);
    const int         height   = height_Rect(bounds_Widget(constAs_Widget(d->owner)));
    const iGmRun     *start    = keep;
    /* Partially visible runs above the kept one. */
    while (start > runs.start && bottom_Rect(start[-1].visBounds) > visRange.start) {
        start--;
    }
    if (keepCenter) {
        while (start > runs.start && top_Rect(start[-1].visBounds) >= visRange.start - height / 2) {
            start--;
        }
    }
    const char *startPos = NULL;
    int         startTop = 0;
    for (const iGmRun *run = start; run <= keep; run++) {
        if ((startPos = sourceLoc_GmDocument(d->doc, run)) != NULL) {
            startTop = top_Rect(run->visBounds);
            break;
        }
    }
    if (!startPos || startPos > keep->text.start) {
        startPos = keep->text.start;
        startTop = top_Rect(keep->visBounds);
    }
    setLayoutAnchor_GmDocument(d->doc,
                               startPos,
                               startTop,
                               keep->text.start,
                               top_Rect(keep->visBounds),
                               visRange.end + height);
}

iBool updateDocumentWidthRetainingScrollPosition_DocumentView(iDocumentView *d, iBool keepCenter) {
    setCentered_Banner(d->banner, wantsPaged_DocumentView(d));
    if (isPaged_DocumentView(d) || wantsPaged_DocumentView(d)) {
        return updatePagedLayout_DocumentView_(d, keepCenter);
    }
    const iGmDocumentMetrics metrics  = metrics_DocumentView(d);
    const int                newWidth = metrics.width;
    if (newWidth == size_GmDocument(d->doc).x && !keepCenter /* not a font change */ &&
        !(docPrefs_DocumentView(d)->expandToLongLines.value &&
          docPrefs_DocumentView(d)->plainTextWrap.value &&
          format_GmDocument(d->doc) == plainText_SourceFormat)) {
        return iFalse;
    }
    /* Font changes (i.e., zooming) will keep the view centered, otherwise keep the top
       of the visible area fixed. */
    const iGmRun *run     = keepCenter ? middleRun_DocumentView_(d) : NULL;
    if (!isSourceRun_DocumentView_(d, run)) {
        run = firstVisibleSourceRun_DocumentView_(d);
    }
    if (!run) {
        run = d->visibleRuns.start;
    }
    const char *  runLoc  = (run ? run->text.start : NULL);
    if (isSourceRun_DocumentView_(d, run)) {
        setLayoutAnchor_DocumentView_(d, run, keepCenter);
    }
    int           voffset = 0;
    float         midPos  = 0.5f; /* normalized position of view center inside the run */
    if (!keepCenter && run) {
        /* Keep the first visible run visible at the same position. */
        /* TODO: First *fully* visible run? */
        voffset = visibleRange_DocumentView(d).start - top_Rect(run->visBounds);
    }
    else if (run) {
        const iRangei visRange = visibleRange_DocumentView(d);
        midPos = (float) ((visRange.start + visRange.end) / 2 - top_Rect(run->bounds)) /
                 (float) height_Rect(run->bounds);
    }
    run = NULL;
    setMetrics_GmDocument(d->doc, &metrics);
    setWidth_Banner(d->banner, newWidth);
    hostDocumentRunsInvalidated_DocumentView_(d);
    if (runLoc && !keepCenter) {
        run = findRunAtLoc_GmDocument(d->doc, runLoc);
        if (run) {
            scrollTo_DocumentView(
                d, top_Rect(run->visBounds) + lineHeight_Text(paragraph_FontId) + voffset, iFalse);
        }
    }
    else if (runLoc && keepCenter) {
        run = findRunAtLoc_GmDocument(d->doc, runLoc);
        if (run) {
            const int midY = top_Rect(run->bounds) + (int) (midPos * height_Rect(run->bounds));
            scrollTo_DocumentView(d,
                                  midY - height_Rect(bounds_Widget(constAs_Widget(d->owner))) / 2 +
                                      lineHeight_Text(paragraph_FontId),
                                  iFalse);
        }
    }
    return iTrue;
}

iGmScrollAnchor scrollAnchor_DocumentView(const iDocumentView *d) {
    iGmScrollAnchor anchor = { iInvalidPos, 0.0f };
    if (isPaged_DocumentView(d)) {
        return anchor;
    }
    const iGmRun *run = NULL;
    if (d->visibleRuns.start) {
        for (const iGmRun *i = d->visibleRuns.start; i <= d->visibleRuns.end; i++) {
            if (isSourceRun_DocumentView_(d, i)) {
                run = i;
                break;
            }
        }
    }
    if (!run) {
        run = firstVisibleSourceRun_DocumentView_(d);
    }
    if (run) {
        const int height = height_Rect(run->visBounds);
        anchor.pos   = run->text.start - constBegin_String(source_GmDocument(d->doc));
        anchor.normY = height > 0 ? (float) (visibleRange_DocumentView(d).start -
                                             top_Rect(run->visBounds)) / height
                                  : 0.0f;
    }
    return anchor;
}

void setLayoutScrollAnchor_DocumentView(const iDocumentView *d, iGmDocument *doc,
                                        const iGmScrollAnchor *anchor) {
    /* The anchor run is laid out at an arbitrary position far from the top, and the content
       above it in slices. Near the beginning of the source, layout starts from the top. */
    const int height = iMax(height_Widget(d->owner), size_Root(d->owner->root).y);
    const int top    = anchor->pos < 0x4000 ? 0 : 0x100000;
    setLayoutAnchorOffset_GmDocument(doc, anchor->pos, top, top + 2 * height);
}

iRect runRect_DocumentView(const iDocumentView *d, const iGmRun *run) {
    if (isPaged_DocumentView(d)) {
        return pagedRunRect_DocumentView(d, run);
    }
    const iRect docBounds = documentBounds_DocumentView(d);
    return moved_Rect(run->bounds, addY_I2(topLeft_Rect(docBounds), viewPos_DocumentView(d)));
}

void resetScrollPosition_DocumentView(iDocumentView *d, float normScrollY,
                                      const iGmScrollAnchor *anchor) {
    resetScroll_DocumentView(d);
    if (isPaged_DocumentView(d)) {
        showSourcePosition_DocumentView(d, normScrollY);
    }
    else {
        const iString *src = source_GmDocument(d->doc);
        const iGmRun  *run = anchor && anchor->pos < size_String(src)
                                 ? findRunAtLoc_GmDocument(d->doc, cstr_String(src) + anchor->pos)
                                 : NULL;
        if (run) {
            const int top = top_Rect(run->visBounds) +
                            (int) (anchor->normY * height_Rect(run->visBounds) + 0.5f);
            init_Anim(&d->scrollY.pos,
                      scrollPos_DocumentView_(d, top + lineHeight_Text(paragraph_FontId), iFalse));
        }
        else {
            init_Anim(&d->scrollY.pos, normScrollY * pageHeight_DocumentView(d));
        }
    }
    updateVisible_DocumentView(d);
    clampScroll_DocumentView(d);
    updateSideOpacity_DocumentView(d, iFalse);
    updateDrawBufs_DocumentView(d, updateTimestampBuf_DrawBufsFlag | updateSideBuf_DrawBufsFlag);
}
