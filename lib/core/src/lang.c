/* Copyright 2021 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "lagrange/lang.h"
#include "lagrange/resources.h"

#include <the_Foundation/sortedarray.h>
#include <the_Foundation/string.h>
#include <ctype.h>

iDeclareType(Lang)
iDeclareType(MsgStr)

struct Impl_MsgStr {
    iRangecc id; /* these point to null-terminated strings in Lang.strings */
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
    polish_PluralType,
    slavic_PluralType,
    oneTwoMany_PluralType,
    oneFewMany_PluralType,
    samogitian_PluralType,
};

struct Impl_Lang {
    iBlock strings;
    iSortedArray *messages;
    enum iPluralType pluralType;
    iString langCode;
};

static iLang lang_;

static size_t pluralIndex_Lang_(const iLang *d, int n) {
    switch (d->pluralType) {
        case notEqualToOne_PluralType:
            return n != 1;
        case oneTwoMany_PluralType:
            return n == 1 ? 0 : n == 2 ? 1 : 2;
        case oneFewMany_PluralType:
            return n == 1 ? 0 : (n >= 2 && n <= 4) ? 1 : 2;
        case polish_PluralType:
            return n == 1                                                          ? 0
                   : n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 10 || n % 100 >= 20) ? 1
                                                                                   : 2;
        case slavic_PluralType:
            return n % 10 == 1 && n % 100 != 11                                    ? 0
                   : n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 10 || n % 100 >= 20) ? 1
                                                                                   : 2;
        case samogitian_PluralType:
            return (n % 10 == 1 && n % 100 != 11)                                   ? 0
                   : (n % 10 == 2 && n % 100 != 12)                                 ? 1
                   : (n % 10 >= 3 && n % 10 <= 9 && (n % 100 < 11 || n % 100 > 19)) ? 2
                                                                                    : 3;
        default:
            return 0;
    }
}

static void clear_Lang_(iLang *d) {
    clear_SortedArray(d->messages);
    clear_Block(&d->strings);
}

static void load_Lang_(iLang *d, const char *id) {
    /* Load compiled language strings from a resource blob. */
    /* TODO: How about an array for these? (id, blob, pluralType) */
    iUnused(id);
    const iBlock *data = equal_CStr(id, "fi")      ? &blobFi_Resources
                       : equal_CStr(id, "fr")      ? &blobFr_Resources
                       : equal_CStr(id, "cs")      ? &blobCs_Resources
                       : equal_CStr(id, "ru")      ? &blobRu_Resources
                       : equal_CStr(id, "eo")      ? &blobEo_Resources
                       : equal_CStr(id, "es")      ? &blobEs_Resources
                       : equal_CStr(id, "es_MX")   ? &blobEs_MX_Resources
                       : equal_CStr(id, "eu")      ? &blobEu_Resources
                       : equal_CStr(id, "de")      ? &blobDe_Resources
                       : equal_CStr(id, "gl")      ? &blobGl_Resources
                       : equal_CStr(id, "hu")      ? &blobHu_Resources
                       : equal_CStr(id, "ia")      ? &blobIa_Resources
                       : equal_CStr(id, "ie")      ? &blobIe_Resources
                       : equal_CStr(id, "isv")     ? &blobIsv_Resources
                       : equal_CStr(id, "ja")      ? &blobJa_Resources
                       : equal_CStr(id, "it")      ? &blobIt_Resources
                       : equal_CStr(id, "nl")      ? &blobNl_Resources
                       : equal_CStr(id, "pl")      ? &blobPl_Resources
                       : equal_CStr(id, "sgs")     ? &blobSgs_Resources
                       : equal_CStr(id, "sk")      ? &blobSk_Resources
                       : equal_CStr(id, "sr")      ? &blobSr_Resources
                       : equal_CStr(id, "tok")     ? &blobTok_Resources
                       : equal_CStr(id, "tr")      ? &blobTr_Resources
                       : equal_CStr(id, "uk")      ? &blobUk_Resources
                       : equal_CStr(id, "zh_Hans") ? &blobZh_Hans_Resources
                       : equal_CStr(id, "zh_Hant") ? &blobZh_Hant_Resources
                                                   : &blobEn_Resources;
    if (data == &blobRu_Resources || data == &blobSr_Resources || data == &blobUk_Resources) {
        d->pluralType = slavic_PluralType;
    }
    else if (data == &blobSgs_Resources) {
        d->pluralType = samogitian_PluralType;
    }
    else if (data == &blobIsv_Resources) {
        d->pluralType = oneTwoMany_PluralType;
    }
    else if (data == &blobCs_Resources || data == &blobSk_Resources) {
        d->pluralType = oneFewMany_PluralType;
    }
    else if (data == &blobPl_Resources) {
        d->pluralType = polish_PluralType;
    }
    else if (data == &blobZh_Hans_Resources || data == &blobZh_Hant_Resources ||
             data == &blobTok_Resources || data == &blobJa_Resources) {
        d->pluralType = none_PluralType;
    }
    else {
        d->pluralType = notEqualToOne_PluralType;
    }
    /* Strings may be terminated with 0x01 instead of NUL. */
    set_Block(&d->strings, data);
    replace_Block(&d->strings, 1, 0);
    iMsgStr msg;
    for (const char *ptr = constBegin_Block(&d->strings); ptr != constEnd_Block(&d->strings);
         ptr++) {
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
        pushBack_Array(&d->messages->values, &msg);
    }
    /* ISO 639 language code. */
    setCStr_String(&d->langCode, id);
    size_t upos = indexOf_String(&d->langCode, '_');
    if (upos != iInvalidPos) {
        truncate_Block(&d->langCode.chars, upos);
    }
}

void init_Lang(void) {
    iLang *d = &lang_;
    init_String(&d->langCode);
    init_Block(&d->strings, 0);
    d->messages = new_SortedArray(sizeof(iMsgStr), cmp_MsgStr_);
    setCurrent_Lang("en");
}

void deinit_Lang(void) {
    iLang *d = &lang_;
    clear_Lang_(d);
    delete_SortedArray(d->messages);
    deinit_Block(&d->strings);
    deinit_String(&d->langCode);
}

void setCurrent_Lang(const char *language) {
    iLang *d = &lang_;
    clear_Lang_(d);
    load_Lang_(d, language);
}

const char *code_Lang(void) {
    return cstr_String(&lang_.langCode);
}

static iBool find_Lang_(iRangecc msgId, iRangecc *str_out) {
    const iLang *d = &lang_;
    iBool convertLowercase = iFalse;
    iBool stripColon = iFalse;
    if (size_Range(&msgId) > 3 && startsWith_CStr(msgId.start, "LC:")) {
        msgId.start += 3;
        convertLowercase = iTrue;
    }
    if (size_Range(&msgId) > 3 && startsWith_CStr(msgId.start, "ST:")) {
        msgId.start += 3;
        stripColon = iTrue;
    }
    size_t pos;
    const iMsgStr key = { .id = msgId };
    if (locate_SortedArray(d->messages, &key, &pos)) {
        *str_out = ((const iMsgStr *) at_SortedArray(d->messages, pos))->str;
        if (stripColon && size_Range(str_out) >= 1) {
            if (str_out->end[-1] == ':') {
                str_out->end--;
            }
        }
        if (convertLowercase) {
            iString msg;
            initRange_String(&msg, *str_out);
            iString *conv = collectNew_String();
            iBool isFirst = iTrue;
            iConstForEach(String, c, &msg) {
                if (c.value == 0x2026 /* ellipsis */) {
                    continue;
                }
                appendChar_String(conv, isFirst ? c.value : lower_Char(c.value));
                isFirst = iFalse;
            }
            deinit_String(&msg);
            *str_out = range_String(conv);
        }
        return iTrue;
    }
    fprintf(stderr, "[Lang] missing: %s\n", cstr_Rangecc(msgId)); fflush(stderr);
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

/* Articles, coordinating conjunctions, and short prepositions (Apple style). */
static iBool isMinorWord_Lang_(iRangecc word) {
    static const char *minorWords[] = { "a",    "an",   "and", "as",   "at",  "but", "by",
                                        "for",  "from", "in",  "into", "nor", "of",  "on",
                                        "onto", "or",   "over", "per", "so",  "the", "to",
                                        "via",  "vs",   "with", "yet" };
    while (!isEmpty_Range(&word) && strchr(":,;", word.end[-1])) {
        word.end--;
    }
    iForIndices(i, minorWords) {
        if (equalCase_Rangecc(word, minorWords[i])) {
            return iTrue;
        }
    }
    return iFalse;
}

static iBool hasLetters_Lang_(iRangecc word) {
    for (const char *ch = word.start; ch != word.end; ch++) {
        if (isalnum((unsigned char) *ch) || (unsigned char) *ch >= 0x80) {
            return iTrue;
        }
    }
    return iFalse;
}

/* File names, addresses, and words like "iOS" are kept as is. */
static iBool isVerbatim_Lang_(iRangecc word) {
    for (const char *ch = word.start; ch != word.end; ch++) {
        if ((unsigned char) *ch < 0x20 || strchr("./@", *ch)) {
            return iTrue;
        }
        if (ch > word.start && isupper((unsigned char) *ch) && islower((unsigned char) *word.start)) {
            return iTrue;
        }
    }
    return iFalse;
}

void titleCase_Lang(iString *text) {
    if (iCmpStr(code_Lang(), "en")) {
        return;
    }
    /* The first and last words are always capitalized. */
    const iRangecc src   = range_String(text);
    const char    *first = NULL;
    const char    *last  = NULL;
    iRangecc       word  = iNullRange;
    while (nextSplit_Rangecc(src, " ", &word)) {
        if (hasLetters_Lang_(word)) {
            if (!first) {
                first = word.start;
            }
            last = word.start;
        }
    }
    if (!first) {
        return;
    }
    iString out;
    init_String(&out);
    word = iNullRange;
    while (nextSplit_Rangecc(src, " ", &word)) {
        if (word.start != src.start) {
            appendCStr_String(&out, " ");
        }
        const iBool isEdge = (word.start == first || word.start == last);
        if (isVerbatim_Lang_(word) || (!isEdge && isMinorWord_Lang_(word))) {
            appendRange_String(&out, word);
            continue;
        }
        /* Each part of a hyphenated compound is capitalized. */
        iString chars;
        initRange_String(&chars, word);
        iBool isPartStart = iTrue;
        iConstForEach(String, c, &chars) {
            appendChar_String(&out, isPartStart ? upper_Char(c.value) : c.value);
            isPartStart = (c.value == '-');
        }
        deinit_String(&chars);
    }
    set_String(text, &out);
    deinit_String(&out);
}

const char *formatCStr_Lang(const char *formatMsgId, int count) {
    return format_CStr(cstrCount_Lang(formatMsgId, count), count);
}

const char *formatCStrs_Lang(const char *formatMsgId, size_t count) {
    return format_CStr(cstrCount_Lang(formatMsgId, (int) count), count);
}

const char *format_Lang(const char *formatTextWithIds, ...) {
    iBlock *msg = new_Block(0);
    va_list args;
    va_start(args, formatTextWithIds);
    vprintf_Block(msg, translateCStr_Lang(formatTextWithIds), args);
    va_end(args);
    return cstr_Block(collect_Block(msg));
}
