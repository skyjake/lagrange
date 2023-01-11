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
#include "gmutil.h"
#include "defs.h"
#include "app.h"

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

static const char *trustedFilename_GmCerts_   = "trusted.2.txt";
static const char *identsDir_GmCerts_         = "idents";
static const char *oldIdentsFilename_GmCerts_ = "idents.binary";
static const char *identsFilename_GmCerts_    = "idents.lgr";
static const char *tempIdentsFilename_GmCerts_= "idents.lgr.tmp";

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

static int cmpUrl_GmIdentity_(const iString *a, const iString *b) {
    return cmpStringCase_String(a, b);
}

void init_GmIdentity(iGmIdentity *d) {
    d->icon  = 0x1f511; /* key */
    d->flags = 0;
    d->cert  = new_TlsCertificate();
    init_Block(&d->fingerprint, 0);
    d->useUrls = newCmp_StringSet(cmpUrl_GmIdentity_);
    init_String(&d->notes);
}

void deinit_GmIdentity(iGmIdentity *d) {
    iRelease(d->useUrls);
    deinit_String(&d->notes);
    delete_TlsCertificate(d->cert);
    deinit_Block(&d->fingerprint);
}

void serialize_GmIdentity(const iGmIdentity *d, iStream *outs) {
    serialize_Block(&d->fingerprint, outs);
    writeU32_Stream(outs, d->icon);
    serialize_String(&d->notes, outs);
    write32_Stream(outs, d->flags);
    writeU32_Stream(outs, (uint32_t) size_StringSet(d->useUrls));
    iConstForEach(StringSet, i, d->useUrls) {
        serialize_String(i.value, outs);
    }
}

void deserialize_GmIdentity(iGmIdentity *d, iStream *ins) {
    deserialize_Block(&d->fingerprint, ins);
    d->icon = readU32_Stream(ins);
    deserialize_String(&d->notes, ins);
    d->flags = read32_Stream(ins);
    size_t n = readU32_Stream(ins);
    while (n-- && !atEnd_Stream(ins)) {
        iString url;
        init_String(&url);
        deserialize_String(&url, ins);
        setUse_GmIdentity(d, &url, iTrue);
        deinit_String(&url);
    }
}

static iBool isValid_GmIdentity_(const iGmIdentity *d) {
    return !isEmpty_TlsCertificate(d->cert);
}

static void setCertificate_GmIdentity_(iGmIdentity *d, iTlsCertificate *cert) {
    delete_TlsCertificate(d->cert);
    d->cert = cert;
    set_Block(&d->fingerprint, collect_Block(fingerprint_TlsCertificate(cert)));
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

iBool isUsed_GmIdentity(const iGmIdentity *d) {
    return d && !isEmpty_StringSet(d->useUrls);
}

iBool isUsedOn_GmIdentity(const iGmIdentity *d, const iString *url) {
#if 0
    size_t pos = iInvalidPos;
    locate_StringSet(d->useUrls, url, &pos);
    if (pos < size_StringSet(d->useUrls)) {
        if (!cmpStringCase_String(url, constAt_StringSet(d->useUrls, pos))) {
            return iTrue;
        }
    }
    if (pos > 0) {
        /* URLs with a longer path will be following the shorter URL(s). */
        if (startsWithCase_String(url, cstr_String(constAt_StringSet(d->useUrls, pos - 1)))) {
            return iTrue;
        }
    }
#endif
    iConstForEach(StringSet, i, d->useUrls) {
        if (startsWithCase_String(url, cstr_String(i.value))) {
            return iTrue;
        }
    }
    return iFalse;
}

iBool isUsedOnDomain_GmIdentity(const iGmIdentity *d, const iRangecc domain) {
    iConstForEach(StringSet, i, d->useUrls) {
        const iRangecc host = urlHost_String(i.value);
        if (equalRangeCase_Rangecc(host, domain)) {
            return iTrue;
        }
    }
    return iFalse;
}

void setUse_GmIdentity(iGmIdentity *d, const iString *url, iBool use) {
    if (use && isUsedOn_GmIdentity(d, url)) {
        return; /* Redudant. */
    }
    if (use) {
        /* Remove all use-URLs that become redundant by this newly added URL. */
        /* TODO: StringSet could have a non-const iterator. */
        iForEach(Array, i, &d->useUrls->strings.values) {
            iString *used = i.value;
            if (startsWithCase_String(used, cstr_String(url))) {
                deinit_String(used);
                remove_ArrayIterator(&i);
            }
        }
#if !defined (NDEBUG)
        const iBool wasInserted =
#endif
        insert_StringSet(d->useUrls, url);
        iAssert(wasInserted);
    }
    else {
        iForEach(Array, i, &d->useUrls->strings.values) {
            iString *used = i.value;
            if (startsWithCase_String(url, cstr_String(used))) {
                deinit_String(used);
                remove_ArrayIterator(&i);
            }
        }        
    }
}

void clearUse_GmIdentity(iGmIdentity *d) {
    clear_StringSet(d->useUrls);
}

const iString *findUse_GmIdentity(const iGmIdentity *d, const iString *url) {
    if (!d) return NULL;
    iConstForEach(StringSet, using, d->useUrls) {
        if (startsWith_String(url, cstr_String(using.value))) {
            return using.value;
        }
    }
    return NULL;
}

const iString *name_GmIdentity(const iGmIdentity *d) {
    iString *name = collect_String(subject_TlsCertificate(d->cert));
    if (startsWith_String(name, "CN = ")) {
        remove_Block(&name->chars, 0, 5);
    }
    return name;
}

iDefineTypeConstruction(GmIdentity)

/*-----------------------------------------------------------------------------------------------*/

struct Impl_GmCerts {
    iMutex *mtx;
    iString saveDir;
    iStringHash *trusted;
    iPtrArray idents;
};

static const char *magicIdMeta_GmCerts_   = "lgL2";
static const char *magicIdentity_GmCerts_ = "iden";

iDefineTypeConstructionArgs(GmCerts, (const char *saveDir), saveDir)

void serialize_GmCerts(const iGmCerts *d, iStream *trusted, iStream *identsMeta) {
    if (trusted) {
        iString line;
        init_String(&line);
        iConstForEach(StringHash, i, d->trusted) {
            const iTrustEntry *trust = value_StringHashNode(i.value);
            format_String(&line,
                          "%s %llu %s\n",
                          cstr_String(key_StringHashConstIterator(&i)),
                          (unsigned long long) integralSeconds_Time(&trust->validUntil),
                          cstrCollect_String(hexEncode_Block(&trust->fingerprint)));
            write_Stream(trusted, &line.chars);
        }
        deinit_String(&line);        
    }
    if (identsMeta) {
        writeData_Stream(identsMeta, magicIdMeta_GmCerts_, 4);
        writeU32_Stream(identsMeta, idents_FileVersion); /* version */
        iConstForEach(PtrArray, i, &d->idents) {
            const iGmIdentity *ident = i.ptr;
            if (~ident->flags & temporary_GmIdentityFlag) {
                writeData_Stream(identsMeta, magicIdentity_GmCerts_, 4);
                serialize_GmIdentity(ident, identsMeta);
            }
        }        
    }
}

void saveIdentities_GmCerts(const iGmCerts *d) {
    const iString *tempPath = collect_String(
            concatCStr_Path(&d->saveDir, tempIdentsFilename_GmCerts_));
    iFile *f = new_File(tempPath);
    if (open_File(f, writeOnly_FileMode)) {
        serialize_GmCerts(d, NULL, stream_File(f));
    }
    iRelease(f);
    commitFile_App(cstrCollect_String(concatCStr_Path(&d->saveDir, identsFilename_GmCerts_)),
                   cstr_String(tempPath));
}

static void save_GmCerts_(const iGmCerts *d) {
    iBeginCollect();
    iFile *f = new_File(collect_String(concatCStr_Path(&d->saveDir, trustedFilename_GmCerts_)));
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        serialize_GmCerts(d, stream_File(f), NULL);        
    }
    iRelease(f);
    iEndCollect();
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
    iGmIdentity *ident = findIdentity_GmCerts(d, finger);
    if (!ident) {
        /* User-provided certificate. */
        ident = new_GmIdentity();
        ident->flags |= imported_GmIdentityFlag;
        iDate today;
        initCurrent_Date(&today);
        set_String(&ident->notes, collect_String(format_Date(&today, "Imported on %b %d, %Y")));
        pushBack_PtrArray(&d->idents, ident);
    }
    setCertificate_GmIdentity_(ident, cert);
    delete_Block(finger);
}

static void loadIdentityCertsAndDiscardInvalid_GmCerts_(iGmCerts *d) {
    const iString *idDir = collect_String(concatCStr_Path(&d->saveDir, identsDir_GmCerts_));
    if (!fileExists_FileInfo(idDir)) {
        makeDirs_Path(idDir);
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

iBool deserializeIdentities_GmCerts(iGmCerts *d, iStream *ins, enum iImportMethod method) {
    char magic[4];
    readData_Stream(ins, sizeof(magic), magic);
    if (memcmp(magic, magicIdMeta_GmCerts_, sizeof(magic))) {
        fprintf(stderr, "[GmCerts] idents file format not recognized\n");
        return iFalse;
    }
    const uint32_t version = readU32_Stream(ins);
    if (version > latest_FileVersion) {
        fprintf(stderr, "[GmCerts] unsupported version (%u)\n", version);
        return iFalse;
    }
    setVersion_Stream(ins, version);
    while (!atEnd_Stream(ins)) {
        readData_Stream(ins, sizeof(magic), magic);
        if (!memcmp(magic, magicIdentity_GmCerts_, sizeof(magic))) {
            iGmIdentity *id = new_GmIdentity();
            deserialize_GmIdentity(id, ins);
            if (method == all_ImportMethod ||
                (method == ifMissing_ImportMethod && !findIdentity_GmCerts(d, &id->fingerprint))) {
                pushBack_PtrArray(&d->idents, id);
            }
            else {
                delete_GmIdentity(id);
            }
        }
        else {
            fprintf(stderr, "[GmCerts] invalid idents file\n");
            return iFalse;
        }
    }
    loadIdentityCertsAndDiscardInvalid_GmCerts_(d);
    return iTrue;
}

static void loadIdentities_GmCerts_(iGmCerts *d) {
    const iString *oldPath = collect_String(concatCStr_Path(&d->saveDir, oldIdentsFilename_GmCerts_));
    const iString *path    = collect_String(concatCStr_Path(&d->saveDir, identsFilename_GmCerts_));
    iFile *f = iClob(new_File(fileExists_FileInfo(path) ? path : oldPath));
    if (open_File(f, readOnly_FileMode)) {
        deserializeIdentities_GmCerts(d, stream_File(f), all_ImportMethod);
    }
    else {
        /* In any case, load any .crt/.key files that may be present in the "idents" dir. */
        loadIdentityCertsAndDiscardInvalid_GmCerts_(d);        
    }
}

iGmIdentity *findIdentity_GmCerts(iGmCerts *d, const iBlock *fingerprint) {
    if (isEmpty_Block(fingerprint)) {
        return NULL;
    }
    iForEach(PtrArray, i, &d->idents) {
        iGmIdentity *ident = i.ptr;
        if (cmp_Block(fingerprint, &ident->fingerprint) == 0) { /* TODO: could use a hash */
            return ident;
        }
    }
    return NULL;
}

iGmIdentity *findIdentityFuzzy_GmCerts(iGmCerts *d, const iString *fuzzy) {
    if (isEmpty_String(fuzzy)) {
        return NULL;
    }
    iGmIdentity *found = NULL;
    iForEach(PtrArray, i, &d->idents) {
        iBeginCollect();
        iGmIdentity *ident = i.ptr;
        if (indexOfCStrSc_String(collect_String(hexEncode_Block(&ident->fingerprint)),
                                 cstr_String(fuzzy), &iCaseInsensitive) != iInvalidPos) {
            found = ident;
        }
        if (!found) {
            const iString *name = name_GmIdentity(ident);
            if (indexOfCStrSc_String(name, cstr_String(fuzzy), &iCaseInsensitive) != iInvalidPos) {
                found = ident;
            }
        }
        iEndCollect();
        if (found) {
            break;
        }
    }    
    return found;
}

void deserializeTrusted_GmCerts(iGmCerts *d, iStream *ins, enum iImportMethod method) {
    iRegExp *      pattern = new_RegExp("([^\\s]+) ([0-9]+) ([a-z0-9]+)", 0);
    const iRangecc src     = range_Block(collect_Block(readAll_Stream(ins)));
    iRangecc       line    = iNullRange;
    lock_Mutex(d->mtx);
    while (nextSplit_Rangecc(src, "\n", &line)) {
        iRegExpMatch m;
        init_RegExpMatch(&m);
        if (matchRange_RegExp(pattern, line, &m)) {
            iBeginCollect();
            const iRangecc key   = capturedRange_RegExpMatch(&m, 1);
            const iRangecc until = capturedRange_RegExpMatch(&m, 2);
            const iRangecc fp    = capturedRange_RegExpMatch(&m, 3);
            long long lsec;
            sscanf(until.start, "%lld", &lsec);
            time_t sec = lsec;
            iDate untilDate;
            initSinceEpoch_Date(&untilDate, sec);
            /* TODO: import method? */
            const iString *hashKey = collect_String(newRange_String(key));
            if (method == all_ImportMethod ||
                (method == ifMissing_ImportMethod && !contains_StringHash(d->trusted, hashKey))) {
                insert_StringHash(d->trusted,
                                  hashKey,
                                  new_TrustEntry(collect_Block(hexDecode_Rangecc(fp)), &untilDate));
            }
            iEndCollect();
        }
    }
    unlock_Mutex(d->mtx);
    iRelease(pattern);
}

static void load_GmCerts_(iGmCerts *d) {
    iFile *f = new_File(collect_String(concatCStr_Path(&d->saveDir, trustedFilename_GmCerts_)));
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        deserializeTrusted_GmCerts(d, stream_File(f), all_ImportMethod);
    }
    iRelease(f);
    loadIdentities_GmCerts_(d);
}

iBool verify_GmCerts_(iTlsRequest *request, const iTlsCertificate *cert, int depth) {
    iGmCerts *d = certs_App();
    if (depth != 0) {
        /* We only check the primary certificate. */
        return iTrue;
    }
    const iAddress *address = address_TlsRequest(request);
    iRangecc        domain  = range_String(hostName_Address(address));
    uint16_t        port    = port_Address(address);
#if 0
    printf("[verify_GmCerts_] peer: %s\n", cstrCollect_String(toString_Address(address)));
    printf("              hostname: %s\n", cstr_String(hostName_Address(address)));
    printf("                  port: %u\n", port_Address(address));
    printf("          cert subject: %s\n", cstrCollect_String(subject_TlsCertificate(cert)));
#endif
    return checkTrust_GmCerts(d, domain, port, cert);
}

void init_GmCerts(iGmCerts *d, const char *saveDir) {
    d->mtx = new_Mutex();
    initCStr_String(&d->saveDir, saveDir);
    d->trusted = new_StringHash();
    init_PtrArray(&d->idents);
    load_GmCerts_(d);
    setVerifyFunc_TlsRequest(verify_GmCerts_);
}

void deinit_GmCerts(iGmCerts *d) {
    setVerifyFunc_TlsRequest(NULL);
    iGuardMutex(d->mtx, {
        saveIdentities_GmCerts(d);
        iForEach(PtrArray, i, &d->idents) {
            delete_GmIdentity(i.ptr);
        }
        deinit_PtrArray(&d->idents);
        iRelease(d->trusted);
        deinit_String(&d->saveDir);
    });
    delete_Mutex(d->mtx);
}

static iRangecc stripFirstDomainLabel_(iRangecc domain) {
    iRangecc label = iNullRange;
    if (nextSplit_Rangecc(domain, ".", &label) && nextSplit_Rangecc(domain, ".", &label)) {
        return (iRangecc){ label.start, domain.end };
    }
    return iNullRange;
}

iBool verifyDomain_GmCerts(const iTlsCertificate *cert, iRangecc domain) {
    if (verifyDomain_TlsCertificate(cert, domain)) {
        return iTrue;
    }
    /* Allow for an implicit wildcard in the domain name. Self-signed TOFU is really only
       about the public/private key pair; any other details should be considered
       complementary. */
    for (iRangecc higherDomain = stripFirstDomainLabel_(domain);
         !isEmpty_Range(&higherDomain);
         higherDomain = stripFirstDomainLabel_(higherDomain)) {
        if (!iStrStrN(higherDomain.start, ".", size_Range(&higherDomain))) {
            /* Must have two labels at least. */
            break;
        }
        if (verifyDomain_TlsCertificate(cert, higherDomain)) {
            return iTrue;
        }
    }
    return iFalse;
}

static void makeTrustKey_(iRangecc domain, uint16_t port, iString *key_out) {
    punyEncodeDomain_Rangecc(domain, key_out);
    appendFormat_String(key_out, ";%u", port ? port : GEMINI_DEFAULT_PORT);    
}

iBool checkTrust_GmCerts(iGmCerts *d, iRangecc domain, uint16_t port, const iTlsCertificate *cert) {
    if (!cert) {
        return iFalse;
    }
    /* We trust CA verification implicitly. */
    const iBool isCATrusted   = (verify_TlsCertificate(cert) == authority_TlsCertificateVerifyStatus);
    const iBool isDomainValid = verifyDomain_GmCerts(cert, domain);
    /* TODO: Could call setTrusted_GmCerts() instead of duplicating the trust-setting. */
    /* Good certificate. If not already trusted, add it now. */
    iDate until;
    validUntil_TlsCertificate(cert, &until);
    iBlock *fingerprint = publicKeyFingerprint_TlsCertificate(cert);
    iString key;
    init_String(&key);
    makeTrustKey_(domain, port, &key);
    lock_Mutex(d->mtx);
    iBool ok = isDomainValid && !isExpired_TlsCertificate(cert);
    iTrustEntry *trust = value_StringHash(d->trusted, &key);
    if (trust) {
        /* We already have it, check if it matches the one we trust for this domain (if it's
           still valid. */
        if (elapsedSeconds_Time(&trust->validUntil) < 0) {
            /* Trusted cert is still valid. */
            const iBool isTrusted = cmp_Block(fingerprint, &trust->fingerprint) == 0;
            /* Even if we don't trust it, we will go ahead and update the trusted certificate
               if a CA vouched for it. */
            if (isTrusted || !isCATrusted) {
                unlock_Mutex(d->mtx);
                delete_Block(fingerprint);
                deinit_String(&key);
                return isTrusted;
            }
        }
        /* Update the trusted cert. */
        if (ok) {
            init_Time(&trust->validUntil, &until);
            set_Block(&trust->fingerprint, fingerprint);
        }
    }
    else {
        if (ok) {
            insert_StringHash(d->trusted, &key, iClob(new_TrustEntry(fingerprint, &until)));
        }
    }
    if (ok) {
        save_GmCerts_(d);
    }
    unlock_Mutex(d->mtx);
    delete_Block(fingerprint);
    deinit_String(&key);
    return ok;
}

void setTrusted_GmCerts(iGmCerts *d, iRangecc domain, uint16_t port, const iBlock *fingerprint,
                        const iDate *validUntil) {
    iString key;
    init_String(&key);
    makeTrustKey_(domain, port, &key);
    lock_Mutex(d->mtx);
    iTrustEntry *trust = value_StringHash(d->trusted, &key);
    if (trust) {
        init_Time(&trust->validUntil, validUntil);
        set_Block(&trust->fingerprint, fingerprint);
    }
    else {
        insert_StringHash(d->trusted, &key, iClob(trust = new_TrustEntry(fingerprint, validUntil)));
    }
    save_GmCerts_(d);
    unlock_Mutex(d->mtx);
    deinit_String(&key);
}

iTime domainValidUntil_GmCerts(const iGmCerts *d, iRangecc domain, uint16_t port) {
    iTime expiry;
    iZap(expiry);
    iString key;
    init_String(&key);
    makeTrustKey_(domain, port, &key);
    lock_Mutex(d->mtx);
    const iTrustEntry *trust = constValue_StringHash(d->trusted, &key);
    if (trust) {
        expiry = trust->validUntil;
    }
    unlock_Mutex(d->mtx);
    deinit_String(&key);
    return expiry;
}

iGmIdentity *identity_GmCerts(iGmCerts *d, unsigned int id) {
    return at_PtrArray(&d->idents, id);
}

const iGmIdentity *constIdentity_GmCerts(const iGmCerts *d, unsigned int id) {
    return constAt_PtrArray(&d->idents, id);
}

const iGmIdentity *identityForUrl_GmCerts(const iGmCerts *d, const iString *url) {
    if (isEmpty_String(url)) {
        return NULL;
    }
    lock_Mutex(d->mtx);
    const iGmIdentity *found = NULL;
    iConstForEach(PtrArray, i, &d->idents) {
        const iGmIdentity *ident = i.ptr;
        iConstForEach(StringSet, j, ident->useUrls) {
            const iString *used = j.value;
            if (startsWithCase_String(url, cstr_String(used))) {
                found = ident;
                goto done;
            }
        }
    }
done:
    unlock_Mutex(d->mtx);
    /* Fallback: Titan URLs use the Gemini identities, if not otherwise specified. */
    if (!found && startsWithCase_String(url, "titan://")) {
        iString *mod = copy_String(url);
        remove_Block(&mod->chars, 0, 5);
        prependCStr_String(mod, "gemini");
        found = identityForUrl_GmCerts(d, collect_String(mod));
    }
    return found;
}

static iGmIdentity *add_GmCerts_(iGmCerts *d, iTlsCertificate *cert, int flags) {
    iGmIdentity *id = new_GmIdentity();
    setCertificate_GmIdentity_(id, cert);
    /* Save the certificate and private key as PEM files. */
    if (~flags & temporary_GmIdentityFlag) {
        const char *finger = cstrCollect_String(hexEncode_Block(&id->fingerprint));
        if (!writeTextFile_(
                collect_String(concatCStr_Path(&d->saveDir, format_CStr("idents/%s.crt", finger))),
                collect_String(pem_TlsCertificate(id->cert)))) {
            delete_GmIdentity(id);
            return NULL;
        }
        if (!writeTextFile_(
                collect_String(concatCStr_Path(&d->saveDir, format_CStr("idents/%s.key", finger))),
                collect_String(privateKeyPem_TlsCertificate(id->cert)))) {
            delete_GmIdentity(id);
            return NULL;
        }
    }
    iGuardMutex(d->mtx, pushBack_PtrArray(&d->idents, id));
    return id;
}

iGmIdentity *newIdentity_GmCerts(iGmCerts *d, int flags, iDate validUntil, const iString *commonName,
                                 const iString *email, const iString *userId, const iString *domain,
                                 const iString *org, const iString *country) {
    /* Note: RFC 5280 defines a self-signed CA certificate as also being self-issued, so
       to honor this definition we set the issuer and the subject to be fully equivalent. */
    const iTlsCertificateName names[] = {
        { issuerCommonName_TlsCertificateNameType,    commonName },
        { issuerEmailAddress_TlsCertificateNameType,  !isEmpty_String(email)   ? email   : NULL },
        { issuerUserId_TlsCertificateNameType,        !isEmpty_String(userId)  ? userId  : NULL },
        { issuerDomain_TlsCertificateNameType,        !isEmpty_String(domain)  ? domain  : NULL },
        { issuerOrganization_TlsCertificateNameType,  !isEmpty_String(org)     ? org     : NULL },
        { issuerCountry_TlsCertificateNameType,       !isEmpty_String(country) ? country : NULL },
        { subjectCommonName_TlsCertificateNameType,   commonName },
        { subjectEmailAddress_TlsCertificateNameType, !isEmpty_String(email)   ? email   : NULL },
        { subjectUserId_TlsCertificateNameType,       !isEmpty_String(userId)  ? userId  : NULL },
        { subjectDomain_TlsCertificateNameType,       !isEmpty_String(domain)  ? domain  : NULL },
        { subjectOrganization_TlsCertificateNameType, !isEmpty_String(org)     ? org     : NULL },
        { subjectCountry_TlsCertificateNameType,      !isEmpty_String(country) ? country : NULL },
        { 0, NULL }
    };
    return add_GmCerts_(d, newSelfSignedRSA_TlsCertificate(2048, validUntil, names), flags);
}

void importIdentity_GmCerts(iGmCerts *d, iTlsCertificate *cert, const iString *notes) {
    iGmIdentity *id = add_GmCerts_(d, cert, 0);
    set_String(&id->notes, notes);
}

static const char *certPath_GmCerts_(const iGmCerts *d, const iGmIdentity *identity) {
    if (!(identity->flags & temporary_GmIdentityFlag)) {
        const char *finger = cstrCollect_String(hexEncode_Block(&identity->fingerprint));
        return concatPath_CStr(cstr_String(&d->saveDir), format_CStr("idents/%s", finger));
    }
    return NULL;
}

void deleteIdentity_GmCerts(iGmCerts *d, iGmIdentity *identity) {
    lock_Mutex(d->mtx);
    /* Only delete the files if we created them. */
    const char *filename = certPath_GmCerts_(d, identity);
    if (filename) {
        remove(format_CStr("%s.crt", filename));
        remove(format_CStr("%s.key", filename));
    }
    removeOne_PtrArray(&d->idents, identity);
    collect_GmIdentity(identity);
    unlock_Mutex(d->mtx);
}

const iString *certificatePath_GmCerts(const iGmCerts *d, const iGmIdentity *identity) {
    const char *filename = certPath_GmCerts_(d, identity);
    if (filename) {
        return collectNewFormat_String("%s.crt", filename);
    }
    return NULL;
}

const iPtrArray *identities_GmCerts(const iGmCerts *d) {
    return &d->idents;
}

void signIn_GmCerts(iGmCerts *d, iGmIdentity *identity, const iString *url) {
    if (identity) {
        signOut_GmCerts(d, url);
        setUse_GmIdentity(identity, url, iTrue);
    }
}

void signOut_GmCerts(iGmCerts *d, const iString *url) {
    iForEach(PtrArray, i, &d->idents) {
        setUse_GmIdentity(i.ptr, url, iFalse);
    }
}

const iPtrArray *listIdentities_GmCerts(const iGmCerts *d, iGmCertsIdentityFilterFunc filter,
                                        void *context) {
    iPtrArray *list = collectNew_PtrArray();
    lock_Mutex(d->mtx);
    iConstForEach(PtrArray, i, &d->idents) {
        if (!filter || filter(context, i.ptr)) {
            pushBack_PtrArray(list, i.ptr);
        }
    }
    unlock_Mutex(d->mtx);
    return list;
}
