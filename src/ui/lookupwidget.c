/* Copyright 2020 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "lookupwidget.h"

#include "app.h"
#include "bookmarks.h"
#include "command.h"
#include "documentwidget.h"
#include "feeds.h"
#include "gmcerts.h"
#include "gmutil.h"
#include "history.h"
#include "inputwidget.h"
#include "listwidget.h"
#include "lang.h"
#include "lookup.h"
#include "util.h"
#include "visited.h"

#include <the_Foundation/mutex.h>
#include <the_Foundation/thread.h>
#include <the_Foundation/regexp.h>

iDeclareType(LookupJob)

struct Impl_LookupJob {
    iRegExp *term;
    iTime now;
    iObjectList *docs;
    iPtrArray results;
};

static void init_LookupJob(iLookupJob *d) {
    d->term = NULL;
    initCurrent_Time(&d->now);
    d->docs = NULL;
    init_PtrArray(&d->results);
}

static void deinit_LookupJob(iLookupJob *d) {
    iForEach(PtrArray, i, &d->results) {
        delete_LookupResult(i.ptr);
    }
    deinit_PtrArray(&d->results);
    iRelease(d->docs);
    iRelease(d->term);
}

iDefineTypeConstruction(LookupJob)

/*----------------------------------------------------------------------------------------------*/

iDeclareType(LookupItem)
typedef iListItemClass iLookupItemClass;

struct Impl_LookupItem {
    iListItem listItem;
    iLookupResult *result;
    int font;
    int fg;
    iString icon;
    iString text;
    iString command;
};

static void init_LookupItem(iLookupItem *d, const iLookupResult *res) {
    init_ListItem(&d->listItem);
    d->result = res ? copy_LookupResult(res) : NULL;
    d->font = uiContent_FontId;
    d->fg = uiText_ColorId;
    init_String(&d->icon);
    if (res && res->icon) {
        appendChar_String(&d->icon, res->icon);
    }
    init_String(&d->text);
    init_String(&d->command);
}

static void deinit_LookupItem(iLookupItem *d) {
    deinit_String(&d->command);
    deinit_String(&d->text);
    deinit_String(&d->icon);
    delete_LookupResult(d->result);
}

static void draw_LookupItem_(iLookupItem *d, iPaint *p, iRect rect, const iListWidget *list) {
    const iBool isPressing = isMouseDown_ListWidget(list);
    const iBool isHover    = isHover_Widget(list) && constHoverItem_ListWidget(list) == d;
    const iBool isCursor   = d->listItem.isSelected;
    if (isHover || isCursor) {
        fillRect_Paint(p,
                       rect,
                       isPressing || isCursor ? uiBackgroundPressed_ColorId
                                              : uiBackgroundFramelessHover_ColorId);
    }
    int fg = isHover || isCursor
                 ? permanent_ColorId | (isPressing || isCursor ? uiTextPressed_ColorId
                                                               : uiTextFramelessHover_ColorId)
                 : d->fg;
    const iInt2 size = measureRange_Text(d->font, range_String(&d->text)).bounds.size;
    iInt2       pos  = init_I2(left_Rect(rect) + 3 * gap_UI, mid_Rect(rect).y - size.y / 2);
    if (d->listItem.isSeparator) {
        pos.y = bottom_Rect(rect) - lineHeight_Text(d->font);
    }
    if (!isEmpty_String(&d->icon)) {
        const iRect iconRect = { init_I2(pos.x, top_Rect(rect)),
                                 init_I2(gap_UI * 5, height_Rect(rect)) };
        const iRect iconVis = visualBounds_Text(d->font, range_String(&d->icon));
        drawRange_Text(d->font,
                       sub_I2(mid_Rect(iconRect), mid_Rect(iconVis)),
                       fg,
                       range_String(&d->icon));
        pos.x += width_Rect(iconRect) + gap_UI * 3 / 2;
    }
    drawRange_Text(d->font, pos, fg, range_String(&d->text));
}

iBeginDefineSubclass(LookupItem, ListItem)
    .draw = (iAny *) draw_LookupItem_,
iEndDefineSubclass(LookupItem)

iDefineObjectConstructionArgs(LookupItem, (const iLookupResult *res), res)

/*----------------------------------------------------------------------------------------------*/

struct Impl_LookupWidget {
    iWidget      widget;
    iListWidget *list;
    size_t       cursor;
    iThread *    work;
    iCondition   jobAvailable; /* wakes up the work thread */
    iMutex *     mtx;
    iString      pendingTerm;
    iObjectList *pendingDocs;
    iLookupJob * finishedJob;
};

static float scoreMatch_(const iRegExp *pattern, iRangecc text) {
    float score = 0.0f;
    iRegExpMatch m;
    init_RegExpMatch(&m);
    while (matchRange_RegExp(pattern, text, &m)) {
        /* Match near the beginning is scored higher. */
        score += (float) size_Range(&m.range) / ((float) m.range.start + 1);
    }
    return score;
}

static float bookmarkRelevance_LookupJob_(const iLookupJob *d, const iBookmark *bm) {
    if (isFolder_Bookmark(bm)) {
        return 0.0f;
    }
    iUrl parts;
    init_Url(&parts, &bm->url);
    const float t = scoreMatch_(d->term, range_String(&bm->title));
    const float h = scoreMatch_(d->term, parts.host);
    const float p = scoreMatch_(d->term, parts.path);
    const float g = scoreMatch_(d->term, range_String(&bm->tags));
    return h + iMax(p, t) + 2 * g; /* extra weight for tags */
}

static float feedEntryRelevance_LookupJob_(const iLookupJob *d, const iFeedEntry *entry) {
    iUrl parts;
    init_Url(&parts, &entry->url);
    const float t = scoreMatch_(d->term, range_String(&entry->title));
    const float h = scoreMatch_(d->term, parts.host);
    const float p = scoreMatch_(d->term, parts.path);
    const double age = secondsSince_Time(&d->now, &entry->posted) / 3600.0 / 24.0; /* days */
    return (t * 3 + h + p) / (age + 1); /* extra weight for title, recency */
}

static float identityRelevance_LookupJob_(const iLookupJob *d, const iGmIdentity *identity) {
    iString *cn = subject_TlsCertificate(identity->cert);
    const float c = scoreMatch_(d->term, range_String(cn));
    const float n = scoreMatch_(d->term, range_String(&identity->notes));
    delete_String(cn);
    return c + 2 * n; /* extra weight for notes */
}

static float visitedRelevance_LookupJob_(const iLookupJob *d, const iVisitedUrl *vis) {
    iUrl parts;
    init_Url(&parts, &vis->url);
    const float h = scoreMatch_(d->term, parts.host);
    const float p = scoreMatch_(d->term, parts.path);
    const double age = secondsSince_Time(&d->now, &vis->when) / 3600.0 / 24.0; /* days */
    return iMax(h, p) / (age + 1); /* extra weight for recency */
}

static iBool matchBookmark_LookupJob_(void *context, const iBookmark *bm) {
    return bookmarkRelevance_LookupJob_(context, bm) > 0;
}

static iBool matchIdentity_LookupJob_(void *context, const iGmIdentity *identity) {
    return identityRelevance_LookupJob_(context, identity) > 0;
}

static void searchBookmarks_LookupJob_(iLookupJob *d) {
    /* Note: Called in a background thread. */
    /* TODO: Thread safety! What if a bookmark gets deleted while its being accessed here? */
    iConstForEach(PtrArray, i, list_Bookmarks(bookmarks_App(), NULL, matchBookmark_LookupJob_, d)) {
        const iBookmark *bm  = i.ptr;
        iLookupResult *  res = new_LookupResult();
        res->type            = bookmark_LookupResultType;
        res->when            = bm->when;
        res->relevance       = bookmarkRelevance_LookupJob_(d, bm);
        res->icon            = bm->icon;
        set_String(&res->label, &bm->title);
        set_String(&res->url, &bm->url);
        set_String(&res->meta, &bm->identity);
        pushBack_PtrArray(&d->results, res);
    }
}

static void searchFeeds_LookupJob_(iLookupJob *d) {
    iConstForEach(PtrArray, i, listEntries_Feeds()) {
        const iFeedEntry *entry = i.ptr;
        const iBookmark *bm = get_Bookmarks(bookmarks_App(), entry->bookmarkId);
        if (!bm) {
            continue;
        }
        const float relevance = feedEntryRelevance_LookupJob_(d, entry);
        if (relevance > 0) {
            iLookupResult *res = new_LookupResult();
            res->type          = feedEntry_LookupResultType;
            res->when          = entry->posted;
            res->relevance     = relevance;
            set_String(&res->url, &entry->url);
            set_String(&res->meta, &bm->title);
            set_String(&res->label, &entry->title);
            res->icon = bm->icon;
            pushBack_PtrArray(&d->results, res);
        }
    }
}

static void searchVisited_LookupJob_(iLookupJob *d) {
    /* Note: Called in a background thread. */
    /* TODO: Thread safety! Visited URLs may be deleted while being accessed here. */
    iConstForEach(PtrArray, i, list_Visited(visited_App(), 0)) {
        const iVisitedUrl *vis = i.ptr;
        const float relevance = visitedRelevance_LookupJob_(d, vis);
        if (relevance > 0) {
            iLookupResult *res = new_LookupResult();
            res->type = history_LookupResultType;
            res->relevance = relevance;
            set_String(&res->label, &vis->url);
            set_String(&res->url, &vis->url);
            res->when = vis->when;
            pushBack_PtrArray(&d->results, res);
        }
    }
}

static void searchHistory_LookupJob_(iLookupJob *d) {
    /* Note: Called in a background thread. */
    size_t index = 0;
    iForEach(ObjectList, i, d->docs) {
        iConstForEach(StringArray, j,
                      searchContents_History(history_DocumentWidget(i.object), d->term)) {
            const char *match = cstr_String(j.value);
            const size_t matchLen = argLabel_Command(match, "len");
            iRangecc text;
            text.start = strstr(match, " str:") + 5;
            text.end = text.start + matchLen;
            const char *url = strstr(text.end, " url:") + 5;
            iLookupResult *res = new_LookupResult();
            res->type = content_LookupResultType;
            res->relevance = ++index; /* most recent comes last */
            setCStr_String(&res->label, "\"");
            appendRange_String(&res->label, text);
            appendCStr_String(&res->label, "\"");
            setCStr_String(&res->url, url);
            pushBack_PtrArray(&d->results, res);
        }
    }
}

static void searchIdentities_LookupJob_(iLookupJob *d) {
    /* Note: Called in a background thread. */
    iConstForEach(PtrArray, i, listIdentities_GmCerts(certs_App(), matchIdentity_LookupJob_, d)) {
        const iGmIdentity *identity = i.ptr;
        iLookupResult *res = new_LookupResult();
        res->type = identity_LookupResultType;
        res->relevance = identityRelevance_LookupJob_(d, identity);
        res->icon = 0x1f464; /* identity->icon; */
        iString *cn = subject_TlsCertificate(identity->cert);
        set_String(&res->label, cn);
        delete_String(cn);
        set_String(&res->meta,
                   collect_String(
                       hexEncode_Block(collect_Block(fingerprint_TlsCertificate(identity->cert)))));
        pushBack_PtrArray(&d->results, res);
    }
}

static iThreadResult worker_LookupWidget_(iThread *thread) {
    iLookupWidget *d = userData_Thread(thread);
//    printf("[LookupWidget] worker is running\n"); fflush(stdout);
    lock_Mutex(d->mtx);
    for (;;) {
        wait_Condition(&d->jobAvailable, d->mtx);
        if (isEmpty_String(&d->pendingTerm)) {
            break; /* Time to quit. */
        }
        iLookupJob *job = new_LookupJob();
        /* Make a regular expression to search for multiple alternative words. */ {
            iString *pattern = new_String();
            iRangecc word = iNullRange;
            iBool isFirst = iTrue;
            iString wordStr;
            init_String(&wordStr);
            while (nextSplit_Rangecc(range_String(&d->pendingTerm), " ", &word)) {
                if (isEmpty_Range(&word)) continue;
                if (!isFirst) appendCStr_String(pattern, ".*");
                setRange_String(&wordStr, word);
                iConstForEach(String, ch, &wordStr) {
                    /* Escape regular expression characters. */
                    if (isSyntaxChar_RegExp(ch.value)) {
                        appendChar_String(pattern, '\\');
                    }
                    appendChar_String(pattern, ch.value);
                }
                isFirst = iFalse;
            }
            deinit_String(&wordStr);
            iAssert(!isEmpty_String(pattern));
            job->term = new_RegExp(cstr_String(pattern), caseInsensitive_RegExpOption);
            delete_String(pattern);
        }
        const size_t termLen = length_String(&d->pendingTerm); /* characters */
        clear_String(&d->pendingTerm);
        job->docs = d->pendingDocs;
        d->pendingDocs = NULL;
        unlock_Mutex(d->mtx);
        /* Do the lookup. */ {
            searchBookmarks_LookupJob_(job);
            searchFeeds_LookupJob_(job);
            searchVisited_LookupJob_(job);
            if (termLen >= 3) {
                searchHistory_LookupJob_(job);
            }
            searchIdentities_LookupJob_(job);
        }
        /* Submit the result. */
        lock_Mutex(d->mtx);
        if (d->finishedJob) {
            /* Previous results haven't been taken yet. */
            delete_LookupJob(d->finishedJob);
        }
//        printf("[LookupWidget] worker has %zu results\n", size_PtrArray(&job->results));
        fflush(stdout);
        d->finishedJob = job;
        postCommand_Widget(as_Widget(d), "lookup.ready");
    }
    unlock_Mutex(d->mtx);
//    printf("[LookupWidget] worker has quit\n"); fflush(stdout);
    return 0;
}

iDefineObjectConstruction(LookupWidget)

static void updateMetrics_LookupWidget_(iLookupWidget *d) {
    setItemHeight_ListWidget(d->list, lineHeight_Text(uiContent_FontId) * 1.333f);
}

void init_LookupWidget(iLookupWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, "lookup");
    setFlags_Widget(w, focusable_WidgetFlag, iTrue);
    setFlags_Widget(w, unhittable_WidgetFlag, isMobile_Platform());
    d->list = addChildFlags_Widget(w, iClob(new_ListWidget()),
                                   resizeToParentWidth_WidgetFlag |
                                   resizeToParentHeight_WidgetFlag);
    /* We will handle focus and cursor manually. */
    setFlags_Widget(as_Widget(d->list), focusable_WidgetFlag, iFalse);
    if (isTerminal_Platform()) {
        setPadding_Widget(as_Widget(d->list), 2, 2, 2, 2);
    }
    d->cursor = iInvalidPos;
    d->work = new_Thread(worker_LookupWidget_);
    setUserData_Thread(d->work, d);
    init_Condition(&d->jobAvailable);
    d->mtx = new_Mutex();
    init_String(&d->pendingTerm);
    d->pendingDocs = NULL;
    d->finishedJob = NULL;
    updateMetrics_LookupWidget_(d);
    start_Thread(d->work);
}

void deinit_LookupWidget(iLookupWidget *d) {
    /* Stop the worker. */ {
        iGuardMutex(d->mtx, {
            iReleasePtr(&d->pendingDocs);
            clear_String(&d->pendingTerm);
            signal_Condition(&d->jobAvailable);
        });
        join_Thread(d->work);
        iRelease(d->work);
    }
    delete_LookupJob(d->finishedJob);
    deinit_String(&d->pendingTerm);
    delete_Mutex(d->mtx);
    deinit_Condition(&d->jobAvailable);
}

void submit_LookupWidget(iLookupWidget *d, const iString *term) {
    iGuardMutex(d->mtx, {
        set_String(&d->pendingTerm, term);
        trim_String(&d->pendingTerm);
        iReleasePtr(&d->pendingDocs);
        if (!isEmpty_String(&d->pendingTerm)) {
            d->pendingDocs = listDocuments_App(get_Root()); /* holds reference to all open tabs */
            signal_Condition(&d->jobAvailable);
        }
        else {
            showCollapsed_Widget(as_Widget(d), iFalse);
        }
    });
}

static void draw_LookupWidget_(const iLookupWidget *d) {
    const iWidget *w = constAs_Widget(d);
    draw_Widget(w);
    /* Draw a frame. */ {
        iPaint p;
        init_Paint(&p);
        drawRect_Paint(&p,
                       bounds_Widget(w),
                       isFocused_Widget(w) ? uiInputFrameFocused_ColorId : uiSeparator_ColorId);
    }
}

static int cmpPtr_LookupResult_(const void *p1, const void *p2) {
    const iLookupResult *a = *(const iLookupResult **) p1;
    const iLookupResult *b = *(const iLookupResult **) p2;
    if (a->type != b->type) {
        return iCmp(a->type, b->type);
    }
    if (fabsf(a->relevance - b->relevance) < 0.0001f) {
        return cmpString_String(&a->url, &b->url);
    }
    return -iCmp(a->relevance, b->relevance);
}

static const char *cstr_LookupResultType(enum iLookupResultType d) {
    switch (d) {
        case bookmark_LookupResultType:
            return "heading.lookup.bookmarks";
        case feedEntry_LookupResultType:
            return "heading.lookup.feeds";
        case history_LookupResultType:
            return "heading.lookup.history";
        case content_LookupResultType:
            return "heading.lookup.pagecontent";
        case identity_LookupResultType:
            return "heading.lookup.identities";
        default:
            return "heading.lookup.other";
    }
}

static void presentResults_LookupWidget_(iLookupWidget *d) {
    iLookupJob *job;
    iGuardMutex(d->mtx, {
        job = d->finishedJob;
        d->finishedJob = NULL;
    });
    if (!job) return;
    clear_ListWidget(d->list);
    sort_Array(&job->results, cmpPtr_LookupResult_);
    enum iLookupResultType lastType = none_LookupResultType;
    const size_t maxPerType = 10; /* TODO: Setting? */
    size_t perType = 0;
    iConstForEach(PtrArray, i, &job->results) {
        const iLookupResult *res = i.ptr;
        if (lastType != res->type) {
            /* Heading separator. */
            iLookupItem *item = new_LookupItem(NULL);
            item->listItem.isSeparator = iTrue;
            item->fg = uiHeading_ColorId;
            item->font = uiLabel_FontId;
            format_String(&item->text, "%s", cstr_Lang(cstr_LookupResultType(res->type)));
            addItem_ListWidget(d->list, item);
            iRelease(item);
            lastType = res->type;
            perType = 0;
        }
        if (perType > maxPerType) {
            continue;
        }
        if (res->type == identity_LookupResultType) {
            const iString *docUrl = url_DocumentWidget(document_App());
            iBlock *finger = hexDecode_Rangecc(range_String(&res->meta));
            const iGmIdentity *ident = findIdentity_GmCerts(certs_App(), finger);
            /* Sign in/out. */ {
                const iBool isUsed = isUsedOn_GmIdentity(ident, docUrl);
                iLookupItem *item = new_LookupItem(res);
                item->fg = uiText_ColorId;
                item->font = uiContent_FontId;
                format_String(&item->text,
                              "%s \u2014 " uiTextStrong_ColorEscape "%s",
                              cstr_String(&res->label),
                              cstr_Lang(isUsed ? "ident.stopuse" : "ident.use"));
                format_String(&item->command, "ident.sign%s ident:%s url:%s",
                              isUsed ? "out arg:0" : "in", cstr_String(&res->meta),
                              cstr_String(docUrl));
                addItem_ListWidget(d->list, item);
                iRelease(item);
            }
            if (isUsed_GmIdentity(ident)) {
                iLookupItem *item = new_LookupItem(res);
                item->fg = uiText_ColorId;
                item->font = uiContent_FontId;
                format_String(&item->text,
                              "%s \u2014 " uiTextStrong_ColorEscape "%s",
                              cstr_String(&res->label),
                              cstr_Lang("ident.stopuse.all"));
                format_String(&item->command, "ident.signout arg:1 ident:%s",
                              cstr_String(&res->meta));
                addItem_ListWidget(d->list, item);
                iRelease(item);
            }
            delete_Block(finger);
            continue;
        }
        iLookupItem *item = new_LookupItem(res);
        const char *url = cstr_String(&res->url);
        if (startsWithCase_String(&res->url, "gemini://")) {
            url += 9;
        }
        switch (res->type) {
            case bookmark_LookupResultType: {
                item->fg = uiTextStrong_ColorId;
                item->font = uiContent_FontId;
                format_String(&item->text,
                              "%s %s",
                              cstr_String(&res->label),
                              uiText_ColorEscape);
                setCStr_String(&item->command, "open");
                if (!isEmpty_String(&res->meta)) {
                    appendCStr_String(&item->command, " setident:");
                    append_String(&item->command, &res->meta);
                    /* Also include in the visible label. */
                    const iGmIdentity *ident = findIdentity_GmCerts(
                        certs_App(), collect_Block(hexDecode_Rangecc(range_String(&res->meta))));
                    if (ident) {
                        appendFormat_String(&item->text, " \u2014 " person_Icon " %s",
                                            cstr_String(name_GmIdentity(ident)));
                    }
                }
                appendFormat_String(&item->text, " \u2014 %s", url);
                appendFormat_String(&item->command, " url:%s", cstr_String(&res->url));
                break;
            }
            case feedEntry_LookupResultType: {
                item->fg = uiTextStrong_ColorId;
                item->font = uiContent_FontId;
                format_String(&item->text,
                              "%s %s\u2014 %s",
                              cstr_String(&res->label),
                              uiText_ColorEscape,
                              cstr_String(&res->meta));
                const iString *cmd = feedEntryOpenCommand_String(&res->url, 0, 0);
                if (cmd) {
                    set_String(&item->command, cmd);
                }
                break;
            }
            case history_LookupResultType: {
                item->fg = uiText_ColorId;
                item->font = uiContent_FontId;
                format_String(&item->text, "%s \u2014 ", url);
                append_String(&item->text, collect_String(format_Time(&res->when, "%b %d, %Y")));
                format_String(&item->command, "open url:%s", cstr_String(&res->url));
                break;
            }
            case content_LookupResultType: {
                item->fg = uiText_ColorId;
                item->font = uiContent_FontId;
                format_String(&item->text, "%s \u2014 %s", url, cstr_String(&res->label));
                format_String(&item->command, "open url:%s", cstr_String(&res->url));
                break;
            }
            default:
                break;
        }
        addItem_ListWidget(d->list, item);
        iRelease(item);
        perType++;
    }
    delete_LookupJob(job);
    /* Re-select the item at the cursor. */
    if (d->cursor != iInvalidPos) {
        d->cursor = iMin(d->cursor, numItems_ListWidget(d->list) - 1);
        ((iListItem *) item_ListWidget(d->list, d->cursor))->isSelected = iTrue;
    }
    scrollOffset_ListWidget(d->list, 0);
    updateVisible_ListWidget(d->list);
    invalidate_ListWidget(d->list);
    const iBool allowShow = isVisible_Widget(d) ||
            (focus_Widget() && !cmp_String(id_Widget(focus_Widget()), "url"));
    showCollapsed_Widget(as_Widget(d), allowShow && numItems_ListWidget(d->list) != 0);
}

static iLookupItem *item_LookupWidget_(iLookupWidget *d, size_t index) {
    return item_ListWidget(d->list, index);
}

static void setCursor_LookupWidget_(iLookupWidget *d, size_t index) {
    if (index != d->cursor) {
        iLookupItem *item = item_LookupWidget_(d, d->cursor);
        if (item) {
            item->listItem.isSelected = iFalse;
            invalidateItem_ListWidget(d->list, d->cursor);
        }
        d->cursor = index;
        if ((item = item_LookupWidget_(d, d->cursor)) != NULL) {
            item->listItem.isSelected = iTrue;
            invalidateItem_ListWidget(d->list, d->cursor);
        }
        scrollToItem_ListWidget(d->list, d->cursor, 0);
    }
}

static iBool moveCursor_LookupWidget_(iLookupWidget *d, int delta) {
    const int dir  = iSign(delta);
    size_t    cur  = d->cursor;
    size_t    good = cur;
    while (delta && ((dir < 0 && cur > 0) || (dir > 0 && cur < numItems_ListWidget(d->list) - 1))) {
        cur += dir;
        if (!item_LookupWidget_(d, cur)->listItem.isSeparator) {
            delta -= dir;
            good = cur;
        }
    }
    setCursor_LookupWidget_(d, good);
    return delta == 0;
}

static iBool processEvent_LookupWidget_(iLookupWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    const char *cmd = command_UserEvent(ev);
    if (isCommand_Widget(w, ev, "lookup.ready") && isFocused_Widget(findWidget_App("url"))) {
        /* Take the results and present them in the list. */
        presentResults_LookupWidget_(d);
        return iTrue;
    }
    if (isMetricsChange_UserEvent(ev)) {
        updateMetrics_LookupWidget_(d);
    }
    else if (isResize_UserEvent(ev) || equal_Command(cmd, "keyboard.changed") ||
             (equal_Command(cmd, "layout.changed") &&
              equal_Rangecc(range_Command(cmd, "id"), "navbar"))) {
        /* Position the lookup popup in relation to the URL bar. */ {
            iRoot    *root       = w->root;
            iWidget  *url        = findChild_Widget(root->widget, "url");
            const int minWidth   = iMin(120 * gap_UI, width_Rect(safeRect_Root(root)));
            const int urlWidth   = width_Widget(url);
            int       extraWidth = 0;
            if (urlWidth < minWidth) {
                extraWidth = minWidth - urlWidth;
            }
            const iRect navBarBounds = bounds_Widget(findChild_Widget(root->widget, "navbar"));
            const iBool atBottom = prefs_App()->bottomNavBar;
            setFixedSize_Widget(
                w,
                init_I2(width_Widget(url) + extraWidth,
                        (atBottom ? top_Rect(navBarBounds)
                                  : (bottom_Rect(rect_Root(root)) - bottom_Rect(navBarBounds))) /
                            2));
            const iInt2 topLeft = atBottom
                                      ? addY_I2(topLeft_Rect(bounds_Widget(url)), -w->rect.size.y)
                                      : bottomLeft_Rect(bounds_Widget(url));
            setPos_Widget(
                w, windowToLocal_Widget(w, max_I2(zero_I2(), addX_I2(topLeft, -extraWidth / 2))));
            if (isMobile_Platform()) {
                /* Adjust height based on keyboard size. */
                /* TODO: Check this again. */
                if (!atBottom) {
                    w->rect.size.y = bottom_Rect(visibleRect_Root(root)) - top_Rect(bounds_Widget(w));
                }
                else {
                    w->rect.pos = windowToLocal_Widget(w, visibleRect_Root(root).pos);
                    w->rect.size.y = height_Rect(visibleRect_Root(root)) - height_Rect(navBarBounds) +
                        (!isPortraitPhone_App() ? bottomSafeInset_Mobile() : 0);
                }
                if (isApple_Platform() && deviceType_App() != desktop_AppDeviceType) {
                    int l = leftSafeInset_Mobile();
                    int r = rightSafeInset_Mobile();
                    //safeAreaInsets_iOS(&l, NULL, &r, NULL);
                    w->rect.size.x = size_Root(root).x - l - r;
                    w->rect.pos.x  = l;
                    /* TODO: Need to use windowToLocal_Widget? */
                }
            }
            arrange_Widget(w);
        }
        updateVisible_ListWidget(d->list);
        invalidate_ListWidget(d->list);
    }
    if (equalArg_Command(cmd, "input.ended", "id", "url") &&
        (deviceType_App() != desktop_AppDeviceType || !isFocused_Widget(w))) {
        showCollapsed_Widget(w, iFalse);
    }
    if (isCommand_Widget(w, ev, "focus.lost")) {
        setCursor_LookupWidget_(d, iInvalidPos);
    }
    if (isCommand_Widget(w, ev, "focus.gained")) {
        if (d->cursor == iInvalidPos) {
            setCursor_LookupWidget_(d, 1);
        }
    }
    if (isCommand_Widget(w, ev, "list.clicked")) {
        iInputWidget *url = findWidget_App("url");
        const iLookupItem *item = constItem_ListWidget(d->list, arg_Command(cmd));
        if (item && !isEmpty_String(&item->command)) {
            setText_InputWidget(url, url_DocumentWidget(document_App()));
            showCollapsed_Widget(w, iFalse);
            setCursor_LookupWidget_(d, iInvalidPos);
            postCommandString_Root(get_Root(), &item->command);
            postCommand_App("focus.set id:"); /* unfocus */
        }
        return iTrue;
    }
    if (ev->type == SDL_MOUSEMOTION) {
        if (contains_Widget(w, init_I2(ev->motion.x, ev->motion.y))) {
            setCursor_Window(get_Window(), SDL_SYSTEM_CURSOR_HAND);
        }
        return iFalse;
    }
    if (ev->type == SDL_KEYDOWN) {
        const int mods = keyMods_Sym(ev->key.keysym.mod);
        const int key = ev->key.keysym.sym;
        if (isFocused_Widget(d)) {
            iWidget *url = findWidget_App("url");
            switch (key) {
                case SDLK_ESCAPE:
                    showCollapsed_Widget(w, iFalse);
                    setCursor_LookupWidget_(d, iInvalidPos);
                    setFocus_Widget(url);
                    return iTrue;
                case SDLK_UP:
                    if (!moveCursor_LookupWidget_(d, -1) && !prefs_App()->bottomNavBar) {
                        setCursor_LookupWidget_(d, iInvalidPos);
                        setFocus_Widget(url);
                    }
                    return iTrue;
                case SDLK_DOWN:
                    moveCursor_LookupWidget_(d, +1);
                    return iTrue;
                case SDLK_PAGEUP:
                    moveCursor_LookupWidget_(d, -visCount_ListWidget(d->list) + 1);
                    return iTrue;
                case SDLK_PAGEDOWN:
                    moveCursor_LookupWidget_(d, visCount_ListWidget(d->list) - 1);
                    return iTrue;
                case SDLK_HOME:
                    setCursor_LookupWidget_(d, 1);
                    return iTrue;
                case SDLK_END:
                    setCursor_LookupWidget_(d, numItems_ListWidget(d->list) - 1);
                    return iTrue;
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                case SDLK_RETURN:
                    postCommand_Widget(w, "list.clicked arg:%zu", d->cursor);
                    return iTrue;
            }
        }
        /* Focus switching between URL bar and lookup results. */
        if (isVisible_Widget(w)) {
            if (((!mods && ((key == SDLK_DOWN && !prefs_App()->bottomNavBar) ||
                            (key == SDLK_UP && prefs_App()->bottomNavBar))) ||
                 key == SDLK_TAB) &&
                focus_Widget() == findWidget_App("url") && numItems_ListWidget(d->list)) {
                setCursor_LookupWidget_(d, 1); /* item 0 is always the first heading */
                setFocus_Widget(w);
                return iTrue;
            }
            else if (key == SDLK_TAB && isFocused_Widget(w)) {
                setFocus_Widget(findWidget_App("url"));
                return iTrue;
            }
        }
    }
    return processEvent_Widget(w, ev);
}

iBeginDefineSubclass(LookupWidget, Widget)
    .draw         = (iAny *) draw_LookupWidget_,
    .processEvent = (iAny *) processEvent_LookupWidget_,
iEndDefineSubclass(LookupWidget)
