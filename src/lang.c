#include "lang.h"

#include <the_Foundation/sortedarray.h>
#include <the_Foundation/string.h>

iDeclareType(Lang)
iDeclareType(MsgStr)

struct Impl_MsgStr {
    const char *id;
    iString str;
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

void init_Lang(void) {
    iLang *d = &lang_;
    d->messages = new_SortedArray(sizeof(iMsgStr), cmp_MsgStr_);
    setCurrent_Lang("en");
}

void deinit_Lang(void) {
    iLang *d = &lang_;
    delete_SortedArray(d->messages);
}

void setCurrent_Lang(const char *language) {
    /* TODO: Load compiled language strings from an embedded blob. */
}

const iString *string_Lang(const char *msgId) {
    const iLang *d = &lang_;
    size_t pos;
    const iMsgStr key = { .id = msgId };
    if (locate_SortedArray(d->messages, &key, &pos)) {
        return &((const iMsgStr *) at_SortedArray(d->messages, pos))->str;
    }
    //iAssert(iFalse);
    fprintf(stderr, "[Lang] missing: %s\n", msgId);
    return collectNewCStr_String(msgId);
}

const char *cstr_Lang(const char *msgId) {
    return cstr_String(string_Lang(msgId));
}
