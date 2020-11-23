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
#include "app.h"

#include <the_Foundation/file.h>
#include <the_Foundation/mutex.h>
#include <the_Foundation/hash.h>
#include <the_Foundation/queue.h>
#include <the_Foundation/path.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/stringset.h>
#include <the_Foundation/thread.h>
#include <SDL_timer.h>

iDeclareType(Feeds)
iDeclareType(FeedJob)

iDefineTypeConstruction(FeedEntry)

void init_FeedEntry(iFeedEntry *d) {
    iZap(d->timestamp);
    init_String(&d->url);
    init_String(&d->title);
    d->bookmarkId = 0;
}

void deinit_FeedEntry(iFeedEntry *d) {
    deinit_String(&d->title);
    deinit_String(&d->url);
}

/*----------------------------------------------------------------------------------------------*/

struct Impl_FeedJob {
    iString     url;
    uint32_t    bookmarkId;
    iTime       startTime;
    iGmRequest *request;
    iPtrArray   results;
};

static void init_FeedJob(iFeedJob *d, const iBookmark *bookmark) {
    initCopy_String(&d->url, &bookmark->url);
    d->bookmarkId = id_Bookmark(bookmark);
    d->request = NULL;
    init_PtrArray(&d->results);
    iZap(d->startTime);
}

static void deinit_FeedJob(iFeedJob *d) {
    iRelease(d->request);
    iForEach(PtrArray, i, &d->results) {
        delete_FeedEntry(i.ptr);
    }
    deinit_PtrArray(&d->results);
    deinit_String(&d->url);
}

iDefineTypeConstructionArgs(FeedJob, (const iBookmark *bm), bm)

/*----------------------------------------------------------------------------------------------*/

static const char *feedsFilename_Feeds_ = "feeds.txt";

struct Impl_Feeds {
    iMutex *  mtx;
    iString   saveDir;
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

static iFeedJob *startNextJob_Feeds_(iFeeds *d) {
    iFeedJob *job;
    if (take_PtrArray(&d->jobs, 0, (void **) &job)) {
        submit_FeedJob_(job);
        return job;
    }
    return NULL;
}

static void parseResult_FeedJob_(iFeedJob *d) {
    /* TODO: Should tell the user if the request failed. */
    if (isSuccess_GmStatusCode(status_GmRequest(d->request))) {
        iBeginCollect();
        iRegExp *linkPattern =
            new_RegExp("^=>\\s*([^\\s]+)\\s+"
                       "([0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9])"
                       "([^0-9].*)",
                       0);
        iString src;
        initBlock_String(&src, body_GmRequest(d->request));
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
                iFeedEntry *entry = new_FeedEntry();
                entry->bookmarkId = d->bookmarkId;
                setRange_String(&entry->url, url);
                set_String(&entry->url, collect_String(lower_String(&entry->url)));
                setRange_String(&entry->title, title);
                int year, month, day;
                sscanf(date.start, "%04d-%02d-%02d", &year, &month, &day);
                init_Time(
                    &entry->timestamp,
                    &(iDate){
                        .year = year, .month = month, .day = day, .hour = 12 /* noon UTC */ });
                pushBack_PtrArray(&d->results, entry);
            }
        }
        deinit_String(&src);
        iRelease(linkPattern);
        iEndCollect();
    }
}

static iBool updateEntries_Feeds_(iFeeds *d, iPtrArray *incoming) {
    iBool gotNew = iFalse;
    lock_Mutex(d->mtx);
    iForEach(PtrArray, i, incoming) {
        iFeedEntry *entry = i.ptr;
        size_t pos;
        if (locate_SortedArray(&d->entries, &entry, &pos)) {
            iFeedEntry *existing = *(iFeedEntry **) at_SortedArray(&d->entries, pos);
            /* Already known, but update it, maybe the time and label have changed. */
            iBool changed = iFalse;
            if (!equalCase_String(&existing->title, &entry->title) ||
                cmp_Time(&existing->timestamp, &entry->timestamp)) {
                changed = iTrue;
            }
            set_String(&existing->title, &entry->title);
            existing->timestamp = entry->timestamp;
            delete_FeedEntry(entry);
            if (changed) {
                /* TODO: better to use a new flag for read feed entries? */
                removeUrl_Visited(visited_App(), &entry->url);
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
    return gotNew;
}

static iThreadResult fetch_Feeds_(iThread *thread) {
    iFeeds *d = &feeds_;
    iUnused(thread);
    iFeedJob *work[maxConcurrentRequests_Feeds]; /* We'll do a couple of concurrent requests. */
    iZap(work);
    iBool gotNew = iFalse;
    postCommand_App("feeds.update.started");
    while (!d->stopWorker) {
        /* Start new jobs. */
        iForIndices(i, work) {
            if (!work[i]) {
                work[i] = startNextJob_Feeds_(d);
            }
        }
        sleep_Thread(0.5); /* TODO: wait on a Condition so we can exit quickly */
        size_t ongoing = 0;
        iForIndices(i, work) {
            if (work[i]) {
                if (isFinished_GmRequest(work[i]->request)) {
                    /* TODO: Handle redirects. Need to resubmit the job with new URL. */
                    parseResult_FeedJob_(work[i]);
                    gotNew |= updateEntries_Feeds_(d, &work[i]->results);
                    delete_FeedJob(work[i]);
                    work[i] = NULL;
                }
                else {
                    ongoing++;
                }
                /* TODO: abort job if it takes too long (> 15 seconds?) */
            }
        }
        /* Stop if everything has finished. */
        if (ongoing == 0 && isEmpty_PtrArray(&d->jobs)) {
            break;
        }
    }
    postCommandf_App("feeds.update.finished arg:%d", gotNew ? 1 : 0);
    initCurrent_Time(&d->lastRefreshedAt);
    return 0;
}

static iBool isSubscribed_(void *context, const iBookmark *bm) {
    iUnused(context);
    return indexOfCStr_String(&bm->tags, "subscribed") != iInvalidPos; /* TODO: RegExp with \b */
}

static iBool startWorker_Feeds_(iFeeds *d) {
    if (d->worker) {
        return iFalse; /* Oops? */
    }
    d->worker = new_Thread(fetch_Feeds_);
    /* Queue up all the subscriptions for the worker. */
    iConstForEach(PtrArray, i, list_Bookmarks(bookmarks_App(), NULL, isSubscribed_, NULL)) {
        iFeedJob job;
        init_FeedJob(&job, i.ptr);
        pushBack_Array(&d->jobs, &job);
    }
    d->stopWorker = iFalse;
    start_Thread(d->worker);
    return iTrue;
}

static uint32_t refresh_Feeds_(uint32_t interval, void *data) {
    /* Called in the SDL timer thread, so let's start a worker thread for running the update. */
    startWorker_Feeds_(&feeds_);
    return 1000 * 60 * 60;
}

static void stopWorker_Feeds_(iFeeds *d) {
    if (d->worker) {
        d->stopWorker = iTrue;
        join_Thread(d->worker);
        iReleasePtr(&d->worker);
    }
    /* TODO: Clear jobs */
}

static int cmp_FeedEntryPtr_(const void *a, const void *b) {
    const iFeedEntry * const *elem[2] = { a, b };
    return cmpStringCase_String(&(*elem[0])->url, &(*elem[1])->url);
}

static void save_Feeds_(iFeeds *d) {
    iFile *f = new_File(collect_String(concatCStr_Path(&d->saveDir, feedsFilename_Feeds_)));
    if (open_File(f, write_FileMode | text_FileMode)) {
        lock_Mutex(d->mtx);
        iString *str = new_String();
        format_String(str, "%llu\n# Feeds\n", integralSeconds_Time(&d->lastRefreshedAt));
        write_File(f, utf8_String(str));
        /* Index of feeds for IDs. */ {
            iConstForEach(PtrArray, i, list_Bookmarks(bookmarks_App(), NULL, isSubscribed_, NULL)) {
                const iBookmark *bm = i.ptr;
                format_String(str, "%08x %s\n", id_Bookmark(bm), cstr_String(&bm->url));
                write_File(f, utf8_String(str));
            }
        }
        writeData_File(f, "# Entries\n", 10);
        iConstForEach(Array, i, &d->entries.values) {
            const iFeedEntry *entry = *(const iFeedEntry **) i.value;
            format_String(str, "%x\n%llu\n%s\n%s\n",
                          entry->bookmarkId,
                          integralSeconds_Time(&entry->timestamp),
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

iDeclareType(FeedHashNode)

struct Impl_FeedHashNode {
    iHashNode node;
    uint32_t bookmarkId;
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
                        }
                    }
                    break;
                }
                case 2: {
                    const uint32_t feedId = strtoul(line.start, NULL, 16);
                    nextSplit_Rangecc(range_Block(src), "\n", &line);
                    const unsigned long long ts = strtoull(line.start, NULL, 10);
                    nextSplit_Rangecc(range_Block(src), "\n", &line);
                    iString url;
                    initRange_String(&url, line);
                    nextSplit_Rangecc(range_Block(src), "\n", &line);
                    iString title;
                    initRange_String(&title, line);
                    nextSplit_Rangecc(range_Block(src), "\n", &line);
                    /* Look it up in the hash. */
                    const iFeedHashNode *node = (iFeedHashNode *) value_Hash(feeds, feedId);
                    if (node) {
                        iFeedEntry *entry = new_FeedEntry();
                        entry->bookmarkId = node->bookmarkId;
                        entry->timestamp.ts.tv_sec = ts;
                        set_String(&entry->url, &url);
                        set_String(&entry->title, &title);
                        insert_SortedArray(&d->entries, &entry);
                    }
                    deinit_String(&title);
                    deinit_String(&url);
                    break;
                }
            }
        }
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
    iZap(d->lastRefreshedAt);
    d->worker = NULL;
    init_PtrArray(&d->jobs);
    init_SortedArray(&d->entries, sizeof(iFeedEntry *), cmp_FeedEntryPtr_);
    load_Feeds_(d);
    d->refreshTimer = SDL_AddTimer(5000, refresh_Feeds_, NULL);
}

void deinit_Feeds(void) {
    iFeeds *d = &feeds_;
    SDL_RemoveTimer(d->refreshTimer);
    stopWorker_Feeds_(d);
    iAssert(isEmpty_PtrArray(&d->jobs));
    deinit_PtrArray(&d->jobs);
    save_Feeds_(d);
    deinit_String(&d->saveDir);
    delete_Mutex(d->mtx);
    iForEach(Array, i, &d->entries.values) {
        iFeedEntry **entry = i.value;
        delete_FeedEntry(*entry);
    }
    deinit_SortedArray(&d->entries);
}

const iPtrArray *listEntries_Feeds(void) {
    iFeeds *d = &feeds_;
    lock_Mutex(d->mtx);
    /* The worker will never delete feed entries so we can use the same ones. Just make a copy
       of the array in case the worker modifies it. */
    iPtrArray *list = collect_PtrArray(copy_Array(&d->entries.values));
    unlock_Mutex(d->mtx);
    /* TODO: Sort the entries based on time. */
    return list;
}
