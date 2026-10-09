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

#pragma once

#include <lagrange/core.h>
#include <the_Foundation/array.h>
#include <the_Foundation/sortedarray.h>
#include <the_Foundation/stringarray.h>
#include <the_Foundation/stringhash.h>
#include <the_Foundation/time.h>
#include <the_Foundation/tlsrequest.h>

/* User data in a device-independent form for syncing. Each data type is a set of records
   identified by a key. Record fields are strings. */

enum iSyncBookmarkField {
    url_SyncBookmarkField,
    title_SyncBookmarkField,
    tags_SyncBookmarkField,
    icon_SyncBookmarkField,
    created_SyncBookmarkField,
    originalUrl_SyncBookmarkField,
    notes_SyncBookmarkField,
    identity_SyncBookmarkField,
    parent_SyncBookmarkField,
    order_SyncBookmarkField,
};

enum iSyncBookField {
    title_SyncBookField,
    author_SyncBookField,
    url_SyncBookField,
    chapter_SyncBookField,
    chapters_SyncBookField,
    pos_SyncBookField,
    timestamp_SyncBookField,
    positionTime_SyncBookField,
};

enum iSyncVisitedField {
    when_SyncVisitedField,
    flags_SyncVisitedField,
};

enum iSyncIdentityField {
    icon_SyncIdentityField,
    notes_SyncIdentityField,
    useUrls_SyncIdentityField, /* newline-separated, sorted */
    cert_SyncIdentityField,    /* PEM */
    key_SyncIdentityField,     /* PEM */
};

enum iSyncTrustedField {
    until_SyncTrustedField,
    fingerprint_SyncTrustedField,
};

enum iSyncSnippetField {
    content_SyncSnippetField,
};

/* Site-specific settings: one record per site and setting, keyed "<site> <setting>". */
enum iSyncSiteField {
    value_SyncSiteField, /* TOML value as written in sitespec.ini */
};

/*----------------------------------------------------------------------------------------------*/

iDeclareClass(SyncRecord)
iDeclareObjectConstructionArgs(SyncRecord, const iString *key, size_t numFields)

struct Impl_SyncRecord {
    iObject      object;
    iString      key;
    iStringArray fields;
};

const iString * field_SyncRecord        (const iSyncRecord *, size_t index);
void            setField_SyncRecord     (iSyncRecord *, size_t index, const iString *value);
void            setFieldCStr_SyncRecord (iSyncRecord *, size_t index, const char *value);
iBool           equal_SyncRecord        (const iSyncRecord *, const iSyncRecord *);

/*----------------------------------------------------------------------------------------------*/

iDeclareType(SyncData)
iDeclareTypeConstruction(SyncData)

struct Impl_SyncData {
    uint64_t     generation;
    uint32_t     maxBookmarkId; /* bookmark IDs up to this may have been used */
    iTime        modified[max_UserDataType];
    iStringHash *records[max_UserDataType];
};

iSyncData *         copy_SyncData       (const iSyncData *);
void                clear_SyncData      (iSyncData *, enum iUserDataType);
iSyncRecord *       add_SyncData        (iSyncData *, enum iUserDataType, const iString *key);
const iSyncRecord * find_SyncData       (const iSyncData *, enum iUserDataType, const iString *key);
iBool               remove_SyncData     (iSyncData *, enum iUserDataType, const iString *key);
size_t              size_SyncData       (const iSyncData *, enum iUserDataType);
iBool               equalRecords_SyncData(const iSyncData *, const iSyncData *, enum iUserDataType);
iBool               equal_SyncData      (const iSyncData *, const iSyncData *); /* all records */
uint32_t            maxBookmarkId_SyncData(const iSyncData *);

/* Archive with the data of each type in its native format, plus metadata. */
iBlock *            serialize_SyncData  (const iSyncData *);
iBool               deserialize_SyncData(iSyncData *, const iBlock *archive);
iBool               isOutdated_SyncData (const iBlock *archive); /* written by an older version */

/* Native formats used by the application's data stores: bookmarks.ini, books.ini,
   visited.txt, trusted.txt, snippets.ini, sitespec.ini. Identities use the same format as
   bookmarks and books. */
void                parseNative_SyncData(iSyncData *, enum iUserDataType, const iBlock *native);
iBlock *            native_SyncData     (const iSyncData *, enum iUserDataType);

/* Bookmark IDs. */
iDeclareType(SyncIdMapping)

struct Impl_SyncIdMapping {
    uint32_t oldId;
    uint32_t newId;
};

void                remapBookmarks_SyncData (iSyncData *, const iArray *idMappings);
uint32_t            mappedId_SyncIdMapping  (const iArray *idMappings, uint32_t id);

/**
 * Three-way merge of the local and remote data, using `base` as the common ancestor.
 * Bookmark IDs of the local data are normalized; the applied changes are appended to
 * `bookmarkIdMap_out`.
 *
 * @param protectedIdentity  Fingerprint (hex) of an identity that must never be deleted.
 *
 * @returns Merged data. The generation is not set.
 */
iSyncData *         merge_SyncData      (const iSyncData *base, const iSyncData *local,
                                         const iSyncData *remote,
                                         const iString *protectedIdentity,
                                         iArray *bookmarkIdMap_out);

/*----------------------------------------------------------------------------------------------*/

/* Recent changes that are synced quickly between full syncs: read status of feed entries and
   reading positions of books. Each item keeps only its latest change. */
enum iSyncRecentVisitFlag {
    removed_SyncRecentVisitFlag = 0x1, /* the URL was removed (e.g., marked as unread) */
};

iDeclareType(SyncRecentVisit)

struct Impl_SyncRecentVisit {
    iString  url;
    int64_t  changed;    /* when the change was made (seconds) */
    int64_t  when;       /* visit time (seconds) */
    uint16_t visitFlags; /* VisitedUrlFlags */
    uint16_t flags;      /* SyncRecentVisitFlags */
};

iDeclareType(SyncRecentBook)

struct Impl_SyncRecentBook {
    iString id;
    int64_t positionTime; /* seconds */
    int32_t chapter;
    int32_t numChapters;
    float   pos;
};

iDeclareType(SyncRecent)
iDeclareTypeConstruction(SyncRecent)

struct Impl_SyncRecent {
    uint64_t     generation;
    iSortedArray visits; /* SyncRecentVisit, by URL */
    iSortedArray books;  /* SyncRecentBook, by ID */
};

iSyncRecent *           copy_SyncRecent     (const iSyncRecent *);
void                    clear_SyncRecent    (iSyncRecent *);
void                    setVisit_SyncRecent (iSyncRecent *, const iSyncRecentVisit *); /* copied */
void                    setBook_SyncRecent  (iSyncRecent *, const iSyncRecentBook *);  /* copied */
const iSyncRecentVisit *findVisit_SyncRecent(const iSyncRecent *, const iString *url);
iBool                   equal_SyncRecent    (const iSyncRecent *, const iSyncRecent *);
void                    merge_SyncRecent    (iSyncRecent *, const iSyncRecent *other);
void                    prune_SyncRecent    (iSyncRecent *, int64_t cutoff); /* removes older changes */
iBlock *                serialize_SyncRecent(const iSyncRecent *);
iBool                   deserialize_SyncRecent(iSyncRecent *, const iBlock *data);

/*----------------------------------------------------------------------------------------------*/

enum iSyncArchive {
    full_SyncArchive,
    recent_SyncArchive,
};

/* Encryption for storing on the server. */
iBlock *            key_SyncData        (const iTlsCertificate *syncIdentity); /* NULL if not Ed25519 */
iBlock *            encrypt_SyncData    (const iBlock *archive, const iBlock *key, enum iSyncArchive);
iBlock *            decrypt_SyncData    (const iBlock *encrypted, const iBlock *key,
                                         enum iSyncArchive); /* NULL on failure */
