#include "gmrequest.h"
#include "gmutil.h"

#include <the_Foundation/file.h>
#include <the_Foundation/tlsrequest.h>
#include <the_Foundation/mutex.h>

#include <SDL_timer.h>

static const int BODY_TIMEOUT = 1500; /* ms */

enum iGmRequestState {
    initialized_GmRequestState,
    receivingHeader_GmRequestState,
    receivingBody_GmRequestState,
    finished_GmRequestState
};

struct Impl_GmRequest {
    iObject object;
    iMutex mutex;
    enum iGmRequestState state;
    iString url;
    iTlsRequest *req;
    enum iGmStatusCode code;
    iString header;
    iBlock body; /* rest of the received data */
    uint32_t timeoutId; /* in case server doesn't close the connection */
    iAudience *updated;
    iAudience *finished;
};

iDefineObjectConstruction(GmRequest)
iDefineAudienceGetter(GmRequest, updated)
iDefineAudienceGetter(GmRequest, finished)

void init_GmRequest(iGmRequest *d) {
    init_Mutex(&d->mutex);
    d->state = initialized_GmRequestState;
    init_String(&d->url);
    d->req = NULL;
    d->code = none_GmStatusCode;
    init_String(&d->header);
    init_Block(&d->body, 0);
    d->timeoutId = 0;
    d->updated = NULL;
    d->finished = NULL;
}

void deinit_GmRequest(iGmRequest *d) {
    lock_Mutex(&d->mutex);
    if (d->timeoutId) {
        SDL_RemoveTimer(d->timeoutId);
    }
    if (d->req) {
        if (!isFinished_GmRequest(d)) {
            iDisconnectObject(TlsRequest, d->req, readyRead, d);
            iDisconnectObject(TlsRequest, d->req, finished, d);
            cancel_TlsRequest(d->req);
            d->state = finished_GmRequestState;
        }
        iRelease(d->req);
        d->req = NULL;
    }
    unlock_Mutex(&d->mutex);
    delete_Audience(d->finished);
    delete_Audience(d->updated);
    deinit_Block(&d->body);
    deinit_String(&d->header);
    deinit_String(&d->url);
    deinit_Mutex(&d->mutex);
}

void setUrl_GmRequest(iGmRequest *d, const iString *url) {
    set_String(&d->url, url);
    urlEncodeSpaces_String(&d->url);
}

static uint32_t timedOutWhileReceivingBody_GmRequest_(uint32_t interval, void *obj) {
    iGmRequest *d = obj;
    iGuardMutex(&d->mutex, cancel_TlsRequest(d->req));
    iUnused(interval);
    return 0;
}

static void restartTimeout_GmRequest_(iGmRequest *d) {
    /* Note: `d` is currently locked. */
    if (d->timeoutId) {
        SDL_RemoveTimer(d->timeoutId);
    }
    d->timeoutId = SDL_AddTimer(BODY_TIMEOUT, timedOutWhileReceivingBody_GmRequest_, d);
}

static void readIncoming_GmRequest_(iAnyObject *obj) {
    iGmRequest *d = (iGmRequest *) obj;
    iBool notifyUpdate = iFalse;
    iBool notifyDone = iFalse;
    lock_Mutex(&d->mutex);
    iAssert(d->state != finished_GmRequestState); /* notifications out of order? */
    iBlock *data = readAll_TlsRequest(d->req);
    fflush(stdout);
    if (d->state == receivingHeader_GmRequestState) {
        appendCStrN_String(&d->header, constData_Block(data), size_Block(data));
        /* Check if the header line is complete. */
        size_t endPos = indexOfCStr_String(&d->header, "\r\n");
        if (endPos != iInvalidPos) {
            /* Move remainder to the body. */
            setData_Block(&d->body,
                          constBegin_String(&d->header) + endPos + 2,
                          size_String(&d->header) - endPos - 2);
            remove_Block(&d->header.chars, endPos, iInvalidSize);
            /* parse and remove the code */
            if (size_String(&d->header) < 3) {
                clear_String(&d->header);
                d->code  = invalidHeader_GmStatusCode;
                d->state = finished_GmRequestState;
                notifyDone = iTrue;
            }
            const int code = toInt_String(&d->header);
            if (code == 0 || cstr_String(&d->header)[2] != ' ') {
                clear_String(&d->header);
                d->code  = invalidHeader_GmStatusCode;
                d->state = finished_GmRequestState;
                notifyDone = iTrue;
            }
            remove_Block(&d->header.chars, 0, 3); /* just the meta */
            d->code = code;
            d->state = receivingBody_GmRequestState;
            notifyUpdate = iTrue;
            /* Start a timeout for the remainder of the response, in case the connection
               remains open. */
            restartTimeout_GmRequest_(d);
        }
    }
    else if (d->state == receivingBody_GmRequestState) {
        append_Block(&d->body, data);
        restartTimeout_GmRequest_(d);
        notifyUpdate = iTrue;
    }
    delete_Block(data);
    unlock_Mutex(&d->mutex);
    if (notifyUpdate) {
        iNotifyAudience(d, updated, GmRequestUpdated);
    }
    if (notifyDone) {
        iNotifyAudience(d, finished, GmRequestFinished);
    }
}

static void requestFinished_GmRequest_(iAnyObject *obj) {
    iGmRequest *d = (iGmRequest *) obj;
    lock_Mutex(&d->mutex);
    /* There shouldn't be anything left to read. */ {
        iBlock *data = readAll_TlsRequest(d->req);
        iAssert(isEmpty_Block(data));
        delete_Block(data);
    }
    SDL_RemoveTimer(d->timeoutId);
    d->timeoutId = 0;
    iReleaseLater(d->req);
    d->req = NULL;
    d->state = finished_GmRequestState;
    unlock_Mutex(&d->mutex);
    iNotifyAudience(d, finished, GmRequestFinished);
}

void submit_GmRequest(iGmRequest *d) {
    iAssert(d->state == initialized_GmRequestState);
    if (d->state != initialized_GmRequestState) {
        return;
    }
    d->code = none_GmStatusCode;
    clear_String(&d->header);
    clear_Block(&d->body);
    iUrl url;
    init_Url(&url, &d->url);
    if (!cmpCStrSc_Rangecc(&url.protocol, "file", &iCaseInsensitive)) {
        iString *path = collect_String(urlDecode_String(collect_String(newRange_String(url.path))));
        iFile *  f    = new_File(path);
        if (open_File(f, readOnly_FileMode)) {
            /* TODO: Check supported file types: images, audio */
            d->code = success_GmStatusCode;
            setCStr_String(&d->header, "text/gemini; charset=utf-8");
            set_Block(&d->body, collect_Block(readAll_File(f)));
            iNotifyAudience(d, updated, GmRequestUpdated);
        }
        else {
            d->code = failedToOpenFile_GmStatusCode;
        }
        iRelease(f);
        d->state = finished_GmRequestState;
        iNotifyAudience(d, finished, GmRequestFinished);
        return;
    }
    d->state = receivingHeader_GmRequestState;
    d->req = new_TlsRequest();
    iConnect(TlsRequest, d->req, readyRead, d, readIncoming_GmRequest_);
    iConnect(TlsRequest, d->req, finished, d, requestFinished_GmRequest_);
    uint16_t port = toInt_String(collect_String(newRange_String(url.port)));
    if (port == 0) {
        port = 1965; /* default Gemini port */
    }
    setUrl_TlsRequest(d->req, collect_String(newRange_String(url.host)), port);
    setContent_TlsRequest(d->req,
                          utf8_String(collectNewFormat_String("%s\r\n", cstr_String(&d->url))));
    submit_TlsRequest(d->req);
}

iBool isFinished_GmRequest(const iGmRequest *d) {
    iBool done;
    iGuardMutex(&d->mutex, done = (d->state == finished_GmRequestState));
    return done;
}

const char *error_GmRequest(const iGmRequest *d) {
    if (d->code == failedToOpenFile_GmStatusCode) {
        return "Failed to open file";
    }
    if (d->code == invalidHeader_GmStatusCode) {
        return "Received invalid header (not Gemini?)";
    }
    return NULL; /* TDOO: detailed error string */
}

enum iGmStatusCode status_GmRequest(const iGmRequest *d) {
    return d->code;
}

const iString *meta_GmRequest(const iGmRequest *d) {
    if (d->state >= receivingBody_GmRequestState) {
        return &d->header;
    }
    return collectNew_String();
}

const iBlock *body_GmRequest(const iGmRequest *d) {
    iBlock *body;
    iGuardMutex(&d->mutex, body = collect_Block(copy_Block(&d->body)));
    return body;
}

const iString *url_GmRequest(const iGmRequest *d) {
    return &d->url;
}

iDefineClass(GmRequest)
