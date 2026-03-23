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

#pragma once

#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>

#include "render/font.h"

iDeclareType(AppleFont)
iDeclareType(AppleTextRun)
iDeclareType(FontFile)
iDeclareClass(AppleText)

#define defaultSystemGlyphScale_AppleText 0.866f   /* empirically determined */

/* Font variant (size + style) with a lazily-created CTFont. */
struct Impl_AppleFont {
    iBaseFont font;
    CTFontRef ctFont;    /* NULL until first use (lazy load) */
    float     pointSize; /* target point size; computed at init, used when creating ctFont */
    int       vertOffset; /* pixels to shift baseline down to center glyph in line box */
};

void ensureCtFont_AppleFont_(iAppleFont *, CFArrayRef cascadeList);

/* Cached shaped text run with escape-parsed attributes. */
struct Impl_AppleTextRun {
    uint32_t        hash;        /* CRC for cache lookup */
    size_t          rawTextLen;  /* raw byte length, for cache collision check */
    int             fontId;
    int             colorId;     /* base fg color ID; part of cache key */
    char           *text;        /* clean UTF-8 (escape-stripped); owned */
    size_t          textLen;
    CTTypesetterRef typesetter;
    size_t         *utf16ToSrc;  /* [utf16Idx] -> byte offset from rawText start; owned */
    CFIndex         utf16Len;    /* UTF-16 unit count of clean text */
    uint32_t        lastUsed;    /* LRU serial */
};

iDeclareTypeConstructionArgs(AppleTextRun,
    const char *rawText, size_t rawLen, int fontId, int colorId, iAppleText *tx)

/*----------------------------------------------------------------------------------------------*/

iAppleFont *appleFont_AppleText_    (iAppleText *, int fontId);
CFArrayRef  cascadeList_AppleText_  (const iAppleText *);

void        allocData_FontFile      (iFontFile *); /* backend-implemented */

extern CFStringRef lagBgKey_; /* custom CFAttributedString attribute key for per-run background
                                 color (CGColorRef value) */
