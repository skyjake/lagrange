#include "lang.h"
#include "embedded.h"

#include <the_Foundation/sortedarray.h>
#include <the_Foundation/string.h>

iDeclareType(Lang)
iDeclareType(MsgStr)

struct Impl_MsgStr {
    iRangecc id; /* these point to null-terminated strings in embedded data */
    iRangecc str;
};

int cmp_MsgStr_(const void *e1, const void *e2) {
    const iMsgStr *a = e1, *b = e2;
    return cmpCStrNSc_Rangecc(a->id, b->id.start, size_Range(&b->id), &iCaseSensitive);
}

/*----------------------------------------------------------------------------------------------*/

enum iPluralType {
    none_PluralType,
    notEqualToOne_PluralType,
    slavic_PluralType,
};

struct Impl_Lang {
    iSortedArray *messages;
    enum iPluralType pluralType;
};

static iLang lang_;

static size_t pluralIndex_Lang_(const iLang *d, int n) {
    switch (d->pluralType) {
        case notEqualToOne_PluralType:
            return n != 1;
        case slavic_PluralType:
            return n % 10 == 1 && n % 100 != 11                                    ? 0
                   : n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 10 || n % 100 >= 20) ? 1
                                                                                   : 2;
        default:
            return 0;
    }
}

static void clear_Lang_(iLang *d) {
    clear_SortedArray(d->messages);
}

static void load_Lang_(iLang *d, const char *id) {
    /* Load compiled language strings from an embedded blob. */
    iUnused(id);
    const iBlock *data = equal_CStr(id, "fi")      ? &blobFi_Embedded
                       : equal_CStr(id, "fr")      ? &blobFr_Embedded
                       : equal_CStr(id, "ru")      ? &blobRu_Embedded
                       : equal_CStr(id, "es")      ? &blobEs_Embedded
//                       : equal_CStr(id, "de")      ? &blobDe_Embedded
                       : equal_CStr(id, "ie")      ? &blobIe_Embedded
                       : equal_CStr(id, "sr")      ? &blobSr_Embedded
                       : equal_CStr(id, "zh_Hans") ? &blobZh_Hans_Embedded
                       : equal_CStr(id, "zh_Hant") ? &blobZh_Hant_Embedded
                                                   : &blobEn_Embedded;
    if (data == &blobRu_Embedded || data == &blobSr_Embedded) {
        d->pluralType = slavic_PluralType;
    }
    else if (data == &blobZh_Hans_Embedded || data == &blobZh_Hant_Embedded) {
        d->pluralType = none_PluralType;
    }
    else {
        d->pluralType = notEqualToOne_PluralType;
    }
    iMsgStr msg;
    for (const char *ptr = constBegin_Block(data); ptr != constEnd_Block(data); ptr++) {
        msg.id.start = ptr;
        while (*++ptr) {}
        msg.id.end = ptr;
        msg.str.start = ++ptr;
        if (*ptr) { /* not empty */
            while (*++ptr) {}
            msg.str.end = ptr;
        }
        else {
            msg.str = msg.id; /* not translated */
        }
        /* Allocate the string. The data has already been sorted. */
//        printf("ID:%s\n", msg.id.start);
        pushBack_Array(&d->messages->values, &msg);
    }
}

void init_Lang(void) {
    iLang *d = &lang_;
    d->messages = new_SortedArray(sizeof(iMsgStr), cmp_MsgStr_);
    setCurrent_Lang("en");
}

void deinit_Lang(void) {
    iLang *d = &lang_;
    clear_Lang_(d);
    delete_SortedArray(d->messages);
}

void setCurrent_Lang(const char *language) {
    iLang *d = &lang_;
    clear_Lang_(d);
    load_Lang_(d, language);
}

static iBool find_Lang_(iRangecc msgId, iRangecc *str_out) {
    const iLang *d = &lang_;
    size_t pos;
    const iMsgStr key = { .id = msgId };
    if (locate_SortedArray(d->messages, &key, &pos)) {
        *str_out = ((const iMsgStr *) at_SortedArray(d->messages, pos))->str;
        return iTrue;
    }
    fprintf(stderr, "[Lang] missing: %s\n", cstr_Rangecc(msgId)); fflush(stderr);
    //    iAssert(iFalse);
    *str_out = msgId;
    return iFalse;
}

iRangecc range_Lang(iRangecc msgId) {
    iRangecc str;
    find_Lang_(msgId, &str);
    return str;
}

const iString *string_Lang(const char *msgId) {
    return collectNewRange_String(range_Lang(range_CStr(msgId)));
}

const char *cstr_Lang(const char *msgId) {
    return range_Lang(range_CStr(msgId)).start; /* guaranteed to be NULL-terminated */
}

static char *pluralId_Lang_(const iLang *d, const char *msgId, int count) {
    const size_t len = strlen(msgId);
    char *pluralId = strdup(msgId);
    pluralId[len - 1] = '0' + pluralIndex_Lang_(d, count);
    return pluralId;
}

const char *cstrCount_Lang(const char *msgId, int count) {
    iAssert(endsWith_Rangecc(range_CStr(msgId), ".n")); /* by convention */
    char *pluralId = pluralId_Lang_(&lang_, msgId, count);
    const char *str = cstr_Lang(pluralId);
    if (str == pluralId) {
        str = msgId; /* not found */
    }
    free(pluralId);
    return str;
}

void translate_Lang(iString *textWithIds) {
    for (const char *pos = cstr_String(textWithIds); *pos; ) {
        iRangecc id;
        id.start = strstr(pos, "${");
        if (!id.start) {
            break;
        }
        id.start += 2;
        id.end = strchr(id.start, '}');
        iAssert(id.end != NULL);
        const size_t idLen = size_Range(&id);
        iRangecc replacement;
        const size_t startPos = id.start - cstr_String(textWithIds) - 2;
        if (find_Lang_(id, &replacement)) {
            /* Replace it. */
            remove_Block(&textWithIds->chars, startPos, idLen + 3);
            insertData_Block(&textWithIds->chars, startPos, replacement.start, size_Range(&replacement));
            pos = cstr_String(textWithIds) + startPos + size_Range(&replacement);
        }
        else {
            remove_Block(&textWithIds->chars, startPos, 1); /* skip on subsequent attempts */
            pos = cstr_String(textWithIds) + startPos + idLen;
        }
    }
}

const char *translateCStr_Lang(const char *textWithIds) {
    if (strstr(textWithIds, "${") == NULL) {
        return textWithIds; /* nothing to replace */
    }
    iString *text = collectNewCStr_String(textWithIds);
    translate_Lang(text);
    return cstr_String(text);
}

const char *formatCStr_Lang(const char *formatMsgId, int count) {
    return format_CStr(cstrCount_Lang(formatMsgId, count), count);
}

const char *formatCStrs_Lang(const char *formatMsgId, size_t count) {
    return format_CStr(cstrCount_Lang(formatMsgId, (int) count), count);
}
