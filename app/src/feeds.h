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
#include <the_Foundation/stringarray.h>
#include <the_Foundation/time.h>

iDeclareType(FeedEntry)
iDeclareTypeConstruction(FeedEntry)
iDeclareTypeSerialization(FeedEntry)

struct Impl_FeedEntry {
    iTime    posted;
    iTime    discovered;
    iString  url;
    iString  title;
    iBool    isHeading;   /* URL fragment points to a heading */
    iBool    isTinylog;   /* URL is a virtual "local:/tinylog/" entry */
    iRangei  sourceRange; /* tinylog: entry section in the cache file; runtime only, may be stale */
    uint32_t bookmarkId;  /* note: runtime only, not a persistent ID */
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
void    pause_Feeds             (void); /* app moved to background */
void    resume_Feeds            (void); /* retries requests interrupted by the pause */
iBool   isRefreshing_Feeds      (void);
void    remapBookmarkIds_Feeds  (const iArray *idMappings); /* SyncIdMapping elements */
void    removeEntries_Feeds     (uint32_t feedBookmarkId);
void    markEntryAsRead_Feeds   (uint32_t feedBookmarkId, const iString *entryUrl, iBool isRead);
iBool   isUnreadEntry_Feeds     (uint32_t feedBookmarkId, const iString *entryUrl);
iBool   isEntryUrl_Feeds        (const iString *url); /* visited URL of any feed entry */
void    markAllEntriesAsRead_Feeds(void);
void    resetKnownEntries_Feeds (void);

void    resetFeed_Feeds         (uint32_t feedBookmarkId); /* subscription type changed */

/* Tinylog entries. */
iDeclareType(TinylogInfo)

struct Impl_TinylogInfo {
    uint32_t bookmarkId;
    iString  author;
    iString  avatar;
    iTime    posted;
};

void            init_TinylogInfo        (iTinylogInfo *);
void            deinit_TinylogInfo      (iTinylogInfo *);

uint32_t        tinylogBookmark_Feeds   (const iString *entryUrl); /* zero if not an entry */
iBool           tinylogInfo_Feeds       (const iString *entryUrl, iTinylogInfo *info_out);
iString *       responseLink_Feeds      (const iString *entryUrl); /* NULL if not an entry */

/* Gemini URLs on a page that could be subscribed to. Returns a collected array. */
const iStringArray *listSubscribableUrls_Feeds(const iString *source, iBool isPlainText,
                                               const iString *pageUrl);

enum iFeedType {
    autoDetect_FeedType,
    gemini_FeedType,
    tinylog_FeedType,
};

/* New bookmarks are added in a new folder. Returns the number of new subscriptions. */
size_t  subscribeAll_Feeds      (const iStringArray *urls, const iString *folderTitle,
                                 enum iFeedType type);

const iPtrArray *   listEntries_Feeds   (void);
const iPtrArray *   listMatchingEntries_Feeds(const char *urlPrefix);
const iPtrArray *   listVisibleEntries_Feeds(iBool unreadOnly, const iString *openUrl);
size_t              numSubscribed_Feeds (void);
size_t              numUnread_Feeds     (void);
