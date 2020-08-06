#pragma once

#include "gmrequest.h"

#include <the_Foundation/array.h>
#include <the_Foundation/string.h>
#include <the_Foundation/time.h>

iDeclareType(RecentUrl)
iDeclareTypeConstruction(RecentUrl)

struct Impl_RecentUrl {
    iString      url;
    int          scrollY;        /* unit is gap_UI */
    iGmResponse *cachedResponse; /* kept in memory for quicker back navigation */
};

iDeclareType(VisitedUrl)
iDeclareTypeConstruction(VisitedUrl)

struct Impl_VisitedUrl {
    iString url;
    iTime   when;
};

/*----------------------------------------------------------------------------------------------*/

iDeclareType(History)
iDeclareTypeConstruction(History)

void    clear_History   (iHistory *);

void    load_History    (iHistory *, const char *dirPath);
void    save_History    (const iHistory *, const char *dirPath);

iRecentUrl *
        recentUrl_History           (iHistory *, size_t pos);
iRecentUrl *
        mostRecentUrl_History       (iHistory *);

const iString *
        url_History                 (const iHistory *, size_t pos);
const iRecentUrl *
        constRecentUrl_History      (const iHistory *d, size_t pos);
const iRecentUrl *
        constMostRecentUrl_History  (const iHistory *);
iTime   urlVisitTime_History        (const iHistory *, const iString *url);
const iGmResponse *
        cachedResponse_History      (const iHistory *);

void    addUrl_History              (iHistory *, const iString *url); /* adds to the stack of recents */
void    setCachedResponse_History   (iHistory *, const iGmResponse *response);
void    visitUrl_History            (iHistory *, const iString *url); /* adds URL to the visited URLs set */
void    replace_History             (iHistory *, const iString *url);
iBool   goBack_History              (iHistory *);
iBool   goForward_History           (iHistory *);
