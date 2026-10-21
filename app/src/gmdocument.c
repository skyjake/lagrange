/* Copyright 2020-2026 Jaakko Keränen <jaakko.keranen@iki.fi>

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

/* Document processing overview:

   setSource_GmDocument()
     |  `origSource` is replaced, or appended to by a partial update
     v
   import ...... `origSource` to `source`: CRLF and NULs removed, whitespace
     |           normalized. Only complete lines until the source is final.
     |           Markdown is converted as a whole (gmdoc/markdown.c).
     v
   parse ....... Links, headings, and preformatted blocks (gmdoc/parser.c).
     |           Width-independent; done lazily ahead of the typesetter.
     v
   typeset ..... DocumentStyle set up from Prefs and metrics; source lines
     |           become GmRuns in `layout` (gmdoc/typesetter.c).
     v
   time-slice .. `typesetter` is kept until the layout is finished
     |           (gmdoc/layout.c). Synchronous down to `layoutSyncHeight`,
     |           then `continueLayout_GmDocument` does one slice at a time.
     |           Appended source is laid out as it arrives.
     v
   finish ...... Caught up with the final source: open blocks closed, pages
     |           cut and banded (gmdoc/paginate.c). Paged layouts are done
     |           in full, never sliced.
     v
   render_GmDocument() draws runs in the visible range.

   An anchored layout keeps a visible run in place on screen. Needed for
   stable resizing of document while paragraphs reflow.

     +--------------------+
     |                    |  `above`: a second typesetter, from the top
     |                    |  down to the seam, in slices
     | lead-in            |  typeset only to seed the state at the seam
     +-- seam ------------+  where the visible layout begins
     |                    |
     | kept run           |  stays at the anchor's top
     |                    |  synchronous down to the anchor's bottom
     +--------------------+

   When `above` reaches the seam, its runs are spliced in front and the
   rest is shifted to match. `settleLayout_GmDocument` enforces this.
   If the seam states differ, the layout continues from `above` instead.

   Appending and splicing may move `source` and `layout` in memory; pointers
   into them are updated with a GmRebase.
*/

#include "gmdoc/impl.h"
#include "gmdoc/palette.h"
#include "gmdoc/parser.h"
#include "gmdoc/layout.h"
#include "gmdoc/markdown.h"
#include "gmutil.h"
#include "gempub.h"
#include "color.h"
#include "render/text.h"
#include "ui/metrics.h"
#include "ui/mediaui.h"
#include "ui/window.h"
#include "bookmarks.h"
#include "books.h"
#include "feeds.h"
#include "app.h"
#include "defs.h"

#include <lagrange/lang.h>
#include <lagrange/visited.h>

#include <the_Foundation/intset.h>
#include <the_Foundation/path.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/stringarray.h>
#include <the_Foundation/stringset.h>

#include <ctype.h>

void init_GmLink(iGmLink *d) {
    init_String(&d->url);
    d->urlRange = iNullRange;
    d->labelRange = iNullRange;
    d->labelIcon = iNullRange;
    d->textRange = iNullRange;
    iZap(d->when);
    d->flags = 0;
}

void deinit_GmLink(iGmLink *d) {
    deinit_String(&d->url);
}

iDefineTypeConstruction(GmLink)

/*----------------------------------------------------------------------------------------------*/

iDefineObjectConstruction(GmDocument)

static void import_GmDocument_(iGmDocument *);

iBool isForcedMonospace_GmDocument_(const iGmDocument *d) {
    if (d->flags.isNex) {
        return iTrue;
    }
    if (d->flags.isConvertedMarkdown) {
        return iFalse;
    }
    if (d->flags.isCoverPage) {
        return iFalse;
    }
    if (equalCase_Rangecc(urlScheme_String(&d->url), "finger")) {
        return iTrue;
    }
    return d->docPrefs.monospaceDocument.value;
}

iBool isGopher_GmDocument_(const iGmDocument *d) {
    const iRangecc scheme = urlScheme_String(&d->url);
    return (isGopherScheme_Rangecc(scheme) || equalCase_Rangecc(scheme, "finger"));
}

enum iGmLineType lineType_GmDocument_(const iGmDocument *d, const iRangecc line) {
    if (d->flags.isNex) {
        return startsWith_Rangecc(line, "=> ") ? link_GmLineType : text_GmLineType;
    }
    if (d->format == plainText_SourceFormat) {
        return text_GmLineType;
    }
    if (d->flags.isSpartan && startsWith_Rangecc(line, "=:")) {
        return link_GmLineType;
    }
    return lineType_Rangecc(line);
}

enum iGmLineType lineType_Rangecc(const iRangecc line) {
    if (isEmpty_Range(&line)) {
        return text_GmLineType;
    }
    if (startsWith_Rangecc(line, "=>")) {
        iRangecc trim = line;
        trim_Rangecc(&trim);
        if (size_Range(&trim) > 2) {
            return link_GmLineType;
        }
        return text_GmLineType;
    }
    if (startsWith_Rangecc(line, "###")) {
        return heading3_GmLineType;
    }
    if (startsWith_Rangecc(line, "##")) {
        return heading2_GmLineType;
    }
    if (startsWith_Rangecc(line, "#")) {
        return heading1_GmLineType;
    }
    if (startsWith_Rangecc(line, "```")) {
        return preformatted_GmLineType;
    }
    if (*line.start == '>') {
        return quote_GmLineType;
    }
    if (size_Range(&line) >= 2 && line.start[0] == '*' && isspace(line.start[1])) {
        return bullet_GmLineType;
    }
    return text_GmLineType;
}

void trimLine_Rangecc(iRangecc *line, enum iGmLineType type, iBool normalize) {
    static const unsigned int skip[max_GmLineType] = { 0, 2, 3, 1, 1, 2, 3, 0 };
    line->start += skip[type];
    if (normalize || (type >= heading1_GmLineType && type <= heading3_GmLineType)) {
        trim_Rangecc(line);
    }
}

void clearLinks_GmDocument_(iGmDocument *d) {
    iForEach(PtrArray, i, &d->links) {
        delete_GmLink(i.ptr);
    }
    clear_PtrArray(&d->links);
}

iBool isGopherMenu_GmDocument(const iGmDocument *d) {
    if (d->flags.isGopherMenu) {
        /* We know from the URL that it's a menu. */
        return iTrue;
    }
    return isGopher_GmDocument_(d) && d->format == gemini_SourceFormat && !d->flags.isCoverPage;
}

enum iFontId font_GmDocument(const iGmDocument *d, enum iGmLineType lineType) {
    return d->style.fonts[lineType];
}

iBool shouldBeNormalized_GmDocument_(const iGmDocument *d) {
    const iDocPrefs *prefs = &d->docPrefs;
    if (d->format == plainText_SourceFormat) {
        return iFalse; /* plain text is always shown as-is */
    }
    if (d->flags.isNex) {
        return iFalse;
    }
    if (prefs->monospaceDocument.value) {
        return iFalse;
    }
    if (isGopherScheme_Rangecc(urlScheme_String(&d->url)) && !prefs->geminiStyledGopher.value) {
        return iFalse;
    }
    return iTrue;
}

void updateOpenURLs_GmDocument_(iGmDocument *d) {
    if (d->openURLs) {
        iReleasePtr(&d->openURLs);
    }
    d->openURLs = listOpenURLs_App();
}

void init_GmDocument(iGmDocument *d) {
    d->origFormat = gemini_SourceFormat; /* format of `origSource` */
    d->format     = gemini_SourceFormat; /* format of `source` */
    d->viewFormat = gemini_SourceFormat; /* user's preference */
    init_String(&d->origSource);
    init_String(&d->source);
    init_String(&d->url);
    init_String(&d->themeUrl);
    init_String(&d->fixedTitle);
    init_String(&d->mimeType);
    init_String(&d->localHost);
    init_ScopeKey(&d->scopeKey);
    initCopy_DocPrefs(&d->docPrefs, activeDocPrefs_App());
    iZap(d->metrics);
    iZap(d->requestedMetrics);
    d->numFilledColumns = 0;
    iZap(d->style);
    d->wrapWidth = 0;
    d->maxLineWidth = -1;
    d->maxRunHeight = 0;
    d->size = zero_I2();
    iZap(d->import);
    init_GmParser(&d->parser);
    d->typesetter = NULL;
    d->layoutSyncHeight = 0;
    d->layoutGen = 0;
    d->layoutAnchor.pos = iInvalidPos;
    init_Array(&d->layout, sizeof(iGmRun));
    init_StringArray(&d->auxText);
    init_PtrArray(&d->links);
    init_String(&d->title);
    init_String(&d->bookTitle);
    init_String(&d->chapterTitle);
    init_String(&d->partTitle);
    init_Array(&d->headings, sizeof(iGmHeading));
    init_Array(&d->pageBands, sizeof(iGmPageBands));
    d->chapter     = 0;
    d->numChapters = 0;
    d->coverImage  = 0;
    init_Array(&d->preMeta, sizeof(iGmPreMeta));
    d->themeSeed = 0;
    d->layoutFontKey = 0;
    d->siteIcon = 0;
    d->media = new_Media();
    d->openURLs = NULL;
    d->warnings = 0;
    iZap(d->palette);
    iZap(d->flags);
}

void deinit_GmDocument(iGmDocument *d) {
    deleteTypesetter_GmDocument_(d);
    deinit_GmParser(&d->parser);
    iReleasePtr(&d->openURLs);
    delete_Media(d->media);
    deinit_String(&d->partTitle);
    deinit_String(&d->chapterTitle);
    deinit_String(&d->bookTitle);
    deinit_String(&d->title);
    clearLinks_GmDocument_(d);
    deinit_PtrArray(&d->links);
    deinit_Array(&d->preMeta);
    deinit_Array(&d->pageBands);
    deinit_Array(&d->headings);
    deinit_StringArray(&d->auxText);
    deinit_Array(&d->layout);
    deinit_DocPrefs(&d->docPrefs);
    deinit_ScopeKey(&d->scopeKey);
    deinit_String(&d->localHost);
    deinit_String(&d->mimeType);
    deinit_String(&d->themeUrl);
    deinit_String(&d->fixedTitle);
    deinit_String(&d->url);
    deinit_String(&d->source);
    deinit_String(&d->origSource);
}

void init_GmRebase(iGmRebase *d, const iGmDocument *doc) {
    d->oldSource = range_String(&doc->source);
    d->newSource = d->oldSource.start;
    d->oldRuns   = (iGmRunRange){ constData_Array(&doc->layout), constEnd_Array(&doc->layout) };
    d->newRuns   = d->oldRuns.start;
    d->layoutGen = doc->layoutGen;
    d->layoutInserted = doc->layoutInserted;
    d->layoutShift    = doc->layoutShift;
    d->numInserted    = 0;
    d->dy             = 0;
    d->isLayoutRedone = iFalse;
}

iBool update_GmRebase(iGmRebase *d, const iGmDocument *doc) {
    d->newSource      = constBegin_String(&doc->source);
    d->isLayoutRedone = (d->layoutGen != doc->layoutGen);
    d->numInserted    = d->isLayoutRedone ? 0 : doc->layoutInserted - d->layoutInserted;
    d->dy             = doc->layoutShift - d->layoutShift;
    d->newRuns        = d->numInserted ? constAt_Array(&doc->layout, d->numInserted)
                                       : constData_Array(&doc->layout);
    return (d->newSource != d->oldSource.start && !isEmpty_Range(&d->oldSource)) ||
           (d->newRuns != d->oldRuns.start && d->oldRuns.start != d->oldRuns.end);
}

static iBool isWithin_(const void *ptr, const void *start, const void *end) {
    /* The old memory may already be freed, so just compare the addresses. */
    return ptr && (uintptr_t) ptr >= (uintptr_t) start && (uintptr_t) ptr <= (uintptr_t) end;
}

static const char *rebaseSource_GmRebase_(const iGmRebase *d, const char *ptr) {
    /* Splitting may leave a range start one past the end. */
    if (isWithin_(ptr, d->oldSource.start, d->oldSource.end + 1)) {
        return d->newSource + ((uintptr_t) ptr - (uintptr_t) d->oldSource.start);
    }
    return ptr;
}

void apply_GmRebase(const iGmRebase *d, iRangecc *range) {
    if (d->newSource != d->oldSource.start) {
        range->start = rebaseSource_GmRebase_(d, range->start);
        range->end   = rebaseSource_GmRebase_(d, range->end);
    }
}

void applyRun_GmRebase(const iGmRebase *d, const iGmRun **run) {
    if (d->newRuns != d->oldRuns.start && isWithin_(*run, d->oldRuns.start, d->oldRuns.end)) {
        *run = d->newRuns + ((uintptr_t) *run - (uintptr_t) d->oldRuns.start) / sizeof(iGmRun);
    }
}

void applyRunRange_GmRebase(const iGmRebase *d, iGmRunRange *runs) {
    applyRun_GmRebase(d, &runs->start);
    applyRun_GmRebase(d, &runs->end);
}

void rebase_GmDocument_(iGmDocument *d, const iGmRebase *rebase) {
    iForEach(Array, r, &d->layout) {
        apply_GmRebase(rebase, &((iGmRun *) r.value)->text);
    }
    iForEach(PtrArray, i, &d->links) {
        iGmLink *link = i.ptr;
        apply_GmRebase(rebase, &link->urlRange);
        apply_GmRebase(rebase, &link->labelRange);
        apply_GmRebase(rebase, &link->labelIcon);
        apply_GmRebase(rebase, &link->textRange);
    }
    iForEach(Array, h, &d->headings) {
        apply_GmRebase(rebase, &((iGmHeading *) h.value)->text);
    }
    iForEach(Array, p, &d->preMeta) {
        iGmPreMeta *meta = p.value;
        apply_GmRebase(rebase, &meta->bounds);
        apply_GmRebase(rebase, &meta->altText);
        apply_GmRebase(rebase, &meta->contents);
        applyRunRange_GmRebase(rebase, &meta->runRange);
    }
    iForEach(Array, b, &d->pageBands) {
        iGmPageBands *bands = b.value;
        apply_GmRebase(rebase, &bands->header);
        apply_GmRebase(rebase, &bands->bookHeader);
        apply_GmRebase(rebase, &bands->rightHeader);
        apply_GmRebase(rebase, &bands->footer);
    }
    apply_GmRebase(rebase, &d->parser.line);
    rebaseTypesetter_GmDocument_(d, rebase);
}

iMedia *media_GmDocument(iGmDocument *d) {
    return d->media;
}

const iMedia *constMedia_GmDocument(const iGmDocument *d) {
    return d->media;
}

const iString *url_GmDocument(const iGmDocument *d) {
    return &d->url;
}

const iDocPrefs *docPrefs_GmDocument(const iGmDocument *d) {
    return &d->docPrefs;
}

const iScopeKey *scopeKey_GmDocument(const iGmDocument *d) {
    return &d->scopeKey;
}

static uint32_t applyDocPrefs_GmDocument_(iGmDocument *d, const iDocPrefs *prefs) {
    const uint32_t diff = diff_DocPrefs(&d->docPrefs, prefs);
    if (diff) {
        set_DocPrefs(&d->docPrefs, prefs);
        if (diff & palette_DocPrefsSpecFlag) {
            invalidatePalette_GmDocument(d);
        }
        if (diff & reparse_DocPrefsSpecFlag) {
            d->import.isValid = iFalse;
            invalidateParse_GmDocument_(d);
        }
        if (diff & (relayout_DocPrefsSpecFlag | reparse_DocPrefsSpecFlag)) {
            d->wrapWidth    = 0;
            d->maxLineWidth = -1;
            invalidateLayout_GmDocument(d);
        }
    }
    return diff;
}

void setDocPrefs_GmDocument(iGmDocument *d, const iDocPrefs *pinned) {
    d->flags.isDocPrefsPinned = iTrue;
    applyDocPrefs_GmDocument_(d, pinned);
}

uint32_t updateDocPrefs_GmDocument(iGmDocument *d) {
    if (d->flags.isDocPrefsPinned) {
        return 0;
    }
    iDocPrefs resolved;
    init_DocPrefs(&resolved);
    resolve_DocPrefs(&resolved, &d->scopeKey, &d->url,
                     !isEmpty_String(&d->mimeType) ? &d->mimeType : NULL);
    if (d->flags.isPageViewInverted) {
        resolved.pageView.value = !resolved.pageView.value;
    }
    const uint32_t diff = applyDocPrefs_GmDocument_(d, &resolved);
    deinit_DocPrefs(&resolved);
    return diff;
}

void setPageViewInverted_GmDocument(iGmDocument *d, iBool invert) {
    d->flags.isPageViewInverted = invert;
}

void setMimeType_GmDocument(iGmDocument *d, const iString *mimeType) {
    set_String(&d->mimeType, mimeType);
}

void setMode_GmDocument(iGmDocument *d, enum iGmDocumentMode mode) {
    d->flags.isCoverPage = (mode == coverPage_GmDocumentMode);
    d->flags.isUiTheme   = (mode == uiTheme_GmDocumentMode);
    invalidatePalette_GmDocument(d);
}

void setFormat_GmDocument(iGmDocument *d, enum iSourceFormat format) {
    const enum iSourceFormat viewFormat =
        (format == plainText_SourceFormat ? format : gemini_SourceFormat);
    if (d->origFormat != format || d->viewFormat != viewFormat) {
        d->import.isValid = iFalse;
    }
    d->origFormat = format;
    d->viewFormat = viewFormat;
}

iBool setViewFormat_GmDocument(iGmDocument *d, enum iSourceFormat viewFormat) {
    if (d->viewFormat != viewFormat) {
        d->viewFormat = viewFormat;
        import_GmDocument_(d);
        return iTrue;
    }
    return iFalse;
}

enum iSourceFormat viewFormat_GmDocument(const iGmDocument *d) {
    return d->viewFormat;
}

iBool equal_GmDocumentMetrics(const iGmDocumentMetrics *d, const iGmDocumentMetrics *other) {
    return d->width == other->width && d->canvasWidth == other->canvasWidth &&
           d->maxContentWidth == other->maxContentWidth && d->pageHeight == other->pageHeight &&
           d->columnsPerPage == other->columnsPerPage &&
           d->firstPageInset == other->firstPageInset && d->lastPageInset == other->lastPageInset &&
           d->bottomClearance == other->bottomClearance &&
           d->topInset == other->topInset && d->bottomInset == other->bottomInset &&
           d->singleColumnWidth == other->singleColumnWidth &&
           d->singleColumnMaxContentWidth == other->singleColumnMaxContentWidth;
}

void setMetrics_GmDocument(iGmDocument *d, const iGmDocumentMetrics *metrics) {
    d->requestedMetrics = *metrics;
    d->wrapWidth        = 0;
    doLayout_GmDocument_(d); /* TODO: just flag need-layout and do it later */
}

iBool updateMetrics_GmDocument(iGmDocument *d, const iGmDocumentMetrics *metrics) {
    if (!equal_GmDocumentMetrics(&d->requestedMetrics, metrics) || isLayoutOutdated_GmDocument(d)) {
        setMetrics_GmDocument(d, metrics);
        return iTrue;
    }
    return iFalse;
}

int contentWidth_GmDocument(const iGmDocument *d) {
    return d->contentWidth;
}

const iGmDocumentMetrics *metrics_GmDocument(const iGmDocument *d) {
    return &d->metrics;
}

const iGmDocumentMetrics *requestedMetrics_GmDocument(const iGmDocument *d) {
    return &d->requestedMetrics;
}

iBool isCoverPage_GmDocument(const iGmDocument *d) {
    return d->flags.isCoverPage;
}

iBool isPaged_GmDocument(const iGmDocument *d) {
    return d->metrics.pageHeight > 0;
}

int pageHeight_GmDocument(const iGmDocument *d) {
    return d->metrics.pageHeight;
}

size_t numColumns_GmDocument(const iGmDocument *d) {
    if (!isPaged_GmDocument(d)) {
        return 1;
    }
    return iMax(1, d->size.y / d->metrics.pageHeight);
}

int columnsPerPage_GmDocument(const iGmDocument *d) {
    return isPaged_GmDocument(d) ? iMax(1, d->metrics.columnsPerPage) : 1;
}

size_t numPages_GmDocument(const iGmDocument *d) {
    return iMax(1, numColumns_GmDocument(d) / columnsPerPage_GmDocument(d));
}

iBool setBook_GmDocument(iGmDocument *d, const iGmBookPosition *pos) {
    if (equal_String(&d->bookTitle, pos->title) &&
        equal_String(&d->chapterTitle, pos->chapterTitle) &&
        equal_String(&d->partTitle, pos->partTitle) && d->chapter == pos->chapter &&
        d->numChapters == pos->numChapters && d->flags.isBookCover == pos->isCover &&
        d->flags.isBookContents == pos->isContents && d->flags.isBookOpen == pos->isOpen) {
        return iFalse;
    }
    set_String(&d->bookTitle, pos->title);
    set_String(&d->chapterTitle, pos->chapterTitle);
    set_String(&d->partTitle, pos->partTitle);
    d->chapter            = pos->chapter;
    d->numChapters        = pos->numChapters;
    d->flags.isBookContents = pos->isContents;
    if (d->flags.isBookCover != pos->isCover || d->flags.isBookOpen != pos->isOpen) {
        d->flags.isBookCover = pos->isCover;
        d->flags.isBookOpen  = pos->isOpen;
        invalidatePalette_GmDocument(d);
    }
    return iTrue;
}

iBool setCoverImage_GmDocument(iGmDocument *d, iGmLinkId linkId) {
    if (d->coverImage == linkId) {
        return iFalse;
    }
    d->coverImage = linkId;
    return iTrue;
}

int chapter_GmDocument(const iGmDocument *d, int *numChapters_out) {
    if (numChapters_out) {
        *numChapters_out = d->numChapters;
    }
    return d->chapter;
}

iRangecc pageHeader_GmDocument(const iGmDocument *d, size_t page, iBool isSpread) {
    if (page >= size_Array(&d->pageBands)) {
        return iNullRange;
    }
    const iGmPageBands *bands = constAt_Array(&d->pageBands, page);
    if (isSpread && page % 2 == 0) {
        return !isEmpty_Range(&bands->bookHeader) ? bands->bookHeader : bands->header;
    }
    if (isSpread && !isEmpty_Range(&bands->rightHeader)) {
        const iGmPageBands *left = constAt_Array(&d->pageBands, page - 1);
        if (!isEmpty_Range(&left->bookHeader)) {
            return bands->rightHeader;
        }
    }
    return bands->header;
}

iRangecc pageFooter_GmDocument(const iGmDocument *d, size_t page) {
    return page < size_Array(&d->pageBands)
               ? ((const iGmPageBands *) constAt_Array(&d->pageBands, page))->footer
               : iNullRange;
}

size_t columnAt_GmDocument(const iGmDocument *d, int documentY) {
    if (!isPaged_GmDocument(d) || documentY < 0) {
        return 0;
    }
    return iMin((size_t) (documentY / d->metrics.pageHeight), numColumns_GmDocument(d) - 1);
}

void setLayoutSyncHeight_GmDocument(iGmDocument *d, int height) {
    d->layoutSyncHeight = height;
}

void redoLayout_GmDocument(iGmDocument *d) {
    doLayout_GmDocument_(d);
}

void invalidateLayout_GmDocument(iGmDocument *d) {
    d->flags.isLayoutInvalidated = iTrue;
}

iBool isLayoutOutdated_GmDocument(const iGmDocument *d) {
    return d->flags.isLayoutInvalidated || d->layoutFontKey != fontKey_App();
}

static void markLinkRunsVisited_GmDocument_(iGmDocument *d, const iIntSet *linkIds) {
    iForEach(Array, r, &d->layout) {
        iGmRun *run = r.value;
        if (run->linkId && !run->mediaId && contains_IntSet(linkIds, run->linkId)) {
            /* TODO: Does this even work? The font IDs may be different. */
            if (run->font == bold_FontId) {
                run->font = d->style.fonts[text_GmLineType];
            }
            else if (run->flags & decoration_GmRunFlag) {
                run->color = linkColor_GmDocument(d, run->linkId, icon_GmLinkPart);
            }
        }
    }
}

iBool updateOpenURLs_GmDocument(iGmDocument *d) {
    iBool wasChanged = iFalse;
    updateOpenURLs_GmDocument_(d);
    iIntSet linkIds;
    init_IntSet(&linkIds);
    iForEach(PtrArray, i, &d->links) {
        iGmLink *link = i.ptr;
        if (!equal_String(&link->url, &d->url)) {
            const iBool isOpen = contains_StringSet(d->openURLs, &link->url);
            if (isOpen ^ ((link->flags & isOpen_GmLinkFlag) != 0)) {
                iChangeFlags(link->flags, isOpen_GmLinkFlag, isOpen);
                if (isOpen) {
                    link->flags |= visited_GmLinkFlag;
                    insert_IntSet(&linkIds, index_PtrArrayIterator(&i) + 1);
                }
                wasChanged = iTrue;
            }
        }
    }
    markLinkRunsVisited_GmDocument_(d, &linkIds);
    deinit_IntSet(&linkIds);
    return wasChanged;
}

iLocalDef iBool isNormalizableSpace_(char ch) {
    return ch == ' ' || ch == '\t';
}

static void normalizeLine_GmDocument_(iGmDocument *d, iRangecc line, iString *out) {
    static iRegExp *ansiCursorFwdPattern_;
    if (!ansiCursorFwdPattern_) {
        ansiCursorFwdPattern_ = new_RegExp("^\x1b\\[([0-9]+)C", 0);
    }
    iGmImport *imp = &d->import;
    if (imp->isPreformat) {
        const char *span = line.start;
        for (const char *ch = line.start; ch != line.end; ch++) {
            if (*ch == 0x1b) {
                /* We can emulate an ANSI cursor forward sequence by adding spaces. */
                iRegExpMatch m;
                init_RegExpMatch(&m);
                if (matchRange_RegExp(ansiCursorFwdPattern_, (iRangecc){ ch, line.end }, &m)) {
                    appendRange_String(out, (iRangecc){ span, ch });
                    int num = strtoul(capturedRange_RegExpMatch(&m, 1).start, NULL, 10);
                    if (num > 0 && num < 200 /* arbitrary sanity limit */) {
                        for (int i = 0; i < num; i++) {
                            appendData_Block(&out->chars, " ", 1);
                        }
                    }
                    ch = end_RegExpMatch(&m) - 1;
                    span = ch + 1;
                    continue;
                }
            }
            if (*ch == '\v') {
                appendRange_String(out, (iRangecc){ span, ch });
                span = ch + 1;
            }
        }
        appendRange_String(out, (iRangecc){ span, line.end });
        appendCStr_String(out, "\n");
        if (d->format == gemini_SourceFormat &&
            lineType_GmDocument_(d, line) == preformatted_GmLineType) {
            imp->isPreformat = iFalse;
        }
        return;
    }
    if (lineType_GmDocument_(d, line) == preformatted_GmLineType) {
        imp->isPreformat = iTrue;
        appendRange_String(out, line);
        appendCStr_String(out, "\n");
        return;
    }
    /* The normalized line is never longer than the original. */
    const size_t start = size_String(out);
    resize_Block(&out->chars, start + size_Range(&line) + 1);
    char  *dst         = data_Block(&out->chars) + start;
    iBool  isPrevSpace = iFalse;
    int    spaceCount  = 0;
    for (const char *ch = line.start; ch != line.end; ch++) {
        char c = *ch;
        if (c == '\v') {
            continue;
        }
        if (isNormalizableSpace_(c)) {
            if (isPrevSpace) {
                if (++spaceCount == 8) {
                    /* There are several consecutive space characters. The author likely
                       really wants to have some space here, so normalize to a tab stop. */
                    dst[-1] = '\t';
                }
                continue; /* skip repeated spaces */
            }
            c = ' ';
            isPrevSpace = iTrue;
        }
        else {
            isPrevSpace = iFalse;
            spaceCount = 0;
        }
        *dst++ = c;
    }
    *dst++ = '\n';
    truncate_Block(&out->chars, dst - (const char *) constData_Block(&out->chars));
}

static void normalize_GmDocument_(iGmDocument *d, iRangecc src, iBool isStart, iString *out) {
    if (isStart) {
        /* Check for a BOM. In UTF-8, the BOM can just be skipped if present. */
        iChar ch = 0;
        decodeBytes_MultibyteChar(src.start, src.end, &ch);
        if (ch == 0xfeff) /* zero-width non-breaking space */ {
            src.start += 3;
        }
        /* Splitting skips a newline at the start (see `nextSplit_Rangecc`). */
        if (src.start < src.end && *src.start == '\n') {
            src.start++;
        }
    }
    while (src.start < src.end) {
        const char *nl   = memchr(src.start, '\n', src.end - src.start);
        const iRangecc line = { src.start, nl ? nl : src.end };
        normalizeLine_GmDocument_(d, line, out);
        src.start = nl ? nl + 1 : src.end;
    }
}

void setUrl_GmDocument(iGmDocument *d, const iString *url) {
    url = canonicalUrl_String(url);
    if (!equal_String(&d->url, url)) {
        d->import.isValid = iFalse;
        invalidateParse_GmDocument_(d);
    }
    set_String(&d->url, url);
    deinit_ScopeKey(&d->scopeKey);
    initUrl_ScopeKey(&d->scopeKey, url);
    clear_String(&d->themeUrl);
    /* Pages of a local Gempub have a scope of their own, and each book is a site. */ {
        const iString *archive = localArchiveUrl_Gempub(url);
        if (archive) {
            setCStr_String(&d->scopeKey.scheme, "gempub");
            set_String(&d->scopeKey.site, archive);
            const iString *source =
                sourceUrl_Books(collect_String(localFilePathFromUrl_String(archive)));
            if (source && !isLocalFileScheme_Rangecc(urlScheme_String(source))) {
                set_String(&d->themeUrl, source);
            }
        }
    }
    /* Tinylog entries are themed and scoped as the subscribed tinylog. */ {
        const uint32_t   bmId = tinylogBookmark_Feeds(url);
        const iBookmark *bm   = bmId ? get_Bookmarks(bookmarks_App(), bmId) : NULL;
        d->flags.isTinylogEntry = (bm != NULL);
        clear_String(&d->fixedTitle);
        if (bm) {
            deinit_ScopeKey(&d->scopeKey);
            initUrl_ScopeKey(&d->scopeKey, &bm->url);
            set_String(&d->themeUrl, &bm->url);
            set_String(&d->fixedTitle, &bm->title);
        }
    }
    updateDocPrefs_GmDocument(d);
    setThemeSeed_GmDocument(
        d, paletteSeed_GmDocument_(d), urlThemeSeed_String(themeUrl_GmDocument_(d)));
    iUrl parts;
    init_Url(&parts, url);
    setRange_String(&d->localHost, parts.host);
    updateIconBasedOnUrl_GmDocument_(d);
    if (!cmp_String(url, "about:fonts")) {
        /* This is an interactive internal page. */
        d->flags.enableCommandLinks = iTrue;
    }
    d->flags.isSpartan = equalCase_Rangecc(parts.scheme, "spartan");
    d->flags.isNex     = equalCase_Rangecc(parts.scheme, "nex") &&
                         (isEmpty_Range(&parts.path) || endsWith_Rangecc(parts.path, "/"));
    d->flags.isGopherMenu = isGopherScheme_Rangecc(parts.scheme) &&
                            startsWith_Rangecc(parts.path, "/1");
}

static void removeNulls_String_(iString *d) {
    if (!memchr(cstr_String(d), 0, size_String(d))) {
        return;
    }
    char        *data = data_Block(&d->chars);
    const size_t size = size_String(d);
    size_t       n    = 0;
    for (size_t i = 0; i < size; i++) {
        if (data[i]) {
            data[n++] = data[i];
        }
    }
    truncate_Block(&d->chars, n);
}

static iBool hasAnsiEscapes_(iRangecc text) {
    static iRegExp *ansiEsc_;
    if (!ansiEsc_) {
        ansiEsc_ = new_RegExp("\x1b[[()]([0-9;AB]*?)[ABCDEFGHJKSTfimn]", 0);
    }
    iRegExpMatch m;
    init_RegExpMatch(&m);
    return matchRange_RegExp(ansiEsc_, text, &m);
}

static iBool isImportedWhole_GmDocument_(const iGmDocument *d) {
    /* Markdown conversion needs the whole document. */
    return d->origFormat == markdown_SourceFormat && d->viewFormat == gemini_SourceFormat;
}

static void importMarkdown_GmDocument_(iGmDocument *d) {
    set_String(&d->source, &d->origSource);
    replace_String(&d->source, "\r\n", "\n");
    removeNulls_String_(&d->source);
    convertMarkdownToGemtext_GmDocument_(d);
    d->flags.isConvertedMarkdown = iTrue;
    if (shouldBeNormalized_GmDocument_(d)) {
        iString *converted = copy_String(&d->source);
        clear_String(&d->source);
        normalize_GmDocument_(d, range_String(converted), iTrue, &d->source);
        delete_String(converted);
    }
    d->import.pos = size_String(&d->origSource);
}

static void importMore_GmDocument_(iGmDocument *d) {
    /* Appends the newly received lines to the source. An incomplete last line is held back
       until the source is final. */
    d->maxLineWidth = -1;
    const iRangecc orig  = range_String(&d->origSource);
    const char    *start = orig.start + d->import.pos;
    const char    *end   = orig.end;
    if (!d->flags.isSourceFinal) {
        while (end > start && end[-1] != '\n') {
            end--;
        }
    }
    if (start == end) {
        return;
    }
    const iBool isStart = (d->import.pos == 0);
    d->import.pos = end - orig.start;
    d->flags.isLayoutCaughtUp = iFalse;
    iString chunk;
    initRange_String(&chunk, (iRangecc){ start, end });
    replace_String(&chunk, "\r\n", "\n");
    removeNulls_String_(&chunk);
    if (hasAnsiEscapes_(range_String(&chunk))) {
        d->warnings |= ansiEscapes_GmDocumentWarning;
    }
    iGmRebase rebase;
    init_GmRebase(&rebase, d);
    if (shouldBeNormalized_GmDocument_(d)) {
        normalize_GmDocument_(d, range_String(&chunk), isStart, &d->source);
    }
    else {
        append_String(&d->source, &chunk);
    }
    deinit_String(&chunk);
#if !defined (NDEBUG)
    /* Always move the source to catch missed rebases. */ {
        iBlock *moved = newData_Block(constData_Block(&d->source.chars), size_String(&d->source));
        set_Block(&d->source.chars, moved);
        delete_Block(moved);
    }
#endif
    if (update_GmRebase(&rebase, d)) {
        rebase_GmDocument_(d, &rebase);
    }
}

static void import_GmDocument_(iGmDocument *d) {
    deleteTypesetter_GmDocument_(d);
    invalidateParse_GmDocument_(d);
    d->maxLineWidth = -1;
    d->format = d->origFormat;
    d->flags.isConvertedMarkdown = iFalse;
    d->import.isValid     = iTrue;
    d->import.pos         = 0;
    d->import.isPreformat = (d->format == plainText_SourceFormat);
    clear_String(&d->source);
    if (!d->flags.isSourceFinal) {
        reserve_Block(&d->source.chars, 0x10000);
    }
    d->warnings &= ~ansiEscapes_GmDocumentWarning;
    if (d->viewFormat == plainText_SourceFormat) {
        d->format = plainText_SourceFormat;
        d->ansiEscapes = allowAll_AnsiFlag;
    }
    else if (d->format == gemini_SourceFormat) {
        d->ansiEscapes = d->docPrefs.ansiEscapes.value;
    }
    else {
        d->ansiEscapes = allowAll_AnsiFlag; /* Markdown uses escapes for styling */
    }
    if (isImportedWhole_GmDocument_(d)) {
        if (hasAnsiEscapes_(range_String(&d->origSource))) {
            d->warnings |= ansiEscapes_GmDocumentWarning;
        }
        importMarkdown_GmDocument_(d);
    }
    else {
        importMore_GmDocument_(d);
    }
}

iBool setSource_GmDocument(iGmDocument *d, const iString *source,
                           const iGmDocumentMetrics *metrics, enum iGmDocumentUpdate updateType) {
    const iBool  isFinal  = (updateType == final_GmDocumentUpdate);
    const size_t oldSize  = size_String(&d->origSource);
    const iBool  isPrefix = size_String(source) >= oldSize &&
                            !memcmp(constBegin_String(source), constBegin_String(&d->origSource), oldSize);
    if (isPrefix && size_String(source) == oldSize && d->import.isValid &&
        isFinal == d->flags.isSourceFinal) {
        updateMetrics_GmDocument(d, metrics);
        return iFalse; /* Nothing to do. */
    }
    const iBool isAppend = isPrefix && d->import.isValid && !d->flags.isSourceFinal &&
                           !isImportedWhole_GmDocument_(d);
    d->flags.isSourceFinal = isFinal;
#if !defined (NDEBUG)
    iTime startTime;
    initCurrent_Time(&startTime);
#endif
    if (!isAppend) {
        set_String(&d->origSource, source);
        import_GmDocument_(d);
#if !defined (NDEBUG)
        logTiming_GmDocument_(d, "import", &startTime);
#endif
        setMetrics_GmDocument(d, metrics); /* re-do layout */
        return iFalse;
    }
    appendRange_String(&d->origSource, (iRangecc){ constBegin_String(source) + oldSize,
                                                   constEnd_String(source) });
    importMore_GmDocument_(d);
#if !defined (NDEBUG)
    logTiming_GmDocument_(d, "append", &startTime);
#endif
    if (d->typesetter && equal_GmDocumentMetrics(&d->requestedMetrics, metrics) &&
        !isLayoutOutdated_GmDocument(d)) {
        const uint32_t gen = d->layoutGen;
        continueLayout_GmDocument(d, d->layoutSyncHeight > 0 ? d->layoutSyncHeight : INT_MAX, 0.0);
        return gen == d->layoutGen;
    }
    setMetrics_GmDocument(d, metrics);
    return iFalse;
}

void foldPre_GmDocument(iGmDocument *d, uint16_t preId) {
    if (preId > 0 && preId <= size_Array(&d->preMeta)) {
        iGmPreMeta *meta = at_Array(&d->preMeta, preId - 1);
        meta->flags ^= folded_GmPreMetaFlag;
    }
}

void updateVisitedLinks_GmDocument(iGmDocument *d) {
    iIntSet linkIds;
    init_IntSet(&linkIds);
    iForEach(PtrArray, i, &d->links) {
        iGmLink *link = i.ptr;
        const iTime visitTime = urlVisitTime_Visited(visited_App(), &link->url);
        if (~link->flags & visited_GmLinkFlag) {
            if (isValid_Time(&visitTime)) {
                link->when = visitTime;
                link->flags |= visited_GmLinkFlag;
                insert_IntSet(&linkIds, index_PtrArrayIterator(&i) + 1);
            }
        }
        else if (!isValid_Time(&visitTime) && ~link->flags & isOpen_GmLinkFlag) {
            /* Removed from history; runs are updated in the next layout. */
            link->when = visitTime;
            link->flags &= ~visited_GmLinkFlag;
        }
    }
    markLinkRunsVisited_GmDocument_(d, &linkIds);
    deinit_IntSet(&linkIds);
}

size_t numPre_GmDocument(const iGmDocument *d) {
    return size_Array(&d->preMeta);
}

const iGmPreMeta *preMeta_GmDocument(const iGmDocument *d, uint16_t preId) {
    if (preId > 0 && preId <= size_Array(&d->preMeta)) {
        return constAt_Array(&d->preMeta, preId - 1);
    }
    return NULL;
}

static size_t firstRunReaching_GmDocument_(const iGmDocument *d, int y) {
    /* Index of the first run whose bottom reaches `y`. Runs are ordered by their tops, so
       the ones starting well above `y` are skipped with a binary search. */
    const iGmRun *runs = constData_Array(&d->layout);
    const size_t  num  = size_Array(&d->layout);
    const int     skip = y - 2 * d->maxRunHeight;
    size_t lo = 0, hi = num;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (top_Rect(runs[mid].visBounds) < skip) {
            lo = mid + 1;
        }
        else {
            hi = mid;
        }
    }
    while (lo < num && bottom_Rect(runs[lo].visBounds) < y) {
        lo++;
    }
#if !defined (NDEBUG)
    /* Check validity. */
    static int isChecked_ = -1;
    if (isChecked_ < 0) {
        isChecked_ = getenv("LAGRANGE_LAYOUT_CHECK") != NULL;
    }
    if (isChecked_) {
        size_t first = 0;
        while (first < num && bottom_Rect(runs[first].visBounds) < y) {
            first++;
        }
        if (first != lo) {
            fprintf(stderr, "[GmDocument] first run reaching %d: %zu, expected %zu\n", y, lo,
                    first);
        }
    }
#endif
    return lo;
}

void render_GmDocument(const iGmDocument *d, iRangei visRangeY, iGmDocumentRenderFunc render,
                       void *context) {
    setAnsiFlags_Text(d->ansiEscapes);
    const iGmRun *runs  = constData_Array(&d->layout);
    const size_t  num   = size_Array(&d->layout);
    const size_t  first = firstRunReaching_GmDocument_(d, visRangeY.start);
    for (size_t i = first; i < num; i++) {
        /* The first run is included even if it starts below the range. Progressive buffer
           filling continues from it. */
        if (i > first && top_Rect(runs[i].visBounds) > visRangeY.end) {
            break;
        }
        render(context, runs + i);
    }
    setAnsiFlags_Text(allowAll_AnsiFlag);
}

static iBool isValidRun_GmDocument_(const iGmDocument *d, const iGmRun *run) {
    if (isEmpty_Array(&d->layout)) {
        return iFalse;
    }
    return run >= (const iGmRun *) constAt_Array(&d->layout, 0) &&
           run < (const iGmRun *) constEnd_Array(&d->layout);
}

const iGmRun *renderProgressive_GmDocument(const iGmDocument *d, const iGmRun *first, int dir,
                                           size_t maxCount,
                                           iRangei visRangeY, iGmDocumentRenderFunc render,
                                           void *context) {
    setAnsiFlags_Text(d->ansiEscapes);
    const iGmRun *run = first;
    while (isValidRun_GmDocument_(d, run)) {
        if ((dir < 0 && bottom_Rect(run->visBounds) <= visRangeY.start) ||
            (dir > 0 && top_Rect(run->visBounds) >= visRangeY.end)) {
            break;
        }
        if (maxCount-- == 0) {
            break;
        }
        render(context, run);
        run += dir;
    }
    setAnsiFlags_Text(allowAll_AnsiFlag);
    return isValidRun_GmDocument_(d, run) ? run : NULL;
}

enum iSourceFormat format_GmDocument(const iGmDocument *d) {
    return d->origFormat;
}

iInt2 size_GmDocument(const iGmDocument *d) {
    return d->size;
}

const iArray *headings_GmDocument(const iGmDocument *d) {
    return &d->headings;
}

const iString *source_GmDocument(const iGmDocument *d) {
    return &d->source;
}

iGmRunRange runRange_GmDocument(const iGmDocument *d) {
    if (isEmpty_Array(&d->layout)) {
        return (iGmRunRange){ NULL, NULL };
    }
    return (iGmRunRange){ constData_Array(&d->layout), constEnd_Array(&d->layout) };
}

size_t memorySize_GmDocument(const iGmDocument *d) {
    return size_String(&d->origSource) +
           size_String(&d->source) +
           size_Array(&d->layout) * sizeof(iGmRun) +
           size_Array(&d->links)  * sizeof(iGmLink) +
           memorySize_Media(d->media);
}

void setWarning_GmDocument(iGmDocument *d, int warning, iBool set) {
    iChangeFlags(d->warnings, warning, set);
}

int warnings_GmDocument(const iGmDocument *d) {
    return d->warnings;
}

iRangecc findText_GmDocument(const iGmDocument *d, const iString *text, const char *start) {
    const char * src      = constBegin_String(&d->source);
    const size_t startPos = (start ? start - src : 0);
    const size_t pos =
        indexOfCStrFromSc_String(&d->source, cstr_String(text), startPos, &iCaseInsensitive);
    if (pos == iInvalidPos) {
        return iNullRange;
    }
    return (iRangecc){ src + pos, src + pos + size_String(text) };
}

iRangecc findTextBefore_GmDocument(const iGmDocument *d, const iString *text, const char *before) {
    iRangecc found = iNullRange;
    const char *start = constBegin_String(&d->source);
    if (!before) before = constEnd_String(&d->source);
    while (start < before) {
        iRangecc range = findText_GmDocument(d, text, start);
        if (range.start == NULL || range.start >= before) break;
        found = range;
        start = range.end;
    }
    return found;
}

iGmRunRange findPreformattedRange_GmDocument(const iGmDocument *d, const iGmRun *run) {
    iAssert(preId_GmRun(run));
    iGmRunRange range = { run, run };
    /* Find the beginning. */
    while (range.start > (const iGmRun *) constData_Array(&d->layout)) {
        const iGmRun *prev = range.start - 1;
        if (preId_GmRun(prev) != preId_GmRun(run)) break;
        range.start = prev;
    }
    /* Find the ending. */
    while (range.end < (const iGmRun *) constEnd_Array(&d->layout)) {
        if (preId_GmRun(range.end) != preId_GmRun(run)) break;
        range.end++;
    }
    return range;
}

const iGmRun *findRun_GmDocument(const iGmDocument *d, iInt2 pos) {
    /* TODO: Perf optimization likely needed; use a block map? */
    const iGmRun *last = NULL;
    iBool isFirstNonDecoration = iTrue;
    iConstForEach(Array, i, &d->layout) {
        const iGmRun *run = i.value;
        if (run->flags & decoration_GmRunFlag) {
            continue;
        }
        const iRangei span = ySpan_Rect(run->bounds);
        if (contains_Range(&span, pos.y) &&
            pos.x >= left_Rect(run->bounds) && pos.x < right_Rect(run->bounds)) {
            last = run;
            break;
        }
        if (isFirstNonDecoration && pos.y < top_Rect(run->bounds)) {
            last = run;
            break;
        }
        if (top_Rect(run->bounds) >= pos.y) {
            break; /* Below the point. */
        }
        last = run;
        isFirstNonDecoration = iFalse;
    }
//    if (last) {
//        printf("found run at (%d,%d): %p [%s]\n", pos.x, pos.y, last, cstr_Rangecc(last->text));
//        fflush(stdout);
//    }
    return last;
}

iRangecc findLoc_GmDocument(const iGmDocument *d, iInt2 pos) {
    const iGmRun *run = findRun_GmDocument(d, pos);
    if (run) {
        return findLoc_GmRun(run, pos);
    }
    return iNullRange;
}

const iGmRun *findRunAtLoc_GmDocument(const iGmDocument *d, const char *textCStr) {
    iConstForEach(Array, i, &d->layout) {
        const iGmRun *run = i.value;
        if (run->flags & decoration_GmRunFlag) {
            continue;
        }
        if (contains_Range(&run->text, textCStr) || run->text.start > textCStr /* went past */) {
            return run;
        }
    }
    return NULL;
}

const char *sourceLoc_GmDocument(const iGmDocument *d, const iGmRun *run) {
    const iRangecc src = range_String(&d->source);
    if (~run->flags & decoration_GmRunFlag && run->text.start >= src.start &&
        run->text.start <= src.end) {
        return run->text.start;
    }
    if (preId_GmRun(run)) {
        return constValue_Array(&d->preMeta, preId_GmRun(run) - 1, iGmPreMeta).bounds.start;
    }
    if (run->linkId) {
        return ((const iGmLink *) constAt_PtrArray(&d->links, run->linkId - 1))->urlRange.start;
    }
    return NULL;
}

const iGmRun *precedingRun_GmDocument(const iGmDocument *d, const iGmRun *run) {
    run--;
    if (indexOf_Array(&d->layout, run) == iInvalidPos) {
        return NULL;
    }
    return run;
}

const iGmRun *findInputPromptRun_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    const iGmRunRange range = runRange_GmDocument(d);
    for (const iGmRun *run = range.start; run != range.end; run++) {
        if (run->mediaType == inputPrompt_MediaType && run->linkId == linkId) {
            return run;
        }
    }
    return NULL;
}

static const iGmLink *link_GmDocument_(const iGmDocument *d, iGmLinkId id) {
    if (id > 0 && id <= size_PtrArray(&d->links)) {
        return constAt_PtrArray(&d->links, id - 1);
    }
    return NULL;
}

size_t numLinks_GmDocument(const iGmDocument *d) {
    return size_PtrArray(&d->links);
}

const iString *linkUrl_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    const iGmLink *link = link_GmDocument_(d, linkId);
    return link ? &link->url : NULL;
}

iRangecc linkUrlRange_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    const iGmLink *link = link_GmDocument_(d, linkId);
    return link->urlRange;
}

iRangecc linkLabel_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    const iGmLink *link = link_GmDocument_(d, linkId);
    if (isEmpty_Range(&link->labelRange)) {
        return link->urlRange;
    }
    return link->labelRange;
}

int linkFlags_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    const iGmLink *link = link_GmDocument_(d, linkId);
    return link ? link->flags : 0;
}

const iTime *linkTime_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    const iGmLink *link = link_GmDocument_(d, linkId);
    return link ? &link->when : NULL;
}

iMediaId linkImage_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    return findLinkImage_Media(d->media, linkId);
}

iMediaId linkAudio_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    return findLinkAudio_Media(d->media, linkId);
}

iLocalDef iBool isWWW_GmLinkScheme(enum iGmLinkScheme d) {
    return d == http_GmLinkScheme || d == mailto_GmLinkScheme;
}

iLocalDef iBool isOldSchool_GmLinkScheme(enum iGmLinkScheme d) {
    return d == gopher_GmLinkScheme || d == finger_GmLinkScheme;
}

enum iColorId linkColor_GmDocument(const iGmDocument *d, iGmLinkId linkId, enum iGmLinkPart part) {
    const iGmLink *link = link_GmDocument_(d, linkId);
    if (!link) {
        return none_ColorId;
    }
    const enum iGmLinkScheme scheme = scheme_GmLinkFlag(link->flags);
    if (link) {
        const iBool isUnsupported = (link->flags & supportedScheme_GmLinkFlag) == 0;
        if (part == icon_GmLinkPart) {
            if (isUnsupported) {
                return tmBadLink_ColorId;
            }
            if (scheme != mailto_GmLinkScheme && link->flags & iconFromLabel_GmLinkFlag) {
                return link->flags & visited_GmLinkFlag ? tmLinkCustomIconVisited_ColorId
                                                        : tmLinkIcon_ColorId;
            }
            if (link->flags & visited_GmLinkFlag) {
                return isWWW_GmLinkScheme(scheme)         ? tmHypertextLinkIconVisited_ColorId
                       : isOldSchool_GmLinkScheme(scheme) ? tmGopherLinkIconVisited_ColorId
                                                          : tmLinkIconVisited_ColorId;
            }
            return isWWW_GmLinkScheme(scheme)         ? tmHypertextLinkIcon_ColorId
                   : isOldSchool_GmLinkScheme(scheme) ? tmGopherLinkIcon_ColorId
                                                      : tmLinkIcon_ColorId;
        }
        if (part == text_GmLinkPart) {
            return isWWW_GmLinkScheme(scheme)         ? tmHypertextLinkText_ColorId
                   : isOldSchool_GmLinkScheme(scheme) ? tmGopherLinkText_ColorId
                                                      : tmLinkText_ColorId;
        }
        if (part == textHover_GmLinkPart) {
            return isWWW_GmLinkScheme(scheme)         ? tmHypertextLinkTextHover_ColorId
                   : isOldSchool_GmLinkScheme(scheme) ? tmGopherLinkTextHover_ColorId
                                                      : tmLinkTextHover_ColorId;
        }
    }
    return tmLinkText_ColorId;
}

iBool isMediaLink_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    if (isTerminal_Platform()) {
        return iFalse; /* can't show/play media (TODO: image rendering?) */
    }
    /* We may already have media for this link. */
    if (findMediaForLink_Media(d->media, linkId, none_MediaType).type) {
        return iTrue;
    }
    /* Check the URL if it appears like a potential media link. */
    const iString *dstUrl = absoluteUrl_String(&d->url, linkUrl_GmDocument(d, linkId));
    const iRangecc scheme = urlScheme_String(dstUrl);
    if (equalCase_Rangecc(scheme, "gemini") || isGopherScheme_Rangecc(scheme) ||
        equalCase_Rangecc(scheme, "spartan") || equalCase_Rangecc(scheme, "nex") ||
        equalCase_Rangecc(scheme, "finger") || isLocalFileScheme_Rangecc(scheme) ||
        willUseProxy_App(scheme)) {
        return (linkFlags_GmDocument(d, linkId) &
                (imageFileExtension_GmLinkFlag | audioFileExtension_GmLinkFlag)) != 0;
    }
    return iFalse;
}

const iString *title_GmDocument(const iGmDocument *d) {
    return &d->title;
}

iChar siteIcon_GmDocument(const iGmDocument *d) {
    return d->siteIcon;
}

int ansiEscapes_GmDocument(const iGmDocument *d) {
    return d->ansiEscapes;
}

void runBaseAttributes_GmDocument(const iGmDocument *d, const iGmRun *run, int *fontId_out,
                                  int *colorId_out) {
    /* Font and color according to the line type. These are needed because each GmRun is
       a segment of a paragraph, and if the font or color changes inside the run, each wrapped
       segment needs to know both the current font/color and ALSO the base font/color, so
       the default attributes can be restored. */
    if (run->isLede) {
        *fontId_out  = firstParagraph_FontId;
        *colorId_out = tmFirstParagraph_ColorId;
    }
    else {
        *fontId_out  = fontWithSize_Text(d->style.fonts[run->lineType], run->font % max_FontSize); /* retain size */
        *colorId_out = d->style.colors[run->lineType];
    }
}

iBool isJustified_GmRun(const iGmRun *d) {
    return (d->flags & (notJustified_GmRunFlag | endOfLine_GmRunFlag)) == 0;
}

int drawBoundWidth_GmRun(const iGmRun *d) {
    return (d->isRTL ? -1 : 1) * width_Rect(isJustified_GmRun(d) ? d->bounds : d->visBounds);
}

iRangecc findLoc_GmRun(const iGmRun *d, iInt2 pos) {
    if (pos.y < top_Rect(d->bounds)) {
        return (iRangecc){ d->text.start, d->text.start };
    }
    if (pos.y > bottom_Rect(d->bounds)) {
        return (iRangecc){ d->text.end, d->text.end };
    }
    const int x = pos.x - left_Rect(d->bounds);
    if (x <= 0) {
        return (iRangecc){ d->text.start, d->text.start };
    }
    if (x > d->bounds.size.x) {
        return (iRangecc){ d->text.end, d->text.end };
    }
    iRangecc loc;
    iWrapText wt = { .text     = d->text,
                     .mode     = anyCharacter_WrapTextMode,
                     .maxWidth = isJustified_GmRun(d) ? drawBoundWidth_GmRun(d) : 0,
                     .justify  = isJustified_GmRun(d),
                     .hitPoint = init_I2(x, 0) };
    measure_WrapText(&wt, d->font);
    loc.start = loc.end = wt.hitChar_out;
    if (!contains_Range(&d->text, loc.start) && loc.start != d->text.end) {
        return iNullRange; /* it's some other text */
    }
    iChar ch;
    if (d->text.end && d->text.end != loc.start) {
        int chLen = decodeBytes_MultibyteChar(loc.start, d->text.end, &ch);
        if (chLen > 0) {
            /* End after the character. */
            loc.end += chLen;
        }
    }
    return loc;
}

iInt2 preRunMargin_GmDocument(const iGmDocument *d, uint16_t preId) {
    iUnused(d, preId);
    return init_I2(3 * gap_Text, 2 * gap_Text);
}

iBool preIsFolded_GmDocument(const iGmDocument *d, uint16_t preId) {
    const iGmPreMeta *meta = preMeta_GmDocument(d, preId);
    return meta && (meta->flags & folded_GmPreMetaFlag) != 0;
}

iBool preHasAltText_GmDocument(const iGmDocument *d, uint16_t preId) {
    const iGmPreMeta *meta = preMeta_GmDocument(d, preId);
    return meta && !isEmpty_Range(&meta->altText);
}

#if !defined (NDEBUG)
static void appendRange_GmDocument_(const iGmDocument *d, iString *out, iRangecc range) {
    const iRangecc src = range_String(&d->source);
    if (!range.start) {
        appendCStr_String(out, " -");
    }
    else if (range.start >= src.start && range.end <= src.end) {
        appendFormat_String(out, " %zu:%zu", (size_t) (range.start - src.start), size_Range(&range));
    }
    else {
        appendFormat_String(out, " \"%s\"", cstr_Rangecc(range));
    }
}

void layoutDigest_GmDocument(const iGmDocument *d, iString *out) {
    /* Everything that a layout produces, for comparing layouts. */
    appendFormat_String(out, "size %d %d contentWidth %d title \"%s\"\n",
                        d->size.x, d->size.y, d->contentWidth, cstr_String(&d->title));
    iConstForEach(PtrArray, i, &d->links) {
        const iGmLink *link = i.ptr;
        appendFormat_String(out, "link %zu %s %x", index_PtrArrayConstIterator(&i) + 1,
                            cstr_String(&link->url), link->flags);
        appendRange_GmDocument_(d, out, link->urlRange);
        appendRange_GmDocument_(d, out, link->labelRange);
        appendRange_GmDocument_(d, out, link->labelIcon);
        appendCStr_String(out, "\n");
    }
    iConstForEach(Array, h, &d->headings) {
        const iGmHeading *head = h.value;
        appendFormat_String(out, "heading %d", head->level);
        appendRange_GmDocument_(d, out, head->text);
        appendCStr_String(out, "\n");
    }
    iConstForEach(Array, p, &d->preMeta) {
        const iGmPreMeta *meta = p.value;
        appendFormat_String(out, "pre %x %d %d %d %d", meta->flags, meta->pixelRect.pos.x,
                            meta->pixelRect.pos.y, meta->pixelRect.size.x, meta->pixelRect.size.y);
        appendRange_GmDocument_(d, out, meta->bounds);
        appendRange_GmDocument_(d, out, meta->altText);
        appendRange_GmDocument_(d, out, meta->contents);
        appendFormat_String(out, " runs %zd %zd\n",
                            meta->runRange.start ? meta->runRange.start - (const iGmRun *) constData_Array(&d->layout) : -1,
                            meta->runRange.end ? meta->runRange.end - (const iGmRun *) constData_Array(&d->layout) : -1);
    }
    iConstForEach(Array, r, &d->layout) {
        const iGmRun *run = r.value;
        appendFormat_String(out, "run %d %d %d %d / %d %d %d %d L%u F%x R%u C%u f%u m%u:%u t%u e%u",
                            run->bounds.pos.x, run->bounds.pos.y,
                            run->bounds.size.x, run->bounds.size.y,
                            run->visBounds.pos.x, run->visBounds.pos.y,
                            run->visBounds.size.x, run->visBounds.size.y,
                            run->linkId, run->flags, run->isRTL, run->color, run->font,
                            run->mediaType, run->mediaId, run->lineType, run->isLede);
        appendRange_GmDocument_(d, out, run->text);
        appendCStr_String(out, "\n");
    }
}

void logTiming_GmDocument_(const iGmDocument *d, const char *phase, const iTime *start) {
    /* Set LAGRANGE_LAYOUT_TIMING to print the duration of document processing phases. */
    static int isEnabled_ = -1;
    if (isEnabled_ < 0) {
        isEnabled_ = getenv("LAGRANGE_LAYOUT_TIMING") != NULL;
    }
    if (isEnabled_) {
        fprintf(stderr, "[GmDocument] %-8s %7.2f ms  source:%zu runs:%zu  %s\n", phase,
               elapsedSeconds_Time(start) * 1000.0, size_String(&d->source),
               size_Array(&d->layout), cstr_String(&d->url));
    }
}

void dumpLayout_GmDocument_(const iGmDocument *d) {
    /* Set LAGRANGE_LAYOUT_DUMP to a file path to append each finished layout there. */
    const char *path = getenv("LAGRANGE_LAYOUT_DUMP");
    if (!path || !*path) {
        return;
    }
    iString *out = collectNewFormat_String("=== %s\n", cstr_String(&d->url));
    layoutDigest_GmDocument(d, out);
    FILE *f = fopen(path, "a");
    if (f) {
        fwrite(cstr_String(out), 1, size_String(out), f);
        fclose(f);
    }
}
#endif

iDefineClass(GmDocument)
