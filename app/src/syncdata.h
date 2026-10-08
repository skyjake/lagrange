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

#include <the_Foundation/array.h>
#include <the_Foundation/stringarray.h>
#include <the_Foundation/stringhash.h>
#include <the_Foundation/time.h>
#include <the_Foundation/tlsrequest.h>

/* User data in a device-independent form for syncing. Each data type is a set of records
   identified by a key. Record fields are strings. */

enum iSyncDataType {
    bookmarks_SyncDataType,
    books_SyncDataType,
    visited_SyncDataType,
    identities_SyncDataType,
    trusted_SyncDataType,
    snippets_SyncDataType,
    sites_SyncDataType,
    max_SyncDataType
};

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

size_t          numFields_SyncDataType  (enum iSyncDataType);

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
    iTime        modified[max_SyncDataType];
    iStringHash *records[max_SyncDataType];
};

iSyncData *         copy_SyncData       (const iSyncData *);
void                clear_SyncData      (iSyncData *, enum iSyncDataType);
iSyncRecord *       add_SyncData        (iSyncData *, enum iSyncDataType, const iString *key);
const iSyncRecord * find_SyncData       (const iSyncData *, enum iSyncDataType, const iString *key);
iBool               remove_SyncData     (iSyncData *, enum iSyncDataType, const iString *key);
size_t              size_SyncData       (const iSyncData *, enum iSyncDataType);
iBool               equalRecords_SyncData(const iSyncData *, const iSyncData *, enum iSyncDataType);
iBool               equal_SyncData      (const iSyncData *, const iSyncData *); /* all records */
uint32_t            maxBookmarkId_SyncData(const iSyncData *);

/* Archive containing all the data, plus metadata. */
iBlock *            serialize_SyncData  (const iSyncData *);
iBool               deserialize_SyncData(iSyncData *, const iBlock *archive);

/* Native formats used by the application's data stores: bookmarks.ini, books.ini,
   visited.txt, trusted.txt, snippets.ini, sitespec.ini. Not applicable to identities. */
void                parseNative_SyncData(iSyncData *, enum iSyncDataType, const iBlock *native);
iBlock *            native_SyncData     (const iSyncData *, enum iSyncDataType);

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

/* Encryption for storing on the server. */
iBlock *            key_SyncData        (const iTlsCertificate *syncIdentity); /* NULL if not Ed25519 */
iBlock *            encrypt_SyncData    (const iBlock *archive, const iBlock *key);
iBlock *            decrypt_SyncData    (const iBlock *encrypted, const iBlock *key); /* NULL on failure */
