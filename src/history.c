#include "history.h"
#include "app.h"

#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <the_Foundation/sortedarray.h>

static const size_t maxStack_History_      = 50;             /* back/forward navigable items */
static const size_t maxAgeVisited_History_ = 3600 * 24 * 30; /* one month */

void init_HistoryItem(iHistoryItem *d) {
    initCurrent_Time(&d->when);
    init_String(&d->url);
    d->scrollY = 0;
}

void deinit_HistoryItem(iHistoryItem *d) {
    deinit_String(&d->url);
}

struct Impl_History {
    iArray stack; /* TODO: should be specific to a DocumentWidget */
    size_t stackPos; /* zero at the latest item */
    iSortedArray visitedUrls;
};

iDefineTypeConstruction(History)

static int cmpUrl_HistoryItem_(const void *a, const void *b) {
    return cmpString_String(&((const iHistoryItem *) a)->url, &((const iHistoryItem *) b)->url);
}

static int cmpNewer_HistoryItem_(const void *insert, const void *existing) {
    return seconds_Time(&((const iHistoryItem *) insert  )->when) >
           seconds_Time(&((const iHistoryItem *) existing)->when);
}

void init_History(iHistory *d) {
    init_Array(&d->stack, sizeof(iHistoryItem));
    d->stackPos = 0;
    init_SortedArray(&d->visitedUrls, sizeof(iHistoryItem), cmpUrl_HistoryItem_);
}

void deinit_History(iHistory *d) {
    clear_History(d);
    deinit_Array(&d->stack);
}

static void writeItems_(const iArray *items, iFile *f) {
    iString *line = new_String();
    iConstForEach(Array, i, items) {
        const iHistoryItem *item = i.value;
        iDate date;
        init_Date(&date, &item->when);
        format_String(line,
                      "%04d-%02d-%02dT%02d:%02d:%02d %04x %s\n",
                      date.year,
                      date.month,
                      date.day,
                      date.hour,
                      date.minute,
                      date.second,
                      item->scrollY,
                      cstr_String(&item->url));
        writeData_File(f, cstr_String(line), size_String(line));
    }
    delete_String(line);
}

void save_History(const iHistory *d, const char *dirPath) {
    iFile *f = newCStr_File(concatPath_CStr(dirPath, "recent.txt"));
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        writeItems_(&d->stack, f);
    }
    iRelease(f);
    f = newCStr_File(concatPath_CStr(dirPath, "visited.txt"));
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        writeItems_(&d->visitedUrls.values, f);
    }
    iRelease(f);
}

static void loadItems_(iArray *items, iFile *f, double maxAge) {
    const iRangecc src  = range_Block(collect_Block(readAll_File(f)));
    iRangecc       line = iNullRange;
    iTime          now;
    initCurrent_Time(&now);
    while (nextSplit_Rangecc(&src, "\n", &line)) {
        int y, m, D, H, M, S, scroll = 0;
        sscanf(line.start, "%04d-%02d-%02dT%02d:%02d:%02d %04x", &y, &m, &D, &H, &M, &S, &scroll);
        if (!y) break;
        iHistoryItem item;
        init_HistoryItem(&item);
        item.scrollY = scroll;
        init_Time(
            &item.when,
            &(iDate){ .year = y, .month = m, .day = D, .hour = H, .minute = M, .second = S });
        if (maxAge > 0.0 && secondsSince_Time(&now, &item.when) > maxAge) {
            continue; /* Too old. */
        }
        initRange_String(&item.url, (iRangecc){ line.start + 25, line.end });
        pushBack_Array(items, &item);
    }
}

void load_History(iHistory *d, const char *dirPath) {
    iFile *f = newCStr_File(concatPath_CStr(dirPath, "recent.txt"));
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        loadItems_(&d->stack, f, 0);
    }
    iRelease(f);
    f = newCStr_File(concatPath_CStr(dirPath, "visited.txt"));
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        loadItems_(&d->visitedUrls.values, f, maxAgeVisited_History_);
    }
    iRelease(f);
}

void clear_History(iHistory *d) {
    iForEach(Array, s, &d->stack) {
        deinit_HistoryItem(s.value);
    }
    clear_Array(&d->stack);
    iForEach(Array, v, &d->visitedUrls.values) {
        deinit_HistoryItem(v.value);
    }
    clear_SortedArray(&d->visitedUrls);
}

iHistoryItem *itemAtPos_History(iHistory *d, size_t pos) {
    if (isEmpty_Array(&d->stack)) return NULL;
    return &value_Array(&d->stack, size_Array(&d->stack) - 1 - pos, iHistoryItem);
}

iHistoryItem *item_History(iHistory *d) {
    return itemAtPos_History(d, d->stackPos);
}

const iString *url_History(iHistory *d, size_t pos) {
    const iHistoryItem *item = itemAtPos_History(d, pos);
    if (item) {
        return &item->url;
    }
    return collectNew_String();
}

static void addVisited_History_(iHistory *d, const iString *url) {
    iHistoryItem visit;
    init_HistoryItem(&visit);
    set_String(&visit.url, url);
    size_t pos;
    if (locate_SortedArray(&d->visitedUrls, &visit, &pos)) {
        iHistoryItem *old = at_SortedArray(&d->visitedUrls, pos);
        if (cmpNewer_HistoryItem_(&visit, old)) {
            old->when = visit.when;
            deinit_HistoryItem(&visit);
            return;
        }
    }
    insert_SortedArray(&d->visitedUrls, &visit);
}

void replace_History(iHistory *d, const iString *url) {
    /* Update in the history. */
    iHistoryItem *item = item_History(d);
    if (item) {
        set_String(&item->url, url);
    }
    addVisited_History_(d, url);
}

void addUrl_History(iHistory *d, const iString *url ){
    /* Cut the trailing history items. */
    if (d->stackPos > 0) {
        for (size_t i = 0; i < d->stackPos - 1; i++) {
            deinit_HistoryItem(itemAtPos_History(d, i));
        }
        removeN_Array(&d->stack, size_Array(&d->stack) - d->stackPos, iInvalidSize);
        d->stackPos = 0;
    }
    /* Insert new item. */
    const iHistoryItem *lastItem = itemAtPos_History(d, 0);
    if (!lastItem || cmpString_String(&lastItem->url, url) != 0) {
        iHistoryItem item;
        init_HistoryItem(&item);
        set_String(&item.url, url);
        pushBack_Array(&d->stack, &item);
        /* Limit the number of items. */
        if (size_Array(&d->stack) > maxStack_History_) {
            deinit_HistoryItem(front_Array(&d->stack));
            remove_Array(&d->stack, 0);
        }
    }
    addVisited_History_(d, url);
}

void visitUrl_History(iHistory *d, const iString *url) {
    addVisited_History_(d, url);
}

iBool goBack_History(iHistory *d) {
    if (d->stackPos < size_Array(&d->stack) - 1) {
        d->stackPos++;
        postCommandf_App("open history:1 scroll:%d url:%s",
                         item_History(d)->scrollY,
                         cstr_String(url_History(d, d->stackPos)));
        return iTrue;
    }
    return iFalse;
}

iBool goForward_History(iHistory *d) {
    if (d->stackPos > 0) {
        d->stackPos--;
        postCommandf_App("open history:1 url:%s", cstr_String(url_History(d, d->stackPos)));
        return iTrue;
    }
    return iFalse;
}

iTime urlVisitTime_History(const iHistory *d, const iString *url) {
    iHistoryItem item;
    size_t pos;
    iZap(item);
    initCopy_String(&item.url, url);
    if (locate_SortedArray(&d->visitedUrls, &item, &pos)) {
        item.when = ((const iHistoryItem *) constAt_SortedArray(&d->visitedUrls, pos))->when;
    }
    deinit_String(&item.url);
    return item.when;
}

void print_History(const iHistory *d) {
    iUnused(d);
#if 0
    iConstForEach(Array, i, &d->history) {
        const size_t idx = index_ArrayConstIterator(&i);
        printf("%s[%zu]: %s\n",
               d->historyPos == size_Array(&d->history) - idx - 1 ? "->" : "  ",
               idx,
               cstr_String(&((const iHistoryItem *) i.value)->url));
    }
    fflush(stdout);
#endif
}

