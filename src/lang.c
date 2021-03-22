#include "lang.h"
#include "embedded.h"

#include <the_Foundation/sortedarray.h>
#include <the_Foundation/string.h>

iDeclareType(Lang)
iDeclareType(MsgStr)

struct Impl_MsgStr {
    const char *id; /* these point to null-terminated strings in embedded data */
    const char *str;
};

int cmp_MsgStr_(const void *e1, const void *e2) {
    const iMsgStr *a = e1, *b = e2;
    return iCmpStr(a->id, b->id);
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
    const iBlock *data = NULL; // &blobLangEn_Embedded;
    iMsgStr msg;
    for (const char *ptr = constBegin_Block(data); ptr != constEnd_Block(data); ptr++) {
        msg.id = ptr;
        while (*++ptr) {}
        msg.str = ++ptr;
        while (*++ptr) {}
        /* Allocate the string. */
        insert_SortedArray(d->messages, &msg);
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

const char *cstr_Lang(const char *msgId) {
    const iLang *d = &lang_;
    size_t pos;
    const iMsgStr key = { .id = iConstCast(char *, msgId) };
    if (locate_SortedArray(d->messages, &key, &pos)) {
        return ((const iMsgStr *) at_SortedArray(d->messages, pos))->str;
    }
    //iAssert(iFalse);
    fprintf(stderr, "[Lang] missing: %s\n", msgId);
    return msgId;
}

const iString *string_Lang(const char *msgId) {
    return collectNewCStr_String(cstr_Lang(msgId));
}
