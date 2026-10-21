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
#include "gmutil.h"
#include "render/text.h"
#include "ui/metrics.h"
#include "ui/root.h"
#include "ui/visbuf.h"
#include "ui/window.h"

#include <SDL3/SDL_timer.h>
#include <math.h>

uint32_t pageTurnDuration_DocumentView(const iDocumentView *d) {
    /* Narrow pages turn faster, though not in proportion to the distance. The full duration
       is for a width of about 1000 points. */
    const float full  = prefs_App()->pageTurn == curl_PageTurnAnimation ? 300 : 250;
    const float width = width_Rect(pageViewport_DocumentView(d)) / (250.0f * gap_UI);
    return full * iClamp(sqrtf(width), 0.5f, 1.0f);
}

uint32_t beginPageTurn_DocumentView(iDocumentView *d, uint32_t duration) {
    /* Rapid turns animate as fast as they are made. */
    const uint32_t now      = SDL_GetTicks();
    const uint32_t interval = now - d->paged.turnTime;
    d->paged.turnTime = now;
    return iMin(duration, interval);
}

static iBool isAlwaysScrolled_(const iString *url) {
    /* Checked whenever the page viewport is needed, so the URL is not fully parsed. */
    static const char *pages_[] = { "about:lagrange", "about:debug" };
    iForIndices(i, pages_) {
        if (startsWithCase_String(url, pages_[i])) {
            const char next = cstr_String(url)[strlen(pages_[i])];
            if (!next || next == '?' || next == '#') {
                return iTrue;
            }
        }
    }
    return iFalse;
}

iBool isPageViewAllowed_DocumentView(const iDocumentView *d) {
    return !isCoverPage_GmDocument(d->doc) && !isAlwaysScrolled_(url_GmDocument(d->doc));
}

iBool wantsPaged_DocumentView(const iDocumentView *d) {
    return docPrefs_GmDocument(d->doc)->pageView.value && isPageViewAllowed_DocumentView(d);
}

iBool isPaged_DocumentView(const iDocumentView *d) {
    return isPaged_GmDocument(d->doc) && size_GmDocument(d->doc).y > 0;
}

iRect pageViewport_DocumentView(const iDocumentView *d) {
    const iWidget *w    = constAs_Widget(d->owner);
    iRect          rect = bounds_Widget(w);
    if (wantsPaged_DocumentView(d) && constAs_Widget(document_Root(w->root)) == w) {
        /* Pages extend under the bars, so their visibility does not affect the layout. */
        return readingRect_Root(w->root, rect);
    }
    adjustEdges_Rect(&rect, 0, 0,
                     -hostPhoneBarsHeight_DocumentView(d), 0);
    return rect;
}

static int preferredWidth_DocumentView_(const iDocumentView *d, int columns) {
    /* Not zoomed: the zoom level only affects the text size. Lines are shorter in two
       columns than when scrolling. */
    float width = fontSize_UI * docPrefs_GmDocument(d->doc)->lineWidth.value;
    if (isTerminal_Platform()) {
        width /= aspect_UI * 0.8f;
    }
    return columns > 1 ? width * 0.85f : width;
}

static float textZoom_(void) {
    return activeDocPrefs_App()->zoomPercent.value / 100.0f; /* follows the fonts */
}

static int pageMargin_DocumentView_(const iDocumentView *d, int columnWidth) {
    /* Margins are relative to the page size. */
    const iRect viewport = pageViewport_DocumentView(d);
    const int   pageSize = iMin(columnWidth, height_Rect(viewport));
    const int   margin   = iMax(2 * gap_UI, pageSize / 14);
    if (columnWidth < width_Rect(viewport)) {
        return margin;
    }
    /* A single column on the page gives way to text that does not fit. Phone-sized pages
       may go down to half of the margin, but wider pages keep their margins like a book. */
    const float wideness = iClamp((columnWidth / (float) gap_UI - 100.0f) / 100.0f, 0.0f, 1.0f);
    const int   minimum  = margin * (0.5f + 0.5f * wideness);
    const int   wanted   = preferredWidth_DocumentView_(d, 1) * textZoom_();
    return iMax(iMax(2 * gap_UI, minimum), iMin(margin, (columnWidth - wanted) / 2));
}

static int maxWidthInColumn_DocumentView_(const iDocumentView *d, int columnWidth) {
    return columnWidth - 2 * pageMargin_DocumentView_(d, columnWidth);
}

static int pagesFor_DocumentView_(const iDocumentView *d, int viewportWidth) {
    /* Two pages when both fit at the preferred width of the actual text size. */
    return preferredWidth_DocumentView_(d, 2) * textZoom_() <=
                   maxWidthInColumn_DocumentView_(d, viewportWidth / 2)
               ? 2
               : 1;
}

static int columnsPerPageFor_DocumentView_(const iDocumentView *d, int pageWidth) {
    /* Two columns when both fit at the preferred width of the actual text size, so small
       text is set in narrower columns. */
    return preferredWidth_DocumentView_(d, 2) * textZoom_() * 0.75f <=
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

static int widthFor_DocumentView_(const iDocumentView *d, int viewportWidth, int columns,
                                  int columnsPerPage) {
    int preferred = preferredWidth_DocumentView_(d, columns);
    if (columnsPerPage == 1) {
        /* Lines of a single column follow the text size. */
        preferred *= textZoom_();
    }
    return iMin(preferred, maxWidthInColumn_DocumentView_(d, viewportWidth / columns));
}

static int maxContentWidthFor_DocumentView_(const iDocumentView *d, int width, int canvasWidth,
                                            int columnsPerPage) {
    /* Preformatted blocks may use half of the margins, but the columns of a page have only
       half of the usual space between them (see `columnShift_DocumentView`). */
    return iMax(width,
                columnsPerPage == 2
                    ? width + (canvasWidth - width) / 2
                    : canvasWidth - pageMargin_DocumentView_(d, canvasWidth));
}

int pagedDocumentWidth_DocumentView(const iDocumentView *d) {
    const int width = width_Rect(pageViewport_DocumentView(d));
    int       perPage;
    const int columns = columnsFor_DocumentView_(d, width, &perPage);
    return widthFor_DocumentView_(d, width, columns, perPage);
}

void pagedMetrics_DocumentView(const iDocumentView *d, iGmDocumentMetrics *metrics) {
    const iRect viewport  = pageViewport_DocumentView(d);
    const iWidget *footer = hostFooterButtons_DocumentView(d);
    const int columns =
        columnsFor_DocumentView_(d, width_Rect(viewport), &metrics->columnsPerPage);
    metrics->width           = widthFor_DocumentView_(d, width_Rect(viewport), columns,
                                                      metrics->columnsPerPage);
    metrics->canvasWidth     = width_Rect(viewport) / columns;
    metrics->maxContentWidth = maxContentWidthFor_DocumentView_(
        d, metrics->width, metrics->canvasWidth, metrics->columnsPerPage);
    if (metrics->columnsPerPage == 2) {
        const int pages = columns / 2;
        metrics->singleColumnWidth = widthFor_DocumentView_(d, width_Rect(viewport), pages, 1);
        metrics->singleColumnMaxContentWidth = maxContentWidthFor_DocumentView_(
            d, metrics->singleColumnWidth, 2 * metrics->canvasWidth, 1);
    }
    metrics->pageHeight      = height_Rect(viewport);
    metrics->lastPageInset   = footer ? height_Widget(footer) : 0;
    const int clearance      = hostFooterClearance_DocumentView(d);
    metrics->bottomClearance = clearance ? hostPhoneBarsHeight_DocumentView(d) + clearance : 0;
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

static iBool isTurning_DocumentView_(const iDocumentView *d) {
    return d->paged.turnFrom != d->paged.column &&
           (d->paged.isDragging || !isFinished_Anim(&d->paged.turnOffset));
}

iBool isCurling_DocumentView(const iDocumentView *d) {
    return prefs_App()->pageTurn == curl_PageTurnAnimation && isTurning_DocumentView_(d);
}

int spreadWidth_DocumentView(const iDocumentView *d) {
    return numColumns_DocumentView(d) * columnWidth_DocumentView_(d);
}

static int spreadStride_DocumentView_(const iDocumentView *d) {
    /* Adjacent spreads are separated by a gap during turns. */
    return spreadWidth_DocumentView(d) + 3 * gap_UI;
}

float curlProgress_DocumentView(const iDocumentView *d) {
    const int   stride = spreadStride_DocumentView_(d);
    const float offset = stride > 0 ? value_Anim(&d->paged.turnOffset) / stride : 0.0f;
    return iClamp((d->paged.column > d->paged.turnFrom ? 1.0f : 0.0f) - offset, 0.0f, 1.0f);
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
    if (isCurling_DocumentView(d)) {
        /* Both spreads stay in place. */
        return left_Rect(pageViewport_DocumentView(d)) +
               (rel - spread * n) * columnWidth_DocumentView_(d);
    }
    return left_Rect(pageViewport_DocumentView(d)) + spread * spreadStride_DocumentView_(d) +
           (rel - spread * n) * columnWidth_DocumentView_(d) +
           (int) value_Anim(&d->paged.turnOffset);
}

int columnShift_DocumentView(const iDocumentView *d, size_t column) {
    const int numPages    = numPages_DocumentView(d);
    const int colsPerPage = columnsPerPage_GmDocument(d->doc);
    if (colsPerPage == 1 && numPages == 1) {
        return 0;
    }
    const int gap = columnWidth_DocumentView_(d) - size_GmDocument(d->doc).x;
    if (numPages == 2) {
        /* On facing pages, the columns next to the crease are moved away from it, halfway
           toward the adjacent column. */
        const float amount = 0.5f;
        /* The two columns of a page are closer to each other than to the edges of the page.
           The outer columns move inward for wider outer margins. */
        if (colsPerPage == 2) {
            const float outer = 0.25f;
            switch (column % 4) {
                case 0: return gap * outer;
                case 1: return -gap * amount;
                case 2: return gap * amount;
                default: return -gap * outer;
            }
        }
        else {
            return (column % 2 == 0 ? -1 : 1) * gap * 0.05f; /* away from the crease */
        }
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

int pageHeaderTextTop_DocumentView(const iDocumentView *d) {
    const int topInset = metrics_GmDocument(d->doc)->topInset;
    if (isHeaderBelowCutout_GmDocument(d->doc)) {
        /* The cutout ends ~11 pt above the safe area. */
        return topInset + 3 * get_Window()->pixelRatio;
    }
    return topInset + (pageBandHeight_GmDocument(d->doc) -
                       lineHeight_Text(pageBandFont_GmDocument(d->doc))) / 2;
}

int pageFooterTextTop_DocumentView(const iDocumentView *d) {
    return height_Rect(pageViewport_DocumentView(d)) - pageFooterHeight_GmDocument(d->doc) +
           (pageBandHeight_GmDocument(d->doc) - lineHeight_Text(pageBandFont_GmDocument(d->doc))) / 2;
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
    const int n          = numColumns_DocumentView(d);
    const int pageHeight = pageHeight_GmDocument(d->doc);
    /* The current spread and the next one, which can be prerendered. During a turn, the
       previous and the current spread. */
    allocPages_VisBuf(d->visBuf, init_I2(columnWidth_DocumentView_(d), pageHeight), 2 * n);
    setPages_VisBuf(d->visBuf, visibleColumns_DocumentView(d).start * pageHeight);
}

/*----------------------------------------------------------------------------------------------*/

size_t anchor_DocumentView(const iDocumentView *d) {
    if (d->paged.anchor != iInvalidPos) {
        return d->paged.anchor;
    }
    /* The first text of the spread. During a turn, the previous spread is also visible. */
    const iString *source = source_GmDocument(d->doc);
    for (const iGmRun *run = d->visibleRuns.start; run && run <= d->visibleRuns.end; run++) {
        if (run->flags & decoration_GmRunFlag || run->mediaId ||
            columnAt_GmDocument(d->doc, top_Rect(run->visBounds)) < d->paged.column) {
            continue;
        }
        const char *loc = run->text.start;
        if (loc >= cstr_String(source) && loc < constEnd_String(source)) {
            return loc - cstr_String(source);
        }
    }
    return iInvalidPos;
}

void setAnchor_DocumentView(iDocumentView *d, size_t sourceOffset) {
    d->paged.anchor = sourceOffset;
}

float sourcePosition_DocumentView(const iDocumentView *d) {
    const size_t size   = size_String(source_GmDocument(d->doc));
    const size_t anchor = anchor_DocumentView(d);
    return size > 0 && anchor != iInvalidPos ? (float) ((double) anchor / size) : 0.0f;
}

void showSourcePosition_DocumentView(iDocumentView *d, float pos) {
    const iString *source = source_GmDocument(d->doc);
    const size_t   offset = (size_t) (iClamp(pos, 0.0f, 1.0f) * (double) size_String(source) + 0.5);
    const iGmRun  *run    = findRunAtLoc_GmDocument(d->doc, cstr_String(source) + offset);
    if (run) {
        showDocumentY_DocumentView(d, top_Rect(run->visBounds));
        d->paged.anchor = offset; /* kept until the page is turned */
    }
}

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
    hostPagesMoved_DocumentView(d);
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

void settlePageTurn_DocumentView(iDocumentView *d) {
    if (d->paged.isDragging || isFinished_Anim(&d->paged.turnOffset)) {
        return;
    }
    init_Anim(&d->paged.turnOffset, targetValue_Anim(&d->paged.turnOffset));
    removeTicker_App(animatePageTurn_DocumentView, d);
    animatePageTurn_DocumentView(d);
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
        hostPageTurnBegins_DocumentView(d);
        d->paged.turnFrom = d->paged.column;
        init_Anim(&d->paged.turnOffset, spreads * spreadStride_DocumentView_(d));
        setValueEased_Anim(&d->paged.turnOffset,
                           0,
                           beginPageTurn_DocumentView(d, pageTurnDuration_DocumentView(d)));
        addTicker_App(animatePageTurn_DocumentView, d);
    }
    else {
        init_Anim(&d->paged.turnOffset, 0);
    }
    d->paged.column    = column;
    d->paged.anchor    = iInvalidPos;
    updateVisible_DocumentView(d);
    refresh_Widget(d->owner);
}

void showDocumentY_DocumentView(iDocumentView *d, int documentY) {
    showColumn_DocumentView(d, columnAt_GmDocument(d->doc, documentY), iFalse);
}

static void hideReadingBars_DocumentView_(iDocumentView *d) {
    iWidget *w = as_Widget(d->owner);
    if (as_Widget(document_Root(w->root)) == w && isReadingMode_Root(w->root) &&
        !w->root->areReadingBarsHidden) {
        showReadingBars_Root(w->root, iFalse);
    }
}

iBool turnPage_DocumentView(iDocumentView *d, int dir) {
    const int n      = numColumns_DocumentView(d);
    const int target = (int) d->paged.column + dir * n;
    if (target < 0 || target >= (int) numColumns_GmDocument(d->doc)) {
        postCommand_Widget(d->owner, "document.page.overflow arg:%d", dir);
        return iFalse;
    }
    /* Swipes animate regardless of the settings. */
    showColumn_DocumentView(
        d, target, prefs_App()->uiAnimations && prefs_App()->pageTurn != none_PageTurnAnimation);
    hideReadingBars_DocumentView_(d);
    return iTrue;
}

int pageTurnZone_DocumentView(const iDocumentView *d, iInt2 windowPos) {
    const iRect viewport = pageViewport_DocumentView(d);
    iRect       tapRect  = viewport;
    if (isMobile_Platform()) {
        /* Side safe areas next to the page can be tapped, too. */
        const iRoot *root = constAs_Widget(d->owner)->root;
        const iRect  full = rect_Root(root);
        const iRect  safe = safeRect_Root(root);
        if (left_Rect(viewport) <= left_Rect(safe)) {
            adjustEdges_Rect(&tapRect, 0, 0, 0, left_Rect(full) - left_Rect(tapRect));
        }
        if (right_Rect(viewport) >= right_Rect(safe)) {
            adjustEdges_Rect(&tapRect, 0, right_Rect(full) - right_Rect(tapRect), 0, 0);
        }
    }
    if (!contains_Rect(tapRect, windowPos)) {
        return 0;
    }
    /* The margins may be too narrow for tapping. */
    const int left  = left_Rect(viewport) + width_Rect(viewport) / 5;
    const int right = right_Rect(viewport) - width_Rect(viewport) / 5;
    return windowPos.x < left ? -1 : windowPos.x >= right ? +1 : 0;
}

void dragPage_DocumentView(iDocumentView *d, int offset) {
    const int n      = numColumns_DocumentView(d);
    const int spread = spreadStride_DocumentView_(d);
    if (!d->paged.isDragging) {
        hostPageTurnBegins_DocumentView(d);
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
        d->paged.anchor    = iInvalidPos;
        updateVisible_DocumentView(d);
    }
    hostPagesMoved_DocumentView(d);
    refresh_Widget(d->owner);
}

void finishPageTurn_Anim(iAnim *anim, float to, float gestureSpeed, uint32_t lastMoveTime,
                         uint32_t maxSpan) {
    /* The turn starts at least as fast as the gesture, but no slower than usual. */
    const float    remaining = to - value_Anim(anim);
    uint32_t       span      = maxSpan;
    if (SDL_GetTicks() - lastMoveTime < 100) {
        const float speed = gestureSpeed * (remaining < 0 ? -1 : 1);
        if (speed > 0) {
            /* Ease out starts at twice the average speed. */
            span = iClamp(2000.0f * fabsf(remaining) / speed, 1, maxSpan);
        }
    }
    setFlags_Anim(anim, easeBoth_AnimFlag, iFalse);
    setFlags_Anim(anim, easeOut_AnimFlag, iTrue);
    setValue_Anim(anim, to, span);
}

static void finishDrag_DocumentView_(iDocumentView *d, float to, iBool followGesture) {
    finishPageTurn_Anim(&d->paged.turnOffset,
                        to,
                        followGesture ? d->paged.dragSpeed : 0.0f,
                        d->paged.dragTime,
                        pageTurnDuration_DocumentView(d));
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
        hideReadingBars_DocumentView_(d);
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
