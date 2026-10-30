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

#include "fontcache.h"

void init_FontCacheStyle(iFontCacheStyle *d) {
    iZap(*d);
    init_String(&d->identifier);
}

void deinit_FontCacheStyle(iFontCacheStyle *d) {
    deinit_String(&d->identifier);
}

void serialize_FontCacheStyle(const iFontCacheStyle *d, iStream *out) {
    serialize_String(&d->identifier, out);
    write32_Stream(out, d->colIndex);
    write32_Stream(out, d->ascent);
    write32_Stream(out, d->descent);
    write32_Stream(out, d->lineGap);
    write32_Stream(out, d->winAscent);
    write32_Stream(out, d->winDescent);
    write32_Stream(out, d->emAdvance);
    write32_Stream(out, d->unitsPerEm);
}

void deserialize_FontCacheStyle(iFontCacheStyle *d, iStream *in) {
    deserialize_String(&d->identifier, in);
    d->colIndex   = read32_Stream(in);
    d->ascent     = read32_Stream(in);
    d->descent    = read32_Stream(in);
    d->lineGap    = read32_Stream(in);
    d->winAscent  = read32_Stream(in);
    d->winDescent = read32_Stream(in);
    d->emAdvance  = read32_Stream(in);
    d->unitsPerEm = read32_Stream(in);
}

/*----------------------------------------------------------------------------------------------*/

void init_FontCacheEntry(iFontCacheEntry *d) {
    iZap(*d);
    init_String(&d->id);
    init_String(&d->name);
    iForIndices(i, d->styles) {
        init_FontCacheStyle(&d->styles[i]);
    }
}

void deinit_FontCacheEntry(iFontCacheEntry *d) {
    iForIndices(i, d->styles) {
        deinit_FontCacheStyle(&d->styles[i]);
    }
    deinit_String(&d->name);
    deinit_String(&d->id);
}

void serialize_FontCacheEntry(const iFontCacheEntry *d, iStream *out) {
    serialize_String(&d->id, out);
    serialize_String(&d->name, out);
    writeU32_Stream(out, d->flags);
    iForIndices(i, d->styles) {
        serialize_FontCacheStyle(&d->styles[i], out);
    }
}

void deserialize_FontCacheEntry(iFontCacheEntry *d, iStream *in) {
    deserialize_String(&d->id, in);
    deserialize_String(&d->name, in);
    d->flags = readU32_Stream(in);
    iForIndices(i, d->styles) {
        deserialize_FontCacheStyle(&d->styles[i], in);
    }
}
