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

#include "gmrequest.h"
#include "gmutil.h"
#include "gmcerts.h"
#include "app.h" /* dataDir_App() */
#include "embedded.h"
#include "ui/text.h"
#include "defs.h"

#include <the_Foundation/file.h>
#include <the_Foundation/mutex.h>
#include <the_Foundation/path.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/tlsrequest.h>

#include <SDL_timer.h>

iDefineTypeConstruction(GmResponse)

void init_GmResponse(iGmResponse *d) {
    d->statusCode = none_GmStatusCode;
    init_String(&d->meta);
    init_Block(&d->body, 0);
    d->certFlags = 0;
    iZap(d->certValidUntil);
    init_String(&d->certSubject);
    iZap(d->when);
}

void initCopy_GmResponse(iGmResponse *d, const iGmResponse *other) {
    d->statusCode = other->statusCode;
    initCopy_String(&d->meta, &other->meta);
    initCopy_Block(&d->body, &other->body);
    d->certFlags = other->certFlags;
    d->certValidUntil = other->certValidUntil;
    initCopy_String(&d->certSubject, &other->certSubject);
    d->when = other->when;
}

void deinit_GmResponse(iGmResponse *d) {
    deinit_String(&d->certSubject);
    deinit_Block(&d->body);
    deinit_String(&d->meta);
}

void clear_GmResponse(iGmResponse *d) {
    d->statusCode = none_GmStatusCode;
    clear_String(&d->meta);
    clear_Block(&d->body);
    d->certFlags = 0;
    iZap(d->certValidUntil);
    clear_String(&d->certSubject);
    iZap(d->when);
}

iGmResponse *copy_GmResponse(const iGmResponse *d) {
    iGmResponse *copied = iMalloc(GmResponse);
    initCopy_GmResponse(copied, d);
    return copied;
}

void serialize_GmResponse(const iGmResponse *d, iStream *outs) {
    write32_Stream(outs, d->statusCode);
    serialize_String(&d->meta, outs);
    serialize_Block(&d->body, outs);
    write32_Stream(outs, d->certFlags);
    serialize_Date(&d->certValidUntil, outs);
    serialize_String(&d->certSubject, outs);
    writeU64_Stream(outs, d->when.ts.tv_sec);
}

void deserialize_GmResponse(iGmResponse *d, iStream *ins) {
    d->statusCode = read32_Stream(ins);
    deserialize_String(&d->meta, ins);
    deserialize_Block(&d->body, ins);
    d->certFlags = read32_Stream(ins);
    deserialize_Date(&d->certValidUntil, ins);
    deserialize_String(&d->certSubject, ins);
    iZap(d->when);
    if (version_Stream(ins) >= addedResponseTimestamps_FileVersion) {
        d->when.ts.tv_sec = readU64_Stream(ins);
    }
}

/*----------------------------------------------------------------------------------------------*/

//static const int bodyTimeout_GmRequest_ = 3000; /* ms */

enum iGmRequestState {
    initialized_GmRequestState,
    receivingHeader_GmRequestState,
    receivingBody_GmRequestState,
    finished_GmRequestState,
    failure_GmRequestState,
};

struct Impl_GmRequest {
    iObject              object;
    iMutex               mutex;
    iGmCerts *           certs; /* not owned */
    enum iGmRequestState state;
    iString              url;
    iTlsRequest *        req;
    iGmResponse          resp;
    //uint32_t             timeoutId; /* in case server doesn't close the connection */
    iAudience *          updated;
//    iAudience *          timeout;
    iAudience *          finished;
};

iDefineObjectConstructionArgs(GmRequest, (iGmCerts *certs), certs)
iDefineAudienceGetter(GmRequest, updated)
//iDefineAudienceGetter(GmRequest, timeout)
iDefineAudienceGetter(GmRequest, finished)

void init_GmRequest(iGmRequest *d, iGmCerts *certs) {
    init_Mutex(&d->mutex);
    init_GmResponse(&d->resp);
    init_String(&d->url);
    d->certs           = certs;
//    d->timeoutId       = 0;
    d->req             = NULL;
    d->state           = initialized_GmRequestState;
    d->updated         = NULL;
//    d->timeout         = NULL;
    d->finished        = NULL;
}

void deinit_GmRequest(iGmRequest *d) {
    if (d->req) {
        iDisconnectObject(TlsRequest, d->req, readyRead, d);
        iDisconnectObject(TlsRequest, d->req, finished, d);
    }
    lock_Mutex(&d->mutex);
//    if (d->timeoutId) {
//        SDL_RemoveTimer(d->timeoutId);
//    }
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
//    delete_Audience(d->timeout);
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

#if 0
static uint32_t timedOutWhileReceivingBody_GmRequest_(uint32_t interval, void *obj) {
    /* Note: Called from SDL's timer thread. */
    iGmRequest *d = obj;
    //postCommandf_App("gmrequest.timeout request:%p", obj);
    iNotifyAudience(d, timeout, GmRequestTimeout);
    iUnused(interval);
    return 0;
}
#endif

void cancel_GmRequest(iGmRequest *d) {
    cancel_TlsRequest(d->req);
}

#if 0
static void restartTimeout_GmRequest_(iGmRequest *d) {
    /* Note: `d` is currently locked. */
    if (d->timeoutId) {
        SDL_RemoveTimer(d->timeoutId);
    }
    d->timeoutId = SDL_AddTimer(bodyTimeout_GmRequest_, timedOutWhileReceivingBody_GmRequest_, d);
}
#endif

static void checkServerCertificate_GmRequest_(iGmRequest *d) {
    const iTlsCertificate *cert = serverCertificate_TlsRequest(d->req);
    d->resp.certFlags = 0;
    if (cert) {
        const iRangecc domain = range_String(hostName_Address(address_TlsRequest(d->req)));
        d->resp.certFlags |= available_GmCertFlag;
        if (!isExpired_TlsCertificate(cert)) {
            d->resp.certFlags |= timeVerified_GmCertFlag;
        }
        /* TODO: Check for IP too (see below), because it may be specified in the SAN. */
#if 0
        iString *ip = toStringFlags_Address(address_TlsRequest(d->req), noPort_SocketStringFlag, 0);
        if (verifyIp_TlsCertificate(cert, ip)) {
            printf("[GmRequest] IP address %s matches!\n", cstr_String(ip));
        }
        else {
            printf("[GmRequest] IP address %s not matched\n", cstr_String(ip));
        }
        delete_String(ip);
#endif
        if (verifyDomain_TlsCertificate(cert, domain)) {
            d->resp.certFlags |= domainVerified_GmCertFlag;
        }
        if (checkTrust_GmCerts(d->certs, domain, cert)) {
            d->resp.certFlags |= trusted_GmCertFlag;
        }
        validUntil_TlsCertificate(cert, &d->resp.certValidUntil);
        set_String(&d->resp.certSubject, collect_String(subject_TlsCertificate(cert)));
    }
}

static void readIncoming_GmRequest_(iAnyObject *obj) {
    iGmRequest *d = (iGmRequest *) obj;
    iBool notifyUpdate = iFalse;
    iBool notifyDone = iFalse;
    lock_Mutex(&d->mutex);
    iAssert(d->state != finished_GmRequestState); /* notifications out of order? */
    iBlock *data = readAll_TlsRequest(d->req);
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
            if (code == 0) {
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
//            restartTimeout_GmRequest_(d);
        }
    }
    else if (d->state == receivingBody_GmRequestState) {
        append_Block(&d->resp.body, data);
//        restartTimeout_GmRequest_(d);
        notifyUpdate = iTrue;
    }
    initCurrent_Time(&d->resp.when);
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
        initCurrent_Time(&d->resp.when);
    }
//    SDL_RemoveTimer(d->timeoutId);
//    d->timeoutId = 0;
    d->state = (status_TlsRequest(d->req) == error_TlsRequestStatus ? failure_GmRequestState
                                                                    : finished_GmRequestState);
    if (d->state == failure_GmRequestState) {
        d->resp.statusCode = tlsFailure_GmStatusCode;
        set_String(&d->resp.meta, errorMessage_TlsRequest(d->req));
    }
    checkServerCertificate_GmRequest_(d);
    unlock_Mutex(&d->mutex);
    iNotifyAudience(d, finished, GmRequestFinished);
}

static const iBlock *aboutPageSource_(iRangecc path) {
    const iBlock *src = NULL;
    if (equalCase_Rangecc(path, "lagrange")) {
        return &blobLagrange_Embedded;
    }
    if (equalCase_Rangecc(path, "help")) {
        return &blobHelp_Embedded;
    }
    if (equalCase_Rangecc(path, "license")) {
        return &blobLicense_Embedded;
    }
    if (equalCase_Rangecc(path, "version")) {
        return &blobVersion_Embedded;
    }
    if (equalCase_Rangecc(path, "debug")) {
        return utf8_String(debugInfo_App());
    }
    return src;
}

static const iBlock *replaceVariables_(const iBlock *block) {
    iRegExp *var = new_RegExp("\\$\\{([^}]+)\\}", 0);
    iRegExpMatch m;
    init_RegExpMatch(&m);
    if (matchRange_RegExp(var, range_Block(block), &m)) {
        iBlock *replaced = collect_Block(copy_Block(block));
        do {
            const iRangei span = m.range;
            const iRangecc name = capturedRange_RegExpMatch(&m, 1);
            iRangecc repl = iNullRange;
            if (equal_Rangecc(name, "APP_VERSION")) {
                repl = range_CStr(LAGRANGE_APP_VERSION);
            }
            else if (startsWith_Rangecc(name, "BT:")) { /* block text */
                repl = range_String(collect_String(renderBlockChars_Text(
                    &fontFiraSansRegular_Embedded,
                    11, /* should be larger if shaded */
                    quadrants_TextBlockMode,
                    &(iString){ iBlockLiteral(
                        name.start + 3, size_Range(&name) - 3, size_Range(&name) - 3) })));
            }
            else if (startsWith_Rangecc(name, "ST:")) { /* shaded text */
                repl = range_String(collect_String(renderBlockChars_Text(
                    &fontSymbola_Embedded,
                    20,
                    shading_TextBlockMode,
                    &(iString){ iBlockLiteral(
                        name.start + 3, size_Range(&name) - 3, size_Range(&name) - 3) })));
            }
            else if (equal_Rangecc(name, "ALT")) {
#if defined (iPlatformApple)
                repl = range_CStr("\u2325");
#else
                repl = range_CStr("Alt");
#endif
            }
            else if (equal_Rangecc(name, "ALT+")) {
#if defined (iPlatformApple)
                repl = range_CStr("\u2325");
#else
                repl = range_CStr("Alt+");
#endif
            }
            else if (equal_Rangecc(name, "CTRL")) {
#if defined (iPlatformApple)
                repl = range_CStr("\u2318");
#else
                repl = range_CStr("Ctrl");
#endif
            }
            else if (equal_Rangecc(name, "CTRL+")) {
#if defined (iPlatformApple)
                repl = range_CStr("\u2318");
#else
                repl = range_CStr("Ctrl+");
#endif
            }
            else if (equal_Rangecc(name, "SHIFT")) {
#if defined (iPlatformApple)
                repl = range_CStr("\u21e7");
#else
                repl = range_CStr("Shift");
#endif
            }
            else if (equal_Rangecc(name, "SHIFT+")) {
#if defined (iPlatformApple)
                repl = range_CStr("\u21e7");
#else
                repl = range_CStr("Shift+");
#endif
            }
            remove_Block(replaced, span.start, size_Range(&span));
            insertData_Block(replaced, span.start, repl.start, size_Range(&repl));
            iZap(m);
        } while (matchRange_RegExp(var, range_Block(replaced), &m));
        block = replaced;
    }
    iRelease(var);
    return block;
}

void submit_GmRequest(iGmRequest *d) {
    iAssert(d->state == initialized_GmRequestState);
    if (d->state != initialized_GmRequestState) {
        return;
    }
    clear_GmResponse(&d->resp);
    iUrl url;
    init_Url(&url, &d->url);
    /* Check for special schemes. */
    /* TODO: If this were a library, these could be handled via callbacks. */
    /* TODO: Handle app's configured proxies and these via the same mechanism. */
    const iString *host = collect_String(newRange_String(url.host));
    uint16_t       port = toInt_String(collect_String(newRange_String(url.port)));
    if (equalCase_Rangecc(url.scheme, "about")) {
        const iBlock *src = aboutPageSource_(url.path);
        if (src) {
            d->resp.statusCode = success_GmStatusCode;
            setCStr_String(&d->resp.meta, "text/gemini; charset=utf-8");
            set_Block(&d->resp.body, replaceVariables_(src));
            d->state = receivingBody_GmRequestState;
            iNotifyAudience(d, updated, GmRequestUpdated);
        }
        else {
            d->resp.statusCode = invalidLocalResource_GmStatusCode;
        }
        d->state = finished_GmRequestState;
        iNotifyAudience(d, finished, GmRequestFinished);
        return;
    }
    else if (equalCase_Rangecc(url.scheme, "file")) {
        iString *path = collect_String(urlDecode_String(collect_String(newRange_String(url.path))));
        iFile *  f    = new_File(path);
        if (open_File(f, readOnly_FileMode)) {
            /* TODO: Check supported file types: images, audio */
            /* TODO: Detect text files based on contents? E.g., is the content valid UTF-8. */
            d->resp.statusCode = success_GmStatusCode;
            if (endsWithCase_String(path, ".gmi") || endsWithCase_String(path, ".gemini")) {
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
            else if (endsWithCase_String(path, ".wav")) {
                setCStr_String(&d->resp.meta, "audio/wave");
            }
            else if (endsWithCase_String(path, ".ogg")) {
                setCStr_String(&d->resp.meta, "audio/ogg");
            }
            else if (endsWithCase_String(path, ".mp3")) {
                setCStr_String(&d->resp.meta, "audio/mpeg");
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
    else if (equalCase_Rangecc(url.scheme, "data")) {
        d->resp.statusCode = success_GmStatusCode;
        iString *src = collectNewCStr_String(url.scheme.start + 5);
        iRangecc header = { constBegin_String(src), constBegin_String(src) };
        while (header.end < constEnd_String(src) && *header.end != ',') {
            header.end++;
        }
        iBool isBase64 = iFalse;
        setRange_String(&d->resp.meta, header);
        /* Check what's in the header. */ {
            iRangecc entry = iNullRange;
            while (nextSplit_Rangecc(header, ";", &entry)) {
                if (equal_Rangecc(entry, "base64")) {
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
    else if (schemeProxy_App(url.scheme)) {
        /* User has configured a proxy server for this scheme. */
        const iString *proxy = schemeProxy_App(url.scheme);
        if (contains_String(proxy, ':')) {
            const size_t cpos = indexOf_String(proxy, ':');
            port = atoi(cstr_String(proxy) + cpos + 1);
            host = collect_String(newCStrN_String(cstr_String(proxy), cpos));
        }
        else {
            host = proxy;
            port = 0;
        }
    }
    else if (!equalCase_Rangecc(url.scheme, "gemini")) {
        d->resp.statusCode = unsupportedProtocol_GmStatusCode;
        d->state = finished_GmRequestState;
        iNotifyAudience(d, finished, GmRequestFinished);
        return;
    }
    d->state = receivingHeader_GmRequestState;
    d->req = new_TlsRequest();
    const iGmIdentity *identity = identityForUrl_GmCerts(d->certs, &d->url);
    if (identity) {
        setCertificate_TlsRequest(d->req, identity->cert);
    }
    iConnect(TlsRequest, d->req, readyRead, d, readIncoming_GmRequest_);
    iConnect(TlsRequest, d->req, finished, d, requestFinished_GmRequest_);
    if (port == 0) {
        port = 1965; /* default Gemini port */
    }
    setUrl_TlsRequest(d->req, host, port);
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
    iAssert(d->state != initialized_GmRequestState);
    return &d->resp;
}

int certFlags_GmRequest(const iGmRequest *d) {
    return d->resp.certFlags;
}

iDate certExpirationDate_GmRequest(const iGmRequest *d) {
    return d->resp.certValidUntil;
}

iDefineClass(GmRequest)
