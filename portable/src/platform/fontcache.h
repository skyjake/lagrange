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

/* Shared font cache entry data structures and serialization.
   Used by both the Apple CoreText backend (apple_fontcache.c) and the
   FreeType backend (freetype_fontcache.c).

   The per-style `identifier` field carries a PostScript name on Apple
   and a file path on FreeType. The `colIndex` is used only by FreeType
   (face index within a TTC); Apple always stores 0 there. */

#pragma once

#include "../fontpack.h"

#include <the_Foundation/stream.h>
#include <the_Foundation/string.h>

iDeclareType(FontCacheStyle)
iDeclareTypeConstruction(FontCacheStyle)
iDeclareTypeSerialization(FontCacheStyle)

struct Impl_FontCacheStyle {
    iString  identifier; /* PostScript name (CoreText) or file path (FreeType) */
    int32_t  colIndex;   /* face index inside a TTC; 0 for CoreText */
    int32_t  ascent;
    int32_t  descent;
    int32_t  lineGap;
    int32_t  winAscent;
    int32_t  winDescent;
    int32_t  emAdvance;
    int32_t  unitsPerEm;
};

iDeclareType(FontCacheEntry)
iDeclareTypeConstruction(FontCacheEntry)
iDeclareTypeSerialization(FontCacheEntry)

struct Impl_FontCacheEntry {
    iString          id;
    iString          name;
    uint32_t         flags;
    iFontCacheStyle  styles[max_FontStyle];
};
