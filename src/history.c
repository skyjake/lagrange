#include "history.h"
#include "app.h"

#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <the_Foundation/sortedarray.h>

static const size_t maxStack_History_      = 50;             /* back/forward navigable items */
static const size_t maxAgeVisited_History_ = 3600 * 24 * 30; /* one month */

void init_RecentUrl(iRecentUrl *d) {
    init_String(&d->url);
    d->scrollY = 0;
    d->cachedResponse = NULL;
}

void deinit_RecentUrl(iRecentUrl *d) {
    deinit_String(&d->url);
    delete_GmResponse(d->cachedResponse);
}

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

struct Impl_History {
    iArray       recent;    /* TODO: should be specific to a DocumentWidget */
    size_t       recentPos; /* zero at the latest item */
    iSortedArray visited;
};

iDefineTypeConstruction(History)

void init_History(iHistory *d) {
    init_Array(&d->recent, sizeof(iRecentUrl));
    d->recentPos = 0;
    init_SortedArray(&d->visited, sizeof(iVisitedUrl), cmpUrl_VisitedUrl_);
}

void deinit_History(iHistory *d) {
    clear_History(d);
    deinit_Array(&d->recent);
    deinit_SortedArray(&d->visited);
}

void save_History(const iHistory *d, const char *dirPath) {
    iString *line = new_String();
    iFile *f = newCStr_File(concatPath_CStr(dirPath, "recent.txt"));
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        iConstForEach(Array, i, &d->recent) {
            const iRecentUrl *item = i.value;
            format_String(line, "%04x %s\n", item->scrollY, cstr_String(&item->url));
            writeData_File(f, cstr_String(line), size_String(line));
        }
    }
    iRelease(f);
    f = newCStr_File(concatPath_CStr(dirPath, "visited.txt"));
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
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
    }
    iRelease(f);
    delete_String(line);
}

void load_History(iHistory *d, const char *dirPath) {
    iFile *f = newCStr_File(concatPath_CStr(dirPath, "recent.txt"));
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        const iRangecc src  = range_Block(collect_Block(readAll_File(f)));
        iRangecc       line = iNullRange;
        while (nextSplit_Rangecc(&src, "\n", &line)) {
            iRangecc nonwhite = line;
            trim_Rangecc(&nonwhite);
            if (isEmpty_Range(&nonwhite)) continue;
            int scroll = 0;
            sscanf(nonwhite.start, "%04x", &scroll);
            iRecentUrl item;
            init_RecentUrl(&item);
            item.scrollY = scroll;
            initRange_String(&item.url, (iRangecc){ nonwhite.start + 5, nonwhite.end });
            pushBack_Array(&d->recent, &item);
        }
    }
    iRelease(f);
    f = newCStr_File(concatPath_CStr(dirPath, "visited.txt"));
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        const iRangecc src  = range_Block(collect_Block(readAll_File(f)));
        iRangecc       line = iNullRange;
        iTime          now;
        initCurrent_Time(&now);
        while (nextSplit_Rangecc(&src, "\n", &line)) {
            int y, m, D, H, M, S;
            sscanf(line.start, "%04d-%02d-%02dT%02d:%02d:%02d ", &y, &m, &D, &H, &M, &S);
            if (!y) break;
            iVisitedUrl item;
            init_VisitedUrl(&item);
            init_Time(
                &item.when,
                &(iDate){ .year = y, .month = m, .day = D, .hour = H, .minute = M, .second = S });
            if (secondsSince_Time(&now, &item.when) > maxAgeVisited_History_) {
                continue; /* Too old. */
            }
            initRange_String(&item.url, (iRangecc){ line.start + 20, line.end });
            insert_SortedArray(&d->visited, &item);
        }
    }
    iRelease(f);
}

void clear_History(iHistory *d) {
    iForEach(Array, s, &d->recent) {
        deinit_RecentUrl(s.value);
    }
    clear_Array(&d->recent);
    iForEach(Array, v, &d->visited.values) {
        deinit_VisitedUrl(v.value);
    }
    clear_SortedArray(&d->visited);
}

iRecentUrl *recentUrl_History(iHistory *d, size_t pos) {
    if (isEmpty_Array(&d->recent)) return NULL;
    return &value_Array(&d->recent, size_Array(&d->recent) - 1 - pos, iRecentUrl);
}

const iRecentUrl *constRecentUrl_History(const iHistory *d, size_t pos) {
    if (isEmpty_Array(&d->recent)) return NULL;
    return &constValue_Array(&d->recent, size_Array(&d->recent) - 1 - pos, iRecentUrl);
}

iRecentUrl *mostRecentUrl_History(iHistory *d) {
    return recentUrl_History(d, d->recentPos);
}

const iRecentUrl *constMostRecentUrl_History(const iHistory *d) {
    return constRecentUrl_History(d, d->recentPos);
}

const iString *url_History(const iHistory *d, size_t pos) {
    const iRecentUrl *item = constRecentUrl_History(d, pos);
    if (item) {
        return &item->url;
    }
    return collectNew_String();
}

static void addVisited_History_(iHistory *d, const iString *url) {
    iVisitedUrl visit;
    init_VisitedUrl(&visit);
    set_String(&visit.url, url);
    size_t pos;
    if (locate_SortedArray(&d->visited, &visit, &pos)) {
        iVisitedUrl *old = at_SortedArray(&d->visited, pos);
        if (cmpNewer_VisitedUrl_(&visit, old)) {
            old->when = visit.when;
            deinit_VisitedUrl(&visit);
            return;
        }
    }
    insert_SortedArray(&d->visited, &visit);
}

void replace_History(iHistory *d, const iString *url) {
    /* Update in the history. */
    iRecentUrl *item = mostRecentUrl_History(d);
    if (item) {
        set_String(&item->url, url);
    }
    addVisited_History_(d, url);
}

void addUrl_History(iHistory *d, const iString *url ){
    /* Cut the trailing history items. */
    if (d->recentPos > 0) {
        for (size_t i = 0; i < d->recentPos - 1; i++) {
            deinit_RecentUrl(recentUrl_History(d, i));
        }
        removeN_Array(&d->recent, size_Array(&d->recent) - d->recentPos, iInvalidSize);
        d->recentPos = 0;
    }
    /* Insert new item. */
    const iRecentUrl *lastItem = recentUrl_History(d, 0);
    if (!lastItem || cmpString_String(&lastItem->url, url) != 0) {
        iRecentUrl item;
        init_RecentUrl(&item);
        set_String(&item.url, url);
        pushBack_Array(&d->recent, &item);
        /* Limit the number of items. */
        if (size_Array(&d->recent) > maxStack_History_) {
            deinit_RecentUrl(front_Array(&d->recent));
            remove_Array(&d->recent, 0);
        }
    }
    addVisited_History_(d, url);
}

void visitUrl_History(iHistory *d, const iString *url) {
    addVisited_History_(d, url);
}

iBool goBack_History(iHistory *d) {
    if (d->recentPos < size_Array(&d->recent) - 1) {
        d->recentPos++;
        postCommandf_App("open history:1 scroll:%d url:%s",
                         mostRecentUrl_History(d)->scrollY,
                         cstr_String(url_History(d, d->recentPos)));
        return iTrue;
    }
    return iFalse;
}

iBool goForward_History(iHistory *d) {
    if (d->recentPos > 0) {
        d->recentPos--;
        postCommandf_App("open history:1 url:%s", cstr_String(url_History(d, d->recentPos)));
        return iTrue;
    }
    return iFalse;
}

iTime urlVisitTime_History(const iHistory *d, const iString *url) {
    iVisitedUrl item;
    size_t pos;
    iZap(item);
    initCopy_String(&item.url, url);
    if (locate_SortedArray(&d->visited, &item, &pos)) {
        item.when = ((const iVisitedUrl *) constAt_SortedArray(&d->visited, pos))->when;
    }
    deinit_String(&item.url);
    return item.when;
}

const iGmResponse *cachedResponse_History(const iHistory *d) {
    const iRecentUrl *item = constMostRecentUrl_History(d);
    return item ? item->cachedResponse : NULL;
}

void setCachedResponse_History(iHistory *d, const iGmResponse *response) {
    iRecentUrl *item = mostRecentUrl_History(d);
    if (item) {
        delete_GmResponse(item->cachedResponse);
        item->cachedResponse = NULL;
        if (category_GmStatusCode(response->statusCode) == categorySuccess_GmStatusCode) {
            item->cachedResponse = copy_GmResponse(response);
        }
    }
}
