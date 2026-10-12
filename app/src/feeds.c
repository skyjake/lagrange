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
#include "tinylog.h"
#include <lagrange/core.h>
#include <lagrange/gmrequest.h>
#include <lagrange/gmutil.h>
#include <lagrange/visited.h>
#include <lagrange/lang.h>
#include "app.h"
#include "sync.h"
#include "syncdata.h"

#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/hash.h>
#include <the_Foundation/intset.h>
#include <the_Foundation/mutex.h>
#include <the_Foundation/path.h>
#include <the_Foundation/queue.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/stringset.h>
#include <the_Foundation/thread.h>
#include <SDL3/SDL_timer.h>
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
    d->isTinylog = iFalse;
    d->sourceRange = (iRangei){ 0, 0 };
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
    iString     feedUrl; /* subscribed URL (before redirects) */
    uint32_t    bookmarkId;
    iTime       startTime;
    iBool       isFirstUpdate; /* hasn't been checked ever before */
    iBool       checkHeadings;
    iBool       ignoreWeb;
    iBool       isTinylog;
    iBool       autoDetect;
    iBool       pendingTitle;
    iGmRequest *request;
    int         numRedirect;
    iPtrArray   results;
    iString     title;          /* from the fetched page */
    iString     tinylogSource;  /* to be cached; empty if not valid */
    iString     tinylogBase;
    iString     tinylogAuthor;
    iString     tinylogAvatar;
};

static void init_FeedJob(iFeedJob *d, const iBookmark *bookmark) {
    initCopy_String(&d->url, &bookmark->url);
    initCopy_String(&d->feedUrl, &bookmark->url);
    d->bookmarkId = id_Bookmark(bookmark);
    d->request = NULL;
    d->numRedirect = 0;
    init_PtrArray(&d->results);
    iZap(d->startTime);
    d->isFirstUpdate = iFalse;
    d->autoDetect    = (bookmark->flags & autoDetect_BookmarkFlag) != 0;
    d->pendingTitle  = (bookmark->flags & pendingTitle_BookmarkFlag) != 0;
    d->isTinylog     = !d->autoDetect && (bookmark->flags & tinylog_BookmarkFlag) != 0;
    d->checkHeadings = !d->autoDetect && !d->isTinylog &&
                       (bookmark->flags & headings_BookmarkFlag) != 0;
    d->ignoreWeb     = (bookmark->flags & ignoreWeb_BookmarkFlag) != 0;
    init_String(&d->title);
    init_String(&d->tinylogSource);
    init_String(&d->tinylogBase);
    init_String(&d->tinylogAuthor);
    init_String(&d->tinylogAvatar);
}

static void deinit_FeedJob(iFeedJob *d) {
    iRelease(d->request);
    iForEach(PtrArray, i, &d->results) {
        delete_FeedEntry(i.ptr);
    }
    deinit_PtrArray(&d->results);
    deinit_String(&d->tinylogAvatar);
    deinit_String(&d->tinylogAuthor);
    deinit_String(&d->tinylogBase);
    deinit_String(&d->tinylogSource);
    deinit_String(&d->title);
    deinit_String(&d->feedUrl);
    deinit_String(&d->url);
}

static iBool isTimedOut_FeedJob_(iFeedJob *d) {
    return elapsedSeconds_Time(&d->startTime) > requestTimeoutSeconds_FeedJob_;
}

iDefineTypeConstructionArgs(FeedJob, (const iBookmark *bm), bm)

/*----------------------------------------------------------------------------------------------*/

static const char *feedsFilename_Feeds_ = "feeds.txt";

struct Impl_Feeds {
    iMutex *  mtx;
    iString   saveDir;
    iIntSet   previouslyCheckedFeeds; /* bookmark IDs */
    iTime     lastRefreshedAt;
    int       refreshTimer;
    uint32_t  refreshInterval; /* milliseconds, for refreshTimer */
    iThread * worker;
    iBool     stopWorker;
    iPtrArray jobs; /* pending */
    iSortedArray entries; /* pointers to all discovered feed entries, sorted by entry ID (URL) */
    iPtrArray tinylogFeeds; /* TinylogFeed */
};

iDeclareType(TinylogFeed)

struct Impl_TinylogFeed {
    iString feedUrl;   /* canonical */
    iString base;      /* entry URI base before resolving collisions */
    iString sourceUrl; /* final URL of the source, for resolving links */
    iString author;
    iString avatar;
};

static iFeeds feeds_;

#define maxConcurrentRequests_Feeds 10

static iBool isInitialized_Feeds_(const iFeeds *d) {
    return d->mtx != NULL;
}

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

static uint32_t findSubscription_Feeds_(const iString *url) {
    /* Bookmarks may have the same URL, and subscriptions may use an identity. */
    url = canonicalUrl_String(url);
    iConstForEach(PtrArray, i, listSubscriptions_()) {
        const iBookmark *bm = i.ptr;
        if (equalCase_String(url, canonicalUrl_String(&bm->url))) {
            return id_Bookmark(bm);
        }
    }
    return 0;
}

/*----------------------------------------------------------------------------------------------*/
/* Tinylog cache */

static const char *tinylogDirName_Feeds_ = "tinylog";

iDeclareType(TinylogCache)

struct Impl_TinylogCache {
    iString url;  /* final URL of the source, for resolving links */
    iString base; /* entry URI base before resolving collisions */
    iString author;
    iString avatar;
    iString source;
    size_t  sourcePos; /* offset of the source in the file */
};

static void init_TinylogCache_(iTinylogCache *d) {
    init_String(&d->url);
    init_String(&d->base);
    init_String(&d->author);
    init_String(&d->avatar);
    init_String(&d->source);
    d->sourcePos = 0;
}

static void deinit_TinylogCache_(iTinylogCache *d) {
    deinit_String(&d->source);
    deinit_String(&d->avatar);
    deinit_String(&d->author);
    deinit_String(&d->base);
    deinit_String(&d->url);
}

static const iString *tinylogCacheHeader_(const iString *url, const iString *base,
                                          const iString *author, const iString *avatar) {
    /* Files are read and written as binary so entry offsets are exact. */
    return collectNewFormat_String("url: %s\nbase: %s\nauthor: %s\navatar: %s\n\n",
                                   cstr_String(url),
                                   cstr_String(base),
                                   cstr_String(author),
                                   cstr_String(avatar));
}

static const iString *tinylogDir_Feeds_(const iFeeds *d) {
    return collect_String(concatCStr_Path(&d->saveDir, tinylogDirName_Feeds_));
}

static const iString *tinylogCacheName_(const iString *feedUrl) {
    uint8_t md5[16];
    md5_Block(utf8_String(canonicalUrl_String(feedUrl)), md5);
    iString *name = collectNew_String();
    for (size_t i = 0; i < 8; i++) {
        appendFormat_String(name, "%02x", md5[i]);
    }
    appendCStr_String(name, ".gmi");
    return name;
}

static const iString *tinylogCachePath_Feeds_(const iFeeds *d, const iString *feedUrl) {
    return collect_String(concat_Path(tinylogDir_Feeds_(d), tinylogCacheName_(feedUrl)));
}

static iBool loadTinylogCache_Feeds_(const iFeeds *d, const iString *feedUrl,
                                     iTinylogCache *cache, iBool headerOnly) {
    iBool  ok = iFalse;
    iFile *f  = new_File(tinylogCachePath_Feeds_(d, feedUrl));
    if (open_File(f, readOnly_FileMode)) {
        iBlock        *data = headerOnly ? read_File(f, 2048) : readAll_File(f);
        const iRangecc src  = range_Block(data);
        iRangecc       line = iNullRange;
        while (nextSplit_Rangecc(src, "\n", &line)) {
            if (isEmpty_Range(&line)) {
                const char *sourceStart = iMin(line.end + 1, src.end);
                cache->sourcePos = sourceStart - src.start;
                if (!headerOnly) {
                    setRange_String(&cache->source, (iRangecc){ sourceStart, src.end });
                }
                ok = !isEmpty_String(&cache->url);
                break;
            }
            const char *sep = strchr(line.start, ' ');
            if (!sep || sep >= line.end) {
                continue;
            }
            const iRangecc key   = { line.start, sep };
            const iRangecc value = { sep + 1, line.end };
            if (equal_Rangecc(key, "url:")) {
                setRange_String(&cache->url, value);
            }
            else if (equal_Rangecc(key, "base:")) {
                setRange_String(&cache->base, value);
            }
            else if (equal_Rangecc(key, "author:")) {
                setRange_String(&cache->author, value);
            }
            else if (equal_Rangecc(key, "avatar:")) {
                setRange_String(&cache->avatar, value);
            }
        }
        delete_Block(data);
    }
    iRelease(f);
    return ok;
}

static void saveTinylogCache_Feeds_(const iFeeds *d, const iFeedJob *job) {
    makeDirs_Path(tinylogDir_Feeds_(d));
    const iString *path    = tinylogCachePath_Feeds_(d, &job->feedUrl);
    iString       *content = collect_String(copy_String(tinylogCacheHeader_(
        &job->url, &job->tinylogBase, &job->tinylogAuthor, &job->tinylogAvatar)));
    append_String(content, &job->tinylogSource);
    iBool  isUnchanged = iFalse;
    iFile *f           = new_File(path);
    if (open_File(f, readOnly_FileMode)) {
        iBlock *old = readAll_File(f);
        isUnchanged = cmp_Block(old, utf8_String(content)) == 0;
        delete_Block(old);
    }
    iRelease(f);
    if (!isUnchanged) {
        const iString *tmpPath = collectNewFormat_String("%s.tmp", cstr_String(path));
        f = new_File(tmpPath);
        if (open_File(f, write_FileMode)) {
            write_File(f, utf8_String(content));
            close_File(f);
            commitFile_Core(cstr_String(path), cstr_String(tmpPath));
        }
        iRelease(f);
    }
}

static iTinylogFeed *findTinylogFeed_Feeds_(const iFeeds *d, const iString *feedUrl) {
    /* Caller must lock. */
    feedUrl = canonicalUrl_String(feedUrl);
    iConstForEach(PtrArray, i, &d->tinylogFeeds) {
        iTinylogFeed *tf = i.ptr;
        if (equalCase_String(&tf->feedUrl, feedUrl)) {
            return tf;
        }
    }
    return NULL;
}

static const iString *tinylogBase_Feeds_(const iFeeds *d, const iString *feedUrl) {
    const iTinylogFeed *tf = findTinylogFeed_Feeds_(d, feedUrl);
    return tf ? &tf->base : NULL;
}

static void setTinylogFeed_Feeds_(iFeeds *d, const iString *feedUrl, const iTinylogCache *info) {
    /* Caller must lock. */
    iTinylogFeed *tf = findTinylogFeed_Feeds_(d, feedUrl);
    if (!tf) {
        tf = iMalloc(TinylogFeed);
        initCopy_String(&tf->feedUrl, canonicalUrl_String(feedUrl));
        init_String(&tf->base);
        init_String(&tf->sourceUrl);
        init_String(&tf->author);
        init_String(&tf->avatar);
        pushBack_PtrArray(&d->tinylogFeeds, tf);
    }
    set_String(&tf->base, &info->base);
    set_String(&tf->sourceUrl, &info->url);
    set_String(&tf->author, &info->author);
    set_String(&tf->avatar, &info->avatar);
}

static iString *tinylogPrefix_Feeds_(const iFeeds *d, const iString *feedUrl,
                                     const iString *base) {
    /* The earliest created subscription gets the base as-is, others are disambiguated. */
    const iBookmark *first = NULL;
    iConstForEach(PtrArray, i, listSubscriptions_()) {
        const iBookmark *bm = i.ptr;
        if (!(bm->flags & (tinylog_BookmarkFlag | autoDetect_BookmarkFlag))) {
            continue;
        }
        const iString *bmBase = tinylogBase_Feeds_(d, &bm->url);
        if (bmBase && equal_String(bmBase, base) &&
            (!first || cmp_Time(&bm->when, &first->when) < 0)) {
            first = bm;
        }
    }
    iString *prefix = copy_String(base);
    if (first &&
        !equalCase_String(canonicalUrl_String(&first->url), canonicalUrl_String(feedUrl))) {
        appendCStr_String(prefix, "-");
        append_String(prefix, collect_String(pathSlug_Tinylog(feedUrl)));
    }
    return prefix;
}

static void loadTinylogFeeds_Feeds_(iFeeds *d) {
    iConstForEach(PtrArray, i, listSubscriptions_()) {
        const iBookmark *bm = i.ptr;
        if (bm->flags & tinylog_BookmarkFlag) {
            iTinylogCache cache;
            init_TinylogCache_(&cache);
            if (loadTinylogCache_Feeds_(d, &bm->url, &cache, iTrue)) {
                setTinylogFeed_Feeds_(d, &bm->url, &cache);
            }
            deinit_TinylogCache_(&cache);
        }
    }
}

static void removeTinylogCaches_Feeds_(const iFeeds *d, iBool unusedOnly) {
    iStringSet *used = new_StringSet();
    if (unusedOnly) {
        iConstForEach(PtrArray, i, listSubscriptions_()) {
            const iBookmark *bm = i.ptr;
            if (bm->flags & (tinylog_BookmarkFlag | autoDetect_BookmarkFlag)) {
                insert_StringSet(used, tinylogCacheName_(&bm->url));
            }
        }
    }
    iForEach(DirFileInfo, i, iClob(new_DirFileInfo(tinylogDir_Feeds_(d)))) {
        const iString *path = path_FileInfo(i.value);
        const iRangecc name = baseName_Path(path);
        if (endsWith_Rangecc(name, ".gmi") &&
            !contains_StringSet(used, collectNewRange_String(name))) {
            removePath_CStr(cstr_String(path));
        }
    }
    iRelease(used);
}

/*----------------------------------------------------------------------------------------------*/

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

static void parseTitle_FeedJob_(iFeedJob *d, const iString *src) {
    iRangecc line  = iNullRange;
    iBool    isPre = iFalse;
    while (nextSplit_Rangecc(range_String(src), "\n", &line)) {
        if (startsWith_Rangecc(line, "```")) {
            isPre = !isPre;
        }
        else if (!isPre && startsWith_Rangecc(line, "#") && !startsWith_Rangecc(line, "##")) {
            iRangecc title = { line.start + 1, line.end };
            trim_Rangecc(&title);
            setRange_String(&d->title, title);
            break;
        }
    }
}

static void parseTinylog_FeedJob_(iFeedJob *d, const iString *src) {
    const size_t maxSize = (size_t) iMax(1, prefs_App()->maxTinylogSize) * 1024;
    iString     *source  = truncated_Tinylog(src, maxSize);
    iTinylog    *log     = new_Tinylog();
    parse_Tinylog(log, source, &d->feedUrl);
    if (!isEmpty_Array(&log->entries)) {
        set_String(&d->tinylogSource, source);
        iString *base = uriBase_Tinylog(&d->feedUrl, &log->author);
        set_String(&d->tinylogBase, base);
        delete_String(base);
        set_String(&d->tinylogAuthor, &log->author);
        set_String(&d->tinylogAvatar, &log->avatar);
        set_String(&d->title, &log->title);
        const char *sourceStart = constBegin_String(&log->source);
        const int   headerSize  = (int) size_String(tinylogCacheHeader_(
            &d->url, &d->tinylogBase, &d->tinylogAuthor, &d->tinylogAvatar));
        iTime now;
        iTime perEntryAdjust;
        initCurrent_Time(&now);
        initSeconds_Time(&perEntryAdjust, 1.0);
        iConstForEach(Array, i, &log->entries) {
            const iTinylogEntry *logEntry = i.value;
            iFeedEntry          *entry    = new_FeedEntry();
            entry->isTinylog  = iTrue;
            entry->bookmarkId = d->bookmarkId;
            entry->posted     = logEntry->stamp.utc;
            entry->discovered = now;
            sub_Time(&now, &perEntryAdjust);
            set_String(&entry->url, &logEntry->id); /* prefixed when the URI base is resolved */
            entry->sourceRange =
                (iRangei) { headerSize + (int) (logEntry->range.start - sourceStart),
                            headerSize + (int) (logEntry->range.end - sourceStart) };
            iString *excerpt = excerpt_TinylogEntry(logEntry);
            set_String(&entry->title, excerpt);
            delete_String(excerpt);
            pushBack_PtrArray(&d->results, entry);
        }
    }
    delete_Tinylog(log);
    delete_String(source);
}

static iBool parseResult_FeedJob_(iFeedJob *d) {
    const enum iGmStatusCode statusCode = status_GmRequest(d->request);
    /* Returns true if the job is done and can be released. False means the job continues. */
    if (category_GmStatusCode(statusCode) == categoryRedirect_GmStatusCode) {
        const iString *dstUrl = absoluteUrl_String(&d->url, meta_GmRequest(d->request));
        if (statusCode == redirectPermanent_GmStatusCode) {
            if (updateUrls_Bookmark(bookmarks_App(), &d->url, dstUrl)) {
                notify_App("bookmarks.changed");
            }
        }
        /* Set up a new request. */
        if (++d->numRedirect < 5) {
            set_String(&d->url, dstUrl);
            iRelease(d->request);
            submit_FeedJob_(d);
            return iFalse;
        }
        return iTrue;
    }
    /* TODO: Should tell the user if the request failed. */
    if (isSuccess_GmStatusCode(statusCode)) {
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
        if (d->autoDetect) {
            d->isTinylog = isTinylog_Source(&src);
        }
        if (d->isTinylog) {
            parseTinylog_FeedJob_(d, &src);
            deinit_String(&src);
            iRelease(linkPattern);
            iEndCollect();
            return iTrue;
        }
        parseTitle_FeedJob_(d, &src);
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
                set_String(&entry->url, absoluteUrl_String(url_GmRequest(d->request), &entry->url));
                stripDefaultUrlPort_String(&entry->url);
                set_String(&entry->url, canonicalUrl_String(&entry->url));
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
                    stripDefaultUrlPort_String(&entry->url);
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
                if (!contains_IntSet(&d->previouslyCheckedFeeds, id_Bookmark(bm))) {
                    continue; /* not successfully checked yet */
                }
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
            if (!entry->isHeading && !entry->isTinylog && isValid_Time(&entry->discovered) &&
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

static iStringSet *listEntryUrlsFrom_Feeds_(const iFeeds *d, uint32_t sourceId) {
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
        if (isEmpty_PtrArray(incoming)) {
            return iFalse; /* probably not the usual page; keep the known headings */
        }
        lock_Mutex(d->mtx);
        iStringSet *known = listEntryUrlsFrom_Feeds_(d, sourceId);
        iStringSet *presentInSource = new_StringSet();
        /* Look for unknown entries. */
        iForEach(PtrArray, i, incoming) {
            iFeedEntry *entry = i.ptr;
            insert_StringSet(presentInSource, &entry->url);
            if (!contains_StringSet(known, &entry->url)) {
                insert_SortedArray(&d->entries, &entry);
                gotNew = iTrue;
                remove_PtrArrayIterator(&i);
            }
        }
        iString *headingsPageUrl = NULL;
        iForEach(Array, e, &d->entries.values) {
            iFeedEntry *entry = *(iFeedEntry **) e.value;
            /* All known entries that are no longer present in source must be deleted.  */
            if (entry->bookmarkId == sourceId &&
                !contains_StringSet(presentInSource, &entry->url)) {
                delete_FeedEntry(entry);
                remove_ArrayIterator(&e);
            }
            /* Remember the base page URL of heading entries; it is used for read-status
               tracking and must be kept in visited. */
            else if (!headingsPageUrl && entry->bookmarkId == sourceId && entry->isHeading) {
                headingsPageUrl = copy_String(url_FeedEntry(entry));
            }
        }
        iRelease(presentInSource);
        iRelease(known);
        unlock_Mutex(d->mtx);
        if (headingsPageUrl) {
            /* Ensure the page URL is kept so heading entry read status doesn't expire.
               Without this, the Kept flag set by `markEntryAsRead_Feeds` would be cleared
               by the unkept-cleanup below (which only knows fragment-suffixed entry URLs). */
            setUrlKept_Visited(visited_App(), headingsPageUrl, iTrue);
            delete_String(headingsPageUrl);
        }
    }
    else {
        /* All visited URLs still present in the source should be kept indefinitely so their
           read status remains correct. The Kept flag will be cleared after the URL has been
           discarded from the entry database and enough time has passed. */ {
            iForEach(PtrArray, i, incoming) {
                const iFeedEntry *entry = i.ptr;
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

static iBool updateTinylogEntries_Feeds_(iFeeds *d, const iFeedJob *job, iPtrArray *incoming) {
    /* The cached source is authoritative: entries not present in it are removed. */
    iBool         gotNew      = iFalse;
    iBool         hasOverlap  = iFalse;
    iStringArray *presentUrls = new_StringArray();
    iStringArray *readUrls    = new_StringArray();
    const iTime   pageVisit   = urlVisitTime_Visited(visited_App(), &job->feedUrl);
    lock_Mutex(d->mtx);
    iStringSet      *known   = listEntryUrlsFrom_Feeds_(d, job->bookmarkId);
    iStringSet      *present = new_StringSet();
    iPtrArray       *added   = new_PtrArray();
    const iBool      wasKnown = !isEmpty_StringSet(known);
    iForEach(PtrArray, i, incoming) {
        iFeedEntry *entry = i.ptr;
        if (contains_StringSet(present, &entry->url)) {
            continue;
        }
        insert_StringSet(present, &entry->url);
        pushBack_StringArray(presentUrls, &entry->url);
        size_t pos;
        if (locate_SortedArray(&d->entries, &entry, &pos)) {
            /* Edits don't make the entry unread again. */
            iFeedEntry *existing = *(iFeedEntry **) at_SortedArray(&d->entries, pos);
            set_String(&existing->title, &entry->title);
            existing->posted      = entry->posted;
            existing->sourceRange = entry->sourceRange;
            hasOverlap = iTrue;
        }
        else {
            insert_SortedArray(&d->entries, &entry);
            pushBack_PtrArray(added, entry);
            remove_PtrArrayIterator(&i);
            gotNew = iTrue;
        }
    }
    iForEach(Array, e, &d->entries.values) {
        iFeedEntry *entry = *(iFeedEntry **) e.value;
        if (entry->bookmarkId == job->bookmarkId && !contains_StringSet(present, &entry->url)) {
            delete_FeedEntry(entry);
            remove_ArrayIterator(&e);
        }
    }
    if (!isEmpty_PtrArray(added) && (job->isFirstUpdate || (wasKnown && !hasOverlap))) {
        /* Only the most recent entry is left unread. Also applies if all the URIs changed. */
        const iFeedEntry *newest = NULL;
        iConstForEach(PtrArray, a, added) {
            const iFeedEntry *entry = a.ptr;
            if (!newest || cmp_Time(&entry->posted, &newest->posted) > 0) {
                newest = entry;
            }
        }
        iConstForEach(PtrArray, b, added) {
            const iFeedEntry *entry = b.ptr;
            if (entry != newest || cmp_Time(&entry->posted, &pageVisit) <= 0) {
                pushBack_StringArray(readUrls, &entry->url);
            }
        }
    }
    delete_PtrArray(added);
    iRelease(present);
    iRelease(known);
    unlock_Mutex(d->mtx);
    iConstForEach(StringArray, u, presentUrls) {
        setUrlKept_Visited(visited_App(), u.value, iTrue);
    }
    if (!isEmpty_StringArray(readUrls)) {
        /* Not synced as individual recent changes. */
        iTime now;
        initCurrent_Time(&now);
        visitUrlsTime_Visited(visited_App(),
                              readUrls,
                              transient_VisitedUrlFlag | kept_VisitedUrlFlag |
                                  markedAsRead_VisitedUrlFlag,
                              now);
    }
    iRelease(readUrls);
    iRelease(presentUrls);
    return gotNew;
}

static iBool registerTinylog_Feeds_(iFeeds *d, const iFeedJob *job) {
    if (isEmpty_String(&job->tinylogSource)) {
        return iFalse; /* not valid; keep what we have */
    }
    saveTinylogCache_Feeds_(d, job);
    iTinylogCache info;
    init_TinylogCache_(&info);
    set_String(&info.url, &job->url);
    set_String(&info.base, &job->tinylogBase);
    set_String(&info.author, &job->tinylogAuthor);
    set_String(&info.avatar, &job->tinylogAvatar);
    iGuardMutex(d->mtx, setTinylogFeed_Feeds_(d, &job->feedUrl, &info));
    deinit_TinylogCache_(&info);
    return iTrue;
}

static iBool applyTinylog_Feeds_(iFeeds *d, iFeedJob *job) {
    /* All bases of the refresh must be registered first so collisions are resolved
       consistently. */
    lock_Mutex(d->mtx);
    iString *prefix = tinylogPrefix_Feeds_(d, &job->feedUrl, &job->tinylogBase);
    unlock_Mutex(d->mtx);
    iForEach(PtrArray, i, &job->results) {
        iFeedEntry *entry = i.ptr;
        set_String(&entry->url,
                   canonicalUrl_String(collectNewFormat_String(
                       "%s/%s", cstr_String(prefix), cstr_String(&entry->url))));
    }
    delete_String(prefix);
    return updateTinylogEntries_Feeds_(d, job, &job->results);
}

static iThreadResult fetch_Feeds_(iThread *thread) {
    iFeeds *d = &feeds_;
    iUnused(thread);
    iFeedJob *work[maxConcurrentRequests_Feeds]; /* We'll do a couple of concurrent requests. */
    iZap(work);
    iBool gotNew = iFalse;
    iPtrArray *tinylogJobs = new_PtrArray(); /* applied after all jobs have finished */
    notify_App("feeds.update.started");
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
                        if (isSuccess_GmStatusCode(status_GmRequest(work[i]->request))) {
                            iFeedJob *job = work[i];
                            if (job->autoDetect || job->pendingTitle) {
                                postCommandf_App("feeds.fetched bmid:%u tinylog:%d title:%s",
                                                 job->bookmarkId,
                                                 job->isTinylog,
                                                 cstr_String(&job->title));
                            }
                            if (job->isTinylog) {
                                if (registerTinylog_Feeds_(d, job)) {
                                    pushBack_PtrArray(tinylogJobs, job);
                                    work[i] = NULL;
                                }
                            }
                            else {
                                gotNew |= updateEntries_Feeds_(
                                    d, job->checkHeadings, job->bookmarkId, &job->results);
                                if (job->isFirstUpdate) {
                                    iGuardMutex(d->mtx,
                                                insert_IntSet(&d->previouslyCheckedFeeds,
                                                              job->bookmarkId));
                                }
                            }
                        }
                        delete_FeedJob(work[i]); /* NULL if kept */
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
            notifyf_App("feeds.update.progress arg:%d total:%zu", numFinishedJobs, totalJobs);
        }
        /* Stop if everything has finished. */
        if (ongoing == 0 && isEmpty_PtrArray(&d->jobs)) {
            break;
        }
    }
    if (d->stopWorker) {
        iForIndices(i, work) {
            if (work[i]) {
                cancel_GmRequest(work[i]->request);
            }
        }
    }
    iForEach(PtrArray, t, tinylogJobs) {
        iFeedJob *job = t.ptr;
        if (!d->stopWorker) {
            gotNew |= applyTinylog_Feeds_(d, job);
            if (job->isFirstUpdate) {
                iGuardMutex(d->mtx, insert_IntSet(&d->previouslyCheckedFeeds, job->bookmarkId));
            }
        }
        delete_FeedJob(job);
    }
    delete_PtrArray(tinylogJobs);
    initCurrent_Time(&d->lastRefreshedAt);
    save_Feeds_(d);
    removeTinylogCaches_Feeds_(d, iTrue);
    /* Check if there are visited URLs marked as Kept that can be cleared because they are no
       longer present in the database. */ {
        iStringSet *knownEntryUrls = new_StringSet();
        lock_Mutex(d->mtx);
        iConstForEach(Array, i, &d->entries.values) {
            const iFeedEntry *entry = *(const iFeedEntry **) i.value;
            insert_StringSet(knownEntryUrls, &entry->url);
            if (entry->isHeading) {
                /* The read status of heading entries is tracked via the fragment-stripped base
                   URL in visited. Include it so its "kept" flag is preserved by the cleanup. */
                insert_StringSet(knownEntryUrls, url_FeedEntry(entry));
            }
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
    notifyf_App("feeds.update.finished arg:%d unread:%zu", gotNew ? 1 : 0,
                     numUnread_Feeds());
    return 0;
}

static iBool startWorker_Feeds_(iFeeds *d) {
    if (d->worker || isRunning_Sync()) {
        return iFalse; /* Refresh is already ongoing, or bookmarks may change. */
    }
    /* Queue up all the subscriptions for the worker. */
    iConstForEach(PtrArray, i, listSubscriptions_()) {
        const iBookmark *bm = i.ptr;
        iFeedJob *job = new_FeedJob(bm);
        if (!contains_IntSet(&d->previouslyCheckedFeeds, id_Bookmark(bm))) {
            job->isFirstUpdate = iTrue;
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

static uint32_t refresh_Feeds_(void *userdata, SDL_TimerID timerID, uint32_t interval) {
    iUnused(userdata, timerID, interval);
    /* Called in the SDL timer thread, so let's start a worker thread for running the refresh. */
    startWorker_Feeds_(&feeds_);
    return 0;
}

static void removeRefreshTimer_Feeds_(iFeeds *d) {
    if (d->refreshTimer) {
        SDL_RemoveTimer(d->refreshTimer);
        d->refreshTimer = 0;
    }
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
                        const uint32_t bookmarkId = findSubscription_Feeds_(feedUrl);
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
                        entry->isTinylog = startsWith_String(&entry->url, "local:/tinylog/");
                        entry->isHeading = !entry->isTinylog && isHeadingEntry_FeedEntry_(entry);
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

static const iBlock *aboutFeedsPage_Feeds_(iRangecc path, iRangecc query);
static const iBlock *tinylogPage_Feeds_(iRangecc path);

void init_Feeds(const char *saveDir) {
    iFeeds *d = &feeds_;
    d->refreshTimer = 0;
    d->mtx = new_Mutex();
    initCStr_String(&d->saveDir, saveDir);
    init_IntSet(&d->previouslyCheckedFeeds);
    iZap(d->lastRefreshedAt);
    d->worker = NULL;
    init_PtrArray(&d->jobs);
    init_SortedArray(&d->entries, sizeof(iFeedEntry *), cmp_FeedEntryPtr_);
    init_PtrArray(&d->tinylogFeeds);
    load_Feeds_(d);
    loadTinylogFeeds_Feeds_(d);
    setRefreshInterval_Feeds(prefs_App()->feedInterval);
    addAboutHandler_GmRequest(aboutFeedsPage_Feeds_);
    addLocalHandler_GmRequest(tinylogPage_Feeds_);
}

void deinit_Feeds(void) {
    iFeeds *d = &feeds_;
    removeRefreshTimer_Feeds_(d);
    stopWorker_Feeds_(d);
    iAssert(isEmpty_PtrArray(&d->jobs));
    deinit_PtrArray(&d->jobs);
    deinit_String(&d->saveDir);
    delete_Mutex(d->mtx);
    iForEach(Array, i, &d->entries.values) {
        iFeedEntry **entry = i.value;
        delete_FeedEntry(*entry);
    }
    iForEach(PtrArray, t, &d->tinylogFeeds) {
        iTinylogFeed *tf = t.ptr;
        deinit_String(&tf->avatar);
        deinit_String(&tf->author);
        deinit_String(&tf->sourceUrl);
        deinit_String(&tf->base);
        deinit_String(&tf->feedUrl);
        free(tf);
    }
    deinit_PtrArray(&d->tinylogFeeds);
    deinit_IntSet(&d->previouslyCheckedFeeds);
    deinit_SortedArray(&d->entries);
}

void resetKnownEntries_Feeds(void) {
    iFeeds *d = &feeds_;
    iString *saveDir = copy_String(&d->saveDir);
    removeTinylogCaches_Feeds_(d, iFalse);
    deinit_Feeds();
    removePath_CStr(cstrCollect_String(concatCStr_Path(saveDir, feedsFilename_Feeds_)));
    init_Feeds(cstr_String(saveDir));
    delete_String(saveDir);
}

void refresh_Feeds(void) {
    startWorker_Feeds_(&feeds_);
}

void setRefreshInterval_Feeds(enum iFeedInterval feedInterval) {
    iFeeds *d = &feeds_;
    if (isInitialized_Feeds_(d)) {
        removeRefreshTimer_Feeds_(d);
        d->refreshInterval = feedInterval * 1000;
        if (d->refreshInterval && isValid_Time(&d->lastRefreshedAt)) {
            const double remaining = (double) feedInterval - elapsedSeconds_Time(&d->lastRefreshedAt);
            d->refreshTimer =
                SDL_AddTimer((uint32_t) (iMax(1.0, remaining) * 1000), refresh_Feeds_, NULL);
        }
    }
}

void refreshFinished_Feeds(void) {
    stopWorker_Feeds_(&feeds_);
    setRefreshInterval_Feeds(prefs_App()->feedInterval); /* restart the timer */
}

iBool isRefreshing_Feeds(void) {
    return feeds_.worker != NULL;
}

void remapBookmarkIds_Feeds(const iArray *idMappings) {
    iFeeds *d = &feeds_;
    if (!isInitialized_Feeds_(d) || isEmpty_Array(idMappings)) {
        return;
    }
    lock_Mutex(d->mtx);
    iIntSet checked;
    init_IntSet(&checked);
    iConstForEach(IntSet, i, &d->previouslyCheckedFeeds) {
        insert_IntSet(&checked, mappedId_SyncIdMapping(idMappings, *(const int *) i.value));
    }
    deinit_IntSet(&d->previouslyCheckedFeeds);
    d->previouslyCheckedFeeds = checked;
    /* Entries are sorted by bookmark ID, too. */
    iPtrArray *entries = collect_PtrArray(copy_Array(&d->entries.values));
    clear_SortedArray(&d->entries);
    iForEach(PtrArray, j, entries) {
        iFeedEntry *entry = j.ptr;
        entry->bookmarkId = mappedId_SyncIdMapping(idMappings, entry->bookmarkId);
        insert_SortedArray(&d->entries, &entry);
    }
    unlock_Mutex(d->mtx);
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
    removeTinylogCaches_Feeds_(d, iTrue);
}

void resetFeed_Feeds(uint32_t feedBookmarkId) {
    iFeeds *d = &feeds_;
    removeEntries_Feeds(feedBookmarkId);
    iGuardMutex(d->mtx, remove_IntSet(&d->previouslyCheckedFeeds, feedBookmarkId));
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
                visitUrlTime_Visited(vis,
                                     url,
                                     transient_VisitedUrlFlag | kept_VisitedUrlFlag |
                                         (isRead ? markedAsRead_VisitedUrlFlag : 0),
                                     entryTime);
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
                visitUrl_Visited(vis,
                                 entryUrl,
                                 transient_VisitedUrlFlag | kept_VisitedUrlFlag |
                                     markedAsRead_VisitedUrlFlag);
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

static iBool hasEntryAt_Feeds_(const iFeeds *d, const iString *url, iBool isPrefix) {
    /* Entries are sorted by URL, so the first one at or after `url` is checked. */
    const iFeedEntry  entry    = { .url = *url };
    const iFeedEntry *entryPtr = &entry;
    size_t pos;
    locate_SortedArray(&d->entries, &entryPtr, &pos);
    if (pos >= size_SortedArray(&d->entries)) {
        return iFalse;
    }
    const iString *found = &(*(const iFeedEntry **) constAt_SortedArray(&d->entries, pos))->url;
    return isPrefix ? startsWith_String(found, cstr_String(url)) : equal_String(found, url);
}

iBool isEntryUrl_Feeds(const iString *url) {
    /* Entries with a fragment are headings on the page that is visited. */
    iFeeds *d = &feeds_;
    iBool found;
    lock_Mutex(d->mtx);
    found = hasEntryAt_Feeds_(d, url, iFalse) ||
            hasEntryAt_Feeds_(d, collectNewFormat_String("%s#", cstr_String(url)), iTrue);
    unlock_Mutex(d->mtx);
    return found;
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

const iPtrArray *listMatchingEntries_Feeds(const char *urlPrefix) {
    iFeeds *d = &feeds_;
    lock_Mutex(d->mtx);
    iPtrArray *list = collectNew_PtrArray();
    iForEach(PtrArray, i, &d->entries.values) {
        iFeedEntry *entry = i.ptr;
        if (startsWith_String(&entry->url, urlPrefix)) {
            pushBack_PtrArray(list, entry);
        }
    }
    unlock_Mutex(d->mtx);
    return list;
}

const iPtrArray *listVisibleEntries_Feeds(iBool unreadOnly, const iString *openUrl) {
    iPtrArray *list = collectNew_PtrArray();
    iTime now;
    initCurrent_Time(&now);
    iConstForEach(PtrArray, i, listEntries_Feeds()) {
        const iFeedEntry *entry = i.ptr;
        if (isHidden_FeedEntry(entry)) {
            continue;
        }
        /* Don't show entries in the far future. */
        if (secondsSince_Time(&now, &entry->posted) < -24 * 60 * 60) {
            continue;
        }
        if (unreadOnly && !isUnread_FeedEntry(entry) &&
            !(openUrl && equal_String(openUrl, &entry->url))) {
            continue;
        }
        pushBack_PtrArray(list, entry);
        if (size_PtrArray(list) == 100) {
            /* For more items, one can always see "about:feeds". A large number of items
               is a bit difficult to navigate in the sidebar. */
            break;
        }
    }
    return list;
}

void markAllEntriesAsRead_Feeds(void) {
    iConstForEach(PtrArray, i, listEntries_Feeds()) {
        const iFeedEntry *entry = i.ptr;
        markEntryAsRead_Feeds(entry->bookmarkId, &entry->url, iTrue);
    }
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

static const iString *entryListPage_Feeds_(void) {
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

static const iBlock *aboutFeedsPage_Feeds_(iRangecc path, iRangecc query) {
    iUnused(query);
    if (equalCase_Rangecc(path, "feeds")) {
        return utf8_String(entryListPage_Feeds_());
    }
    return NULL;
}

/*----------------------------------------------------------------------------------------------*/

void init_TinylogInfo(iTinylogInfo *d) {
    d->bookmarkId = 0;
    init_String(&d->author);
    init_String(&d->avatar);
    iZap(d->posted);
}

void deinit_TinylogInfo(iTinylogInfo *d) {
    deinit_String(&d->avatar);
    deinit_String(&d->author);
}

static const iFeedEntry *findTinylogEntry_Feeds_(const iFeeds *d, const iString *url) {
    /* Caller must lock. Entries are sorted by URL, so the first one at or after `url` is
       checked. */
    const iFeedEntry  entry    = { .url = *url };
    const iFeedEntry *entryPtr = &entry;
    size_t pos;
    locate_SortedArray(&d->entries, &entryPtr, &pos);
    if (pos < size_SortedArray(&d->entries)) {
        const iFeedEntry *found = *(const iFeedEntry **) constAt_SortedArray(&d->entries, pos);
        if (found->isTinylog && equal_String(&found->url, url)) {
            return found;
        }
    }
    return NULL;
}

uint32_t tinylogBookmark_Feeds(const iString *entryUrl) {
    iFeeds *d = &feeds_;
    if (!isInitialized_Feeds_(d) || !startsWithCase_String(entryUrl, "local:/tinylog/")) {
        return 0;
    }
    entryUrl = canonicalUrl_String(entryUrl);
    uint32_t id = 0;
    lock_Mutex(d->mtx);
    const iFeedEntry *entry = findTinylogEntry_Feeds_(d, entryUrl);
    if (entry) {
        id = entry->bookmarkId;
    }
    unlock_Mutex(d->mtx);
    return id;
}

static iBool readTinylogSection_Feeds_(const iFeeds *d, const iString *feedUrl, iRangei range,
                                       iString *section_out) {
    /* The section must be followed by the next entry or the end of the file. */
    iBool  ok = iFalse;
    iFile *f  = new_File(tinylogCachePath_Feeds_(d, feedUrl));
    if (open_File(f, readOnly_FileMode) && range.end <= (int) size_File(f)) {
        const size_t size = size_Range(&range);
        seek_File(f, range.start);
        iBlock *data = read_File(f, size + 2);
        if (size_Block(data) == size ||
            (size_Block(data) == size + 2 && !memcmp(constData_Block(data) + size, "##", 2))) {
            truncate_Block(data, size);
            setBlock_String(section_out, data);
            ok = iTrue;
        }
        delete_Block(data);
    }
    iRelease(f);
    return ok;
}

static void updateTinylogRanges_Feeds_(iFeeds *d, uint32_t bookmarkId, const iString *prefix,
                                       const iTinylog *log, size_t sourcePos) {
    /* Caller must lock. */
    const char *sourceStart = constBegin_String(&log->source);
    iConstForEach(Array, i, &log->entries) {
        const iTinylogEntry *logEntry = i.value;
        const iFeedEntry     key      = {
            .url = *canonicalUrl_String(collectNewFormat_String(
                "%s/%s", cstr_String(prefix), cstr_String(&logEntry->id))),
            .bookmarkId = bookmarkId
        };
        const iFeedEntry *keyPtr = &key;
        size_t pos;
        if (locate_SortedArray(&d->entries, &keyPtr, &pos)) {
            iFeedEntry *entry  = *(iFeedEntry **) at_SortedArray(&d->entries, pos);
            entry->sourceRange = (iRangei){ (int) (sourcePos + (logEntry->range.start - sourceStart)),
                                            (int) (sourcePos + (logEntry->range.end - sourceStart)) };
        }
    }
}

static const iTinylogEntry *loadTinylogEntry_Feeds_(iFeeds *d, const iBookmark *bm,
                                                    const iString *entryUrl, iTinylog *log,
                                                    iString *sourceUrl_out) {
    /* `entryUrl` must be canonical. The entry's known position in the cache file is used if
       its contents check out. Otherwise, the whole file is parsed and positions updated. */
    const iRangecc idRange = baseNameSep_Path(entryUrl, "/");
    const iString *id      = collectNewRange_String(idRange);
    iRangei        range   = { 0, 0 };
    lock_Mutex(d->mtx);
    const iFeedEntry *feedEntry = findTinylogEntry_Feeds_(d, entryUrl);
    if (feedEntry) {
        range = feedEntry->sourceRange;
    }
    const iTinylogFeed *tf = findTinylogFeed_Feeds_(d, &bm->url);
    if (tf) {
        set_String(sourceUrl_out, &tf->sourceUrl);
    }
    unlock_Mutex(d->mtx);
    const iBool isUniqueStamp = indexOf_String(id, '-') == iInvalidPos; /* no "-2" suffix */
    if (!isEmpty_Range(&range) && isUniqueStamp && !isEmpty_String(sourceUrl_out)) {
        iString *section = collectNew_String();
        if (readTinylogSection_Feeds_(d, &bm->url, range, section)) {
            parse_Tinylog(log, section, NULL);
            if (size_Array(&log->entries) == 1) {
                const iTinylogEntry *entry = constAt_Array(&log->entries, 0);
                if (entry->range.start == constBegin_String(&log->source) &&
                    equal_String(&entry->id, id)) {
                    return entry;
                }
            }
        }
    }
    const iTinylogEntry *found = NULL;
    iTinylogCache        cache;
    init_TinylogCache_(&cache);
    if (loadTinylogCache_Feeds_(d, &bm->url, &cache, iFalse)) {
        parse_Tinylog(log, &cache.source, &bm->url);
        set_String(sourceUrl_out, &cache.url);
        const iString *prefix =
            collectNewRange_String((iRangecc){ constBegin_String(entryUrl), idRange.start - 1 });
        iGuardMutex(d->mtx,
                    updateTinylogRanges_Feeds_(d, id_Bookmark(bm), prefix, log, cache.sourcePos));
        found = findEntry_Tinylog(log, id);
    }
    deinit_TinylogCache_(&cache);
    return found;
}

iBool tinylogInfo_Feeds(const iString *entryUrl, iTinylogInfo *info_out) {
    iFeeds *d = &feeds_;
    if (!isInitialized_Feeds_(d) || !startsWithCase_String(entryUrl, "local:/tinylog/")) {
        return iFalse;
    }
    entryUrl = canonicalUrl_String(entryUrl);
    lock_Mutex(d->mtx);
    const iFeedEntry *entry = findTinylogEntry_Feeds_(d, entryUrl);
    if (entry) {
        info_out->bookmarkId = entry->bookmarkId;
        info_out->posted     = entry->posted;
    }
    unlock_Mutex(d->mtx);
    const iBookmark *bm = entry ? get_Bookmarks(bookmarks_App(), info_out->bookmarkId) : NULL;
    if (!bm) {
        return iFalse;
    }
    lock_Mutex(d->mtx);
    const iTinylogFeed *tf = findTinylogFeed_Feeds_(d, &bm->url);
    if (tf) {
        set_String(&info_out->author, &tf->author);
        set_String(&info_out->avatar, &tf->avatar);
    }
    unlock_Mutex(d->mtx);
    return iTrue;
}

iString *responseLink_Feeds(const iString *entryUrl) {
    iFeeds          *d    = &feeds_;
    const uint32_t   bmId = tinylogBookmark_Feeds(entryUrl);
    const iBookmark *bm   = bmId ? get_Bookmarks(bookmarks_App(), bmId) : NULL;
    if (!bm) {
        return NULL;
    }
    iString             *link      = NULL;
    iTinylog            *log       = new_Tinylog();
    iString             *sourceUrl = collectNew_String();
    const iTinylogEntry *entry =
        loadTinylogEntry_Feeds_(d, bm, canonicalUrl_String(entryUrl), log, sourceUrl);
    if (entry) {
        iString *author = collectNew_String();
        lock_Mutex(d->mtx);
        const iTinylogFeed *tf = findTinylogFeed_Feeds_(d, &bm->url);
        if (tf) {
            set_String(author, &tf->author);
        }
        unlock_Mutex(d->mtx);
        if (isEmpty_String(author)) {
            set_String(author, collect_String(derivedAuthor_Tinylog(&bm->url)));
        }
        link = newFormat_String("=> %s RE: %s %s",
                                cstr_String(isEmpty_String(sourceUrl) ? &bm->url : sourceUrl),
                                cstr_String(author),
                                cstr_String(collect_String(format_TinylogStamp(&entry->stamp))));
    }
    delete_Tinylog(log);
    return link;
}

iDeclareType(TinylogRef)

struct Impl_TinylogRef {
    iString url; /* local entry URL */
    iString section;
    iString sourceUrl;
    iString author;
    iTime   posted;
};

iDeclareType(TinylogThread)

struct Impl_TinylogThread {
    iFeeds     *feeds;
    iPtrArray   refs; /* TinylogRef, in the order they are quoted */
    iStringSet *seen; /* entry URLs on the page */
};

static iBool responseRef_Feeds_(void *context, const iString *url, const iTinylogStamp *stamp) {
    iTinylogThread  *d    = context;
    const uint32_t   bmId = findSubscription_Feeds_(urlFragmentStripped_String(url));
    const iBookmark *bm   = bmId ? get_Bookmarks(bookmarks_App(), bmId) : NULL;
    if (!bm || ~bm->flags & tinylog_BookmarkFlag) {
        return iFalse;
    }
    iBool         found = iFalse;
    iTinylogCache cache;
    init_TinylogCache_(&cache);
    if (loadTinylogCache_Feeds_(d->feeds, &bm->url, &cache, iFalse)) {
        iTinylog *log = new_Tinylog();
        parse_Tinylog(log, &cache.source, &bm->url);
        const iTinylogEntry *entry = findStamp_Tinylog(log, stamp);
        if (entry) {
            lock_Mutex(d->feeds->mtx);
            iString *prefix = tinylogPrefix_Feeds_(d->feeds, &bm->url, &cache.base);
            unlock_Mutex(d->feeds->mtx);
            const iString *entryUrl = canonicalUrl_String(collectNewFormat_String(
                "%s/%s", cstr_String(prefix), cstr_String(&entry->id)));
            delete_String(prefix);
            if (!contains_StringSet(d->seen, entryUrl)) {
                insert_StringSet(d->seen, entryUrl);
                iTinylogRef *ref = iMalloc(TinylogRef);
                initCopy_String(&ref->url, entryUrl);
                initRange_String(&ref->section, entry->range);
                initCopy_String(&ref->sourceUrl, &cache.url);
                initCopy_String(&ref->author, &log->author);
                ref->posted = entry->stamp.utc;
                pushBack_PtrArray(&d->refs, ref);
            }
            found = iTrue;
        }
        delete_Tinylog(log);
    }
    deinit_TinylogCache_(&cache);
    return found;
}

static void prependQuotedRef_TinylogThread_(iTinylogThread *d, const iTinylogRef *ref,
                                            iString *page) {
    iTinylog *log = new_Tinylog();
    parse_Tinylog(log, &ref->section, NULL);
    if (!isEmpty_Array(&log->entries)) {
        iDate posted;
        init_Date(&posted, &ref->posted);
        /* Day of month without zero padding. */
        iString *dateFormat = collectNewCStr_String(cstr_Lang("banner.tinylogentry.date"));
        replace_String(dateFormat, "%d", format_CStr("%d", posted.day));
        iString *quote = new_String();
        appendFormat_String(quote, "=> %s ", cstr_String(&ref->url));
        appendFormat_String(quote,
                            cstr_Lang("feeds.tinylog.wrote"),
                            cstr_String(&ref->author),
                            cstr_String(collect_String(
                                format_Date(&posted, cstr_String(dateFormat)))));
        appendCStr_String(quote, "\n");
        iString *content = content_TinylogEntry(
            constAt_Array(&log->entries, 0), &ref->sourceUrl, responseRef_Feeds_, d);
        iRangecc line = iNullRange;
        while (nextSplit_Rangecc(range_String(content), "\n", &line)) {
            appendFormat_String(quote, isEmpty_Range(&line) ? ">\n" : "> %s\n",
                                cstr_Rangecc(line));
        }
        appendCStr_String(quote, "\n");
        prepend_String(page, quote);
        delete_String(content);
        delete_String(quote);
    }
    delete_Tinylog(log);
}

static const iBlock *tinylogPage_Feeds_(iRangecc path) {
    if (!startsWith_Rangecc(path, "/tinylog/")) {
        return NULL;
    }
    iFeeds          *d    = &feeds_;
    iString         *page = collectNew_String();
    const iString   *url  = collectNewFormat_String("local:%s", cstr_Rangecc(path));
    const uint32_t   bmId = tinylogBookmark_Feeds(url);
    const iBookmark *bm   = bmId ? get_Bookmarks(bookmarks_App(), bmId) : NULL;
    if (!bm) {
        setCStr_String(page, translateCStr_Lang("${feeds.tinylog.notfound}\n"));
        return utf8_String(page);
    }
    iTinylog            *log       = new_Tinylog();
    iString             *sourceUrl = collectNew_String();
    const iTinylogEntry *entry =
        loadTinylogEntry_Feeds_(d, bm, canonicalUrl_String(url), log, sourceUrl);
    if (entry) {
        iTinylogThread thread = { .feeds = d, .seen = new_StringSet() };
        init_PtrArray(&thread.refs);
        insert_StringSet(thread.seen, canonicalUrl_String(url));
        iString *content = content_TinylogEntry(entry, sourceUrl, responseRef_Feeds_, &thread);
        append_String(page, content);
        delete_String(content);
        /* Quoting may add more references. */
        for (size_t i = 0; i < size_PtrArray(&thread.refs); i++) {
            prependQuotedRef_TinylogThread_(&thread, at_PtrArray(&thread.refs, i), page);
        }
        iForEach(PtrArray, r, &thread.refs) {
            iTinylogRef *ref = r.ptr;
            deinit_String(&ref->author);
            deinit_String(&ref->sourceUrl);
            deinit_String(&ref->section);
            deinit_String(&ref->url);
            free(ref);
        }
        deinit_PtrArray(&thread.refs);
        iRelease(thread.seen);
    }
    else {
        appendFormat_String(page,
                            "%s\n\n=> %s %s\n",
                            cstr_Lang("feeds.tinylog.unavailable"),
                            cstr_String(&bm->url),
                            cstr_String(&bm->title));
    }
    delete_Tinylog(log);
    return utf8_String(page);
}

const iStringArray *listSubscribableUrls_Feeds(const iString *source, iBool isPlainText,
                                               const iString *pageUrl) {
    iStringArray *urls  = iClob(new_StringArray());
    iStringSet   *seen  = iClob(new_StringSet());
    iRangecc      line  = iNullRange;
    iBool         isPre = iFalse;
    insert_StringSet(seen, canonicalUrl_String(pageUrl));
    while (nextSplit_Rangecc(range_String(source), "\n", &line)) {
        iRangecc text = line;
        trim_Rangecc(&text);
        const iString *url = NULL;
        if (isPlainText) {
            /* Each line must be an absolute URL by itself. */
            iUrl parts;
            url = collectNewRange_String(text);
            init_Url(&parts, url);
            if (isEmpty_Range(&parts.scheme) || indexOf_String(url, ' ') != iInvalidPos ||
                indexOf_String(url, '\t') != iInvalidPos) {
                continue;
            }
        }
        else {
            if (startsWith_Rangecc(text, "```")) {
                isPre = !isPre;
                continue;
            }
            if (isPre || !startsWith_Rangecc(text, "=>")) {
                continue;
            }
            text.start += 2;
            trimStart_Rangecc(&text);
            iRangecc word = iNullRange;
            if (!nextSplit_Rangecc(text, " ", &word)) {
                continue;
            }
            url = absoluteUrl_String(pageUrl, collect_String(newRange_String(word)));
        }
        iUrl parts;
        init_Url(&parts, url);
        if (!equalCase_Rangecc(parts.scheme, "gemini") || isEmpty_Range(&parts.host)) {
            continue;
        }
        iString *canon = collect_String(copy_String(url));
        stripDefaultUrlPort_String(canon);
        url = canonicalUrl_String(canon);
        if (contains_StringSet(seen, url) || findSubscription_Feeds_(url)) {
            continue;
        }
        insert_StringSet(seen, url);
        pushBack_StringArray(urls, url);
    }
    return urls;
}

size_t subscribeAll_Feeds(const iStringArray *urls, const iString *folderTitle,
                          enum iFeedType type) {
    iBookmarks    *bmarks    = bookmarks_App();
    const uint32_t typeFlags = type == tinylog_FeedType      ? tinylog_BookmarkFlag
                               : type == autoDetect_FeedType ? autoDetect_BookmarkFlag
                                                             : 0;
    uint32_t folderId = 0;
    size_t   count    = 0;
    iConstForEach(StringArray, i, urls) {
        const iString *url = i.value;
        iBookmark     *bm  = get_Bookmarks(bmarks, findUrl_Bookmarks(bmarks, url));
        if (bm && bm->flags & remote_BookmarkFlag) {
            bm = NULL; /* can't modify remote bookmarks */
        }
        if (bm && bm->flags & subscribed_BookmarkFlag) {
            continue;
        }
        if (!bm) {
            if (!folderId) {
                folderId = addToFolder_Bookmarks(bmarks, NULL, folderTitle, NULL, 0, 0);
            }
            iUrl parts;
            init_Url(&parts, url);
            iString *title = collectNewRange_String(parts.host);
            append_String(title, collect_String(urlDecode_String(collectNewRange_String(parts.path))));
            bm = get_Bookmarks(bmarks,
                               addToFolder_Bookmarks(bmarks,
                                                     url,
                                                     title,
                                                     NULL,
                                                     siteIcon_Bookmarks(bmarks, url),
                                                     folderId));
            bm->flags |= pendingTitle_BookmarkFlag;
        }
        bm->flags &= ~(headings_BookmarkFlag | tinylog_BookmarkFlag | autoDetect_BookmarkFlag);
        bm->flags |= subscribed_BookmarkFlag | typeFlags;
        count++;
    }
    return count;
}
