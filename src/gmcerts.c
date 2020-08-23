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

#include "gmcerts.h"

#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/mutex.h>
#include <the_Foundation/path.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/stringarray.h>
#include <the_Foundation/stringhash.h>
#include <the_Foundation/stringlist.h>
#include <the_Foundation/time.h>
#include <ctype.h>

static const char *filename_GmCerts_       = "trusted.txt";
static const char *identsDir_GmCerts_      = "idents";
static const char *identsFilename_GmCerts_ = "idents.binary";

iDeclareClass(TrustEntry)

struct Impl_TrustEntry {
    iObject object;
    iBlock fingerprint;
    iTime validUntil;
};

void init_TrustEntry(iTrustEntry *d, const iBlock *fingerprint, const iDate *until) {
    initCopy_Block(&d->fingerprint, fingerprint);
    init_Time(&d->validUntil, until);
}

void deinit_TrustEntry(iTrustEntry *d) {
    deinit_Block(&d->fingerprint);
}

iDefineObjectConstructionArgs(TrustEntry,
                              (const iBlock *fingerprint, const iDate *until),
                              fingerprint, until)
iDefineClass(TrustEntry)

/*----------------------------------------------------------------------------------------------*/

static int cmpUrl_GmIdentity_(const void *a, const void *b) {
    return cmpStringCase_String((const iString *) a, (const iString *) b);
}

void init_GmIdentity(iGmIdentity *d) {
    d->icon  = 0x1f511; /* key */
    d->flags = 0;
    d->cert  = new_TlsCertificate();
//    init_String(&d->fileName);
    init_Block(&d->fingerprint, 0);
    init_SortedArray(&d->useUrls, sizeof(iString), cmpUrl_GmIdentity_);
    init_String(&d->notes);
}

static void clear_GmIdentity_(iGmIdentity *d) {
    iForEach(Array, i, &d->useUrls.values) {
        deinit_String(i.value);
    }
    clear_SortedArray(&d->useUrls);
}

void deinit_GmIdentity(iGmIdentity *d) {
    clear_GmIdentity_(d);
    deinit_String(&d->notes);
    deinit_SortedArray(&d->useUrls);
    delete_TlsCertificate(d->cert);
    deinit_Block(&d->fingerprint);
//    deinit_String(&d->fileName);
}

void serialize_GmIdentity(const iGmIdentity *d, iStream *outs) {
//    serialize_String(&d->fileName, outs);
    serialize_Block(&d->fingerprint, outs);
    writeU32_Stream(outs, d->icon);
    serialize_String(&d->notes, outs);
    write32_Stream(outs, d->flags);
    writeU32_Stream(outs, size_SortedArray(&d->useUrls));
    iConstForEach(Array, i, &d->useUrls.values) {
        serialize_String(i.value, outs);
    }
}

void deserialize_GmIdentity(iGmIdentity *d, iStream *ins) {
//    deserialize_String(&d->fileName, ins);
    deserialize_Block(&d->fingerprint, ins);
    d->icon = readU32_Stream(ins);
    deserialize_String(&d->notes, ins);
    d->flags = read32_Stream(ins);
    size_t n = readU32_Stream(ins);
    while (n-- && !atEnd_Stream(ins)) {
        iString url;
        init_String(&url);
        deserialize_String(&url, ins);
        insert_SortedArray(&d->useUrls, &url);
    }
}

static iBool isValid_GmIdentity_(const iGmIdentity *d) {
    return !isEmpty_TlsCertificate(d->cert);
}

static void setCertificate_GmIdentity_(iGmIdentity *d, iTlsCertificate *cert) {
    delete_TlsCertificate(d->cert);
    d->cert = cert;
}

static const iString *readFile_(const iString *path) {
    iString *str = NULL;
    iFile *f = new_File(path);
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        str = readString_File(f);
    }
    iRelease(f);
    return str ? collect_String(str) : collectNew_String();
}

static iBool writeTextFile_(const iString *path, const iString *content) {
    iFile *f = iClob(new_File(path));
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        write_File(f, &content->chars);
        close_File(f);
        return iTrue;
    }
    return iFalse;
}

iDefineTypeConstruction(GmIdentity)

/*-----------------------------------------------------------------------------------------------*/

struct Impl_GmCerts {
    iMutex mtx;
    iString saveDir;
    iStringHash *trusted;
    iPtrArray idents;
};

static const char *magicIdMeta_GmCerts_   = "lgL2";
static const char *magicIdentity_GmCerts_ = "iden";

iDefineTypeConstructionArgs(GmCerts, (const char *saveDir), saveDir)

static void saveIdentities_GmCerts_(const iGmCerts *d) {
    iFile *f = new_File(collect_String(concatCStr_Path(&d->saveDir, identsFilename_GmCerts_)));
    if (open_File(f, writeOnly_FileMode)) {
        writeData_File(f, magicIdMeta_GmCerts_, 4);
        writeU32_File(f, 0); /* version */
        iConstForEach(PtrArray, i, &d->idents) {
            const iGmIdentity *ident = i.ptr;
            if (~ident->flags & temporary_GmIdentityFlag) {
                writeData_File(f, magicIdentity_GmCerts_, 4);
                serialize_GmIdentity(ident, stream_File(f));
            }
        }
    }
    iRelease(f);
}

static void save_GmCerts_(const iGmCerts *d) {
    iBeginCollect();
    iFile *f = new_File(collect_String(concatCStr_Path(&d->saveDir, filename_GmCerts_)));
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        iString line;
        init_String(&line);
        iConstForEach(StringHash, i, d->trusted) {
            const iTrustEntry *trust = value_StringHashNode(i.value);
            format_String(&line,
                          "%s %ld %s\n",
                          cstr_String(key_StringHashConstIterator(&i)),
                          integralSeconds_Time(&trust->validUntil),
                          cstrCollect_String(hexEncode_Block(&trust->fingerprint)));
            write_File(f, &line.chars);
        }
        deinit_String(&line);
    }
    iRelease(f);
    iEndCollect();
}

static void loadIdentities_GmCerts_(iGmCerts *d) {
    iFile *f =
        iClob(new_File(collect_String(concatCStr_Path(&d->saveDir, identsFilename_GmCerts_))));
    if (open_File(f, readOnly_FileMode)) {
        char magic[4];
        readData_File(f, sizeof(magic), magic);
        if (memcmp(magic, magicIdMeta_GmCerts_, sizeof(magic))) {
            printf("%s: format not recognized\n", cstr_String(path_File(f)));
            return;
        }
        setVersion_Stream(stream_File(f), readU32_File(f));
        while (!atEnd_File(f)) {
            readData_File(f, sizeof(magic), magic);
            if (!memcmp(magic, magicIdentity_GmCerts_, sizeof(magic))) {
                iGmIdentity *id = new_GmIdentity();
                deserialize_GmIdentity(id, stream_File(f));
                pushBack_PtrArray(&d->idents, id);
            }
            else {
                printf("%s: invalid file contents\n", cstr_String(path_File(f)));
                break;
            }
        }
    }
}

static iGmIdentity *findIdentity_GmCerts_(iGmCerts *d, const iBlock *fingerprint) {
    iForEach(PtrArray, i, &d->idents) {
        iGmIdentity *ident = i.ptr;
        if (cmp_Block(fingerprint, &ident->fingerprint) == 0) { /* TODO: could use a hash */
            return ident;
        }
    }
    return NULL;
}

static void loadIdentityFromCertificate_GmCerts_(iGmCerts *d, const iString *crtPath) {
    iAssert(fileExists_FileInfo(crtPath));
    iString *keyPath = collect_String(copy_String(crtPath));
    truncate_Block(&keyPath->chars, size_String(keyPath) - 3);
    appendCStr_String(keyPath, "key");
    if (!fileExists_FileInfo(keyPath)) {
        return;
    }
    iTlsCertificate *cert = newPemKey_TlsCertificate(readFile_(crtPath), readFile_(keyPath));
    iBlock *finger = fingerprint_TlsCertificate(cert);
    iGmIdentity *ident = findIdentity_GmCerts_(d, finger);
    if (!ident) {
        ident = new_GmIdentity();
        set_Block(&ident->fingerprint, finger);
        iDate today;
        initCurrent_Date(&today);
        set_String(&ident->notes, collect_String(format_Date(&today, "Imported on %b %d, %Y")));
    }
    setCertificate_GmIdentity_(ident, cert);
    delete_Block(finger);
}

static void load_GmCerts_(iGmCerts *d) {
    iFile *f = new_File(collect_String(concatCStr_Path(&d->saveDir, filename_GmCerts_)));
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        iRegExp *      pattern = new_RegExp("([^\\s]+) ([0-9]+) ([a-z0-9]+)", 0);
        const iRangecc src     = range_Block(collect_Block(readAll_File(f)));
        iRangecc       line    = iNullRange;
        while (nextSplit_Rangecc(&src, "\n", &line)) {
            iRegExpMatch m;
            if (matchRange_RegExp(pattern, line, &m)) {
                const iRangecc domain = capturedRange_RegExpMatch(&m, 1);
                const iRangecc until  = capturedRange_RegExpMatch(&m, 2);
                const iRangecc fp     = capturedRange_RegExpMatch(&m, 3);
                time_t sec;
                sscanf(until.start, "%ld", &sec);
                iDate untilDate;
                initSinceEpoch_Date(&untilDate, sec);
                insert_StringHash(d->trusted,
                                  collect_String(newRange_String(domain)),
                                  new_TrustEntry(collect_Block(hexDecode_Rangecc(fp)),
                                                 &untilDate));
            }
        }
        iRelease(pattern);
    }
    iRelease(f);
    /* Load all identity certificates. */ {
        loadIdentities_GmCerts_(d);
        const iString *idDir = collect_String(concatCStr_Path(&d->saveDir, identsDir_GmCerts_));
        if (!fileExists_FileInfo(idDir)) {
            mkdir_Path(idDir);
        }
        iForEach(DirFileInfo, i, iClob(directoryContents_FileInfo(iClob(new_FileInfo(idDir))))) {
            const iFileInfo *entry = i.value;
            if (endsWithCase_String(path_FileInfo(entry), ".crt")) {
                loadIdentityFromCertificate_GmCerts_(d, path_FileInfo(entry));
            }
        }
        /* Remove certificates whose crt/key files were missing. */
        iForEach(PtrArray, j, &d->idents) {
            iGmIdentity *ident = j.ptr;
            if (!isValid_GmIdentity_(ident)) {
                delete_GmIdentity(ident);
                remove_PtrArrayIterator(&j);
            }
        }
    }
}

void init_GmCerts(iGmCerts *d, const char *saveDir) {
    init_Mutex(&d->mtx);
    initCStr_String(&d->saveDir, saveDir);
    d->trusted = new_StringHash();
    init_PtrArray(&d->idents);
    load_GmCerts_(d);
}

void deinit_GmCerts(iGmCerts *d) {
    iGuardMutex(&d->mtx, {
        saveIdentities_GmCerts_(d);
        iForEach(PtrArray, i, &d->idents) {
            delete_GmIdentity(i.ptr);
        }
        deinit_PtrArray(&d->idents);
        iRelease(d->trusted);
        deinit_String(&d->saveDir);
    });
    deinit_Mutex(&d->mtx);
}

iBool checkTrust_GmCerts(iGmCerts *d, iRangecc domain, const iTlsCertificate *cert) {
    if (!cert) {
        return iFalse;
    }
    if (isExpired_TlsCertificate(cert)) {
        return iFalse;
    }
    if (!verifyDomain_TlsCertificate(cert, domain)) {
        return iFalse;
    }
    /* Good certificate. If not already trusted, add it now. */
    const iString *key = collect_String(newRange_String(domain));
    iDate until;
    validUntil_TlsCertificate(cert, &until);
    iBlock *fingerprint = collect_Block(fingerprint_TlsCertificate(cert));
    lock_Mutex(&d->mtx);
    iTrustEntry *trust = value_StringHash(d->trusted, key);
    if (trust) {
        /* We already have it, check if it matches the one we trust for this domain (if it's
           still valid. */
        iTime now;
        initCurrent_Time(&now);
        if (secondsSince_Time(&trust->validUntil, &now) > 0) {
            /* Trusted cert is still valid. */
            const iBool isTrusted = cmp_Block(fingerprint, &trust->fingerprint) == 0;
            unlock_Mutex(&d->mtx);
            return isTrusted;
        }
        /* Update the trusted cert. */
        init_Time(&trust->validUntil, &until);
        set_Block(&trust->fingerprint, fingerprint);
    }
    else {
        insert_StringHash(d->trusted, key, iClob(new_TrustEntry(fingerprint, &until)));
    }
    save_GmCerts_(d);
    unlock_Mutex(&d->mtx);
    return iTrue;
}

const iGmIdentity *identityForUrl_GmCerts(const iGmCerts *d, const iString *url) {
    iConstForEach(PtrArray, i, &d->idents) {
        const iGmIdentity *ident = i.ptr;
        iConstForEach(Array, j, &ident->useUrls.values) {
            const iString *used = j.value;
            if (startsWithCase_String(url, cstr_String(used))) {
                return ident;
            }
        }
    }
    return NULL;
}

iGmIdentity *newIdentity_GmCerts(iGmCerts *d, int flags, iDate validUntil, const iString *commonName,
                                 const iString *userId, const iString *org,
                                 const iString *country) {
    const iTlsCertificateName names[] = {
        { issuerCommonName_TlsCertificateNameType,    collectNewCStr_String("fi.skyjake.Lagrange") },
        { subjectCommonName_TlsCertificateNameType,   commonName },
        { subjectUserId_TlsCertificateNameType,       userId },
        { subjectOrganization_TlsCertificateNameType, org },
        { subjectCountry_TlsCertificateNameType,      country },
        { 0, NULL }
    };
    iGmIdentity *id = new_GmIdentity();
    setCertificate_GmIdentity_(id, newSelfSignedRSA_TlsCertificate(2048, validUntil, names));
    /* Save the certificate and private key as PEM files. */
    if (~flags & temporary_GmIdentityFlag) {
        const char *finger = cstrCollect_String(hexEncode_Block(&id->fingerprint));
        if (!writeTextFile_(
                collect_String(concatCStr_Path(&d->saveDir, format_CStr("%s.crt", finger))),
                collect_String(pem_TlsCertificate(id->cert)))) {
            delete_GmIdentity(id);
            return NULL;
        }
        if (!writeTextFile_(
                collect_String(concatCStr_Path(&d->saveDir, format_CStr("%s.key", finger))),
                collect_String(privateKeyPem_TlsCertificate(id->cert)))) {
            delete_GmIdentity(id);
            return NULL;
        }
    }
    return id;
}
