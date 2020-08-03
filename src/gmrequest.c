#include "gmrequest.h"
#include "gmutil.h"
#include "gmcerts.h"
#include "app.h"

#include <the_Foundation/file.h>
#include <the_Foundation/tlsrequest.h>
#include <the_Foundation/mutex.h>

#include <SDL_timer.h>

static const int BODY_TIMEOUT = 3000; /* ms */

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
    int certFlags;
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
    d->certFlags = 0;
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
            if (code == success_GmStatusCode && isEmpty_String(&d->header)) {
                setCStr_String(&d->header, "text/gemini; charset=utf-8"); /* default */
            }
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
    d->state     = finished_GmRequestState;
    d->certFlags = 0;
    /* Check the server certificate. */ {
        const iTlsCertificate *cert = serverCertificate_TlsRequest(d->req);
        if (cert) {
            iGmCerts *     certDb = certs_App();
            const iRangecc domain = urlHost_String(&d->url);
            d->certFlags |= available_GmRequestCertFlag;
            if (!isExpired_TlsCertificate(cert)) {
                d->certFlags |= timeVerified_GmRequestCertFlag;
            }
            if (verifyDomain_TlsCertificate(cert, domain)) {
                d->certFlags |= domainVerified_GmRequestCertFlag;
            }
            if (checkTrust_GmCerts(certDb, domain, cert)) {
                d->certFlags |= trusted_GmRequestCertFlag;
            }
        }
#if 0
        printf("Server certificate:\n%s\n", cstrLocal_String(pem_TlsCertificate(cert)));
        iBlock *sha = fingerprint_TlsCertificate(cert);
        printf("Fingerprint: %s\n",
               cstr_String(collect_String(
                   hexEncode_Block(collect_Block(fingerprint_TlsCertificate(cert))))));
        delete_Block(sha);
        iDate expiry;
        validUntil_TlsCertificate(cert, &expiry);
        printf("Valid until %04d-%02d-%02d\n", expiry.year, expiry.month, expiry.day);
        printf("Has expired: %s\n", isExpired_TlsCertificate(cert) ? "yes" : "no");
        //printf("Subject: %s\n", cstrLocal_String(subject_TlsCertificate(serverCertificate_TlsRequest(d->req))));
        /* Verify. */ {
            iUrl parts;
            init_Url(&parts, &d->url);
            printf("Domain name is %s\n",
                   verifyDomain_TlsCertificate(cert, parts.host) ? "valid" : "not valid");
        }
        fflush(stdout);
#endif
    }
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
    if (equalCase_Rangecc(&url.protocol, "file")) {
        iString *path = collect_String(urlDecode_String(collect_String(newRange_String(url.path))));
        iFile *  f    = new_File(path);
        if (open_File(f, readOnly_FileMode)) {
            /* TODO: Check supported file types: images, audio */
            /* TODO: Detect text files based on contents? E.g., is the content valid UTF-8. */
            d->code = success_GmStatusCode;
            d->certFlags = 0;
            if (endsWithCase_String(path, ".gmi")) {
                setCStr_String(&d->header, "text/gemini; charset=utf-8");
            }
            else if (endsWithCase_String(path, ".txt")) {
                setCStr_String(&d->header, "text/plain");
            }
            else if (endsWithCase_String(path, ".png")) {
                setCStr_String(&d->header, "image/png");
            }
            else if (endsWithCase_String(path, ".jpg") || endsWithCase_String(path, ".jpeg")) {
                setCStr_String(&d->header, "image/jpeg");
            }
            else if (endsWithCase_String(path, ".gif")) {
                setCStr_String(&d->header, "image/gif");
            }
            else {
                setCStr_String(&d->header, "application/octet-stream");
            }
            set_Block(&d->body, collect_Block(readAll_File(f)));
            d->state = receivingBody_GmRequestState;
            iNotifyAudience(d, updated, GmRequestUpdated);
        }
        else {
            d->code = failedToOpenFile_GmStatusCode;
            setCStr_String(&d->header, cstr_String(path));
        }
        iRelease(f);
        d->certFlags = 0;
        d->state = finished_GmRequestState;
        iNotifyAudience(d, finished, GmRequestFinished);
        return;
    }
    else if (equalCase_Rangecc(&url.protocol, "data")) {
        d->code = success_GmStatusCode;
        d->certFlags = 0;
        iString *src = collectNewCStr_String(url.protocol.start + 5);
        iRangecc header = { constBegin_String(src), constBegin_String(src) };
        while (header.end < constEnd_String(src) && *header.end != ',') {
            header.end++;
        }
        iBool isBase64 = iFalse;
        setRange_String(&d->header, header);
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
        set_Block(&d->body, &src->chars);
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

int certFlags_GmRequest(const iGmRequest *d) {
    return d->certFlags;
}

iDate certExpirationDate_GmRequest(const iGmRequest *d) {
    iDate expiry;
    validUntil_TlsCertificate(serverCertificate_TlsRequest(d->req), &expiry);
    return expiry;
}

iDefineClass(GmRequest)
