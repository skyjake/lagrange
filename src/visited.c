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

static const size_t maxAgeVisited_Visited_ = 3600 * 24 * 30; /* one month */

void init_VisitedUrl(iVisitedUrl *d) {
    initCurrent_Time(&d->when);
    init_String(&d->url);
}

void deinit_VisitedUrl(iVisitedUrl *d) {
    deinit_String(&d->url);
}

static int cmpUrl_VisitedUrl_(const void *a, const void *b) {
    return cmpString_String(&((const iVisitedUrl *) a)->url, &((const iVisitedUrl *) b)->url);
}

static int cmpNewer_VisitedUrl_(const void *insert, const void *existing) {
    return seconds_Time(&((const iVisitedUrl *) insert  )->when) >
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

void save_Visited(const iVisited *d, const char *dirPath) {
    iString *line = new_String();
    iFile *f = newCStr_File(concatPath_CStr(dirPath, "visited.txt"));
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        lock_Mutex(d->mtx);
        iConstForEach(Array, i, &d->visited.values) {
            const iVisitedUrl *item = i.value;
            iDate date;
            init_Date(&date, &item->when);
            format_String(line,
                          "%04d-%02d-%02dT%02d:%02d:%02d %s\n",
                          date.year,
                          date.month,
                          date.day,
                          date.hour,
                          date.minute,
                          date.second,
                          cstr_String(&item->url));
            writeData_File(f, cstr_String(line), size_String(line));
        }
        unlock_Mutex(d->mtx);
    }
    iRelease(f);
    delete_String(line);
}

void load_Visited(iVisited *d, const char *dirPath) {
    iFile *f = newCStr_File(concatPath_CStr(dirPath, "visited.txt"));
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        lock_Mutex(d->mtx);
        const iRangecc src  = range_Block(collect_Block(readAll_File(f)));
        iRangecc       line = iNullRange;
        iTime          now;
        initCurrent_Time(&now);
        while (nextSplit_Rangecc(src, "\n", &line)) {
            int y, m, D, H, M, S;
            sscanf(line.start, "%04d-%02d-%02dT%02d:%02d:%02d ", &y, &m, &D, &H, &M, &S);
            if (!y) break;
            iVisitedUrl item;
            init_VisitedUrl(&item);
            init_Time(
                &item.when,
                &(iDate){ .year = y, .month = m, .day = D, .hour = H, .minute = M, .second = S });
            if (secondsSince_Time(&now, &item.when) > maxAgeVisited_Visited_) {
                continue; /* Too old. */
            }
            initRange_String(&item.url, (iRangecc){ line.start + 20, line.end });
            insert_SortedArray(&d->visited, &item);
        }
        unlock_Mutex(d->mtx);
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

void visitUrl_Visited(iVisited *d, const iString *url) {
    iVisitedUrl visit;
    init_VisitedUrl(&visit);
    set_String(&visit.url, url);
    size_t pos;
    lock_Mutex(d->mtx);
    if (locate_SortedArray(&d->visited, &visit, &pos)) {
        iVisitedUrl *old = at_SortedArray(&d->visited, pos);
        if (cmpNewer_VisitedUrl_(&visit, old)) {
            old->when = visit.when;
            unlock_Mutex(d->mtx);
            deinit_VisitedUrl(&visit);
            return;
        }
    }
    insert_SortedArray(&d->visited, &visit);
    unlock_Mutex(d->mtx);
}

void removeUrl_Visited(iVisited *d, const iString *url) {
    iGuardMutex(d->mtx, {
        size_t pos = find_Visited_(d, url);
        if (pos != iInvalidPos) {
            deinit_VisitedUrl(at_SortedArray(&d->visited, pos));
            remove_Array(&d->visited.values, pos);
        }
    });
}

iTime urlVisitTime_Visited(const iVisited *d, const iString *url) {
    iVisitedUrl item;
    size_t pos;
    iZap(item);
    initCopy_String(&item.url, url);
    lock_Mutex(d->mtx);
    if (locate_SortedArray(&d->visited, &item, &pos)) {
        item.when = ((const iVisitedUrl *) constAt_SortedArray(&d->visited, pos))->when;
    }
    unlock_Mutex(d->mtx);
    deinit_String(&item.url);
    return item.when;
}

static int cmpWhenDescending_VisitedUrlPtr_(const void *a, const void *b) {
    const iVisitedUrl *s = *(const void **) a, *t = *(const void **) b;
    return -cmp_Time(&s->when, &t->when);
}

const iArray *list_Visited(const iVisited *d, size_t count) {
    iPtrArray *urls = collectNew_PtrArray();
    iGuardMutex(d->mtx, {
        iConstForEach(Array, i, &d->visited.values) {
            pushBack_PtrArray(urls, i.value);
        }
    });
    sort_Array(urls, cmpWhenDescending_VisitedUrlPtr_);
    if (count > 0 && size_Array(urls) > count) {
        resize_Array(urls, count);
    }
    return urls;
}
