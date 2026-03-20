/* Copyright 2023 Dima Krasner <dima@dimakrasner.com>
   Copyright 2024 Jaakko Keränen <jaakko.keranen@iki.fi>

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
#include "gmutil.h"

#include <the_Foundation/audience.h>
#include <the_Foundation/datagram.h>
#include <the_Foundation/mutex.h>
#include <the_Foundation/string.h>

iDeclareType(GuppyChunk);
iDeclareClass(Guppy);

struct Impl_GuppyChunk {
    int    seq;
    iBlock data;
};

struct Impl_Guppy {
    iObject     object;
    int         state;
    iMutex *    mtx;  /* not owned */
    iString *   url;  /* not owned */
    iString *   meta; /* not owned */
    iBlock *    body; /* not owned */
    iAddress *  address;
    iDatagram * datagram;
    int         timer;
    uint64_t    firstSent;
    uint64_t    lastSent;
    iGuppyChunk chunks[16];
    int         firstSeq;
    int         lastSeq;
    int         currentSeq;
    iAudience * timeout;
    iAudience * error;
};

iDeclareObjectConstruction(Guppy);
iDeclareAudienceGetter(Guppy, timeout);
iDeclareAudienceGetter(Guppy, error);
iDeclareNotifyFunc(Guppy, Timeout);
iDeclareNotifyFunc(Guppy, Error);

void    open_Guppy              (iGuppy *, const iString *host, uint16_t port);
void    cancel_Guppy            (iGuppy *);
void    processResponse_Guppy   (iGuppy *, enum iGmRequestState *state,
                                 enum iGmStatusCode *statusCode, iBool *notifyUpdate,
                                 iBool *notifyDone);
