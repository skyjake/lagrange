#include "history.h"
#include "app.h"

#include <the_Foundation/file.h>
#include <the_Foundation/sortedarray.h>

static const size_t maxSize_History_ = 5000;

void init_HistoryItem(iHistoryItem *d) {
    initCurrent_Time(&d->when);
    init_String(&d->url);
}

void deinit_HistoryItem(iHistoryItem *d) {
    deinit_String(&d->url);
}

struct Impl_History {
    iArray history;
    size_t historyPos; /* zero at the latest item */
    iSortedArray visitedUrls;
};

iDefineTypeConstruction(History)

static int cmp_VisitedUrls_(const void *a, const void *b) {
    const iHistoryItem *elem[2] = { a, b };
    return cmpString_String(&elem[0]->url, &elem[1]->url);
}

static int cmpWhen_HistoryItem_(const void *old, const void *ins) {
    const iHistoryItem *elem[2] = { old, ins };
    const double tOld = seconds_Time(&elem[0]->when), tIns = seconds_Time(&elem[1]->when);
    return tIns > tOld;
}

static void updateVisitedUrls_History_(iHistory *d) {
    clear_SortedArray(&d->visitedUrls);
    iConstForEach(Array, i, &d->history) {
        insertIf_SortedArray(&d->visitedUrls, &i.value, cmpWhen_HistoryItem_);
    }
}

void init_History(iHistory *d) {
    init_Array(&d->history, sizeof(iHistoryItem));
    d->historyPos = 0;
    init_SortedArray(&d->visitedUrls, sizeof(const iHistoryItem *), cmp_VisitedUrls_);
}

void deinit_History(iHistory *d) {
    deinit_SortedArray(&d->visitedUrls);
    clear_History(d);
    deinit_Array(&d->history);
}

void save_History(const iHistory *d, const iString *path) {
    iFile *f = new_File(path);
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        iString *line = new_String();
        iConstForEach(Array, i, &d->history) {
            const iHistoryItem *item = i.value;
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
        delete_String(line);
    }
    iRelease(f);
}

void load_History(iHistory *d, const iString *path) {
    iFile *f = new_File(path);
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        iString *src = newBlock_String(collect_Block(readAll_File(f)));
        const iRangecc range = range_String(src);
        iRangecc line = iNullRange;
        while (nextSplit_Rangecc(&range, "\n", &line)) {
            int y, m, D, H, M, S;
            sscanf(line.start, "%04d-%02d-%02dT%02d:%02d:%02d", &y, &m, &D, &H, &M, &S);
            if (!y) break;
            iHistoryItem item;
            init_HistoryItem(&item);
            init_Time(
                &item.when,
                &(iDate){ .year = y, .month = m, .day = D, .hour = H, .minute = M, .second = S });
            initRange_String(&item.url, (iRangecc){ line.start + 20, line.end });
            pushBack_Array(&d->history, &item);
        }
        delete_String(src);
    }
    iRelease(f);
}

void clear_History(iHistory *d) {
    iForEach(Array, i, &d->history) {
        deinit_HistoryItem(i.value);
    }
    clear_Array(&d->history);
}

iHistoryItem *itemAtPos_History(iHistory *d, size_t pos) {
    if (isEmpty_Array(&d->history)) return NULL;
    return &value_Array(&d->history, size_Array(&d->history) - 1 - pos, iHistoryItem);
}

iHistoryItem *item_History(iHistory *d) {
    return itemAtPos_History(d, d->historyPos);
}

const iString *url_History(iHistory *d, size_t pos) {
    const iHistoryItem *item = itemAtPos_History(d, pos);
    if (item) {
        return &item->url;
    }
    return collectNew_String();
}

void addUrl_History(iHistory *d, const iString *url ){
    /* Cut the trailing history items. */
    if (d->historyPos > 0) {
        for (size_t i = 0; i < d->historyPos - 1; i++) {
            deinit_HistoryItem(itemAtPos_History(d, i));
        }
        removeN_Array(
            &d->history, size_Array(&d->history) - d->historyPos, iInvalidSize);
        d->historyPos = 0;
    }
    /* Insert new item. */
    const iHistoryItem *lastItem = itemAtPos_History(d, 0);
    if (!lastItem || cmpString_String(&lastItem->url, url) != 0) {
        iHistoryItem item;
        init_HistoryItem(&item);
        set_String(&item.url, url);
        pushBack_Array(&d->history, &item);
        /* Don't make it too long. */
        if (size_Array(&d->history) > maxSize_History_) {
            deinit_HistoryItem(front_Array(&d->history));
            remove_Array(&d->history, 0);
        }
    }
}

iBool goBack_History(iHistory *d) {
    if (d->historyPos < size_Array(&d->history) - 1) {
        d->historyPos++;
        postCommandf_App("open history:1 url:%s",
                         cstr_String(url_History(d, d->historyPos)));
        return iTrue;
    }
    return iFalse;
}

iBool goForward_History(iHistory *d) {
    if (d->historyPos > 0) {
        d->historyPos--;
        postCommandf_App("open history:1 url:%s",
                         cstr_String(url_History(d, d->historyPos)));
        return iTrue;
    }
    return iFalse;
}

iTime urlVisitTime_History(const iHistory *d, const iString *url) {
    iHistoryItem item;
    size_t pos;
    iZap(item);
    initCopy_String(&item.url, url);
    if (locate_SortedArray(&d->visitedUrls, &(const void *){ &item }, &pos)) {
        item.when = ((const iHistoryItem *) constAt_SortedArray(&d->visitedUrls, pos))->when;
    }
    deinit_String(&item.url);
    return item.when;
}

void print_History(const iHistory *d) {
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

