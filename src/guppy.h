/* Copyright 2023 Dima Krasner <dima@dimakrasner.com>

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

#include <the_Foundation/mutex.h>
#include <the_Foundation/string.h>
#include <the_Foundation/socket.h>
#include <the_Foundation/audience.h>

enum iGuppyState {
    none_GuppyState,
    inProgress_GuppyState,
    invalidResponse_GuppyState,
    inputRequired_GuppyState,
    redirect_GuppyState,
    error_GuppyState,
    finished_GuppyState
};

struct GuppyChunk {
    int    seq;
    iBlock data;
};

iDeclareType(Guppy)

struct Impl_Guppy {
    enum iGuppyState  state;
    iMutex *          mtx;
    iString *         url;
    iString *         meta;
    iSocket *         socket;
    iBlock *          body;
    int               timer;
    uint64_t          firstSent;
    uint64_t          lastSent;
    struct GuppyChunk chunks[16];
    int               firstSeq;
    int               lastSeq;
    int               currentSeq;
    iAudience *       timeout;
};

iDeclareTypeConstruction(Guppy)

iDeclareAudienceGetter(Guppy, timeout)

void              open_Guppy                 (iGuppy *);
void              cancel_Guppy               (iGuppy *);
enum iGuppyState  processResponse_Guppy      (iGuppy *, iBool *notifyUpdate);
