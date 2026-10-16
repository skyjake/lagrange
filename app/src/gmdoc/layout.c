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

#include "layout.h"
#include "parser.h"
#include "typesetter.h"
#include "render/text.h"

/* Layout is done progressively in time slices. An anchored layout begins at a visible line,
   and the content above it is laid out separately and spliced in front. */

static void typesetAnchored_Typesetter_(iTypesetter *d, const iGmLayoutAnchor *anchor,
                                        const char *leadIn) {
    /* Lays out the visible part of the document, seeded from a preceding line. */
    iGmDocument   *doc  = d->doc;
    const iRangecc src  = range_String(&doc->source);
    const char    *seam = lineStart_Rangecc_(src, src.start + anchor->startPos);
    beginAt_Typesetter_(d, leadIn);
    d->seamPos = seam;
    typesetLines_Typesetter_(d, INT_MAX, 0.0);
    iAssert(d->isAtSeam);
    /* The lines before the seam only set up the state. */
    d->earlierBottom = lastVisibleRunBottom_Typesetter_(d);
    clear_Array(d->layout);
    d->seamPos     = NULL;
    d->isAtSeam    = iFalse;
    d->contentLine = (iRangecc){ seam - 1, seam - 1 };
    d->keepPos     = src.start + anchor->pos;
    d->syncSpan    = anchor->bottom - anchor->top;
    const iGmRun *kept = layoutUntilKept_Typesetter_(d);
    if (kept) {
        shift_Typesetter_(d, anchor->top - top_Rect(kept->visBounds));
    }
    d->above = malloc(sizeof(iTypesetter));
    init_Typesetter_(d->above, doc, iTrue);
    begin_Typesetter_(d->above);
    d->above->seamPos = seam;
    typesetSome_Typesetter_(d, anchor->bottom, 0.0);
}

static iTypesetter *spliceAbove_GmDocument_(iGmDocument *d) {
    /* The content above the seam has been laid out. */
    iTypesetter *ts    = d->typesetter;
    iTypesetter *above = ts->above;
    ts->above = NULL;
    if (isSame_TypesetterSeam_(&above->seam, &ts->seam)) {
        const int dy = above->seam.y - ts->seam.y;
        shift_Typesetter_(ts, dy);
        iGmRebase rebase;
        init_GmRebase(&rebase, d);
        const size_t n = size_Array(&above->ownLayout);
        insertN_Array(&d->layout, 0, constData_Array(&above->ownLayout), n);
        d->layoutInserted += n;
        d->layoutShift    += dy;
        if (update_GmRebase(&rebase, d)) {
            rebase_GmDocument_(d, &rebase);
        }
        ts->maxRunWidth  = iMax(ts->maxRunWidth, above->maxRunWidth);
        ts->maxRunHeight = iMax(ts->maxRunHeight, above->maxRunHeight);
        if (!isEmpty_String(&above->headingTitle)) {
            set_String(&ts->headingTitle, &above->headingTitle);
        }
        if (!isEmpty_String(&above->firstContentLine)) {
            set_String(&ts->firstContentLine, &above->firstContentLine);
        }
        deinit_Typesetter_(above);
        free(above);
    }
    else {
        /* The seeded state was wrong. The layout continues from the content above, and the
           kept run stays where it is on screen. */
#if !defined (NDEBUG)
        const iTypesetterSeam *a = &above->seam, *b = &ts->seam;
        fprintf(stderr,
                "[GmDocument] seam mismatch at %zu: gap %d/%d type %d/%d %d/%d link %zu/%zu "
                "margin %d/%d pre %u/%u %d/%d indents %d/%d first %d/%d quote %d/%d "
                "blank %d/%d wrap %d/%d\n",
                (size_t) (above->seamPos - constBegin_String(&d->source)),
                a->gap, b->gap, a->prevType, b->prevType, a->prevNonBlankType,
                b->prevNonBlankType, a->nextLink, b->nextLink, a->pendingMargin,
                b->pendingMargin, a->preId, b->preId, a->isPreformat, b->isPreformat,
                a->enableIndents, b->enableIndents, a->isFirstText, b->isFirstText,
                a->addQuoteIcon, b->addQuoteIcon, a->followsBlank, b->followsBlank,
                a->prevWrapParagraph, b->prevWrapParagraph);
#endif
        const iGmRun *kept   = findKeptRun_Typesetter_(ts);
        const int     oldTop = kept ? top_Rect(kept->visBounds) : 0;
        const iArray  runs   = d->layout;
        d->layout = above->ownLayout;
        above->ownLayout = runs;
        clear_Array(&above->ownLayout);
        above->layout             = &d->layout;
        above->contentLine        = (iRangecc){ above->seamPos - 1, above->seamPos - 1 };
        above->seamPos            = NULL;
        above->isAtSeam           = iFalse;
        above->keepPos            = ts->keepPos;
        above->syncSpan           = ts->syncSpan;
        above->isWrapWidthChecked = ts->isWrapWidthChecked;
        iForEach(Array, p, &d->preMeta) {
            ((iGmPreMeta *) p.value)->runRange = (iGmRunRange){ NULL, NULL };
        }
        const iGmRun *newKept = layoutUntilKept_Typesetter_(above);
        d->layoutGen++;
        if (kept && newKept) {
            d->layoutShift += top_Rect(newKept->visBounds) - oldTop;
        }
        typesetLines_Typesetter_(above,
                                 newKept ? top_Rect(newKept->visBounds) + above->syncSpan : INT_MAX,
                                 0.0);
        d->typesetter = above;
        deinit_Typesetter_(ts);
        free(ts);
        ts = above;
    }
    return ts;
}

static void typeset_GmDocument_(iGmDocument *d) {
    deleteTypesetter_GmDocument_(d);
    iGmLayoutAnchor anchor = d->layoutAnchor;
    d->layoutAnchor.pos = iInvalidPos;
    const iRangecc src = range_String(&d->source);
    if (anchor.pos >= size_Range(&src) || anchor.startPos > anchor.pos || isPaged_GmDocument(d)) {
        anchor.pos = iInvalidPos;
    }
    const char  *keepPos = (anchor.pos != iInvalidPos ? src.start + anchor.pos : NULL);
    iTypesetter *ts      = malloc(sizeof(iTypesetter));
    init_Typesetter_(ts, d, iFalse);
    d->flags.isAnchoredLayout = iFalse;
    if (beginLayout_GmDocument_(d, keepPos != NULL)) {
        ts->isWrapWidthChecked = d->flags.isSourceFinal;
        if (keepPos) {
            parseUntil_GmDocument_(d, keepPos);
        }
        /* If there is little content above, it is laid out right away. */
        const char *leadIn =
            keepPos && anchor.startTop > anchor.bottom - anchor.startTop
                ? findLeadIn_GmDocument_(d, lineStart_Rangecc_(src, src.start + anchor.startPos))
                : NULL;
        if (leadIn) {
            d->flags.isAnchoredLayout = iTrue;
            typesetAnchored_Typesetter_(ts, &anchor, leadIn);
            d->typesetter = ts;
            return;
        }
        begin_Typesetter_(ts);
        /* Paged layouts are always done in full. */
        int minHeight = (d->layoutSyncHeight > 0 && !ts->isPaged ? d->layoutSyncHeight : INT_MAX);
        if (keepPos) {
            ts->keepPos = keepPos;
            const iGmRun *kept = layoutUntilKept_Typesetter_(ts);
            minHeight = kept ? top_Rect(kept->visBounds) + anchor.bottom - anchor.top : INT_MAX;
        }
        const iBool isCaughtUp = typesetSome_Typesetter_(ts, minHeight, 0.0);
        if (isCaughtUp && (d->flags.isSourceFinal || ts->isPaged)) {
            finish_Typesetter_(ts);
        }
        else {
            /* Continued in time slices, or when more source is received. */
            d->typesetter = ts;
            return;
        }
    }
    deinit_Typesetter_(ts);
    free(ts);
}

static void applyMetrics_GmDocument_(iGmDocument *d, const iGmDocumentMetrics *metrics) {
    if (d->metrics.maxContentWidth != metrics->maxContentWidth) {
        d->wrapWidth = 0;
    }
    d->metrics = *metrics;
    d->size.x  = metrics->width;
}

#if !defined (NDEBUG)
static void moveRuns_GmDocument_(iGmDocument *d) {
    /* Always move the runs to catch missed rebases. */
    iGmRebase rebase;
    init_GmRebase(&rebase, d);
    iArray moved;
    initCopy_Array(&moved, &d->layout);
    deinit_Array(&d->layout);
    d->layout = moved;
    if (update_GmRebase(&rebase, d)) {
        rebase_GmDocument_(d, &rebase);
    }
}

static void checkLayout_GmDocument_(iGmDocument *d) {
    /* Compares the layout with one done from the top in one go. */
    iString *layout = new_String();
    iString *check  = new_String();
    layoutDigest_GmDocument(d, layout);
    const int syncHeight = d->layoutSyncHeight;
    d->layoutSyncHeight = 0;
    doLayout_GmDocument_(d);
    d->layoutSyncHeight = syncHeight;
    layoutDigest_GmDocument(d, check);
    if (equal_String(layout, check)) {
        fprintf(stderr, "[GmDocument] layout check: ok\n");
    }
    else {
        iRangecc a = iNullRange, b = iNullRange;
        int lineNum = 0;
        while (nextSplit_Rangecc(range_String(layout), "\n", &a) &&
               nextSplit_Rangecc(range_String(check), "\n", &b)) {
            lineNum++;
            if (!equal_Rangecc(a, cstr_Rangecc(b))) {
                break;
            }
        }
        fprintf(stderr, "[GmDocument] layout check: MISMATCH at line %d\n  got: %s\n  exp: %s\n",
                lineNum, cstr_Rangecc(a), cstr_Rangecc(b));
    }
    delete_String(check);
    delete_String(layout);
}
#endif

void deleteTypesetter_GmDocument_(iGmDocument *d) {
    if (d->typesetter) {
        deinit_Typesetter_(d->typesetter);
        free(d->typesetter);
        d->typesetter = NULL;
    }
}

void rebaseTypesetter_GmDocument_(iGmDocument *d, const iGmRebase *rebase) {
    if (d->typesetter) {
        rebase_Typesetter_(d->typesetter, rebase);
    }
}

void doLayout_GmDocument_(iGmDocument *d) {
#if !defined (NDEBUG)
    iTime startTime;
    initCurrent_Time(&startTime);
#endif
    applyMetrics_GmDocument_(d, &d->requestedMetrics);
    d->numFilledColumns = 0;
    typeset_GmDocument_(d);
    if (columnsPerPage_GmDocument(d) == 2 && d->numFilledColumns == 1 &&
        d->metrics.singleColumnWidth > 0) {
        /* The page has a single column if all the content fits in the first one. */
        iGmDocumentMetrics single = d->metrics;
        single.width              = single.singleColumnWidth;
        single.maxContentWidth    = single.singleColumnMaxContentWidth;
        single.canvasWidth       *= 2;
        single.columnsPerPage     = 1;
        applyMetrics_GmDocument_(d, &single);
        typeset_GmDocument_(d);
    }
#if !defined (NDEBUG)
    logTiming_GmDocument_(d, "layout", &startTime);
    if (!d->typesetter) {
        dumpLayout_GmDocument_(d);
    }
#endif
}

void setLayoutAnchor_GmDocument(iGmDocument *d, const char *startPos, int startTop,
                                const char *pos, int top, int bottom) {
    const char *src = constBegin_String(&d->source);
    d->layoutAnchor = (iGmLayoutAnchor){ startPos - src, startTop, pos - src, top, bottom };
}

void setLayoutAnchorOffset_GmDocument(iGmDocument *d, size_t pos, int top, int bottom) {
    d->layoutAnchor = (iGmLayoutAnchor){ pos, top, pos, top, bottom };
}

iBool clearLayoutAnchor_GmDocument(iGmDocument *d) {
    const iBool wasSet = (d->layoutAnchor.pos != iInvalidPos);
    d->layoutAnchor.pos = iInvalidPos;
    return wasSet;
}

iBool isLayoutFinished_GmDocument(const iGmDocument *d) {
    return d->typesetter == NULL;
}

iBool isLayoutPending_GmDocument(const iGmDocument *d) {
    return d->typesetter &&
           (d->typesetter->above || !d->flags.isLayoutCaughtUp || d->flags.isSourceFinal);
}

iBool isLayoutIndeterminate_GmDocument(const iGmDocument *d) {
    return d->typesetter && d->typesetter->above;
}

iBool settleLayout_GmDocument(iGmDocument *d) {
    if (!isLayoutIndeterminate_GmDocument(d)) {
        return iFalse;
    }
    typesetLines_Typesetter_(d->typesetter->above, INT_MAX, 0.0);
    iTypesetter *ts = spliceAbove_GmDocument_(d);
    publish_Typesetter_(ts, d->flags.isLayoutCaughtUp);
#if !defined (NDEBUG)
    moveRuns_GmDocument_(d);
#endif
    return iTrue;
}

iBool continueLayout_GmDocument(iGmDocument *d, int minHeight, double maxSeconds) {
    iTypesetter *ts = d->typesetter;
    if (!isLayoutPending_GmDocument(d)) {
        return iFalse;
    }
#if !defined (NDEBUG)
    iTime startTime;
    initCurrent_Time(&startTime);
#endif
    iBool isSliceUsed = iFalse;
    if (ts->above) {
        if (maxSeconds <= 0.0 && minHeight != INT_MAX) {
            return iFalse; /* the content above is laid out in slices */
        }
        typesetLines_Typesetter_(ts->above, maxSeconds > 0.0 ? 0 : INT_MAX, maxSeconds);
        if (ts->above->isAtSeam) {
            ts = spliceAbove_GmDocument_(d);
            publish_Typesetter_(ts, d->flags.isLayoutCaughtUp);
        }
        isSliceUsed = (maxSeconds > 0.0);
    }
    if (!isSliceUsed) {
        if (d->flags.isSourceFinal && !ts->isWrapWidthChecked) {
            ts->isWrapWidthChecked = iTrue;
            if (isExpandingToLongLines_GmDocument_(d)) {
                /* The widest line determines the layout width. */
                const int oldWrapWidth = d->wrapWidth;
                d->wrapWidth = 0;
                setAnsiFlags_Text(d->ansiEscapes);
                determinePlainTextWrapWidth_GmDocument_(d);
                setAnsiFlags_Text(allowAll_AnsiFlag);
                if (d->wrapWidth != oldWrapWidth) {
                    doLayout_GmDocument_(d);
                    return iTrue;
                }
            }
        }
        if (typesetSome_Typesetter_(ts, minHeight, maxSeconds) && d->flags.isSourceFinal) {
            finish_Typesetter_(ts);
            deleteTypesetter_GmDocument_(d);
        }
    }
#if !defined (NDEBUG)
    logTiming_GmDocument_(d, isSliceUsed ? "above" : "continue", &startTime);
    moveRuns_GmDocument_(d);
    if (!d->typesetter) {
        dumpLayout_GmDocument_(d);
        if (d->flags.isAnchoredLayout && getenv("LAGRANGE_LAYOUT_CHECK")) {
            checkLayout_GmDocument_(d);
        }
    }
#endif
    return iTrue;
}

void finishLayout_GmDocument(iGmDocument *d) {
    continueLayout_GmDocument(d, INT_MAX, 0.0);
}
