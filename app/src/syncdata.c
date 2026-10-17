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

#include <lagrange/listfile.h>
#include <lagrange/visited.h>

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
    { "url" },         { "title" }, { "tags" },     { "icon", iTrue },   { "created", iTrue },
    { "originalurl" }, { "notes" }, { "identity" }, { "parent", iTrue }, { "order", iTrue },
};

static const iSyncFieldInfo bookFields_[] = {
    { "title" },
    { "author" },
    { "url" },
    { "chapter", iTrue },
    { "chapters", iTrue },
    { "pos", iTrue },
    { "timestamp", iTrue },
    { "positiontime", iTrue },
    { "edition" },
    { "shelved", iTrue },
    { "deleted", iTrue },
};

static const iSyncFieldInfo visitedFields_[] = {
    { "when", iTrue },
    { "flags" },
};

static const iSyncFieldInfo identityFields_[] = {
    { "icon", iTrue }, { "notes" }, { "useurls" }, { "cert" }, { "key" },
};

static const iSyncFieldInfo trustedFields_[] = {
    { "until", iTrue },
    { "fingerprint" },
};

static const iSyncFieldInfo snippetFields_[] = {
    { "content" },
};

static const iSyncFieldInfo siteFields_[] = {
    { "value" },
};

static const struct {
    const char           *name;
    const char           *entry; /* in the archive */
    const iSyncFieldInfo *fields;
    size_t                numFields;
} types_[max_UserDataType] = {
    { "bookmarks", "bookmarks.ini", bookmarkFields_, iElemCount(bookmarkFields_) },
    { "books", "books.ini", bookFields_, iElemCount(bookFields_) },
    { "visited", "visited.txt", visitedFields_, iElemCount(visitedFields_) },
    { "identities", "identities.ini", identityFields_, iElemCount(identityFields_) },
    { "trusted", "trusted.txt", trustedFields_, iElemCount(trustedFields_) },
    { "snippets", "snippets.ini", snippetFields_, iElemCount(snippetFields_) },
    { "sites", "sitespec.ini", siteFields_, iElemCount(siteFields_) },
};

static const uint32_t formatVersion_SyncData_ = 2;
static const char    *metaEntry_SyncData_     = "sync.ini";
static const char     magic_SyncData_[2][5]   = {
    { 'L', 'g', 'S', 'y', 1 /* version */ },
    { 'L', 'g', 'S', 'r', 1 /* version */ }, /* recent changes */
};
static const uint32_t recentFormatVersion_SyncData_ = 1;
static const char    *keyLabel_SyncData_            = "lagrange-sync-v1";

static int fieldIndex_SyncData_(enum iUserDataType type, const iString *name) {
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

static void insert_SyncData_(iSyncData *d, enum iUserDataType type, iSyncRecord *record) {
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

void clear_SyncData(iSyncData *d, enum iUserDataType type) {
    clear_StringHash(d->records[type]);
}

iSyncRecord *add_SyncData(iSyncData *d, enum iUserDataType type, const iString *key) {
    iSyncRecord *record = new_SyncRecord(key, types_[type].numFields);
    insert_StringHash(d->records[type], key, record);
    iRelease(record);
    return record;
}

const iSyncRecord *find_SyncData(const iSyncData *d, enum iUserDataType type, const iString *key) {
    return constValue_StringHash(d->records[type], key);
}

iBool remove_SyncData(iSyncData *d, enum iUserDataType type, const iString *key) {
    return remove_StringHash(d->records[type], key);
}

size_t size_SyncData(const iSyncData *d, enum iUserDataType type) {
    return size_StringHash(d->records[type]);
}

iBool equalRecords_SyncData(const iSyncData *d, const iSyncData *other, enum iUserDataType type) {
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
    for (int i = 0; i < max_UserDataType; i++) {
        if (!equalRecords_SyncData(d, other, i)) {
            return iFalse;
        }
    }
    return iTrue;
}

uint32_t maxBookmarkId_SyncData(const iSyncData *d) {
    uint32_t maxId = d->maxBookmarkId;
    iConstForEach(StringHash, i, d->records[bookmarks_UserDataType]) {
        const iSyncRecord *rec = value_StringHashNode(i.value);
        maxId = iMax(maxId, (uint32_t) toInt_String(&rec->key));
    }
    return maxId;
}

/*----------------------------------------------------------------------------------------------*/

iBlock *serialize_SyncData(const iSyncData *d) {
    iArchive *arch = new_Archive();
    openWritable_Archive(arch);
    setCompressionLevel_Archive(arch, 9);
    iString *meta = new_String();
    format_String(meta,
                  "format = %u\n"
                  "generation = %llu\n"
                  "maxbookmarkid = %u\n\n"
                  "[modified]\n",
                  formatVersion_SyncData_,
                  (unsigned long long) d->generation,
                  d->maxBookmarkId);
    for (int i = 0; i < max_UserDataType; i++) {
        appendFormat_String(meta, "%s = %lld\n", types_[i].name,
                            (long long) integralSeconds_Time(&d->modified[i]));
        iBlock *data = native_SyncData(d, i);
        setDataCStr_Archive(arch, types_[i].entry, data);
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

iDeclareType(SyncMetaParser)

struct Impl_SyncMetaParser {
    iSyncData *data;
    uint32_t   format;
};

static void handleMetaKeyValue_SyncData_(void *context, const iString *table, const iString *key,
                                         const iTomlValue *value) {
    iSyncMetaParser *parser = context;
    iSyncData       *d      = parser->data;
    if (value->type != int64_TomlType) {
        return;
    }
    if (isEmpty_String(table)) {
        if (!cmp_String(key, "format")) {
            parser->format = (uint32_t) value->value.int64;
        }
        else if (!cmp_String(key, "generation")) {
            d->generation = (uint64_t) value->value.int64;
        }
        else if (!cmp_String(key, "maxbookmarkid")) {
            d->maxBookmarkId = (uint32_t) value->value.int64;
        }
    }
    else if (!cmp_String(table, "modified")) {
        for (int i = 0; i < max_UserDataType; i++) {
            if (!cmp_String(key, types_[i].name)) {
                initSeconds_Time(&d->modified[i], (double) value->value.int64);
            }
        }
    }
}

static uint32_t parseMeta_SyncData_(iSyncData *d, const iArchive *arch) {
    const iBlock *meta = dataCStr_Archive(arch, metaEntry_SyncData_);
    if (!meta) {
        return 0;
    }
    iSyncData       scratch;
    iSyncMetaParser parser = { d ? d : &scratch, 0 };
    iTomlParser    *toml   = new_TomlParser();
    setHandlers_TomlParser(toml, NULL, handleMetaKeyValue_SyncData_, &parser);
    if (!parse_TomlParser(toml, collect_String(newBlock_String(meta)))) {
        parser.format = 0;
    }
    delete_TomlParser(toml);
    return parser.format;
}

iBool deserialize_SyncData(iSyncData *d, const iBlock *archive) {
    iArchive *arch = new_Archive();
    iBool     ok   = iFalse;
    if (openData_Archive(arch, archive) &&
        parseMeta_SyncData_(d, arch) == formatVersion_SyncData_) {
        for (int i = 0; i < max_UserDataType; i++) {
            const iBlock *data = dataCStr_Archive(arch, types_[i].entry);
            if (data) {
                parseNative_SyncData(d, i, data);
            }
            else {
                clear_SyncData(d, i);
            }
        }
        ok = iTrue;
    }
    iRelease(arch);
    return ok;
}

iBool isOutdated_SyncData(const iBlock *archive) {
    iArchive *arch = new_Archive();
    iBool     isOld = iFalse;
    if (openData_Archive(arch, archive)) {
        const uint32_t format = parseMeta_SyncData_(NULL, arch);
        isOld = format > 0 && format < formatVersion_SyncData_;
    }
    iRelease(arch);
    return isOld;
}

/*----------------------------------------------------------------------------------------------*/

static int cmpVisit_SyncRecent_(const void *a, const void *b) {
    return cmpString_String(&((const iSyncRecentVisit *) a)->url,
                            &((const iSyncRecentVisit *) b)->url);
}

static int cmpBook_SyncRecent_(const void *a, const void *b) {
    return cmpString_String(&((const iSyncRecentBook *) a)->id,
                            &((const iSyncRecentBook *) b)->id);
}

void init_SyncRecent(iSyncRecent *d) {
    d->generation = 0;
    init_SortedArray(&d->visits, sizeof(iSyncRecentVisit), cmpVisit_SyncRecent_);
    init_SortedArray(&d->books, sizeof(iSyncRecentBook), cmpBook_SyncRecent_);
}

void deinit_SyncRecent(iSyncRecent *d) {
    clear_SyncRecent(d);
    deinit_SortedArray(&d->books);
    deinit_SortedArray(&d->visits);
}

iDefineTypeConstruction(SyncRecent)

void clear_SyncRecent(iSyncRecent *d) {
    iForEach(Array, i, &d->visits.values) {
        deinit_String(&((iSyncRecentVisit *) i.value)->url);
    }
    iForEach(Array, j, &d->books.values) {
        deinit_String(&((iSyncRecentBook *) j.value)->id);
    }
    clear_SortedArray(&d->visits);
    clear_SortedArray(&d->books);
    d->generation = 0;
}

iSyncRecent *copy_SyncRecent(const iSyncRecent *d) {
    iSyncRecent *copy = new_SyncRecent();
    copy->generation = d->generation;
    iConstForEach(Array, i, &d->visits.values) {
        setVisit_SyncRecent(copy, i.value);
    }
    iConstForEach(Array, j, &d->books.values) {
        setBook_SyncRecent(copy, j.value);
    }
    return copy;
}

void setVisit_SyncRecent(iSyncRecent *d, const iSyncRecentVisit *visit) {
    size_t pos;
    if (locate_SortedArray(&d->visits, visit, &pos)) {
        iSyncRecentVisit *old = at_SortedArray(&d->visits, pos);
        old->changed    = visit->changed;
        old->when       = visit->when;
        old->visitFlags = visit->visitFlags;
        old->flags      = visit->flags;
        return;
    }
    iSyncRecentVisit copy = *visit;
    initCopy_String(&copy.url, &visit->url);
    insert_Array(&d->visits.values, pos, &copy);
}

void setBook_SyncRecent(iSyncRecent *d, const iSyncRecentBook *book) {
    size_t pos;
    if (locate_SortedArray(&d->books, book, &pos)) {
        iSyncRecentBook *old = at_SortedArray(&d->books, pos);
        old->positionTime = book->positionTime;
        old->chapter      = book->chapter;
        old->numChapters  = book->numChapters;
        old->pos          = book->pos;
        return;
    }
    iSyncRecentBook copy = *book;
    initCopy_String(&copy.id, &book->id);
    insert_Array(&d->books.values, pos, &copy);
}

const iSyncRecentVisit *findVisit_SyncRecent(const iSyncRecent *d, const iString *url) {
    const iSyncRecentVisit key = { .url = *url };
    size_t pos;
    return locate_SortedArray(&d->visits, &key, &pos) ? constAt_SortedArray(&d->visits, pos)
                                                      : NULL;
}

static const iSyncRecentBook *findBook_SyncRecent_(const iSyncRecent *d, const iString *id) {
    const iSyncRecentBook key = { .id = *id };
    size_t pos;
    return locate_SortedArray(&d->books, &key, &pos) ? constAt_SortedArray(&d->books, pos) : NULL;
}

iBool equal_SyncRecent(const iSyncRecent *d, const iSyncRecent *other) {
    if (size_SortedArray(&d->visits) != size_SortedArray(&other->visits) ||
        size_SortedArray(&d->books) != size_SortedArray(&other->books)) {
        return iFalse;
    }
    for (size_t i = 0; i < size_SortedArray(&d->visits); i++) {
        const iSyncRecentVisit *a = constAt_SortedArray(&d->visits, i);
        const iSyncRecentVisit *b = constAt_SortedArray(&other->visits, i);
        if (!equal_String(&a->url, &b->url) || a->changed != b->changed || a->when != b->when ||
            a->visitFlags != b->visitFlags || a->flags != b->flags) {
            return iFalse;
        }
    }
    for (size_t i = 0; i < size_SortedArray(&d->books); i++) {
        const iSyncRecentBook *a = constAt_SortedArray(&d->books, i);
        const iSyncRecentBook *b = constAt_SortedArray(&other->books, i);
        if (!equal_String(&a->id, &b->id) || a->positionTime != b->positionTime ||
            a->chapter != b->chapter || a->numChapters != b->numChapters || a->pos != b->pos) {
            return iFalse;
        }
    }
    return iTrue;
}

static iBool isNewerVisit_(const iSyncRecentVisit *a, const iSyncRecentVisit *b) {
    if (a->changed != b->changed) {
        return a->changed > b->changed;
    }
    /* A removal at the same time is assumed to come after the visit. */
    return (a->flags & removed_SyncRecentVisitFlag) && !(b->flags & removed_SyncRecentVisitFlag);
}

void merge_SyncRecent(iSyncRecent *d, const iSyncRecent *other) {
    iConstForEach(Array, i, &other->visits.values) {
        const iSyncRecentVisit *visit = i.value;
        const iSyncRecentVisit *old   = findVisit_SyncRecent(d, &visit->url);
        if (!old || isNewerVisit_(visit, old)) {
            setVisit_SyncRecent(d, visit);
        }
    }
    iConstForEach(Array, j, &other->books.values) {
        const iSyncRecentBook *book = j.value;
        const iSyncRecentBook *old  = findBook_SyncRecent_(d, &book->id);
        if (!old || book->positionTime > old->positionTime) {
            setBook_SyncRecent(d, book);
        }
    }
}

void prune_SyncRecent(iSyncRecent *d, int64_t cutoff) {
    iForEach(Array, i, &d->visits.values) {
        iSyncRecentVisit *visit = i.value;
        if (visit->changed < cutoff) {
            deinit_String(&visit->url);
            remove_ArrayIterator(&i);
        }
    }
    iForEach(Array, j, &d->books.values) {
        iSyncRecentBook *book = j.value;
        if (book->positionTime < cutoff) {
            deinit_String(&book->id);
            remove_ArrayIterator(&j);
        }
    }
}

iBlock *serialize_SyncRecent(const iSyncRecent *d) {
    iBuffer *buf = new_Buffer();
    openEmpty_Buffer(buf);
    iStream *outs = stream_Buffer(buf);
    writeU32_Stream(outs, recentFormatVersion_SyncData_);
    writeU64_Stream(outs, d->generation);
    writeU32_Stream(outs, (uint32_t) size_SortedArray(&d->visits));
    iConstForEach(Array, i, &d->visits.values) {
        const iSyncRecentVisit *visit = i.value;
        serialize_String(&visit->url, outs);
        write64_Stream(outs, visit->changed);
        write64_Stream(outs, visit->when);
        writeU16_Stream(outs, visit->visitFlags);
        writeU16_Stream(outs, visit->flags);
    }
    writeU32_Stream(outs, (uint32_t) size_SortedArray(&d->books));
    iConstForEach(Array, j, &d->books.values) {
        const iSyncRecentBook *book = j.value;
        serialize_String(&book->id, outs);
        write64_Stream(outs, book->positionTime);
        write32_Stream(outs, book->chapter);
        write32_Stream(outs, book->numChapters);
        writef_Stream(outs, book->pos);
    }
    iBlock *out = compressLevel_Block(data_Buffer(buf), 1);
    iRelease(buf);
    return out;
}

iBool deserialize_SyncRecent(iSyncRecent *d, const iBlock *data) {
    clear_SyncRecent(d);
    iBlock  *raw = decompress_Block(data);
    iBuffer *buf = new_Buffer();
    open_Buffer(buf, raw);
    iStream *ins = stream_Buffer(buf);
    iBool    ok  = !isEmpty_Block(raw) && readU32_Stream(ins) == recentFormatVersion_SyncData_;
    if (ok) {
        d->generation = readU64_Stream(ins);
        const uint32_t numVisits = readU32_Stream(ins);
        for (uint32_t i = 0; ok && i < numVisits; i++) {
            iSyncRecentVisit visit;
            init_String(&visit.url);
            deserialize_String(&visit.url, ins);
            visit.changed    = read64_Stream(ins);
            visit.when       = read64_Stream(ins);
            visit.visitFlags = readU16_Stream(ins);
            visit.flags      = readU16_Stream(ins);
            ok = !atEnd_Stream(ins);
            setVisit_SyncRecent(d, &visit);
            deinit_String(&visit.url);
        }
        const uint32_t numBooks = ok ? readU32_Stream(ins) : 0;
        for (uint32_t i = 0; ok && i < numBooks; i++) {
            iSyncRecentBook book;
            init_String(&book.id);
            deserialize_String(&book.id, ins);
            book.positionTime = read64_Stream(ins);
            book.chapter      = read32_Stream(ins);
            book.numChapters  = read32_Stream(ins);
            book.pos          = readf_Stream(ins);
            ok = i + 1 == numBooks || !atEnd_Stream(ins);
            setBook_SyncRecent(d, &book);
            deinit_String(&book.id);
        }
    }
    iRelease(buf);
    delete_Block(raw);
    if (!ok) {
        clear_SyncRecent(d);
    }
    return ok;
}

/*----------------------------------------------------------------------------------------------*/

iDeclareType(NativeParser)

struct Impl_NativeParser {
    iSyncData         *data;
    enum iUserDataType type;
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
    const int index = fieldIndex_SyncData_(d->type, key);
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
        default:
            break;
    }
}

static void parseLines_SyncData_(iSyncData *d, enum iUserDataType type, const iBlock *native) {
    const iRangecc src  = range_Block(native);
    iRangecc       line = iNullRange;
    while (nextLine_ListFile(src, &line)) {
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
        if (type == visited_UserDataType) {
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

static void parseSites_SyncData_(iSyncData *d, const iBlock *native) {
    /* Each line is a table header or "key = value". */
    const iRangecc src  = range_Block(native);
    iRangecc       line = iNullRange;
    iRangecc       site = iNullRange;
    while (nextSplit_Rangecc(src, "\n", &line)) {
        trim_Rangecc(&line);
        if (startsWith_Rangecc(line, "[") && endsWith_Rangecc(line, "]")) {
            site = (iRangecc){ line.start + 1, line.end - 1 };
            continue;
        }
        const char *eq = iStrStrN(line.start, " = ", size_Range(&line));
        if (isEmpty_Range(&site) || !eq) {
            continue;
        }
        iString *key = collectNewRange_String(site);
        appendCStr_String(key, " ");
        appendRange_String(key, (iRangecc){ line.start, eq });
        iSyncRecord *rec = add_SyncData(d, sites_UserDataType, key);
        setField_SyncRecord(
            rec, value_SyncSiteField, collectNewRange_String((iRangecc){ eq + 3, line.end }));
    }
}

static void appendSites_SyncData_(const iSyncData *d, iString *out) {
    iStringSet *keys = new_StringSet(); /* sorted, so each site's settings are together */
    iConstForEach(StringHash, i, d->records[sites_UserDataType]) {
        insert_StringSet(keys, &((const iSyncRecord *) value_StringHashNode(i.value))->key);
    }
    iRangecc site = iNullRange;
    iConstForEach(StringSet, k, keys) {
        const size_t sep = lastIndexOfCStr_String(k.value, " ");
        if (sep == iInvalidPos) {
            continue;
        }
        const iRangecc keySite = { cstr_String(k.value), cstr_String(k.value) + sep };
        if (!equalRange_Rangecc(site, keySite)) {
            appendFormat_String(out, "%s[%s]\n", isEmpty_Range(&site) ? "" : "\n",
                                cstr_Rangecc(keySite));
            site = keySite;
        }
        appendFormat_String(out, "%s = %s\n",
                            cstr_String(k.value) + sep + 1,
                            cstr_String(field_SyncRecord(find_SyncData(d, sites_UserDataType, k.value),
                                                         value_SyncSiteField)));
    }
    iRelease(keys);
}

void parseNative_SyncData(iSyncData *d, enum iUserDataType type, const iBlock *native) {
    clear_SyncData(d, type);
    switch (type) {
        case bookmarks_UserDataType:
        case books_UserDataType:
        case identities_UserDataType:
        case snippets_UserDataType: {
            iNativeParser parser = { d, type, NULL };
            iTomlParser *toml = new_TomlParser();
            setHandlers_TomlParser(
                toml, handleTable_NativeParser_, handleKeyValue_NativeParser_, &parser);
            parse_TomlParser(toml, collect_String(newBlock_String(native)));
            delete_TomlParser(toml);
            break;
        }
        case visited_UserDataType:
        case trusted_UserDataType:
            parseLines_SyncData_(d, type, native);
            break;
        case sites_UserDataType:
            parseSites_SyncData_(d, native);
            break;
        default:
            iAssert(iFalse);
            break;
    }
}

static void appendRecords_SyncData_(const iSyncData *d, enum iUserDataType type, iString *out) {
    iStringSet *keys = new_StringSet(); /* sorted, so similar keys compress well together */
    iConstForEach(StringHash, i, d->records[type]) {
        insert_StringSet(keys, &((const iSyncRecord *) value_StringHashNode(i.value))->key);
    }
    iConstForEach(StringSet, k, keys) {
        const iSyncRecord *rec = find_SyncData(d, type, k.value);
        switch (type) {
            case visited_UserDataType:
                appendFormat_String(out, "%s %s %s\n",
                                    cstr_String(field_SyncRecord(rec, when_SyncVisitedField)),
                                    cstr_String(field_SyncRecord(rec, flags_SyncVisitedField)),
                                    cstr_String(&rec->key));
                break;
            case trusted_UserDataType:
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
    iRelease(keys);
}

iBlock *native_SyncData(const iSyncData *d, enum iUserDataType type) {
    iString *out = new_String();
    if (type == sites_UserDataType) {
        appendSites_SyncData_(d, out);
    }
    else {
        appendRecords_SyncData_(d, type, out);
    }
    iBlock *data = copy_Block(utf8_String(out));
    delete_String(out);
    return data;
}

/*----------------------------------------------------------------------------------------------*/

void pruneVisited_SyncData(iSyncData *d, int64_t cutoff) {
    iStringArray *pruned = new_StringArray();
    iConstForEach(StringHash, i, d->records[visited_UserDataType]) {
        const iSyncRecord *rec   = value_StringHashNode(i.value);
        const uint16_t     flags = (uint16_t) strtoul(
            cstr_String(field_SyncRecord(rec, flags_SyncVisitedField)), NULL, 16);
        if (isTransientOnly_VisitedUrlFlags(flags) &&
            number_SyncRecord_(rec, when_SyncVisitedField) < cutoff) {
            pushBack_StringArray(pruned, &rec->key);
        }
    }
    iConstForEach(StringArray, j, pruned) {
        remove_SyncData(d, visited_UserDataType, j.value);
    }
    iRelease(pruned);
}

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
    iConstForEach(StringHash, i, d->records[bookmarks_UserDataType]) {
        const iSyncRecord *rec = value_StringHashNode(i.value);
        const uint32_t newId = mappedId_SyncIdMapping(idMappings, toInt_String(&rec->key));
        iSyncRecord *copy = new_SyncRecord(collectNewFormat_String("%u", newId),
                                           types_[bookmarks_UserDataType].numFields);
        for (size_t f = 0; f < types_[bookmarks_UserDataType].numFields; f++) {
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
    iRelease(d->records[bookmarks_UserDataType]);
    d->records[bookmarks_UserDataType] = remapped;
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
    const iStringHash *B = base->records[bookmarks_UserDataType];
    const iStringHash *L = local->records[bookmarks_UserDataType];
    const iStringHash *R = remote->records[bookmarks_UserDataType];
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
    enum iUserDataType type;
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
    if (d->type == trusted_UserDataType) {
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
    if (d->type == visited_UserDataType) {
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
    else if (d->type == books_UserDataType) {
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
        static const enum iSyncBookField latest[] = { timestamp_SyncBookField,
                                                       shelved_SyncBookField,
                                                       deleted_SyncBookField };
        iForIndices(i, latest) {
            setField_SyncRecord(out, latest[i],
                                field_SyncRecord(number_SyncRecord_(local, latest[i]) >
                                                         number_SyncRecord_(remote, latest[i])
                                                     ? local
                                                     : remote,
                                                 latest[i]));
        }
        if (isChanged_(base, local, edition_SyncBookField) &&
            isChanged_(base, remote, edition_SyncBookField)) {
            /* Editions are dated, so the newer one wins. */
            const iString *le = field_SyncRecord(local, edition_SyncBookField);
            const iString *re = field_SyncRecord(remote, edition_SyncBookField);
            setField_SyncRecord(out, edition_SyncBookField, cmpString_String(le, re) > 0 ? le : re);
        }
    }
    return out;
}

static iBool survivesDeletion_SyncMerge_(const iSyncMerge *d, const iSyncRecord *base,
                                         const iSyncRecord *kept) {
    if (d->type == visited_UserDataType) {
        /* Visited again after the deletion. */
        return number_SyncRecord_(kept, when_SyncVisitedField) >
               number_SyncRecord_(base, when_SyncVisitedField);
    }
    if (d->type == identities_UserDataType && d->protectedIdentity) {
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
    for (int type = 0; type < max_UserDataType; type++) {
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
    iForEach(StringHash, m, out->records[bookmarks_UserDataType]) {
        iSyncRecord *rec = value_StringHashNode(m.value);
        const iString *parent = field_SyncRecord(rec, parent_SyncBookmarkField);
        if (!isEmpty_String(parent) && cmp_String(parent, "0") &&
            !contains_StringHash(out->records[bookmarks_UserDataType], parent)) {
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

iBlock *encrypt_SyncData(const iBlock *archive, const iBlock *key, enum iSyncArchive kind) {
    /* The magic is authenticated, so the archives cannot be swapped with each other. */
    iBlock *aad   = newData_Block(magic_SyncData_[kind], sizeof(magic_SyncData_[kind]));
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

iBlock *decrypt_SyncData(const iBlock *encrypted, const iBlock *key, enum iSyncArchive kind) {
    const char  *magic      = magic_SyncData_[kind];
    const size_t magicSize  = sizeof(magic_SyncData_[kind]);
    const size_t headerSize = magicSize + iAesGcmNonceSize;
    if (size_Block(encrypted) < headerSize + iAesGcmTagSize ||
        memcmp(constData_Block(encrypted), magic, magicSize)) {
        return NULL;
    }
    iBlock *aad    = newData_Block(magic, magicSize);
    iBlock *nonce  = newData_Block(constBegin_Block(encrypted) + magicSize,
                                   iAesGcmNonceSize);
    iBlock *cipher = newData_Block(constBegin_Block(encrypted) + headerSize,
                                   size_Block(encrypted) - headerSize);
    iBlock *plain  = decryptAesGcm_Block(cipher, key, nonce, aad);
    delete_Block(cipher);
    delete_Block(nonce);
    delete_Block(aad);
    return plain;
}
