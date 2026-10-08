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
#include "storage.h"

#include <the_Foundation/commandline.h>
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/path.h>
#include <the_Foundation/tlsrequest.h>
#include <signal.h>
#include <stdio.h>

static const char *usage_ =
    "Usage: lgrsyncd [options]\n"
    "\n"
    "  -d, --dir DIR        Data directory (default: current directory)\n"
    "  -p, --port N         Listening port (default: 1965)\n"
    "  -h, --host NAME      Host name for a generated server certificate\n"
    "      --cert FILE      Server certificate (PEM, Ed25519); generated if missing\n"
    "      --key FILE       Server private key (PEM); generated if missing\n"
    "      --max-size N     Maximum stored data per account in bytes (default: 1048576)\n"
    "      --allow FILE     Allowed client certificate fingerprints (SHA-256, hex), one per\n"
    "                       line; if omitted, anyone with an Ed25519 certificate can sync\n";

static iBool writeTextFile_(const iString *path, const iString *text) {
    iFile *f = new_File(path);
    iBool ok = iFalse;
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        ok = write_File(f, utf8_String(text)) == size_String(text);
    }
    iRelease(f);
    return ok;
}

static iBool generateCertificate_(const iServerConfig *cfg, const iString *host) {
    iDate until;
    initCurrent_Date(&until);
    until.year += 50;
    const iTlsCertificateName names[] = {
        { issuerCommonName_TlsCertificateNameType, host },
        { subjectCommonName_TlsCertificateNameType, host },
        { 0, NULL }
    };
    iTlsCertificate *cert =
        newSelfSigned_TlsCertificate(ed25519_TlsCertificateKeyType, 0, until, names);
    if (!cert) {
        return iFalse;
    }
    const iBool ok =
        writeTextFile_(&cfg->certPath, collect_String(pem_TlsCertificate(cert))) &&
        writeTextFile_(&cfg->keyPath, collect_String(privateKeyPem_TlsCertificate(cert)));
    delete_TlsCertificate(cert);
    printf("Generated a server certificate for \"%s\"\n", cstr_String(host));
    return ok;
}

static iStringSet *loadAllowList_(const iString *path) {
    iFile *f = new_File(path);
    iStringSet *set = NULL;
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        set = new_StringSet();
        const iBlock *src = collect_Block(readAll_File(f));
        iRangecc line = iNullRange;
        while (nextSplit_Rangecc(range_Block(src), "\n", &line)) {
            iString *fp = collectNewRange_String(line);
            trim_String(fp);
            if (!isEmpty_String(fp) && !startsWith_String(fp, "#")) {
                insert_StringSet(set, collect_String(lower_String(fp)));
            }
        }
    }
    iRelease(f);
    return set;
}

static int run_(int argc, char *argv[]) {
    iCommandLine *args = iClob(new_CommandLine(argc, argv));
    iServerConfig cfg;
    initCStr_String(&cfg.dataDir, ".");
    init_String(&cfg.certPath);
    init_String(&cfg.keyPath);
    cfg.port        = 1965;
    cfg.maxDataSize = 1024 * 1024;
    iString *host     = collectNewCStr_String("localhost");
    iString *allowPath = collectNew_String();
    iCommandLineArg *arg;
    if (contains_CommandLine(args, "help")) {
        puts(usage_);
        return 0;
    }
    if ((arg = iClob(checkArgumentValues_CommandLine(args, "d;dir", 1))) != NULL) {
        set_String(&cfg.dataDir, value_CommandLineArg(arg, 0));
    }
    if ((arg = iClob(checkArgumentValues_CommandLine(args, "p;port", 1))) != NULL) {
        cfg.port = (uint16_t) toInt_String(value_CommandLineArg(arg, 0));
    }
    if ((arg = iClob(checkArgumentValues_CommandLine(args, "h;host", 1))) != NULL) {
        set_String(host, value_CommandLineArg(arg, 0));
    }
    if ((arg = iClob(checkArgumentValues_CommandLine(args, "cert", 1))) != NULL) {
        set_String(&cfg.certPath, value_CommandLineArg(arg, 0));
    }
    if ((arg = iClob(checkArgumentValues_CommandLine(args, "key", 1))) != NULL) {
        set_String(&cfg.keyPath, value_CommandLineArg(arg, 0));
    }
    if ((arg = iClob(checkArgumentValues_CommandLine(args, "max-size", 1))) != NULL) {
        cfg.maxDataSize = (size_t) strtoull(cstr_String(value_CommandLineArg(arg, 0)), NULL, 10);
    }
    if ((arg = iClob(checkArgumentValues_CommandLine(args, "allow", 1))) != NULL) {
        set_String(allowPath, value_CommandLineArg(arg, 0));
    }
    set_String(&cfg.dataDir, collect_String(makeAbsolute_Path(&cfg.dataDir)));
    makeDirs_Path(&cfg.dataDir);
    if (isEmpty_String(&cfg.certPath)) {
        set_String(&cfg.certPath, collect_String(concatCStr_Path(&cfg.dataDir, "server.crt")));
    }
    if (isEmpty_String(&cfg.keyPath)) {
        set_String(&cfg.keyPath, collect_String(concatCStr_Path(&cfg.dataDir, "server.key")));
    }
    if (!fileExists_FileInfo(&cfg.certPath) || !fileExists_FileInfo(&cfg.keyPath)) {
        if (!generateCertificate_(&cfg, host)) {
            fprintf(stderr, "failed to generate a server certificate\n");
            return 1;
        }
    }
    iStringSet *allowed = NULL;
    if (!isEmpty_String(allowPath)) {
        allowed = iClob(loadAllowList_(allowPath));
        if (!allowed) {
            fprintf(stderr, "failed to read allow list: %s\n", cstr_String(allowPath));
            return 1;
        }
    }
    printf("Lagrange Sync " LAGRANGE_SYNC_VERSION "\n\n");
    printf("  Data directory: %s\n", cstr_String(&cfg.dataDir));
    printf("  Certificate:    %s\n", cstr_String(&cfg.certPath));
    printf("  Maximum size:   %zu bytes\n", cfg.maxDataSize);
    if (allowed) {
        printf("  Allowed:        %zu certificates\n", size_StringSet(allowed));
    }
    printf("\n");
    iStorage *storage = new_Storage(
        collect_String(concatCStr_Path(&cfg.dataDir, "accounts")));
    setAllowList_Storage(storage, allowed);
    const iBool ok = run_Server(&cfg, storage);
    delete_Storage(storage);
    deinit_String(&cfg.keyPath);
    deinit_String(&cfg.certPath);
    deinit_String(&cfg.dataDir);
    return ok ? 0 : 1;
}

int main(int argc, char *argv[]) {
#if !defined (iPlatformMsys) && !defined (iPlatformWindows)
    signal(SIGPIPE, SIG_IGN); /* write errors are handled by the_Foundation */
#endif
    init_Foundation();
    const int rc = run_(argc, argv);
    deinit_Foundation();
    return rc;
}
