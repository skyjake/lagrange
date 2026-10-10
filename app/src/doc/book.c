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
#include "ui/documentwidget.h"
#include "ui/root.h"
#include "ui/scrollwidget.h"
#include "ui/util.h"
#include "ui/widget.h"

#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <SDL3/SDL_timer.h>
#include <math.h>

static const int progressUnit_DocumentBook_ = 10000; /* fractions of a chapter */

iDefineTypeConstruction(DocumentBook)

void init_DocumentBook(iDocumentBook *d) {
    d->owner             = NULL;
    d->flags             = 0;
    d->pendingChapter    = iInvalidPos;
    d->progressChapter   = iInvalidPos;
    d->progressAnimEnd   = 0;
    d->pendingChapterPos = -1.0f;
    d->resumeChapterPos  = -1.0f;
    d->savedChapter      = iInvalidPos;
    d->savedColumn       = 0;
    init_String(&d->readingBookId);
    d->didTurnPage       = iFalse;
}

void deinit_DocumentBook(iDocumentBook *d) {
    deinit_String(&d->readingBookId);
}

void setOwner_DocumentBook(iDocumentBook *d, iDocumentWidget *owner) {
    d->owner = owner;
}

static iDocumentView *view_DocumentBook_(const iDocumentBook *d) {
    return view_DocumentWidget(d->owner);
}

static iDocumentFetch *fetch_DocumentBook_(const iDocumentBook *d) {
    return fetch_DocumentWidget(d->owner);
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
    iDocumentSwipe *swipe = swipe_DocumentWidget(d->owner);
    iDocumentView  *view  = view_DocumentBook_(d);
    reset_DocumentSwipe(swipe);
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
    postCommandf_Root(as_Widget(d->owner)->root, "open url:%s", cstr_String(url));
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

void resume_DocumentBook(iDocumentBook *d, size_t chapter, float chapterPos) {
    d->pendingChapter    = chapter;
    d->pendingChapterPos = -1.0f;
    d->resumeChapterPos  = chapterPos;
}

void resetSavedPosition_DocumentBook(iDocumentBook *d) {
    d->savedChapter = iInvalidPos;
}

void applyResume_DocumentBook(iDocumentBook *d) {
    if (d->resumeChapterPos < 0.0f) {
        return;
    }
    const iGempub *gempub = gempub_DocumentBook_(d);
    if (gempub && isCoverPageUrl_Gempub(gempub, url_DocumentWidget(d->owner))) {
        return; /* the chapter is opened next */
    }
    iDocumentView *view = view_DocumentBook_(d);
    size_t numChapters;
    if (chapter_DocumentBook_(d, &numChapters) == d->pendingChapter) {
        if (isPaged_DocumentView(view)) {
            showSourcePosition_DocumentView(view, d->resumeChapterPos);
        }
        else {
            init_Anim(&view->scrollY.pos, d->resumeChapterPos * pageHeight_DocumentView(view));
        }
    }
    d->resumeChapterPos = -1.0f;
}

static void savePosition_DocumentBook_(iDocumentBook *d) {
    /* Positions are saved when chapters are opened and pages are turned. */
    size_t       numChapters;
    const size_t chapter = chapter_DocumentBook_(d, &numChapters);
    if (chapter == iInvalidPos || d->savedChapter == iInvalidPos || d->resumeChapterPos >= 0.0f) {
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
        gempub_DocumentBook_(d), (int) chapter, (int) numChapters, sourcePosition_DocumentBook_(d));
}

void beginPosition_DocumentBook(iDocumentBook *d) {
    /* Restored tabs and layout changes keep the saved position, but another chapter replaces it. */
    size_t       numChapters;
    const size_t chapter = chapter_DocumentBook_(d, &numChapters);
    d->savedChapter      = chapter;
    d->savedColumn       = currentColumn_DocumentBook_(d);
    if (chapter != iInvalidPos) {
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
        if (!book || book->chapter != (int) chapter) {
            setPosition_Books(
                gempub, (int) chapter, (int) numChapters, sourcePosition_DocumentBook_(d));
        }
    }
}

void didFinishRequest_DocumentBook(iDocumentBook *d) {
    iDocumentView *view = view_DocumentBook_(d);
    d->flags &= ~readingOn_DocumentBookFlag;
    if (d->pendingChapterPos >= 0.0f) {
        /* Arrived via the progress bar. */
        size_t numChapters;
        if (chapter_DocumentBook_(d, &numChapters) == d->pendingChapter) {
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
        iDocumentSwipe *swipe = swipe_DocumentWidget(d->owner);
        d->flags &= ~animateChapterSwipe_DocumentBookFlag;
        swipe->flags &= ~begun_DocumentSwipeFlag;
        const int to = isSwipingBack_DocumentSwipe(swipe) ? width_Widget(d->owner) : 0;
        if (!prefs_App()->uiAnimations) {
            setValue_Anim(&swipe->offset, to, 0);
        }
        else if (swipe->flags & pageCurl_DocumentSwipeFlag) {
            setValueEased_Anim(&swipe->offset, to, pageTurnDuration_DocumentView(view));
        }
        else {
            setFlags_Anim(&swipe->offset, easeOut_AnimFlag, iTrue);
            setValue_Anim(&swipe->offset, to, 250);
        }
        animate_DocumentWidget(d->owner);
    }
}

void jumpToPosition_DocumentBook(iDocumentBook *d, size_t chapter, float pos) {
    size_t numChapters;
    if (chapter_DocumentBook_(d, &numChapters) == chapter) {
        resume_DocumentBook(d, chapter, pos);
        applyResume_DocumentBook(d);
        d->savedColumn = currentColumn_DocumentBook_(d);
        if (isPaged_DocumentView(view_DocumentBook_(d))) {
            pagesMoved_DocumentWidget(d->owner);
        }
        refresh_Widget(d->owner);
    }
    else if (chapter < numChapters) {
        resume_DocumentBook(d, chapter, pos);
        d->flags |= readingOn_DocumentBookFlag;
        postCommandf_Root(as_Widget(d->owner)->root,
                          "open url:%s",
                          cstr_String(navLinkUrl_Gempub(gempub_DocumentBook_(d), chapter)));
    }
}

void checkSyncedPosition_DocumentBook(iDocumentBook *d) {
    size_t       numChapters;
    const size_t chapter = chapter_DocumentBook_(d, &numChapters);
    if (chapter == iInvalidPos || d->resumeChapterPos >= 0.0f) {
        return;
    }
    const iBook *book = find_Books(id_Books(gempub_DocumentBook_(d)));
    if (!book || book->chapter < 0 || (size_t) book->chapter >= numChapters) {
        return;
    }
    const float pos = sourcePosition_DocumentBook_(d);
    if ((size_t) book->chapter == chapter && fabsf(book->chapterPos - pos) < 0.001f) {
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
        format_Lang("${dlg.book.synced}", book->chapter + 1),
        (iMenuItem[]){ { "${dlg.book.synced.stay}", SDLK_ESCAPE, 0, NULL },
                       { uiTextAction_ColorEscape "${dlg.book.synced.jump}",
                         SDLK_RETURN,
                         0,
                         format_CStr("book.jump ptr:%p chapter:%d pos:%f",
                                     d->owner, book->chapter, book->chapterPos) } },
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
        if ((size_t) target != (isPending ? d->pendingChapter : chapter)) {
            d->pendingChapter = target;
            d->flags |= readingOn_DocumentBookFlag;
            postCommandf_Root(as_Widget(d->owner)->root,
                              "open url:%s",
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
        return iFalse;
    }
    /* Each chapter is an equal part of a book's progress. The bar moves to another
       chapter like the pages slide in, unless it is being dragged. */
    const iDocumentView *view   = view_DocumentBook_(d);
    iScrollWidget       *scroll = scrollBar_DocumentWidget(d->owner);
    const int            unit   = progressUnit_DocumentBook_;
    const uint32_t       now    = SDL_GetTicks();
    if (d->progressChapter != iInvalidPos && chapter != d->progressChapter &&
        ~flags_Widget(as_Widget(scroll)) & pressed_WidgetFlag) {
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
        const iString *author = collectNewRange_String(urlHost_String(sourceUrl));
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
    if (overwrite && (!existing || !path_Book(existing)) && sourceUrl) {
        /* Redownloading replaces the book even if its title has changed. */
        const iBook *previous = findBySource_Books(sourceUrl, iTrue);
        if (previous) {
            existing = previous;
        }
    }
    const iString *existingPath = existing ? path_Book(existing) : NULL;
    if (existingPath && !overwrite && isEqualToFileContents_(existingPath, data)) {
        delete_Block(data);
        if (sourceUrl) {
            setSource_Books(id, sourceUrl);
        }
        postCommandf_Root(w->root,
                          "!open doc:%p url:%s",
                          owner,
                          cstrCollect_String(makeFileUrl_String(existingPath)));
        return;
    }
    if (existingPath && !overwrite) {
        makeQuestion_Widget(
            uiHeading_ColorEscape "${heading.book.exists}",
            format_Lang("${dlg.book.exists}", cstr_String(title)),
            (iMenuItem[]){ { "${cancel}",
                             SDLK_ESCAPE,
                             0,
                             format_CStr("!open doc:%p url:%s",
                                         owner,
                                         cstrCollect_String(makeFileUrl_String(existingPath))) },
                           { uiTextCaution_ColorEscape "${dlg.book.overwrite}",
                             0,
                             0,
                             format_CStr("document.savebook overwrite:1 doc:%p", owner) } },
            2);
        delete_Block(data);
        return;
    }
    const iString *savedPath = add_Books(data, fileName, existingPath, sourceUrl);
    delete_Block(data);
    if (!savedPath) {
        makeSimpleMessage_Widget(uiTextCaution_ColorEscape "${heading.save.error}",
                                 "${dlg.book.save.error}");
        return;
    }
    postCommand_App("books.changed");
    if (gempub || isDownload) {
        postCommandf_Root(w->root,
                          "!open doc:%p url:%s",
                          owner,
                          cstrCollect_String(makeFileUrl_String(savedPath)));
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
