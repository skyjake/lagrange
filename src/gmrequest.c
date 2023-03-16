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
#include "resources.h"
#include "sitespec.h"
#include "defs.h"

#include <the_Foundation/archive.h>
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
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
    init_Block(&d->identityFingerprint, 0);
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
    initCopy_Block(&d->identityFingerprint, &other->identityFingerprint);
}

void deinit_GmResponse(iGmResponse *d) {
    deinit_String(&d->certSubject);
    deinit_Block(&d->body);
    deinit_Block(&d->certFingerprint);
    deinit_String(&d->meta);
    deinit_Block(&d->identityFingerprint);
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
    clear_Block(&d->identityFingerprint);
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
    serialize_Block(&d->identityFingerprint, outs);
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
    if (version_Stream(ins) >= responseIdentity_FileVersion) {
        deserialize_Block(&d->identityFingerprint, ins);
    }
}

/*----------------------------------------------------------------------------------------------*/

iDeclareType(UploadData)
iDeclareTypeConstruction(UploadData)
    
struct Impl_UploadData {
    iBlock  data;
    iString mime;
    iString token;
};

iDefineTypeConstruction(UploadData)

void init_UploadData(iUploadData *d) {
    init_Block(&d->data, 0);
    init_String(&d->mime);
    init_String(&d->token);
}

void deinit_UploadData(iUploadData *d) {
    deinit_String(&d->token);
    deinit_String(&d->mime);
    deinit_Block(&d->data);
}

/*----------------------------------------------------------------------------------------------*/

static iAtomicInt idGen_;

enum iGmRequestState {
    initialized_GmRequestState,
    receivingHeader_GmRequestState,
    receivingBody_GmRequestState,
    finished_GmRequestState,
    failure_GmRequestState,
};

struct Impl_GmRequest {
    iObject              object;
    uint32_t             id;
    iMutex *             mtx;
    iGmCerts *           certs; /* not owned */
    const iGmIdentity *  identity;
    enum iGmRequestState state;
    iString              url;
    iUploadData *        upload;
    iTlsRequest *        req;
    iGopher              gopher;
    iSocket *            spartan;
    iGmResponse *        resp;
    iBool                isProxy;
    iBool                isFilterEnabled;
    iBool                isRespLocked;
    iBool                isRespFiltered;
    iAtomicInt           allowUpdate;
    iAudience *          updated;
    iAudience *          finished;
    iGmRequestProgressFunc sendProgress;
};

iDefineObjectConstructionArgs(GmRequest, (iGmCerts *certs), certs)
iDefineAudienceGetter(GmRequest, updated)
iDefineAudienceGetter(GmRequest, finished)
    
static uint16_t port_GmRequest_(iGmRequest *d) {
    return urlPort_String(&d->url);
}
    
static void checkServerCertificate_GmRequest_(iGmRequest *d) {
    const iTlsCertificate *cert = d->req ? serverCertificate_TlsRequest(d->req) : NULL;
    iGmResponse *resp = d->resp;
    resp->certFlags = 0;
    if (cert) {
        const iRangecc domain = range_String(hostName_Address(address_TlsRequest(d->req)));
        const uint16_t port   = port_Address(address_TlsRequest(d->req));
        resp->certFlags |= available_GmCertFlag;
        set_Block(&resp->certFingerprint, collect_Block(publicKeyFingerprint_TlsCertificate(cert)));
        resp->certFlags |= haveFingerprint_GmCertFlag;
        if (!isExpired_TlsCertificate(cert)) {
            resp->certFlags |= timeVerified_GmCertFlag;
        }
        if (verifyDomain_GmCerts(cert, domain)) {
            resp->certFlags |= domainVerified_GmCertFlag;
        }
        if (checkTrust_GmCerts(d->certs, domain, port, cert)) {
            resp->certFlags |= trusted_GmCertFlag;
        }
        if (verify_TlsCertificate(cert) == authority_TlsCertificateVerifyStatus) {
            resp->certFlags |= authorityVerified_GmCertFlag;
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
                if (d->isFilterEnabled && willTryFilter_MimeHooks(mimeHooks_App(), &resp->meta)) {
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
    if (d->state == finished_GmRequestState || d->state == failure_GmRequestState) {
        /* The request has already finished or been aborted (e.g., invalid header). */
        delete_Block(readAll_TlsRequest(req));
        unlock_Mutex(d->mtx);
        return;
    }
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

static void applyFilter_GmRequest_(iGmRequest *d) {
    iAssert(d->state == finished_GmRequestState);
    iBlock *xbody = tryFilter_MimeHooks(mimeHooks_App(), &d->resp->meta, &d->resp->body, &d->url);
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

static void requestFinished_GmRequest_(iGmRequest *d, iTlsRequest *req) {
    iAssert(req == d->req);
    lock_Mutex(d->mtx);
    /* There shouldn't be anything left to read. */ {
        iBlock *data = readAll_TlsRequest(req);
        iAssert(isEmpty_Block(data));
        delete_Block(data);
        initCurrent_Time(&d->resp->when);
    }
    if (d->state == receivingHeader_GmRequestState &&
        status_TlsRequest(req) != error_TlsRequestStatus) {
        d->state = failure_GmRequestState;
        d->resp->statusCode = incompleteHeader_GmStatusCode;
        setCStr_String(&d->resp->meta, "");
    }
    else {
        d->state = (status_TlsRequest(req) == error_TlsRequestStatus ? failure_GmRequestState
                                                                     : finished_GmRequestState);
        if (d->state == failure_GmRequestState) {
            if (!isVerified_TlsRequest(req)) {
                if (isExpired_TlsCertificate(serverCertificate_TlsRequest(req))) {
                    d->resp->statusCode = d->isProxy ? proxyCertificateExpired_GmStatusCode
                                                     : tlsServerCertificateExpired_GmStatusCode;
                    setCStr_String(&d->resp->meta, get_GmError(d->resp->statusCode)->title);
                }
                else {
                    d->resp->statusCode = d->isProxy ? proxyCertificateNotVerified_GmStatusCode
                                                     : tlsServerCertificateNotVerified_GmStatusCode;
                    setCStr_String(&d->resp->meta, get_GmError(d->resp->statusCode)->title);
                }
            }
            else {
                d->resp->statusCode = tlsFailure_GmStatusCode;
                set_String(&d->resp->meta, errorMessage_TlsRequest(req));
            }
        }
    }
    checkServerCertificate_GmRequest_(d);
    unlock_Mutex(d->mtx);
    /* Check for mimehooks. */
    if (d->isRespFiltered && d->state == finished_GmRequestState) {
        applyFilter_GmRequest_(d);
    }
    iNotifyAudience(d, finished, GmRequestFinished);
}

static const iBlock *aboutPageSource_(iRangecc path, iRangecc query) {
    const struct { const char *name; const iBlock *data; } staticPages[] = {
        { "about",          &blobAbout_Resources },
        { "lagrange",       &blobLagrange_Resources },
        { "help",           &blobHelp_Resources },
        { "license",        &blobLicense_Resources },
        { "version",        &blobVersion_Resources },
        { "version-1.10",   &blobVersion_1_10_Resources },
        { "version-1.5",    &blobVersion_1_5_Resources },
        { "version-0.13",   &blobVersion_0_13_Resources },
    };
    iForIndices(i, staticPages) {
        if (equalCase_Rangecc(path, staticPages[i].name)) {
            return staticPages[i].data;
        }
    }
    if (equalCase_Rangecc(path, "debug")) {
        return utf8_String(debugInfo_App());
    }
    if (equalCase_Rangecc(path, "fonts")) {
        return utf8_String(infoPage_Fonts(query));
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
    return NULL;
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
            else if (equal_Rangecc(name, "ALT")) {
                repl = range_CStr(isApple_Platform() ? "\u2325" : "Alt");
            }
            else if (equal_Rangecc(name, "ALT+")) {
                repl = range_CStr(isApple_Platform() ? "\u2325" : "Alt+");
            }
            else if (equal_Rangecc(name, "CTRL")) {
                repl = range_CStr(isApple_Platform() ? "\u2318" : "Ctrl");
            }
            else if (equal_Rangecc(name, "CTRL+")) {
                repl = range_CStr(isApple_Platform() ? "\u2318" : "Ctrl+");
            }
            else if (equal_Rangecc(name, "SHIFT")) {
                repl = range_CStr(isApple_Platform() ? "\u21e7" : "Shift");
            }
            else if (equal_Rangecc(name, "SHIFT+")) {
                repl = range_CStr(isApple_Platform() ? "\u21e7" : "Shift+");
            }
            else {
                /* Translated string. */
                repl = range_String(string_Lang(cstr_Rangecc(name)));
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

static void spartanRead_GmRequest_(iGmRequest *d, iSocket *socket) {
    iBool notifyUpdate = iFalse;
    iBool notifyDone   = iFalse;
    lock_Mutex(d->mtx);
    iBlock *data = readAll_Socket(socket);
    if (!isEmpty_Block(data)) {
        if (d->state == receivingHeader_GmRequestState) {
            append_Block(&d->resp->meta.chars, data);
            size_t crlf = indexOfCStr_String(&d->resp->meta, "\r\n");
            if (crlf != iInvalidPos) {
                setData_Block(&d->resp->body, constBegin_String(&d->resp->meta) + crlf + 2,
                              size_Block(&d->resp->meta.chars) - crlf - 2);
                if (!isEmpty_Block(&d->resp->body)) {
                    notifyUpdate = iTrue;
                }
                truncate_Block(&d->resp->meta.chars, crlf);
                d->state = receivingBody_GmRequestState;
                iRegExp *metaPattern = new_RegExp("^([0-9]) (.*)", 0);
                iRegExpMatch m;
                init_RegExpMatch(&m);
                if (matchString_RegExp(metaPattern, &d->resp->meta, &m)) {
                    switch (toInt_String(collect_String(captured_RegExpMatch(&m, 1)))) {
                        case 2:
                            d->resp->statusCode = success_GmStatusCode;
                            set_String(&d->resp->meta, collect_String(captured_RegExpMatch(&m, 2)));
                            break;
                        case 3:
                            d->resp->statusCode = redirectTemporary_GmStatusCode;
                            d->state = finished_GmRequestState;
                            set_String(&d->resp->meta, collect_String(captured_RegExpMatch(&m, 2)));
                            notifyDone = iTrue;
                            break;
                        case 4:
                            d->resp->statusCode = badRequest_GmStatusCode;
                            d->state = finished_GmRequestState;
                            set_String(&d->resp->meta, collect_String(captured_RegExpMatch(&m, 2)));
                            notifyDone = iTrue;
                            break;
                        case 5:
                            d->resp->statusCode = permanentFailure_GmStatusCode;
                            d->state = finished_GmRequestState;
                            set_String(&d->resp->meta, collect_String(captured_RegExpMatch(&m, 2)));
                            notifyDone = iTrue;
                            break;
                        default:
                            d->resp->statusCode = invalidHeader_GmStatusCode;
                            d->state            = finished_GmRequestState;
                            notifyDone          = iTrue;                            
                            break;
                    } 
                }
                else {
                    d->resp->statusCode = invalidHeader_GmStatusCode;
                    d->state            = finished_GmRequestState;
                    notifyDone          = iTrue;
                }
                iRelease(metaPattern);
            }
        }
        else if (d->state == receivingBody_GmRequestState) {
            append_Block(&d->resp->body, data);
            notifyUpdate = iTrue;
        }
    }
    delete_Block(data);    
    unlock_Mutex(d->mtx);
    if (notifyUpdate) {
        iNotifyAudience(d, updated, GmRequestUpdated);
    }
    if (notifyDone) {
        iNotifyAudience(d, finished, GmRequestFinished);
    }
}

static void spartanDisconnected_GmRequest_(iGmRequest *d, iSocket *socket) {
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

static void spartanError_GmRequest_(iGmRequest *d, iSocket *socket, int error, const char *msg) {
    iUnused(socket);
    lock_Mutex(d->mtx);
    d->state = failure_GmRequestState;
    d->resp->statusCode = tlsFailure_GmStatusCode;
    format_String(&d->resp->meta, "%s (errno %d)", msg, error);
    clear_Block(&d->resp->body);
    unlock_Mutex(d->mtx);
    iNotifyAudience(d, finished, GmRequestFinished);
}

static void beginSpartanConnection_GmRequest_(iGmRequest *d, const iString *host, uint16_t port) {
    d->state = receivingHeader_GmRequestState;
    d->spartan = new_Socket(cstr_String(host), port);
    iConnect(Socket, d->spartan, readyRead,    d, spartanRead_GmRequest_);
    iConnect(Socket, d->spartan, disconnected, d, spartanDisconnected_GmRequest_);
    iConnect(Socket, d->spartan, error,        d, spartanError_GmRequest_);
    open_Socket(d->spartan);
    iUrl url;
    init_Url(&url, &d->url);
    iBlock *message = new_Block(0);
    iBlock *data    = new_Block(0);
    if (!isEmpty_Range(&url.query)) {
        set_Block(data,
                  utf8_String(collect_String(urlDecode_String(
                      collectNewRange_String((iRangecc){ url.query.start + 1, url.query.end })))));
    }
    if (d->upload) {
        set_Block(data, &d->upload->data);    
    }
    printf_Block(message,
                 "%s %s %zu\r\n",
                 cstr_Rangecc(url.host),
                 !isEmpty_Range(&url.path) ? cstr_Rangecc(url.path) : "/",
                 size_Block(data));
    write_Socket(d->spartan, message);
    write_Socket(d->spartan, data);
    delete_Block(data);
    delete_Block(message);
}

/*----------------------------------------------------------------------------------------------*/

void init_GmRequest(iGmRequest *d, iGmCerts *certs) {
    d->mtx             = new_Mutex();
    d->id              = add_Atomic(&idGen_, 1) + 1;
    d->identity        = NULL;
    d->resp            = new_GmResponse();
    d->isProxy         = iFalse;
    d->isFilterEnabled = iTrue;
    d->isRespLocked    = iFalse;
    d->isRespFiltered  = iFalse;
    set_Atomic(&d->allowUpdate, iTrue);
    init_String(&d->url);
    init_Gopher(&d->gopher);
    d->spartan      = NULL;
    d->upload       = NULL;
    d->certs        = certs;
    d->req          = NULL;
    d->updated      = NULL;
    d->finished     = NULL;
    d->sendProgress = NULL;
    d->state        = initialized_GmRequestState;
}

void deinit_GmRequest(iGmRequest *d) {
    if (d->req) {
        iDisconnectObject(TlsRequest, d->req, sent, d);
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
    delete_UploadData(d->upload);
    deinit_Gopher(&d->gopher);
    iRelease(d->spartan);
    delete_Audience(d->finished);
    delete_Audience(d->updated);
    delete_GmResponse(d->resp);
    deinit_String(&d->url);
    delete_Mutex(d->mtx);
}

void enableFilters_GmRequest(iGmRequest *d, iBool enable) {
    d->isFilterEnabled = enable;
}

void setUrl_GmRequest(iGmRequest *d, const iString *url) {
    set_String(&d->url, canonicalUrl_String(urlFragmentStripped_String(url)));
    /* Encode hostname to Punycode here because we want to submit the Punycode domain name
       in the request. (TODO: Pending possible Gemini spec change.) */
    punyEncodeUrlHost_String(&d->url);
    /* TODO: Gemini spec allows UTF-8 encoded URLs, but still need to percent-encode non-ASCII
       characters? Could be a server-side issue, e.g., if they're using a URL parser meant for
       the web. */
    /* Encode everything except already-percent encoded characters. */
    iString *enc = urlEncodeExclude_String(&d->url, "%" URL_RESERVED_CHARS);
    /* Normalize empty paths to /. */ {
        iUrl parts;
        init_Url(&parts, enc);
        if (isEmpty_Range(&parts.path) && equalCase_Rangecc(parts.scheme, "gemini") &&
            parts.path.start) {
            /* Normalize to "/" as per specification (November 2021 update). */
            insertData_Block(&enc->chars, parts.path.start - constBegin_String(enc), "/", 1);
        }
    }
    set_String(&d->url, enc);
    delete_String(enc);
    d->identity = identityForUrl_GmCerts(d->certs, &d->url);
}

void setIdentity_GmRequest(iGmRequest *d, const iGmIdentity *id) {
    d->identity = id;
}

static iBool isTitan_GmRequest_(const iGmRequest *d) {
    return equalCase_Rangecc(urlScheme_String(&d->url), "titan");
}

void setUploadData_GmRequest(iGmRequest *d, const iString *mime, const iBlock *payload,
                             const iString *token) {
    if (!d->upload) {
        d->upload = new_UploadData();   
    }
    set_Block(&d->upload->data, payload);
    set_String(&d->upload->mime, mime);
    set_String(&d->upload->token, token);
}

void setSendProgressFunc_GmRequest(iGmRequest *d, iGmRequestProgressFunc func) {
    d->sendProgress = func;
}

static void bytesSent_GmRequest_(iGmRequest *d, iTlsRequest *req, size_t sent, size_t toSend) {
    iUnused(req);
    if (d->sendProgress) {
        d->sendProgress(d, sent, toSend);
    }
}

static iBool isDirectory_(const iString *path) {
    /* TODO: move this to the_Foundation */
    iFileInfo *info = new_FileInfo(path);
    const iBool isDir = isDirectory_FileInfo(info);
    iRelease(info);
    return isDir;
}

static int cmp_FileInfoPtr_(const iFileInfo **a, const iFileInfo **b) {
    return cmpStringCase_String(path_FileInfo(*a), path_FileInfo(*b));
}

static const iString *directoryIndexPage_Archive_(const iArchive *d, const iString *entryPath) {
    static const char *names[] = { "index.gmi", "index.gemini" };
    iForIndices(i, names) {
        iString *path = !isEmpty_String(entryPath) ?
            concatCStr_Path(entryPath, names[i]) : newCStr_String(names[i]);
        if (entry_Archive(d, path)) {
            return collect_String(path);
        }
        delete_String(path);
    }
    return NULL;
}

void submit_GmRequest(iGmRequest *d) {
    iAssert(d->state == initialized_GmRequestState);
    if (d->state != initialized_GmRequestState) {
        return;
    }
    set_Atomic(&d->allowUpdate, iTrue);
    iGmResponse *resp = d->resp;
    clear_GmResponse(resp);
#if !defined (NDEBUG) && !defined (iPlatformTerminal)
    fprintf(stderr, "[GmRequest] URL: %s\n", cstr_String(&d->url)); fflush(stderr);
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
            if (equalCase_Rangecc(url.path, "lagrange")) {
                /* The "Powered by" line needs dynamic updates depending on the build. */
                iString body;
                initBlock_String(&body, &resp->body);
                replace_String(&body, "OpenSSL", libraryName_TlsRequest());
#if defined (iPlatformTerminal)
                replace_String(&body, "SDL 2", "ncurses");
#endif
                set_Block(&resp->body, utf8_String(&body));
                deinit_String(&body);
            }
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
        /* TODO: Move handling of "file://" URLs elsewhere, it's getting complex. */
        iString *path = collect_String(localFilePathFromUrl_String(&d->url));
        /* Note: As a local file path, `path` uses the OS directory separators
           (i.e., \ on Windows). `Archive` accepts both. */
        iFile *f = new_File(path);
        if (isDirectory_(path)) {
            if (endsWith_String(path, iPathSeparator)) {
                removeEnd_String(path, 1);
            }
            resp->statusCode = success_GmStatusCode;
            setCStr_String(&resp->meta, "text/gemini");
            iString *page = collectNew_String();
            iString *parentDir = collectNewRange_String(dirName_Path(path));
            if (!isMobile_Platform()) {
                appendFormat_String(page, "=> %s " keyUpArrow_Icon " %s" iPathSeparator "\n\n",
                                    cstrCollect_String(makeFileUrl_String(parentDir)),
                                    cstr_String(parentDir));
            }
            appendFormat_String(page, "# %s\n", cstr_Rangecc(baseName_Path(path)));
            /* Make a directory index page. */
            iPtrArray *sortedInfo = collectNew_PtrArray();
            iForEach(DirFileInfo, entry,
                     iClob(directoryContents_FileInfo(iClob(new_FileInfo(path))))) {
                /* Ignore some files. */
                if (isApple_Platform()) {
                    const iRangecc name = baseName_Path(path_FileInfo(entry.value));
                    if (equal_Rangecc(name, ".DS_Store") ||
                        equal_Rangecc(name, ".localized")) {
                        continue;
                    }
                }
                pushBack_PtrArray(sortedInfo, ref_Object(entry.value));
            }
            sort_Array(sortedInfo, (int (*)(const void *, const void *)) cmp_FileInfoPtr_);
            iForEach(PtrArray, s, sortedInfo) {
                const iFileInfo *entry = s.ptr;
                appendFormat_String(page, "=> %s %s%s%s\n",
                                    cstrCollect_String(makeFileUrl_String(path_FileInfo(entry))),
                                    isDirectory_FileInfo(entry) ? folder_Icon " " : "",
                                    cstr_Rangecc(baseName_Path(path_FileInfo(entry))),
                                    isDirectory_FileInfo(entry) ? iPathSeparator : "");
                iRelease(entry);
            }
            set_Block(&resp->body, utf8_String(page));
        }
        else if (open_File(f, readOnly_FileMode)) {
            resp->statusCode = success_GmStatusCode;
            setCStr_String(&resp->meta, mediaType_Path(path));
            /* TODO: Detect text files based on contents? E.g., is the content valid UTF-8. */
            set_Block(&resp->body, collect_Block(readAll_File(f)));
            d->state = receivingBody_GmRequestState;
            iNotifyAudience(d, updated, GmRequestUpdated);
        }
        else {
            /* It could be a path inside an archive. */
            const iString *container = findContainerArchive_Path(path);
            if (container) {
                iArchive *arch = iClob(new_Archive());
                if (openFile_Archive(arch, container)) {
                    iString *entryPath = collect_String(copy_String(path));
                    remove_Block(&entryPath->chars, 0, size_String(container) + 1); /* last slash, too */
                    iBool isDir = isDirectory_Archive(arch, entryPath);
                    if (isDir && !isEmpty_String(entryPath) &&
                        !endsWith_String(entryPath, iPathSeparator)) {
                        /* Must have a slash for directories, otherwise relative navigation
                           will not work. */
                        resp->statusCode = redirectPermanent_GmStatusCode;
                        set_String(&resp->meta, withSpacesEncoded_String(collect_String(makeFileUrl_String(container))));
                        appendCStr_String(&resp->meta, "/");
                        append_String(&resp->meta, entryPath);
                        appendCStr_String(&resp->meta, "/");
                        goto fileRequestFinished;
                    }
                    /* Check for a Gemini index page. */
                    if (isDir && prefs_App()->openArchiveIndexPages) {
                        const iString *indexPath = directoryIndexPage_Archive_(arch, entryPath);
                        if (indexPath) {
                            set_String(entryPath, indexPath);
                            isDir = iFalse;
                        }
                    }
                    /* Show an archive index page if this is a directory. */
                    /* TODO: Use a built-in MIME hook for this? */
                    if (isDir) {
                        iString *page = new_String();
                        const iRangecc containerName = baseName_Path(container);
                        const iString *containerUrl =
                            withSpacesEncoded_String(collect_String(makeFileUrl_String(container)));
                        const iBool isRoot = isEmpty_String(entryPath);
                        if (!isRoot) {
                            const iRangecc curDir = dirName_Path(entryPath);
                            const iRangecc parentDir = dirName_Path(collectNewRange_String(curDir));
                            if (!equal_Rangecc(parentDir, ".")) {
                                /* A subdirectory. */
                                appendFormat_String(page,
                                                    "=> ../ " keyUpArrow_Icon " %s" iPathSeparator
                                                    "\n",
                                                    cstr_Rangecc(parentDir));
                            }
                            else {
                                /* Top-level directory. */
                                appendFormat_String(page,
                                                    "=> %s/ " keyUpArrow_Icon " Root\n",
                                                    cstr_String(containerUrl));
                            }
                            appendFormat_String(page, "# %s\n\n", cstr_Rangecc(baseName_Path(collectNewRange_String(curDir))));
                        }
                        else {
                            /* The root directory. */
                            appendFormat_String(page, "=> %s " close_Icon " ${archive.exit}\n",
                                                cstr_String(containerUrl));
                            appendFormat_String(page, "# %s\n\n", cstr_Rangecc(containerName));
                            appendFormat_String(page,
                                                cstrCount_Lang("archive.summary.n",
                                                               (int) numEntries_Archive(arch)),
                                                numEntries_Archive(arch),
                                                (double) sourceSize_Archive(arch) / 1.0e6);
                            appendCStr_String(page, "\n\n");
                        }
                        iStringSet *contents = iClob(listDirectory_Archive(arch, entryPath));
                        if (!isRoot) {
                            if (isEmpty_StringSet(contents)) {
                                appendCStr_String(page, "${dir.empty}\n");
                            }
                            else if (size_StringSet(contents) > 1) {
                                appendFormat_String(page, cstrCount_Lang("dir.summary.n",
                                                                         (int) size_StringSet(contents)),
                                                    size_StringSet(contents));
                                appendCStr_String(page, "\n\n");
                            }
                        }
                        translate_Lang(page);
                        iConstForEach(StringSet, e, contents) {
                            const iString *subPath = e.value;
                            iRangecc relSub = range_String(subPath);
                            relSub.start += size_String(entryPath);
                            appendFormat_String(page, "=> %s/%s %s%s\n",
                                                cstr_String(&d->url),
                                                cstr_String(withSpacesEncoded_String(collectNewRange_String(relSub))),
                                                endsWith_Rangecc(relSub, "/") ? folder_Icon " " : "",
                                                cstr_Rangecc(relSub));
                        }
                        resp->statusCode = success_GmStatusCode;
                        setCStr_String(&resp->meta, "text/gemini; charset=utf-8");
                        set_Block(&resp->body, utf8_String(page));
                        delete_String(page);
                    }
                    else {
                        const iBlock *data = data_Archive(arch, entryPath);
                        if (data) {
                            resp->statusCode = success_GmStatusCode;
                            setCStr_String(&resp->meta, mediaType_Path(entryPath));
                            set_Block(&resp->body, data);
                        }
                        else {
                            resp->statusCode = failedToOpenFile_GmStatusCode;
                            setCStr_String(&resp->meta, cstr_String(path));
                        }
                    }
                fileRequestFinished:;
                }
            }
            else {
                resp->statusCode = failedToOpenFile_GmStatusCode;
                setCStr_String(&resp->meta, cstr_String(path));
            }
        }
        iRelease(f);
        d->state = finished_GmRequestState;
        /* MIME hooks may to this content. */
        if (d->isFilterEnabled && resp->statusCode == success_GmStatusCode) {
            /* TODO: Use a background thread, the hook may take some time to run. */
            applyFilter_GmRequest_(d);
        }
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
        schemeProxyHostAndPort_App(url.scheme, &host, &port);
        d->isProxy = iTrue;
    }
    else if (equalCase_Rangecc(url.scheme, "gopher")) {
        beginGopherConnection_GmRequest_(d, host, port ? port : 70);
        return;
    }
    else if (equalCase_Rangecc(url.scheme, "finger")) {
        beginGopherConnection_GmRequest_(d, host, port ? port : 79);
        return;
    }
    else if (equalCase_Rangecc(url.scheme, "spartan")) {
        beginSpartanConnection_GmRequest_(d, host, port ? port : 300);
        return;
    }
    else if (!equalCase_Rangecc(url.scheme, "gemini") &&
             !equalCase_Rangecc(url.scheme, "titan")) {
        resp->statusCode = unsupportedProtocol_GmStatusCode;
        d->state = finished_GmRequestState;
        iNotifyAudience(d, finished, GmRequestFinished);
        return;
    }
    d->state = receivingHeader_GmRequestState;
    d->req = new_TlsRequest();
    if (d->identity) {
        setCertificate_TlsRequest(d->req, d->identity->cert);
        set_Block(&resp->identityFingerprint, &d->identity->fingerprint);
    }
    /* Site-specific settings. */ {
        iString siteRoot;
        initRange_String(&siteRoot, urlRoot_String(&d->url));
        setSessionCacheEnabled_TlsRequest(
            d->req, value_SiteSpec(&siteRoot, tlsSessionCache_SiteSpeckey) != 0);
        deinit_String(&siteRoot);
    }
    iConnect(TlsRequest, d->req, readyRead, d, readIncoming_GmRequest_);
    iConnect(TlsRequest, d->req, sent, d, bytesSent_GmRequest_);
    iConnect(TlsRequest, d->req, finished, d, requestFinished_GmRequest_);
    if (port == 0) {
        port = GEMINI_DEFAULT_PORT; /* default Gemini port */
    }
    setHost_TlsRequest(d->req, host, port);
    /* Titan requests can have an arbitrary payload. */
    if (isTitan_GmRequest_(d)) {
        iBlock content;
        init_Block(&content, 0);
        if (d->upload) {
            printf_Block(&content,
                         "%s;mime=%s;size=%zu",
                         cstr_String(&d->url),
                         cstr_String(&d->upload->mime),
                         size_Block(&d->upload->data));
            if (!isEmpty_String(&d->upload->token)) {
                appendCStr_Block(&content, ";token=");
                append_Block(&content,
                             utf8_String(collect_String(urlEncode_String(&d->upload->token))));
            }
            appendCStr_Block(&content, "\r\n");
            append_Block(&content, &d->upload->data);
        }
        else {
            /* Empty data. */
            printf_Block(
                &content, "%s;mime=application/octet-stream;size=0\r\n", cstr_String(&d->url));
        }
        setContent_TlsRequest(d->req, &content);
        deinit_Block(&content);
    }
    else {
        /* Gemini request. */
        setContent_TlsRequest(d->req,
                              utf8_String(collectNewFormat_String("%s\r\n", cstr_String(&d->url))));
    }
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

uint32_t id_GmRequest(const iGmRequest *d) {
    return d ? d->id : 0;
}

iBool isFinished_GmRequest(const iGmRequest *d) {
    if (d) {
        iBool done;
        iGuardMutex(d->mtx,
                    done = (d->state == finished_GmRequestState || d->state == failure_GmRequestState));
        return done;
    }
    return iTrue;
}

iBool filtersEnabled_GmRequest(const iGmRequest *d) {
    return d->isFilterEnabled;
}

enum iGmStatusCode status_GmRequest(const iGmRequest *d) {
    if (d) {
        enum iGmStatusCode code;
        iGuardMutex(d->mtx, code = d->resp->statusCode);
        return code;
    }
    return none_GmStatusCode;
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

iBool isProxy_GmRequest(const iGmRequest *d) {
    return d->isProxy;
}

const iAddress *address_GmRequest(const iGmRequest *d) {
    return d && d->req ? address_TlsRequest(d->req) : NULL;
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
