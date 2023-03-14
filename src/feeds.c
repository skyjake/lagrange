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

#include "feeds.h"
#include "bookmarks.h"
#include "gmrequest.h"
#include "visited.h"
#include "lang.h"
#include "app.h"

#include <the_Foundation/file.h>
#include <the_Foundation/hash.h>
#include <the_Foundation/intset.h>
#include <the_Foundation/mutex.h>
#include <the_Foundation/path.h>
#include <the_Foundation/queue.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/stringset.h>
#include <the_Foundation/thread.h>
#include <SDL_timer.h>
#include <ctype.h>

iDeclareType(Feeds)
iDeclareType(FeedJob)

iDefineTypeConstruction(FeedEntry)

void init_FeedEntry(iFeedEntry *d) {
    iZap(d->posted);
    iZap(d->discovered);
    init_String(&d->url);
    init_String(&d->title);
    d->bookmarkId = 0;
    d->isHeading = iFalse;
}

void deinit_FeedEntry(iFeedEntry *d) {
    deinit_String(&d->title);
    deinit_String(&d->url);
}

const iString *url_FeedEntry(const iFeedEntry *d) {
    return urlFragmentStripped_String(&d->url);
}

iBool isUnread_FeedEntry(const iFeedEntry *d) {
    const size_t fragPos = indexOf_String(&d->url, '#');
    if (fragPos != iInvalidPos) {
        /* Check if the entry is newer than the latest visit. If the URL has not been visited,
           `urlVisitTime_Visited` returns a zero timestamp that is always earlier than
           `posted`. */
        const iTime visTime = urlVisitTime_Visited(visited_App(), url_FeedEntry(d));
        return cmp_Time(&visTime, &d->posted) < 0;
    }
    if (!containsUrl_Visited(visited_App(), &d->url)) {
        return iTrue;
    }
    return iFalse;
}

/*----------------------------------------------------------------------------------------------*/

static int requestTimeoutSeconds_FeedJob_ = 10.0f;

struct Impl_FeedJob {
    iString     url;
    uint32_t    bookmarkId;
    iTime       startTime;
    iBool       isFirstUpdate; /* hasn't been checked ever before */
    iBool       checkHeadings;
    iBool       ignoreWeb;
    iGmRequest *request;
    int         numRedirect;
    iPtrArray   results;
};

static void init_FeedJob(iFeedJob *d, const iBookmark *bookmark) {
    initCopy_String(&d->url, &bookmark->url);
    d->bookmarkId = id_Bookmark(bookmark);
    d->request = NULL;
    d->numRedirect = 0;
    init_PtrArray(&d->results);
    iZap(d->startTime);
    d->isFirstUpdate = iFalse;
    d->checkHeadings = (bookmark->flags & headings_BookmarkFlag) != 0;
    d->ignoreWeb     = (bookmark->flags & ignoreWeb_BookmarkFlag) != 0;
}

static void deinit_FeedJob(iFeedJob *d) {
    iRelease(d->request);
    iForEach(PtrArray, i, &d->results) {
        delete_FeedEntry(i.ptr);
    }
    deinit_PtrArray(&d->results);
    deinit_String(&d->url);
}

static iBool isTimedOut_FeedJob_(iFeedJob *d) {
    return elapsedSeconds_Time(&d->startTime) > requestTimeoutSeconds_FeedJob_;
}

iDefineTypeConstructionArgs(FeedJob, (const iBookmark *bm), bm)

/*----------------------------------------------------------------------------------------------*/

static const char *feedsFilename_Feeds_         = "feeds.txt";
static const int   updateIntervalSeconds_Feeds_ = 4 * 60 * 60;

struct Impl_Feeds {
    iMutex *  mtx;
    iString   saveDir;
    iIntSet   previouslyCheckedFeeds; /* bookmark IDs */
    iTime     lastRefreshedAt;
    int       refreshTimer;
    iThread * worker;
    iBool     stopWorker;
    iPtrArray jobs; /* pending */
    iSortedArray entries; /* pointers to all discovered feed entries, sorted by entry ID (URL) */
};

static iFeeds feeds_;

#define maxConcurrentRequests_Feeds 4

static void submit_FeedJob_(iFeedJob *d) {
    d->request = new_GmRequest(certs_App());
    setUrl_GmRequest(d->request, &d->url);
    initCurrent_Time(&d->startTime);
    submit_GmRequest(d->request);
}

static iBool isSubscribed_(void *context, const iBookmark *bm) {
    iUnused(context);
    return (bm->flags & subscribed_BookmarkFlag) != 0;
}

static const iPtrArray *listSubscriptions_(void) {
    return list_Bookmarks(bookmarks_App(), NULL, isSubscribed_, NULL);
}

static iFeedJob *startNextJob_Feeds_(iFeeds *d) {
    if (isEmpty_PtrArray(&d->jobs)) {
        return NULL;
    }
    iFeedJob *job;
    take_PtrArray(&d->jobs, 0, (void **) &job);
    submit_FeedJob_(job);
    return job;
}

static iBool isTrimmablePunctuation_(iChar c) {
    if (c == '"') {
        return iFalse; /* Probably quoted text. */
    }
    if (c == '(' || c == '[' || c == '{' || c == '<') {
        return iFalse;
    }
    /* Dashes or punctuation? */
    return c == 0x2013 || c == 0x2014 || (c < 128 && ispunct(c));
}

static void trimTitle_(iString *title) {
    const char *start = constBegin_String(title);
    iConstForEach(String, i, title) {
        start = i.pos;
        if (!isSpace_Char(i.value) && !isTrimmablePunctuation_(i.value)) {
            break;
        }
    }
    remove_Block(&title->chars, 0, start - constBegin_String(title));
}

static iBool isUrlIgnored_FeedJob_(const iFeedJob *d, iRangecc url) {
    if (d->ignoreWeb) {
        return startsWithCase_Rangecc(url, "http");
    }
    return iFalse;
}

static iBool parseResult_FeedJob_(iFeedJob *d) {
    /* Returns true if the job is done and can be released. False means the job continues. */
    if (category_GmStatusCode(status_GmRequest(d->request)) == categoryRedirect_GmStatusCode) {
        /* Set up a new request. */
        if (++d->numRedirect < 5) {
            set_String(&d->url, meta_GmRequest(d->request));
            iRelease(d->request);
            submit_FeedJob_(d);
            return iFalse;
        }
        return iTrue;
    }
    /* TODO: Should tell the user if the request failed. */       
    if (isSuccess_GmStatusCode(status_GmRequest(d->request))) {
        iBeginCollect();
        iTime now;
        iTime perEntryAdjust;
        initSeconds_Time(&perEntryAdjust, 1.0);
        initCurrent_Time(&now);
        iRegExp *linkPattern =
            new_RegExp("^=>\\s*([^\\s]+)\\s+"
                       "([0-9][0-9][0-9][0-9]-[0-1][0-9]-[0-3][0-9])"
                       "([^0-9].*)",
                       0);
        iString src;
        initBlock_String(&src, &lockResponse_GmRequest(d->request)->body);
        unlockResponse_GmRequest(d->request);
        iRangecc srcLine = iNullRange;
        while (nextSplit_Rangecc(range_String(&src), "\n", &srcLine)) {
            iRangecc line = srcLine;
            trimEnd_Rangecc(&line);
            iRegExpMatch m;
            init_RegExpMatch(&m);
            if (matchRange_RegExp(linkPattern, line, &m)) {
                const iRangecc url   = capturedRange_RegExpMatch(&m, 1);
                const iRangecc date  = capturedRange_RegExpMatch(&m, 2);
                const iRangecc title = capturedRange_RegExpMatch(&m, 3);
                if (isUrlIgnored_FeedJob_(d, url)) {
                    continue;
                }
                iFeedEntry *entry = new_FeedEntry();
                entry->discovered = now;
                sub_Time(&now, &perEntryAdjust);
                entry->bookmarkId = d->bookmarkId;
                setRange_String(&entry->url, url);
                set_String(&entry->url, canonicalUrl_String(absoluteUrl_String(url_GmRequest(d->request), &entry->url)));
                setRange_String(&entry->title, title);
                trimTitle_(&entry->title);
                int year, month, day;
                sscanf(date.start, "%04d-%02d-%02d", &year, &month, &day);
                init_Time(
                    &entry->posted,
                    &(iDate){
                        .year = year, .month = month, .day = day, .hour = 12 /* noon UTC */ });
                pushBack_PtrArray(&d->results, entry);
            }
            if (d->checkHeadings) {
                init_RegExpMatch(&m);
                if (startsWith_Rangecc(line, "#")) {
                    while (*line.start == '#' && line.start < line.end) {
                        line.start++;
                    }
                    trimStart_Rangecc(&line);
                    iFeedEntry *entry = new_FeedEntry();
                    entry->isHeading = iTrue;
                    entry->posted = now;
                    if (!d->isFirstUpdate) {
                        entry->discovered = now;
                        sub_Time(&now, &perEntryAdjust);
                    }
                    entry->bookmarkId = d->bookmarkId;
                    iString *title = newRange_String(line);
                    set_String(&entry->title, title);
                    set_String(&entry->url, &d->url);
                    appendChar_String(&entry->url, '#');
                    append_String(&entry->url, collect_String(urlEncode_String(title)));
                    set_String(&entry->url, canonicalUrl_String(&entry->url));
                    delete_String(title);
                    pushBack_PtrArray(&d->results, entry);
                }
            }
        }
        deinit_String(&src);
        iRelease(linkPattern);
        iEndCollect();
    }
    return iTrue;
}

static void save_Feeds_(iFeeds *d) {
    iFile *f = new_File(collect_String(concatCStr_Path(&d->saveDir, feedsFilename_Feeds_)));
    if (open_File(f, write_FileMode | text_FileMode)) {
        lock_Mutex(d->mtx);
        iString *str = new_String();
        format_String(str, "%llu\n# Feeds\n", (unsigned long long)
                      integralSeconds_Time(&d->lastRefreshedAt));
        write_File(f, utf8_String(str));
        /* Index of feeds for IDs. */ {
            iConstForEach(PtrArray, i, listSubscriptions_()) {
                const iBookmark *bm = i.ptr;
                format_String(str, "%08x %s\n", id_Bookmark(bm), cstr_String(&bm->url));
                write_File(f, utf8_String(str));
            }
        }
        writeData_File(f, "# Entries\n", 10);
        iTime now;
        initCurrent_Time(&now);
        iConstForEach(Array, i, &d->entries.values) {
            const iFeedEntry *entry = *(const iFeedEntry **) i.value;
            /* Heading entries are kept as long as they are present in the source. */
            if (!entry->isHeading && isValid_Time(&entry->discovered) &&
                secondsSince_Time(&now, &entry->discovered) > maxAge_Visited) {
                continue; /* Forget entries discovered long ago. */
            }
            format_String(str, "%x\n%llu\n%llu\n%s\n%s\n",
                          entry->bookmarkId,
                          (unsigned long long) integralSeconds_Time(&entry->posted),
                          (unsigned long long) integralSeconds_Time(&entry->discovered),
                          cstr_String(&entry->url),
                          cstr_String(&entry->title));
            write_File(f, utf8_String(str));
        }
        delete_String(str);
        close_File(f);
        unlock_Mutex(d->mtx);
    }
    iRelease(f);
}

static iBool isHeadingEntry_FeedEntry_(const iFeedEntry *d) {
    return contains_String(&d->url, '#');
}

static iStringSet *listHeadingEntriesFrom_Feeds_(const iFeeds *d, uint32_t sourceId) {
    iStringSet *set = new_StringSet();
    iConstForEach(Array, i, &d->entries.values) {
        const iFeedEntry *entry = *(const iFeedEntry **) i.value;
        if (entry->bookmarkId == sourceId) {
            insert_StringSet(set, &entry->url);
        }
    }
    return set;
}

static iBool updateEntries_Feeds_(iFeeds *d, iBool isHeadings, uint32_t sourceId,
                                  iPtrArray *incoming) {
    /* Entries are removed from `incoming` if they are added to the Feeds entries array.
       Anything remaining in `incoming` will be deleted afterwards. */
    iBool gotNew = iFalse;
    iTime now;
    initCurrent_Time(&now);
    if (isHeadings) {
        lock_Mutex(d->mtx);
//        printf("Updating sourceID %d...\n", sourceId);
        iStringSet *known = listHeadingEntriesFrom_Feeds_(d, sourceId);
//        puts("  Known URLs:");
//        iConstForEach(StringSet, ss, known) {
//            printf("   {%s}\n", cstr_String(ss.value));
//        }
        iStringSet *presentInSource = new_StringSet();
        /* Look for unknown entries. */
        iForEach(PtrArray, i, incoming) {
            iFeedEntry *entry = i.ptr;
            insert_StringSet(presentInSource, &entry->url);
            if (!contains_StringSet(known, &entry->url)) {
//                printf("  {%s} is new\n", cstr_String(&entry->url));
                insert_SortedArray(&d->entries, &entry);
                gotNew = iTrue;
                remove_PtrArrayIterator(&i);
            }
        }
//        puts("  URLs present in source:");
//        iConstForEach(StringSet, ps, presentInSource) {
//            printf("    {%s}\n", cstr_String(ps.value));
//        }
//        puts("  URLs to purge:");
        /* All known entries that are no longer present in source must be deleted. */
        iForEach(Array, e, &d->entries.values) {
            iFeedEntry *entry = *(iFeedEntry **) e.value;
            if (entry->bookmarkId == sourceId &&
                !contains_StringSet(presentInSource, &entry->url)) {
//                printf("    {%s}\n", cstr_String(&entry->url));
                delete_FeedEntry(entry);
                remove_ArrayIterator(&e);
            }
        }
//        puts("Done.");
        iRelease(presentInSource);
        iRelease(known);
        unlock_Mutex(d->mtx);
    }
    else {
        /* All visited URLs still present in the source should be kept indefinitely so their
           read status remains correct. The Kept flag will be cleared after the URL has been
           discarded from the entry database and enough time has passed. */ {
//            printf("updating entries from %d:\n", sourceId);
            iForEach(PtrArray, i, incoming) {
                const iFeedEntry *entry = i.ptr;
//                printf("marking as kept: {%s}\n", cstr_String(&entry->url));
                setUrlKept_Visited(visited_App(), &entry->url, iTrue);
            }
        }
        iStringSet *handledUrls = new_StringSet();
        lock_Mutex(d->mtx);
        iForEach(PtrArray, i, incoming) {
            iFeedEntry *entry = i.ptr;
            size_t pos;
            if (contains_StringSet(handledUrls, &entry->url)) {
                /* This is a duplicate. Each unique URL is handled only once. */
                delete_FeedEntry(entry);
                remove_PtrArrayIterator(&i);
                continue;
            }
            insert_StringSet(handledUrls, &entry->url);
            if (locate_SortedArray(&d->entries, &entry, &pos)) {
                iFeedEntry *existing = *(iFeedEntry **) at_SortedArray(&d->entries, pos);
                iAssert(!isHeadingEntry_FeedEntry_(existing));
                iAssert(!isHeadingEntry_FeedEntry_(entry));
                /* Already known, but update it, maybe the time and label have changed. */
                iBool changed = iFalse;
                iDate newDate;
                iDate oldDate;
                init_Date(&newDate, &entry->posted);
                init_Date(&oldDate, &existing->posted);
                if (!equalCase_String(&existing->title, &entry->title) ||
                    (newDate.year != oldDate.year || newDate.month != oldDate.month ||
                     newDate.day != oldDate.day)) {
                    changed = iTrue;
                }
                set_String(&existing->title, &entry->title);
                existing->posted     = entry->posted;
                existing->discovered = entry->discovered; /* prevent discarding */
                delete_FeedEntry(entry);
                if (changed) {
                    /* TODO: better to use a new flag for read feed entries? */
                    removeUrl_Visited(visited_App(), &existing->url);
                    gotNew = iTrue;
                }
            }
            else {
                insert_SortedArray(&d->entries, &entry);
                gotNew = iTrue;
            }
            remove_PtrArrayIterator(&i);
        }
        unlock_Mutex(d->mtx);
        fflush(stdout);
        iRelease(handledUrls);
    }
    return gotNew;
}

static iThreadResult fetch_Feeds_(iThread *thread) {
    iFeeds *d = &feeds_;
    iUnused(thread);
    iFeedJob *work[maxConcurrentRequests_Feeds]; /* We'll do a couple of concurrent requests. */
    iZap(work);
    iBool gotNew = iFalse;
    postCommand_App("feeds.update.started");
    const size_t totalJobs = size_PtrArray(&d->jobs);
    int numFinishedJobs = 0;
    while (!d->stopWorker) {
        /* Start new jobs. */
        iForIndices(i, work) {
            if (!work[i]) {
                work[i] = startNextJob_Feeds_(d);
            }
        }
        sleep_Thread(0.5); /* TODO: wait on a Condition so we can exit quickly */
        if (d->stopWorker) break;
        size_t ongoing = 0;
        iBool doNotify = iFalse;
        iForIndices(i, work) {
            if (work[i]) {
                if (isFinished_GmRequest(work[i]->request)) {
                    if (parseResult_FeedJob_(work[i])) {
                        gotNew |= updateEntries_Feeds_(
                            d, work[i]->checkHeadings, work[i]->bookmarkId, &work[i]->results);
                        delete_FeedJob(work[i]);
                        work[i] = NULL;
                        numFinishedJobs++;
                        doNotify = iTrue;
                    }
                    else {
                        ongoing++;
                    }
                }
                else if (isTimedOut_FeedJob_(work[i])) {
                    /* Maybe we'll get it next time! */
                    delete_FeedJob(work[i]);
                    work[i] = NULL;
                    numFinishedJobs++;
                    doNotify = iTrue;
                }
                else {
                    ongoing++;
                }
                /* TODO: abort job if it takes too long (> 15 seconds?) */
            }
        }
        if (doNotify) {
            postCommandf_App("feeds.update.progress arg:%d total:%zu", numFinishedJobs, totalJobs);
        }
        /* Stop if everything has finished. */
        if (ongoing == 0 && isEmpty_PtrArray(&d->jobs)) {
            break;
        }
    }
    initCurrent_Time(&d->lastRefreshedAt);
    save_Feeds_(d);
    /* Check if there are visited URLs marked as Kept that can be cleared because they are no
       longer present in the database. */ {
        iStringSet *knownEntryUrls = new_StringSet();
        lock_Mutex(d->mtx);
        iConstForEach(Array, i, &d->entries.values) {
            const iFeedEntry *entry = *(const iFeedEntry **) i.value;
            insert_StringSet(knownEntryUrls, &entry->url);
        }
        unlock_Mutex(d->mtx);
        iConstForEach(PtrArray, j, listKept_Visited(visited_App())) {
            iVisitedUrl *visUrl = j.ptr;
            if (!contains_StringSet(knownEntryUrls, &visUrl->url)) {
                visUrl->flags &= ~kept_VisitedUrlFlag;
//                printf("unkept: {%s}\n", cstr_String(&visUrl->url));
            }
        }
        iRelease(knownEntryUrls);
    }
    postCommandf_App("feeds.update.finished arg:%d unread:%zu", gotNew ? 1 : 0,
                     numUnread_Feeds());
    return 0;
}

static iBool startWorker_Feeds_(iFeeds *d) {
    if (d->worker) {
        return iFalse; /* Refresh is already ongoing. */
    }
    /* Queue up all the subscriptions for the worker. */
    iConstForEach(PtrArray, i, listSubscriptions_()) {
        const iBookmark *bm = i.ptr;
        iFeedJob *job = new_FeedJob(bm);
        if (!contains_IntSet(&d->previouslyCheckedFeeds, id_Bookmark(bm))) {
            job->isFirstUpdate = iTrue;
//            printf("first check of %x: %s\n", id_Bookmark(bm), cstr_String(&bm->title));
//            fflush(stdout);
            insert_IntSet(&d->previouslyCheckedFeeds, id_Bookmark(bm));
        }
        pushBack_PtrArray(&d->jobs, job);
    }
    if (!isEmpty_Array(&d->jobs)) {
        d->worker = new_Thread(fetch_Feeds_);
        d->stopWorker = iFalse;
        start_Thread(d->worker);
        return iTrue;
    }
    return iFalse;
}

static uint32_t refresh_Feeds_(uint32_t interval, void *data) {
    /* Called in the SDL timer thread, so let's start a worker thread for running the update. */
    startWorker_Feeds_(&feeds_);
    return 1000 * updateIntervalSeconds_Feeds_;
}

static void stopWorker_Feeds_(iFeeds *d) {
    if (d->worker) {
        d->stopWorker = iTrue;
        join_Thread(d->worker);
        iReleasePtr(&d->worker);
    }
    /* Clear remaining jobs. */
    iForEach(PtrArray, i, &d->jobs) {
        delete_FeedJob(i.ptr);
    }
    clear_PtrArray(&d->jobs);
}

static int cmp_FeedEntryPtr_(const void *a, const void *b) {
    const iFeedEntry * const *elem[2] = { a, b };
    const int cmp = cmpString_String(&(*elem[0])->url, &(*elem[1])->url);
    if (cmp == 0) {
        /* The same URL can be coming from different feeds. */
        return iCmp((*elem[0])->bookmarkId, (*elem[1])->bookmarkId);
    }
    return cmp;
}

iDeclareType(FeedHashNode)

struct Impl_FeedHashNode {
    iHashNode node;
    uint32_t  bookmarkId;
};

static void load_Feeds_(iFeeds *d) {
    /* TODO: If there are lots of entries, it would make sense to load async. */
    iFile *f = new_File(collect_String(concatCStr_Path(&d->saveDir, feedsFilename_Feeds_)));
    if (open_File(f, read_FileMode | text_FileMode)) {
        iBlock * src     = readAll_File(f);
        iRangecc line    = iNullRange;
        int      section = 0;
        iHash *  feeds   = new_Hash(); /* mapping from IDs to feed URLs */
        while (nextSplit_Rangecc(range_Block(src), "\n", &line)) {
            if (equal_Rangecc(line, "# Feeds")) {
                section = 1;
                continue;
            }
            else if (equal_Rangecc(line, "# Entries")) {
                section = 2;
                continue;
            }
            switch (section) {
                case 0: {
                    unsigned long long ts = 0;
                    sscanf(line.start, "%llu", &ts);
                    d->lastRefreshedAt.ts.tv_sec = ts;
                    break;
                }
                case 1: {
                    if (size_Range(&line) > 8) {
                        uint32_t id = 0;
                        sscanf(line.start, "%08x", &id);
                        iString *feedUrl =
                            collect_String(newRange_String((iRangecc){ line.start + 9, line.end }));
                        const uint32_t bookmarkId = findUrl_Bookmarks(bookmarks_App(), feedUrl);
                        if (bookmarkId) {
                            iFeedHashNode *node = iMalloc(FeedHashNode);
                            node->node.key      = id;
                            node->bookmarkId    = bookmarkId;
                            insert_Hash(feeds, &node->node);
                            insert_IntSet(&d->previouslyCheckedFeeds, bookmarkId);
                        }
                    }
                    break;
                }
                case 2: {
                    /* TODO: Cleanup needed...
                       All right, this could maybe use a bit more robust, structured format.
                       The code below is messy. */
                    const uint32_t feedId = (uint32_t) strtoul(line.start, NULL, 16);
                    if (!nextSplit_Rangecc(range_Block(src), "\n", &line)) {
                        goto aborted;
                    }
                    const unsigned long long posted = strtoull(line.start, NULL, 10);
                    if (posted == 0) {
                        goto aborted;
                    }
                    if (!nextSplit_Rangecc(range_Block(src), "\n", &line)) {
                        goto aborted;
                    }
                    char *endp = NULL;
                    const unsigned long long discovered = strtoull(line.start, &endp, 10);
                    if (endp != line.end) {
                        goto aborted;
                    }
                    if (!nextSplit_Rangecc(range_Block(src), "\n", &line)) {
                        goto aborted;
                    }
                    const iRangecc urlRange = line;
                    if (!nextSplit_Rangecc(range_Block(src), "\n", &line)) {
                        goto aborted;
                    }
                    const iRangecc titleRange = line;
                    iString *url   = newRange_String(urlRange);
                    iString *title = newRange_String(titleRange);
                    /* Look it up in the hash. */
                    const iFeedHashNode *node = (iFeedHashNode *) value_Hash(feeds, feedId);
                    if (node) {
                        iFeedEntry *entry = new_FeedEntry();
                        entry->bookmarkId           = node->bookmarkId;
                        entry->posted.ts.tv_sec     = posted;
                        entry->discovered.ts.tv_sec = discovered;
                        set_String(&entry->url, url);
                        stripDefaultUrlPort_String(&entry->url);
                        set_String(&entry->url, canonicalUrl_String(&entry->url));
                        set_String(&entry->title, title);
                        entry->isHeading = isHeadingEntry_FeedEntry_(entry);
//                        if (entry->isHeading) {
//                            printf("[Feeds] src:%d url:{%s}\n", entry->bookmarkId,
//                                   cstr_String(&entry->url));
//                        }
                        insert_SortedArray(&d->entries, &entry);
                    }
                    delete_String(title);
                    delete_String(url);
                    break;
                }
            }
        }
    aborted:
        /* Cleanup. */
        delete_Block(src);
        iForEach(Hash, i, feeds) {
            free(i.value);
        }
        delete_Hash(feeds);
    }
    iRelease(f);
}

/*----------------------------------------------------------------------------------------------*/

void init_Feeds(const char *saveDir) {
    iFeeds *d = &feeds_;
    d->mtx = new_Mutex();
    initCStr_String(&d->saveDir, saveDir);
    init_IntSet(&d->previouslyCheckedFeeds);
    iZap(d->lastRefreshedAt);
    d->worker = NULL;
    init_PtrArray(&d->jobs);
    init_SortedArray(&d->entries, sizeof(iFeedEntry *), cmp_FeedEntryPtr_);
    load_Feeds_(d);
    /* Update feeds if it has been a while. */
    int intervalSec = updateIntervalSeconds_Feeds_;
    if (isValid_Time(&d->lastRefreshedAt)) {
        const double elapsed = elapsedSeconds_Time(&d->lastRefreshedAt);
        intervalSec = iMax(1, updateIntervalSeconds_Feeds_ - elapsed);
    }
    d->refreshTimer = SDL_AddTimer(1000 * intervalSec, refresh_Feeds_, NULL);
}

void deinit_Feeds(void) {
    iFeeds *d = &feeds_;
    SDL_RemoveTimer(d->refreshTimer);
    stopWorker_Feeds_(d);
    iAssert(isEmpty_PtrArray(&d->jobs));
    deinit_PtrArray(&d->jobs);
    deinit_String(&d->saveDir);
    delete_Mutex(d->mtx);
    iForEach(Array, i, &d->entries.values) {
        iFeedEntry **entry = i.value;
        delete_FeedEntry(*entry);
    }
    deinit_IntSet(&d->previouslyCheckedFeeds);
    deinit_SortedArray(&d->entries);
}

void refresh_Feeds(void) {
    startWorker_Feeds_(&feeds_);
}

void refreshFinished_Feeds(void) {
    stopWorker_Feeds_(&feeds_);
}

void removeEntries_Feeds(uint32_t feedBookmarkId) {
    iFeeds *d = &feeds_;
    iForEach(Array, i, &d->entries.values) {
        iFeedEntry **entry = i.value;
        if ((*entry)->bookmarkId == feedBookmarkId) {
            delete_FeedEntry(*entry);
            remove_ArrayIterator(&i);
        }
    }
}

void markEntryAsRead_Feeds(uint32_t feedBookmarkId, const iString *entryUrl, iBool isRead) {
    const iBookmark *bm = get_Bookmarks(bookmarks_App(), feedBookmarkId);
    if (bm) {
        iFeeds *d = &feeds_;
        iVisited *vis = visited_App();
        if (bm->flags & headings_BookmarkFlag) {
            iTime oneSecond;
            initSeconds_Time(&oneSecond, 1.0);
            const iString *url = urlFragmentStripped_String(entryUrl);
            /* The unread state of headings is tracked based on the last visit time. */
            const iFeedEntry entry = { .url = *entryUrl, .bookmarkId = feedBookmarkId };
            const iFeedEntry *entryPtr = &entry;
            size_t pos;
            lock_Mutex(d->mtx);
            if (locate_SortedArray(&d->entries, &entryPtr, &pos)) {
                const iFeedEntry *entry = *(const iFeedEntry **) at_SortedArray(&d->entries, pos);
                if (isRead && !isUnread_FeedEntry(entry)) {
                    unlock_Mutex(d->mtx);
                    return;
                }
                iTime entryTime = entry->posted;
                unlock_Mutex(d->mtx);
                if (!isRead) {
                    sub_Time(&entryTime, &oneSecond);
                }
                visitUrlTime_Visited(
                    vis, url, transient_VisitedUrlFlag | kept_VisitedUrlFlag, entryTime);
            }
            else {
                unlock_Mutex(d->mtx);
            }            
        }
        else {
            /* The unread state depends on whether the URL has been visited. */
            if (!isRead && containsUrl_Visited(vis, entryUrl)) {
                removeUrl_Visited(vis, entryUrl);
            }
            else if (isRead) {
                visitUrl_Visited(vis, entryUrl, transient_VisitedUrlFlag | kept_VisitedUrlFlag);
            }            
        }
    }
}

iBool isUnreadEntry_Feeds(uint32_t feedBookmarkId, const iString *entryUrl) {
    iBool isUnread = iFalse;
    iFeeds *d = &feeds_;
    lock_Mutex(d->mtx);
    iFeedEntry entry = { .url = *entryUrl, .bookmarkId = feedBookmarkId };
    iFeedEntry *entryPtr = &entry;
    size_t pos;
    if (locate_SortedArray(&d->entries, &entryPtr, &pos)) {
        isUnread = isUnread_FeedEntry(*(iFeedEntry **) at_SortedArray(&d->entries, pos));
    }
    unlock_Mutex(d->mtx);
    return isUnread;
}

static int cmpTimeDescending_FeedEntryPtr_(const void *a, const void *b) {
    const iFeedEntry * const *e1 = a, * const *e2 = b;
    const int cmpPosted = -cmp_Time(&(*e1)->posted, &(*e2)->posted);
    if (cmpPosted) return cmpPosted;
    /* Posting timestamps may only be accurate to a day, so also sort by discovery time. */
    return -cmp_Time(&(*e1)->discovered, &(*e2)->discovered);
}

const iPtrArray *listEntries_Feeds(void) {
    iFeeds *d = &feeds_;
    lock_Mutex(d->mtx);
    /* The worker will never delete feed entries so we can use the same ones. Just make a copy
       of the array in case the worker modifies it. */
    iPtrArray *list = collect_PtrArray(copy_Array(&d->entries.values));
    unlock_Mutex(d->mtx);
    sort_Array(list, cmpTimeDescending_FeedEntryPtr_);
    return list;
}

size_t numSubscribed_Feeds(void) {
    return size_PtrArray(listSubscriptions_());
}

size_t numUnread_Feeds(void) {
    size_t count = 0;
    size_t max = 100; /* match the number of items shown in the sidebar */
    iConstForEach(PtrArray, i, listEntries_Feeds()) {
        if (!max--) break;
        const iFeedEntry *entry = i.ptr;
        if (isValid_Time(&entry->discovered) && isUnread_FeedEntry(i.ptr)) {
            count++;
        }
    }
    return count;
}

const iString *entryListPage_Feeds(void) {
    iFeeds *d = &feeds_;
    iString *src = collectNew_String();
    setCStr_String(src, translateCStr_Lang("# ${feeds.list.title}\n\n"));
    lock_Mutex(d->mtx);
    const iPtrArray *subs = listSubscriptions_();
    const int elapsed = elapsedSeconds_Time(&d->lastRefreshedAt) / 60;
    appendFormat_String(
        src,
        formatCStrs_Lang("feeds.list.counts.n", size_PtrArray(subs)),
        formatCStrs_Lang("feeds.list.entrycount.n", size_SortedArray(&d->entries)));
    if (isValid_Time(&d->lastRefreshedAt)) {
        if (elapsed == 0) {
            appendCStr_String(src, translateCStr_Lang("\n${feeds.list.refreshtime.now}\n"));
        }
        else {
            appendFormat_String(src,
                                translateCStr_Lang("\n${feeds.list.refreshtime}\n"),
                                elapsed < 60     ? formatCStr_Lang("minutes.ago.n", elapsed)
                                : elapsed < 1440 ? formatCStr_Lang("hours.ago.n", elapsed / 60)
                                                 : formatCStr_Lang("days.ago.n", elapsed / 1440));
        }
    }
    iDate on;
    iZap(on);
    iConstForEach(PtrArray, i, listEntries_Feeds()) {
        const iFeedEntry *entry = i.ptr;
        if (isHidden_FeedEntry(entry)) {
            continue; /* A hidden entry. */
        }
        iDate entryDate;
        init_Date(&entryDate, &entry->posted);
        if (on.year != entryDate.year || on.month != entryDate.month || on.day != entryDate.day) {
            appendFormat_String(
                src, "## %s\n", cstrCollect_String(format_Date(&entryDate, "%Y-%m-%d")));
            on = entryDate;
        }
        const iBookmark *bm = get_Bookmarks(bookmarks_App(), entry->bookmarkId);
        if (bm) {
            appendFormat_String(src,
                                "=> %s %s - %s\n",
                                cstr_String(&entry->url),
                                cstr_String(&bm->title),
                                cstr_String(&entry->title));
        }
    }
    unlock_Mutex(d->mtx);
    return src;
}
