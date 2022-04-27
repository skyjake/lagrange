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

#include "fontpack.h"

#include <the_Foundation/block.h>
#include <the_Foundation/string.h>
#include <the_Foundation/vec2.h>
#include <SDL_render.h>

iDeclareType(Player)
iDeclareType(GmMediaInfo)

struct Impl_GmMediaInfo {
    const char *type; /* MIME */
    size_t      numBytes;
    iBool       isPermanent;
};

iDeclareType(MediaId)
iDeclareType(Media)
iDeclareTypeConstruction(Media)

enum iMediaFlags {
    allowHide_MediaFlag   = iBit(1),
    partialData_MediaFlag = iBit(2),
};

enum iMediaType { /* Note: There is a limited number of bits for these; see GmRun. */
    none_MediaType,
    image_MediaType,
    //animatedImage_MediaType, /* TODO */
    audio_MediaType,
    download_MediaType,
    max_MediaType
};

struct Impl_MediaId {
    enum iMediaType type;
    uint16_t id; /* see GmRun for actually used number of bits */
};

iLocalDef size_t index_MediaId(const iMediaId mediaId) {
    return (size_t) mediaId.id - 1;
}

#define iInvalidMediaId     (iMediaId){ none_MediaType, 0 }

void            clear_Media             (iMedia *);
iBool           setUrl_Media            (iMedia *, uint16_t linkId, enum iMediaType mediaType, const iString *url);
iBool           setData_Media           (iMedia *, uint16_t linkId, const iString *mime, const iBlock *data, int flags);

size_t          memorySize_Media        (const iMedia *);
iMediaId        findMediaForLink_Media  (const iMedia *, uint16_t linkId, enum iMediaType mediaType);

iMediaId        id_Media        (const iMedia *, uint16_t linkId, enum iMediaType type);
iBool           info_Media      (const iMedia *, iMediaId mediaId, iGmMediaInfo *info_out);

iLocalDef iMediaId findLinkImage_Media(const iMedia *d, uint16_t linkId) {
    return findMediaForLink_Media(d, linkId, image_MediaType);
}
iLocalDef iMediaId findLinkAudio_Media (const iMedia *d, uint16_t linkId) {
    return findMediaForLink_Media(d, linkId, audio_MediaType);
}
iLocalDef iMediaId findLinkDownload_Media(const iMedia *d, uint16_t linkId) {
    return findMediaForLink_Media(d, linkId, download_MediaType);
}

iLocalDef iBool imageInfo_Media(const iMedia *d, uint16_t mediaId, iGmMediaInfo *info_out) {
    return info_Media(d, (iMediaId){ image_MediaType, mediaId }, info_out);
}
iLocalDef iBool audioInfo_Media(const iMedia *d, uint16_t mediaId, iGmMediaInfo *info_out) {
    return info_Media(d, (iMediaId){ audio_MediaType, mediaId }, info_out);
}
iLocalDef iBool downloadInfo_Media(const iMedia *d, uint16_t mediaId, iGmMediaInfo *info_out) {
    return info_Media(d, (iMediaId){ download_MediaType, mediaId }, info_out);
}

iInt2           imageSize_Media         (const iMedia *, iMediaId imageId);
SDL_Texture *   imageTexture_Media      (const iMedia *, iMediaId imageId);

size_t          numAudio_Media          (const iMedia *);
iPlayer *       audioPlayer_Media       (const iMedia *, iMediaId audioId);
void            pauseAllPlayers_Media   (const iMedia *, iBool setPaused);

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
    
iMediaRequest * newReused_MediaRequest  (iDocumentWidget *doc, unsigned int linkId,
                                         iGmRequest *request);
