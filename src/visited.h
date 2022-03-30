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

#include "gmrequest.h"

#include <the_Foundation/ptrarray.h>
#include <the_Foundation/string.h>
#include <the_Foundation/time.h>

iDeclareType(VisitedUrl)
iDeclareTypeConstruction(VisitedUrl)

extern const int maxAge_Visited; /* seconds */

struct Impl_VisitedUrl {
    iString  url;
    iTime    when;
    uint16_t flags;
};

enum iVisitedUrlFlag {
    transient_VisitedUrlFlag = 0x1, /* redirected; don't show in history */
    kept_VisitedUrlFlag      = 0x2, /* don't discard this even after max age */
};

iDeclareType(Visited)
iDeclareTypeConstruction(Visited)

void    clear_Visited           (iVisited *);
void    load_Visited            (iVisited *, const char *dirPath);
void    save_Visited            (const iVisited *, const char *dirPath);
void    serialize_Visited       (const iVisited *, iStream *out);
void    deserialize_Visited     (iVisited *, iStream *ins, iBool mergeKeepingLatest);

iTime   urlVisitTime_Visited    (const iVisited *, const iString *url);
void    visitUrl_Visited        (iVisited *, const iString *url, uint16_t visitFlags); /* adds URL to the visited URLs set */
void    visitUrlTime_Visited    (iVisited *, const iString *url, uint16_t visitFlags, iTime when);
void    setUrlKept_Visited      (iVisited *, const iString *url, iBool isKept); /* URL is marked as (non)discardable */
void    removeUrl_Visited       (iVisited *, const iString *url);
iBool   containsUrl_Visited     (const iVisited *, const iString *url);

const iPtrArray *   list_Visited        (const iVisited *, size_t count); /* returns collected */
const iPtrArray *   listKept_Visited    (const iVisited *);
