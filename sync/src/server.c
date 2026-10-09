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

#include "server.h"

#include <the_Foundation/atomic.h>
#include <the_Foundation/file.h>
#include <the_Foundation/mutex.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/thread.h>
#include <the_Foundation/time.h>
#include <the_Foundation/tlsservice.h>

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>

enum { maxHeaderSize_ = 1024, maxConnections_ = 32, maxRecentSize_ = 256 * 1024 };

static const double requestTimeout_ = 10;        /* seconds to receive the request line */
static const double uploadTimeout_  = 30;        /* seconds, in addition to the transfer time */
static const double minUploadRate_  = 5000;      /* bytes per second */
static const double expiryInterval_ = 24 * 3600; /* seconds */
static const double summaryInterval_ = 24 * 3600; /* seconds */

static const char *dataPath_   = "/data";
static const char *recentPath_ = "/recent";

static iAtomicInt isStopping_;

/* Activity since the last summary. */
iDeclareType(ServerStats)

struct Impl_ServerStats {
    iMutex   mtx;
    uint64_t numRequests;
    uint64_t bytesSent;
    uint64_t bytesReceived;
};

static void addTraffic_ServerStats_(iServerStats *d, size_t sent, size_t received) {
    iGuardMutex(&d->mtx, {
        d->bytesSent += sent;
        d->bytesReceived += received;
    });
}

/* Owns the request handlers. */
iDeclareClass(Server)

struct Impl_Server {
    iObject              object;
    const iServerConfig *cfg;
    iStorage            *storage;
    iServerStats         stats;
    iMutex               mtx;
    iPtrArray            handlers;
};

/* Request state of a single connection. Notifications arrive in the connection's thread. */
iDeclareClass(Handler)

struct Impl_Handler {
    iObject              object;
    iAtomicInt           isFinished; /* connection closed; can be released */
    iTlsConnection      *conn;
    const iServerConfig *cfg;
    iStorage            *storage;
    iServerStats        *stats;
    iString              remoteAddr;
    iString              fingerprint; /* client certificate; empty if none */
    iBool                isEd25519;
    iString              header;
    iBlock               received;
    enum iStorageBlob    blob;
    size_t               uploadSize;
    uint64_t             uploadGen;
    iBool                isUpload;
    iBool                isDone;
};

static void log_Handler_(const iHandler *d, const char *format, ...) {
    iString *msg = new_String();
    va_list args;
    va_start(args, format);
    vprintf_Block(&msg->chars, format, args);
    va_end(args);
    iDate now;
    initCurrent_Date(&now);
    printf("%s %s [%.8s] %s\n",
           cstrCollect_String(format_Date(&now, "%Y-%m-%d %H:%M:%S")),
           cstr_String(&d->remoteAddr),
           isEmpty_String(&d->fingerprint) ? "--------" : cstr_String(&d->fingerprint),
           cstr_String(msg));
    fflush(stdout);
    delete_String(msg);
}

static void respond_Handler_(iHandler *d, iTlsConnection *conn, int status, const char *meta,
                             const iBlock *body) {
    iString *header = collectNewFormat_String("%02d %s\r\n", status, meta);
    write_TlsConnection(conn, utf8_String(header));
    if (body) {
        write_TlsConnection(conn, body);
    }
    addTraffic_ServerStats_(d->stats, size_String(header) + (body ? size_Block(body) : 0), 0);
    close_TlsConnection(conn);
    d->isDone = iTrue;
    log_Handler_(d, "%02d %s%s", status, meta,
                 body ? format_CStr(" (%zu bytes)", size_Block(body)) : "");
}

iDeclareType(RequestUrl)

struct Impl_RequestUrl {
    iRangecc scheme;
    iRangecc path;   /* Titan parameters are part of the path */
    iRangecc params; /* e.g., ";size=123" */
    iRangecc query;  /* includes the "?" */
};

static iBool parse_RequestUrl_(iRequestUrl *d, iRangecc url) {
    iZap(*d);
    const char *sep = strstr(url.start, "://");
    if (!sep || sep >= url.end) {
        return iFalse;
    }
    d->scheme = (iRangecc){ url.start, sep };
    const char *pos = sep + 3;
    while (pos < url.end && *pos != '/' && *pos != '?') {
        pos++; /* authority */
    }
    d->path.start = pos;
    while (pos < url.end && *pos != '?' && *pos != '#') {
        pos++;
    }
    d->path.end = pos;
    if (pos < url.end && *pos == '?') {
        d->query.start = pos;
        while (pos < url.end && *pos != '#') {
            pos++;
        }
        d->query.end = pos;
    }
    const char *semi = isEmpty_Range(&d->path) ? NULL : memchr(d->path.start, ';', size_Range(&d->path));
    if (semi) {
        d->params   = (iRangecc){ semi, d->path.end };
        d->path.end = semi;
    }
    return iTrue;
}

static iBool parseGeneration_(iRangecc query, uint64_t *gen_out) {
    /* The query is just the generation number. */
    if (size_Range(&query) < 2 || query.start[0] != '?') {
        return iFalse;
    }
    char *end = NULL;
    *gen_out = strtoull(query.start + 1, &end, 10);
    return end == query.end;
}

static iBool parseSize_(iRangecc params, size_t *size_out) {
    /* Titan parameters: ;size=N;mime=...;token=... */
    iRangecc param = iNullRange;
    while (nextSplit_Rangecc(params, ";", &param)) {
        if (startsWith_Rangecc(param, "size=")) {
            char *end = NULL;
            *size_out = strtoull(param.start + 5, &end, 10);
            return end == param.end;
        }
    }
    return iFalse;
}

static void respondInfo_Handler_(iHandler *d, iTlsConnection *conn) {
    iString *page = collectNewCStr_String("# Lagrange Sync\n\n");
    if (isEmpty_String(&d->fingerprint)) {
        appendCStr_String(page, "No client certificate.\n");
    }
    else {
        appendFormat_String(page, "Certificate fingerprint:\n%s\n\n", cstr_String(&d->fingerprint));
        if (!d->isEd25519) {
            appendCStr_String(page, "\u26a0 This is not an Ed25519 certificate. It cannot be used for syncing.\n");
        }
        else if (!isAllowed_Storage(d->storage, &d->fingerprint)) {
            appendCStr_String(page, "This certificate is not authorized for syncing.\n");
        }
        else if (isFull_Storage(d->storage, &d->fingerprint)) {
            appendCStr_String(page, "The server is full. New accounts cannot be created.\n");
        }
        else {
            uint64_t gen  = 0;
            iBlock  *data = load_Storage(d->storage, &d->fingerprint, data_StorageBlob, &gen);
            if (data) {
                appendFormat_String(page, "Stored: %.1f KB (generation %llu)\n",
                                    size_Block(data) / 1024.0f, (unsigned long long) gen);
                delete_Block(data);
                data = load_Storage(d->storage, &d->fingerprint, recent_StorageBlob, &gen);
                if (data) {
                    appendFormat_String(page, "Recent changes: %.1f KB (generation %llu)\n",
                                        size_Block(data) / 1024.0f, (unsigned long long) gen);
                    delete_Block(data);
                }
            }
            else {
                appendCStr_String(page, "No data stored.\n");
            }
        }
    }
    respond_Handler_(d, conn, 20, "text/gemini; charset=utf-8", utf8_String(page));
}

static void respondDownload_Handler_(iHandler *d, iTlsConnection *conn, uint64_t clientGen) {
    uint64_t gen  = 0;
    iBlock  *data = load_Storage(d->storage, &d->fingerprint, d->blob, &gen);
    if (gen == clientGen) {
        respond_Handler_(d, conn, 40, "unchanged", NULL);
    }
    else {
        /* The generation precedes the data, since Gemini does not allow custom MIME
           parameters. */
        iBlock *body = collect_Block(
            newCStr_Block(format_CStr("%llu\n", (unsigned long long) gen)));
        if (data) {
            append_Block(body, data);
        }
        respond_Handler_(d, conn, 20, "application/lagrange-sync", body);
    }
    delete_Block(data);
}

static void finishUpload_Handler_(iHandler *d, iTlsConnection *conn) {
    truncate_Block(&d->received, d->uploadSize);
    uint64_t newGen = 0;
    switch (store_Storage(
        d->storage, &d->fingerprint, d->blob, d->uploadGen, &d->received, &newGen)) {
        case ok_StoreResult:
            respond_Handler_(d, conn, 20, "text/plain",
                             collect_Block(newCStr_Block(
                                 format_CStr("%llu\n", (unsigned long long) newGen))));
            break;
        case stale_StoreResult:
            respond_Handler_(d, conn, 40, "stale", NULL);
            break;
        case full_StoreResult:
            respond_Handler_(d, conn, 61, "Server is full", NULL);
            break;
        case noAccount_StoreResult:
            respond_Handler_(d, conn, 61, "No account", NULL);
            break;
        default:
            respond_Handler_(d, conn, 42, "Failed to store data", NULL);
            break;
    }
}

static void handleRequest_Handler_(iHandler *d, iTlsConnection *conn) {
    log_Handler_(d, "%s", cstr_String(&d->header));
    iGuardMutex(&d->stats->mtx, d->stats->numRequests++);
    iRequestUrl url;
    if (!parse_RequestUrl_(&url, range_String(&d->header))) {
        respond_Handler_(d, conn, 59, "Bad request", NULL);
        return;
    }
    const iBool isTitan = equalCase_Rangecc(url.scheme, "titan");
    if (!isTitan && !equalCase_Rangecc(url.scheme, "gemini")) {
        respond_Handler_(d, conn, 53, "Unsupported scheme", NULL);
        return;
    }
    if (!isTitan && (isEmpty_Range(&url.path) || equal_Rangecc(url.path, "/"))) {
        respondInfo_Handler_(d, conn);
        return;
    }
    if (endsWith_Rangecc(url.path, dataPath_)) {
        d->blob = data_StorageBlob;
    }
    else if (endsWith_Rangecc(url.path, recentPath_)) {
        d->blob = recent_StorageBlob;
    }
    else {
        respond_Handler_(d, conn, 51, "Not found", NULL);
        return;
    }
    if (isEmpty_String(&d->fingerprint)) {
        respond_Handler_(d, conn, 60, "Client certificate required", NULL);
        return;
    }
    if (!d->isEd25519) {
        respond_Handler_(d, conn, 62, "Ed25519 certificate required", NULL);
        return;
    }
    if (!isAllowed_Storage(d->storage, &d->fingerprint)) {
        respond_Handler_(d, conn, 61, "Certificate not authorized", NULL);
        return;
    }
    uint64_t gen = 0;
    if (!parseGeneration_(url.query, &gen)) {
        respond_Handler_(d, conn, 59, "Generation missing", NULL);
        return;
    }
    touch_Storage(d->storage, &d->fingerprint);
    if (!isTitan) {
        respondDownload_Handler_(d, conn, gen);
        return;
    }
    if (!parseSize_(url.params, &d->uploadSize)) {
        respond_Handler_(d, conn, 59, "Missing size parameter", NULL);
        return;
    }
    const size_t maxSize =
        d->blob == recent_StorageBlob ? iMin(maxRecentSize_, d->cfg->maxDataSize)
                                      : d->cfg->maxDataSize;
    if (d->uploadSize > maxSize) {
        respond_Handler_(d, conn, 59, format_CStr("Data too large (maximum is %zu bytes)",
                                                  maxSize), NULL);
        return;
    }
    /* Stale uploads are rejected before receiving the data. */
    uint64_t current = 0;
    delete_Block(load_Storage(d->storage, &d->fingerprint, d->blob, &current));
    if (current != gen) {
        respond_Handler_(d, conn, 40, "stale", NULL);
        return;
    }
    if (d->blob == data_StorageBlob && isFull_Storage(d->storage, &d->fingerprint)) {
        respond_Handler_(d, conn, 61, "Server is full", NULL);
        return;
    }
    if (d->blob == recent_StorageBlob) {
        uint64_t dataGen = 0;
        iBlock  *data    = load_Storage(d->storage, &d->fingerprint, data_StorageBlob, &dataGen);
        if (!data) {
            respond_Handler_(d, conn, 61, "No account", NULL);
            return;
        }
        delete_Block(data);
    }
    d->isUpload  = iTrue;
    d->uploadGen = gen;
    setDeadline_TlsConnection(conn, uploadTimeout_ + d->uploadSize / minUploadRate_);
}

static void readyRead_Handler_(iHandler *d, iTlsConnection *conn) {
    iBlock *data = readAll_TlsConnection(conn);
    addTraffic_ServerStats_(d->stats, 0, size_Block(data));
    if (!d->isDone) {
        append_Block(&d->received, data);
    }
    delete_Block(data);
    if (d->isDone) {
        return;
    }
    if (!d->isUpload) {
        const char *start = constData_Block(&d->received);
        const char *crlf  = strstr(start, "\r\n");
        if (!crlf) {
            if (size_Block(&d->received) > maxHeaderSize_) {
                respond_Handler_(d, conn, 59, "Bad request", NULL);
            }
            return;
        }
        setRange_String(&d->header, (iRangecc){ start, crlf });
        remove_Block(&d->received, 0, crlf + 2 - start);
        handleRequest_Handler_(d, conn);
    }
    if (d->isUpload && !d->isDone && size_Block(&d->received) >= d->uploadSize) {
        finishUpload_Handler_(d, conn);
    }
}

static void disconnected_Handler_(iHandler *d, iTlsConnection *conn) {
    iUnused(conn);
    /* This is the last notification from the connection. */
    if (!d->isDone) {
        if (d->isUpload) {
            log_Handler_(d, "disconnected before a response (received %zu of %zu bytes)",
                         size_Block(&d->received), d->uploadSize);
        }
        else {
            log_Handler_(d, "disconnected before a response");
        }
    }
    set_Atomic(&d->isFinished, iTrue);
}

static void init_Handler(iHandler *d, const iServer *server, iTlsConnection *conn) {
    set_Atomic(&d->isFinished, iFalse);
    d->conn    = ref_Object(conn);
    d->cfg     = server->cfg;
    d->storage = server->storage;
    d->stats   = (iServerStats *) &server->stats;
    init_String(&d->remoteAddr);
    if (address_TlsConnection(conn)) {
        iString *addr = toString_Address(address_TlsConnection(conn));
        set_String(&d->remoteAddr, addr);
        delete_String(addr);
    }
    init_String(&d->fingerprint);
    d->isEd25519 = iFalse;
    const iTlsCertificate *cert = clientCertificate_TlsConnection(conn);
    if (cert) {
        iBlock *fp = fingerprint_TlsCertificate(cert);
        iString *hex = hexEncode_Block(fp);
        set_String(&d->fingerprint, hex);
        delete_String(hex);
        delete_Block(fp);
        d->isEd25519 = (keyType_TlsCertificate(cert) == ed25519_TlsCertificateKeyType);
    }
    init_String(&d->header);
    init_Block(&d->received, 0);
    d->blob       = data_StorageBlob;
    d->uploadSize = 0;
    d->uploadGen  = 0;
    d->isUpload   = iFalse;
    d->isDone     = iFalse;
    setDeadline_TlsConnection(conn, requestTimeout_);
}

static void deinit_Handler(iHandler *d) {
    /* The connection may be released in another thread, so its audiences must be
       left before then. */
    iDisconnect(TlsConnection, d->conn, readyRead, d, readyRead_Handler_);
    iDisconnect(TlsConnection, d->conn, disconnected, d, disconnected_Handler_);
    iRelease(d->conn);
    deinit_Block(&d->received);
    deinit_String(&d->header);
    deinit_String(&d->fingerprint);
    deinit_String(&d->remoteAddr);
}

iDefineObjectConstructionArgs(Handler,
                              (const iServer *server, iTlsConnection *conn),
                              server, conn)
iDefineClass(Handler)

/*----------------------------------------------------------------------------------------------*/

static void init_Server(iServer *d, const iServerConfig *cfg, iStorage *storage) {
    d->cfg     = cfg;
    d->storage = storage;
    iZap(d->stats);
    init_Mutex(&d->stats.mtx);
    init_Mutex(&d->mtx);
    init_PtrArray(&d->handlers);
}

static void releaseHandlers_Server_(iServer *d, iBool all) {
    iPtrArray *done = new_PtrArray();
    iGuardMutex(&d->mtx, {
        iForEach(PtrArray, i, &d->handlers) {
            iHandler *handler = i.ptr;
            if (all || value_Atomic(&handler->isFinished)) {
                pushBack_PtrArray(done, handler);
                remove_PtrArrayIterator(&i);
            }
        }
    });
    iForEach(PtrArray, j, done) {
        iRelease(j.ptr);
    }
    delete_PtrArray(done);
}

static void deinit_Server(iServer *d) {
    releaseHandlers_Server_(d, iTrue);
    deinit_PtrArray(&d->handlers);
    deinit_Mutex(&d->mtx);
    deinit_Mutex(&d->stats.mtx);
}

iDefineObjectConstructionArgs(Server,
                              (const iServerConfig *cfg, iStorage *storage),
                              cfg, storage)
iDefineClass(Server)

static void accepted_Server_(iServer *d, iTlsService *sv, iTlsConnection *conn) {
    iUnused(sv);
    iHandler *handler = new_Handler(d, conn);
    iGuardMutex(&d->mtx, pushBack_PtrArray(&d->handlers, handler));
    iConnect(TlsConnection, conn, readyRead, handler, readyRead_Handler_);
    iConnect(TlsConnection, conn, disconnected, handler, disconnected_Handler_);
}

static const char *formatBytes_(uint64_t bytes) {
    if (bytes >= 1024 * 1024) {
        return format_CStr("%.1f MB", bytes / (1024.0 * 1024.0));
    }
    return format_CStr("%.1f KB", bytes / 1024.0);
}

static void printSummary_Server_(iServer *d) {
    uint64_t numRequests, bytesSent, bytesReceived;
    iGuardMutex(&d->stats.mtx, {
        numRequests   = d->stats.numRequests;
        bytesSent     = d->stats.bytesSent;
        bytesReceived = d->stats.bytesReceived;
        d->stats.numRequests   = 0;
        d->stats.bytesSent     = 0;
        d->stats.bytesReceived = 0;
    });
    uint64_t     totalSize   = 0;
    const size_t numAccounts = numAccounts_Storage(d->storage, &totalSize);
    iDate now;
    initCurrent_Date(&now);
    printf("%s Summary: %zu account%s (average %s), %llu request%s, %s sent, %s received\n",
           cstrCollect_String(format_Date(&now, "%Y-%m-%d %H:%M:%S")),
           numAccounts,
           numAccounts != 1 ? "s" : "",
           formatBytes_(numAccounts ? totalSize / numAccounts : 0),
           (unsigned long long) numRequests,
           numRequests != 1 ? "s" : "",
           formatBytes_(bytesSent),
           formatBytes_(bytesReceived));
    fflush(stdout);
}

static const iString *readFile_(const iString *path) {
    iFile *f = iClob(new_File(path));
    return open_File(f, readOnly_FileMode | text_FileMode) ? collect_String(readString_File(f))
                                                           : NULL;
}

static void stop_(int sig) {
    iUnused(sig);
    set_Atomic(&isStopping_, iTrue);
}

iBool run_Server(const iServerConfig *cfg, iStorage *storage) {
    const iString *certPem = readFile_(&cfg->certPath);
    const iString *keyPem  = readFile_(&cfg->keyPath);
    if (!certPem || !keyPem) {
        fprintf(stderr, "failed to read the server certificate\n");
        return iFalse;
    }
    iTlsCertificate *cert   = newPemKey_TlsCertificate(certPem, keyPem);
    iServer         *server = new_Server(cfg, storage);
    iTlsService     *sv     = new_TlsService(cfg->port);
    setMaxConnections_TlsService(sv, maxConnections_);
    setMaxConnectionsPerAddress_TlsService(sv, cfg->maxPerAddress);
    iConnect(TlsService, sv, accepted, server, accepted_Server_);
    iBool ok = iFalse;
    if (keyType_TlsCertificate(cert) != ed25519_TlsCertificateKeyType) {
        fprintf(stderr, "the server certificate must use an Ed25519 key\n");
    }
    else if (!setMinProtocolVersion_TlsService(sv, tls1_3_TlsProtocolVersion)) {
        fprintf(stderr, "TLS 1.3 is not supported by the TLS library\n");
    }
    else if (!setCertificate_TlsService(sv, cert)) {
        fprintf(stderr, "invalid server certificate or private key\n");
    }
    else if (!open_TlsService(sv)) {
        fprintf(stderr, "failed to listen on port %u\n", cfg->port);
    }
    else {
        printf("Listening on port %u\n", cfg->port);
        fflush(stdout);
        signal(SIGINT, stop_);
        signal(SIGTERM, stop_);
        iTime lastExpiry = { 0 };
        iTime lastSummary;
        initCurrent_Time(&lastSummary);
        while (!value_Atomic(&isStopping_)) {
            if (elapsedSeconds_Time(&lastSummary) > summaryInterval_) {
                printSummary_Server_(server);
                initCurrent_Time(&lastSummary);
            }
            if (cfg->expireDays > 0 && (!isValid_Time(&lastExpiry) ||
                                        elapsedSeconds_Time(&lastExpiry) > expiryInterval_)) {
                const size_t num = expire_Storage(storage, cfg->expireDays * 24.0 * 3600.0);
                if (num) {
                    printf("Removed %zu inactive account%s\n", num, num != 1 ? "s" : "");
                    fflush(stdout);
                }
                initCurrent_Time(&lastExpiry);
            }
            sleep_Thread(0.25);
            releaseHandlers_Server_(server, iFalse);
        }
        printf("Stopping\n");
        close_TlsService(sv);
        ok = iTrue;
    }
    iRelease(sv);
    iRelease(server);
    delete_TlsCertificate(cert);
    return ok;
}
