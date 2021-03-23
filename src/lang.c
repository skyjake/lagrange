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

struct Impl_Lang {
    iSortedArray *messages;
};

static iLang lang_;

static void clear_Lang_(iLang *d) {
    clear_SortedArray(d->messages);
}

static void load_Lang_(iLang *d, const char *id) {
    /* Load compiled language strings from an embedded blob. */
    iUnused(id);
    const iBlock *data = equal_CStr(id, "fi") ? &blobFi_Embedded : &blobEn_Embedded;
    iMsgStr msg;
    for (const char *ptr = constBegin_Block(data); ptr != constEnd_Block(data); ptr++) {
        msg.id.start = ptr;
        while (*++ptr) {}
        msg.id.end = ptr;
        msg.str.start = ++ptr;
        while (*++ptr) {}
        msg.str.end = ptr;
        /* Allocate the string. The data has already been sorted. */
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

iRangecc range_Lang(iRangecc msgId) {
    const iLang *d = &lang_;
    size_t pos;
    const iMsgStr key = { .id = msgId };
    if (locate_SortedArray(d->messages, &key, &pos)) {
        return ((const iMsgStr *) at_SortedArray(d->messages, pos))->str;
    }
    fprintf(stderr, "[Lang] missing: %s\n", cstr_Rangecc(msgId)); fflush(stderr);
//    iAssert(iFalse);
    return msgId;
}

const char *cstr_Lang(const char *msgId) {
    return range_Lang(range_CStr(msgId)).start; /* guaranteed to be NULL-terminated */
}

const iString *string_Lang(const char *msgId) {
    return collectNewRange_String(range_Lang(range_CStr(msgId)));
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
        const size_t   idLen       = size_Range(&id);
        const iRangecc replacement = range_Lang(id);
        const size_t   startPos    = id.start - cstr_String(textWithIds) - 2;
        /* Replace it. */
        remove_Block(&textWithIds->chars, startPos, idLen + 3);
        insertData_Block(&textWithIds->chars, startPos, replacement.start, size_Range(&replacement));
        pos = cstr_String(textWithIds) + startPos + size_Range(&replacement);
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
