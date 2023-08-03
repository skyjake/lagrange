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

#pragma once

#include "defs.h"
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/string.h>
#include <the_Foundation/time.h>

iDeclareType(FeedEntry)
iDeclareTypeConstruction(FeedEntry)

struct Impl_FeedEntry {
    iTime posted;
    iTime discovered;
    iString url;
    iString title;
    iBool isHeading; /* URL fragment points to a heading */
    uint32_t bookmarkId; /* note: runtime only, not a persistent ID */
};

iLocalDef iBool isHidden_FeedEntry(const iFeedEntry *d) {
    return !isValid_Time(&d->discovered);
}

const iString * url_FeedEntry       (const iFeedEntry *);
iBool           isUnread_FeedEntry  (const iFeedEntry *);

/*----------------------------------------------------------------------------------------------*/

void    init_Feeds              (const char *saveDir);
void    deinit_Feeds            (void);
void    refresh_Feeds           (void);
void    setRefreshInterval_Feeds(enum iFeedInterval feedInterval);
void    refreshFinished_Feeds   (void); /* called on "feeds.refresh.finished" */
void    removeEntries_Feeds     (uint32_t feedBookmarkId);
void    markEntryAsRead_Feeds   (uint32_t feedBookmarkId, const iString *entryUrl, iBool isRead);
iBool   isUnreadEntry_Feeds     (uint32_t feedBookmarkId, const iString *entryUrl);

const iPtrArray *   listEntries_Feeds   (void);
const iString *     entryListPage_Feeds (void);
size_t              numSubscribed_Feeds (void);
size_t              numUnread_Feeds     (void);
