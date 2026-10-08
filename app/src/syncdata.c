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

#include "syncdata.h"

#include <the_Foundation/archive.h>
#include <the_Foundation/buffer.h>
#include <the_Foundation/crypto.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/stringset.h>
#include <the_Foundation/toml.h>

iDeclareType(SyncFieldInfo)

struct Impl_SyncFieldInfo {
    const char *name;
    iBool       isNumeric;
};

static const iSyncFieldInfo bookmarkFields_[] = {
    { "url" }, { "title" }, { "tags" }, { "icon", iTrue }, { "created", iTrue },
    { "originalurl" }, { "notes" }, { "identity" }, { "parent", iTrue }, { "order", iTrue },
};

static const iSyncFieldInfo bookFields_[] = {
    { "title" }, { "author" }, { "url" }, { "chapter", iTrue }, { "chapters", iTrue },
    { "pos", iTrue }, { "timestamp", iTrue }, { "positiontime", iTrue },
};

static const iSyncFieldInfo visitedFields_[] = {
    { "when", iTrue }, { "flags" },
};

static const iSyncFieldInfo identityFields_[] = {
    { "icon", iTrue }, { "notes" }, { "useurls" }, { "cert" }, { "key" },
};

static const iSyncFieldInfo trustedFields_[] = {
    { "until", iTrue }, { "fingerprint" },
};

static const iSyncFieldInfo snippetFields_[] = {
    { "content" },
};

static const struct {
    const char           *name;
    const iSyncFieldInfo *fields;
    size_t                numFields;
} types_[max_SyncDataType] = {
    { "bookmarks", bookmarkFields_, iElemCount(bookmarkFields_) },
    { "books", bookFields_, iElemCount(bookFields_) },
    { "visited", visitedFields_, iElemCount(visitedFields_) },
    { "identities", identityFields_, iElemCount(identityFields_) },
    { "trusted", trustedFields_, iElemCount(trustedFields_) },
    { "snippets", snippetFields_, iElemCount(snippetFields_) },
};

static const uint32_t formatVersion_SyncData_ = 1;
static const char    *metaEntry_SyncData_     = "sync.ini";
static const char     magic_SyncData_[5]      = { 'L', 'g', 'S', 'y', 1 /* version */ };
static const char    *keyLabel_SyncData_      = "lagrange-sync-v1";

size_t numFields_SyncDataType(enum iSyncDataType type) {
    return types_[type].numFields;
}

static int fieldIndex_SyncDataType_(enum iSyncDataType type, const iString *name) {
    for (size_t i = 0; i < types_[type].numFields; i++) {
        if (!cmp_String(name, types_[type].fields[i].name)) {
            return (int) i;
        }
    }
    return -1;
}

/*----------------------------------------------------------------------------------------------*/

void init_SyncRecord(iSyncRecord *d, const iString *key, size_t numFields) {
    initCopy_String(&d->key, key);
    init_StringArray(&d->fields);
    resize_StringArray(&d->fields, numFields);
}

void deinit_SyncRecord(iSyncRecord *d) {
    deinit_StringArray(&d->fields);
    deinit_String(&d->key);
}

iDefineObjectConstructionArgs(SyncRecord, (const iString *key, size_t numFields), key, numFields)
iDefineClass(SyncRecord)

const iString *field_SyncRecord(const iSyncRecord *d, size_t index) {
    return constAt_StringArray(&d->fields, index);
}

void setField_SyncRecord(iSyncRecord *d, size_t index, const iString *value) {
    set_StringArray(&d->fields, index, value);
}

void setFieldCStr_SyncRecord(iSyncRecord *d, size_t index, const char *value) {
    setCStr_StringArray(&d->fields, index, value);
}

iBool equal_SyncRecord(const iSyncRecord *d, const iSyncRecord *other) {
    if (size_StringArray(&d->fields) != size_StringArray(&other->fields)) {
        return iFalse;
    }
    for (size_t i = 0; i < size_StringArray(&d->fields); i++) {
        if (!equal_String(field_SyncRecord(d, i), field_SyncRecord(other, i))) {
            return iFalse;
        }
    }
    return iTrue;
}

static iSyncRecord *copy_SyncRecord_(const iSyncRecord *d) {
    iSyncRecord *copy = new_SyncRecord(&d->key, size_StringArray(&d->fields));
    for (size_t i = 0; i < size_StringArray(&d->fields); i++) {
        setField_SyncRecord(copy, i, field_SyncRecord(d, i));
    }
    return copy;
}

static int64_t number_SyncRecord_(const iSyncRecord *d, size_t index) {
    return strtoll(cstr_String(field_SyncRecord(d, index)), NULL, 10);
}

/*----------------------------------------------------------------------------------------------*/

void init_SyncData(iSyncData *d) {
    d->generation    = 0;
    d->maxBookmarkId = 0;
    iForIndices(i, d->records) {
        iZap(d->modified[i]);
        d->records[i] = new_StringHash();
    }
}

void deinit_SyncData(iSyncData *d) {
    iForIndices(i, d->records) {
        iRelease(d->records[i]);
    }
}

iDefineTypeConstruction(SyncData)

static void insert_SyncData_(iSyncData *d, enum iSyncDataType type, iSyncRecord *record) {
    insert_StringHash(d->records[type], &record->key, record);
    iRelease(record);
}

iSyncData *copy_SyncData(const iSyncData *d) {
    iSyncData *copy = new_SyncData();
    copy->generation    = d->generation;
    copy->maxBookmarkId = d->maxBookmarkId;
    iForIndices(i, d->records) {
        copy->modified[i] = d->modified[i];
        iConstForEach(StringHash, j, d->records[i]) {
            insert_SyncData_(copy, i, copy_SyncRecord_(value_StringHashNode(j.value)));
        }
    }
    return copy;
}

void clear_SyncData(iSyncData *d, enum iSyncDataType type) {
    clear_StringHash(d->records[type]);
}

iSyncRecord *add_SyncData(iSyncData *d, enum iSyncDataType type, const iString *key) {
    iSyncRecord *record = new_SyncRecord(key, types_[type].numFields);
    insert_StringHash(d->records[type], key, record);
    iRelease(record);
    return record;
}

const iSyncRecord *find_SyncData(const iSyncData *d, enum iSyncDataType type, const iString *key) {
    return constValue_StringHash(d->records[type], key);
}

iBool remove_SyncData(iSyncData *d, enum iSyncDataType type, const iString *key) {
    return remove_StringHash(d->records[type], key);
}

size_t size_SyncData(const iSyncData *d, enum iSyncDataType type) {
    return size_StringHash(d->records[type]);
}

iBool equalRecords_SyncData(const iSyncData *d, const iSyncData *other, enum iSyncDataType type) {
    if (size_StringHash(d->records[type]) != size_StringHash(other->records[type])) {
        return iFalse;
    }
    iConstForEach(StringHash, i, d->records[type]) {
        const iSyncRecord *rec = value_StringHashNode(i.value);
        const iSyncRecord *otherRec = find_SyncData(other, type, &rec->key);
        if (!otherRec || !equal_SyncRecord(rec, otherRec)) {
            return iFalse;
        }
    }
    return iTrue;
}

iBool equal_SyncData(const iSyncData *d, const iSyncData *other) {
    for (int i = 0; i < max_SyncDataType; i++) {
        if (!equalRecords_SyncData(d, other, i)) {
            return iFalse;
        }
    }
    return iTrue;
}

uint32_t maxBookmarkId_SyncData(const iSyncData *d) {
    uint32_t maxId = d->maxBookmarkId;
    iConstForEach(StringHash, i, d->records[bookmarks_SyncDataType]) {
        const iSyncRecord *rec = value_StringHashNode(i.value);
        maxId = iMax(maxId, (uint32_t) toInt_String(&rec->key));
    }
    return maxId;
}

/*----------------------------------------------------------------------------------------------*/

static iBlock *serializeRecords_SyncData_(const iSyncData *d, enum iSyncDataType type) {
    iBuffer *buf = new_Buffer();
    openEmpty_Buffer(buf);
    iStream *outs = stream_Buffer(buf);
    writeU32_Stream(outs, (uint32_t) size_StringHash(d->records[type]));
    writeU32_Stream(outs, (uint32_t) types_[type].numFields);
    iConstForEach(StringHash, i, d->records[type]) {
        const iSyncRecord *rec = value_StringHashNode(i.value);
        serialize_String(&rec->key, outs);
        iConstForEach(StringArray, f, &rec->fields) {
            serialize_String(f.value, outs);
        }
    }
    iBlock *data = copy_Block(data_Buffer(buf));
    iRelease(buf);
    return data;
}

static iBool deserializeRecords_SyncData_(iSyncData *d, enum iSyncDataType type,
                                          const iBlock *data) {
    iBuffer *buf = new_Buffer();
    open_Buffer(buf, data);
    iStream *ins = stream_Buffer(buf);
    const uint32_t count     = readU32_Stream(ins);
    const uint32_t numFields = readU32_Stream(ins);
    iString *key   = new_String();
    iString *value = new_String();
    iBool    ok    = iTrue;
    for (uint32_t i = 0; i < count; i++) {
        if (atEnd_Stream(ins)) {
            ok = iFalse;
            break;
        }
        deserialize_String(key, ins);
        iSyncRecord *rec = add_SyncData(d, type, key);
        for (uint32_t f = 0; f < numFields; f++) {
            deserialize_String(value, ins);
            /* Fields added in later versions are ignored. */
            if (f < types_[type].numFields) {
                setField_SyncRecord(rec, f, value);
            }
        }
    }
    delete_String(value);
    delete_String(key);
    iRelease(buf);
    return ok;
}

iBlock *serialize_SyncData(const iSyncData *d) {
    iArchive *arch = new_Archive();
    openWritable_Archive(arch);
    iString *meta = new_String();
    format_String(meta,
                  "format = %u\n"
                  "generation = %llu\n"
                  "maxbookmarkid = %u\n\n"
                  "[modified]\n",
                  formatVersion_SyncData_,
                  (unsigned long long) d->generation,
                  d->maxBookmarkId);
    for (int i = 0; i < max_SyncDataType; i++) {
        appendFormat_String(meta, "%s = %lld\n", types_[i].name,
                            (long long) integralSeconds_Time(&d->modified[i]));
        iBlock *data = serializeRecords_SyncData_(d, i);
        setData_Archive(arch, collectNewFormat_String("%s.bin", types_[i].name), data);
        delete_Block(data);
    }
    setDataCStr_Archive(arch, metaEntry_SyncData_, utf8_String(meta));
    delete_String(meta);
    iBuffer *buf = new_Buffer();
    openEmpty_Buffer(buf);
    serialize_Archive(arch, stream_Buffer(buf));
    iBlock *zip = copy_Block(data_Buffer(buf));
    iRelease(buf);
    iRelease(arch);
    return zip;
}

static void handleMetaKeyValue_SyncData_(void *context, const iString *table, const iString *key,
                                         const iTomlValue *value) {
    iSyncData *d = context;
    if (value->type != int64_TomlType) {
        return;
    }
    if (isEmpty_String(table)) {
        if (!cmp_String(key, "generation")) {
            d->generation = (uint64_t) value->value.int64;
        }
        else if (!cmp_String(key, "maxbookmarkid")) {
            d->maxBookmarkId = (uint32_t) value->value.int64;
        }
    }
    else if (!cmp_String(table, "modified")) {
        for (int i = 0; i < max_SyncDataType; i++) {
            if (!cmp_String(key, types_[i].name)) {
                initSeconds_Time(&d->modified[i], (double) value->value.int64);
            }
        }
    }
}

iBool deserialize_SyncData(iSyncData *d, const iBlock *archive) {
    iArchive *arch = new_Archive();
    iBool     ok   = iFalse;
    if (openData_Archive(arch, archive)) {
        const iBlock *meta = dataCStr_Archive(arch, metaEntry_SyncData_);
        if (meta) {
            iTomlParser *toml = new_TomlParser();
            setHandlers_TomlParser(toml, NULL, handleMetaKeyValue_SyncData_, d);
            ok = parse_TomlParser(toml, collect_String(newBlock_String(meta)));
            delete_TomlParser(toml);
            for (int i = 0; ok && i < max_SyncDataType; i++) {
                clear_SyncData(d, i);
                const iBlock *data =
                    dataCStr_Archive(arch, format_CStr("%s.bin", types_[i].name));
                if (data) {
                    ok = deserializeRecords_SyncData_(d, i, data);
                }
            }
        }
    }
    iRelease(arch);
    return ok;
}

/*----------------------------------------------------------------------------------------------*/

iDeclareType(NativeParser)

struct Impl_NativeParser {
    iSyncData         *data;
    enum iSyncDataType type;
    iSyncRecord       *record;
};

static void handleTable_NativeParser_(void *context, const iString *table, iBool isStart) {
    iNativeParser *d = context;
    d->record = isStart ? add_SyncData(d->data, d->type, table) : NULL;
}

static void handleKeyValue_NativeParser_(void *context, const iString *table, const iString *key,
                                         const iTomlValue *value) {
    iNativeParser *d = context;
    iUnused(table);
    if (!d->record) {
        return;
    }
    const int index = fieldIndex_SyncDataType_(d->type, key);
    if (index < 0) {
        return;
    }
    switch (value->type) {
        case string_TomlType:
            setField_SyncRecord(d->record, index, value->value.string);
            break;
        case int64_TomlType:
            setFieldCStr_SyncRecord(d->record, index,
                                    format_CStr("%lld", (long long) value->value.int64));
            break;
        case float64_TomlType:
            setFieldCStr_SyncRecord(d->record, index, format_CStr("%f", value->value.float64));
            break;
        case boolean_TomlType:
            setFieldCStr_SyncRecord(d->record, index, value->value.boolean ? "1" : "0");
            break;
    }
}

static void parseLines_SyncData_(iSyncData *d, enum iSyncDataType type, const iBlock *native) {
    const iRangecc src  = range_Block(native);
    iRangecc       line = iNullRange;
    while (nextSplit_Rangecc(src, "\n", &line)) {
        iRangecc words[3];
        iRangecc word = iNullRange;
        size_t   n    = 0;
        /* The last word extends to the end of the line. */
        while (n < 2 && nextSplit_Rangecc(line, " ", &word)) {
            words[n++] = word;
        }
        if (n < 2 || word.end >= line.end) {
            continue;
        }
        words[2] = (iRangecc){ word.end + 1, line.end };
        if (type == visited_SyncDataType) {
            /* when flags url */
            iSyncRecord *rec = add_SyncData(d, type, collectNewRange_String(words[2]));
            setField_SyncRecord(rec, when_SyncVisitedField, collectNewRange_String(words[0]));
            setFieldCStr_SyncRecord(
                rec,
                flags_SyncVisitedField,
                format_CStr("%04x", (unsigned) strtoul(cstr_Rangecc(words[1]), NULL, 16)));
        }
        else {
            /* domain until fingerprint */
            iSyncRecord *rec = add_SyncData(d, type, collectNewRange_String(words[0]));
            setField_SyncRecord(rec, until_SyncTrustedField, collectNewRange_String(words[1]));
            setField_SyncRecord(rec, fingerprint_SyncTrustedField, collectNewRange_String(words[2]));
        }
    }
}

void parseNative_SyncData(iSyncData *d, enum iSyncDataType type, const iBlock *native) {
    clear_SyncData(d, type);
    switch (type) {
        case bookmarks_SyncDataType:
        case books_SyncDataType:
        case snippets_SyncDataType: {
            iNativeParser parser = { d, type, NULL };
            iTomlParser *toml = new_TomlParser();
            setHandlers_TomlParser(
                toml, handleTable_NativeParser_, handleKeyValue_NativeParser_, &parser);
            parse_TomlParser(toml, collect_String(newBlock_String(native)));
            delete_TomlParser(toml);
            break;
        }
        case visited_SyncDataType:
        case trusted_SyncDataType:
            parseLines_SyncData_(d, type, native);
            break;
        default:
            iAssert(iFalse);
            break;
    }
}

iBlock *native_SyncData(const iSyncData *d, enum iSyncDataType type) {
    iString *out = new_String();
    iConstForEach(StringHash, i, d->records[type]) {
        const iSyncRecord *rec = value_StringHashNode(i.value);
        switch (type) {
            case visited_SyncDataType:
                appendFormat_String(out, "%s %s %s\n",
                                    cstr_String(field_SyncRecord(rec, when_SyncVisitedField)),
                                    cstr_String(field_SyncRecord(rec, flags_SyncVisitedField)),
                                    cstr_String(&rec->key));
                break;
            case trusted_SyncDataType:
                appendFormat_String(out, "%s %s %s\n",
                                    cstr_String(&rec->key),
                                    cstr_String(field_SyncRecord(rec, until_SyncTrustedField)),
                                    cstr_String(field_SyncRecord(rec, fingerprint_SyncTrustedField)));
                break;
            default:
                appendFormat_String(out, "[%s]\n", cstr_String(&rec->key));
                for (size_t f = 0; f < types_[type].numFields; f++) {
                    const iString *value = field_SyncRecord(rec, f);
                    if (isEmpty_String(value)) {
                        continue;
                    }
                    if (types_[type].fields[f].isNumeric) {
                        appendFormat_String(out, "%s = %s\n", types_[type].fields[f].name,
                                            cstr_String(value));
                    }
                    else {
                        appendFormat_String(out, "%s = \"%s\"\n", types_[type].fields[f].name,
                                            cstrCollect_String(quote_String(value, iFalse)));
                    }
                }
                appendCStr_String(out, "\n");
                break;
        }
    }
    iBlock *data = copy_Block(utf8_String(out));
    delete_String(out);
    return data;
}

/*----------------------------------------------------------------------------------------------*/

uint32_t mappedId_SyncIdMapping(const iArray *idMappings, uint32_t id) {
    iConstForEach(Array, i, idMappings) {
        const iSyncIdMapping *map = i.value;
        if (map->oldId == id) {
            return map->newId;
        }
    }
    return id;
}

void remapBookmarks_SyncData(iSyncData *d, const iArray *idMappings) {
    if (isEmpty_Array(idMappings)) {
        return;
    }
    iStringHash *remapped = new_StringHash();
    iConstForEach(StringHash, i, d->records[bookmarks_SyncDataType]) {
        const iSyncRecord *rec = value_StringHashNode(i.value);
        const uint32_t newId = mappedId_SyncIdMapping(idMappings, toInt_String(&rec->key));
        iSyncRecord *copy = new_SyncRecord(collectNewFormat_String("%u", newId),
                                           types_[bookmarks_SyncDataType].numFields);
        for (size_t f = 0; f < types_[bookmarks_SyncDataType].numFields; f++) {
            setField_SyncRecord(copy, f, field_SyncRecord(rec, f));
        }
        const uint32_t parent = (uint32_t) number_SyncRecord_(rec, parent_SyncBookmarkField);
        if (parent) {
            setFieldCStr_SyncRecord(copy, parent_SyncBookmarkField,
                                    format_CStr("%u", mappedId_SyncIdMapping(idMappings, parent)));
        }
        insert_StringHash(remapped, &copy->key, copy);
        iRelease(copy);
    }
    iRelease(d->records[bookmarks_SyncDataType]);
    d->records[bookmarks_SyncDataType] = remapped;
}

static iBool isFolder_SyncBookmark_(const iSyncRecord *d) {
    return isEmpty_String(field_SyncRecord(d, url_SyncBookmarkField));
}

static const iSyncRecord *findMatchingBookmark_(const iStringHash *base, const iStringHash *remote,
                                                const iStringSet *matched,
                                                const iSyncRecord *local, uint32_t localParent) {
    const iBool isFolder = isFolder_SyncBookmark_(local);
    iConstForEach(StringHash, i, remote) {
        const iSyncRecord *rec = value_StringHashNode(i.value);
        if (contains_StringHash(base, &rec->key) || contains_StringSet(matched, &rec->key) ||
            isFolder_SyncBookmark_(rec) != isFolder) {
            continue;
        }
        if (isFolder) {
            if (equal_String(field_SyncRecord(rec, title_SyncBookmarkField),
                             field_SyncRecord(local, title_SyncBookmarkField)) &&
                (uint32_t) number_SyncRecord_(rec, parent_SyncBookmarkField) == localParent) {
                return rec;
            }
        }
        else if (equal_String(field_SyncRecord(rec, url_SyncBookmarkField),
                              field_SyncRecord(local, url_SyncBookmarkField)) &&
                 equal_String(field_SyncRecord(rec, identity_SyncBookmarkField),
                              field_SyncRecord(local, identity_SyncBookmarkField))) {
            return rec;
        }
    }
    return NULL;
}

static void normalizeBookmarkIds_SyncData_(const iSyncData *base, iSyncData *local,
                                           const iSyncData *remote, iArray *map_out) {
    const iStringHash *B = base->records[bookmarks_SyncDataType];
    const iStringHash *L = local->records[bookmarks_SyncDataType];
    const iStringHash *R = remote->records[bookmarks_SyncDataType];
    uint32_t maxId = iMax(iMax(maxBookmarkId_SyncData(base), maxBookmarkId_SyncData(local)),
                          maxBookmarkId_SyncData(remote));
    iArray     *map     = new_Array(sizeof(iSyncIdMapping));
    iStringSet *matched = new_StringSet();
    iPtrArray  *pending = new_PtrArray();
    /* New local folders are matched with new remote folders, parents first. */
    iConstForEach(StringHash, i, L) {
        const iSyncRecord *rec = value_StringHashNode(i.value);
        if (!contains_StringHash(B, &rec->key) && isFolder_SyncBookmark_(rec)) {
            pushBack_PtrArray(pending, rec);
        }
    }
    for (iBool progress = iTrue; progress && !isEmpty_PtrArray(pending); ) {
        progress = iFalse;
        iForEach(PtrArray, i, pending) {
            const iSyncRecord *rec    = i.ptr;
            const uint32_t     parent = (uint32_t) number_SyncRecord_(rec, parent_SyncBookmarkField);
            iBool isParentPending = iFalse;
            iConstForEach(PtrArray, j, pending) {
                if ((uint32_t) toInt_String(&((const iSyncRecord *) j.ptr)->key) == parent) {
                    isParentPending = iTrue;
                    break;
                }
            }
            if (isParentPending) {
                continue;
            }
            const iSyncRecord *match = findMatchingBookmark_(
                B, R, matched, rec, mappedId_SyncIdMapping(map, parent));
            if (match) {
                pushBack_Array(map, &(iSyncIdMapping){ toInt_String(&rec->key),
                                                       toInt_String(&match->key) });
                insert_StringSet(matched, &match->key);
            }
            remove_PtrArrayIterator(&i);
            progress = iTrue;
        }
    }
    /* New local bookmarks are matched by URL and identity. */
    iConstForEach(StringHash, j, L) {
        const iSyncRecord *rec = value_StringHashNode(j.value);
        if (!contains_StringHash(B, &rec->key) && !isFolder_SyncBookmark_(rec)) {
            const iSyncRecord *match = findMatchingBookmark_(B, R, matched, rec, 0);
            if (match) {
                pushBack_Array(map, &(iSyncIdMapping){ toInt_String(&rec->key),
                                                       toInt_String(&match->key) });
                insert_StringSet(matched, &match->key);
            }
        }
    }
    /* Unmatched new local items get unused IDs if theirs may have been used elsewhere. */
    iConstForEach(StringHash, k, L) {
        const iSyncRecord *rec = value_StringHashNode(k.value);
        const uint32_t     id  = toInt_String(&rec->key);
        if (contains_StringHash(B, &rec->key) || mappedId_SyncIdMapping(map, id) != id) {
            continue;
        }
        if (contains_StringHash(R, &rec->key) || id <= maxBookmarkId_SyncData(remote)) {
            pushBack_Array(map, &(iSyncIdMapping){ id, ++maxId });
        }
    }
    remapBookmarks_SyncData(local, map);
    local->maxBookmarkId = iMax(local->maxBookmarkId, maxId);
    if (map_out) {
        pushBackN_Array(map_out, constData_Array(map), size_Array(map));
    }
    delete_PtrArray(pending);
    iRelease(matched);
    delete_Array(map);
}

/*----------------------------------------------------------------------------------------------*/

iDeclareType(SyncMerge)

struct Impl_SyncMerge {
    enum iSyncDataType type;
    iBool              preferLocal; /* local data of this type was modified more recently */
    const iString     *protectedIdentity;
};

static iBool isChanged_(const iSyncRecord *base, const iSyncRecord *rec, size_t index) {
    return !base || !equal_String(field_SyncRecord(base, index), field_SyncRecord(rec, index));
}

static const iString *mergeField_SyncMerge_(const iSyncMerge *d, const iSyncRecord *base,
                                            const iSyncRecord *local, const iSyncRecord *remote,
                                            size_t index) {
    const iString *lv = field_SyncRecord(local, index);
    const iString *rv = field_SyncRecord(remote, index);
    if (!isChanged_(base, local, index)) {
        return rv;
    }
    if (!isChanged_(base, remote, index) || equal_String(lv, rv)) {
        return lv;
    }
    return d->preferLocal ? lv : rv;
}

static iBool isAnyChanged_(const iSyncRecord *base, const iSyncRecord *rec, size_t first,
                           size_t last) {
    for (size_t i = first; i <= last; i++) {
        if (isChanged_(base, rec, i)) {
            return iTrue;
        }
    }
    return iFalse;
}

static iSyncRecord *mergeRecord_SyncMerge_(const iSyncMerge *d, const iSyncRecord *base,
                                           const iSyncRecord *local, const iSyncRecord *remote) {
    const size_t numFields = types_[d->type].numFields;
    if (d->type == trusted_SyncDataType) {
        /* The entry is updated as a whole. */
        const iBool lc = !base || !equal_SyncRecord(base, local);
        const iBool rc = !base || !equal_SyncRecord(base, remote);
        if (lc && rc) {
            return copy_SyncRecord_(number_SyncRecord_(local, until_SyncTrustedField) >
                                            number_SyncRecord_(remote, until_SyncTrustedField)
                                        ? local
                                        : remote);
        }
        return copy_SyncRecord_(lc ? local : remote);
    }
    iSyncRecord *out = new_SyncRecord(&local->key, numFields);
    for (size_t i = 0; i < numFields; i++) {
        setField_SyncRecord(out, i, mergeField_SyncMerge_(d, base, local, remote, i));
    }
    if (d->type == visited_SyncDataType) {
        const iBool isLocalLater = number_SyncRecord_(local, when_SyncVisitedField) >
                                   number_SyncRecord_(remote, when_SyncVisitedField);
        setField_SyncRecord(out, when_SyncVisitedField,
                            field_SyncRecord(isLocalLater ? local : remote, when_SyncVisitedField));
        if (isChanged_(base, local, flags_SyncVisitedField) &&
            isChanged_(base, remote, flags_SyncVisitedField)) {
            setField_SyncRecord(out, flags_SyncVisitedField,
                                field_SyncRecord(isLocalLater ? local : remote,
                                                 flags_SyncVisitedField));
        }
    }
    else if (d->type == books_SyncDataType) {
        /* The reading position is a single unit. */
        const iBool lc = isAnyChanged_(base, local, chapter_SyncBookField, positionTime_SyncBookField);
        const iBool rc = isAnyChanged_(base, remote, chapter_SyncBookField, positionTime_SyncBookField);
        const iSyncRecord *pos = remote;
        if (lc && rc) {
            const int64_t lt = number_SyncRecord_(local, positionTime_SyncBookField);
            const int64_t rt = number_SyncRecord_(remote, positionTime_SyncBookField);
            pos = lt > rt || (lt == rt && d->preferLocal) ? local : remote;
        }
        else if (lc) {
            pos = local;
        }
        for (size_t i = chapter_SyncBookField; i <= positionTime_SyncBookField; i++) {
            setField_SyncRecord(out, i, field_SyncRecord(pos, i));
        }
        setField_SyncRecord(out, timestamp_SyncBookField,
                            field_SyncRecord(number_SyncRecord_(local, timestamp_SyncBookField) >
                                                     number_SyncRecord_(remote, timestamp_SyncBookField)
                                                 ? local
                                                 : remote,
                                             timestamp_SyncBookField));
    }
    return out;
}

static iBool survivesDeletion_SyncMerge_(const iSyncMerge *d, const iSyncRecord *base,
                                         const iSyncRecord *kept) {
    if (d->type == visited_SyncDataType) {
        /* Visited again after the deletion. */
        return number_SyncRecord_(kept, when_SyncVisitedField) >
               number_SyncRecord_(base, when_SyncVisitedField);
    }
    if (d->type == identities_SyncDataType && d->protectedIdentity) {
        return equal_String(&kept->key, d->protectedIdentity);
    }
    return iFalse;
}

static void mergeKey_SyncMerge_(const iSyncMerge *d, const iStringHash *base,
                                const iStringHash *local, const iStringHash *remote,
                                const iString *key, iSyncData *out) {
    const iSyncRecord *b = constValue_StringHash(base, key);
    const iSyncRecord *l = constValue_StringHash(local, key);
    const iSyncRecord *r = constValue_StringHash(remote, key);
    iSyncRecord *merged = NULL;
    if (l && r) {
        merged = mergeRecord_SyncMerge_(d, b, l, r);
    }
    else if (!b) {
        merged = copy_SyncRecord_(l ? l : r); /* created on one side */
    }
    else if (l || r) {
        /* Deleted on one side. */
        const iSyncRecord *kept = l ? l : r;
        if (survivesDeletion_SyncMerge_(d, b, kept)) {
            merged = copy_SyncRecord_(kept);
        }
    }
    if (merged) {
        insert_SyncData_(out, d->type, merged);
    }
}

iSyncData *merge_SyncData(const iSyncData *base, const iSyncData *local, const iSyncData *remote,
                          const iString *protectedIdentity, iArray *bookmarkIdMap_out) {
    iSyncData *out = new_SyncData();
    iSyncData *normLocal = copy_SyncData(local);
    normalizeBookmarkIds_SyncData_(base, normLocal, remote, bookmarkIdMap_out);
    for (int type = 0; type < max_SyncDataType; type++) {
        const iSyncMerge merge = {
            .type              = type,
            .preferLocal       = cmp_Time(&local->modified[type], &remote->modified[type]) > 0,
            .protectedIdentity = protectedIdentity,
        };
        const iStringHash *B = base->records[type];
        const iStringHash *L = normLocal->records[type];
        const iStringHash *R = remote->records[type];
        iConstForEach(StringHash, i, L) {
            mergeKey_SyncMerge_(&merge, B, L, R, key_StringHashConstIterator(&i), out);
        }
        iConstForEach(StringHash, j, R) {
            const iString *key = key_StringHashConstIterator(&j);
            if (!contains_StringHash(L, key)) {
                mergeKey_SyncMerge_(&merge, B, L, R, key, out);
            }
        }
        iConstForEach(StringHash, k, B) {
            const iString *key = key_StringHashConstIterator(&k);
            if (!contains_StringHash(L, key) && !contains_StringHash(R, key)) {
                mergeKey_SyncMerge_(&merge, B, L, R, key, out);
            }
        }
        out->modified[type] = local->modified[type];
        max_Time(&out->modified[type], &remote->modified[type]);
    }
    /* Bookmarks whose folder was deleted are moved to the top level. */
    iForEach(StringHash, m, out->records[bookmarks_SyncDataType]) {
        iSyncRecord *rec = value_StringHashNode(m.value);
        const iString *parent = field_SyncRecord(rec, parent_SyncBookmarkField);
        if (!isEmpty_String(parent) && cmp_String(parent, "0") &&
            !contains_StringHash(out->records[bookmarks_SyncDataType], parent)) {
            setFieldCStr_SyncRecord(rec, parent_SyncBookmarkField, "");
        }
    }
    out->maxBookmarkId = iMax(maxBookmarkId_SyncData(normLocal), maxBookmarkId_SyncData(remote));
    out->maxBookmarkId = iMax(out->maxBookmarkId, maxBookmarkId_SyncData(out));
    delete_SyncData(normLocal);
    return out;
}

/*----------------------------------------------------------------------------------------------*/

iBlock *key_SyncData(const iTlsCertificate *syncIdentity) {
    iBlock *raw = rawPrivateKey_TlsCertificate(syncIdentity);
    if (!raw) {
        return NULL;
    }
    iBlock *label = newCStr_Block(keyLabel_SyncData_);
    iBlock *key   = hmacSha256_Block(label, raw);
    delete_Block(label);
    delete_Block(raw);
    return key;
}

iBlock *encrypt_SyncData(const iBlock *archive, const iBlock *key) {
    iBlock *aad   = newData_Block(magic_SyncData_, sizeof(magic_SyncData_));
    iBlock *nonce = newRandom_Block(iAesGcmNonceSize);
    iBlock *enc   = nonce ? encryptAesGcm_Block(archive, key, nonce, aad) : NULL;
    iBlock *out   = NULL;
    if (enc) {
        out = copy_Block(aad);
        append_Block(out, nonce);
        append_Block(out, enc);
    }
    delete_Block(enc);
    delete_Block(nonce);
    delete_Block(aad);
    return out;
}

iBlock *decrypt_SyncData(const iBlock *encrypted, const iBlock *key) {
    const size_t headerSize = sizeof(magic_SyncData_) + iAesGcmNonceSize;
    if (size_Block(encrypted) < headerSize + iAesGcmTagSize ||
        memcmp(constData_Block(encrypted), magic_SyncData_, sizeof(magic_SyncData_))) {
        return NULL;
    }
    iBlock *aad    = newData_Block(magic_SyncData_, sizeof(magic_SyncData_));
    iBlock *nonce  = newData_Block(constBegin_Block(encrypted) + sizeof(magic_SyncData_),
                                   iAesGcmNonceSize);
    iBlock *cipher = newData_Block(constBegin_Block(encrypted) + headerSize,
                                   size_Block(encrypted) - headerSize);
    iBlock *plain  = decryptAesGcm_Block(cipher, key, nonce, aad);
    delete_Block(cipher);
    delete_Block(nonce);
    delete_Block(aad);
    return plain;
}
