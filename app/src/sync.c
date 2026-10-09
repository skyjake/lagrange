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

#include "sync.h"
#include "syncdata.h"
#include "app.h"
#include "bookmarks.h"
#include "books.h"
#include "feeds.h"
#include "ui/command.h"
#include "ui/dialog.h"
#include "ui/miscdialogs.h"
#include "ui/root.h"
#include "ui/widget.h"
#include "ui/window.h"
#if defined (iPlatformAppleMobile)
#   include "platform/ios.h"
#endif

#include <lagrange/core.h>
#include <lagrange/gmcerts.h>
#include <lagrange/gmutil.h>
#include <lagrange/lang.h>
#include <lagrange/prefs.h>
#include <lagrange/sitespec.h>
#include <lagrange/snippets.h>
#include <lagrange/visited.h>

#include <the_Foundation/atomic.h>
#include <the_Foundation/buffer.h>
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/mutex.h>
#include <the_Foundation/path.h>
#include <the_Foundation/stringarray.h>
#include <the_Foundation/thread.h>
#include <the_Foundation/tlsrequest.h>
#include <the_Foundation/toml.h>
#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_timer.h>

static const char  *stateFileName_Sync_       = "state.ini";
static const char  *pristineFileName_Sync_    = "pristine.zip";
static const char  *recentFileName_Sync_      = "recent.zip";
static const char  *recentLocalFileName_Sync_ = "recentlocal.zip";
static const double timerInterval_Sync_       = 5 * 60;  /* seconds; checks if a sync is due */
static const double retryDelay_Sync_          = 15;      /* seconds; when blocked */
static const double failureBackoff_Sync_      = 10 * 60; /* seconds */
static const int    maxAttempts_Sync_         = 3;
static const double requestTimeout_Sync_      = 30;      /* seconds */
static const double quitTimeout_Sync_         = 4;       /* seconds; when the app is closing */
static const double minTransferRate_Sync_     = 10000;   /* bytes per second */
static const double recentWindow_Sync_        = 24 * 3600; /* seconds of recent changes kept */
static const double recentDelay_Sync_         = 60;      /* seconds from a change to uploading */
static const double recentInterval_Sync_      = 60;      /* minimum seconds between checks */
static const double recentBackoff_Sync_       = 2 * 60;  /* seconds */

static const char *typeNames_Sync_[max_UserDataType] = {
    "bookmarks", "books", "visited", "identities", "trusted", "snippets", "sites"
};

enum iSyncUploadResult {
    ok_SyncUploadResult,
    stale_SyncUploadResult,
    failed_SyncUploadResult,
};

enum iSyncDownloadResult {
    failed_SyncDownloadResult,
    unchanged_SyncDownloadResult,
    data_SyncDownloadResult,
};

enum iSyncMode {
    full_SyncMode,
    recent_SyncMode, /* only the recent changes */
};

iDeclareType(Sync)

struct Impl_Sync {
    iMutex      *mtx;
    iString      dir;
    /* Persistent state. */
    iTime        lastSyncTime;  /* last successful sync */
    iTime        lastCheckTime; /* last successful contact with the server */
    iTime        lastAttemptTime;
    iString      lastError;
    iTime        modified[max_UserDataType];
    iTime        syncedTime;    /* data modified after this has not been uploaded */
    iString      syncedServer;  /* the pristine data belongs to this server and identity */
    iString      syncedIdentity;
    uint64_t     generation;    /* of the pristine data, which is loaded only when syncing */
    iSyncRecent *recent;           /* recent changes stored on the server */
    iSyncRecent *recentLocal;      /* local changes to visited feed entries */
    iTime        recentModified;   /* last local change to the recent data */
    iTime        recentSyncedTime; /* recent changes before this are on the server */
    /* Runtime. */
    iAtomicInt   isRunning;        /* full sync */
    enum iSyncMode mode;
    iTime        recentCheckTime;   /* last successful contact */
    iTime        recentAttemptTime; /* this session only, for rate limiting */
    iTime        recentFailTime;
    iString      recentError;
    iAtomicInt   isRecentChangePosted;
    iBool        isFullPending;    /* full sync starts after the recent changes */
    iBool        isFullPendingManual;
    iBool        isBlocking;       /* worker runs in the calling thread */
    iBool        isBackgroundTask;
    double       requestTimeout;
    SDL_TimerID  recentTimer;
    iBool        isApplying;
    iBool        isManual;
    iString      trustHost;     /* untrusted server certificate offered to the user */
    uint16_t     trustPort;
    iBlock      *trustFingerprint;
    iDate        trustValidUntil;
    iBlock      *declinedFingerprint;
    SDL_TimerID  timer;
    SDL_TimerID  retryTimer;
    iThread     *worker;
    /* Worker input. */
    iString      url;
    iString      identity;
    iTlsCertificate *cert;
    iBlock      *key;
    iSyncData   *base;
    iSyncData   *snapshot;
    iSyncRecent *outgoing;    /* local recent changes; NULL if none */
    iTime        snapshotTime;
    /* Worker output. */
    iSyncRecent *recentResult;
    iSyncData   *result;      /* to be applied locally */
    iBool        isPristineStaged; /* new pristine data is in a temporary file */
    uint64_t     newGeneration;
    iArray       idMap;       /* bookmark IDs renumbered in `result` */
    iString      error;
    iBlock      *serverFingerprint; /* set if the server certificate was not trusted */
    iDate        serverValidUntil;
    iBool        didContact;
    iBool        didUpload;   /* all local changes are on the server */
};

static iSync sync_;

static const iPrefs *prefs_Sync_(void) {
    return prefs_App();
}

static const iString *server_Sync_(void) {
    return &prefs_Sync_()->strings[syncServer_PrefsString];
}

static const iString *identity_Sync_(void) {
    return &prefs_Sync_()->strings[syncIdentity_PrefsString];
}

static iBool isConfigured_Sync_(void) {
    return prefs_Sync_()->sync && !isEmpty_String(server_Sync_()) &&
           !isEmpty_String(identity_Sync_());
}

static const iString *path_Sync_(const iSync *d, const char *fileName) {
    return collect_String(concatCStr_Path(&d->dir, fileName));
}

/*----------------------------------------------------------------------------------------------*/
/* Persistent state */

static void handleStateKeyValue_Sync_(void *context, const iString *table, const iString *key,
                                      const iTomlValue *value) {
    iSync *d = context;
    if (!cmp_String(table, "modified")) {
        for (int i = 0; i < max_UserDataType; i++) {
            if (!cmp_String(key, typeNames_Sync_[i])) {
                initSeconds_Time(&d->modified[i], number_TomlValue(value));
            }
        }
        return;
    }
    if (value->type == string_TomlType) {
        if (!cmp_String(key, "server")) {
            set_String(&d->syncedServer, value->value.string);
        }
        else if (!cmp_String(key, "identity")) {
            set_String(&d->syncedIdentity, value->value.string);
        }
        else if (!cmp_String(key, "error")) {
            set_String(&d->lastError, value->value.string);
        }
        return;
    }
    const double num = number_TomlValue(value);
    if (!cmp_String(key, "lastsync")) {
        initSeconds_Time(&d->lastSyncTime, num);
    }
    else if (!cmp_String(key, "lastcheck")) {
        initSeconds_Time(&d->lastCheckTime, num);
    }
    else if (!cmp_String(key, "lastattempt")) {
        initSeconds_Time(&d->lastAttemptTime, num);
    }
    else if (!cmp_String(key, "synced")) {
        initSeconds_Time(&d->syncedTime, num);
    }
    else if (!cmp_String(key, "recentmodified")) {
        initSeconds_Time(&d->recentModified, num);
    }
    else if (!cmp_String(key, "recentsynced")) {
        initSeconds_Time(&d->recentSyncedTime, num);
    }
    else if (!cmp_String(key, "recentcheck")) {
        initSeconds_Time(&d->recentCheckTime, num);
    }
    else if (!cmp_String(key, "generation")) {
        d->generation = (uint64_t) num;
    }
}

static void loadRecent_Sync_(const iSync *d, iSyncRecent *recent, const char *fileName) {
    iFile *f = new_File(path_Sync_(d, fileName));
    if (open_File(f, readOnly_FileMode)) {
        if (!deserialize_SyncRecent(recent, collect_Block(readAll_File(f)))) {
            fprintf(stderr, "[Sync] failed to read %s\n", fileName);
        }
    }
    iRelease(f);
}

static iSyncData *loadPristine_Sync_(const iSync *d) {
    iSyncData *pristine = new_SyncData();
    iFile     *f        = new_File(path_Sync_(d, pristineFileName_Sync_));
    if (open_File(f, readOnly_FileMode)) {
        iBlock *data = readAll_File(f);
        if (!deserialize_SyncData(pristine, data)) {
            fprintf(stderr, "[Sync] failed to read the pristine data\n");
            delete_SyncData(pristine);
            pristine = new_SyncData();
        }
        delete_Block(data);
    }
    iRelease(f);
    return pristine;
}

static void loadState_Sync_(iSync *d) {
    iFile *f = new_File(path_Sync_(d, stateFileName_Sync_));
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        iTomlParser *toml = new_TomlParser();
        setHandlers_TomlParser(toml, NULL, handleStateKeyValue_Sync_, d);
        parse_TomlParser(toml, collect_String(readString_File(f)));
        delete_TomlParser(toml);
    }
    iRelease(f);
    loadRecent_Sync_(d, d->recent, recentFileName_Sync_);
    loadRecent_Sync_(d, d->recentLocal, recentLocalFileName_Sync_);
}

static void saveState_Sync_(iSync *d) {
    makeDirs_Path(&d->dir);
    iString *str = new_String();
    lock_Mutex(d->mtx);
    format_String(str,
                  "lastsync = %f\nlastcheck = %f\nlastattempt = %f\nsynced = %f\n"
                  "recentmodified = %f\nrecentsynced = %f\nrecentcheck = %f\ngeneration = %llu\n"
                  "server = \"%s\"\nidentity = \"%s\"\nerror = \"%s\"\n\n[modified]\n",
                  seconds_Time(&d->lastSyncTime),
                  seconds_Time(&d->lastCheckTime),
                  seconds_Time(&d->lastAttemptTime),
                  seconds_Time(&d->syncedTime),
                  seconds_Time(&d->recentModified),
                  seconds_Time(&d->recentSyncedTime),
                  seconds_Time(&d->recentCheckTime),
                  (unsigned long long) d->generation,
                  cstrCollect_String(quote_String(&d->syncedServer, iFalse)),
                  cstrCollect_String(quote_String(&d->syncedIdentity, iFalse)),
                  cstrCollect_String(quote_String(&d->lastError, iFalse)));
    for (int i = 0; i < max_UserDataType; i++) {
        appendFormat_String(str, "%s = %f\n", typeNames_Sync_[i], seconds_Time(&d->modified[i]));
    }
    unlock_Mutex(d->mtx);
    iFile *f = new_File(path_Sync_(d, stateFileName_Sync_));
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        write_File(f, utf8_String(str));
    }
    iRelease(f);
    delete_String(str);
}

static const iString *tempPath_Sync_(const iSync *d, const char *fileName) {
    return collectNewFormat_String("%s.tmp", cstr_String(path_Sync_(d, fileName)));
}

static iBool writeTempFile_Sync_(iSync *d, const char *fileName, const iBlock *data) {
    makeDirs_Path(&d->dir);
    iFile *f = new_File(tempPath_Sync_(d, fileName));
    const iBool ok = open_File(f, writeOnly_FileMode);
    if (ok) {
        write_File(f, data);
        close_File(f);
    }
    iRelease(f);
    return ok;
}

static void commitFile_Sync_(iSync *d, const char *fileName) {
    commitFile_Core(cstr_String(path_Sync_(d, fileName)), cstr_String(tempPath_Sync_(d, fileName)));
}

static void writeFile_Sync_(iSync *d, const char *fileName, const iBlock *data) {
    if (writeTempFile_Sync_(d, fileName, data)) {
        commitFile_Sync_(d, fileName);
    }
}

static void saveRecent_Sync_(iSync *d) {
    writeFile_Sync_(d, recentFileName_Sync_, collect_Block(serialize_SyncRecent(d->recent)));
    lock_Mutex(d->mtx);
    iBlock *local = serialize_SyncRecent(d->recentLocal);
    unlock_Mutex(d->mtx);
    writeFile_Sync_(d, recentLocalFileName_Sync_, collect_Block(local));
}

static void reset_Sync_(iSync *d) {
    /* Next sync begins from scratch, like the first one. */
    d->generation = 0;
    remove(cstr_String(path_Sync_(d, pristineFileName_Sync_)));
    delete_SyncRecent(d->recent);
    d->recent = new_SyncRecent();
    lock_Mutex(d->mtx);
    delete_SyncRecent(d->recentLocal);
    d->recentLocal = new_SyncRecent();
    unlock_Mutex(d->mtx);
    remove(cstr_String(path_Sync_(d, recentFileName_Sync_)));
    remove(cstr_String(path_Sync_(d, recentLocalFileName_Sync_)));
    iZap(d->recentModified);
    iZap(d->recentSyncedTime);
    iZap(d->recentCheckTime);
    iZap(d->recentFailTime);
    clear_String(&d->recentError);
    iZap(d->lastSyncTime);
    iZap(d->lastCheckTime);
    iZap(d->lastAttemptTime);
    iZap(d->syncedTime);
    clear_String(&d->lastError);
    set_String(&d->syncedServer, server_Sync_());
    set_String(&d->syncedIdentity, identity_Sync_());
    saveState_Sync_(d);
}

static iBool isDirty_Sync_(const iSync *d) {
    iBool dirty = iFalse;
    lock_Mutex(d->mtx);
    for (int i = 0; i < max_UserDataType; i++) {
        if (cmp_Time(&d->modified[i], &d->syncedTime) > 0) {
            dirty = iTrue;
        }
    }
    unlock_Mutex(d->mtx);
    return dirty;
}

static iBool isRecentDirty_Sync_(const iSync *d) {
    iBool dirty;
    iGuardMutex(d->mtx, dirty = cmp_Time(&d->recentModified, &d->recentSyncedTime) > 0);
    return dirty;
}

static void recentChanged_Sync_(iSync *d) {
    if (!prefs_Sync_()->sync) {
        return;
    }
    iGuardMutex(d->mtx, initCurrent_Time(&d->recentModified));
    if (!exchange_Atomic(&d->isRecentChangePosted, iTrue)) {
        postCommand_App("sync.recent.changed");
    }
}

static void userDataChanged_Sync_(enum iUserDataType type) {
    iSync *d = &sync_;
    if (d->isApplying || !d->mtx) {
        return;
    }
    iGuardMutex(d->mtx, initCurrent_Time(&d->modified[type]));
    if (type == books_UserDataType) {
        recentChanged_Sync_(d);
    }
}

static void visitedUrlChanged_Sync_(const iString *url, iBool isRemoved) {
    /* Only the read status of feed entries is synced quickly. */
    iSync *d = &sync_;
    iUnused(isRemoved);
    if (d->isApplying || !d->mtx || !prefs_Sync_()->sync || !isEntryUrl_Feeds(url)) {
        return;
    }
    iTime now;
    initCurrent_Time(&now);
    const int64_t changed = integralSeconds_Time(&now);
    lock_Mutex(d->mtx);
    prune_SyncRecent(d->recentLocal, changed - (int64_t) recentWindow_Sync_);
    setVisit_SyncRecent(d->recentLocal, &(iSyncRecentVisit){ .url = *url, .changed = changed });
    unlock_Mutex(d->mtx);
    recentChanged_Sync_(d);
}

/*----------------------------------------------------------------------------------------------*/
/* Snapshots of the current data */

static void snapshotType_Sync_(iSyncData *snap, enum iUserDataType type) {
    iBuffer *buf = new_Buffer();
    openEmpty_Buffer(buf);
    iStream *outs = stream_Buffer(buf);
    switch (type) {
        case bookmarks_UserDataType:
            serialize_Bookmarks(bookmarks_App(), outs);
            break;
        case books_UserDataType:
            serialize_Books(outs);
            break;
        case visited_UserDataType:
            serialize_Visited(visited_App(), outs);
            break;
        case trusted_UserDataType:
            serialize_GmCerts(certs_App(), outs, NULL);
            break;
        case snippets_UserDataType:
            serialize_Snippets(outs);
            break;
        case sites_UserDataType:
            serialize_SiteSpec(outs);
            break;
        default:
            break;
    }
    parseNative_SyncData(snap, type, data_Buffer(buf));
    iRelease(buf);
}

static iSyncData *takeSnapshot_Sync_(iSync *d) {
    iSyncData *snap = new_SyncData();
    for (int type = 0; type < max_UserDataType; type++) {
        if (type != identities_UserDataType) {
            snapshotType_Sync_(snap, type);
        }
    }
    iConstForEach(PtrArray, i, listIdentities_GmCerts(certs_App(), NULL, NULL)) {
        const iGmIdentity *ident = i.ptr;
        if (ident->flags & temporary_GmIdentityFlag || !hasPrivateKey_TlsCertificate(ident->cert)) {
            continue;
        }
        iSyncRecord *rec =
            add_SyncData(snap, identities_UserDataType,
                         collect_String(hexEncode_Block(&ident->fingerprint)));
        setFieldCStr_SyncRecord(rec, icon_SyncIdentityField, format_CStr("%u", ident->icon));
        setField_SyncRecord(rec, notes_SyncIdentityField, &ident->notes);
        iStringArray *urls = new_StringArray();
        iConstForEach(StringSet, u, ident->useUrls) {
            pushBack_StringArray(urls, u.value); /* sorted */
        }
        setField_SyncRecord(rec, useUrls_SyncIdentityField,
                            collect_String(joinCStr_StringArray(urls, "\n")));
        iRelease(urls);
        setField_SyncRecord(rec, cert_SyncIdentityField,
                            collect_String(pem_TlsCertificate(ident->cert)));
        setField_SyncRecord(rec, key_SyncIdentityField,
                            collect_String(privateKeyPem_TlsCertificate(ident->cert)));
    }
    lock_Mutex(d->mtx);
    for (int type = 0; type < max_UserDataType; type++) {
        snap->modified[type] = d->modified[type];
    }
    unlock_Mutex(d->mtx);
    return snap;
}

static int64_t recentCutoff_Sync_(void) {
    iTime now;
    initCurrent_Time(&now);
    return integralSeconds_Time(&now) - (int64_t) recentWindow_Sync_;
}

static iSyncRecent *takeRecentSnapshot_Sync_(iSync *d) {
    const int64_t cutoff = recentCutoff_Sync_();
    iSyncRecent  *snap   = new_SyncRecent();
    lock_Mutex(d->mtx);
    prune_SyncRecent(d->recentLocal, cutoff);
    iSyncRecent *log = copy_SyncRecent(d->recentLocal);
    unlock_Mutex(d->mtx);
    /* Current state of the changed URLs; missing ones were removed. */
    iConstForEach(Array, i, &log->visits.values) {
        const iSyncRecentVisit *change = i.value;
        const iTime when = urlVisitTime_Visited(visited_App(), &change->url);
        iSyncRecentVisit visit = { .url = change->url, .changed = change->changed };
        if (isValid_Time(&when)) {
            visit.when       = integralSeconds_Time(&when);
            visit.visitFlags = urlVisitFlags_Visited(visited_App(), &change->url);
        }
        else {
            visit.flags = removed_SyncRecentVisitFlag;
        }
        setVisit_SyncRecent(snap, &visit);
    }
    delete_SyncRecent(log);
    iTime since;
    initSeconds_Time(&since, (double) cutoff);
    iConstForEach(PtrArray, j, listPositions_Books(since)) {
        const iBook *book = j.ptr;
        setBook_SyncRecent(snap,
                           &(iSyncRecentBook){ .id           = book->id,
                                               .positionTime = integralSeconds_Time(&book->positionTime),
                                               .chapter      = book->chapter,
                                               .numChapters  = book->numChapters,
                                               .pos          = book->chapterPos });
    }
    return snap;
}

/*----------------------------------------------------------------------------------------------*/
/* Applying synced data */

static iStream *nativeStream_(const iSyncData *data, enum iUserDataType type) {
    iBuffer *buf = iClob(new_Buffer());
    open_Buffer(buf, collect_Block(native_SyncData(data, type)));
    return stream_Buffer(buf);
}

static void applyIdentities_Sync_(const iSyncData *data, const iString *protectedIdentity) {
    iGmCerts *certs = certs_App();
    iConstForEach(StringHash, i, data->records[identities_UserDataType]) {
        const iSyncRecord *rec   = value_StringHashNode(i.value);
        iGmIdentity       *ident = findIdentity_GmCerts(
            certs, collect_Block(hexDecode_Rangecc(range_String(&rec->key))));
        if (!ident) {
            iTlsCertificate *cert =
                newPemKey_TlsCertificate(field_SyncRecord(rec, cert_SyncIdentityField),
                                         field_SyncRecord(rec, key_SyncIdentityField));
            if (isEmpty_TlsCertificate(cert) || !hasPrivateKey_TlsCertificate(cert)) {
                delete_TlsCertificate(cert);
                continue;
            }
            ident = importIdentity_GmCerts(certs, cert, field_SyncRecord(rec, notes_SyncIdentityField));
            if (!ident) {
                continue;
            }
        }
        ident->icon = (iChar) strtoul(cstr_String(field_SyncRecord(rec, icon_SyncIdentityField)), NULL, 10);
        set_String(&ident->notes, field_SyncRecord(rec, notes_SyncIdentityField));
        clearUse_GmIdentity(ident);
        iRangecc url = iNullRange;
        while (nextSplit_Rangecc(range_String(field_SyncRecord(rec, useUrls_SyncIdentityField)),
                                 "\n", &url)) {
            setUse_GmIdentity(ident, collectNewRange_String(url), iTrue);
        }
    }
    iConstForEach(PtrArray, j, listIdentities_GmCerts(certs, NULL, NULL)) {
        iGmIdentity   *ident = j.ptr;
        const iString *fp    = collect_String(hexEncode_Block(&ident->fingerprint));
        if (~ident->flags & temporary_GmIdentityFlag &&
            !find_SyncData(data, identities_UserDataType, fp) &&
            !equal_String(fp, protectedIdentity)) {
            deleteIdentity_GmCerts(certs, ident);
        }
    }
    saveIdentities_GmCerts(certs);
    postCommand_App("idents.changed");
}

static void applyType_Sync_(const iSyncData *data, enum iUserDataType type,
                            const iArray *bookmarkIdMap, const iString *protectedIdentity) {
    switch (type) {
        case bookmarks_UserDataType: {
            iBookmarks    *bm     = bookmarks_App();
            const uint32_t recent = recentFolder_Bookmarks(bm);
            clear_Bookmarks(bm);
            deserialize_Bookmarks(bm, nativeStream_(data, type), all_ImportMethod);
            reserveIds_Bookmarks(bm, data->maxBookmarkId);
            setRecentFolder_Bookmarks(bm, mappedId_SyncIdMapping(bookmarkIdMap, recent));
            save_Bookmarks(bm, cstr_String(dataDir_App()));
            notify_App("bookmarks.changed sync:1");
            break;
        }
        case books_UserDataType:
            applySync_Books(nativeStream_(data, type));
            notify_App("books.changed");
            notify_App("books.synced");
            break;
        case visited_UserDataType:
            clear_Visited(visited_App());
            deserialize_Visited(visited_App(), nativeStream_(data, type), iFalse);
            notify_App("visited.changed");
            break;
        case identities_UserDataType:
            applyIdentities_Sync_(data, protectedIdentity);
            break;
        case trusted_UserDataType:
            replaceTrusted_GmCerts(certs_App(), nativeStream_(data, type));
            break;
        case snippets_UserDataType:
            deserialize_Snippets(nativeStream_(data, type), all_ImportMethod);
            notify_App("snippets.changed");
            break;
        case sites_UserDataType:
            replace_SiteSpec(nativeStream_(data, type));
            docPrefsChanged_App();
            break;
        default:
            break;
    }
}

static iBool isMappedTo_(const iArray *idMap, uint32_t id) {
    iConstForEach(Array, i, idMap) {
        if (((const iSyncIdMapping *) i.value)->newId == id) {
            return iTrue;
        }
    }
    return iFalse;
}

static iBool isMappedFrom_(const iArray *idMap, uint32_t id) {
    iConstForEach(Array, i, idMap) {
        if (((const iSyncIdMapping *) i.value)->oldId == id) {
            return iTrue;
        }
    }
    return iFalse;
}

static void apply_Sync_(iSync *d, const iSyncData *result, const iArray *idMap) {
    /* The data may have changed while syncing, so the result is merged with the current data
       using the snapshot as the base. */
    iSyncData *live     = takeSnapshot_Sync_(d);
    iSyncData *base     = copy_SyncData(d->snapshot);
    live->generation    = d->snapshot->generation;
    live->maxBookmarkId = d->snapshot->maxBookmarkId;
    iArray    *liveMap  = new_Array(sizeof(iSyncIdMapping));
    iArray    *finalMap = new_Array(sizeof(iSyncIdMapping));
    iArray    *mergeMap = new_Array(sizeof(iSyncIdMapping));
    /* Bookmarks created during the sync must not collide with renumbered ones. */ {
        uint32_t next = iMax(maxBookmarkId_SyncData(live), maxBookmarkId_SyncData(result));
        iConstForEach(StringHash, i, live->records[bookmarks_UserDataType]) {
            const iSyncRecord *rec = value_StringHashNode(i.value);
            const uint32_t     id  = toInt_String(&rec->key);
            if (!find_SyncData(d->snapshot, bookmarks_UserDataType, &rec->key) &&
                (find_SyncData(result, bookmarks_UserDataType, &rec->key) ||
                 isMappedTo_(idMap, id))) {
                pushBack_Array(liveMap, &(iSyncIdMapping){ id, ++next });
            }
        }
        pushBackN_Array(liveMap, constData_Array(idMap), size_Array(idMap));
    }
    remapBookmarks_SyncData(base, idMap);
    remapBookmarks_SyncData(live, liveMap);
    iSyncData *final = merge_SyncData(base, live, result, &d->identity, mergeMap);
    /* Mapping from the current bookmark IDs to the final ones. */ {
        iConstForEach(Array, i, liveMap) {
            const iSyncIdMapping *m = i.value;
            pushBack_Array(finalMap,
                           &(iSyncIdMapping){ m->oldId, mappedId_SyncIdMapping(mergeMap, m->newId) });
        }
        iConstForEach(Array, j, mergeMap) {
            const iSyncIdMapping *m = j.value;
            if (!isMappedTo_(liveMap, m->oldId) && !isMappedFrom_(liveMap, m->oldId)) {
                pushBack_Array(finalMap, m);
            }
        }
    }
    /* The current data with the final bookmark IDs. */
    remapBookmarks_SyncData(live, mergeMap);
    d->isApplying = iTrue;
    for (int type = 0; type < max_UserDataType; type++) {
        if (!equalRecords_SyncData(final, live, type) ||
            (type == bookmarks_UserDataType && !isEmpty_Array(finalMap))) {
            applyType_Sync_(final, type, finalMap, &d->identity);
        }
    }
    reserveIds_Bookmarks(bookmarks_App(), final->maxBookmarkId);
    d->isApplying = iFalse;
    remapBookmarkIds_Feeds(finalMap);
    delete_SyncData(final);
    delete_Array(mergeMap);
    delete_Array(finalMap);
    delete_Array(liveMap);
    delete_SyncData(base);
    delete_SyncData(live);
}

static void applyRecent_Sync_(iSync *d, const iSyncRecent *recent) {
    iVisited *visited          = visited_App();
    iBool     isVisitedChanged = iFalse;
    iBool     isBooksChanged   = iFalse;
    d->isApplying = iTrue;
    iConstForEach(Array, i, &recent->visits.values) {
        const iSyncRecentVisit *visit = i.value;
        lock_Mutex(d->mtx);
        const iSyncRecentVisit *own = findVisit_SyncRecent(d->recentLocal, &visit->url);
        const iBool isOwnLater = own && own->changed >= visit->changed;
        unlock_Mutex(d->mtx);
        const iTime   local     = urlVisitTime_Visited(visited, &visit->url);
        const int64_t localWhen = isValid_Time(&local) ? integralSeconds_Time(&local) : 0;
        if (isOwnLater || localWhen > visit->changed) {
            continue; /* changed here afterwards */
        }
        if (visit->flags & removed_SyncRecentVisitFlag) {
            if (localWhen) {
                removeUrl_Visited(visited, &visit->url);
                isVisitedChanged = iTrue;
            }
        }
        else if (localWhen != visit->when) {
            iTime when;
            initSeconds_Time(&when, (double) visit->when);
            visitUrlTime_Visited(visited, &visit->url, visit->visitFlags, when);
            isVisitedChanged = iTrue;
        }
    }
    iConstForEach(Array, j, &recent->books.values) {
        const iSyncRecentBook *book = j.value;
        iTime positionTime;
        initSeconds_Time(&positionTime, (double) book->positionTime);
        if (setSyncedPosition_Books(
                &book->id, book->chapter, book->numChapters, book->pos, positionTime)) {
            isBooksChanged = iTrue;
        }
    }
    d->isApplying = iFalse;
    if (isVisitedChanged) {
        notify_App("visited.changed");
    }
    if (isBooksChanged) {
        notify_App("books.changed");
        notify_App("books.synced");
    }
}

/*----------------------------------------------------------------------------------------------*/
/* Network (worker thread) */

static const iString *endpoint_Sync_(const iSync *d, const char *scheme, const char *name) {
    /* The data is located under the configured URL. */
    iUrl parts;
    init_Url(&parts, &d->url);
    iString *ep = collectNewFormat_String("%s://%s", scheme, cstr_Rangecc(parts.host));
    if (!isEmpty_Range(&parts.port)) {
        appendFormat_String(ep, ":%s", cstr_Rangecc(parts.port));
    }
    appendRange_String(ep, parts.path);
    if (!endsWith_String(ep, "/")) {
        appendCStr_String(ep, "/");
    }
    appendCStr_String(ep, name);
    return ep;
}

static iRangecc host_Sync_(const iSync *d, uint16_t *port_out) {
    iUrl parts;
    init_Url(&parts, &d->url);
    *port_out = isEmpty_Range(&parts.port) ? GEMINI_DEFAULT_PORT
                                           : (uint16_t) atoi(cstr_Rangecc(parts.port));
    return parts.host;
}

static iBool request_Sync_(iSync *d, const iString *requestUrl, const iBlock *body,
                           int *status_out, iString *meta_out, iBlock *body_out) {
    uint16_t       port;
    const iRangecc host = host_Sync_(d, &port);
    iTlsRequest *req = new_TlsRequest();
    setHost_TlsRequest(req, collectNewRange_String(host), port);
    iBlock *content = collect_Block(newCStr_Block(cstr_String(requestUrl)));
    appendCStr_Block(content, "\r\n");
    if (body) {
        append_Block(content, body);
    }
    setContent_TlsRequest(req, content);
    setCertificate_TlsRequest(req, d->cert);
    submit_TlsRequest(req);
    /* Uploads may take a while on slow connections. */
    const double timeout = (d->requestTimeout > 0 ? d->requestTimeout : requestTimeout_Sync_) +
                           size_Block(content) / minTransferRate_Sync_;
    iBool ok = iFalse;
    if (!waitForFinishedTimeout_TlsRequest(req, timeout)) {
        cancel_TlsRequest(req);
        set_String(&d->error, collectNewCStr_String(cstr_Lang("sync.error.timeout")));
    }
    else if (!isVerified_TlsRequest(req)) {
        const iTlsCertificate *serverCert = serverCertificate_TlsRequest(req);
        if (serverCert && isExpired_TlsCertificate(serverCert)) {
            set_String(&d->error, collectNewCStr_String(cstr_Lang("sync.error.expired")));
        }
        else {
            set_String(&d->error, collectNewCStr_String(cstr_Lang("sync.error.untrusted")));
            if (serverCert) {
                delete_Block(d->serverFingerprint);
                d->serverFingerprint = publicKeyFingerprint_TlsCertificate(serverCert);
                validUntil_TlsCertificate(serverCert, &d->serverValidUntil);
            }
        }
    }
    else if (status_TlsRequest(req) == error_TlsRequestStatus) {
        format_String(&d->error, "%s", cstr_String(errorMessage_TlsRequest(req)));
    }
    else if (!serverCertificate_TlsRequest(req) ||
             keyType_TlsCertificate(serverCertificate_TlsRequest(req)) !=
                 ed25519_TlsCertificateKeyType) {
        set_String(&d->error, collectNewCStr_String(cstr_Lang("sync.error.servercert")));
    }
    else {
        iBlock *response = readAll_TlsRequest(req);
        const char *start = constData_Block(response);
        const char *crlf  = strstr(start, "\r\n");
        if (crlf && crlf - start >= 2) {
            *status_out = atoi(start);
            setRange_String(meta_out, (iRangecc){ iMin(start + 3, crlf), crlf });
            setData_Block(body_out, crlf + 2, size_Block(response) - (crlf + 2 - start));
            ok = iTrue;
        }
        else {
            set_String(&d->error, collectNewCStr_String(cstr_Lang("sync.error.response")));
        }
        delete_Block(response);
    }
    iRelease(req);
    return ok;
}

static const char *mimeType_Sync_ = "application/lagrange-sync";

static iBool takeGeneration_(iBlock *body, uint64_t *gen_out) {
    /* The generation is the first line of the response body. */
    const char *start = constData_Block(body);
    const char *nl    = memchr(start, '\n', size_Block(body));
    if (!nl || nl == start) {
        return iFalse;
    }
    for (const char *c = start; c < nl; c++) {
        if (*c < '0' || *c > '9') {
            return iFalse;
        }
    }
    *gen_out = strtoull(start, NULL, 10);
    remove_Block(body, 0, nl + 1 - start);
    return iTrue;
}

static void setServerError_Sync_(iSync *d, int status, const iString *meta) {
    format_String(&d->error, "%s %d %s", cstr_Lang("sync.error.server"), status, cstr_String(meta));
}

static const char *archiveName_(enum iSyncArchive kind) {
    return kind == recent_SyncArchive ? "recent" : "data";
}

static void setError_Sync_(iSync *d, const char *msgId) {
    set_String(&d->error, collectNewCStr_String(cstr_Lang(msgId)));
}

static enum iSyncDownloadResult downloadArchive_Sync_(iSync *d, enum iSyncArchive kind,
                                                      uint64_t knownGen, uint64_t *gen_out,
                                                      iBlock **plain_out) {
    /* `plain_out` is set to NULL if nothing is stored yet. */
    int      status = 0;
    iString *meta   = collectNew_String();
    iBlock  *body   = collect_Block(new_Block(0));
    *plain_out = NULL;
    if (!request_Sync_(d,
                       collectNewFormat_String(
                           "%s?%llu",
                           cstr_String(endpoint_Sync_(d, "gemini", archiveName_(kind))),
                           (unsigned long long) knownGen),
                       NULL, &status, meta, body)) {
        return failed_SyncDownloadResult;
    }
    d->didContact = iTrue;
    if (status == 40 && equal_CStr(cstr_String(meta), "unchanged")) {
        return unchanged_SyncDownloadResult;
    }
    if (status != 20) {
        setServerError_Sync_(d, status, meta);
        return failed_SyncDownloadResult;
    }
    if (!equal_CStr(cstr_String(meta), mimeType_Sync_) || !takeGeneration_(body, gen_out)) {
        setError_Sync_(d, "sync.error.response");
        return failed_SyncDownloadResult;
    }
    if (*gen_out < knownGen) {
        setError_Sync_(d, "sync.error.rollback");
        return failed_SyncDownloadResult;
    }
    if (*gen_out == 0 && isEmpty_Block(body)) {
        return data_SyncDownloadResult;
    }
    *plain_out = decrypt_SyncData(body, d->key, kind);
    if (!*plain_out) {
        setError_Sync_(d, "sync.error.decrypt");
        return failed_SyncDownloadResult;
    }
    return data_SyncDownloadResult;
}

static enum iSyncUploadResult uploadArchive_Sync_(iSync *d, enum iSyncArchive kind,
                                                  const iBlock *archive, uint64_t baseGeneration,
                                                  uint64_t *newGen_out) {
    iBlock *enc = encrypt_SyncData(archive, d->key, kind);
    if (!enc) {
        setError_Sync_(d, "sync.error.encrypt");
        return failed_SyncUploadResult;
    }
    int      status = 0;
    iString *meta   = collectNew_String();
    iBlock  *body   = collect_Block(new_Block(0));
    const iBool ok = request_Sync_(
        d,
        collectNewFormat_String("%s;size=%zu?%llu",
                                cstr_String(endpoint_Sync_(d, "titan", archiveName_(kind))),
                                size_Block(enc),
                                (unsigned long long) baseGeneration),
        enc, &status, meta, body);
    delete_Block(enc);
    if (!ok) {
        return failed_SyncUploadResult;
    }
    if (status == 20) {
        if (!takeGeneration_(body, newGen_out)) {
            setError_Sync_(d, "sync.error.response");
            return failed_SyncUploadResult;
        }
        return ok_SyncUploadResult;
    }
    if (status == 40 && equal_CStr(cstr_String(meta), "stale")) {
        return stale_SyncUploadResult;
    }
    setServerError_Sync_(d, status, meta);
    return failed_SyncUploadResult;
}

static iSyncData *download_Sync_(iSync *d) {
    uint64_t gen   = 0;
    iBlock  *plain = NULL;
    switch (downloadArchive_Sync_(d, full_SyncArchive, d->base->generation, &gen, &plain)) {
        case unchanged_SyncDownloadResult:
            return copy_SyncData(d->base);
        case failed_SyncDownloadResult:
            return NULL;
        default:
            break;
    }
    iSyncData *remote = new_SyncData();
    if (!plain) {
        return remote; /* nothing stored yet */
    }
    if (!deserialize_SyncData(remote, plain)) {
        if (isOutdated_SyncData(plain)) {
            /* Replaced like on the first sync, so nothing is deleted. */
            delete_SyncData(remote);
            remote             = new_SyncData();
            remote->generation = gen;
            delete_SyncData(d->base);
            d->base = new_SyncData();
        }
        else {
            setError_Sync_(d, "sync.error.decrypt");
            delete_SyncData(remote);
            remote = NULL;
        }
    }
    else if (remote->generation != gen) {
        setError_Sync_(d, "sync.error.rollback");
        delete_SyncData(remote);
        remote = NULL;
    }
    delete_Block(plain);
    return remote;
}

static enum iSyncUploadResult upload_Sync_(iSync *d, iSyncData *data, uint64_t baseGeneration) {
    data->generation = baseGeneration + 1;
    iBlock *archive = serialize_SyncData(data);
    const enum iSyncUploadResult result =
        uploadArchive_Sync_(d, full_SyncArchive, archive, baseGeneration, &data->generation);
    delete_Block(archive);
    return result;
}

static iSyncRecent *downloadRecent_Sync_(iSync *d) {
    uint64_t gen   = 0;
    iBlock  *plain = NULL;
    switch (downloadArchive_Sync_(d, recent_SyncArchive, d->recent->generation, &gen, &plain)) {
        case unchanged_SyncDownloadResult:
            return copy_SyncRecent(d->recent);
        case failed_SyncDownloadResult:
            return NULL;
        default:
            break;
    }
    iSyncRecent *remote = new_SyncRecent();
    if (!plain) {
        return remote;
    }
    if (!deserialize_SyncRecent(remote, plain)) {
        /* An unknown format is replaced, since the full sync has the data, too. */
        fprintf(stderr, "[Sync] recent changes in unknown format\n");
        remote->generation = gen;
    }
    else if (remote->generation != gen) {
        setError_Sync_(d, "sync.error.rollback");
        delete_SyncRecent(remote);
        remote = NULL;
    }
    delete_Block(plain);
    return remote;
}

static enum iSyncUploadResult uploadRecent_Sync_(iSync *d, iSyncRecent *recent,
                                                 uint64_t baseGeneration) {
    recent->generation = baseGeneration + 1;
    iBlock *archive = serialize_SyncRecent(recent);
    const enum iSyncUploadResult result =
        uploadArchive_Sync_(d, recent_SyncArchive, archive, baseGeneration, &recent->generation);
    delete_Block(archive);
    return result;
}

static void stagePristine_Sync_(iSync *d, const iSyncData *pristine) {
    /* Committed when the result is applied. */
    iBlock *data = serialize_SyncData(pristine);
    d->isPristineStaged = writeTempFile_Sync_(d, pristineFileName_Sync_, data);
    d->newGeneration    = pristine->generation;
    delete_Block(data);
}

static iThreadResult run_Sync_(iThread *thread) {
    iSync *d = &sync_;
    iUnused(thread);
    d->base = loadPristine_Sync_(d);
    d->snapshot->generation    = d->base->generation;
    d->snapshot->maxBookmarkId = d->base->maxBookmarkId;
    int attempt;
    for (attempt = 0; attempt < maxAttempts_Sync_; attempt++) {
        clear_Array(&d->idMap);
        iSyncData *remote = download_Sync_(d);
        if (!remote) {
            break;
        }
        iSyncData *merged = merge_SyncData(d->base, d->snapshot, remote, &d->identity, &d->idMap);
        if (equal_SyncData(merged, remote)) {
            /* Nothing new to upload. */
            merged->generation = remote->generation;
            d->result    = merged;
            d->didUpload = iTrue;
            stagePristine_Sync_(d, remote);
            delete_SyncData(remote);
            break;
        }
        const enum iSyncUploadResult up = upload_Sync_(d, merged, remote->generation);
        if (up == stale_SyncUploadResult) {
            /* Someone else uploaded in the meantime. */
            delete_SyncData(merged);
            delete_SyncData(remote);
            continue;
        }
        d->result = merged;
        if (up == ok_SyncUploadResult) {
            d->didUpload = iTrue;
            stagePristine_Sync_(d, merged);
        }
        else {
            /* Local changes remain pending. */
            stagePristine_Sync_(d, remote);
        }
        delete_SyncData(remote);
        break;
    }
    if (attempt == maxAttempts_Sync_) {
        setError_Sync_(d, "sync.error.busy");
    }
    delete_SyncData(d->base);
    d->base = NULL;
    postCommand_App("sync.worker.finished");
    return 0;
}

static iThreadResult runRecent_Sync_(iThread *thread) {
    iSync *d = &sync_;
    iUnused(thread);
    const int64_t cutoff = recentCutoff_Sync_();
    int attempt;
    for (attempt = 0; attempt < maxAttempts_Sync_; attempt++) {
        iSyncRecent *remote = downloadRecent_Sync_(d);
        if (!remote) {
            break;
        }
        iSyncRecent *merged = copy_SyncRecent(remote);
        if (d->outgoing) {
            merge_SyncRecent(merged, d->outgoing);
        }
        prune_SyncRecent(merged, cutoff);
        /* Old changes alone are not a reason to upload. */
        iSyncRecent *current = copy_SyncRecent(remote);
        prune_SyncRecent(current, cutoff);
        const iBool isSame = equal_SyncRecent(merged, current);
        delete_SyncRecent(current);
        if (isSame) {
            delete_SyncRecent(merged);
            d->recentResult = remote;
            d->didUpload    = iTrue;
            break;
        }
        const enum iSyncUploadResult up = uploadRecent_Sync_(d, merged, remote->generation);
        if (up == stale_SyncUploadResult) {
            delete_SyncRecent(merged);
            delete_SyncRecent(remote);
            continue;
        }
        if (up == ok_SyncUploadResult) {
            d->recentResult = merged;
            d->didUpload    = iTrue;
            delete_SyncRecent(remote);
        }
        else {
            d->recentResult = remote; /* the remote changes can still be applied */
            delete_SyncRecent(merged);
        }
        break;
    }
    if (attempt == maxAttempts_Sync_) {
        setError_Sync_(d, "sync.error.busy");
    }
    if (!d->isBlocking) {
        postCommand_App("sync.worker.finished");
    }
    return 0;
}

/*----------------------------------------------------------------------------------------------*/
/* Starting and finishing */

static uint32_t timerCallback_Sync_(void *userdata, SDL_TimerID timerId, uint32_t interval) {
    iUnused(userdata, timerId);
    postCommand_App("sync.check");
    postCommand_App("sync.recent");
    return interval;
}

static uint32_t recentTimerCallback_Sync_(void *userdata, SDL_TimerID timerId, uint32_t interval) {
    iUnused(userdata, timerId, interval);
    postCommand_App("sync.recent push:1");
    return 0;
}

static uint32_t retryCallback_Sync_(void *userdata, SDL_TimerID timerId, uint32_t interval) {
    iUnused(userdata, timerId, interval);
    sync_.retryTimer = 0;
    postCommand_App(sync_.isManual ? "sync.now" : "sync.check");
    return 0;
}

static void updateTimer_Sync_(iSync *d) {
    const iBool wantTimer = isConfigured_Sync_() && prefs_Sync_()->syncInterval > 0;
    if (wantTimer && !d->timer) {
        d->timer = SDL_AddTimer(timerInterval_Sync_ * 1000, timerCallback_Sync_, NULL);
    }
    else if (!wantTimer && d->timer) {
        SDL_RemoveTimer(d->timer);
        d->timer = 0;
    }
}

static iBool isEditorOpen_Sync_(void) {
    iConstForEach(PtrArray, i, regularWindows_App()) {
        const iWindow *win = i.ptr;
        iForIndices(r, win->roots) {
            if (!win->roots[r]) {
                continue;
            }
            iConstForEach(ObjectList, c, children_Widget(win->roots[r]->widget)) {
                const iString *id = id_Widget(c.object);
                if (startsWith_String(id, "bmed") || startsWith_String(id, "feedcfg") ||
                    startsWith_String(id, "sitespec")) {
                    return iTrue;
                }
            }
        }
    }
    return iFalse;
}

static iGmIdentity *syncIdentity_Sync_(void) {
    const iString *fp = identity_Sync_();
    return isEmpty_String(fp) ? NULL
                              : findIdentity_GmCerts(
                                    certs_App(), collect_Block(hexDecode_Rangecc(range_String(fp))));
}

static void notifyStatus_Sync_(void) {
    notify_App("sync.status.changed");
}

static void fail_Sync_(iSync *d, const char *error) {
    set_String(&d->lastError, collectNewCStr_String(error));
    saveState_Sync_(d);
    notifyStatus_Sync_();
    if (d->isManual) {
        makeSimpleMessage_Widget(uiTextCaution_ColorEscape "${heading.sync.failed}", error);
    }
}

static void promptTrust_Sync_(iSync *d) {
    uint16_t port;
    setRange_String(&d->trustHost, host_Sync_(d, &port));
    d->trustPort = port;
    delete_Block(d->trustFingerprint);
    d->trustFingerprint = copy_Block(d->serverFingerprint);
    d->trustValidUntil  = d->serverValidUntil;
    delete_Block(d->declinedFingerprint);
    d->declinedFingerprint = copy_Block(d->serverFingerprint);
    iWidget *old = findWidget_App("sync.trust");
    if (old) {
        destroyDialog_Widget(old);
    }
    iBlock  *trusted = trustedFingerprint_GmCerts(certs_App(), range_String(&d->trustHost), port);
    iString *msg     = collectNew_String();
    if (trusted) {
        format_String(msg,
                      "%s\n\n%s\n" uiTextStrong_ColorEscape "%s\n\n" uiText_ColorEscape
                      "%s\n" uiTextStrong_ColorEscape "%s",
                      cstr_Lang("dlg.sync.untrusted.changed"),
                      cstr_Lang("dlg.sync.fp.trusted"),
                      cstrCollect_String(fingerprintText_GmCerts(trusted)),
                      cstr_Lang("dlg.sync.fp.server"),
                      cstrCollect_String(fingerprintText_GmCerts(d->trustFingerprint)));
        delete_Block(trusted);
    }
    else {
        format_String(msg,
                      "%s\n\n%s\n" uiTextStrong_ColorEscape "%s",
                      cstr_Lang("dlg.sync.untrusted.unknown"),
                      cstr_Lang("dlg.sync.fp.server"),
                      cstrCollect_String(fingerprintText_GmCerts(d->trustFingerprint)));
    }
    appendFormat_String(msg, "\n\n" uiText_ColorEscape "%s", cstr_Lang("dlg.sync.untrusted.check"));
    /* Cancel is the default, since trusting needs a deliberate choice. */
    iArray *items = collectNew_Array(sizeof(iMenuItem));
    if (!isTerminal_Platform()) {
        pushBackN_Array(items, (iMenuItem[]){
            { "${dlg.sync.copyfp}", 0, 0, "sync.copyfingerprint" },
            { "---" } }, 2);
    }
    pushBackN_Array(items, (iMenuItem[]){
        { uiTextAction_ColorEscape "${dlg.cert.trust}", 0, 0, "!sync.trustcert" },
        { "${cancel}", SDLK_RETURN, 0, "cancel" } }, 2);
    iWidget *dlg = makeQuestion_Widget(uiTextCaution_ColorEscape "${heading.sync.untrusted}",
                                       cstr_String(msg),
                                       constData_Array(items),
                                       size_Array(items));
    addAction_Widget(dlg, SDLK_ESCAPE, 0, "cancel");
    setId_Widget(dlg, "sync.trust");
}

static iBool canSyncRecent_Sync_(const iSync *d) {
    /* Recent changes are synced after the first full sync. */
    return isConfigured_Sync_() && d->generation > 0 &&
           equal_String(&d->syncedServer, server_Sync_()) &&
           equal_String(&d->syncedIdentity, identity_Sync_());
}

static iBool beginWork_Sync_(iSync *d) {
    const iGmIdentity *ident = syncIdentity_Sync_();
    iBlock *key = ident ? key_SyncData(ident->cert) : NULL;
    if (!key) {
        return iFalse;
    }
    d->key  = key;
    d->cert = copy_TlsCertificate(ident->cert);
    set_String(&d->url, server_Sync_());
    if (!startsWith_String(&d->url, "gemini://")) {
        prependCStr_String(&d->url, "gemini://");
    }
    set_String(&d->identity, identity_Sync_());
    initCurrent_Time(&d->snapshotTime);
    d->didContact = iFalse;
    d->didUpload  = iFalse;
    clear_String(&d->error);
    delete_Block(d->serverFingerprint);
    d->serverFingerprint = NULL;
    return iTrue;
}

static void endWork_Sync_(iSync *d) {
    if (d->worker) {
        join_Thread(d->worker);
        iReleasePtr(&d->worker);
    }
    delete_Block(d->key);
    d->key = NULL;
    delete_TlsCertificate(d->cert);
    d->cert = NULL;
}

static void startRecent_Sync_(iSync *d, iBool isBlocking);

static void start_Sync_(iSync *d, iBool isManual) {
    if (value_Atomic(&d->isRunning)) {
        return;
    }
    if (d->worker) {
        /* Recent changes are being synced; the full sync follows. */
        d->isFullPending = iTrue;
        d->isFullPendingManual |= isManual;
        return;
    }
    d->isManual = isManual;
    if (!isConfigured_Sync_()) {
        if (isManual) {
            makeSimpleMessage_Widget(uiHeading_ColorEscape "${heading.sync}", "${sync.status.setup}");
        }
        return;
    }
    if (isEditorOpen_Sync_()) {
        if (isManual) {
            makeSimpleMessage_Widget(uiHeading_ColorEscape "${heading.sync}",
                                     "${sync.error.editoropen}");
        }
        else if (!d->retryTimer) {
            d->retryTimer = SDL_AddTimer(retryDelay_Sync_ * 1000, retryCallback_Sync_, NULL);
        }
        return;
    }
    if (isRefreshing_Feeds() || isFetchingRemote_Bookmarks(bookmarks_App())) {
        if (!d->retryTimer) {
            d->retryTimer = SDL_AddTimer(retryDelay_Sync_ * 1000, retryCallback_Sync_, NULL);
        }
        return;
    }
    if (!equal_String(&d->syncedServer, server_Sync_()) ||
        !equal_String(&d->syncedIdentity, identity_Sync_())) {
        reset_Sync_(d);
    }
    if (!d->isFullPending && canSyncRecent_Sync_(d)) {
        /* Recent changes first, so the full merge includes them. */
        d->isFullPending       = iTrue;
        d->isFullPendingManual = isManual;
        startRecent_Sync_(d, iFalse);
        if (d->worker) {
            return;
        }
    }
    d->isFullPending       = iFalse;
    d->isFullPendingManual = iFalse;
    initCurrent_Time(&d->lastAttemptTime);
    if (!beginWork_Sync_(d)) {
        fail_Sync_(d, cstr_Lang("sync.error.identity"));
        return;
    }
    set_Atomic(&d->isRunning, iTrue);
    d->mode = full_SyncMode;
    discardRemote_Bookmarks(bookmarks_App());
    d->snapshot = takeSnapshot_Sync_(d);
    clear_Array(&d->idMap);
    notifyStatus_Sync_();
    notify_App("sync.started");
    d->worker = new_Thread(run_Sync_);
    setName_Thread(d->worker, "Sync");
    start_Thread(d->worker);
}

static void trust_Sync_(iSync *d) {
    if (!d->trustFingerprint) {
        return;
    }
    setTrusted_GmCerts(certs_App(),
                       range_String(&d->trustHost),
                       d->trustPort,
                       d->trustFingerprint,
                       &d->trustValidUntil);
    delete_Block(d->trustFingerprint);
    d->trustFingerprint = NULL;
    start_Sync_(d, iTrue);
}

static void finish_Sync_(iSync *d) {
    endWork_Sync_(d);
    if (d->result) {
        apply_Sync_(d, d->result, &d->idMap);
    }
    if (d->isPristineStaged) {
        commitFile_Sync_(d, pristineFileName_Sync_);
        d->generation       = d->newGeneration;
        d->isPristineStaged = iFalse;
    }
    if (d->didContact) {
        initCurrent_Time(&d->lastCheckTime);
    }
    const iBool isError = !isEmpty_String(&d->error);
    if (!isError) {
        initCurrent_Time(&d->lastSyncTime);
        clear_String(&d->lastError);
    }
    else {
        set_String(&d->lastError, &d->error);
    }
    if (d->didUpload) {
        lock_Mutex(d->mtx);
        d->syncedTime = d->snapshotTime;
        unlock_Mutex(d->mtx);
    }
    delete_SyncData(d->result);
    d->result = NULL;
    delete_SyncData(d->snapshot);
    d->snapshot = NULL;
    set_Atomic(&d->isRunning, iFalse);
    saveState_Sync_(d);
    fetchRemote_Bookmarks(bookmarks_App());
    notify_App("sync.finished");
    notifyStatus_Sync_();
    if (d->serverFingerprint) {
        /* Automatic syncs only ask once about the same certificate. */
        if (d->isManual || !d->declinedFingerprint ||
            cmp_Block(d->serverFingerprint, d->declinedFingerprint)) {
            promptTrust_Sync_(d);
        }
    }
    else if (isError && d->isManual) {
        makeSimpleMessage_Widget(uiTextCaution_ColorEscape "${heading.sync.failed}",
                                 cstr_String(&d->lastError));
    }
}

static void check_Sync_(iSync *d) {
    if (!isConfigured_Sync_() || value_Atomic(&d->isRunning) || prefs_Sync_()->syncInterval <= 0) {
        return;
    }
    if (isValid_Time(&d->lastAttemptTime) && !isEmpty_String(&d->lastError) &&
        elapsedSeconds_Time(&d->lastAttemptTime) < failureBackoff_Sync_) {
        return;
    }
    if (isValid_Time(&d->lastCheckTime) &&
        elapsedSeconds_Time(&d->lastCheckTime) < prefs_Sync_()->syncInterval * 3600.0) {
        return;
    }
    start_Sync_(d, iFalse);
}

/*----------------------------------------------------------------------------------------------*/
/* Recent changes */

static void finishRecent_Sync_(iSync *d) {
    endWork_Sync_(d);
    if (d->recentResult) {
        applyRecent_Sync_(d, d->recentResult);
        delete_SyncRecent(d->recent);
        d->recent       = d->recentResult;
        d->recentResult = NULL;
        saveRecent_Sync_(d);
    }
    if (d->didContact) {
        initCurrent_Time(&d->recentCheckTime);
    }
    set_String(&d->recentError, &d->error);
    if (isEmpty_String(&d->error)) {
        iZap(d->recentFailTime);
    }
    else {
        initCurrent_Time(&d->recentFailTime);
    }
    if (d->didUpload) {
        iGuardMutex(d->mtx, d->recentSyncedTime = d->snapshotTime);
    }
    delete_SyncRecent(d->outgoing);
    d->outgoing = NULL;
    saveState_Sync_(d);
    notifyStatus_Sync_();
#if defined (iPlatformAppleMobile)
    if (d->isBackgroundTask) {
        d->isBackgroundTask = iFalse;
        endBackgroundTask_iOS();
    }
#endif
    if (d->isBlocking) {
        return;
    }
    if (d->isFullPending) {
        start_Sync_(d, d->isFullPendingManual);
    }
    else if (isRecentDirty_Sync_(d) && !d->recentTimer) {
        /* Changed during the sync. */
        d->recentTimer = SDL_AddTimer(recentDelay_Sync_ * 1000, recentTimerCallback_Sync_, NULL);
    }
}

static void startRecent_Sync_(iSync *d, iBool isBlocking) {
    if (d->worker || value_Atomic(&d->isRunning) || !beginWork_Sync_(d)) {
        return;
    }
    d->mode       = recent_SyncMode;
    d->isBlocking = isBlocking;
    initCurrent_Time(&d->recentAttemptTime);
    d->outgoing   = isRecentDirty_Sync_(d) ? takeRecentSnapshot_Sync_(d) : NULL;
    if (isBlocking) {
        runRecent_Sync_(NULL);
        finishRecent_Sync_(d);
        d->isBlocking = iFalse;
        return;
    }
    d->worker = new_Thread(runRecent_Sync_);
    setName_Thread(d->worker, "SyncRecent");
    start_Thread(d->worker);
    notifyStatus_Sync_();
}

static void checkRecent_Sync_(iSync *d, iBool isPush) {
    if (!canSyncRecent_Sync_(d)) {
        return;
    }
    if (d->worker || value_Atomic(&d->isRunning)) {
        if (isPush && !d->recentTimer) {
            d->recentTimer =
                SDL_AddTimer(recentDelay_Sync_ * 1000, recentTimerCallback_Sync_, NULL);
        }
        return;
    }
    if (isValid_Time(&d->recentFailTime) &&
        elapsedSeconds_Time(&d->recentFailTime) < recentBackoff_Sync_) {
        return;
    }
    if (!isPush && isValid_Time(&d->recentAttemptTime) &&
        elapsedSeconds_Time(&d->recentAttemptTime) < recentInterval_Sync_) {
        return;
    }
    startRecent_Sync_(d, iFalse);
}

static void createIdentity_Sync_(const iString *name) {
    iDate until;
    initCurrent_Date(&until);
    until.year += 20;
    iString *commonName = collect_String(copy_String(name));
    trim_String(commonName);
    if (isEmpty_String(commonName)) {
        setCStr_String(commonName, cstr_Lang("sync.identity.name"));
    }
    iString *empty = collectNew_String();
    iGmIdentity *ident = newIdentity_GmCerts(certs_App(),
                                             0,
                                             ed25519_TlsCertificateKeyType,
                                             until,
                                             commonName,
                                             empty, empty, empty, empty, empty);
    if (!ident) {
        makeSimpleMessage_Widget(uiTextCaution_ColorEscape "${heading.sync.failed}",
                                 "${sync.error.newidentity}");
        return;
    }
    setCStr_String(&ident->notes, cstr_Lang("sync.identity.notes"));
    postCommand_App("idents.changed");
    postCommandf_App("sync.identity fp:%s",
                     cstrCollect_String(hexEncode_Block(&ident->fingerprint)));
    notify_App("sync.identities.changed");
}

/*----------------------------------------------------------------------------------------------*/

void init_Sync(const char *dataDir) {
    iSync *d = &sync_;
    iZap(*d);
    d->mtx = new_Mutex();
    initCStr_String(&d->dir, concatPath_CStr(dataDir, "sync"));
    init_String(&d->lastError);
    init_String(&d->syncedServer);
    init_String(&d->syncedIdentity);
    init_String(&d->url);
    init_String(&d->identity);
    init_String(&d->error);
    init_String(&d->recentError);
    init_String(&d->trustHost);
    init_Array(&d->idMap, sizeof(iSyncIdMapping));
    d->recent      = new_SyncRecent();
    d->recentLocal = new_SyncRecent();
    loadState_Sync_(d);
    setUserDataChangedFunc_Core(userDataChanged_Sync_);
    setVisitedUrlChangedFunc_Core(visitedUrlChanged_Sync_);
    /* Prefs are loaded via the event queue, so this is handled after them. */
    postCommand_App("sync.prefs.changed launch:1");
}

void deinit_Sync(void) {
    iSync *d = &sync_;
    if (d->timer) {
        SDL_RemoveTimer(d->timer);
    }
    if (d->retryTimer) {
        SDL_RemoveTimer(d->retryTimer);
    }
    if (d->recentTimer) {
        SDL_RemoveTimer(d->recentTimer);
    }
    if (d->worker) {
        /* The result is discarded; the local changes are still pending. */
        join_Thread(d->worker);
        iReleasePtr(&d->worker);
    }
#if defined (iPlatformDesktop)
    else if (isRecentDirty_Sync_(d) && canSyncRecent_Sync_(d)) {
        /* Last chance to upload the recent changes. */
        d->requestTimeout = quitTimeout_Sync_;
        startRecent_Sync_(d, iTrue);
    }
#endif
    setVisitedUrlChangedFunc_Core(NULL);
    setUserDataChangedFunc_Core(NULL);
    saveState_Sync_(d);
    saveRecent_Sync_(d);
    delete_SyncData(d->result);
    delete_SyncData(d->snapshot);
    delete_SyncData(d->base);
    delete_SyncRecent(d->recentResult);
    delete_SyncRecent(d->outgoing);
    delete_SyncRecent(d->recentLocal);
    delete_SyncRecent(d->recent);
    delete_Block(d->key);
    delete_TlsCertificate(d->cert);
    deinit_Array(&d->idMap);
    deinit_String(&d->error);
    deinit_String(&d->recentError);
    deinit_String(&d->trustHost);
    delete_Block(d->serverFingerprint);
    delete_Block(d->trustFingerprint);
    delete_Block(d->declinedFingerprint);
    deinit_String(&d->identity);
    deinit_String(&d->url);
    deinit_String(&d->syncedIdentity);
    deinit_String(&d->syncedServer);
    deinit_String(&d->lastError);
    deinit_String(&d->dir);
    delete_Mutex(d->mtx);
    iZap(*d);
}

iBool isEnabled_Sync(void) {
    return isConfigured_Sync_();
}

iBool isRunning_Sync(void) {
    return value_Atomic(&sync_.isRunning) != 0;
}

iTime lastCheckTime_Sync(void) {
    /* Includes the recent changes, which have the reading positions. */
    iTime t = sync_.lastCheckTime;
    max_Time(&t, &sync_.recentCheckTime);
    return t;
}

void enterBackground_Sync(void) {
    /* Recent changes are uploaded before the app is suspended. */
    iSync *d = &sync_;
    if (!d->mtx || d->worker || !isRecentDirty_Sync_(d) || !canSyncRecent_Sync_(d)) {
        return;
    }
#if defined (iPlatformAppleMobile)
    beginBackgroundTask_iOS();
    d->isBackgroundTask = iTrue;
#endif
    startRecent_Sync_(d, iFalse);
#if defined (iPlatformAppleMobile)
    if (!d->worker && d->isBackgroundTask) {
        d->isBackgroundTask = iFalse;
        endBackgroundTask_iOS();
    }
#endif
}

iString *statusText_Sync(void) {
    iSync   *d   = &sync_;
    iString *str = new_String();
    if (!prefs_Sync_()->sync) {
        setCStr_String(str, cstr_Lang("sync.status.off"));
        return str;
    }
    if (!isConfigured_Sync_()) {
        setCStr_String(str, cstr_Lang("sync.status.setup"));
        return str;
    }
    if (isRunning_Sync()) {
        setCStr_String(str, cstr_Lang("sync.status.running"));
        return str;
    }
    if (isValid_Time(&d->lastSyncTime)) {
        format_String(str, "%s %s", cstr_Lang("sync.status.last"),
                      cstrCollect_String(format_Time(&d->lastSyncTime, "%Y-%m-%d %H:%M")));
        if (d->generation) {
            appendFormat_String(str, " (%s %llu)", cstr_Lang("sync.status.generation"),
                                (unsigned long long) d->generation);
        }
    }
    else {
        setCStr_String(str, cstr_Lang("sync.status.never"));
    }
    if (isDirty_Sync_(d)) {
        appendFormat_String(str, "\n%s", cstr_Lang("sync.status.pending"));
    }
    if (!isEmpty_String(&d->lastError)) {
        appendFormat_String(str, "\n" uiTextCaution_ColorEscape "%s %s", cstr_Lang("sync.status.error"),
                            cstr_String(&d->lastError));
    }
    /* Recent changes are synced quietly, so their status is only shown here. */
    if (canSyncRecent_Sync_(d)) {
        if (d->worker && d->mode == recent_SyncMode) {
            appendFormat_String(str, "\n\n%s", cstr_Lang("sync.status.recent.running"));
        }
        else if (isValid_Time(&d->recentCheckTime)) {
            appendFormat_String(str, "\n\n%s %s", cstr_Lang("sync.status.recent.last"),
                                cstrCollect_String(format_Time(&d->recentCheckTime,
                                                               "%Y-%m-%d %H:%M")));
        }
        if (isRecentDirty_Sync_(d)) {
            appendFormat_String(str, "\n%s", cstr_Lang("sync.status.recent.pending"));
        }
        if (!isEmpty_String(&d->recentError)) {
            appendFormat_String(str, "\n" uiTextCaution_ColorEscape "%s %s",
                                cstr_Lang("sync.status.error"), cstr_String(&d->recentError));
        }
    }
    return str;
}

iBool handleCommand_Sync(const char *cmd) {
    iSync *d = &sync_;
    if (!d->mtx) {
        return iFalse;
    }
    if (equal_Command(cmd, "bookmarks.changed")) {
        if (!argLabel_Command(cmd, "remote") && !argLabel_Command(cmd, "sync")) {
            userDataChanged_Sync_(bookmarks_UserDataType);
        }
        return iFalse;
    }
    if (equal_Command(cmd, "window.focus.gained")) {
        checkRecent_Sync_(d, iFalse);
        return iFalse;
    }
    if (!startsWith_CStr(cmd, "sync.")) {
        return iFalse;
    }
    if (equal_Command(cmd, "sync.now")) {
        start_Sync_(d, iTrue);
        return iTrue;
    }
    if (equal_Command(cmd, "sync.check")) {
        check_Sync_(d);
        return iTrue;
    }
    if (equal_Command(cmd, "sync.recent")) {
        const iBool isPush = argLabel_Command(cmd, "push") != 0;
        if (isPush) {
            d->recentTimer = 0; /* the timer has fired */
        }
        checkRecent_Sync_(d, isPush);
        return iTrue;
    }
    if (equal_Command(cmd, "sync.recent.changed")) {
        set_Atomic(&d->isRecentChangePosted, iFalse);
        notifyStatus_Sync_();
        if (canSyncRecent_Sync_(d) && !d->recentTimer) {
            d->recentTimer =
                SDL_AddTimer(recentDelay_Sync_ * 1000, recentTimerCallback_Sync_, NULL);
        }
        return iTrue;
    }
    if (equal_Command(cmd, "sync.worker.finished")) {
        if (d->mode == recent_SyncMode) {
            finishRecent_Sync_(d);
        }
        else {
            finish_Sync_(d);
        }
        return iTrue;
    }
    if (equal_Command(cmd, "sync.prefs.changed")) {
        if (!prefs_Sync_()->sync && !argLabel_Command(cmd, "launch") &&
            (isValid_Time(&d->lastAttemptTime) || d->generation)) {
            reset_Sync_(d); /* syncing again will be like the first time */
        }
        updateTimer_Sync_(d);
        notifyStatus_Sync_();
        check_Sync_(d);
        return iTrue;
    }
    if (equal_Command(cmd, "sync.copyfingerprint")) {
        if (d->trustFingerprint) {
            SDL_SetClipboardText(cstrCollect_String(hexEncode_Block(d->trustFingerprint)));
        }
        return iTrue;
    }
    if (equal_Command(cmd, "sync.trustcert")) {
        trust_Sync_(d);
        return iTrue;
    }
    if (equal_Command(cmd, "sync.newidentity")) {
        if (suffixPtr_Command(cmd, "value")) {
            createIdentity_Sync_(collect_String(suffix_Command(cmd, "value")));
        }
        else {
            makeSyncIdentityCreation_Widget();
        }
        return iTrue;
    }
    return iFalse;
}
