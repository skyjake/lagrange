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

#include "impl.h"
#include "palette.h"
#include "typesetter.h"
#include "app.h"
#include "defs.h"
#include "render/text.h"
#include "ui/metrics.h"
#include "ui/window.h"

#include <lagrange/lang.h>
#include <the_Foundation/regexp.h>

static int lastVisibleRunBottom_GmDocument_(const iGmDocument *d) {
    iReverseConstForEach(Array, i, &d->layout) {
        const iGmRun *run = i.value;
        if (isEmpty_Range(&run->text)) {
            continue;
        }
        return top_Rect(run->visBounds) + height_Rect(run->visBounds) * d->docPrefs.lineSpacing.value;
    }
    return 0;
}

static iInt2 measurePreformattedBlock_GmDocument_(const iGmDocument *d, const char *start, int font,
                                                  iRangecc *contents, const char **endPos) {
    const iRangecc content = { start, constEnd_String(&d->source) };
    iRangecc line = iNullRange;
    nextSplit_Rangecc(content, "\n", &line);
    iAssert(startsWith_Rangecc(line, "```"));
    *contents = (iRangecc){ line.end + 1, line.end + 1 };
    while (nextSplit_Rangecc(content, "\n", &line)) {
        if (startsWith_Rangecc(line, "```")) {
            if (endPos) *endPos = line.end;
            break;
        }
        contents->end = line.end;
    }
    return measureRange_Text(font, *contents).bounds.size;
}

static void linkContentWasLaidOut_GmDocument_(iGmDocument *d, const iGmMediaInfo *mediaInfo,
                                              uint16_t linkId) {
    iGmLink *link = at_PtrArray(&d->links, linkId - 1);
    link->flags |= content_GmLinkFlag;
    if (mediaInfo && mediaInfo->isPermanent) {
        link->flags |= permanent_GmLinkFlag;
    }
}

static void alignDecoration_GmRun_(iGmRun *run, iBool isCentered) {
    const iRect visBounds = visualBounds_Text(run->font, run->text);
    const int   visWidth  = width_Rect(visBounds);
    int         xAdjust   = 0;
    if (!isCentered) {
        /* Keep the icon aligned to the left edge. */
        const int alignWidth = width_Rect(run->visBounds) * 4 / 5;
        xAdjust -= left_Rect(visBounds);
        if (visWidth > alignWidth) {
            /* ...unless it's a wide icon, in which case move it to the left. */
            xAdjust -= visWidth - alignWidth;
        }
        else if (visWidth < alignWidth) {
            /* ...or a narrow icon, which needs to be centered but leave a gap. */
            xAdjust += (alignWidth - visWidth) / 2;
        }
    }
    else {
        /* Centered. */
        xAdjust += (width_Rect(run->visBounds) - visWidth) / 2;
    }
    run->visBounds.pos.x  += xAdjust;
    run->visBounds.size.x -= xAdjust;
}

iDeclareType(RunTypesetter)

struct Impl_RunTypesetter {
    iArray layout;
    iGmRun run;
    iInt2  pos;
    float  lineHeightReduction;
    int    indent;
    int    layoutWidth; /* for detecting wide (scrollable) runs */
    int    rightMargin;
    iBool  isWordWrapped;
    iBool  isPreformat;
    int    baseFont;
    int    baseColor;
    float  lineSpacing;
};

static void init_RunTypesetter_(iRunTypesetter *d) {
    iZap(*d);
    init_Array(&d->layout, sizeof(iGmRun));
}

static void deinit_RunTypesetter_(iRunTypesetter *d) {
    deinit_Array(&d->layout);
}

static void clear_RunTypesetter_(iRunTypesetter *d) {
    clear_Array(&d->layout);
}

static size_t commit_RunTypesetter_(iRunTypesetter *d, iGmDocument *doc) {
    const size_t n = size_Array(&d->layout);
    /* Update the actual content width of the document. This may exceed the page width
       if there are unwrappable lines. */
    for (size_t i = 0; i < size_Array(&d->layout); i++) {
        doc->contentWidth = iMax(value_Array(&d->layout, i, iGmRun).visBounds.size.x,
                                 doc->contentWidth);
    }
    pushBackN_Array(&doc->layout, constData_Array(&d->layout), size_Array(&d->layout));
    clear_RunTypesetter_(d);
    return n;
}

static void applyAttributes_RunTypesetter_(iRunTypesetter *d, iTextAttrib attrib) {
    /* WARNING: This is duplicated in run_Font_(). Make sure they behave identically. */
    if (attrib.monospace) {
        d->run.font = fontWithFamily_Text(d->baseFont, monospace_FontId);
        d->run.color = tmPreformatted_ColorId;
    }
    else if (attrib.italic) {
        d->run.font = fontWithStyle_Text(d->baseFont, italic_FontStyle);
    }
    else if (attrib.regular) {
        d->run.font = fontWithStyle_Text(d->baseFont, regular_FontStyle);
    }
    else if (attrib.bold) {
        d->run.font = fontWithStyle_Text(d->baseFont, bold_FontStyle);
        d->run.color = tmFirstParagraph_ColorId;
    }
    else if (attrib.light) {
        d->run.font = fontWithStyle_Text(d->baseFont, light_FontStyle);
    }
    else {
        d->run.font  = d->baseFont;
        d->run.color = d->baseColor;
    }
}

static iBool typesetOneLine_RunTypesetter_(iWrapText *wrap, iRangecc wrapRange, iTextAttrib attrib,
                                           int origin, int advance) {
    iAssert(wrapRange.start <= wrapRange.end);
    trimEnd_Rangecc(&wrapRange);
    iRunTypesetter *d = wrap->context;
    d->run.text = wrapRange;
    applyAttributes_RunTypesetter_(d, attrib);
#if 0
    const int msr = measureRange_Text(d->run.font, wrapRange).advance.x;
    if (iAbs(msr - advance) > 3) {
        printf("\n[RunTypesetter] wrong wrapRange advance! actual:%d wrapped:%d\n\n", msr, advance);
    }
#endif
    if (~d->run.flags & startOfLine_GmRunFlag && d->lineHeightReduction > 0.0f) {
        d->pos.y -= d->lineHeightReduction * lineHeight_Text(d->baseFont);
    }
    d->run.bounds.pos = addX_I2(d->pos, origin + d->indent);
    const iInt2 dims = init_I2(advance, lineHeight_Text(d->baseFont));
    iChangeFlags(d->run.flags, wide_GmRunFlag, (d->isPreformat && dims.x > d->layoutWidth));
    d->run.bounds.size.x    = iMax(wrap->maxWidth, dims.x) - origin; /* Extends to the right edge for selection. */
    d->run.bounds.size.y    = dims.y;
    d->run.visBounds        = d->run.bounds;
    d->run.visBounds.size.x = dims.x;
    d->run.isRTL            = attrib.isBaseRTL;
    /* Hit testing covers the extra line spacing, split evenly above and below the text. */
    const int pitch = dims.y * d->lineSpacing;
    if (pitch > dims.y) {
        d->run.bounds.pos.y -= (pitch - dims.y) / 2;
        d->run.bounds.size.y = pitch;
    }
//    printf("origin:%d isRTL:%d\n{%s}\n", origin, attrib.isBaseRTL, cstr_Rangecc(wrapRange));
    pushBack_Array(&d->layout, &d->run);
    d->run.flags &= ~startOfLine_GmRunFlag;
    d->pos.y += pitch;
    return iTrue; /* continue to next wrapped line */
}

static iBool isHRule_(iRangecc line) {
    /* This is used in Markdown sources. */
    if (!startsWith_Rangecc(line, "---")) {
        return iFalse;
    }
    size_t n = 0;
    for (const char *ch = line.start; ch < line.end; ch++) {
        if (*ch != '-') {
            return iFalse;
        }
        n++;
    }
    return n >= 3;
}

/*
static iBool isMonospace_GmDocument_(const iGmDocument *d) {
    const iRangecc scheme = urlScheme_String(&d->url);
    if (equalCase_Rangecc(scheme, "gemini")) {
        return d->format == plainText_SourceFormat;
    }
    return isForcedMonospace_GmDocument_(d);
}
*/

static iBool isHardWrapped_GmDocument_(const iGmDocument *d) {
    if (d->flags.isNex) return iTrue; /* never softwrapped */
    return d->format == plainText_SourceFormat || isGopherMenu_GmDocument(d);
}

static void determinePlainTextWrapWidth_GmDocument(iGmDocument *d) {
    const iDocPrefs *prefs = &d->docPrefs;
    /* Only do this once (and whenever font size changes). */
    if (d->wrapWidth) return;
    /* For plain text with word wrap and expand-to-long-lines, measure all lines first to
       find the widest one and potentially increase the layout width up to the full canvas
       width, so that long lines don't have to be wrapped unnecessarily. */
    if (!isPaged_GmDocument(d) && isHardWrapped_GmDocument_(d) && prefs->plainTextWrap.value &&
        prefs->expandToLongLines.value) {
        iRegExp *linkPattern = d->format == gemini_SourceFormat ? newGemtextLink_RegExp() : NULL;
        int      maxLine = 0;
        iRangecc seg     = iNullRange;
        while (nextSplit_Rangecc(range_String(&d->source), "\n", &seg)) {
            iRangecc line = seg;
            if (*line.end == '\r') {
                line.end--; /* trim CR always */
            }
            iRangecc visLine = line;
            if (d->format == gemini_SourceFormat) {
                iRegExpMatch m;
                init_RegExpMatch(&m);
                if (matchRange_RegExp(linkPattern, line, &m)) {
                    visLine = capturedRange_RegExpMatch(&m, 2);
                }
            }
            maxLine = iMaxi(maxLine, measureRange_Text(preformatted_FontId, visLine).advance.x);
        }
        d->wrapWidth = iMin(maxLine, d->metrics.maxContentWidth);
        iRelease(linkPattern);
    }
}

/*----------------------------------------------------------------------------------------------*/
/* Pagination: the continuous layout is cut into columns that begin at multiples of the page
   height. A page has one or more columns, and a header and a footer. */

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

static int cutoutShift_GmDocument_(const iGmDocument *d) {
    /* The header is next to the cutout, so the content area moves up. */
    return isHeaderBelowCutout_GmDocument(d) ? lineHeight_Text(paragraph_FontId) * 4 / 5 : 0;
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

static void paginate_GmDocument_(iGmDocument *d) {
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

/*----------------------------------------------------------------------------------------------*/
/* Typesetter lays out the document source as runs, one source line at a time. */

static int outsideMargin_GmDocument_(const iGmDocument *d) {
    /* Distance to the edge of the canvas. */
    return iMax(0, (d->metrics.canvasWidth - d->size.x) / 2);
}

static iBool isLineIndentable_(iRangecc line) {
    trim_Rangecc(&line);
    if (size_Range(&line) < 3 || !isAlpha_Char(*line.start)) {
        /* Maybe a numbered list, or a symbol of some sort. */
        return iFalse;
    }
    /* Lists typically have punctuation at the beginning. */
    for (const char *i = line.start; i != line.start + 3; i++) {
        if (isPunct_Char(*i) && *i != '`') {
            if ((*i == '\'' || *i == '"') && i != line.start) continue;
            return iFalse;
        }
    }
    /* Looks like regular text. */
    return iTrue;
}


iDeclareType(Typesetter)

struct Impl_Typesetter {
    iGmDocument          *doc;
    const iDocPrefs      *prefs;
    const iDocumentStyle *style;
    iBool                 isMono;
    iBool                 isGopher;
    iBool                 isPaged;
    iBool                 isNormalized;
    iBool                 isJustified;
    int                   outsideMargin;
    const iArray         *oldPreMeta; /* remember fold states */
    iRangecc              content;
    iRangecc              contentLine;
    /* Current line. */
    iRangecc              line; /* trimmed; modifying `contentLine` would confuse `nextSplit_Rangecc` */
    enum iGmLineType      type;
    float                 indent;
    iGmRun                run;
    /* State carried over between lines. */
    iInt2                 pos;
    iBool                 isFirstText;
    iBool                 addQuoteIcon;
    iBool                 isPreformat;
    int                   preFont;
    uint16_t              preId;
    iBool                 enableIndents;
    enum iGmLineType      prevType;
    enum iGmLineType      prevNonBlankType;
    iBool                 followsBlank;
    int                   pendingMargin;     /* added below the next line's top margin */
    int                   numHeading1;
    iBool                 prevWrapParagraph; /* previous line was a wrapped paragraph */
    iString               firstContentLine;  /* may be used as a title if one isn't specified */
};

static iRegExp *ansiPattern_(void) {
    static iRegExp *pattern_;
    if (!pattern_) {
        pattern_ = makeAnsiEscapePattern_Text(iTrue /* with ESC */);
    }
    return pattern_;
}

static void init_Typesetter_(iTypesetter *d, iGmDocument *doc) {
    iZap(*d);
    d->doc           = doc;
    d->prefs         = &doc->docPrefs;
    d->style         = &doc->style;
    d->isMono        = isForcedMonospace_GmDocument_(doc);
    d->isGopher      = isGopher_GmDocument_(doc);
    d->isPaged       = isPaged_GmDocument(doc);
    d->isNormalized  = shouldBeNormalized_GmDocument_(doc);
    d->isJustified   = d->prefs->justifyParagraph.value;
    d->outsideMargin = outsideMargin_GmDocument_(doc);
    init_String(&d->firstContentLine);
}

static void deinit_Typesetter_(iTypesetter *d) {
    deinit_String(&d->firstContentLine);
}

static int verticalPad_Typesetter_(const iTypesetter *d) {
    return d->style->verticalPad * lineHeight_Text(d->style->fonts[text_GmLineType]);
}

static iBool begin_Typesetter_(iTypesetter *d) {
    iGmDocument *doc = d->doc;
    init_DocumentStyle(&doc->style,
                       &(iDocumentStyleParams){
                           .prefs            = d->prefs,
                           .width            = doc->size.x,
                           .outsideMargin    = d->outsideMargin,
                           .isMonospace      = d->isMono,
                           .isGopher         = d->isGopher,
                           .isPaged          = d->isPaged,
                           .isPlainText      = doc->viewFormat == plainText_SourceFormat,
                           .isDarkBackground = isDark_GmDocumentTheme(currentTheme_GmDocument_(doc)),
                           .isUiTheme        = doc->flags.isUiTheme,
                           .isBookCover      = doc->flags.isBookCover });
    doc->flags.isLayoutInvalidated = iFalse;
    doc->layoutFontKey = fontKey_App();
    clear_Array(&doc->layout);
    clear_StringArray(&doc->auxText);
    clear_Array(&doc->pageBands);
    clearLinks_GmDocument_(doc);
    clear_Array(&doc->headings);
    d->oldPreMeta = collect_Array(copy_Array(&doc->preMeta));
    clear_Array(&doc->preMeta);
    clear_String(&doc->title);
    doc->contentWidth = 0;
    if (doc->size.x <= 0 || isEmpty_String(&doc->source)) {
        return iFalse;
    }
    updateOpenURLs_GmDocument_(doc);
    d->content           = range_String(&doc->source);
    d->contentLine       = iNullRange;
    d->pos               = init_I2(0, verticalPad_Typesetter_(d));
    d->isFirstText       = d->style->hasLede;
    d->addQuoteIcon      = d->prefs->quoteIcon.value;
    d->isPreformat       = iFalse;
    d->preFont           = d->style->fonts[preformatted_GmLineType];
    d->preId             = 0;
    d->enableIndents     = iFalse;
    d->prevType          = text_GmLineType;
    d->prevNonBlankType  = undefined_GmLineType;
    d->followsBlank      = iFalse;
    d->pendingMargin     = 0;
    d->numHeading1       = 0;
    d->prevWrapParagraph = iFalse;
    if (doc->format == plainText_SourceFormat) {
        d->isPreformat = iTrue;
        d->isFirstText = iFalse;
    }
    doc->warnings &= ~missingGlyphs_GmDocumentWarning;
    checkMissing_Text(); /* clear the flag */
    setAnsiFlags_Text(doc->ansiEscapes);
    determinePlainTextWrapWidth_GmDocument(doc);
    return iTrue;
}

static void parseLink_Typesetter_(iTypesetter *d) {
    iGmLinkId linkId = 0;
    d->line = addLink_GmDocument_(d->doc, d->line, &linkId);
    d->run.linkId = linkId;
    if (!d->run.linkId) {
        /* Invalid formatting. */
        d->type = text_GmLineType;
    }
}

static void addRule_Typesetter_(iTypesetter *d) {
    iGmRun hrule = d->run;
    const int leftIndent = d->style->ruleIndent * gap_Text;
    const int rightIndent = leftIndent + (d->isJustified ? -1 : 0) * gap_Text;
    hrule.visBounds.pos  = add_I2(d->pos, init_I2(leftIndent, gap_Text * aspect_UI));
    hrule.visBounds.size = init_I2(d->doc->size.x - leftIndent - rightIndent, 0);
    hrule.bounds         = zero_Rect(); /* just visual */
    hrule.text           = iNullRange;
    hrule.flags          = ruler_GmRunFlag | decoration_GmRunFlag;
    pushBack_Array(&d->doc->layout, &hrule);
    d->pos.y += gap_Text * (1 + aspect_UI);
}

static void beginPreformatted_Typesetter_(iTypesetter *d) {
    iGmDocument *doc = d->doc;
    d->isPreformat = iTrue;
    const size_t preIndex = d->preId++;
    d->preFont            = d->style->fonts[preformatted_GmLineType];
    iGmPreMeta meta = { .bounds = d->line };
    meta.pixelRect.size = measurePreformattedBlock_GmDocument_(
        doc, d->line.start, d->preFont, &meta.contents, &meta.bounds.end);
    /*
    int overrun = meta.pixelRect.size.x - doc->size.x;
    if (d->prevNonBlankType == undefined_GmLineType && overrun > 0) {
        meta.initialOffset = iMin(overrun / 2, d->outsideMargin - 5 * gap_UI);
    }
    */
    trimLine_Rangecc(&d->line, d->type, d->isNormalized);
    meta.altText = d->line; /* without the ``` */
    /* Reuse previous state. */
    if (preIndex < size_Array(d->oldPreMeta)) {
        meta.flags = constValue_Array(d->oldPreMeta, preIndex, iGmPreMeta).flags &
                     folded_GmPreMetaFlag;
    }
    else if (d->prefs->collapsePre.value >= byDefault_Collapse && !d->isGopher) {
        meta.flags |= folded_GmPreMetaFlag;
    }
    pushBack_Array(&doc->preMeta, &meta);
    d->prevWrapParagraph = iFalse;
}

static iBool detectLineType_Typesetter_(iTypesetter *d) {
    /* Returns False if the line has been fully handled. */
    iGmDocument          *doc   = d->doc;
    const iDocumentStyle *style = d->style;
    if (doc->flags.isNex) {
        d->type = lineType_GmDocument_(doc, d->line);
        if (d->type == link_GmLineType) {
            parseLink_Typesetter_(d);
        }
        d->run.font = style->fonts[d->type];
        if (d->type == link_GmLineType) {
            d->indent = (float) measure_Text(d->run.font, "=> ").advance.x / (float) gap_Text;
        }
    }
    else if (!d->isPreformat) {
        d->type = lineType_GmDocument_(doc, d->line);
        if (doc->origFormat == markdown_SourceFormat && isHRule_(d->line)) {
            addRule_Typesetter_(d);
            return iFalse;
        }
        if (d->contentLine.start == d->content.start) {
            d->prevType = d->type;
        }
        d->indent = style->indents[d->type];
        if (d->type == preformatted_GmLineType) {
            beginPreformatted_Typesetter_(d);
            return iFalse;
        }
        else if (d->type == link_GmLineType) {
            parseLink_Typesetter_(d);
        }
        trimLine_Rangecc(&d->line, d->type, d->isNormalized);
        d->run.font = style->fonts[d->type];
        /* Remember headings for the document outline. */
        if (d->type == heading1_GmLineType || d->type == heading2_GmLineType ||
            d->type == heading3_GmLineType) {
            pushBack_Array(
                &doc->headings,
                &(iGmHeading){ .text = d->line, .level = d->type - heading1_GmLineType });
        }
    }
    else {
        /* Preformatted line. */
        d->type = preformatted_GmLineType;
        if (d->contentLine.start == d->content.start) {
            d->prevType = d->type;
        }
        if (doc->format == gemini_SourceFormat &&
            startsWithSc_Rangecc(d->line, "```", &iCaseSensitive)) {
            d->isPreformat = iFalse;
            return iFalse;
        }
        d->run.mediaType = max_MediaType; /* preformatted block */
        d->run.mediaId   = d->preId;
        d->run.font      = d->preFont;
        d->indent        = style->indents[d->type];
    }
    return iTrue;
}

static void addBlankLine_Typesetter_(iTypesetter *d) {
    /* Empty lines don't produce text runs. */
    iGmRun *run = &d->run;
    d->prevWrapParagraph = iFalse;
    const int lineHeight = lineHeight_Text(run->font);
    const int pitch      = lineHeight * d->prefs->lineSpacing.value;
    if (d->type == preformatted_GmLineType) {
        /* Empty lines in a preformatted blocks should functionally be part of the block. */
        run->bounds = (iRect){ d->pos, init_I2(1, lineHeight) };
        run->visBounds = run->bounds;
        if (pitch > lineHeight) {
            run->bounds.pos.y -= (pitch - lineHeight) / 2;
            run->bounds.size.y = pitch;
        }
        run->text = d->line;
        pushBack_Array(&d->doc->layout, run);
    }
    else if (d->type == quote_GmLineType && !d->prefs->quoteIcon.value) {
        /* For quote indicators we still need to produce a run. */
        run->visBounds.pos  = addX_I2(d->pos, d->style->indents[d->type] * gap_Text);
        run->visBounds.size = init_I2(lineHeight_Text(run->font), lineHeight_Text(run->font));
        run->bounds         = zero_Rect(); /* just visual */
        run->text           = iNullRange;
        run->flags          = ruler_GmRunFlag | decoration_GmRunFlag;
        pushBack_Array(&d->doc->layout, run);
    }
    d->pos.y += pitch;
    d->prevType = d->type;
    if (d->type != quote_GmLineType) {
        d->addQuoteIcon = d->prefs->quoteIcon.value;
    }
    d->followsBlank = iTrue;
}

static void updateIndent_Typesetter_(iTypesetter *d) {
    /* Begin indenting after the first preformatted block. */
    if (d->type != preformatted_GmLineType || d->prevType != preformatted_GmLineType) {
        d->enableIndents = iTrue;
    }
    /* Gopher: Always indent preformatted blocks. */
    if (d->isGopher && d->type == preformatted_GmLineType) {
        d->enableIndents = iTrue;
    }
    if (!d->enableIndents) {
        d->indent = 0;
    }
}

static void addTopMargin_Typesetter_(iTypesetter *d) {
    /* Check the margin vs. previous run. */
    const iDocumentStyle *style = d->style;
    if (d->isPreformat && d->prevType == preformatted_GmLineType) {
        return;
    }
    const float top    = style->topMargins[d->type];
    const float bottom = d->prevNonBlankType != undefined_GmLineType &&
                                 style->flags[d->prevNonBlankType] & keepBottomMargin_DocumentStyleFlag
                             ? style->bottomMargins[d->prevNonBlankType]
                             : style->bottomMargins[d->prevType];
    int required = iMax(top, bottom) * lineHeight_Text(style->fonts[text_GmLineType]);
    int moveUp   = 0;
    const iGmDocument *doc = d->doc;
    if (d->isPaged &&
        (doc->chapter > 0 || doc->flags.isBookCover || doc->flags.isBookContents) &&
        d->numHeading1 == 1 &&
        d->type == heading2_GmLineType && d->prevNonBlankType == heading1_GmLineType) {
        /* A subtitle sits closer to the title, with the difference added below it. */
        moveUp = lineHeight_Text(style->fonts[text_GmLineType]);
    }
    if (d->type == link_GmLineType && d->prevNonBlankType == link_GmLineType && d->followsBlank) {
        required = style->linkGroupMargin * lineHeight_Text(style->fonts[text_GmLineType]);
    }
    if (d->type == quote_GmLineType && d->prevType == quote_GmLineType) {
        /* No margin between consecutive quote lines. */
        required = 0;
    }
    if (isEmpty_Array(&d->doc->layout) &&
        !(d->isPaged && d->type == heading1_GmLineType && d->doc->metrics.firstPageInset == 0)) {
        required = 0; /* top of document; without a banner, pages begin a chapter lower down */
    }
    required *= d->prefs->lineSpacing.value;
    moveUp   *= d->prefs->lineSpacing.value;
    int delta = d->pos.y - lastVisibleRunBottom_GmDocument_(d->doc);
    if (delta < required - moveUp) {
        d->pos.y += (required - moveUp - delta);
    }
    d->pos.y += d->pendingMargin;
    d->pendingMargin = iMax(0, required - delta) - iMax(0, required - moveUp - delta);
}

static iBool addFoldedPreformatted_Typesetter_(iTypesetter *d) {
    /* Folded blocks are represented by a single run with the alt text. */
    iGmDocument *doc = d->doc;
    if (!d->isPreformat || doc->format == plainText_SourceFormat) {
        return iFalse;
    }
    const iGmPreMeta *meta = constAt_Array(&doc->preMeta, d->preId - 1);
    if (~meta->flags & folded_GmPreMetaFlag) {
        return iFalse;
    }
    const iBool isBlank = isEmpty_Range(&meta->altText);
    iGmRun      altText = { .font  = d->style->fonts[text_GmLineType],
                            .color = tmQuote_ColorId,
                            .flags = (isBlank ? decoration_GmRunFlag : 0) | altText_GmRunFlag
    };
    const iInt2 margin = preRunMargin_GmDocument(doc, 0);
    altText.text       = isBlank ? range_Lang(range_CStr("doc.pre.nocaption"))
                                 : meta->altText;
    iInt2 size = measureWrapRange_Text(altText.font, doc->size.x - 2 * margin.x,
                                       altText.text).bounds.size;
    altText.bounds = altText.visBounds = init_Rect(d->pos.x, d->pos.y, doc->size.x,
                                                   size.y + 2 * margin.y);
    altText.mediaType = max_MediaType; /* preformatted */
    altText.mediaId = d->preId;
    pushBack_Array(&doc->layout, &altText);
    d->pos.y += height_Rect(altText.bounds);
    d->contentLine = meta->bounds; /* Skip the whole thing. */
    d->isPreformat = iFalse;
    d->prevType = preformatted_GmLineType;
    return iTrue;
}

static void updateTitle_Typesetter_(iTypesetter *d) {
    /* Save the document title (first high-level heading). */
    iGmDocument *doc = d->doc;
    if (d->type == heading1_GmLineType && isEmpty_String(&doc->title)) {
        setRange_String(&doc->title, d->line);
        /* Get rid of ANSI escapes. */
        replaceRegExp_String(&doc->title, ansiPattern_(), "", NULL, NULL);
    }
    else if (d->type != preformatted_GmLineType && d->type != heading1_GmLineType &&
             isEmpty_String(&d->firstContentLine) && size_Range(&d->line) >= 3) {
        setRange_String(&d->firstContentLine, d->line);
        replaceRegExp_String(&d->firstContentLine, ansiPattern_(), "", NULL, NULL);
    }
}

static void addBullet_Typesetter_(iTypesetter *d) {
    /* TODO: Literata bullet is broken? */
    const iDocumentStyle *style = d->style;
    iGmRun bulRun = d->run;
    bulRun.color = tmQuote_ColorId;
    bulRun.visBounds.pos = addX_I2(
        d->pos, (style->indents[text_GmLineType] - (isTerminal_Platform() ? 0.0f : 0.55f)) * gap_Text);
    bulRun.visBounds.size =
        init_I2((style->indents[bullet_GmLineType] - style->indents[text_GmLineType]) * gap_Text,
                lineHeight_Text(bulRun.font));
    bulRun.bounds = zero_Rect(); /* just visual */
    bulRun.text   = range_CStr("•");
    bulRun.flags |= decoration_GmRunFlag;
    alignDecoration_GmRun_(&bulRun, iTrue);
    pushBack_Array(&d->doc->layout, &bulRun);
}

static void addQuoteIcon_Typesetter_(iTypesetter *d) {
    static const char *quote = "“";
    iGmRun quoteRun = d->run;
    quoteRun.font   = heading1_FontId;
    quoteRun.text   = range_CStr(quote);
    quoteRun.color  = tmQuoteIcon_ColorId;
    iRect vis       = visualBounds_Text(quoteRun.font, quoteRun.text);
    quoteRun.visBounds.size = measure_Text(quoteRun.font, quote).bounds.size;
    quoteRun.visBounds.pos =
        add_I2(d->pos,
               init_I2((d->style->indents[quote_GmLineType] - 5 * aspect_UI) * gap_Text,
                       !isTerminal_Platform()
                           ? (lineHeight_Text(quote_FontId) / 2 - bottom_Rect(vis))
                           : 0));
    quoteRun.bounds = zero_Rect(); /* just visual */
    quoteRun.flags |= decoration_GmRunFlag;
    if (isTerminal_Platform()) {
        quoteRun.font = d->style->fonts[text_GmLineType];
    }
    pushBack_Array(&d->doc->layout, &quoteRun);
}

static void addLinkIcon_Typesetter_(iTypesetter *d) {
    static const char *arrow           = rightArrowhead_Icon;
    static const char *envelope        = envelope_Icon;
    static const char *folder          = file_Icon;
    static const char *globe           = globe_Icon;
    static const char *magnifyingGlass = "\U0001f50d";
    static const char *pointingFinger  = "\U0001f449";
    static const char *uploadArrow     = upload_Icon;
    static const char *image           = photo_Icon;
    iGmDocument *doc = d->doc;
    iGmRun icon = d->run;
    icon.visBounds.pos  = d->pos;
    icon.visBounds.size = init_I2(d->indent * gap_Text, lineHeight_Text(d->run.font));
    icon.bounds         = zero_Rect(); /* just visual */
    iGmLink *link = at_PtrArray(&doc->links, d->run.linkId - 1);
    const enum iGmLinkScheme scheme = scheme_GmLinkFlag(link->flags);
    icon.text           = range_CStr(link->flags & query_GmLinkFlag    ? (doc->flags.isSpartan ? upload_Icon : magnifyingGlass)
                                     : scheme == titan_GmLinkScheme    ? uploadArrow
                                     : scheme == finger_GmLinkScheme   ? pointingFinger
                                     : scheme == nex_GmLinkScheme      ? nex_Icon
                                     : scheme == guppy_GmLinkScheme    ? guppy_Icon
                                     : (scheme == spartan_GmLinkScheme &&
                                        !doc->flags.isSpartan)         ? spartan_Icon
                                     : (scheme == mailto_GmLinkScheme ||
                                        scheme == misfin_GmLinkScheme) ? envelope
                                     : scheme == data_GmLinkScheme     ? paperclip_Icon
                                     : link->flags & remote_GmLinkFlag ? globe
                                     : link->flags & imageFileExtension_GmLinkFlag ? image
                                     : link->flags & fontpackFileExtension_GmLinkFlag ? fontpack_Icon
                                     : scheme == file_GmLinkScheme     ? folder
                                                                       : arrow);
    /* TODO: List bullets needs the same centering logic. */
    /* Special exception for the tiny bullet operator. */
    icon.font = equal_Rangecc(link->labelIcon, "∙") ? preformatted_FontId : icon.font;
    /* Check actual height to align with the paragraph text. The icon glyph
       may come from a different font. */ {
        const int glyphHeight = measureRange_Text(icon.font, icon.text).bounds.size.y;
        if (glyphHeight > icon.visBounds.size.y) {
            const int delta = glyphHeight - icon.visBounds.size.y;
            icon.visBounds.size.y += delta;
            icon.visBounds.pos.y -= delta / 2;
        }
    }
    /* Custom link icon is shown on local Gemini links only. */
    if (!isEmpty_Range(&link->labelIcon)) {
        icon.text = link->labelIcon;
    }
    icon.flags |= decoration_GmRunFlag | startOfLine_GmRunFlag;
    if (!doc->flags.isNex) {
        alignDecoration_GmRun_(&icon, iFalse);
    }
    else {
        /* Nex directory link "icons" are actually the => arrows that appear in
           the source text. */
        icon.visBounds.size.x = d->indent * gap_Text;
        icon.bounds = icon.visBounds;
    }
    icon.color = linkColor_GmDocument(doc, d->run.linkId, icon_GmLinkPart);
    pushBack_Array(&doc->layout, &icon);
}

static void setRunStyle_Typesetter_(iTypesetter *d) {
    const iDocumentStyle *style = d->style;
    iGmRun *run = &d->run;
    run->lineType = d->type;
    run->color    = style->colors[d->type];
    if (d->doc->format == plainText_SourceFormat) {
        run->color = style->colors[text_GmLineType];
    }
    /* Special formatting for the first paragraph (e.g., subtitle, introduction, or lede). */
    if (d->type == text_GmLineType && d->isFirstText) {
        run->font      = style->ledeFont;
        run->color     = style->ledeColor;
        run->isLede    = iTrue;
        d->isFirstText = iFalse;
    }
    else if (d->type != heading1_GmLineType) {
        d->isFirstText = iFalse;
    }
}

static iBool isTitleLayout_GmDocument_(const iGmDocument *d) {
    iConstForEach(Array, i, &d->layout) {
        const iGmRun *run = i.value;
        if (run->lineType != heading1_GmLineType && run->lineType != heading2_GmLineType) {
            return iFalse;
        }
    }
    return iTrue;
}

static size_t typesetParagraph_Typesetter_(iTypesetter *d) {
    /* Returns the number of runs added. */
    iGmDocument          *doc   = d->doc;
    const iDocPrefs      *prefs = d->prefs;
    const iDocumentStyle *style = d->style;
    const iGmRun         *run   = &d->run;
    const enum iGmLineType type = d->type;
    const iRangecc        line  = d->line;
    int preWidth = 0;
    if (d->isPaged && d->isPreformat && doc->format != plainText_SourceFormat) {
        /* A wide block may extend past the margins, centered on the column. */
        const int blockWidth = constValue_Array(&doc->preMeta, d->preId - 1, iGmPreMeta).pixelRect.size.x;
        if (blockWidth > doc->size.x) {
            preWidth = iMin(blockWidth, iMax(doc->size.x, doc->metrics.maxContentWidth));
        }
    }
    const int layoutWidth = preWidth ? preWidth : doc->size.x;
    const int preShift    = (layoutWidth - doc->size.x) / 2;
    if (d->isPreformat && doc->format != plainText_SourceFormat) {
        /* Remember the top left coordinates of the block (first line of block). */
        iGmPreMeta *meta = at_Array(&doc->preMeta, d->preId - 1);
        if (~meta->flags & topLeft_GmPreMetaFlag) {
            meta->pixelRect.pos = addX_I2(d->pos, -preShift);
            meta->flags |= topLeft_GmPreMetaFlag;
        }
    }
    iAssert(!isEmpty_Range(&line)); /* must have something at this point */
    size_t numRunsAdded = 0;
    const iBool isTextType =
        (type == text_GmLineType || type == bullet_GmLineType || type == quote_GmLineType ||
         type == link_GmLineType);
    const iBool isParagraphJustified = d->isJustified && isTextType;
    iRunTypesetter rts;
    init_RunTypesetter_(&rts);
    rts.run           = *run;
    rts.pos           = addX_I2(d->pos, -preShift);
    rts.lineSpacing   = prefs->lineSpacing.value;
    rts.isWordWrapped = (d->isPaged                       ? iTrue /* no wide runs */
                         : doc->flags.isNex               ? iFalse
                         : isHardWrapped_GmDocument_(doc) ? prefs->plainTextWrap.value
                                                          : !d->isPreformat);
    rts.isPreformat   = d->isPreformat;
    rts.layoutWidth   = layoutWidth;
    rts.indent        = d->indent * gap_Text;
    rts.rightMargin   = style->rightMargins[type] * gap_Text;
    if (!d->isMono) {
        /* Visited links are never bold. */
        if (run->linkId && !prefs->boldLinkVisited.value &&
            linkFlags_GmDocument(doc, run->linkId) & visited_GmLinkFlag) {
            rts.run.font = style->fonts[text_GmLineType];
        }
    }
    if (!prefs->quoteIcon.value && type == quote_GmLineType) {
        rts.run.flags |= ruler_GmRunFlag;
    }
    /* Determine the maximum width of wrapped lines. */
    const int wrapMaxWidth =
        rts.isWordWrapped
            ? doc->wrapWidth > 0
                  ? doc->wrapWidth
                  : (layoutWidth - run->bounds.pos.x - rts.indent - rts.rightMargin)
            : 0 /* unlimited */;
    const iBool indentParagraphFirstLine = isLineIndentable_(line);
    int firstLineIndent = 0;
    if (indentParagraphFirstLine && type == text_GmLineType && d->prevWrapParagraph) {
        /* Previous line was text, too, and it wrapped so we may need some first-line
           indentation to make the new line distinct from the wrapped preceding lines. */
        const char *endp;
        tryAdvance_Text(rts.run.font, line, wrapMaxWidth, &endp);
        if (endp < line.end) {
            /* This will wrap, too; make it distinct. */
            firstLineIndent = lineHeight_Text(rts.run.font); /* about 1 em */
        }
    }
    for (;;) { /* need to retry if the font needs changing */
        rts.run.flags |= startOfLine_GmRunFlag;
        if (!isParagraphJustified) {
            rts.run.flags |= notJustified_GmRunFlag;
        }
        rts.baseFont  = rts.run.font;
        rts.baseColor = rts.run.color;
        iWrapText wrapText = { .text            = line,
                               .maxWidth        = wrapMaxWidth,
                               .firstLineIndent = firstLineIndent,
                               .mode            = word_WrapTextMode,
                               .wrapFunc        = typesetOneLine_RunTypesetter_,
                               .context         = &rts };
        measure_WrapText(&wrapText, rts.run.font);
        d->prevWrapParagraph = (size_Array(&rts.layout) > 1 && type == text_GmLineType);
        if (!rts.run.isLede || size_Array(&rts.layout) <= style->maxLedeLines) {
            if (wrapText.baseDir < 0) {
                /* Right-aligned paragraphs need margins and decorations to be flipped. */
                iForEach(Array, pr, &rts.layout) {
                    iGmRun *prun = pr.value;
                    const int offset = rts.rightMargin - rts.indent;
                    prun->bounds.pos.x    += offset;
                    prun->visBounds.pos.x += offset;
                }
                if (type == bullet_GmLineType || type == link_GmLineType ||
                    (type == quote_GmLineType && prefs->quoteIcon.value)) {
                    iGmRun *decor = back_Array(&doc->layout);
                    iAssert(decor->flags & decoration_GmRunFlag);
                    decor->visBounds.pos.x = doc->size.x - width_Rect(decor->visBounds) -
                                             decor->visBounds.pos.x +
                                             gap_Text * (type == bullet_GmLineType  ? 1.5f
                                                         : type == quote_GmLineType ? 0.0f
                                                                                    : 1.0f);
                }
            }
            /* Level 2 headings that begin the page are titles, too. */
            if (style->flags[type] & centered_DocumentStyleFlag ||
                (d->isPaged && type == heading2_GmLineType && isTitleLayout_GmDocument_(doc))) {
                iForEach(Array, hr, &rts.layout) {
                    iGmRun *hrun = hr.value;
                    hrun->visBounds.pos.x = (doc->size.x - width_Rect(hrun->visBounds)) / 2;
                }
            }
            numRunsAdded = commit_RunTypesetter_(&rts, doc);
            break;
        }
        /* Try again... */
        clear_RunTypesetter_(&rts);
        rts.pos         = d->pos;
        rts.run.font    = rts.baseFont  = style->fonts[text_GmLineType];
        rts.run.color   = rts.baseColor = style->colors[text_GmLineType];
        rts.run.isLede  = iFalse;
    }
    d->pos = addX_I2(rts.pos, preShift);
    deinit_RunTypesetter_(&rts);
    return numRunsAdded;
}

static void addNexLabel_Typesetter_(iTypesetter *d) {
    /* Nex links may have an extra label following them. */
    iGmDocument   *doc  = d->doc;
    const iGmLink *link = constAt_PtrArray(&doc->links, d->run.linkId - 1);
    if (!isEmpty_Range(&link->labelRange)) {
        const iGmRun *lastRun = constBack_Array(&doc->layout);
        iGmRun label      = *lastRun;
        label.bounds.pos  = topRight_Rect(lastRun->visBounds);
        label.bounds.size = measureRange_Text(d->run.font, link->labelRange).bounds.size;
        label.visBounds   = label.bounds;
        label.text        = link->labelRange;
        label.lineType    = text_GmLineType;
        label.linkId      = 0;
        pushBack_Array(&doc->layout, &label);
    }
}

static void markEndOfLine_Typesetter_(iTypesetter *d, size_t numRunsAdded) {
    iGmRun *lastRun = back_Array(&d->doc->layout);
    if (numRunsAdded == 2) {
        /* The last line isn't justified in any case, and justifying just the first line may
           look inappropriate if there isn't other justified paragraphs next to it. */
        lastRun[ 0].flags |= notJustified_GmRunFlag;
        lastRun[-1].flags |= notJustified_GmRunFlag;
    }
    lastRun->flags |= endOfLine_GmRunFlag;
    if (lastRun->linkId && lastRun->flags & startOfLine_GmRunFlag) {
        /* Single-run link: the icon should also be marked endOfLine. */
        lastRun[-1].flags |= endOfLine_GmRunFlag;
    }
}

static void centerLinkLines_Typesetter_(iTypesetter *d, iGmLinkId linkId) {
    iArray *layout = &d->doc->layout;
    size_t  pos    = size_Array(layout);
    while (pos > 0) {
        const iGmRun *run = constAt_Array(layout, pos - 1);
        if (run->linkId != linkId || isMedia_GmRun(run)) {
            break;
        }
        pos--;
    }
    /* Decorations belong to the line that follows them. The link icon is centered along
       with the text, measured from the visible glyph. */
    while (pos < size_Array(layout)) {
        size_t  end  = pos;
        int     top  = INT_MIN;
        iRangei span = { INT_MAX, INT_MIN };
        for (; end < size_Array(layout); end++) {
            const iGmRun *run = constAt_Array(layout, end);
            if (top != INT_MIN && top_Rect(run->visBounds) != top) {
                break;
            }
            if (run->flags & decoration_GmRunFlag) {
                span.start = iMin(span.start,
                                  left_Rect(run->visBounds) +
                                      left_Rect(visualBounds_Text(run->font, run->text)));
            }
            else {
                top        = top_Rect(run->visBounds);
                span.start = iMin(span.start, left_Rect(run->visBounds));
                span.end   = iMax(span.end, right_Rect(run->visBounds));
            }
        }
        if (top != INT_MIN) {
            const int shift = (d->doc->size.x - size_Range(&span)) / 2 - span.start;
            for (size_t i = pos; i < end; i++) {
                ((iGmRun *) at_Array(layout, i))->visBounds.pos.x += shift;
            }
        }
        pos = end;
    }
}

static void addImage_Typesetter_(iTypesetter *d, iMediaId media, const iGmMediaInfo *info,
                                 int margin) {
    iGmDocument *doc = d->doc;
    iGmRun      *run = &d->run;
    const int    outsideMargin = d->outsideMargin;
    const iInt2 imgSize = imageSize_Media(doc->media, media);
    const iBool isCover = run->linkId == doc->coverImage;
    run->bounds.pos = d->pos;
    run->bounds.size.x = doc->size.x;
    const float aspect = (float) imgSize.y / (float) imgSize.x;
    run->bounds.size.y = doc->size.x * aspect;
    /* Extend the image to full width, including outside margin, if the viewport
       is narrow enough. */
    if (d->style->isFullWidthImages) {
        run->bounds.size.x += outsideMargin * 2;
        run->bounds.size.y += outsideMargin * 2 * aspect;
        run->bounds.pos.x  -= outsideMargin;
    }
    run->visBounds = run->bounds;
    /// XXX: Don't use window pixel ratio, use the UI scaling factor on Window.s
    const iInt2 maxSize = mulf_I2(
        imgSize,
        gap_UI / 2 * activeDocPrefs_App()->zoomPercent.value / 100.0f);
    if (width_Rect(run->visBounds) > maxSize.x) {
        /* Don't scale the image up too much. */
        run->visBounds.size.y =
            run->visBounds.size.y * maxSize.x / width_Rect(run->visBounds);
        run->visBounds.size.x = maxSize.x;
        run->visBounds.pos.x  = run->bounds.size.x / 2 - width_Rect(run->visBounds) / 2;
        run->bounds.size.y    = run->visBounds.size.y;
    }
    if (d->isPaged) {
        /* Must fit on a page, with the link above it and the caption. */
        int linkHeight = 0;
        for (size_t i = size_Array(&doc->layout); i-- > 0;) {
            const iGmRun *prev = constAt_Array(&doc->layout, i);
            if (prev->linkId != run->linkId || isMedia_GmRun(prev)) {
                break;
            }
            linkHeight = iMax(linkHeight, d->pos.y - top_Rect(prev->visBounds));
        }
        const int maxHeight = doc->metrics.pageHeight - pageHeaderHeight_GmDocument(doc) -
                              pageFooterHeight_GmDocument(doc) -
                              (isCover ? 0 : 2 * lineHeight_Text(paragraph_FontId)) - linkHeight;
        if (height_Rect(run->visBounds) > maxHeight && maxHeight > 0) {
            run->visBounds.size.x =
                width_Rect(run->visBounds) * maxHeight / height_Rect(run->visBounds);
            run->visBounds.size.y = maxHeight;
            run->visBounds.pos.x  = run->bounds.pos.x + width_Rect(run->bounds) / 2 -
                                    width_Rect(run->visBounds) / 2;
            run->bounds.size.y    = maxHeight;
        }
        centerLinkLines_Typesetter_(d, run->linkId);
    }
    if (isCover) {
        run->linkId = 0; /* not clickable */
    }
    pushBack_Array(&doc->layout, run);
    d->pos.y += run->bounds.size.y + margin / 2;
    if (isCover) {
        run->bounds.size.y = 0; /* already advanced */
        d->pos.y += margin / 2;
    }
    else if (!isEqual_I2(imgSize, zero_I2())) {
        /* Image metadata caption */
        run->font = FONT_ID(documentBody_FontId, semiBold_FontStyle, contentSmall_FontSize);
        run->color = tmQuoteIcon_ColorId;
        run->flags = decoration_GmRunFlag | caption_GmRunFlag;
        run->mediaId = 0;
        run->mediaType = 0;
        run->visBounds.pos.y = d->pos.y;
        run->visBounds.size.y = lineHeight_Text(run->font);
        run->bounds = zero_Rect();
        iString caption;
        init_String(&caption);
        const iBool inMegabytes = info->numBytes >= 1000000;
        format_String(&caption,
                      "%s — %d x %d — %.1f%s",
                      info->type,
                      imgSize.x,
                      imgSize.y,
                      info->numBytes / (inMegabytes ? 1.0e6f : 1.0e3f),
                      inMegabytes ? cstr_Lang("mb") : cstr_Lang("kb"));
        pushBack_StringArray(&doc->auxText, &caption);
        run->text = range_String(&caption);
        /* Center it. */
        run->visBounds.size.x = measureRange_Text(run->font, range_String(&caption)).bounds.size.x;
        run->visBounds.pos.x = doc->size.x / 2 - run->visBounds.size.x / 2;
        deinit_String(&caption);
        pushBack_Array(&doc->layout, run);
        d->pos.y += run->visBounds.size.y + margin;
    }
}

static void addLinkContent_Typesetter_(iTypesetter *d) {
    /* Image or audio content. */
    iGmDocument   *doc   = d->doc;
    iGmRun        *run   = &d->run;
    const iMediaId media = findMediaForLink_Media(doc->media, run->linkId, none_MediaType);
    iGmMediaInfo info;
    info_Media(doc->media, media, &info);
    run->mediaType = media.type;
    run->mediaId   = media.id;
    run->text      = iNullRange;
    run->font      = uiLabel_FontId;
    run->color     = 0;
    const int margin = lineHeight_Text(d->style->fonts[text_GmLineType]) / 2;
    if (media.type) {
        d->pos.y += margin;
        run->bounds.size.y = 0;
        linkContentWasLaidOut_GmDocument_(doc, &info, run->linkId);
    }
    switch (media.type) {
        case image_MediaType:
            addImage_Typesetter_(d, media, &info, margin);
            break;
        case audio_MediaType: {
            run->bounds.pos    = d->pos;
            run->bounds.size.x = doc->size.x;
            run->bounds.size.y = lineHeight_Text(uiContent_FontId) *
                                     (isMobile_Platform() ? 1.5f : 1.0f) + 3 * gap_UI;
            run->visBounds     = run->bounds;
            pushBack_Array(&doc->layout, run);
            break;
        }
        case download_MediaType: {
            run->bounds.pos    = d->pos;
            run->bounds.size.x = doc->size.x;
            run->bounds.size.y = 2 * lineHeight_Text(uiContent_FontId) + 4 * gap_UI;
            run->visBounds     = run->bounds;
            pushBack_Array(&doc->layout, run);
            break;
        }
        case inputPrompt_MediaType: {
            run->bounds.pos    = d->pos;
            run->bounds.size.x = doc->size.x;
            int h = 0;
            iBool isSensitive;
            inputPromptInfo_Media(doc->media, media, &isSensitive, NULL, NULL, &h);
            if (h <= 0) {
                /* Not yet reported by the widget; reserve a placeholder height. */
                h = lineHeight_Text(uiContent_FontId) + 4 * gap_UI;
            }
            run->bounds.size.y = h;
            run->visBounds     = run->bounds;
            pushBack_Array(&doc->layout, run);
            break;
        }
        default:
            break;
    }
    if (media.type && run->bounds.size.y) {
        d->pos.y += run->bounds.size.y + margin;
    }
}

static void typesetLine_Typesetter_(iTypesetter *d) {
    d->line = d->contentLine;
    if (*d->line.end == '\r') {
        d->line.end--; /* trim CR always */
    }
    d->run    = (iGmRun){ .color = white_ColorId };
    d->indent = 0.0f;
    if (!detectLineType_Typesetter_(d)) {
        return;
    }
    if (isEmpty_Range(&d->line)) {
        addBlankLine_Typesetter_(d);
        return;
    }
    if (d->type == link_GmLineType && d->run.linkId == d->doc->coverImage &&
        findLinkImage_Media(d->doc->media, d->run.linkId).type) {
        /* Just the image, without the link text. */
        addLinkContent_Typesetter_(d);
        d->prevType         = d->type;
        d->prevNonBlankType = d->type;
        d->followsBlank     = iFalse;
        return;
    }
    updateIndent_Typesetter_(d);
    addTopMargin_Typesetter_(d);
    if (addFoldedPreformatted_Typesetter_(d)) {
        return;
    }
    updateTitle_Typesetter_(d);
    if (d->type == bullet_GmLineType) {
        addBullet_Typesetter_(d);
    }
    if (d->type == quote_GmLineType && d->addQuoteIcon) {
        d->addQuoteIcon = iFalse;
        addQuoteIcon_Typesetter_(d);
    }
    else if (d->type != quote_GmLineType) {
        d->addQuoteIcon = d->prefs->quoteIcon.value;
    }
    if (d->type == link_GmLineType) {
        addLinkIcon_Typesetter_(d);
    }
    setRunStyle_Typesetter_(d);
    const size_t numRunsAdded = typesetParagraph_Typesetter_(d);
    if (numRunsAdded == 0) {
        d->pos.y += lineHeight_Text(d->run.font) * d->prefs->lineSpacing.value;
        d->followsBlank = iTrue;
        return;
    }
    if (d->doc->flags.isNex && d->type == link_GmLineType) {
        addNexLabel_Typesetter_(d);
    }
    markEndOfLine_Typesetter_(d, numRunsAdded);
    if (d->type == link_GmLineType) {
        addLinkContent_Typesetter_(d);
    }
    if (d->type == heading1_GmLineType) {
        d->numHeading1++;
    }
    d->prevType         = d->type;
    d->prevNonBlankType = d->type;
    d->followsBlank     = iFalse;
}

static void markWidePreformatted_Typesetter_(iTypesetter *d) {
    /* Mark preformatted blocks wide if at least one run is wide. */
    iGmDocument *doc = d->doc;
    iForEach(Array, i, &doc->layout) {
        iGmRun *run = i.value;
        if (preId_GmRun(run) && run->flags & wide_GmRunFlag) {
            iGmPreMeta *meta = at_Array(&doc->preMeta, preId_GmRun(run) - 1);
            meta->runRange = findPreformattedRange_GmDocument(doc, run);
            for (const iGmRun *j = meta->runRange.start; j != meta->runRange.end; j++) {
                iGmRun *jRun = iConstCast(iGmRun *, j);
                jRun->flags |= wide_GmRunFlag;
                iChangeFlags(jRun->flags, startOfLine_GmRunFlag, j == meta->runRange.start);
                iChangeFlags(jRun->flags, endOfLine_GmRunFlag, j + 1 == meta->runRange.end);
            }
            /* Skip to the end of the block. */
            i.pos = meta->runRange.end - (const iGmRun *) constData_Array(&doc->layout) - 1;
        }
    }
}

static void finish_Typesetter_(iTypesetter *d) {
    iGmDocument *doc = d->doc;
    doc->size.y = d->pos.y + verticalPad_Typesetter_(d);
    if (!d->isJustified) {
        iForEach(Array, r, &doc->layout) {
            ((iGmRun *) r.value)->flags |= notJustified_GmRunFlag;
        }
    }
    doc->contentWidth += d->style->indents[text_GmLineType] * gap_Text; /* indent not included in run widths */
    if (checkMissing_Text()) {
        doc->warnings |= missingGlyphs_GmDocumentWarning;
    }
    /* If a title wasn't found, use the first content line but truncate it if it's long. */
    const iBool isHeadingTitle = !isEmpty_String(&doc->title);
    if (!isHeadingTitle) {
        set_String(&doc->title, &d->firstContentLine);
        if (length_String(&doc->title) > 40) {
            truncate_String(&doc->title, 40);
            /* Find a word boundary. */
            while (size_String(&doc->title) > 10 && isAlpha_Char(last_String(&doc->title))) {
                removeEnd_String(&doc->title, 1);
            }
        }
        trim_String(&doc->title);
    }
    if (!isEmpty_String(&doc->bookTitle)) {
        /* Book pages are titled by the book and the chapter. */
        const iString *chapter = isHeadingTitle ? &doc->title : &doc->chapterTitle;
        if (isEmpty_String(chapter) || equalCase_String(chapter, &doc->bookTitle)) {
            set_String(&doc->title, &doc->bookTitle);
        }
        else {
            set_String(&doc->title, collectNewFormat_String("%s: %s", cstr_String(&doc->bookTitle),
                                                            cstr_String(chapter)));
        }
    }
    if (d->isPaged) {
        paginate_GmDocument_(doc);
    }
    markWidePreformatted_Typesetter_(d);
    setAnsiFlags_Text(allowAll_AnsiFlag);
#if  0
    printf("[GmDocument] layout size: %zu runs (%zu bytes), layout width: %d, content width: %d\n",
           size_Array(&doc->layout),
           size_Array(&doc->layout) * sizeof(iGmRun),
           doc->size.x,
           doc->contentWidth);
#endif
}

static void typeset_GmDocument_(iGmDocument *d) {
    iTypesetter ts;
    init_Typesetter_(&ts, d);
    if (begin_Typesetter_(&ts)) {
        while (nextSplit_Rangecc(ts.content, "\n", &ts.contentLine)) {
            typesetLine_Typesetter_(&ts);
        }
        finish_Typesetter_(&ts);
    }
    deinit_Typesetter_(&ts);
}

static void applyMetrics_GmDocument_(iGmDocument *d, const iGmDocumentMetrics *metrics) {
    if (d->metrics.maxContentWidth != metrics->maxContentWidth) {
        d->wrapWidth = 0;
    }
    d->metrics = *metrics;
    d->size.x  = metrics->width;
}

void doLayout_GmDocument_(iGmDocument *d) {
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
}
