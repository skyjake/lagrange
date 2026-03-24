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

/* Backend-private data structures for the STB TrueType + HarfBuzz rendering backend.
   Only include this from files that are part of the STB backend (text_stb.c,
   text_simple.c). Do not include from fontpack.h or other widely-included headers. */

#pragma once

#include "../fontpack.h"

#if defined (LAGRANGE_ENABLE_STB_TRUETYPE)
#   include "../stb_truetype.h"
#endif

#if defined (LAGRANGE_ENABLE_HARFBUZZ)
#   include <hb.h>
#endif

#if defined (LAGRANGE_ENABLE_STB_TRUETYPE)

typedef struct {
    stbtt_fontinfo stbInfo;
#   if defined (LAGRANGE_ENABLE_HARFBUZZ)
    hb_blob_t *hbBlob;
    hb_face_t *hbFace;
    hb_font_t *hbFont;
#   endif
} iStbFontData;

iLocalDef iStbFontData *stbData_FontFile(const iFontFile *d) {
    return (iStbFontData *) d->data;
}

iLocalDef uint32_t findGlyphIndex_FontFile(const iFontFile *d, iChar ch) {
    return stbtt_FindGlyphIndex(&stbData_FontFile(d)->stbInfo, ch);
}

#endif /* LAGRANGE_ENABLE_STB_TRUETYPE */
