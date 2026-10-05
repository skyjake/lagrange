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

static int preferredWidth_DocumentView_(const iDocumentView *d, int columns) {
    /* Not zoomed: the zoom level only affects the text size. Lines are shorter in two
       columns than when scrolling. */
    const int width = fontSize_UI * docPrefs_GmDocument(d->doc)->lineWidth.value;
    return columns > 1 ? width * 0.85f : width;
}

static int pageMargin_DocumentView_(const iDocumentView *d, int columnWidth) {
    /* Margins are relative to the page size. */
    const int pageSize = iMin(columnWidth, height_Rect(pageViewport_DocumentView(d)));
    return iMax(2 * gap_UI, pageSize / 14);
}

static int maxWidthInColumn_DocumentView_(const iDocumentView *d, int columnWidth) {
    return columnWidth - 2 * pageMargin_DocumentView_(d, columnWidth);
}

static int pagesFor_DocumentView_(const iDocumentView *d, int viewportWidth) {
    /* Two pages when both fit at the preferred width. */
    return preferredWidth_DocumentView_(d, 2) <=
                   maxWidthInColumn_DocumentView_(d, viewportWidth / 2)
               ? 2
               : 1;
}

static int columnsPerPageFor_DocumentView_(const iDocumentView *d, int pageWidth) {
    /* Two columns when both fit at the preferred width of the actual text size, so small
       text is set in narrower columns. */
    const float zoom = activeDocPrefs_App()->zoomPercent.value / 100.0f; /* follows the fonts */
    return preferredWidth_DocumentView_(d, 2) * zoom * 0.75f <=
                   maxWidthInColumn_DocumentView_(d, pageWidth / 2)
               ? 2
               : 1;
}

static int columnsFor_DocumentView_(const iDocumentView *d, int viewportWidth, int *perPage) {
    /* All columns of the spread. */
    const int pages = pagesFor_DocumentView_(d, viewportWidth);
    const int cols  = columnsPerPageFor_DocumentView_(d, viewportWidth / pages);
    if (perPage) {
        *perPage = cols;
    }
    return pages * cols;
}

int pagedDocumentWidth_DocumentView(const iDocumentView *d) {
    const int width   = width_Rect(pageViewport_DocumentView(d));
    const int columns = columnsFor_DocumentView_(d, width, NULL);
    return iMin(preferredWidth_DocumentView_(d, columns),
                maxWidthInColumn_DocumentView_(d, width / columns));
}

void pagedMetrics_DocumentView(const iDocumentView *d, iGmDocumentMetrics *metrics) {
    const iRect viewport  = pageViewport_DocumentView(d);
    const iWidget *footer = footerButtons_DocumentWidget(d->owner);
    metrics->width           = pagedDocumentWidth_DocumentView(d);
    metrics->canvasWidth     = width_Rect(viewport) /
                           columnsFor_DocumentView_(d, width_Rect(viewport), &metrics->columnsPerPage);
    /* Preformatted blocks may use half of the margins, but the columns of a page have only
       half of the usual space between them (see `columnShift_DocumentView`). */
    metrics->maxContentWidth =
        iMax(metrics->width,
             metrics->columnsPerPage == 2
                 ? metrics->width + (metrics->canvasWidth - metrics->width) / 2
                 : metrics->canvasWidth - pageMargin_DocumentView_(d, metrics->canvasWidth));
    metrics->pageHeight      = height_Rect(viewport);
    metrics->lastPageInset   = footer ? height_Widget(footer) : 0;
    /* The header and footer extend into the safe area insets. */
    const iRect safe         = safeRect_Root(constAs_Widget(d->owner)->root);
    metrics->topInset        = iMax(0, top_Rect(safe) - top_Rect(viewport));
    metrics->bottomInset     = iMax(0, bottom_Rect(viewport) - bottom_Rect(safe));
}

static int columnWidth_DocumentView_(const iDocumentView *d) {
    return metrics_GmDocument(d->doc)->canvasWidth;
}

int numColumns_DocumentView(const iDocumentView *d) {
    if (!isPaged_DocumentView(d) || columnWidth_DocumentView_(d) <= 0) {
        return 1;
    }
    return iClamp(width_Rect(pageViewport_DocumentView(d)) / columnWidth_DocumentView_(d), 1, 4);
}

int numPages_DocumentView(const iDocumentView *d) {
    return iMax(1, numColumns_DocumentView(d) / columnsPerPage_GmDocument(d->doc));
}

size_t currentColumn_DocumentView(const iDocumentView *d) {
    return d->paged.column;
}

iRangei visibleColumns_DocumentView(const iDocumentView *d) {
    const int n    = numColumns_DocumentView(d);
    iRangei   cols = { (int) d->paged.column, (int) d->paged.column + n };
    if (d->paged.isDragging || !isFinished_Anim(&d->paged.turnOffset)) {
        cols.start = iMin(cols.start, (int) d->paged.turnFrom);
        cols.end   = iMax(cols.end, (int) d->paged.turnFrom + n);
    }
    return cols;
}

int spreadWidth_DocumentView(const iDocumentView *d) {
    return numColumns_DocumentView(d) * columnWidth_DocumentView_(d);
}

static int spreadStride_DocumentView_(const iDocumentView *d) {
    /* Adjacent spreads are separated by a gap during turns. */
    return spreadWidth_DocumentView(d) + 3 * gap_UI;
}

float columnPosition_DocumentView(const iDocumentView *d) {
    const int   n      = numColumns_DocumentView(d);
    const int   stride = spreadStride_DocumentView_(d);
    const float pos    = (float) d->paged.column -
                      (stride > 0 ? value_Anim(&d->paged.turnOffset) * n / stride : 0.0f);
    return iClamp(pos, 0.0f, (float) iMax(0, (int) numColumns_GmDocument(d->doc) - n));
}

int columnLeft_DocumentView(const iDocumentView *d, size_t column) {
    const int n      = numColumns_DocumentView(d);
    const int rel    = (int) column - (int) d->paged.column;
    const int spread = rel >= 0 ? rel / n : -((n - 1 - rel) / n);
    return left_Rect(pageViewport_DocumentView(d)) + spread * spreadStride_DocumentView_(d) +
           (rel - spread * n) * columnWidth_DocumentView_(d) +
           (int) value_Anim(&d->paged.turnOffset);
}

int columnShift_DocumentView(const iDocumentView *d, size_t column) {
    /* The two columns of a page are closer to each other than to the edges of the page. */
    if (columnsPerPage_GmDocument(d->doc) != 2) {
        return 0;
    }
    const int gap = columnWidth_DocumentView_(d) - size_GmDocument(d->doc).x;
    if (numPages_DocumentView(d) == 2) {
        /* On facing pages, the columns next to the crease are moved away from it, halfway
           toward the adjacent column. */
        return column % 4 == 1 ? -gap / 2 : column % 4 == 2 ? gap / 2 : 0;
    }
    /* On a single page, both move toward the center, leaving the same gap between them. */
    return column % 2 == 0 ? gap / 4 : -gap / 4;
}

iInt2 columnOrigin_DocumentView(const iDocumentView *d, size_t column) {
    return init_I2(columnLeft_DocumentView(d, column) + columnShift_DocumentView(d, column) +
                       (columnWidth_DocumentView_(d) - size_GmDocument(d->doc).x) / 2,
                   top_Rect(pageViewport_DocumentView(d)) -
                       (int) column * pageHeight_GmDocument(d->doc));
}

iRangei columnSpan_DocumentView(const iDocumentView *d, size_t column) {
    /* The column extends halfway to the adjacent columns of the spread. */
    const int n     = numColumns_DocumentView(d);
    const int width = size_GmDocument(d->doc).x;
    const int left  = columnLeft_DocumentView(d, column);
    iRangei   span  = { left, left + columnWidth_DocumentView_(d) };
    if (column % n) {
        span.start = (columnOrigin_DocumentView(d, column - 1).x + width +
                      columnOrigin_DocumentView(d, column).x) / 2;
    }
    if ((column + 1) % n) {
        span.end = (columnOrigin_DocumentView(d, column).x + width +
                    columnOrigin_DocumentView(d, column + 1).x) / 2;
    }
    return span;
}

iRect pageTextRect_DocumentView(const iDocumentView *d, size_t page) {
    const size_t first    = page * columnsPerPage_GmDocument(d->doc);
    const size_t last     = first + columnsPerPage_GmDocument(d->doc) - 1;
    const iRect  viewport = pageViewport_DocumentView(d);
    const int    left     = columnOrigin_DocumentView(d, first).x;
    const int    right    = columnOrigin_DocumentView(d, last).x + size_GmDocument(d->doc).x;
    return init_Rect(left, top_Rect(viewport), right - left, height_Rect(viewport));
}

iInt2 pagedDocumentPos_DocumentView(const iDocumentView *d, iInt2 windowPos) {
    const int n      = numColumns_DocumentView(d);
    size_t    column = d->paged.column;
    while (column + 1 < d->paged.column + n &&
           windowPos.x >= columnSpan_DocumentView(d, column).end) {
        column++;
    }
    return sub_I2(windowPos, columnOrigin_DocumentView(d, column));
}

iRect pagedRunRect_DocumentView(const iDocumentView *d, const iGmRun *run) {
    const size_t column = columnAt_GmDocument(d->doc, top_Rect(run->visBounds));
    return moved_Rect(run->bounds, columnOrigin_DocumentView(d, column));
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
    setPages_VisBuf(d->visBuf, visibleColumns_DocumentView(d).start * pageHeight);
}

/*----------------------------------------------------------------------------------------------*/

void resetPage_DocumentView(iDocumentView *d) {
    removeTicker_App(animatePageTurn_DocumentView, d);
    d->paged.column      = 0;
    d->paged.anchor      = iInvalidPos;
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
    pagesMoved_DocumentWidget(d->owner);
    if (!isFinished_Anim(&d->paged.turnOffset)) {
        addTicker_App(animatePageTurn_DocumentView, d);
        return;
    }
    if (d->paged.isReverting) {
        d->paged.isReverting = iFalse;
        d->paged.column      = d->paged.turnFrom;
        d->paged.anchor      = iInvalidPos;
        init_Anim(&d->paged.turnOffset, 0);
    }
    updateVisible_DocumentView(d); /* buffers move on to the next spread */
}

void showColumn_DocumentView(iDocumentView *d, size_t column, iBool isAnimated) {
    if (d->paged.isDragging) {
        return;
    }
    const size_t n = numColumns_DocumentView(d);
    column = iMin(column, numColumns_GmDocument(d->doc) - 1);
    column -= column % n;
    if (column == d->paged.column) {
        return;
    }
    d->paged.isReverting = iFalse;
    const int spreads = ((int) column - (int) d->paged.column) / (int) n;
    if (isAnimated && iAbs(spreads) == 1) {
        /* The old spread slides out as the new one slides in. */
        d->paged.turnFrom = d->paged.column;
        init_Anim(&d->paged.turnOffset, spreads * spreadStride_DocumentView_(d));
        setValueEased_Anim(&d->paged.turnOffset, 0, turnDuration_PagedView_);
        addTicker_App(animatePageTurn_DocumentView, d);
    }
    else {
        init_Anim(&d->paged.turnOffset, 0);
    }
    d->paged.column = column;
    d->paged.anchor = iInvalidPos;
    updateVisible_DocumentView(d);
    refresh_Widget(d->owner);
}

void showDocumentY_DocumentView(iDocumentView *d, int documentY) {
    showColumn_DocumentView(d, columnAt_GmDocument(d->doc, documentY), iFalse);
}

iBool turnPage_DocumentView(iDocumentView *d, int dir) {
    const int n      = numColumns_DocumentView(d);
    const int target = (int) d->paged.column + dir * n;
    if (target < 0 || target >= (int) numColumns_GmDocument(d->doc)) {
        postCommand_Widget(d->owner, "document.page.overflow arg:%d", dir);
        return iFalse;
    }
    /* Swipes animate regardless, but other turns follow the smooth scrolling setting. */
    showColumn_DocumentView(d, target, prefs_App()->smoothScrolling);
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
        const size_t last = d->paged.column + numColumns_DocumentView(d) - 1;
        left  = columnOrigin_DocumentView(d, d->paged.column).x;
        right = columnOrigin_DocumentView(d, last).x + size_GmDocument(d->doc).x;
    }
    return windowPos.x < left ? -1 : windowPos.x >= right ? +1 : 0;
}

void dragPage_DocumentView(iDocumentView *d, int offset) {
    const int n      = numColumns_DocumentView(d);
    const int spread = spreadStride_DocumentView_(d);
    if (!d->paged.isDragging) {
        if (d->paged.isReverting) {
            d->paged.column = d->paged.turnFrom;
        }
        d->paged.isDragging  = iTrue;
        d->paged.isReverting = iFalse;
        d->paged.dragFrom    = d->paged.column;
        d->paged.dragDir     = 0;
        d->paged.dragOffset  = offset;
        d->paged.dragTime    = SDL_GetTicks();
        d->paged.dragSpeed   = 0.0f;
    }
    else {
        const uint32_t now = SDL_GetTicks();
        const uint32_t dt  = now - d->paged.dragTime;
        if (dt > 0) {
            const float speed = (offset - d->paged.dragOffset) * 1000.0f / dt;
            /* After a pause, only the latest movement counts. */
            d->paged.dragSpeed  = dt > 100 ? speed : (d->paged.dragSpeed + speed) / 2;
            d->paged.dragOffset = offset;
            d->paged.dragTime   = now;
        }
    }
    const size_t oldCol  = d->paged.column;
    const int    dir     = offset < 0 ? +1 : offset > 0 ? -1 : 0;
    const int    target  = (int) d->paged.dragFrom + dir * n;
    d->paged.dragDir     = dir;
    d->paged.turnFrom    = d->paged.dragFrom;
    if (!dir || target < 0 || target >= (int) numColumns_GmDocument(d->doc)) {
        /* Nothing to turn to, so the pages resist. */
        d->paged.column = d->paged.dragFrom;
        init_Anim(&d->paged.turnOffset, offset / 6);
    }
    else {
        d->paged.column = target;
        init_Anim(&d->paged.turnOffset, offset + dir * spread);
    }
    if (d->paged.column != oldCol) {
        d->paged.anchor = iInvalidPos;
        updateVisible_DocumentView(d);
    }
    pagesMoved_DocumentWidget(d->owner);
    refresh_Widget(d->owner);
}

static void finishDrag_DocumentView_(iDocumentView *d, float to, iBool followGesture) {
    /* The turn starts at least as fast as the gesture, but no slower than usual. */
    iAnim         *anim      = &d->paged.turnOffset;
    const float    remaining = to - value_Anim(anim);
    uint32_t       span      = turnDuration_PagedView_;
    if (followGesture && SDL_GetTicks() - d->paged.dragTime < 100) {
        const float speed = d->paged.dragSpeed * (remaining < 0 ? -1 : 1);
        if (speed > 0) {
            /* Ease out starts at twice the average speed. */
            span = iClamp(2000.0f * fabsf(remaining) / speed, 1, turnDuration_PagedView_);
        }
    }
    setFlags_Anim(anim, easeBoth_AnimFlag, iFalse);
    setFlags_Anim(anim, easeOut_AnimFlag, iTrue);
    setValue_Anim(anim, to, span);
}

void endPageDrag_DocumentView(iDocumentView *d, iBool isCancelled) {
    if (!d->paged.isDragging) {
        return;
    }
    d->paged.isDragging = iFalse;
    if (d->paged.column == d->paged.turnFrom) {
        if (!isCancelled && d->paged.dragDir) {
            postCommand_Widget(d->owner, "document.page.overflow arg:%d", d->paged.dragDir);
        }
        finishDrag_DocumentView_(d, 0, iFalse); /* resisting pages spring back */
    }
    else if (isCancelled) {
        /* Slide back to where the drag began. */
        const int dir = d->paged.column > d->paged.turnFrom ? +1 : -1;
        d->paged.isReverting = iTrue;
        finishDrag_DocumentView_(d, dir * spreadStride_DocumentView_(d), iTrue);
    }
    else {
        finishDrag_DocumentView_(d, 0, iTrue);
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
