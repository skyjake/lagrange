/* Copyright 2023 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "snippets.h"
#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <the_Foundation/stringhash.h>
#include <the_Foundation/stream.h>
#include <the_Foundation/toml.h>

iDeclareClass(Snippet);

struct Impl_Snippet {
    iObject object;
    iString content;
};

static void init_Snippet(iSnippet *d, const iString *content) {
    initCopy_String(&d->content, content);
}

static void deinit_Snippet(iSnippet *d) {
    deinit_String(&d->content);
}

iDefineObjectConstructionArgs(Snippet, (const iString *content), content)
iDefineClass(Snippet)

/*----------------------------------------------------------------------------------------------*/

static const char *fileName_Snippets_ = "snippets.ini";

iDeclareType(Snippets);

struct Impl_Snippets {
    iStringHash *hash;
};

static iSnippets *snippets_;

void init_Snippets(const char *saveDir) {
    iAssert(!snippets_);
    iSnippets *d = snippets_ = iMalloc(Snippets);
    d->hash = new_StringHash();
    load_Snippets(saveDir);
}

void deinit_Snippets(void) {
    iAssert(snippets_);
    iSnippets *d = snippets_;
    iRelease(d->hash);
}

void serialize_Snippets(iStream *outs) {
    iAssert(snippets_);
    iSnippets *d = snippets_;
    iString *str = new_String();
    iConstForEach(StringHash, i, d->hash) {
        const iString  *key   = key_StringHashConstIterator(&i);
        const iSnippet *value = value_StringHashNode(i.value);
        format_String(str, "[%s]\ncontent = \"%s\"\n\n",
                      cstr_String(key),
                      cstrCollect_String(quote_String(&value->content, iFalse)));
        write_Stream(outs, utf8_String(str));
    }
    delete_String(str);
}

static void handleKeyValue_Snippets_(void *context, const iString *table, const iString *key,
                                     const iTomlValue *value) {
    if (!cmp_String(key, "content") && value->type == string_TomlType) {
        set_Snippets(table, value->value.string);
    }
}

void deserialize_Snippets(iStream *ins) {
    iAssert(snippets_);
    iSnippets *d = snippets_;
    clear_StringHash(d->hash);
    iTomlParser *toml = new_TomlParser();
    setHandlers_TomlParser(toml, NULL, handleKeyValue_Snippets_, NULL);
    parse_TomlParser(toml, collect_String(newBlock_String(collect_Block(readAll_Stream(ins)))));
    delete_TomlParser(toml);
}

void save_Snippets(const char *saveDir) {
    iFile *f = newCStr_File(concatPath_CStr(saveDir, fileName_Snippets_));
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        serialize_Snippets(stream_File(f));
    }
    iRelease(f);
}

void load_Snippets(const char *saveDir) {
    iFile *f = newCStr_File(concatPath_CStr(saveDir, fileName_Snippets_));
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        deserialize_Snippets(stream_File(f));
    }
    iRelease(f);
}

iBool set_Snippets(const iString *name, const iString *content) {
    iAssert(snippets_);
    iSnippets *d = snippets_;
    if (isEmpty_String(name) || (content && isEmpty_String(content))) {
        return iFalse;
    }
    if (content) {
        insert_StringHash(d->hash, name, iClob(new_Snippet(content)));
    }
    else {
        remove_StringHash(d->hash, name);
    }
    return iTrue;
}

const iString *get_Snippets(const iString *name) {
    iAssert(snippets_);
    iSnippets *d = snippets_;
    const iSnippet *value = value_StringHash(d->hash, name);
    if (value) {
        return &value->content;
    }
    return collectNew_String();
}

const iStringArray *names_Snippets(void) {
    iAssert(snippets_);
    iSnippets *d = snippets_;
    iStringArray *names = new_StringArray();
    iConstForEach(StringHash, i, d->hash) {
        pushBack_StringArray(names, key_StringHashConstIterator(&i));
    }
    return collect_StringArray(names);
}
