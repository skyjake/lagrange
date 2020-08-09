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

/*----------------------------------------------------------------------------------------------*/

iDeclareType(History)
iDeclareTypeConstruction(History)

iHistory *  copy_History                (const iHistory *);

void        clear_History               (iHistory *);
void        load_History                (iHistory *, const char *dirPath);
void        add_History                 (iHistory *, const iString *url);
void        replace_History             (iHistory *, const iString *url);
void        setCachedResponse_History   (iHistory *, const iGmResponse *response);
iBool       goBack_History              (iHistory *);
iBool       goForward_History           (iHistory *);
iRecentUrl *recentUrl_History           (iHistory *, size_t pos);
iRecentUrl *mostRecentUrl_History       (iHistory *);

void        save_History                (const iHistory *, const char *dirPath);
const iString *
            url_History                 (const iHistory *, size_t pos);
const iRecentUrl *
            constRecentUrl_History      (const iHistory *d, size_t pos);
const iRecentUrl *
            constMostRecentUrl_History  (const iHistory *);
const iGmResponse *
            cachedResponse_History      (const iHistory *);

