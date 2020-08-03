#include "gmcerts.h"

#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/stringhash.h>
#include <the_Foundation/time.h>
#include <ctype.h>

static const char *filename_GmCerts_ = "trusted.txt";

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

iDefineObjectConstructionArgs(TrustEntry, (const iBlock *fingerprint, const iDate *until), fingerprint, until)
iDefineClass(TrustEntry)

/*-----------------------------------------------------------------------------------------------*/

struct Impl_GmCerts {
    iString saveDir;
    iStringHash *trusted;
};

iDefineTypeConstructionArgs(GmCerts, (const char *saveDir), saveDir)

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
}

void init_GmCerts(iGmCerts *d, const char *saveDir) {
    initCStr_String(&d->saveDir, saveDir);
    d->trusted = new_StringHash();
    load_GmCerts_(d);
}

void deinit_GmCerts(iGmCerts *d) {    
    iRelease(d->trusted);
    deinit_String(&d->saveDir);
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
    iTrustEntry *trust = value_StringHash(d->trusted, key);
    if (trust) {
        /* We already have it, check if it matches the one we trust for this domain (if it's
           still valid. */
        iTime now;
        initCurrent_Time(&now);
        if (secondsSince_Time(&trust->validUntil, &now) > 0) {
            /* Trusted cert is still valid. */
            return cmp_Block(fingerprint, &trust->fingerprint) == 0;
        }
        /* Update the trusted cert. */
        init_Time(&trust->validUntil, &until);
        set_Block(&trust->fingerprint, fingerprint);
    }
    else {
        insert_StringHash(d->trusted, key, iClob(new_TrustEntry(fingerprint, &until)));
    }
    save_GmCerts_(d);
    return iTrue;
}
