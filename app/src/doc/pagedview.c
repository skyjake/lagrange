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

#include "pagedview.h"

#include "app.h"
#include "banner.h"
#include "ui/documentwidget.h"
#include "ui/metrics.h"
#include "ui/root.h"
#include "ui/visbuf.h"

static const uint32_t turnDuration_PagedView_ = 250;

iBool wantsPaged_DocumentView(const iDocumentView *d) {
    return !isTerminal_Platform() && docPrefs_GmDocument(d->doc)->pageView.value &&
           !isCoverPage_GmDocument(d->doc);
}

iBool isPaged_DocumentView(const iDocumentView *d) {
    return isPaged_GmDocument(d->doc) && size_GmDocument(d->doc).y > 0;
}

iRect pageViewport_DocumentView(const iDocumentView *d) {
    iRect rect = bounds_Widget(constAs_Widget(d->owner));
    adjustEdges_Rect(&rect, 0, 0,
                     -(phoneToolbarHeight_DocumentWidget(d->owner) +
                       phoneBottomNavbarHeight_DocumentWidget(d->owner)), 0);
    return rect;
}

static int columnsFor_DocumentView_(const iDocumentView *d, int viewportWidth) {
    /* Two columns when both fit at the preferred width. */
    return preferredDocumentWidth_DocumentView(d) <=
                   maxDocumentWidthIn_DocumentView(d, viewportWidth / 2)
               ? 2
               : 1;
}

int pagedDocumentWidth_DocumentView(const iDocumentView *d) {
    const int width = width_Rect(pageViewport_DocumentView(d));
    return iMin(preferredDocumentWidth_DocumentView(d),
                maxDocumentWidthIn_DocumentView(d, width / columnsFor_DocumentView_(d, width)));
}

void pagedMetrics_DocumentView(const iDocumentView *d, iGmDocumentMetrics *metrics) {
    const iRect viewport  = pageViewport_DocumentView(d);
    const iWidget *footer = footerButtons_DocumentWidget(d->owner);
    metrics->width           = pagedDocumentWidth_DocumentView(d);
    metrics->canvasWidth     = width_Rect(viewport) / columnsFor_DocumentView_(d, width_Rect(viewport));
    metrics->maxContentWidth = metrics->width;
    metrics->pageHeight      = height_Rect(viewport);
    metrics->lastPageInset   = footer ? height_Widget(footer) : 0;
}

static int columnWidth_DocumentView_(const iDocumentView *d) {
    return metrics_GmDocument(d->doc)->canvasWidth;
}

int numColumns_DocumentView(const iDocumentView *d) {
    if (!isPaged_DocumentView(d) || columnWidth_DocumentView_(d) <= 0) {
        return 1;
    }
    return iClamp(width_Rect(pageViewport_DocumentView(d)) / columnWidth_DocumentView_(d), 1, 2);
}

size_t currentPage_DocumentView(const iDocumentView *d) {
    return d->paged.page;
}

iRangei visiblePages_DocumentView(const iDocumentView *d) {
    const int n     = numColumns_DocumentView(d);
    iRangei   pages = { (int) d->paged.page, (int) d->paged.page + n };
    if (!isFinished_Anim(&d->paged.turnOffset)) {
        pages.start = iMin(pages.start, (int) d->paged.turnFrom);
        pages.end   = iMax(pages.end, (int) d->paged.turnFrom + n);
    }
    return pages;
}

int pageLeft_DocumentView(const iDocumentView *d, size_t page) {
    return left_Rect(pageViewport_DocumentView(d)) +
           ((int) page - (int) d->paged.page) * columnWidth_DocumentView_(d) +
           (int) value_Anim(&d->paged.turnOffset);
}

iInt2 pageOrigin_DocumentView(const iDocumentView *d, size_t page) {
    return init_I2(pageLeft_DocumentView(d, page) +
                       (columnWidth_DocumentView_(d) - size_GmDocument(d->doc).x) / 2,
                   top_Rect(pageViewport_DocumentView(d)) -
                       (int) page * pageHeight_GmDocument(d->doc));
}

iInt2 pagedDocumentPos_DocumentView(const iDocumentView *d, iInt2 windowPos) {
    const int colWidth = iMax(1, columnWidth_DocumentView_(d));
    const int x        = windowPos.x - pageLeft_DocumentView(d, d->paged.page);
    const int column   = iClamp(x < 0 ? -1 : x / colWidth, 0, numColumns_DocumentView(d) - 1);
    return sub_I2(windowPos, pageOrigin_DocumentView(d, d->paged.page + column));
}

iRect pagedRunRect_DocumentView(const iDocumentView *d, const iGmRun *run) {
    const size_t page = pageAt_GmDocument(d->doc, top_Rect(run->visBounds));
    return moved_Rect(run->bounds, pageOrigin_DocumentView(d, page));
}

void allocPagedVisBuffer_DocumentView(const iDocumentView *d) {
    if (!isVisible_Widget(d->owner)) {
        dealloc_VisBuf(d->visBuf);
        return;
    }
    const int n          = numColumns_DocumentView(d);
    const int pageHeight = pageHeight_GmDocument(d->doc);
    /* The current spread and the next one, which can be prerendered. During a turn, the
       previous and the current spread. */
    allocPages_VisBuf(d->visBuf, init_I2(columnWidth_DocumentView_(d), pageHeight), 2 * n);
    setPages_VisBuf(d->visBuf, visiblePages_DocumentView(d).start * pageHeight);
}

/*----------------------------------------------------------------------------------------------*/

void resetPage_DocumentView(iDocumentView *d) {
    removeTicker_App(animatePageTurn_DocumentView, d);
    d->paged.page     = 0;
    d->paged.turnFrom = 0;
    init_Anim(&d->paged.turnOffset, 0);
}

void animatePageTurn_DocumentView(iAny *context) {
    iDocumentView *d = context;
    if (current_Root() == NULL) {
        return; /* pending destruction */
    }
    refresh_Widget(d->owner);
    if (!isFinished_Anim(&d->paged.turnOffset)) {
        addTicker_App(animatePageTurn_DocumentView, d);
    }
    else {
        updateVisible_DocumentView(d); /* buffers move on to the next spread */
    }
}

void showPage_DocumentView(iDocumentView *d, size_t page, iBool isAnimated) {
    const size_t n = numColumns_DocumentView(d);
    page = iMin(page, numPages_GmDocument(d->doc) - 1);
    page -= page % n;
    if (page == d->paged.page) {
        return;
    }
    const int spreads = ((int) page - (int) d->paged.page) / (int) n;
    if (isAnimated && iAbs(spreads) == 1) {
        /* The old spread slides out as the new one slides in. */
        d->paged.turnFrom = d->paged.page;
        init_Anim(&d->paged.turnOffset, spreads * (int) n * columnWidth_DocumentView_(d));
        setValueEased_Anim(&d->paged.turnOffset, 0, turnDuration_PagedView_);
        addTicker_App(animatePageTurn_DocumentView, d);
    }
    else {
        init_Anim(&d->paged.turnOffset, 0);
    }
    d->paged.page = page;
    updateVisible_DocumentView(d);
    refresh_Widget(d->owner);
}

void showDocumentY_DocumentView(iDocumentView *d, int documentY) {
    showPage_DocumentView(d, pageAt_GmDocument(d->doc, documentY), iFalse);
}

iBool turnPage_DocumentView(iDocumentView *d, int dir) {
    const int n      = numColumns_DocumentView(d);
    const int target = (int) d->paged.page + dir * n;
    if (target < 0 || target >= (int) numPages_GmDocument(d->doc)) {
        postCommand_Widget(d->owner, "document.page.overflow arg:%d", dir);
        return iFalse;
    }
    showPage_DocumentView(d, target, iTrue);
    return iTrue;
}

int pageTurnZone_DocumentView(const iDocumentView *d, iInt2 windowPos) {
    const iRect viewport = pageViewport_DocumentView(d);
    if (!contains_Rect(viewport, windowPos)) {
        return 0;
    }
    int left, right;
    if (deviceType_App() == phone_AppDeviceType) {
        /* The margins are too narrow for tapping. */
        left  = left_Rect(viewport) + width_Rect(viewport) / 4;
        right = right_Rect(viewport) - width_Rect(viewport) / 4;
    }
    else {
        const size_t last = d->paged.page + numColumns_DocumentView(d) - 1;
        left  = pageOrigin_DocumentView(d, d->paged.page).x;
        right = pageOrigin_DocumentView(d, last).x + size_GmDocument(d->doc).x;
    }
    return windowPos.x < left ? -1 : windowPos.x >= right ? +1 : 0;
}
