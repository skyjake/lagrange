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

#include "visited.h"
#include "app.h"

#include <the_Foundation/file.h>
#include <the_Foundation/mutex.h>
#include <the_Foundation/path.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/sortedarray.h>

const int maxAge_Visited = 6 * 3600 * 24 * 30; /* six months */

void init_VisitedUrl(iVisitedUrl *d) {
    initCurrent_Time(&d->when);
    init_String(&d->url);
    d->flags = 0;
}

void deinit_VisitedUrl(iVisitedUrl *d) {
    deinit_String(&d->url);
}

static int cmpUrl_VisitedUrl_(const void *a, const void *b) {
    return cmpString_String(&((const iVisitedUrl *) a)->url, &((const iVisitedUrl *) b)->url);
}

static int cmpNewer_VisitedUrl_(const void *insert, const void *existing) {
    return seconds_Time(&((const iVisitedUrl *) insert  )->when) >=
           seconds_Time(&((const iVisitedUrl *) existing)->when);
}

/*----------------------------------------------------------------------------------------------*/

struct Impl_Visited {
    iMutex *mtx;
    iSortedArray visited;
};

iDefineTypeConstruction(Visited)

void init_Visited(iVisited *d) {
    d->mtx = new_Mutex();
    init_SortedArray(&d->visited, sizeof(iVisitedUrl), cmpUrl_VisitedUrl_);
}

void deinit_Visited(iVisited *d) {
    iGuardMutex(d->mtx, {
        clear_Visited(d);
        deinit_SortedArray(&d->visited);
    });
    delete_Mutex(d->mtx);
}

void serialize_Visited(const iVisited *d, iStream *out) {
    iString *line = new_String();
    lock_Mutex(d->mtx);
    iConstForEach(Array, i, &d->visited.values) {
        const iVisitedUrl *item = i.value;
        format_String(line,
                      "%llu %04x %s\n",
                      (unsigned long long) integralSeconds_Time(&item->when),
                      item->flags,
                      cstr_String(&item->url));
        writeData_Stream(out, cstr_String(line), size_String(line));
    }
    unlock_Mutex(d->mtx);
    delete_String(line);
}

void save_Visited(const iVisited *d, const char *dirPath) {
    iFile *f = newCStr_File(concatPath_CStr(dirPath, "visited.2.txt"));
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        serialize_Visited(d, stream_File(f));
    }
    iRelease(f);
}

void deserialize_Visited(iVisited *d, iStream *ins, iBool mergeKeepingLatest) {
    const iRangecc src  = range_Block(collect_Block(readAll_Stream(ins)));
    iRangecc       line = iNullRange;
    iTime          now;
    initCurrent_Time(&now);
    lock_Mutex(d->mtx);
    while (nextSplit_Rangecc(src, "\n", &line)) {
        if (size_Range(&line) < 8) continue;
        char *endp = NULL;
        const unsigned long long ts = strtoull(line.start, &endp, 10);
        if (ts == 0) break;
        const uint32_t flags = (uint32_t) strtoul(skipSpace_CStr(endp), &endp, 16);
        const char *urlStart = skipSpace_CStr(endp);
        iVisitedUrl item;
        item.when.ts = (struct timespec){ .tv_sec = ts };
        if (~flags & kept_VisitedUrlFlag &&
            secondsSince_Time(&now, &item.when) > maxAge_Visited) {
            continue; /* Too old. */
        }
        item.flags = flags;
        initRange_String(&item.url, (iRangecc){ urlStart, line.end });
        set_String(&item.url, &item.url);
        if (mergeKeepingLatest) {
            /* Check if we already have this. */
            size_t existingPos;
            if (locate_SortedArray(&d->visited, &item, &existingPos)) {
                iVisitedUrl *existing = at_SortedArray(&d->visited, existingPos);
                max_Time(&existing->when, &item.when);
                existing->flags = item.flags;
                continue;
            }
        }
        insert_SortedArray(&d->visited, &item);
    }
    unlock_Mutex(d->mtx);
}

void load_Visited(iVisited *d, const char *dirPath) {
    iFile *f = newCStr_File(concatPath_CStr(dirPath, "visited.2.txt"));
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        deserialize_Visited(d, stream_File(f), iFalse /* no merge */);
    }
    iRelease(f);
}

void clear_Visited(iVisited *d) {
    lock_Mutex(d->mtx);
    iForEach(Array, v, &d->visited.values) {
        deinit_VisitedUrl(v.value);
    }
    clear_SortedArray(&d->visited);
    unlock_Mutex(d->mtx);
}

static size_t find_Visited_(const iVisited *d, const iString *url) {
    iVisitedUrl visit;
    init_VisitedUrl(&visit);
    set_String(&visit.url, url);
    size_t pos = iInvalidPos;
    iGuardMutex(d->mtx, {
        locate_SortedArray(&d->visited, &visit, &pos);
        deinit_VisitedUrl(&visit);
    });
    return pos;
}

void visitUrl_Visited(iVisited *d, const iString *url, uint16_t visitFlags) {
    iTime when;
    initCurrent_Time(&when);
    visitUrlTime_Visited(d, url, visitFlags, when);
}

void visitUrlTime_Visited(iVisited *d, const iString *url, uint16_t visitFlags, iTime when) {
    if (isEmpty_String(url)) return;
    url = canonicalUrl_String(url);
    iVisitedUrl visit;
    init_VisitedUrl(&visit);
    visit.when = when;
    visit.flags = visitFlags;
    set_String(&visit.url, url);
    size_t pos;
    lock_Mutex(d->mtx);
    if (locate_SortedArray(&d->visited, &visit, &pos)) {
        iVisitedUrl *old = at_SortedArray(&d->visited, pos);
        if (old->flags & kept_VisitedUrlFlag) {
            visitFlags |= kept_VisitedUrlFlag; /* must continue to be kept */
        }
        if (cmpNewer_VisitedUrl_(&visit, old)) {
            old->when = visit.when;
            old->flags = visitFlags;
            unlock_Mutex(d->mtx);
            deinit_VisitedUrl(&visit);
            return;
        }
    }
    insert_SortedArray(&d->visited, &visit);
    unlock_Mutex(d->mtx);
}

void setUrlKept_Visited(iVisited *d, const iString *url, iBool isKept) {
    if (isEmpty_String(url)) return;
    iVisitedUrl visit;
    init_VisitedUrl(&visit);
    set_String(&visit.url, canonicalUrl_String(url));
    size_t pos;
    lock_Mutex(d->mtx);
    if (locate_SortedArray(&d->visited, &visit, &pos)) {
        iVisitedUrl *vis = at_SortedArray(&d->visited, pos);
        iChangeFlags(vis->flags, kept_VisitedUrlFlag, isKept);
    }
    unlock_Mutex(d->mtx);
    deinit_VisitedUrl(&visit);
}

void removeUrl_Visited(iVisited *d, const iString *url) {
    url = canonicalUrl_String(url);
    iGuardMutex(d->mtx, {
        size_t pos = find_Visited_(d, url);
        if (pos < size_SortedArray(&d->visited)) {
            iVisitedUrl *visUrl = at_SortedArray(&d->visited, pos);
            if (equal_String(&visUrl->url, url)) {
                deinit_VisitedUrl(visUrl);
                remove_Array(&d->visited.values, pos);
            }
        }
    });
}

iTime urlVisitTime_Visited(const iVisited *d, const iString *url) {
    iVisitedUrl item;
    size_t pos;
    iZap(item);
    initCopy_String(&item.url, canonicalUrl_String(url));
    lock_Mutex(d->mtx);
    if (locate_SortedArray(&d->visited, &item, &pos)) {
        item.when = ((const iVisitedUrl *) constAt_SortedArray(&d->visited, pos))->when;
    }
    unlock_Mutex(d->mtx);
    deinit_String(&item.url);
    return item.when;
}

iBool containsUrl_Visited(const iVisited *d, const iString *url) {
    const iTime time = urlVisitTime_Visited(d, url);
    return isValid_Time(&time);
}

static int cmpWhenDescending_VisitedUrlPtr_(const void *a, const void *b) {
    const iVisitedUrl *s = *(const void **) a, *t = *(const void **) b;
    return -cmp_Time(&s->when, &t->when);
}

const iPtrArray *list_Visited(const iVisited *d, size_t count) {
    iPtrArray *urls = collectNew_PtrArray();
    iGuardMutex(d->mtx, {
        iConstForEach(Array, i, &d->visited.values) {
            const iVisitedUrl *vis = i.value;
            if (~vis->flags & transient_VisitedUrlFlag) {
                pushBack_PtrArray(urls, vis);
            }
        }
    });
    sort_Array(urls, cmpWhenDescending_VisitedUrlPtr_);
    if (count > 0 && size_Array(urls) > count) {
        resize_Array(urls, count);
    }
    return urls;
}

const iPtrArray *listKept_Visited(const iVisited *d) {
    iPtrArray *urls = collectNew_PtrArray();
    iGuardMutex(d->mtx, {
        iConstForEach(Array, i, &d->visited.values) {
            const iVisitedUrl *vis = i.value;
            if (vis->flags & kept_VisitedUrlFlag) {
                pushBack_PtrArray(urls, vis);
            }
        }
    });
    return urls;
}
