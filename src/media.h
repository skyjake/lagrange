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
iDeclareType(GmMediaInfo)

struct Impl_GmMediaInfo {
    const char *type; /* MIME */
    size_t      numBytes;
    iBool       isPermanent;
};

iDeclareType(Media)
iDeclareTypeConstruction(Media)

enum iMediaFlags {
    allowHide_MediaFlag   = iBit(1),
    partialData_MediaFlag = iBit(2),
};

void    clear_Media             (iMedia *);
iBool   setDownloadUrl_Media    (iMedia *, uint16_t linkId, const iString *url);
iBool   setData_Media           (iMedia *, uint16_t linkId, const iString *mime, const iBlock *data, int flags);

iMediaId        findLinkImage_Media (const iMedia *, uint16_t linkId);
iBool           imageInfo_Media     (const iMedia *, iMediaId imageId, iGmMediaInfo *info_out);
iInt2           imageSize_Media     (const iMedia *, iMediaId imageId);
SDL_Texture *   imageTexture_Media  (const iMedia *, iMediaId imageId);

size_t          numAudio_Media      (const iMedia *);
iMediaId        findLinkAudio_Media (const iMedia *, uint16_t linkId);
iBool           audioInfo_Media     (const iMedia *, iMediaId audioId, iGmMediaInfo *info_out);
iPlayer *       audioPlayer_Media   (const iMedia *, iMediaId audioId);

iMediaId        findLinkDownload_Media  (const iMedia *, uint16_t linkId);
iBool           downloadInfo_Media      (const iMedia *, iMediaId downloadId, iGmMediaInfo *info_out);
void            downloadStats_Media     (const iMedia *, iMediaId downloadId, const iString **path_out,
                                         float *bytesPerSecond_out, iBool *isFinished_out);

/*----------------------------------------------------------------------------------------------*/

iDeclareType(GmRequest)
iDeclareType(DocumentWidget)

iDeclareClass(MediaRequest)

struct Impl_MediaRequest {
    iObject          object;
    iDocumentWidget *doc;
    unsigned int     linkId;
    iGmRequest *     req;
};

iDeclareObjectConstructionArgs(MediaRequest, iDocumentWidget *doc, unsigned int linkId,
                               const iString *url, iBool enableFilters)
