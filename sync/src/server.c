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
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/thread.h>
#include <the_Foundation/time.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

enum { maxHeaderSize_ = 1024, maxConnections_ = 32, ioTimeoutSeconds_ = 20 };

static const char *dataPath_ = "/data";

iDeclareType(Connection)

struct Impl_Connection {
    const iServerConfig *cfg;
    iStorage            *storage;
    SSL                 *ssl;
    int                  fd;
    iString              remoteAddr;
    iString              fingerprint; /* client certificate; empty if none */
    iBool                isEd25519;
    iBlock               received;    /* header and possibly the beginning of the body */
};

static iAtomicInt numConnections_;

static void log_Connection_(const iConnection *d, const char *format, ...) {
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

static iBool write_Connection_(iConnection *d, const void *data, size_t size) {
    const char *ptr = data;
    while (size > 0) {
        const int n = SSL_write(d->ssl, ptr, (int) iMin(size, 0x10000));
        if (n <= 0) {
            return iFalse;
        }
        ptr += n;
        size -= n;
    }
    return iTrue;
}

static void respond_Connection_(iConnection *d, int status, const char *meta,
                                const iBlock *body) {
    iString *header = collectNewFormat_String("%02d %s\r\n", status, meta);
    write_Connection_(d, cstr_String(header), size_String(header));
    if (body) {
        write_Connection_(d, constData_Block(body), size_Block(body));
    }
    log_Connection_(d, "%02d %s%s", status, meta,
                    body ? cstrCollect_String(newFormat_String(" (%zu bytes)", size_Block(body)))
                         : "");
}

static iBool readMore_Connection_(iConnection *d) {
    char buf[0x4000];
    const int n = SSL_read(d->ssl, buf, sizeof(buf));
    if (n <= 0) {
        return iFalse;
    }
    appendData_Block(&d->received, buf, n);
    return iTrue;
}

static iBool readHeader_Connection_(iConnection *d, iString *header_out) {
    for (;;) {
        const char *start = constData_Block(&d->received);
        const char *crlf  = NULL;
        for (size_t i = 0; i + 1 < size_Block(&d->received); i++) {
            if (start[i] == '\r' && start[i + 1] == '\n') {
                crlf = start + i;
                break;
            }
        }
        if (crlf) {
            setRange_String(header_out, (iRangecc){ start, crlf });
            remove_Block(&d->received, 0, crlf + 2 - start);
            return iTrue;
        }
        if (size_Block(&d->received) > maxHeaderSize_ || !readMore_Connection_(d)) {
            return iFalse;
        }
    }
}

static void checkClientCertificate_Connection_(iConnection *d) {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    X509 *peer = SSL_get1_peer_certificate(d->ssl);
#else
    X509 *peer = SSL_get_peer_certificate(d->ssl);
#endif
    if (!peer) {
        return;
    }
    uint8_t *der = NULL;
    const int len = i2d_X509(peer, &der);
    if (len > 0) {
        uint8_t sha[SHA256_DIGEST_LENGTH];
        SHA256(der, len, sha);
        iBlock *digest = newData_Block(sha, sizeof(sha));
        iString *hex = hexEncode_Block(digest);
        set_String(&d->fingerprint, hex);
        delete_String(hex);
        delete_Block(digest);
        OPENSSL_free(der);
    }
    d->isEd25519 = EVP_PKEY_base_id(X509_get0_pubkey(peer)) == EVP_PKEY_ED25519;
    X509_free(peer);
}

iDeclareType(RequestUrl)

struct Impl_RequestUrl {
    iRangecc scheme;
    iRangecc path;
    iRangecc query; /* includes the "?" */
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
    return iTrue;
}

static iBool parseGeneration_(iRangecc query, uint64_t *gen_out) {
    /* Query is just the generation number. */
    if (size_Range(&query) < 2 || query.start[0] != '?') {
        return iFalse;
    }
    char *end = NULL;
    *gen_out = strtoull(query.start + 1, &end, 10);
    return end == query.end;
}

static void handleInfo_Connection_(iConnection *d) {
    iString *page = collectNewCStr_String("# Lagrange Sync\n\n");
    if (isEmpty_String(&d->fingerprint)) {
        appendCStr_String(page, "No client certificate.\n");
    }
    else {
        appendFormat_String(page, "Certificate fingerprint: %s\n", cstr_String(&d->fingerprint));
        if (!d->isEd25519) {
            appendCStr_String(page, "This is not an Ed25519 certificate; it cannot be used for syncing.\n");
        }
        else if (!isAllowed_Storage(d->storage, &d->fingerprint)) {
            appendCStr_String(page, "This certificate is not authorized for syncing.\n");
        }
        else {
            uint64_t gen = 0;
            iBlock *data = load_Storage(d->storage, &d->fingerprint, &gen);
            if (data) {
                appendFormat_String(page, "Stored: %zu bytes (generation %llu)\n",
                                    size_Block(data), (unsigned long long) gen);
                delete_Block(data);
            }
            else {
                appendCStr_String(page, "No data stored.\n");
            }
        }
    }
    respond_Connection_(d, 20, "text/gemini; charset=utf-8", utf8_String(page));
}

static void handleDownload_Connection_(iConnection *d, uint64_t clientGen) {
    uint64_t gen  = 0;
    iBlock  *data = load_Storage(d->storage, &d->fingerprint, &gen);
    if (gen == clientGen) {
        respond_Connection_(d, 40, "unchanged", NULL);
    }
    else {
        respond_Connection_(d,
                            20,
                            format_CStr("application/octet-stream; gen=%llu",
                                        (unsigned long long) gen),
                            data ? data : collect_Block(new_Block(0)));
    }
    delete_Block(data);
}

static void handleUpload_Connection_(iConnection *d, iRangecc params, uint64_t baseGen) {
    /* Titan parameters: ;size=N;mime=...;token=... */
    size_t   size  = 0;
    iBool    hasSize = iFalse;
    iRangecc param = iNullRange;
    while (nextSplit_Rangecc(params, ";", &param)) {
        if (startsWith_Rangecc(param, "size=")) {
            char *end = NULL;
            size = strtoull(param.start + 5, &end, 10);
            hasSize = (end == param.end);
        }
    }
    if (!hasSize) {
        respond_Connection_(d, 59, "Missing size parameter", NULL);
        return;
    }
    if (size > d->cfg->maxDataSize) {
        respond_Connection_(d, 59, format_CStr("Data too large (maximum is %zu bytes)",
                                               d->cfg->maxDataSize), NULL);
        return;
    }
    uint64_t current = 0;
    delete_Block(load_Storage(d->storage, &d->fingerprint, &current));
    if (current != baseGen) {
        respond_Connection_(d, 40, "stale", NULL);
        return;
    }
    while (size_Block(&d->received) < size) {
        if (!readMore_Connection_(d)) {
            log_Connection_(d, "upload interrupted");
            return;
        }
    }
    truncate_Block(&d->received, size);
    uint64_t newGen = 0;
    switch (store_Storage(d->storage, &d->fingerprint, baseGen, &d->received, &newGen)) {
        case ok_StoreResult:
            respond_Connection_(d, 20, format_CStr("text/plain; gen=%llu",
                                                   (unsigned long long) newGen), NULL);
            break;
        case stale_StoreResult:
            respond_Connection_(d, 40, "stale", NULL);
            break;
        default:
            respond_Connection_(d, 42, "Failed to store data", NULL);
            break;
    }
}

static void handle_Connection_(iConnection *d) {
    iString *header = collectNew_String();
    if (!readHeader_Connection_(d, header)) {
        log_Connection_(d, "bad request");
        respond_Connection_(d, 59, "Bad request", NULL);
        return;
    }
    log_Connection_(d, "%s", cstr_String(header));
    iRequestUrl url;
    if (!parse_RequestUrl_(&url, range_String(header))) {
        respond_Connection_(d, 59, "Bad request", NULL);
        return;
    }
    const iBool isTitan = equalCase_Rangecc(url.scheme, "titan");
    if (!isTitan && !equalCase_Rangecc(url.scheme, "gemini")) {
        respond_Connection_(d, 53, "Unsupported scheme", NULL);
        return;
    }
    /* Titan parameters are part of the path. */
    iRangecc path   = url.path;
    iRangecc params = iNullRange;
    const char *semi = isEmpty_Range(&path) ? NULL : memchr(path.start, ';', size_Range(&path));
    if (semi) {
        params = (iRangecc){ semi, path.end };
        path.end = semi;
    }
    if (!isTitan && (isEmpty_Range(&path) || equal_Rangecc(path, "/"))) {
        handleInfo_Connection_(d);
        return;
    }
    if (!endsWith_Rangecc(path, dataPath_)) {
        respond_Connection_(d, 51, "Not found", NULL);
        return;
    }
    if (isEmpty_String(&d->fingerprint)) {
        respond_Connection_(d, 60, "Client certificate required", NULL);
        return;
    }
    if (!d->isEd25519) {
        respond_Connection_(d, 62, "Ed25519 certificate required", NULL);
        return;
    }
    if (!isAllowed_Storage(d->storage, &d->fingerprint)) {
        respond_Connection_(d, 61, "Certificate not authorized", NULL);
        return;
    }
    uint64_t gen = 0;
    if (!parseGeneration_(url.query, &gen)) {
        respond_Connection_(d, 59, "Generation missing", NULL);
        return;
    }
    if (isTitan) {
        handleUpload_Connection_(d, params, gen);
    }
    else {
        handleDownload_Connection_(d, gen);
    }
}

static iThreadResult run_Connection_(iThread *thread) {
    iConnection *d = userData_Thread(thread);
    if (SSL_accept(d->ssl) == 1) {
        checkClientCertificate_Connection_(d);
        handle_Connection_(d);
        SSL_shutdown(d->ssl);
    }
    else {
        log_Connection_(d, "TLS handshake failed");
    }
    SSL_free(d->ssl);
    close(d->fd);
    deinit_Block(&d->received);
    deinit_String(&d->fingerprint);
    deinit_String(&d->remoteAddr);
    free(d);
    add_Atomic(&numConnections_, -1);
    return 0;
}

static int acceptAnyCertificate_(int preverifyOk, X509_STORE_CTX *ctx) {
    iUnused(preverifyOk, ctx);
    return 1; /* clients use self-signed certificates */
}

static SSL_CTX *makeContext_(const iServerConfig *cfg) {
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    if (SSL_CTX_use_certificate_chain_file(ctx, cstr_String(&cfg->certPath)) != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx, cstr_String(&cfg->keyPath), SSL_FILETYPE_PEM) != 1) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, acceptAnyCertificate_);
    static const unsigned char sessionContext[] = "lagrange-sync";
    SSL_CTX_set_session_id_context(ctx, sessionContext, sizeof(sessionContext) - 1);
    return ctx;
}

iBool run_Server(const iServerConfig *cfg, iStorage *storage) {
    signal(SIGPIPE, SIG_IGN);
    SSL_CTX *ctx = makeContext_(cfg);
    if (!ctx) {
        fprintf(stderr, "failed to set up TLS\n");
        return iFalse;
    }
    const int sock = socket(AF_INET6, SOCK_STREAM, 0);
    const int yes = 1, no = 0;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no));
    struct sockaddr_in6 addr;
    iZap(addr);
    addr.sin6_family = AF_INET6;
    addr.sin6_addr   = in6addr_any;
    addr.sin6_port   = htons(cfg->port);
    if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) != 0 || listen(sock, 16) != 0) {
        perror("failed to listen");
        close(sock);
        SSL_CTX_free(ctx);
        return iFalse;
    }
    printf("Listening on port %u\n", cfg->port);
    fflush(stdout);
    iPtrArray *threads = new_PtrArray();
    for (;;) {
        struct sockaddr_in6 remote;
        socklen_t remoteLen = sizeof(remote);
        const int fd = accept(sock, (struct sockaddr *) &remote, &remoteLen);
        /* Release finished threads. */
        iForEach(PtrArray, i, threads) {
            if (isFinished_Thread(i.ptr)) {
                join_Thread(i.ptr);
                iRelease(i.ptr);
                remove_PtrArrayIterator(&i);
            }
        }
        if (fd < 0) {
            continue;
        }
        if (value_Atomic(&numConnections_) >= maxConnections_) {
            close(fd);
            continue;
        }
        const struct timeval timeout = { ioTimeoutSeconds_, 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        iConnection *conn = calloc(1, sizeof(iConnection));
        conn->cfg     = cfg;
        conn->storage = storage;
        conn->fd      = fd;
        conn->ssl     = SSL_new(ctx);
        SSL_set_fd(conn->ssl, fd);
        init_String(&conn->fingerprint);
        init_Block(&conn->received, 0);
        char addrStr[INET6_ADDRSTRLEN] = "";
        inet_ntop(AF_INET6, &remote.sin6_addr, addrStr, sizeof(addrStr));
        initCStr_String(&conn->remoteAddr, addrStr);
        add_Atomic(&numConnections_, 1);
        iThread *thread = new_Thread(run_Connection_);
        setUserData_Thread(thread, conn);
        pushBack_PtrArray(threads, thread);
        start_Thread(thread);
    }
    return iTrue;
}
