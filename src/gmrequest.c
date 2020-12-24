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
#include "gopher.h"
#include "app.h" /* dataDir_App() */
#include "mimehooks.h"
#include "feeds.h"
#include "bookmarks.h"
#include "ui/text.h"
#include "embedded.h"
#include "defs.h"

#include <the_Foundation/file.h>
#include <the_Foundation/mutex.h>
#include <the_Foundation/path.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/socket.h>
#include <the_Foundation/tlsrequest.h>

#include <SDL_timer.h>

iDefineTypeConstruction(GmResponse)

void init_GmResponse(iGmResponse *d) {
    d->statusCode = none_GmStatusCode;
    init_String(&d->meta);
    init_Block(&d->body, 0);
    d->certFlags = 0;
    init_Block(&d->certFingerprint, 0);
    iZap(d->certValidUntil);
    init_String(&d->certSubject);
    iZap(d->when);
}

void initCopy_GmResponse(iGmResponse *d, const iGmResponse *other) {
    d->statusCode = other->statusCode;
    initCopy_String(&d->meta, &other->meta);
    initCopy_Block(&d->body, &other->body);
    d->certFlags = other->certFlags;
    initCopy_Block(&d->certFingerprint, &other->certFingerprint);
    d->certValidUntil = other->certValidUntil;
    initCopy_String(&d->certSubject, &other->certSubject);
    d->when = other->when;
}

void deinit_GmResponse(iGmResponse *d) {
    deinit_String(&d->certSubject);
    deinit_Block(&d->body);
    deinit_Block(&d->certFingerprint);
    deinit_String(&d->meta);
}

void clear_GmResponse(iGmResponse *d) {
    d->statusCode = none_GmStatusCode;
    clear_String(&d->meta);
    clear_Block(&d->body);
    d->certFlags = 0;
    clear_Block(&d->certFingerprint);
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
    /* TODO: Add certificate fingerprint, but need to bump file version first. */
    write32_Stream(outs, d->certFlags & ~haveFingerprint_GmCertFlag);
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
    clear_Block(&d->certFingerprint);
    if (version_Stream(ins) >= addedResponseTimestamps_FileVersion) {
        d->when.ts.tv_sec = readU64_Stream(ins);
    }
}

/*----------------------------------------------------------------------------------------------*/

enum iGmRequestState {
    initialized_GmRequestState,
    receivingHeader_GmRequestState,
    receivingBody_GmRequestState,
    finished_GmRequestState,
    failure_GmRequestState,
};

struct Impl_GmRequest {
    iObject              object;
    iMutex *             mtx;
    iGmCerts *           certs; /* not owned */
    enum iGmRequestState state;
    iString              url;
    iTlsRequest *        req;
    iGopher              gopher;
    iGmResponse *        resp;
    iBool                isRespLocked;
    iBool                isRespFiltered;
    iAtomicInt           allowUpdate;
    iAudience *          updated;
    iAudience *          finished;
};

iDefineObjectConstructionArgs(GmRequest, (iGmCerts *certs), certs)
iDefineAudienceGetter(GmRequest, updated)
iDefineAudienceGetter(GmRequest, finished)

static void checkServerCertificate_GmRequest_(iGmRequest *d) {
    const iTlsCertificate *cert = serverCertificate_TlsRequest(d->req);
    iGmResponse *resp = d->resp;
    resp->certFlags = 0;
    if (cert) {
        const iRangecc domain = range_String(hostName_Address(address_TlsRequest(d->req)));
        resp->certFlags |= available_GmCertFlag;
        set_Block(&resp->certFingerprint, collect_Block(fingerprint_TlsCertificate(cert)));
        resp->certFlags |= haveFingerprint_GmCertFlag;
        if (!isExpired_TlsCertificate(cert)) {
            resp->certFlags |= timeVerified_GmCertFlag;
        }
        if (verifyDomain_TlsCertificate(cert, domain)) {
            resp->certFlags |= domainVerified_GmCertFlag;
        }
        if (checkTrust_GmCerts(d->certs, domain, cert)) {
            resp->certFlags |= trusted_GmCertFlag;
        }
        validUntil_TlsCertificate(cert, &resp->certValidUntil);
        set_String(&resp->certSubject, collect_String(subject_TlsCertificate(cert)));
    }
}

static int processIncomingData_GmRequest_(iGmRequest *d, const iBlock *data) {
    iBool        notifyUpdate = iFalse;
    iBool        notifyDone   = iFalse;
    iGmResponse *resp         = d->resp;
    if (d->state == receivingHeader_GmRequestState) {
        appendCStrN_String(&resp->meta, constData_Block(data), size_Block(data));
        /* Check if the header line is complete. */
        size_t endPos = indexOfCStr_String(&resp->meta, "\r\n");
        if (endPos != iInvalidPos) {
            /* Move remainder to the body. */
            setData_Block(&resp->body,
                          constBegin_String(&resp->meta) + endPos + 2,
                          size_String(&resp->meta) - endPos - 2);
            remove_Block(&resp->meta.chars, endPos, iInvalidSize);
            /* Parse and remove the code. */
            iRegExp *metaPattern = new_RegExp("^([0-9][0-9])(( )(.*))?", 0);
            /* TODO: Empty <META> means no <SPACE>? Not according to the spec? */
            iRegExpMatch m;
            init_RegExpMatch(&m);
            int code = 0;
            if (matchString_RegExp(metaPattern, &resp->meta, &m)) {
                code = atoi(capturedRange_RegExpMatch(&m, 1).start);
                remove_Block(&resp->meta.chars,
                             0,
                             capturedRange_RegExpMatch(&m, 1).end -
                                 constBegin_String(&resp->meta)); /* leave just the <META> */
                trimStart_String(&resp->meta);
            }
            if (code == 0) {
                clear_String(&resp->meta);
                resp->statusCode = invalidHeader_GmStatusCode;
                d->state         = finished_GmRequestState;
                notifyDone       = iTrue;
            }
            else {
                if (code == success_GmStatusCode && isEmpty_String(&resp->meta)) {
                    setCStr_String(&resp->meta, "text/gemini; charset=utf-8"); /* default */
                }
                resp->statusCode = code;
                d->state         = receivingBody_GmRequestState;
                notifyUpdate     = iTrue;
                if (willTryFilter_MimeHooks(mimeHooks_App(), &resp->meta)) {
                    d->isRespFiltered = iTrue;
                }
            }
            checkServerCertificate_GmRequest_(d);
            iRelease(metaPattern);
        }
    }
    else if (d->state == receivingBody_GmRequestState) {
        append_Block(&resp->body, data);
        notifyUpdate = iTrue;
    }
    return (notifyUpdate ? 1 : 0) | (notifyDone ? 2 : 0);
}

static void readIncoming_GmRequest_(iGmRequest *d, iTlsRequest *req) {
    lock_Mutex(d->mtx);
    iGmResponse *resp = d->resp;
    iAssert(d->state != finished_GmRequestState); /* notifications out of order? */
    iBlock *  data         = readAll_TlsRequest(req);
    const int ubits        = processIncomingData_GmRequest_(d, data);
    iBool     notifyUpdate = (ubits & 1) != 0;
    iBool     notifyDone   = (ubits & 2) != 0;
    initCurrent_Time(&resp->when);
    delete_Block(data);
    unlock_Mutex(d->mtx);
    if (notifyUpdate && !d->isRespFiltered) {
        const iBool allowed = exchange_Atomic(&d->allowUpdate, iFalse);
        if (allowed) {
            iNotifyAudience(d, updated, GmRequestUpdated);
        }
    }
    if (notifyDone) {
        iNotifyAudience(d, finished, GmRequestFinished);
    }
}

static void requestFinished_GmRequest_(iGmRequest *d, iTlsRequest *req) {
    iAssert(req == d->req);
    lock_Mutex(d->mtx);
    /* There shouldn't be anything left to read. */ {
        iBlock *data = readAll_TlsRequest(req);
        iAssert(isEmpty_Block(data));
        delete_Block(data);
        initCurrent_Time(&d->resp->when);
    }
    d->state = (status_TlsRequest(req) == error_TlsRequestStatus ? failure_GmRequestState
                                                                 : finished_GmRequestState);
    if (d->state == failure_GmRequestState) {
        d->resp->statusCode = tlsFailure_GmStatusCode;
        set_String(&d->resp->meta, errorMessage_TlsRequest(req));
    }
    checkServerCertificate_GmRequest_(d);
    unlock_Mutex(d->mtx);
    /* Check for mimehooks. */
    if (d->isRespFiltered && d->state == finished_GmRequestState) {
        iBlock *xbody =
            tryFilter_MimeHooks(mimeHooks_App(), &d->resp->meta, &d->resp->body, &d->url);
        if (xbody) {
            lock_Mutex(d->mtx);
            clear_String(&d->resp->meta);
            clear_Block(&d->resp->body);
            d->state = receivingHeader_GmRequestState;
            processIncomingData_GmRequest_(d, xbody);
            d->state = finished_GmRequestState;
            unlock_Mutex(d->mtx);
        }
    }
    iNotifyAudience(d, finished, GmRequestFinished);
}

static const iBlock *aboutPageSource_(iRangecc path, iRangecc query) {
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
    if (equalCase_Rangecc(path, "feeds")) {
        return utf8_String(entryListPage_Feeds());
    }
    if (equalCase_Rangecc(path, "bookmarks")) {
        return utf8_String(bookmarkListPage_Bookmarks(
            bookmarks_App(),
            equal_Rangecc(query, "?tags")      ? listByTag_BookmarkListType
            : equal_Rangecc(query, "?created") ? listByCreationTime_BookmarkListType
                                               : listByFolder_BookmarkListType));
    }
    if (equalCase_Rangecc(path, "blank")) {
        return utf8_String(collectNewCStr_String("\n"));
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

static void gopherRead_GmRequest_(iGmRequest *d, iSocket *socket) {
    iBool notifyUpdate = iFalse;
    lock_Mutex(d->mtx);
    d->resp->statusCode = success_GmStatusCode;
    iBlock *data = readAll_Socket(socket);
    if (!isEmpty_Block(data)) {
        processResponse_Gopher(&d->gopher, data);
    }
    delete_Block(data);
    unlock_Mutex(d->mtx);
    if (notifyUpdate) {
        iNotifyAudience(d, updated, GmRequestUpdated);
    }
}

static void gopherDisconnected_GmRequest_(iGmRequest *d, iSocket *socket) {
    iUnused(socket);
    iBool notify = iFalse;
    lock_Mutex(d->mtx);
    if (d->state != failure_GmRequestState) {
        d->state = finished_GmRequestState;
        notify = iTrue;
    }
    unlock_Mutex(d->mtx);
    if (notify) {
        iNotifyAudience(d, finished, GmRequestFinished);
    }
}

static void gopherError_GmRequest_(iGmRequest *d, iSocket *socket, int error, const char *msg) {
    iUnused(socket);
    lock_Mutex(d->mtx);
    d->state = failure_GmRequestState;
    d->resp->statusCode = tlsFailure_GmStatusCode;
    format_String(&d->resp->meta, "%s (errno %d)", msg, error);
    clear_Block(&d->resp->body);
    unlock_Mutex(d->mtx);
    iNotifyAudience(d, finished, GmRequestFinished);
}

static void beginGopherConnection_GmRequest_(iGmRequest *d, const iString *host, uint16_t port) {
    if (port == 0) {
        port = 70; /* default port */
    }
    clear_Block(&d->gopher.source);
    iGmResponse *resp = d->resp;
    d->gopher.meta   = &resp->meta;
    d->gopher.output = &resp->body;
    d->state         = receivingBody_GmRequestState;
    d->gopher.socket = new_Socket(cstr_String(host), port);
    iConnect(Socket, d->gopher.socket, readyRead,    d, gopherRead_GmRequest_);
    iConnect(Socket, d->gopher.socket, disconnected, d, gopherDisconnected_GmRequest_);
    iConnect(Socket, d->gopher.socket, error,        d, gopherError_GmRequest_);
    open_Gopher(&d->gopher, &d->url);
    if (d->gopher.needQueryArgs) {
        resp->statusCode = input_GmStatusCode;
        setCStr_String(&resp->meta, "Enter query:");
        d->state = finished_GmRequestState;
        iNotifyAudience(d, finished, GmRequestFinished);
    }
}

/*----------------------------------------------------------------------------------------------*/

void init_GmRequest(iGmRequest *d, iGmCerts *certs) {
    d->mtx = new_Mutex();
    d->resp = new_GmResponse();
    d->isRespLocked = iFalse;
    d->isRespFiltered = iFalse;
    set_Atomic(&d->allowUpdate, iTrue);
    init_String(&d->url);
    init_Gopher(&d->gopher);
    d->certs      = certs;
    d->req        = NULL;
    d->updated    = NULL;
    d->finished   = NULL;
    d->state      = initialized_GmRequestState;
}

void deinit_GmRequest(iGmRequest *d) {
    if (d->req) {
        iDisconnectObject(TlsRequest, d->req, readyRead, d);
        iDisconnectObject(TlsRequest, d->req, finished, d);
    }
    lock_Mutex(d->mtx);
    if (!isFinished_GmRequest(d)) {
        unlock_Mutex(d->mtx);
        cancel_GmRequest(d);
        d->state = finished_GmRequestState;
    }
    else {
        unlock_Mutex(d->mtx);
    }
    iReleasePtr(&d->req);
    deinit_Gopher(&d->gopher);
    delete_Audience(d->finished);
    delete_Audience(d->updated);
    delete_GmResponse(d->resp);
    deinit_String(&d->url);
    delete_Mutex(d->mtx);
}

void setUrl_GmRequest(iGmRequest *d, const iString *url) {
    set_String(&d->url, url);
    /* Encode hostname to Punycode here because we want to submit the Punycode domain name
       in the request. (TODO: Pending possible Gemini spec change.) */
    punyEncodeUrlHost_String(&d->url);
    /* TODO: Gemini spec allows UTF-8 encoded URLs, but still need to percent-encode non-ASCII
       characters? Could be a server-side issue, e.g., if they're using a URL parser meant for
       the web. */
    urlEncodePath_String(&d->url);
    urlEncodeSpaces_String(&d->url);
}

void submit_GmRequest(iGmRequest *d) {
    iAssert(d->state == initialized_GmRequestState);
    if (d->state != initialized_GmRequestState) {
        return;
    }
    set_Atomic(&d->allowUpdate, iTrue);
    iGmResponse *resp = d->resp;
    clear_GmResponse(resp);
#if !defined (NDEBUG)
    printf("[GmRequest] URL: %s\n", cstr_String(&d->url));
#endif
    iUrl url;
    init_Url(&url, &d->url);
    /* Check for special schemes. */
    /* TODO: If this were a library, these could be handled via callbacks. */
    /* TODO: Handle app's configured proxies and these via the same mechanism. */
    const iString *host = collect_String(newRange_String(url.host));
    uint16_t       port = toInt_String(collect_String(newRange_String(url.port)));
    if (equalCase_Rangecc(url.scheme, "about")) {
        const iBlock *src = aboutPageSource_(url.path, url.query);
        if (src) {
            resp->statusCode = success_GmStatusCode;
            setCStr_String(&resp->meta, "text/gemini; charset=utf-8");
            set_Block(&resp->body, replaceVariables_(src));
            d->state = receivingBody_GmRequestState;
            iNotifyAudience(d, updated, GmRequestUpdated);
        }
        else {
            resp->statusCode = invalidLocalResource_GmStatusCode;
        }
        d->state = finished_GmRequestState;
        iNotifyAudience(d, finished, GmRequestFinished);
        return;
    }
    else if (equalCase_Rangecc(url.scheme, "file")) {
        iString *path = collect_String(urlDecode_String(collect_String(newRange_String(url.path))));
#if defined (iPlatformMsys)
        /* Remove the extra slash from the beginning. */
        if (startsWith_String(path, "/")) {
            remove_Block(&path->chars, 0, 1);
        }
#endif
        iFile *  f    = new_File(path);
        if (open_File(f, readOnly_FileMode)) {
            /* TODO: Check supported file types: images, audio */
            /* TODO: Detect text files based on contents? E.g., is the content valid UTF-8. */
            resp->statusCode = success_GmStatusCode;
            if (endsWithCase_String(path, ".gmi") || endsWithCase_String(path, ".gemini")) {
                setCStr_String(&resp->meta, "text/gemini; charset=utf-8");
            }
            else if (endsWithCase_String(path, ".txt")) {
                setCStr_String(&resp->meta, "text/plain");
            }
            else if (endsWithCase_String(path, ".png")) {
                setCStr_String(&resp->meta, "image/png");
            }
            else if (endsWithCase_String(path, ".jpg") || endsWithCase_String(path, ".jpeg")) {
                setCStr_String(&resp->meta, "image/jpeg");
            }
            else if (endsWithCase_String(path, ".gif")) {
                setCStr_String(&resp->meta, "image/gif");
            }
            else if (endsWithCase_String(path, ".wav")) {
                setCStr_String(&resp->meta, "audio/wave");
            }
            else if (endsWithCase_String(path, ".ogg")) {
                setCStr_String(&resp->meta, "audio/ogg");
            }
            else if (endsWithCase_String(path, ".mp3")) {
                setCStr_String(&resp->meta, "audio/mpeg");
            }
            else if (endsWithCase_String(path, ".mid")) {
                setCStr_String(&resp->meta, "audio/midi");
            }
            else {
                setCStr_String(&resp->meta, "application/octet-stream");
            }
            set_Block(&resp->body, collect_Block(readAll_File(f)));
            d->state = receivingBody_GmRequestState;
            iNotifyAudience(d, updated, GmRequestUpdated);
        }
        else {
            resp->statusCode = failedToOpenFile_GmStatusCode;
            setCStr_String(&resp->meta, cstr_String(path));
        }
        iRelease(f);
        d->state = finished_GmRequestState;
        iNotifyAudience(d, finished, GmRequestFinished);
        return;
    }
    else if (equalCase_Rangecc(url.scheme, "data")) {
        resp->statusCode = success_GmStatusCode;
        iString *src = collectNewCStr_String(url.scheme.start + 5);
        iRangecc header = { constBegin_String(src), constBegin_String(src) };
        while (header.end < constEnd_String(src) && *header.end != ',') {
            header.end++;
        }
        iBool isBase64 = iFalse;
        setRange_String(&resp->meta, header);
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
        set_Block(&resp->body, &src->chars);
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
    else if (equalCase_Rangecc(url.scheme, "gopher")) {
        beginGopherConnection_GmRequest_(d, host, port);
        return;
    }
    else if (!equalCase_Rangecc(url.scheme, "gemini")) {
        resp->statusCode = unsupportedProtocol_GmStatusCode;
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
    setHost_TlsRequest(d->req, host, port);
    setContent_TlsRequest(d->req,
                          utf8_String(collectNewFormat_String("%s\r\n", cstr_String(&d->url))));
    submit_TlsRequest(d->req);
}

void cancel_GmRequest(iGmRequest *d) {
    if (d->req) {
        cancel_TlsRequest(d->req);
    }
    cancel_Gopher(&d->gopher);
}

iGmResponse *lockResponse_GmRequest(iGmRequest *d) {
    iAssert(!d->isRespLocked);
    lock_Mutex(d->mtx);
    d->isRespLocked = iTrue;
    return d->resp;
}

void unlockResponse_GmRequest(iGmRequest *d) {
    if (d) {
        iAssert(d->isRespLocked);
        d->isRespLocked = iFalse;
        set_Atomic(&d->allowUpdate, iTrue);
        unlock_Mutex(d->mtx);
    }
}

iBool isFinished_GmRequest(const iGmRequest *d) {
    iBool done;
    iGuardMutex(d->mtx,
                done = (d->state == finished_GmRequestState || d->state == failure_GmRequestState));
    return done;
}

enum iGmStatusCode status_GmRequest(const iGmRequest *d) {
    enum iGmStatusCode code;
    iGuardMutex(d->mtx, code = d->resp->statusCode);
    return code;
}

const iString *meta_GmRequest(const iGmRequest *d) {
    iAssert(isFinished_GmRequest(d));
    return &d->resp->meta;
}

const iBlock *body_GmRequest(const iGmRequest *d) {
    iAssert(isFinished_GmRequest(d));
    return &d->resp->body;
}

size_t bodySize_GmRequest(const iGmRequest *d) {
    size_t size;
    iGuardMutex(d->mtx, size = size_Block(&d->resp->body));
    return size;
}

const iString *url_GmRequest(const iGmRequest *d) {
    return &d->url;
}

int certFlags_GmRequest(const iGmRequest *d) {
    int flags;
    iGuardMutex(d->mtx, flags = d->resp->certFlags);
    return flags;
}

iDate certExpirationDate_GmRequest(const iGmRequest *d) {
    iDate expr;
    iGuardMutex(d->mtx, expr = d->resp->certValidUntil);
    return expr;
}

iDefineClass(GmRequest)
