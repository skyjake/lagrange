/* Copyright 2026 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "../history.h"
#include <the_Foundation/block.h>
#include <the_Foundation/stream.h>
#include <the_Foundation/string.h>

enum iReloadInterval {
    never_RelodPeriod,
    minute_ReloadInterval,
    fiveMinutes_ReloadInterval,
    fifteenMinutes_ReloadInterval,
    hour_ReloadInterval,
    fourHours_ReloadInterval,
    twicePerDay_ReloadInterval,
    day_ReloadInterval,
    max_ReloadInterval
};

int             seconds_ReloadInterval  (enum iReloadInterval);
const char *    label_ReloadInterval    (enum iReloadInterval);

/*----------------------------------------------------------------------------------------------*/

iDeclareType(PersistentDocumentState)
iDeclareTypeConstruction(PersistentDocumentState)
iDeclareTypeSerialization(PersistentDocumentState)

struct Impl_PersistentDocumentState {
    iHistory *history;
    iString * url;
    iBlock *setIdentity; /* identity (fingerprint) to use for requests, overriding default one */
    enum iReloadInterval reloadInterval;
    int generation;
};

void    serializeWithContent_PersistentDocumentState    (const iPersistentDocumentState *,
                                                         iStream *outs, iBool withContent);
