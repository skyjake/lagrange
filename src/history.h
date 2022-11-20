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

#include "gmdocument.h"
#include "gmrequest.h"

#include <the_Foundation/ptrarray.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/string.h>
#include <the_Foundation/stringarray.h>
#include <the_Foundation/time.h>

iDeclareType(RecentUrl)
iDeclareTypeConstruction(RecentUrl)
    
struct Impl_RecentUrl {
    iString      url;
    float        normScrollY;    /* normalized to document height */
    iGmResponse *cachedResponse; /* kept in memory for quicker back navigation */
    iGmDocument *cachedDoc;      /* cached copy of the presentation: layout and media (not serialized) */
    iBlock       setIdentity;    /* fingerprint of identity that was pinned*/
    uint16_t     flags;
};

iDeclareType(MemInfo)

struct Impl_MemInfo {
    size_t cacheSize;   /* number of bytes stored persistently */
    size_t memorySize;  /* number of bytes stored in RAM */
};

/*----------------------------------------------------------------------------------------------*/

iDeclareType(History)
iDeclareTypeConstruction(History)
iDeclareTypeSerialization(History)

iHistory *  copy_History                (const iHistory *);
void        lock_History                (iHistory *);
void        unlock_History              (iHistory *);

void        clear_History               (iHistory *);
void        add_History                 (iHistory *, const iString *url);
void        undo_History                (iHistory *); /* removes the most recent URL */
void        replace_History             (iHistory *, const iString *url);
void        setIdentity_History         (iHistory *, const iBlock *identityFingerprint);
void        setCachedResponse_History   (iHistory *, const iGmResponse *response);
void        setCachedDocument_History   (iHistory *, iGmDocument *doc);
iBool       goBack_History              (iHistory *);
iBool       goForward_History           (iHistory *);
iRecentUrl *precedingLocked_History     (iHistory *); /* requires manual lock/unlock! */
iRecentUrl *recentUrl_History           (iHistory *, size_t pos);
iRecentUrl *mostRecentUrl_History       (iHistory *);
//iRecentUrl *findUrl_History             (iHistory *, const iString *url, int timeDir);

void        clearCache_History                  (iHistory *);
size_t      pruneLeastImportant_History         (iHistory *);
size_t      pruneLeastImportantMemory_History   (iHistory *);
void        invalidateTheme_History             (iHistory *); /* theme has changed, cached contents need updating */
void        invalidateCachedLayout_History      (iHistory *);

iBool       atNewest_History            (const iHistory *);
iBool       atOldest_History            (const iHistory *);

const iStringArray *   searchContents_History   (const iHistory *, const iRegExp *pattern); /* chronologically ascending */

const iString *
            url_History                 (const iHistory *, size_t pos);
const iRecentUrl *
            constRecentUrl_History      (const iHistory *d, size_t pos);
const iRecentUrl *
            constMostRecentUrl_History  (const iHistory *);
const iGmResponse *
            cachedResponse_History      (const iHistory *);
size_t      cacheSize_History           (const iHistory *);
size_t      memorySize_History          (const iHistory *);

iString *   debugInfo_History           (const iHistory *);
iMemInfo    memoryUsage_History         (const iHistory *);
