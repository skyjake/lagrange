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

#include "book.h"
#include "impl.h"

#include "../app.h"
#include "../books.h"
#include "../gempub.h"
#include "../gmdocument.h"
#include "../gmutil.h"
#include "documentview.h"
#include "fetch.h"
#include "pagedview.h"
#include "swipe.h"
#include "ui/command.h"
#include "ui/root.h"
#include "ui/scrollwidget.h"
#include "ui/util.h"
#include "ui/widget.h"
#include "ui/window.h"

#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <SDL3/SDL_timer.h>
#include <limits.h>
#include <math.h>

static const int progressUnit_DocumentBook_ = 10000; /* fractions of a chapter */
static const int noChapter_DocumentBook_    = INT_MIN;

iDefineTypeConstruction(DocumentBook)

static void unfreeze_DocumentBook_(iDocumentBook *d) {
    if (d->flags & drawFrozen_DocumentBookFlag) {
        d->flags &= ~drawFrozen_DocumentBookFlag;
        postCommand_App("window.unfreeze");
    }
}

void init_DocumentBook(iDocumentBook *d) {
    d->owner             = NULL;
    d->flags             = 0;
    d->pendingChapter    = noChapter_DocumentBook_;
    d->progressChapter   = iInvalidPos;
    d->progressAnimEnd   = 0;
    d->pendingChapterPos = -1.0f;
    d->resumeChapterPos  = -1.0f;
    d->savedChapter      = noChapter_DocumentBook_;
    d->savedColumn       = 0;
    init_String(&d->readingBookId);
    d->didTurnPage       = iFalse;
    init_String(&d->offeredEdition);
}

void deinit_DocumentBook(iDocumentBook *d) {
    unfreeze_DocumentBook_(d); /* closed while the resumed chapter was opening */
    deinit_String(&d->offeredEdition);
    deinit_String(&d->readingBookId);
}

void setOwner_DocumentBook(iDocumentBook *d, iDocumentWidget *owner) {
    d->owner = owner;
}

static iDocumentView *view_DocumentBook_(const iDocumentBook *d) {
    return d->owner->view;
}

static iDocumentFetch *fetch_DocumentBook_(const iDocumentBook *d) {
    return d->owner->fetch;
}

static const iGempub *gempub_DocumentBook_(const iDocumentBook *d) {
    return fetch_DocumentBook_(d)->sourceGempub;
}

static size_t currentColumn_DocumentBook_(const iDocumentBook *d) {
    const iDocumentView *view = view_DocumentBook_(d);
    return isPaged_DocumentView(view) ? currentColumn_DocumentView(view) : 0;
}

static float sourcePosition_DocumentBook_(const iDocumentBook *d) {
    const iDocumentView *view = view_DocumentBook_(d);
    return isPaged_DocumentView(view) ? sourcePosition_DocumentView(view) : 0.0f;
}

static const iString *adjacentChapterUrl_DocumentBook_(const iDocumentBook *d, int dir) {
    /* The reading order of a book is the cover, the contents, and the chapters. */
    const iGempub *gempub = gempub_DocumentBook_(d);
    const iString *url    = url_DocumentWidget(d->owner);
    if (!gempub || isRemote_Gempub(gempub) || !dir) {
        return NULL;
    }
    size_t pos;
    if (isCoverPageUrl_Gempub(gempub, url)) {
        pos = 0;
    }
    else if (isIndexPageUrl_Gempub(gempub, url)) {
        pos = 1;
    }
    else {
        const size_t index = navIndex_Gempub(gempub, url);
        if (index == iInvalidPos) {
            return NULL;
        }
        pos = index + 2;
    }
    if ((dir < 0 && pos == 0) || (dir > 0 && pos + 1 >= navSize_Gempub(gempub) + 2)) {
        return NULL;
    }
    pos = dir < 0 ? pos - 1 : pos + 1;
    return pos == 0   ? coverPageUrl_Gempub(gempub)
           : pos == 1 ? indexPageUrl_Gempub(gempub)
                      : navLinkUrl_Gempub(gempub, pos - 2);
}

static size_t chapter_DocumentBook_(const iDocumentBook *d, size_t *numChapters_out) {
    /* Index of the shown chapter in the book's navigation links. */
    const iGempub *gempub = gempub_DocumentBook_(d);
    int            numChapters;
    const int      chapter = chapter_GmDocument(view_DocumentBook_(d)->doc, &numChapters);
    if (!gempub || isRemote_Gempub(gempub) || chapter <= 0) {
        return iInvalidPos;
    }
    *numChapters_out = numChapters;
    return chapter - 1;
}

static iBool position_DocumentBook_(const iDocumentBook *d, int *chapter_out,
                                    size_t *numChapters_out) {
    /* The shown chapter, or the cover or contents page (see iBookChapter). */
    const size_t chapter = chapter_DocumentBook_(d, numChapters_out);
    if (chapter != iInvalidPos) {
        *chapter_out = (int) chapter;
        return iTrue;
    }
    const iGempub *gempub = gempub_DocumentBook_(d);
    const iString *url    = url_DocumentWidget(d->owner);
    if (!gempub || isRemote_Gempub(gempub)) {
        return iFalse;
    }
    if (isCoverPageUrl_Gempub(gempub, url)) {
        *chapter_out = cover_BookChapter;
    }
    else if (isIndexPageUrl_Gempub(gempub, url)) {
        *chapter_out = contents_BookChapter;
    }
    else {
        return iFalse;
    }
    *numChapters_out = navSize_Gempub(gempub);
    return iTrue;
}

static const iString *chapterUrl_DocumentBook_(const iDocumentBook *d, int chapter) {
    const iGempub *gempub = gempub_DocumentBook_(d);
    return chapter == cover_BookChapter      ? coverPageUrl_Gempub(gempub)
           : chapter == contents_BookChapter ? indexPageUrl_Gempub(gempub)
                                             : navLinkUrl_Gempub(gempub, chapter);
}

static int progressThumbSize_DocumentBook_(const iDocumentBook *d) {
    /* A spread may extend past the last column of the chapter. */
    const iDocumentView *view    = view_DocumentBook_(d);
    const float          numCols = numColumns_GmDocument(view->doc);
    const float          visible = iMin((float) numColumns_DocumentView(view),
                                        numCols - columnPosition_DocumentView(view));
    return (int) (progressUnit_DocumentBook_ * iMax(0.0f, visible) / numCols);
}

static size_t columnEndingAt_DocumentBook_(const iDocumentBook *d, float pos) {
    /* The spread that ends at a position in the document (0...1). */
    const iDocumentView *view    = view_DocumentBook_(d);
    const int            numCols = (int) numColumns_GmDocument(view->doc);
    const int            column  = (int) ceilf(pos * numCols) - numColumns_DocumentView(view);
    return (size_t) iClamp(column, 0, numCols - 1);
}

iBool openAdjacentChapter_DocumentBook(iDocumentBook *d, int dir, iBool isGesture) {
    /* Turning past the first or last page continues in the previous or next chapter. The
       current view slides away like in a history swipe. A gesture moves the views itself. */
    const iString *url = adjacentChapterUrl_DocumentBook_(d, dir);
    if (!url) {
        return iFalse;
    }
    iDocumentSwipe *swipe = d->owner->swipe;
    iDocumentView  *view  = view_DocumentBook_(d);
    reset_DocumentSwipe(swipe);
    settlePageTurn_DocumentView(view); /* drawn as it is when sliding away */
    if (prefs_App()->pageTurn == curl_PageTurnAnimation && isPaged_DocumentView(view)) {
        swipe->flags |= pageCurl_DocumentSwipeFlag;
    }
    setViewSwipedAway_DocumentSwipe(swipe, iTrue);
    if (dir < 0) {
        setOverlay_DocumentSwipe(swipe, iTrue);
    }
    else {
        setOffset_DocumentSwipe(swipe, width_Widget(d->owner));
    }
    setOutgoingView_DocumentSwipe(swipe, view);
    swipe->flags |= begun_DocumentSwipeFlag; /* until the chapter has been loaded */
    iChangeFlags(d->flags, showLastPage_DocumentBookFlag, dir < 0);
    iChangeFlags(d->flags, animateChapterSwipe_DocumentBookFlag, !isGesture);
    d->flags |= readingOn_DocumentBookFlag;
    postCommandf_Root(as_Widget(d->owner)->root, "open doc:%p url:%s", d->owner, cstr_String(url));
    return iTrue;
}

int chapterSwipeDir_DocumentBook(const iDocumentBook *d, const char *cmd) {
    /* A swipe past the first or last page goes to the adjacent chapter, if there is one. */
    const iDocumentView *view   = view_DocumentBook_(d);
    const int            side   = argLabel_Command(cmd, "side");
    const size_t         column = currentColumn_DocumentView(view);
    const int dir = side == 1 && column == 0 ? -1
                    : side == 2 && column + numColumns_DocumentView(view) >=
                                       numColumns_GmDocument(view->doc)
                        ? +1
                        : 0;
    return dir && adjacentChapterUrl_DocumentBook_(d, dir) ? dir : 0;
}

iBool isReadingOn_DocumentBook(const iDocumentBook *d) {
    return (d->flags & readingOn_DocumentBookFlag) != 0;
}

void resume_DocumentBook(iDocumentBook *d, int chapter, float chapterPos) {
    d->pendingChapter    = chapter;
    d->pendingChapterPos = -1.0f;
    d->resumeChapterPos  = chapterPos;
}

void freezeForResume_DocumentBook(iDocumentBook *d, const iString *url) {
    /* A shelf book's cover is not shown if reading continues in another chapter. */
    if (d->flags & readingOn_DocumentBookFlag || isReader_DocumentWidget(d->owner) ||
        !isLocalFileScheme_Rangecc(urlScheme_String(url))) {
        return;
    }
    const iBook *book = findByPath_Books(collect_String(localFilePathFromUrl_String(url)));
    if (!book || (book->chapter != contents_BookChapter &&
                  (book->chapter < 0 || book->chapter >= book->numChapters))) {
        return;
    }
    iWindow *win = window_Widget(d->owner);
    if (isDrawFrozen_Window(win)) {
        return; /* someone else unfreezes */
    }
    setFreezeDraw_MainWindow(as_MainWindow(win), iTrue);
    d->flags |= drawFrozen_DocumentBookFlag;
}

void resetSavedPosition_DocumentBook(iDocumentBook *d) {
    d->savedChapter = noChapter_DocumentBook_;
}

void applyResume_DocumentBook(iDocumentBook *d) {
    if (d->resumeChapterPos < 0.0f) {
        unfreeze_DocumentBook_(d);
        return;
    }
    iDocumentView *view = view_DocumentBook_(d);
    int            chapter;
    size_t         numChapters;
    const iBool    isBook = position_DocumentBook_(d, &chapter, &numChapters);
    if (isBook && chapter == cover_BookChapter && d->pendingChapter != cover_BookChapter) {
        return; /* the chapter is opened next */
    }
    if (isBook && chapter == d->pendingChapter) {
        if (isPaged_DocumentView(view)) {
            showSourcePosition_DocumentView(view, d->resumeChapterPos);
        }
        else {
            init_Anim(&view->scrollY.pos, d->resumeChapterPos * pageHeight_DocumentView(view));
        }
    }
    d->resumeChapterPos = -1.0f;
    unfreeze_DocumentBook_(d);
}

static void savePosition_DocumentBook_(iDocumentBook *d) {
    /* Positions are saved when chapters are opened and pages are turned. */
    int    chapter;
    size_t numChapters;
    if (!position_DocumentBook_(d, &chapter, &numChapters) ||
        d->savedChapter == noChapter_DocumentBook_ || d->resumeChapterPos >= 0.0f) {
        return; /* no baseline yet */
    }
    const size_t column = currentColumn_DocumentBook_(d);
    if (chapter == d->savedChapter && column == d->savedColumn) {
        return;
    }
    d->savedChapter = chapter;
    d->savedColumn  = column;
    d->didTurnPage  = iTrue;
    setPosition_Books(
        gempub_DocumentBook_(d), chapter, (int) numChapters, sourcePosition_DocumentBook_(d));
}

void beginPosition_DocumentBook(iDocumentBook *d) {
    /* Restored tabs and layout changes keep the saved position, but another chapter replaces it. */
    int    chapter;
    size_t numChapters;
    d->savedChapter = noChapter_DocumentBook_;
    d->savedColumn  = currentColumn_DocumentBook_(d);
    if (!position_DocumentBook_(d, &chapter, &numChapters) || d->resumeChapterPos >= 0.0f) {
        return; /* the resumed chapter is opened next */
    }
    d->savedChapter       = chapter;
    const iGempub *gempub = gempub_DocumentBook_(d);
    const iString *bookId = id_Books(gempub);
    if (!equal_String(&d->readingBookId, bookId)) {
        /* The synced position may be more recent. */
        set_String(&d->readingBookId, bookId);
        d->didTurnPage = iFalse;
        beginReading_Books(gempub);
        notify_App("sync.recent");
    }
    const iBook *book = find_Books(bookId);
    if (!book || book->chapter != chapter) {
        setPosition_Books(gempub, chapter, (int) numChapters, sourcePosition_DocumentBook_(d));
    }
}

void didFinishRequest_DocumentBook(iDocumentBook *d) {
    iDocumentView *view = view_DocumentBook_(d);
    d->flags &= ~readingOn_DocumentBookFlag;
    if (d->pendingChapterPos >= 0.0f) {
        /* Arrived via the progress bar. */
        int    chapter;
        size_t numChapters;
        if (position_DocumentBook_(d, &chapter, &numChapters) && chapter == d->pendingChapter) {
            if (isPaged_DocumentView(view)) {
                showColumn_DocumentView(
                    view, columnEndingAt_DocumentBook_(d, d->pendingChapterPos), iFalse);
            }
            d->pendingChapterPos = -1.0f;
        }
    }
    applyResume_DocumentBook(d);
    if (d->flags & showLastPage_DocumentBookFlag) {
        /* Going backward from the next chapter. */
        d->flags &= ~showLastPage_DocumentBookFlag;
        if (isPaged_DocumentView(view)) {
            showColumn_DocumentView(view, numColumns_GmDocument(view->doc) - 1, iFalse);
        }
    }
    beginPosition_DocumentBook(d);
    if (isPaged_DocumentView(view)) {
        pagesMoved_DocumentWidget(d->owner);
    }
    if (d->flags & animateChapterSwipe_DocumentBookFlag) {
        /* The new chapter has arrived, so the views can slide. */
        iDocumentSwipe *swipe = d->owner->swipe;
        d->flags &= ~animateChapterSwipe_DocumentBookFlag;
        swipe->flags &= ~begun_DocumentSwipeFlag;
        const int to = isSwipingBack_DocumentSwipe(swipe) ? width_Widget(d->owner) : 0;
        if (!prefs_App()->uiAnimations ||
            (isPaged_DocumentView(view) && prefs_App()->pageTurn == none_PageTurnAnimation)) {
            setValue_Anim(&swipe->offset, to, 0);
        }
        else if (swipe->flags & pageCurl_DocumentSwipeFlag) {
            setValueEased_Anim(&swipe->offset,
                               to,
                               beginPageTurn_DocumentView(view, pageTurnDuration_DocumentView(view)));
        }
        else {
            setFlags_Anim(&swipe->offset, easeOut_AnimFlag, iTrue);
            setValue_Anim(&swipe->offset, to, beginPageTurn_DocumentView(view, 250));
        }
        animate_DocumentWidget(d->owner);
    }
}

void jumpToPosition_DocumentBook(iDocumentBook *d, int chapter, float pos) {
    int    current;
    size_t numChapters;
    if (!position_DocumentBook_(d, &current, &numChapters)) {
        return;
    }
    if (current == chapter) {
        resume_DocumentBook(d, chapter, pos);
        applyResume_DocumentBook(d);
        d->savedColumn = currentColumn_DocumentBook_(d);
        if (isPaged_DocumentView(view_DocumentBook_(d))) {
            pagesMoved_DocumentWidget(d->owner);
        }
        refresh_Widget(d->owner);
    }
    else if (chapter >= contents_BookChapter && chapter < (int) numChapters) {
        resume_DocumentBook(d, chapter, pos);
        d->flags |= readingOn_DocumentBookFlag;
        postCommandf_Root(as_Widget(d->owner)->root,
                          "open doc:%p url:%s",
                          d->owner,
                          cstr_String(chapterUrl_DocumentBook_(d, chapter)));
    }
}

void checkSyncedPosition_DocumentBook(iDocumentBook *d) {
    int    chapter;
    size_t numChapters;
    if (!position_DocumentBook_(d, &chapter, &numChapters) || d->resumeChapterPos >= 0.0f) {
        return;
    }
    const iGempub *gempub = gempub_DocumentBook_(d);
    if (isOutdated_Books(gempub)) {
        return; /* the position is in the newer edition */
    }
    const iBook *book = find_Books(id_Books(gempub));
    if (!book || book->chapter < contents_BookChapter || book->chapter >= (int) numChapters) {
        return;
    }
    const float pos = sourcePosition_DocumentBook_(d);
    if (book->chapter == chapter && fabsf(book->chapterPos - pos) < 0.001f) {
        return;
    }
    if (!d->didTurnPage) {
        jumpToPosition_DocumentBook(d, book->chapter, book->chapterPos);
        return;
    }
    if (document_Root(as_Widget(d->owner)->root) != d->owner) {
        return; /* only the visible tab asks */
    }
    makeQuestion_Widget(
        uiHeading_ColorEscape "${heading.book.synced}",
        book->chapter >= 0 ? format_Lang("${dlg.book.synced}", book->chapter + 1)
                           : "${dlg.book.synced.frontmatter}",
        (iMenuItem[]){ { "${cancel}" },
                       { uiTextAction_ColorEscape "${dlg.book.synced.jump}",
                         SDLK_RETURN,
                         0,
                         format_CStr("book.jump doc:%p chapter:%d pos:%f",
                                     d->owner, book->chapter, book->chapterPos) } },
        2);
}

void checkEdition_DocumentBook(iDocumentBook *d) {
    const iGempub *gempub = gempub_DocumentBook_(d);
    if (!gempub || isRemote_Gempub(gempub) || isRequestOngoing_DocumentWidget(d->owner) ||
        !isOutdated_Books(gempub)) {
        return;
    }
    const iBook *book = find_Books(id_Books(gempub));
    if (!book || !isShelved_Book(book) || !isDownloadable_Book(book)) {
        return;
    }
    iWidget    *w   = as_Widget(d->owner);
    const char *cmd = format_CStr(
        "open doc:%p overwritebook:1 url:%s", d->owner, cstr_String(&book->url));
    if (!d->didTurnPage) {
        postCommand_Root(w->root, cmd);
        return;
    }
    if (document_Root(w->root) != d->owner || equal_String(&d->offeredEdition, &book->edition)) {
        return; /* the visible tab asks once */
    }
    set_String(&d->offeredEdition, &book->edition);
    makeQuestion_Widget(
        uiHeading_ColorEscape "${heading.book.newedition}",
        format_Lang("${dlg.book.newedition}", cstr_String(&book->title)),
        (iMenuItem[]){ { "${cancel}" },
                       { uiTextAction_ColorEscape "${dlg.book.newedition.update}",
                         SDLK_RETURN,
                         0,
                         cmd } },
        2);
}

iBool moveProgress_DocumentBook(iDocumentBook *d, int thumb) {
    /* The progress bar of a book's chapter spans the whole book, so it may be dragged to
       another chapter. The opened chapter shows the pages at the dragged position. */
    size_t       numChapters;
    const size_t chapter = chapter_DocumentBook_(d, &numChapters);
    if (chapter == iInvalidPos || numChapters == 0) {
        return iFalse;
    }
    const int unit   = progressUnit_DocumentBook_;
    const int end    = thumb + progressThumbSize_DocumentBook_(d); /* follows the mouse */
    const int target = iClamp((end + unit - 1) / unit - 1, 0, (int) numChapters - 1);
    const float pos  = iClamp((float) (end - target * unit) / unit, 0.0f, 1.0f);
    const iBool isPending = d->pendingChapterPos >= 0.0f;
    if (isPending || (size_t) target != chapter) {
        if (target != (isPending ? d->pendingChapter : (int) chapter)) {
            d->pendingChapter = target;
            d->flags |= readingOn_DocumentBookFlag;
            postCommandf_Root(as_Widget(d->owner)->root,
                              "open doc:%p url:%s",
                              d->owner,
                              cstr_String(navLinkUrl_Gempub(gempub_DocumentBook_(d), target)));
        }
        d->pendingChapterPos = pos;
        return iTrue;
    }
    showColumn_DocumentView(view_DocumentBook_(d), columnEndingAt_DocumentBook_(d, pos), iFalse);
    return iTrue;
}

iBool updateProgress_DocumentBook(iDocumentBook *d) {
    size_t       numChapters;
    const size_t chapter = chapter_DocumentBook_(d, &numChapters);
    if (chapter == iInvalidPos) {
        d->progressChapter = iInvalidPos;
        savePosition_DocumentBook_(d); /* the contents may have multiple pages */
        return iFalse;
    }
    /* Each chapter is an equal part of a book's progress. The bar moves to another
       chapter like the pages slide in, unless it is being dragged. */
    const iDocumentView *view   = view_DocumentBook_(d);
    iScrollWidget       *scroll = scrollBar_DocumentWidget(d->owner);
    const int            unit   = progressUnit_DocumentBook_;
    const uint32_t       now    = SDL_GetTicks();
    if (d->progressChapter != iInvalidPos && chapter != d->progressChapter &&
        ~flags_Widget(scroll) & pressed_WidgetFlag) {
        d->progressAnimEnd = now + pageTurnDuration_DocumentView(view);
    }
    d->progressChapter = chapter;
    setRange_ScrollWidget(scroll, (iRangei){ 0, (int) numChapters * unit });
    /* The pages of the new chapter may still be turned (e.g., to the last one). */
    setThumbAnimated_ScrollWidget(scroll,
                                  (int) chapter * unit +
                                      (int) (columnPosition_DocumentView(view) * unit /
                                             numColumns_GmDocument(view->doc)),
                                  progressThumbSize_DocumentBook_(d),
                                  d->progressAnimEnd > now ? d->progressAnimEnd - now : 0);
    savePosition_DocumentBook_(d);
    return iTrue;
}

static iBool isEqualToFileContents_(const iString *path, const iBlock *data) {
    if (fileSize_FileInfo(path) != size_Block(data)) {
        return iFalse;
    }
    iFile *f = new_File(path);
    if (!open_File(f, readOnly_FileMode)) {
        iRelease(f);
        return iFalse;
    }
    const char *expected = constData_Block(data);
    const size_t total   = size_Block(data);
    size_t       pos     = 0;
    char         buf[0x10000];
    while (pos < total) {
        const size_t num = readData_File(f, iMin(sizeof(buf), total - pos), buf);
        if (num == 0 || memcmp(buf, expected + pos, num)) {
            break;
        }
        pos += num;
    }
    iRelease(f);
    return pos == total;
}

static iBool isNewEdition_(const iGempub *gempub, const iString *existingPath) {
    /* Revisions don't change the edition, but republishing does. Retitling is handled
       by add_Books(). */
    static const enum iGempubProperty dates[] = { published_GempubProperty,
                                                  publishDate_GempubProperty };
    iGempub *existing = new_Gempub();
    iBool    isNew    = iFalse;
    if (openFile_Gempub(existing, existingPath)) {
        iForIndices(i, dates) {
            if (!equal_String(property_Gempub(gempub, dates[i]),
                              property_Gempub(existing, dates[i]))) {
                isNew = iTrue;
                break;
            }
        }
    }
    delete_Gempub(existing);
    return isNew;
}

iDocumentWidget *findTab_DocumentBook(const iString *bookId, const iString *path) {
    const iString *url = path ? collect_String(makeFileUrl_String(path)) : NULL;
    iConstForEach(ObjectList, i, iClob(listDocuments_App(NULL))) {
        const iGempub *gempub = gempub_DocumentWidget(i.object);
        if (gempub && !isRemote_Gempub(gempub) &&
            ((url && equal_String(coverPageUrl_Gempub(gempub), url)) ||
             (bookId && equal_String(id_Books(gempub), bookId)))) {
            return (iDocumentWidget *) i.object;
        }
    }
    return NULL;
}

void tidyShelf_DocumentBook(void) {
    iBool isChanged = iFalse;
    iConstForEach(StringArray, i, listObsolete_Books()) {
        /* An open book is updated or removed later. */
        iDocumentWidget *tab = findTab_DocumentBook(i.value, NULL);
        if (tab) {
            checkEdition_DocumentBook(tab->book);
        }
        else {
            removeDownload_Books(i.value);
            isChanged = iTrue;
        }
    }
    prune_Books();
    if (isChanged) {
        postCommand_App("books.changed");
    }
}

void openFromShelf_DocumentBook(iDocumentBook *d, const iString *path, iBool isReplaced) {
    /* Only one copy of a book is open, so reading positions don't conflict. */
    iDocumentWidget *owner  = d->owner;
    const iGempub   *gempub = gempub_DocumentBook_(d);
    const iString   *url    = collect_String(makeFileUrl_String(path));
    iDocumentWidget *tab    = findTab_DocumentBook(gempub ? id_Books(gempub) : NULL, path);
    if (tab && tab != owner) {
        postCommandf_Root(as_Widget(tab)->root, "tabs.switch page:%p", tab);
        if (isReplaced) {
            postCommandf_Root(as_Widget(tab)->root, "!open doc:%p url:%s", tab, cstr_String(url));
        }
        return;
    }
    /* The remote copy is not kept in history. */
    postCommandf_Root(as_Widget(owner)->root,
                      "!open doc:%p%s url:%s",
                      owner,
                      gempub && isRemote_Gempub(gempub) && !isReader_DocumentWidget(owner)
                          ? " redirect:1"
                          : "",
                      cstr_String(url));
}

void saveToShelf_DocumentBook(iDocumentBook *d, iBool overwrite) {
    /* Books are copied to the shelf and pages become single-chapter books. */
    iDocumentWidget      *owner     = d->owner;
    iWidget              *w         = as_Widget(owner);
    const iDocumentFetch *fetch     = fetch_DocumentBook_(d);
    const iGmDocument    *doc       = view_DocumentBook_(d)->doc;
    const iGempub        *gempub    = fetch->sourceGempub;
    const iString        *sourceUrl = url_DocumentWidget(owner);
    const iString        *id;
    const iString        *fileName;
    const iString        *title;
    iBlock               *data;
    const iBool           isDownload = overwrite; /* requested via the shelf */
    if (gempub) {
        id       = id_Books(gempub);
        iString *bookTitle = collect_String(copy_String(property_Gempub(gempub, title_GempubProperty)));
        translate_Lang(bookTitle); /* may be a placeholder */
        title    = bookTitle;
        fileName = title;
        if (isRemote_Gempub(gempub)) {
            data = copy_Block(&fetch->sourceContent);
        }
        else {
            const iString *path =
                collect_String(localFilePathFromUrl_String(coverPageUrl_Gempub(gempub)));
            if (isOnShelf_Books(path)) {
                postCommandf_Root(w->root, "sidebar.mode arg:%d show:1", gempub_SidebarMode);
                return;
            }
            iFile *f = iClob(new_File(path));
            if (!open_File(f, readOnly_FileMode)) {
                return;
            }
            data = readAll_File(f);
        }
    }
    else if (format_GmDocument(doc) == gemini_SourceFormat &&
             !cmp_String(&fetch->sourceMime, "text/gemini")) {
        /* Using the title of a book not yet downloaded keeps its ID. */
        const iBook *pending = sourceUrl ? findBySource_Books(sourceUrl, iFalse) : NULL;
        title = pending && !isEmpty_String(&pending->title)
                    ? collect_String(copy_String(&pending->title))
                    : title_GmDocument(doc);
        if (isEmpty_String(title)) {
            title = bookmarkTitle_DocumentWidget(owner);
        }
        iRangecc authorRange = urlUser_String(sourceUrl);
        if (isEmpty_Range(&authorRange)) {
            authorRange = urlHost_String(sourceUrl);
        }
        const iString *author = collectNewRange_String(authorRange);
        id       = collect_String(makeId_Books(title, author));
        fileName = title;
        data     = newPageArchive_Gempub(sourceUrl, title, author, source_GmDocument(doc));
        overwrite = iTrue; /* saving the page again updates the book */
    }
    else {
        makeSimpleMessage_Widget(uiTextCaution_ColorEscape "${heading.book.save}",
                                 "${dlg.book.save.unsupported}");
        return;
    }
    const iBook *existing = find_Books(id);
    if ((overwrite || gempub) && (!existing || !path_Book(existing)) && sourceUrl) {
        /* A book from the same source is replaced even if its title has changed. */
        const iBook *previous = findBySource_Books(sourceUrl, iTrue);
        if (previous) {
            existing = previous;
        }
    }
    const iString *existingPath = existing ? path_Book(existing) : NULL;
    if (existingPath && (gempub || !overwrite) && isEqualToFileContents_(existingPath, data)) {
        /* Nothing new to save. */
        delete_Block(data);
        if (sourceUrl) {
            setSource_Books(id, sourceUrl);
        }
        openFromShelf_DocumentBook(d, existingPath, iFalse);
        return;
    }
    if (existingPath && !overwrite) {
        makeQuestion_Widget(
            uiHeading_ColorEscape "${heading.book.exists}",
            format_Lang("${dlg.book.exists}",
                        cstr_String(isEmpty_String(&existing->title) ? title : &existing->title)),
            (iMenuItem[]){ { "${cancel}",
                             SDLK_ESCAPE,
                             0,
                             format_CStr("document.openbook doc:%p path:%s",
                                         owner,
                                         cstr_String(existingPath)) },
                           { uiTextCaution_ColorEscape "${dlg.book.overwrite}",
                             0,
                             0,
                             format_CStr("document.savebook overwrite:1 doc:%p", owner) } },
            2);
        delete_Block(data);
        return;
    }
    const iBook   *known        = find_Books(id);
    const iBool    isNewEdition = gempub && existingPath && isNewEdition_(gempub, existingPath) &&
                                  /* the synced position may be in this edition already */
                                  (!known || !equal_String(&known->edition, edition_Books(gempub)));
    const iString *savedPath    = add_Books(data, fileName, existingPath, sourceUrl);
    delete_Block(data);
    if (!savedPath) {
        makeSimpleMessage_Widget(uiTextCaution_ColorEscape "${heading.save.error}",
                                 "${dlg.book.save.error}");
        return;
    }
    if (isNewEdition) {
        resetPosition_Books(id);
    }
    postCommand_App("books.changed");
    if (gempub || isDownload) {
        openFromShelf_DocumentBook(d, savedPath, existingPath != NULL);
    }
    else {
        const iMenuItem items[2] = {
            { "${dlg.book.open}", 0, 0,
              format_CStr("!open doc:%p url:%s", owner, cstrCollect_String(makeFileUrl_String(savedPath))) },
            { "${dlg.message.ok}", 0, 0, "message.ok" },
        };
        makeMessage_Widget("${heading.book.saved}",
                           format_Lang("${dlg.book.saved}", cstr_String(title)),
                           items,
                           iElemCount(items));
    }
}
