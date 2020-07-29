#pragma once

#include <the_Foundation/array.h>
#include <the_Foundation/string.h>
#include <the_Foundation/time.h>

iDeclareType(History)
iDeclareType(HistoryItem)

struct Impl_HistoryItem {
    iTime   when;
    iString url;
};

iDeclareTypeConstruction(HistoryItem)

struct Impl_History {
    iArray history;
    size_t historyPos; /* zero at the latest item */
};

iDeclareTypeConstruction(History)

void    clear_History   (iHistory *);

void    load_History    (iHistory *, const iString *path);
void    save_History    (const iHistory *, const iString *path);

iHistoryItem *  itemAtPos_History   (iHistory *, size_t pos);
const iString * url_History         (iHistory *, size_t pos);
iTime           urlVisitTime_History(const iHistory *, const iString *url);
void            print_History       (const iHistory *);

iLocalDef iHistoryItem *item_History(iHistory *d) {
    return itemAtPos_History(d, d->historyPos);
}

void    addUrl_History      (iHistory *, const iString *url);

iBool   goBack_History      (iHistory *);
iBool   goForward_History   (iHistory *);
