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

#include <the_Foundation/block.h>
#include <the_Foundation/string.h>
#include <the_Foundation/vec2.h>
#include <SDL_render.h>

typedef uint16_t iMediaId;

iDeclareType(Player)
iDeclareType(GmImageInfo)
iDeclareType(GmAudioInfo)

struct Impl_GmImageInfo {
    iInt2       size;
    size_t      numBytes;
    const char *mime;
    iBool       isPermanent;
};

struct Impl_GmAudioInfo {
    const char *mime;
    iBool       isPermanent;
};

iDeclareType(Media)
iDeclareTypeConstruction(Media)

enum iMediaFlags {
    allowHide_MediaFlag   = iBit(1),
    partialData_MediaFlag = iBit(2),
};

void    clear_Media     (iMedia *);
void    setData_Media   (iMedia *, uint16_t linkId, const iString *mime, const iBlock *data, int flags);

iMediaId        findLinkImage_Media (const iMedia *, uint16_t linkId);
iBool           imageInfo_Media     (const iMedia *, iMediaId imageId, iGmImageInfo *info_out);
SDL_Texture *   imageTexture_Media  (const iMedia *, iMediaId imageId);

size_t          numAudio_Media      (const iMedia *);
iMediaId        findLinkAudio_Media (const iMedia *, uint16_t linkId);
iBool           audioInfo_Media     (const iMedia *, iMediaId audioId, iGmAudioInfo *info_out);
iPlayer *       audioPlayer_Media   (const iMedia *, iMediaId audioId);
