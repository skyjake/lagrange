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

#include <SDL3/SDL_timer.h>
#include <math.h>

static const uint32_t turnDuration_PagedView_ = 250;

iBool wantsPaged_DocumentView(const iDocumentView *d) {
    return !isTerminal_Platform() && docPrefs_GmDocument(d->doc)->pageView.value &&
           !isCoverPage_GmDocument(d->doc);
}

iBool isPaged_DocumentView(const iDocumentView *d) {
    return isPaged_GmDocument(d->doc) && size_GmDocument(d->doc).y > 0;
}

iRect pageViewport_DocumentView(const iDocumentView *d) {
    const iWidget *w    = constAs_Widget(d->owner);
    iRect          rect = bounds_Widget(w);
    if (wantsPaged_DocumentView(d) && document_Root(w->root) == d->owner) {
        /* Pages extend under the bars, so their visibility does not affect the layout. */
        return readingRect_Root(w->root, rect);
    }
    adjustEdges_Rect(&rect, 0, 0,
                     -(phoneToolbarHeight_DocumentWidget(d->owner) +
                       phoneBottomNavbarHeight_DocumentWidget(d->owner)), 0);
    return rect;
}

static int preferredWidth_DocumentView_(const iDocumentView *d) {
    /* Lines are shorter in two columns than when scrolling. */
    return preferredDocumentWidth_DocumentView(d) * 0.8f;
}

static int columnsFor_DocumentView_(const iDocumentView *d, int viewportWidth) {
    /* Two columns when both fit at the preferred width. */
    return preferredWidth_DocumentView_(d) <=
                   maxDocumentWidthIn_DocumentView(d, viewportWidth / 2)
               ? 2
               : 1;
}

int pagedDocumentWidth_DocumentView(const iDocumentView *d) {
    const int width   = width_Rect(pageViewport_DocumentView(d));
    const int columns = columnsFor_DocumentView_(d, width);
    /* A single column has room for the full line width. */
    return iMin(columns == 1 ? preferredDocumentWidth_DocumentView(d)
                             : preferredWidth_DocumentView_(d),
                maxDocumentWidthIn_DocumentView(d, width / columns));
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
    if (d->paged.isDragging || !isFinished_Anim(&d->paged.turnOffset)) {
        pages.start = iMin(pages.start, (int) d->paged.turnFrom);
        pages.end   = iMax(pages.end, (int) d->paged.turnFrom + n);
    }
    return pages;
}

int spreadWidth_DocumentView(const iDocumentView *d) {
    return numColumns_DocumentView(d) * columnWidth_DocumentView_(d);
}

static int spreadStride_DocumentView_(const iDocumentView *d) {
    /* Adjacent spreads are separated by a gap during turns. */
    return spreadWidth_DocumentView(d) + 3 * gap_UI;
}

int pageLeft_DocumentView(const iDocumentView *d, size_t page) {
    const int n      = numColumns_DocumentView(d);
    const int rel    = (int) page - (int) d->paged.page;
    const int spread = rel >= 0 ? rel / n : -((n - 1 - rel) / n);
    return left_Rect(pageViewport_DocumentView(d)) + spread * spreadStride_DocumentView_(d) +
           (rel - spread * n) * columnWidth_DocumentView_(d) +
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
    d->paged.page        = 0;
    d->paged.turnFrom    = 0;
    d->paged.isDragging  = iFalse;
    d->paged.isReverting = iFalse;
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
        return;
    }
    if (d->paged.isReverting) {
        d->paged.isReverting = iFalse;
        d->paged.page        = d->paged.turnFrom;
        init_Anim(&d->paged.turnOffset, 0);
    }
    updateVisible_DocumentView(d); /* buffers move on to the next spread */
}

void showPage_DocumentView(iDocumentView *d, size_t page, iBool isAnimated) {
    if (d->paged.isDragging) {
        return;
    }
    const size_t n = numColumns_DocumentView(d);
    page = iMin(page, numPages_GmDocument(d->doc) - 1);
    page -= page % n;
    if (page == d->paged.page) {
        return;
    }
    d->paged.isReverting = iFalse;
    const int spreads = ((int) page - (int) d->paged.page) / (int) n;
    if (isAnimated && iAbs(spreads) == 1) {
        /* The old spread slides out as the new one slides in. */
        d->paged.turnFrom = d->paged.page;
        init_Anim(&d->paged.turnOffset, spreads * spreadStride_DocumentView_(d));
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

void dragPage_DocumentView(iDocumentView *d, int offset) {
    const int n      = numColumns_DocumentView(d);
    const int spread = spreadStride_DocumentView_(d);
    if (!d->paged.isDragging) {
        if (d->paged.isReverting) {
            d->paged.page = d->paged.turnFrom;
        }
        d->paged.isDragging  = iTrue;
        d->paged.isReverting = iFalse;
        d->paged.dragFrom    = d->paged.page;
        d->paged.dragDir     = 0;
    }
    const size_t oldPage = d->paged.page;
    const int    dir     = offset < 0 ? +1 : offset > 0 ? -1 : 0;
    const int    target  = (int) d->paged.dragFrom + dir * n;
    d->paged.dragDir     = dir;
    d->paged.turnFrom    = d->paged.dragFrom;
    if (!dir || target < 0 || target >= (int) numPages_GmDocument(d->doc)) {
        /* Nothing to turn to, so the pages resist. */
        d->paged.page = d->paged.dragFrom;
        init_Anim(&d->paged.turnOffset, offset / 6);
    }
    else {
        d->paged.page = target;
        init_Anim(&d->paged.turnOffset, offset + dir * spread);
    }
    if (d->paged.page != oldPage) {
        updateVisible_DocumentView(d);
    }
    refresh_Widget(d->owner);
}

void endPageDrag_DocumentView(iDocumentView *d, iBool isCancelled) {
    if (!d->paged.isDragging) {
        return;
    }
    d->paged.isDragging = iFalse;
    if (d->paged.page == d->paged.turnFrom) {
        if (!isCancelled && d->paged.dragDir) {
            postCommand_Widget(d->owner, "document.page.overflow arg:%d", d->paged.dragDir);
        }
        setValueEased_Anim(&d->paged.turnOffset, 0, turnDuration_PagedView_);
    }
    else if (isCancelled) {
        /* Slide back to where the drag began. */
        const int dir = d->paged.page > d->paged.turnFrom ? +1 : -1;
        d->paged.isReverting = iTrue;
        setValueEased_Anim(&d->paged.turnOffset,
                           dir * spreadStride_DocumentView_(d),
                           turnDuration_PagedView_);
    }
    else {
        setValueEased_Anim(&d->paged.turnOffset, 0, turnDuration_PagedView_);
    }
    addTicker_App(animatePageTurn_DocumentView, d);
}

void wheelPage_DocumentView(iDocumentView *d, float amount) {
    if (fabsf(amount) >= 1.0f) {
        /* Mouse wheel notch. */
        turnPage_DocumentView(d, amount > 0 ? -1 : +1);
        return;
    }
    /* Fine-grained wheels (e.g., trackpads) turn one page per gesture. */
    const uint32_t now = SDL_GetTicks();
    if (now - d->paged.wheelTime > 300) {
        d->paged.wheelAccum    = 0;
        d->paged.isWheelLocked = iFalse;
    }
    d->paged.wheelTime = now;
    if (!d->paged.isWheelLocked) {
        d->paged.wheelAccum += amount;
        if (fabsf(d->paged.wheelAccum) >= 1.0f) {
            d->paged.isWheelLocked = iTrue;
            turnPage_DocumentView(d, d->paged.wheelAccum > 0 ? -1 : +1);
        }
    }
}
