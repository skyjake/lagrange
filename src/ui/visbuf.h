/* Copyright 2020 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include <the_Foundation/range.h>
#include <the_Foundation/vec2.h>
#include <SDL_render.h>

iDeclareType(VisBuf)
iDeclareType(VisBufTexture)

struct Impl_VisBufTexture {
    SDL_Texture *texture;
    int origin;
    iRangei validRange;
    void *user; /* user provided data pointer for additional per-buffer data */
};

#define numBuffers_VisBuf   ((size_t) 4)

struct Impl_VisBuf {
    iInt2 texSize;
    iRangei vis;
    iVisBufTexture buffers[numBuffers_VisBuf];
    void (*bufferInvalidated)(iVisBuf *, size_t index);
};

iDeclareTypeConstruction(VisBuf)

void    invalidate_VisBuf       (iVisBuf *);
iBool   alloc_VisBuf            (iVisBuf *, const iInt2 size, int granularity);
void    dealloc_VisBuf          (iVisBuf *);
iBool   reposition_VisBuf       (iVisBuf *, const iRangei vis); /* returns true if `vis` changes */
void    validate_VisBuf         (iVisBuf *);

iRangei allocRange_VisBuf       (const iVisBuf *);
iRangei bufferRange_VisBuf      (const iVisBuf *, size_t index);
void    invalidRanges_VisBuf    (const iVisBuf *, const iRangei full, iRangei *out_invalidRanges);
void    draw_VisBuf             (const iVisBuf *, iInt2 topLeft, iRangei yClipBounds);
