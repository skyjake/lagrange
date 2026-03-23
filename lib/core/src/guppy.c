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

#include "lagrange/guppy.h"
#include <the_Foundation/address.h>
#include <the_Foundation/regexp.h>

enum iGuppyState {
    none_GuppyState,
    inProgress_GuppyState,
    invalidResponse_GuppyState,
    inputRequired_GuppyState,
    redirect_GuppyState,
    error_GuppyState,
    finished_GuppyState,
};

iDefineObjectConstruction(Guppy);
iDefineClass(Guppy);

iDefineAudienceGetter(Guppy, timeout);
iDefineAudienceGetter(Guppy, error);

static iRegExp *metaPattern_;

static void request_Guppy_(iGuppy *d) {
    write_Datagram(d->datagram, utf8_String(collectNewFormat_String("%s\r\n", cstr_String(d->url))));
}

static void ack_Guppy_(iGuppy *d, const int seq) {
    write_Datagram(d->datagram, utf8_String(collectNewFormat_String("%d\r\n", seq)));
}

static iThreadResult retryThread_Guppy_(iThread *thread) {
    iGuppy *d = userData_Thread(thread);
    lock_Mutex(&d->timerMutex);
    while (d->timerRunning) {
        iTime timeout;
        initTimeout_Time(&timeout, 0.1);
        waitTimeout_Condition(&d->timerCond, &d->timerMutex, &timeout);
        if (!d->timerRunning) break;
        unlock_Mutex(&d->timerMutex);
        lock_Mutex(d->mtx);
        /* Stop the session on timeout */
        if (isValid_Time(&d->firstSent) && elapsedSeconds_Time(&d->firstSent) >= 6.0) {
            unlock_Mutex(d->mtx);
            iGuardMutex(&d->timerMutex, d->timerRunning = iFalse);
            iNotifyAudience(d, timeout, GuppyTimeout);
            return 0;
        }
        /* Resend the request if we're still waiting for the first chunk */
        if (!d->firstSeq && isConnected_Datagram(d->datagram) &&
            elapsedSeconds_Time(&d->lastSent) >= 1.0) {
            request_Guppy_(d);
            d->lastSent = now_Time();
        }
        /* Resend the last ack if we're still waiting for more chunks */
        else if (d->currentSeq && elapsedSeconds_Time(&d->lastSent) >= 0.5) {
            ack_Guppy_(d, d->currentSeq);
            d->lastSent = now_Time();
        }
        unlock_Mutex(d->mtx);
        lock_Mutex(&d->timerMutex);
    }
    unlock_Mutex(&d->timerMutex);
    return 0;
}

static void addressLookupFinished_Guppy_(iGuppy *d, iAddress *address);

void init_Guppy(iGuppy *d) {
    if (!metaPattern_) {
        metaPattern_ = new_RegExp("^([0-9]+)(.*)", 0);
    }
    d->state = none_GuppyState;
    d->mtx = NULL;
    d->url = NULL;
    d->address = NULL;
    d->datagram = new_Datagram();
    d->body = NULL;
    d->timer = NULL;
    init_Mutex(&d->timerMutex);
    init_Condition(&d->timerCond);
    d->timerRunning = iFalse;
    d->firstSent = (iTime){ 0 };
    d->lastSent  = (iTime){ 0 };
    iForIndices(i, d->chunks) {
        d->chunks[i].seq = 0;
        init_Block(&d->chunks[i].data, 0);
    }
    d->firstSeq = 0;
    d->lastSeq = 0;
    d->currentSeq = 0;
    d->timeout = NULL;
    d->error = NULL;
}

void deinit_Guppy(iGuppy *d) {
    if (d->timer) {
        lock_Mutex(&d->timerMutex);
        d->timerRunning = iFalse;
        signal_Condition(&d->timerCond);
        unlock_Mutex(&d->timerMutex);
        join_Thread(d->timer);
        iRelease(d->timer);
        d->timer = NULL;
    }
    deinit_Condition(&d->timerCond);
    deinit_Mutex(&d->timerMutex);
    iForIndices(i, d->chunks) {
        deinit_Block(&d->chunks[i].data);
    }
    delete_Audience(d->timeout);
    delete_Audience(d->error);
    iRelease(d->address);
    iRelease(d->datagram);
}

static void addressLookupFinished_Guppy_(iGuppy *d, iAddress *address) {
    if (!isHostFound_Address(address)) {
        d->state = error_GuppyState;
        iReleasePtr(&d->address);
        iNotifyAudience(d, error, GuppyError);
        return;
    }
    connect_Datagram(d->datagram, d->address);
    request_Guppy_(d);
    d->lastSent = now_Time();
    if (!isValid_Time(&d->firstSent)) {
        d->firstSent = d->lastSent;
    }
    if (!d->timer) {
        lock_Mutex(&d->timerMutex);
        d->timerRunning = iTrue;
        unlock_Mutex(&d->timerMutex);
        d->timer = new_Thread(retryThread_Guppy_);
        setName_Thread(d->timer, "GuppyTimer");
        setUserData_Thread(d->timer, d);
        start_Thread(d->timer);
    }
}

void open_Guppy(iGuppy *d, const iString *host, uint16_t port) {
    iReleasePtr(&d->address);
    if (!openRandom_Datagram(d->datagram)) {
        d->state = error_GuppyState;
        iNotifyAudience(d, error, GuppyError);
        return;
    }
    d->address = new_Address();
    iConnect(Address, d->address, lookupFinished, d, addressLookupFinished_Guppy_);
    lookupCStr_Address(d->address, cstr_String(host), port, udp_SocketType);
    d->state = inProgress_GuppyState;
}

void cancel_Guppy(iGuppy *d) {
    iDisconnectObject(Datagram, d->datagram, message, d);
    iDisconnectObject(Datagram, d->datagram, error, d);
    if (d->address) {
        iDisconnect(Address, d->address, lookupFinished, d, addressLookupFinished_Guppy_);
    }
    /* Signal the timer thread to stop. Don't join here. `cancel_Guppy` may be
       called while d->mtx is held, and the timer thread may be waiting to acquire
       that same mutex. `deinit_Guppy` performs the join. */
    if (d->timer) {
        iGuardMutex(&d->timerMutex, {
            d->timerRunning = iFalse;
            signal_Condition(&d->timerCond);
        });
    }
}

static void storeChunk_Guppy_(iGuppy *d, int seq, const void *data, size_t size) {
    int slot = -1, maxSeq = -1, maxSeqSlot = -1;
    iBool found = false;
    if (!d->firstSeq && seq < INT_MAX) {
        d->firstSeq = seq;
    }
    if (!d->lastSeq && size == 0) {
        d->lastSeq = seq;
        return;
    }
    if ((d->currentSeq && seq <= d->currentSeq) || (d->firstSeq && seq < d->firstSeq) ||
        (d->lastSeq && seq > d->lastSeq)) {
        return;
    }
    iForIndices(i, d->chunks) {
        /* Maybe we already have this chunk */
        if ((found = (d->chunks[i].seq == seq))) {
            break;
        }
        /* We found a slot we can use */
        if (slot < 0 &&
            (d->chunks[i].seq == 0 || (d->firstSeq > 0 && d->chunks[i].seq < d->firstSeq) ||
             (d->lastSeq > 0 && d->chunks[i].seq > d->lastSeq))) {
            slot = i;
        }
        /* This slot is the one we're less likely to need soon */
        if (d->chunks[i].seq > maxSeq) {
            maxSeq = d->chunks[i].seq;
            maxSeqSlot = i;
        }
    }
    if (!found) {
        /* Must free one slot if this is the first chunk but all slots are occupied */
        if (seq == d->firstSeq && slot < 0) {
            slot = maxSeqSlot;
        }
        if (slot >= 0) {
            d->chunks[slot].seq = seq;
            setData_Block(&d->chunks[slot].data, data, size);
        }
    }
}

static void processChunks_Guppy_(iGuppy *d, iBool *notifyUpdate) {
    iBool again = iFalse;
    do {
        /* Append consequentive chunks we have at the moment */
        again = false;
        iForIndices(i, d->chunks) {
            if ((d->currentSeq && d->currentSeq < INT_MAX &&
                 d->chunks[i].seq == d->currentSeq + 1) ||
                (d->currentSeq == 0 && d->firstSeq > 0 && d->chunks[i].seq == d->firstSeq)) {
                append_Block(d->body, &d->chunks[i].data);
                *notifyUpdate = iTrue;
                d->currentSeq = d->chunks[i].seq;
                d->chunks[i].seq = 0;
                clear_Block(&d->chunks[i].data);
                again = i < iElemCount(d->chunks) - 1;
            }
        }
    } while (again);
    /* We're done if the last chunk appended to the buffer is the one before the EOF packet */
    if (d->lastSeq && d->currentSeq == d->lastSeq - 1) {
        d->state = finished_GuppyState;
    }
}

static enum iGuppyState processResponse_Guppy_(iGuppy *d, iBool *notifyUpdate) {
    iBlock *data;
    while (d->state == inProgress_GuppyState &&
           (data = receive_Datagram(d->datagram, NULL)) != NULL) {
        if (!isEmpty_Block(data)) {
            iString header;
            init_String(&header);
            append_Block(&header.chars, data);
            size_t crlf = indexOfCStr_String(&header, "\r\n");
            if (crlf != iInvalidPos) {
                truncate_Block(&header.chars, crlf);
                iRegExpMatch m;
                init_RegExpMatch(&m);
                // iBool first = iFalse;
                if (matchString_RegExp(metaPattern_, &header, &m)) {
                    int seq = toInt_String(collect_String(captured_RegExpMatch(&m, 1)));
                    if (!d->firstSeq) {
                        iString *meta = collect_String(captured_RegExpMatch(&m, 2));
                        size_t metaLength = length_String(meta);
                        if (metaLength > 0) {
                            meta = collect_String(mid_String(meta, 1, metaLength - 1));
                        }
                        switch (seq) {
                            case 0:
                            case 5:
                                d->state = invalidResponse_GuppyState;
                                clear_String(meta);
                                break;
                            case 1:
                                d->state = inputRequired_GuppyState;
                                set_String(d->meta, meta);
                                break;
                            case 3:
                                d->state = redirect_GuppyState;
                                set_String(d->meta, meta);
                                break;
                            case 4:
                                d->state = error_GuppyState;
                                break;
                            default:
                                // first = iTrue;
                                d->state = inProgress_GuppyState;
                                if (meta) {
                                    set_String(d->meta, meta);
                                }
                        }
                    }
                    if (seq >= 6) {
                        ack_Guppy_(d, seq);
                        d->lastSent = now_Time();
                        if (d->state == inProgress_GuppyState) {
                            storeChunk_Guppy_(
                                d, seq, constData_Block(data) + crlf + 2, size_Block(data) - crlf - 2);
                        }
                    }
                }
                else {
                    d->state = invalidResponse_GuppyState;
                }
                processChunks_Guppy_(d, notifyUpdate);
            }
            deinit_String(&header);
        }
        delete_Block(data);
    }
    if (d->state != inProgress_GuppyState) {
        cancel_Guppy(d);
    }
    return d->state;
}

void processResponse_Guppy(iGuppy *d, enum iGmRequestState *state, enum iGmStatusCode *statusCode,
                           iBool *notifyUpdate, iBool *notifyDone) {
    switch (processResponse_Guppy_(d, notifyUpdate)) {
        case invalidResponse_GuppyState:
            *state = finished_GmRequestState;
            *statusCode = invalidHeader_GmStatusCode;
            break;
        case inputRequired_GuppyState:
            *state = finished_GmRequestState;
            *statusCode = input_GmStatusCode;
            break;
        case redirect_GuppyState:
            *state = finished_GmRequestState;
            *statusCode = redirectTemporary_GmStatusCode;
            break;
        case error_GuppyState:
            *state = finished_GmRequestState;
            *statusCode = permanentFailure_GmStatusCode;
            break;
        case finished_GuppyState:
            *state = finished_GmRequestState;
            *statusCode = success_GmStatusCode;
            break;
        default:
            *notifyDone = iFalse;
    }
}
