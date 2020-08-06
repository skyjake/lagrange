#include "gmrequest.h"
#include "gmutil.h"
#include "gmcerts.h"
#include "app.h"

#include <the_Foundation/file.h>
#include <the_Foundation/tlsrequest.h>
#include <the_Foundation/mutex.h>

#include <SDL_timer.h>

void init_GmResponse(iGmResponse *d) {
    d->statusCode = none_GmStatusCode;
    init_String(&d->meta);
    init_Block(&d->body, 0);
    d->certFlags = 0;
    iZap(d->certValidUntil);
}

void initCopy_GmResponse(iGmResponse *d, const iGmResponse *other) {
    d->statusCode = other->statusCode;
    initCopy_String(&d->meta, &other->meta);
    initCopy_Block(&d->body, &other->body);
    d->certFlags = other->certFlags;
    d->certValidUntil = other->certValidUntil;
}

void deinit_GmResponse(iGmResponse *d) {
    deinit_Block(&d->body);
    deinit_String(&d->meta);
}

void clear_GmResponse(iGmResponse *d) {
    d->statusCode = none_GmStatusCode;
    clear_String(&d->meta);
    clear_Block(&d->body);
    d->certFlags = 0;
    iZap(d->certValidUntil);
}

iGmResponse *copy_GmResponse(const iGmResponse *d) {
    iGmResponse *copied = iMalloc(GmResponse);
    initCopy_GmResponse(copied, d);
    return copied;
}

/*----------------------------------------------------------------------------------------------*/

static const int bodyTimeout_GmRequest_ = 3000; /* ms */

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
    iGmResponse resp;
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
    init_GmResponse(&d->resp);
    init_String(&d->url);
    d->req = NULL;
    d->timeoutId = 0;
    d->updated = NULL;
    d->finished = NULL;
}

void deinit_GmRequest(iGmRequest *d) {
    if (d->req) {
        iDisconnectObject(TlsRequest, d->req, readyRead, d);
        iDisconnectObject(TlsRequest, d->req, finished, d);
    }
    lock_Mutex(&d->mutex);
    if (d->timeoutId) {
        SDL_RemoveTimer(d->timeoutId);
    }
    if (!isFinished_GmRequest(d)) {
        unlock_Mutex(&d->mutex);
        cancel_TlsRequest(d->req);
        d->state = finished_GmRequestState;
    }
    else {
        unlock_Mutex(&d->mutex);
    }
    iRelease(d->req);
    d->req = NULL;
    delete_Audience(d->finished);
    delete_Audience(d->updated);
    deinit_GmResponse(&d->resp);
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
    d->timeoutId = SDL_AddTimer(bodyTimeout_GmRequest_, timedOutWhileReceivingBody_GmRequest_, d);
}

static void checkServerCertificate_GmRequest_(iGmRequest *d) {
    const iTlsCertificate *cert = serverCertificate_TlsRequest(d->req);
    d->resp.certFlags = 0;
    if (cert) {
        iGmCerts *     certDb = certs_App();
        const iRangecc domain = urlHost_String(&d->url);
        d->resp.certFlags |= available_GmCertFlag;
        if (!isExpired_TlsCertificate(cert)) {
            d->resp.certFlags |= timeVerified_GmCertFlag;
        }
        if (verifyDomain_TlsCertificate(cert, domain)) {
            d->resp.certFlags |= domainVerified_GmCertFlag;
        }
        if (checkTrust_GmCerts(certDb, domain, cert)) {
            d->resp.certFlags |= trusted_GmCertFlag;
        }
        validUntil_TlsCertificate(serverCertificate_TlsRequest(d->req), &d->resp.certValidUntil);
    }
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
        appendCStrN_String(&d->resp.meta, constData_Block(data), size_Block(data));
        /* Check if the header line is complete. */
        size_t endPos = indexOfCStr_String(&d->resp.meta, "\r\n");
        if (endPos != iInvalidPos) {
            /* Move remainder to the body. */
            setData_Block(&d->resp.body,
                          constBegin_String(&d->resp.meta) + endPos + 2,
                          size_String(&d->resp.meta) - endPos - 2);
            remove_Block(&d->resp.meta.chars, endPos, iInvalidSize);
            /* parse and remove the code */
            if (size_String(&d->resp.meta) < 3) {
                clear_String(&d->resp.meta);
                d->resp.statusCode = invalidHeader_GmStatusCode;
                d->state           = finished_GmRequestState;
                notifyDone         = iTrue;
            }
            const int code = toInt_String(&d->resp.meta);
            if (code == 0 || cstr_String(&d->resp.meta)[2] != ' ') {
                clear_String(&d->resp.meta);
                d->resp.statusCode = invalidHeader_GmStatusCode;
                d->state           = finished_GmRequestState;
                notifyDone         = iTrue;
            }
            remove_Block(&d->resp.meta.chars, 0, 3); /* just the meta */
            if (code == success_GmStatusCode && isEmpty_String(&d->resp.meta)) {
                setCStr_String(&d->resp.meta, "text/gemini; charset=utf-8"); /* default */
            }
            d->resp.statusCode = code;
            d->state           = receivingBody_GmRequestState;
            checkServerCertificate_GmRequest_(d);
            notifyUpdate = iTrue;
            /* Start a timeout for the remainder of the response, in case the connection
               remains open. */
            restartTimeout_GmRequest_(d);
        }
    }
    else if (d->state == receivingBody_GmRequestState) {
        append_Block(&d->resp.body, data);
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
    d->state     = finished_GmRequestState;
    checkServerCertificate_GmRequest_(d);
    unlock_Mutex(&d->mutex);
    iNotifyAudience(d, finished, GmRequestFinished);
}

void submit_GmRequest(iGmRequest *d) {
    iAssert(d->state == initialized_GmRequestState);
    if (d->state != initialized_GmRequestState) {
        return;
    }
    clear_GmResponse(&d->resp);
    iUrl url;
    init_Url(&url, &d->url);
    if (equalCase_Rangecc(&url.protocol, "file")) {
        iString *path = collect_String(urlDecode_String(collect_String(newRange_String(url.path))));
        iFile *  f    = new_File(path);
        if (open_File(f, readOnly_FileMode)) {
            /* TODO: Check supported file types: images, audio */
            /* TODO: Detect text files based on contents? E.g., is the content valid UTF-8. */
            d->resp.statusCode = success_GmStatusCode;
            if (endsWithCase_String(path, ".gmi")) {
                setCStr_String(&d->resp.meta, "text/gemini; charset=utf-8");
            }
            else if (endsWithCase_String(path, ".txt")) {
                setCStr_String(&d->resp.meta, "text/plain");
            }
            else if (endsWithCase_String(path, ".png")) {
                setCStr_String(&d->resp.meta, "image/png");
            }
            else if (endsWithCase_String(path, ".jpg") || endsWithCase_String(path, ".jpeg")) {
                setCStr_String(&d->resp.meta, "image/jpeg");
            }
            else if (endsWithCase_String(path, ".gif")) {
                setCStr_String(&d->resp.meta, "image/gif");
            }
            else {
                setCStr_String(&d->resp.meta, "application/octet-stream");
            }
            set_Block(&d->resp.body, collect_Block(readAll_File(f)));
            d->state = receivingBody_GmRequestState;
            iNotifyAudience(d, updated, GmRequestUpdated);
        }
        else {
            d->resp.statusCode = failedToOpenFile_GmStatusCode;
            setCStr_String(&d->resp.meta, cstr_String(path));
        }
        iRelease(f);
        d->state = finished_GmRequestState;
        iNotifyAudience(d, finished, GmRequestFinished);
        return;
    }
    else if (equalCase_Rangecc(&url.protocol, "data")) {
        d->resp.statusCode = success_GmStatusCode;
        iString *src = collectNewCStr_String(url.protocol.start + 5);
        iRangecc header = { constBegin_String(src), constBegin_String(src) };
        while (header.end < constEnd_String(src) && *header.end != ',') {
            header.end++;
        }
        iBool isBase64 = iFalse;
        setRange_String(&d->resp.meta, header);
        /* Check what's in the header. */ {
            iRangecc entry = iNullRange;
            while (nextSplit_Rangecc(&header, ";", &entry)) {
                if (equal_Rangecc(&entry, "base64")) {
                    isBase64 = iTrue;
                }
            }
        }
        remove_Block(&src->chars, 0, size_Range(&header) + 1);
        if (isBase64) {
            set_Block(&src->chars, collect_Block(base64Decode_Block(&src->chars)));
        }
        else {
            set_String(src, collect_String(urlDecode_String(src)));
        }
        set_Block(&d->resp.body, &src->chars);
        d->state = receivingBody_GmRequestState;
        iNotifyAudience(d, updated, GmRequestUpdated);
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

enum iGmStatusCode status_GmRequest(const iGmRequest *d) {
    return d->resp.statusCode;
}

const iString *meta_GmRequest(const iGmRequest *d) {
    if (d->state >= receivingBody_GmRequestState) {
        return &d->resp.meta;
    }
    return collectNew_String();
}

const iBlock *body_GmRequest(const iGmRequest *d) {
    iBlock *body;
    iGuardMutex(&d->mutex, body = collect_Block(copy_Block(&d->resp.body)));
    return body;
}

const iString *url_GmRequest(const iGmRequest *d) {
    return &d->url;
}

const iGmResponse *response_GmRequest(const iGmRequest *d) {
    iAssert(d->state == finished_GmRequestState);
    return &d->resp;
}

int certFlags_GmRequest(const iGmRequest *d) {
    return d->resp.certFlags;
}

iDate certExpirationDate_GmRequest(const iGmRequest *d) {
    return d->resp.certValidUntil;
}

iDefineClass(GmRequest)
