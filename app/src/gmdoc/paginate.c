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

#include "paginate.h"
#include "app.h"
#include "render/text.h"
#include "ui/metrics.h"
#include "ui/window.h"

#include <lagrange/lang.h>

/* Pagination: the continuous layout is cut into columns that begin at multiples of the page
   height. A page has one or more columns, and a header and a footer. */

static int cutoutShift_GmDocument_(const iGmDocument *d) {
    /* The header is next to the cutout, so the content area moves up. */
    return isHeaderBelowCutout_GmDocument(d) ? lineHeight_Text(paragraph_FontId) * 4 / 5 : 0;
}

static iBool isAttachedDecoration_GmRun_(const iGmRun *d) {
    return d->flags & decoration_GmRunFlag && ~d->flags & caption_GmRunFlag;
}

static size_t groupEnd_GmDocument_(const iArray *runs, size_t start) {
    /* A group of runs is kept on the same page. Decorations belong to the run that follows
       them, and captions to the run that precedes them. */
    const size_t n = size_Array(runs);
    size_t       i = start;
    while (i < n && isAttachedDecoration_GmRun_(constAt_Array(runs, i))) {
        i++;
    }
    if (i < n) {
        const int top = top_Rect(((const iGmRun *) constAt_Array(runs, i))->visBounds);
        for (i++; i < n; i++) {
            const iGmRun *run = constAt_Array(runs, i);
            if (~run->flags & caption_GmRunFlag &&
                (isAttachedDecoration_GmRun_(run) || top_Rect(run->visBounds) != top)) {
                break;
            }
        }
    }
    return i;
}

static iRangei groupSpan_GmDocument_(const iArray *runs, size_t start, size_t end) {
    iRangei span = { INT_MAX, INT_MIN };
    for (size_t i = start; i < end; i++) {
        const iRangei y = ySpan_Rect(((const iGmRun *) constAt_Array(runs, i))->visBounds);
        span.start = iMin(span.start, y.start);
        span.end   = iMax(span.end, y.end);
    }
    return span;
}

static iBool isHeadingGroup_GmDocument_(const iArray *runs, size_t start, size_t end) {
    for (size_t i = start; i < end; i++) {
        const iGmRun *run = constAt_Array(runs, i);
        if (~run->flags & decoration_GmRunFlag) {
            return run->lineType >= heading1_GmLineType && run->lineType <= heading3_GmLineType;
        }
    }
    return iFalse;
}

static iBool isChapterGroup_GmDocument_(const iArray *runs, size_t start, size_t end) {
    /* The first line of a level 1 heading. */
    for (size_t i = start; i < end; i++) {
        const iGmRun *run = constAt_Array(runs, i);
        if (~run->flags & decoration_GmRunFlag) {
            return run->lineType == heading1_GmLineType && run->flags & startOfLine_GmRunFlag;
        }
    }
    return iFalse;
}

static iRangecc auxText_GmDocument_(iGmDocument *d, const iString *text) {
    pushBack_StringArray(&d->auxText, text);
    return range_String(constAt_StringArray(&d->auxText, size_StringArray(&d->auxText) - 1));
}

static iRangecc fitPageBandText_GmDocument_(iGmDocument *d, iRangecc text) {
    /* Shortened to fit within the paragraph lines of the page, with an ellipsis. Centered, so
       the wider side margin applies to both sides. */
    const int font     = pageBandFont_GmDocument(d);
    const int margin   = iMax(d->style.indents[text_GmLineType],
                              d->style.rightMargins[text_GmLineType]) * gap_Text;
    const int maxWidth = (columnsPerPage_GmDocument(d) - 1) * d->metrics.canvasWidth + d->size.x -
                         2 * margin;
    const char *end;
    tryAdvance_Text(font, text, maxWidth, &end);
    if (end < text.end) {
        const char *ellipsis = "\u2026";
        tryAdvance_Text(font, text, maxWidth - measure_Text(font, ellipsis).advance.x, &end);
        iString *str = newRange_String((iRangecc){ text.start, end });
        trimEnd_String(str);
        appendCStr_String(str, ellipsis);
        text = auxText_GmDocument_(d, str);
        delete_String(str);
    }
    return text;
}

static void breakColumn_GmDocument_(const iGmDocument *d, size_t *column, iBool isNewPage) {
    (*column)++;
    if (isNewPage) {
        const size_t perPage = columnsPerPage_GmDocument(d);
        *column = (*column + perPage - 1) / perPage * perPage;
    }
}

static iRangecc headingText_GmDocument_(const iGmDocument *d, const iGmRun *run) {
    /* Full text of a heading that begins with `run`. */
    iConstForEach(Array, i, &d->headings) {
        const iGmHeading *head = i.value;
        if (head->text.start == run->text.start) {
            return head->text;
        }
    }
    return run->text;
}

static void setPageBands_GmDocument_(iGmDocument *d) {
    const size_t numPages = numPages_GmDocument(d);
    const int    pageSpan = columnsPerPage_GmDocument(d) * d->metrics.pageHeight;
    /* Headers show the last level 1 heading on or before the page, except on the first page
       and on a page that begins with one. The first page of a book's chapter has it unless the
       heading is on the page. A single-page chapter has the book's title instead. */
    const iBool isBook = !isEmpty_String(&d->bookTitle);
    iRangecc title  = auxText_GmDocument_(d, collect_String(upper_String(&d->chapterTitle)));
    const iRangecc bookTitle =
        auxText_GmDocument_(d, collect_String(upper_String(&d->bookTitle)));
    size_t   runPos = 0;
    clear_Array(&d->pageBands);
    for (size_t page = 0; page < numPages; page++) {
        const int pageEnd        = (int) (page + 1) * pageSpan;
        iBool     isFirstRun     = iTrue;
        iBool     isChapterStart = iFalse;
        iBool     hasHeading     = iFalse;
        for (; runPos < size_Array(&d->layout); runPos++) {
            const iGmRun *run = constAt_Array(&d->layout, runPos);
            if (top_Rect(run->visBounds) >= pageEnd) {
                break;
            }
            if (run->flags & decoration_GmRunFlag) {
                continue;
            }
            const iBool isHeading =
                run->lineType == heading1_GmLineType && run->flags & startOfLine_GmRunFlag;
            if (isFirstRun) {
                isChapterStart = isHeading;
                isFirstRun     = iFalse;
            }
            if (isHeading) {
                hasHeading = iTrue;
                iString *text = collectNewRange_String(headingText_GmDocument_(d, run));
                title = auxText_GmDocument_(d, collect_String(upper_String(text)));
            }
        }
        iGmPageBands bands = { iNullRange, iNullRange, iNullRange };
        const iBool isCoverStart = d->flags.isBookCover && page == 0;
        const iBool hasHeader    = page > 0 ? !isChapterStart
                                            : isBook && !isCoverStart && !hasHeading;
        if (hasHeader && !isEmpty_Range(&title)) {
            bands.header = fitPageBandText_GmDocument_(d, title);
        }
        else if (isBook && !isCoverStart && numPages == 1) {
            bands.header = fitPageBandText_GmDocument_(d, bookTitle);
        }
        /* The book's title is shown also where the chapter title is. */
        if (!isEmpty_Range(&bookTitle) && !isCoverStart) {
            bands.bookHeader = fitPageBandText_GmDocument_(d, bookTitle);
        }
        const iBool hasPages    = numPages > 1;
        const iBool hasChapters = d->chapter > 0 && d->numChapters > 1;
        if (!d->flags.isBookCover && (hasPages || hasChapters)) {
            bands.footer = fitPageBandText_GmDocument_(
                d,
                auxText_GmDocument_(
                    d,
                    hasPages && hasChapters
                        ? collectNewFormat_String(cstr_Lang("doc.page.chapter"),
                                                  (int) page + 1, (int) numPages,
                                                  d->chapter, d->numChapters)
                    : hasPages ? collectNewFormat_String(cstr_Lang("doc.page.number"),
                                                         (int) page + 1, (int) numPages)
                               : collectNewFormat_String(cstr_Lang("doc.chapter.number"),
                                                         d->chapter, d->numChapters)));
        }
        pushBack_Array(&d->pageBands, &bands);
    }
}

static iBool isContinuationGroup_GmDocument_(const iArray *runs, size_t start, size_t end) {
    /* A wrapped line that continues a paragraph. */
    for (size_t i = start; i < end; i++) {
        const iGmRun *run = constAt_Array(runs, i);
        if (~run->flags & decoration_GmRunFlag) {
            return !isMedia_GmRun(run) && ~run->flags & startOfLine_GmRunFlag;
        }
    }
    return iFalse;
}

iDeclareType(ColumnGroup)

struct Impl_ColumnGroup {
    size_t   src;       /* index of the first run in the continuous layout */
    size_t   dest;      /* index of the first run in the paginated layout */
    uint16_t lastPreId; /* before the group was placed */
};

static size_t groupSrcEnd_(const iArray *groups, size_t index, size_t breakPos) {
    return index + 1 < size_Array(groups)
               ? ((const iColumnGroup *) constAt_Array(groups, index + 1))->src
               : breakPos;
}

static const iGmRun *firstContentRun_GmDocument_(const iArray *runs, size_t start, size_t end) {
    for (size_t i = start; i < end; i++) {
        const iGmRun *run = constAt_Array(runs, i);
        if (~run->flags & decoration_GmRunFlag) {
            return run;
        }
    }
    return NULL;
}

static size_t firstMovedGroup_GmDocument_(const iArray *src, const iArray *groups, size_t breakPos) {
    /* Index of the first group in the column that continues in the next column along with
       the group at `breakPos`. Headings are not left at the end of a column, not even with
       only the first line of the following paragraph. An image is not separated from the
       link above it. */
    const size_t n = size_Array(groups);
    size_t       moved = n;
    if (n >= 2 && isContinuationGroup_GmDocument_(src, breakPos, groupEnd_GmDocument_(src, breakPos)) &&
        !isContinuationGroup_GmDocument_(
            src, ((const iColumnGroup *) constAt_Array(groups, n - 1))->src, breakPos)) {
        moved = n - 1;
    }
    iBool         isCaptionMoved = iFalse;
    const iGmRun *image = firstContentRun_GmDocument_(src, breakPos, groupEnd_GmDocument_(src, breakPos));
    if (image && image->linkId && image->mediaType == image_MediaType) {
        while (moved > 0) {
            const iGmRun *caption = firstContentRun_GmDocument_(
                src,
                ((const iColumnGroup *) constAt_Array(groups, moved - 1))->src,
                groupSrcEnd_(groups, moved - 1, breakPos));
            if (!caption || isMedia_GmRun(caption) || caption->linkId != image->linkId) {
                break;
            }
            moved--;
            isCaptionMoved = iTrue;
        }
    }
    size_t first = moved;
    while (first > 0 &&
           isHeadingGroup_GmDocument_(src,
                                      ((const iColumnGroup *) constAt_Array(groups, first - 1))->src,
                                      groupSrcEnd_(groups, first - 1, breakPos))) {
        first--;
    }
    /* A heading at the top of the column stays, so it is not split either. */
    return first > 0 && (first < moved || isCaptionMoved) ? first : n;
}

static size_t keptWithHeadingEnd_GmDocument_(const iArray *src, size_t start) {
    /* End of the headings beginning at `start`, and the first two lines of what follows. */
    const size_t numSrc = size_Array(src);
    size_t       end    = groupEnd_GmDocument_(src, start);
    while (end < numSrc && isHeadingGroup_GmDocument_(src, start, end)) {
        start = end;
        end   = groupEnd_GmDocument_(src, start);
    }
    if (end < numSrc && isContinuationGroup_GmDocument_(src, end, groupEnd_GmDocument_(src, end))) {
        end = groupEnd_GmDocument_(src, end);
    }
    return end;
}

enum iPageAlign {
    top_PageAlign,
    center_PageAlign,
    centerContent_PageAlign, /* the margin above the first run is not kept */
};

static void alignPage_GmDocument_(iGmDocument *d, size_t start, iRangei content,
                                  enum iPageAlign align, uint16_t firstPreId, uint16_t lastPreId) {
    /* Runs from `start` onwards are on the page and begin at the top of `content`. */
    if (align == top_PageAlign ||
        (!d->docPrefs.centerShortDocs.value && !d->flags.isBookCover) ||
        start >= size_Array(&d->layout)) {
        return;
    }
    int top    = content.end;
    int bottom = content.start;
    for (size_t i = start; i < size_Array(&d->layout); i++) {
        const iRect vis = ((const iGmRun *) constAt_Array(&d->layout, i))->visBounds;
        top    = iMin(top, top_Rect(vis));
        bottom = iMax(bottom, bottom_Rect(vis));
    }
    if (align != centerContent_PageAlign) {
        top = content.start;
    }
    const int space = size_Range(&content) - (bottom - top);
    if (space <= 0) {
        return;
    }
    const int offset = content.start - top + space / 2;
    for (size_t i = start; i < size_Array(&d->layout); i++) {
        iGmRun *run = at_Array(&d->layout, i);
        run->visBounds.pos.y += offset;
        if (!isEmpty_Rect(run->bounds)) {
            run->bounds.pos.y += offset;
        }
    }
    for (uint16_t preId = firstPreId; preId && preId <= lastPreId; preId++) {
        ((iGmPreMeta *) at_Array(&d->preMeta, preId - 1))->pixelRect.pos.y += offset;
    }
}

void paginate_GmDocument_(iGmDocument *d) {
    const iArray *src        = collect_Array(copy_Array(&d->layout));
    const size_t  numSrc     = size_Array(src);
    const size_t  perPage    = columnsPerPage_GmDocument(d);
    clear_Array(&d->layout);
    size_t   column     = 0;
    iRangei  content    = columnContentRange_GmDocument(d, column);
    int      offset     = content.start; /* the continuous layout begins at zero */
    int      lastBottom = content.start;
    size_t   firstGroup = 0;
    iArray   colGroups;                /* groups placed in the column */
    size_t   colStart   = 0;           /* layout index of the first run in the column */
    uint16_t lastPreId  = 0;
    uint16_t colPreId   = 1;           /* first preformatted block that may begin in the column */
    int      srcBottom  = 0;           /* bottom of the placed runs in the continuous layout */
    iBool    hasTitle   = iFalse;      /* the first level 1 heading is a title, not a chapter */
    init_Array(&colGroups, sizeof(iColumnGroup));
    for (size_t i = 0; i < numSrc;) {
        size_t        end       = groupEnd_GmDocument_(src, i);
        const iRangei span      = groupSpan_GmDocument_(src, i, end);
        const iBool   isChapter = isChapterGroup_GmDocument_(src, i, end) && (hasTitle || column > 0);
        if (i != firstGroup && (isChapter || span.end + offset > content.end)) {
            /* Continue in the next column. Some of the placed groups may move along. */
            const size_t moved = isChapter ? size_Array(&colGroups)
                                           : firstMovedGroup_GmDocument_(src, &colGroups, i);
            if (moved < size_Array(&colGroups)) {
                const iColumnGroup *group = constAt_Array(&colGroups, moved);
                i = group->src;
                resize_Array(&d->layout, group->dest);
                for (uint16_t preId = group->lastPreId + 1; preId <= lastPreId; preId++) {
                    ((iGmPreMeta *) at_Array(&d->preMeta, preId - 1))->pixelRect.pos.y -= offset;
                }
                lastPreId = group->lastPreId;
            }
            /* Columns are centered, but a chapter's last column is aligned to the top. */
            alignPage_GmDocument_(d, colStart, content,
                                  isChapter && !d->flags.isBookCover ? top_PageAlign
                                                                     : center_PageAlign,
                                  colPreId, lastPreId);
            /* Chapters begin on a new page. */
            breakColumn_GmDocument_(d, &column, isChapter);
            colStart   = size_Array(&d->layout);
            colPreId   = lastPreId + 1;
            content    = columnContentRange_GmDocument(d, column);
            end        = groupEnd_GmDocument_(src, i);
            const iRangei newSpan = groupSpan_GmDocument_(src, i, end);
            offset     = content.start - newSpan.start;
            if (isChapter) {
                /* The margin above the heading remains at the top of the page, as far as the
                   heading and the line following it fit. */
                const iRangei block =
                    groupSpan_GmDocument_(src, i, keptWithHeadingEnd_GmDocument_(src, i));
                offset += iClamp(content.end - (block.end + offset), 0,
                                 iMax(0, newSpan.start - srcBottom));
            }
            lastBottom = content.start;
            firstGroup = i;
            clear_Array(&colGroups);
            continue;
        }
        if (isChapterGroup_GmDocument_(src, i, end)) {
            hasTitle = iTrue;
        }
        pushBack_Array(&colGroups,
                       &(iColumnGroup){ .src = i, .dest = size_Array(&d->layout), .lastPreId = lastPreId });
        for (; i < end; i++) {
            iGmRun run = *(const iGmRun *) constAt_Array(src, i);
            run.visBounds.pos.y += offset;
            if (!isEmpty_Rect(run.bounds)) {
                run.bounds.pos.y += offset;
            }
            const uint16_t preId = preId_GmRun(&run);
            if (preId > lastPreId) {
                /* First run of a preformatted block. */
                iGmPreMeta *meta = at_Array(&d->preMeta, preId - 1);
                meta->pixelRect.pos.y += offset;
                lastPreId = preId;
            }
            lastBottom = iMax(lastBottom, bottom_Rect(run.visBounds));
            srcBottom  = iMax(srcBottom, bottom_Rect(run.visBounds) - offset);
            pushBack_Array(&d->layout, &run);
        }
    }
    /* The last column with content is aligned to the top, unless it is the only one or on a
       cover. The inset at the bottom of the page's last column is needed only if the content
       reaches it. */
    const iBool isLastOnPage   = (column + 1) % perPage == 0;
    const iBool needsInsetPage = isLastOnPage && content.end - lastBottom < d->metrics.lastPageInset;
    if (isLastOnPage && !needsInsetPage) {
        content.end -= d->metrics.lastPageInset;
    }
    alignPage_GmDocument_(d, colStart, content,
                          column == 0 || d->flags.isBookCover ? centerContent_PageAlign
                                                              : top_PageAlign,
                          colPreId, lastPreId);
    if (needsInsetPage) {
        breakColumn_GmDocument_(d, &column, iFalse);
    }
    /* The final page is complete, even if some of its columns are empty. */
    d->numFilledColumns = column + 1;
    const size_t numColumns = (column + perPage) / perPage * perPage;
    d->size.y = (int) numColumns * d->metrics.pageHeight;
    setPageBands_GmDocument_(d);
    deinit_Array(&colGroups);
}

int pageBandFont_GmDocument(const iGmDocument *d) {
    /* Same family as the document's headings. */
    return FONT_ID(isForcedMonospace_GmDocument_(d) ? documentMonospace_FontId
                                                    : documentHeading_FontId,
                   semiBold_FontStyle,
                   deviceType_App() == phone_AppDeviceType ? contentTiny_FontSize
                                                           : contentSmall_FontSize);
}

int pageBandHeight_GmDocument(const iGmDocument *d) {
    return pageBandHeightFor_GmDocument(d, d->metrics.pageHeight);
}

int pageBandHeightFor_GmDocument(const iGmDocument *d, int pageHeight) {
    return iMax(2 * lineHeight_Text(pageBandFont_GmDocument(d)), pageHeight / 14);
}

iBool isHeaderBelowCutout_GmDocument(const iGmDocument *d) {
    /* A phone in portrait has a camera cutout in the top safe area. */
    return deviceType_App() == phone_AppDeviceType &&
           d->metrics.topInset > 40 * get_Window()->pixelRatio;
}

int pageHeaderHeight_GmDocument(const iGmDocument *d) {
    return pageBandHeight_GmDocument(d) + d->metrics.topInset - cutoutShift_GmDocument_(d);
}

int pageFooterHeight_GmDocument(const iGmDocument *d) {
    const int band = pageBandHeight_GmDocument(d);
    if (deviceType_App() == phone_AppDeviceType) {
        /* The text is just above the safe area. The progress bar is at the bottom edge while
           the reading bars are hidden. */
        const int lineHeight = lineHeight_Text(pageBandFont_GmDocument(d));
        return iMax(d->metrics.bottomInset, 3 * gap_UI) + gap_UI + (band + lineHeight) / 2 +
               cutoutShift_GmDocument_(d);
    }
    /* The reading progress bar and the find bar are below the footer. */
    return band + 3 * gap_UI + d->metrics.bottomInset;
}

iRangei columnContentRange_GmDocument(const iGmDocument *d, size_t column) {
    const int top = (int) column * d->metrics.pageHeight;
    return (iRangei){ top + pageHeaderHeight_GmDocument(d) +
                          ((int) column < columnsPerPage_GmDocument(d) ? d->metrics.firstPageInset : 0),
                      top + d->metrics.pageHeight - pageFooterHeight_GmDocument(d) };
}
