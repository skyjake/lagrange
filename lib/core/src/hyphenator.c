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

#include "lagrange/hyphenator.h"
#include "lagrange/resources.h"

#include <the_Foundation/array.h>
#include <the_Foundation/string.h>

iDeclareType(HyphenPattern)

/* Both point to null-terminated strings in a pool. There is one value digit per gap
   between the letters, including both ends. */
struct Impl_HyphenPattern {
    const char *letters; /* UTF-8 */
    const char *values;
};

static int cmp_HyphenPattern_(const void *a, const void *b) {
    return strcmp(((const iHyphenPattern *) a)->letters, ((const iHyphenPattern *) b)->letters);
}

static int cmpKey_(const char *key, const char *str, size_t len) {
    /* `str` is not null-terminated. */
    const int cmp = strncmp(key, str, len);
    return cmp ? cmp : key[len] ? 1 : 0;
}

static size_t lowerBound_(const iHyphenPattern *patterns, size_t lo, size_t hi, const char *str,
                          size_t len) {
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (cmpKey_(patterns[mid].letters, str, len) < 0) {
            lo = mid + 1;
        }
        else {
            hi = mid;
        }
    }
    return lo;
}

static void appendToPool_(iBlock *pool, iRangecc entry, iBool isException) {
    /* Patterns have digits between letters; exceptions have hyphens. */
    iString letters;
    iBlock  values;
    init_String(&letters);
    init_Block(&values, 0);
    pushBack_Block(&values, '0');
    for (const char *pos = entry.start; pos < entry.end; ) {
        iChar ch;
        const int len = decodeBytes_MultibyteChar(pos, entry.end, &ch);
        if (len <= 0) break;
        pos += len;
        if (isException ? ch == '-' : (ch >= '0' && ch <= '9')) {
            ((char *) data_Block(&values))[size_Block(&values) - 1] = isException ? '1' : (char) ch;
        }
        else {
            appendChar_String(&letters, lower_Char(ch));
            pushBack_Block(&values, '0');
        }
    }
    appendData_Block(pool, cstr_String(&letters), size_String(&letters) + 1);
    appendData_Block(pool, cstr_Block(&values), size_Block(&values) + 1);
    deinit_Block(&values);
    deinit_String(&letters);
}

static void index_(iArray *entries, const iBlock *pool) {
    for (const char *pos = constBegin_Block(pool); pos < constEnd_Block(pool); ) {
        iHyphenPattern pat = { .letters = pos };
        pat.values = pos + strlen(pos) + 1;
        pos = pat.values + strlen(pat.values) + 1;
        pushBack_Array(entries, &pat);
    }
    sort_Array(entries, cmp_HyphenPattern_);
}

iDeclareType(HyphenTrieNode)

/* Patterns are looked up byte by byte. The children of a node are contiguous and sorted. */
struct Impl_HyphenTrieNode {
    uint32_t    firstChild;
    uint16_t    numChildren;
    uint8_t     byte;
    const char *values; /* NULL if no pattern ends here */
};

static void buildTrie_(iArray *nodes, size_t nodeIndex, const iHyphenPattern *pats, size_t lo,
                       size_t hi, size_t depth) {
    /* Patterns in [lo, hi) are sorted and share their first `depth` bytes. */
    iHyphenTrieNode *node = at_Array(nodes, nodeIndex);
    while (lo < hi && !pats[lo].letters[depth]) {
        node->values = pats[lo++].values;
    }
    /* Allocate all the children first so they are contiguous. */
    const size_t first = size_Array(nodes);
    for (size_t i = lo; i < hi; i++) {
        const uint8_t byte = pats[i].letters[depth];
        if (i == lo || byte != (uint8_t) pats[i - 1].letters[depth]) {
            pushBack_Array(nodes, &(iHyphenTrieNode){ .byte = byte });
        }
    }
    node              = at_Array(nodes, nodeIndex);
    node->firstChild  = (uint32_t) first;
    node->numChildren = (uint16_t) (size_Array(nodes) - first);
    size_t child = first;
    for (size_t i = lo; i < hi; child++) {
        const char byte = pats[i].letters[depth];
        size_t     end  = i + 1;
        while (end < hi && pats[end].letters[depth] == byte) {
            end++;
        }
        buildTrie_(nodes, child, pats, i, end, depth + 1);
        i = end;
    }
}

static const iHyphenTrieNode *child_(const iHyphenTrieNode *nodes, const iHyphenTrieNode *node,
                                     uint8_t byte) {
    const iHyphenTrieNode *children = nodes + node->firstChild;
    for (unsigned i = 0; i < node->numChildren; i++) {
        if (children[i].byte >= byte) {
            return children[i].byte == byte ? &children[i] : NULL;
        }
    }
    return NULL;
}

/*----------------------------------------------------------------------------------------------*/

struct Impl_Hyphenator {
    iBlock patternPool;
    iBlock exceptionPool;
    iArray trie;       /* root is the first node */
    iArray exceptions; /* sorted; values mark the breaks */
    int    leftMin;
    int    rightMin;
};

iDefineTypeConstructionArgs(Hyphenator, (const iBlock *patterns), patterns)

void init_Hyphenator(iHyphenator *d, const iBlock *patterns) {
    init_Block(&d->patternPool, 0);
    init_Block(&d->exceptionPool, 0);
    init_Array(&d->trie, sizeof(iHyphenTrieNode));
    init_Array(&d->exceptions, sizeof(iHyphenPattern));
    d->leftMin  = 2;
    d->rightMin = 2;
    iBool    isException = iFalse;
    iRangecc line        = iNullRange;
    while (nextSplit_Rangecc(range_Block(patterns), "\n", &line)) {
        iRangecc entry = line;
        trim_Rangecc(&entry);
        if (isEmpty_Range(&entry) || *entry.start == '#') {
            continue;
        }
        if (startsWith_Rangecc(entry, "@hyphenmin ")) {
            sscanf(entry.start + 11, "%d %d", &d->leftMin, &d->rightMin);
        }
        else if (equal_Rangecc(entry, "@patterns")) {
            isException = iFalse;
        }
        else if (equal_Rangecc(entry, "@exceptions")) {
            isException = iTrue;
        }
        else {
            appendToPool_(isException ? &d->exceptionPool : &d->patternPool, entry, isException);
        }
    }
    index_(&d->exceptions, &d->exceptionPool);
    /* Patterns are only needed in sorted order for building the trie. */ {
        iArray pats;
        init_Array(&pats, sizeof(iHyphenPattern));
        index_(&pats, &d->patternPool);
        pushBack_Array(&d->trie, &(iHyphenTrieNode){ 0 });
        buildTrie_(&d->trie, 0, constData_Array(&pats), 0, size_Array(&pats), 0);
        deinit_Array(&pats);
    }
    d->leftMin  = iMax(1, d->leftMin);
    d->rightMin = iMax(1, d->rightMin);
}

void deinit_Hyphenator(iHyphenator *d) {
    deinit_Array(&d->exceptions);
    deinit_Array(&d->trie);
    deinit_Block(&d->exceptionPool);
    deinit_Block(&d->patternPool);
}

size_t hyphenate_Hyphenator(const iHyphenator *d, const iChar *word, size_t len,
                            uint8_t *breaks_out) {
    memset(breaks_out, 0, len);
    if (len < (size_t) (d->leftMin + d->rightMin) || len > maxWordLength_Hyphenator) {
        return 0;
    }
    /* Lowercase UTF-8 with boundary dots. */
    char    padded[(maxWordLength_Hyphenator + 2) * iMultibyteCharMaxSize + 1];
    size_t  offsets[maxWordLength_Hyphenator + 3]; /* of each character, plus the end */
    uint8_t scores[maxWordLength_Hyphenator + 3]; /* of each gap */
    const size_t numChars = len + 2;
    size_t       size     = 0;
    iZap(scores);
    for (size_t i = 0; i < numChars; i++) {
        offsets[i] = size;
        if (i == 0 || i == numChars - 1) {
            padded[size++] = '.';
        }
        else {
            const iChar ch = word[i - 1];
            if (ch < 0x80) {
                padded[size++] = (char) (ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
            }
            else {
                iMultibyteChar mb;
                init_MultibyteChar(&mb, lower_Char(ch));
                const size_t mbLen = strlen(mb.bytes);
                memcpy(padded + size, mb.bytes, mbLen);
                size += mbLen;
            }
        }
    }
    offsets[numChars] = size;
    padded[size]      = 0;
    /* Exceptions are hyphenated as listed. */
    const char  *wordStart = padded + offsets[1];
    const size_t wordSize  = offsets[numChars - 1] - offsets[1];
    const iHyphenPattern *excs      = constData_Array(&d->exceptions);
    const size_t          numExcs   = size_Array(&d->exceptions);
    const size_t          exc       = lowerBound_(excs, 0, numExcs, wordStart, wordSize);
    if (exc < numExcs && !cmpKey_(excs[exc].letters, wordStart, wordSize)) {
        const char *values = excs[exc].values;
        for (size_t i = 0; values[i]; i++) {
            scores[i + 1] = values[i] - '0';
        }
    }
    else {
        /* Apply all patterns that match anywhere in the word. */
        const iHyphenTrieNode *nodes = constData_Array(&d->trie);
        for (size_t i = 0; i < numChars; i++) {
            const iHyphenTrieNode *node = nodes;
            for (size_t j = i + 1; j <= numChars && node; j++) {
                for (size_t pos = offsets[j - 1]; pos < offsets[j] && node; pos++) {
                    node = child_(nodes, node, (uint8_t) padded[pos]);
                }
                if (node && node->values) {
                    for (size_t k = 0; node->values[k]; k++) {
                        scores[i + k] = iMax(scores[i + k], node->values[k] - '0');
                    }
                }
            }
        }
    }
    size_t count = 0;
    for (size_t i = d->leftMin; i <= len - d->rightMin; i++) {
        if (scores[i + 1] & 1) {
            breaks_out[i] = 1;
            count++;
        }
    }
    return count;
}

/*----------------------------------------------------------------------------------------------*/

static struct {
    const char   *lang;
    const iBlock *patterns;
    iHyphenator  *hyphenator;
} hyphenators_[] = {
    { "en", &blobHyphenEnUs_Resources, NULL },
};

const iHyphenator *forLanguage_Hyphenator(iRangecc lang) {
    /* Only the primary language subtag is used. */
    for (const char *pos = lang.start; pos < lang.end; pos++) {
        if (*pos == '-' || *pos == '_') {
            lang.end = pos;
            break;
        }
    }
    iForIndices(i, hyphenators_) {
        if (equalCase_Rangecc(lang, hyphenators_[i].lang)) {
            if (!hyphenators_[i].hyphenator && !isEmpty_Block(hyphenators_[i].patterns)) {
                hyphenators_[i].hyphenator = new_Hyphenator(hyphenators_[i].patterns);
            }
            return hyphenators_[i].hyphenator;
        }
    }
    return NULL;
}

void deinit_Hyphenators(void) {
    iForIndices(i, hyphenators_) {
        delete_Hyphenator(hyphenators_[i].hyphenator);
        hyphenators_[i].hyphenator = NULL;
    }
}
