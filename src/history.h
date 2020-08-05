#pragma once

#include <the_Foundation/array.h>
#include <the_Foundation/string.h>
#include <the_Foundation/time.h>

iDeclareType(HistoryItem)
iDeclareTypeConstruction(HistoryItem)

struct Impl_HistoryItem {
    iTime   when;
    iString url;
    int     scrollY; /* unit is gap_UI */
};

iDeclareType(History)
iDeclareTypeConstruction(History)

void    clear_History   (iHistory *);

void    load_History    (iHistory *, const char *dirPath);
void    save_History    (const iHistory *, const char *dirPath);

iHistoryItem *  itemAtPos_History   (iHistory *, size_t pos);
iHistoryItem *  item_History        (iHistory *);
const iString * url_History         (iHistory *, size_t pos);
iTime           urlVisitTime_History(const iHistory *, const iString *url);
void            print_History       (const iHistory *);

void    addUrl_History      (iHistory *, const iString *url); /* adds to the stack of recents */
void    visitUrl_History    (iHistory *, const iString *url); /* adds URL to the visited URLs set */
void    replace_History     (iHistory *, const iString *url);

iBool   goBack_History      (iHistory *);
iBool   goForward_History   (iHistory *);
