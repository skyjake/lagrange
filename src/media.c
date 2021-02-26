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

#include "media.h"
#include "gmdocument.h"
#include "gmrequest.h"
#include "ui/window.h"
#include "audio/player.h"
#include "app.h"

#include <the_Foundation/file.h>
#include <the_Foundation/ptrarray.h>
#include <stb_image.h>
#include <SDL_hints.h>
#include <SDL_render.h>
#include <SDL_timer.h>

iDeclareType(GmMediaProps)

struct Impl_GmMediaProps {
    iGmLinkId linkId;
    iString   mime;
    iString   url;
    iBool     isPermanent;
};

static void init_GmMediaProps_(iGmMediaProps *d) {
    d->linkId = 0;
    init_String(&d->mime);
    init_String(&d->url);
    d->isPermanent = iFalse;
}

static void deinit_GmMediaProps_(iGmMediaProps *d) {
    deinit_String(&d->url);
    deinit_String(&d->mime);
}

/*----------------------------------------------------------------------------------------------*/

iDeclareType(GmImage)

struct Impl_GmImage {
    iGmMediaProps props;
    iBlock        partialData; /* cleared when image is converted to texture */
    iInt2         size;
    size_t        numBytes;
    SDL_Texture * texture;
};

void init_GmImage(iGmImage *d, const iBlock *data) {
    init_GmMediaProps_(&d->props);
    initCopy_Block(&d->partialData, data);
    d->size     = zero_I2();
    d->numBytes = 0;
    d->texture  = NULL;
}

void deinit_GmImage(iGmImage *d) {
    deinit_Block(&d->partialData);
    SDL_DestroyTexture(d->texture);
    deinit_GmMediaProps_(&d->props);
}

void makeTexture_GmImage(iGmImage *d) {
    iBlock *data     = &d->partialData;
    d->numBytes      = size_Block(data);
    uint8_t *imgData = stbi_load_from_memory(
        constData_Block(data), size_Block(data), &d->size.x, &d->size.y, NULL, 4);
    if (!imgData) {
        d->size    = zero_I2();
        d->texture = NULL;
    }
    else {
        /* TODO: Save some memory by checking if the alpha channel is actually in use. */
        /* TODO: Resize down to min(maximum texture size, window size). */
        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
            imgData, d->size.x, d->size.y, 32, d->size.x * 4, SDL_PIXELFORMAT_ABGR8888);
        /* TODO: In multiwindow case, all windows must have the same shared renderer?
           Or at least a shared context. */
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1"); /* linear scaling */
        d->texture = SDL_CreateTextureFromSurface(renderer_Window(get_Window()), surface);
        SDL_FreeSurface(surface);
        stbi_image_free(imgData);
    }
    clear_Block(data);
}

iDefineTypeConstructionArgs(GmImage, (const iBlock *data), data)

/*----------------------------------------------------------------------------------------------*/

iDeclareType(GmAudio)

struct Impl_GmAudio {
    iGmMediaProps props;
    iPlayer *player;
};

void init_GmAudio(iGmAudio *d) {
    init_GmMediaProps_(&d->props);
    d->player = new_Player();
}

void deinit_GmAudio(iGmAudio *d) {
    delete_Player(d->player);
    deinit_GmMediaProps_(&d->props);
}

iDefineTypeConstruction(GmAudio)

/*----------------------------------------------------------------------------------------------*/

iDeclareType(GmDownload)

struct Impl_GmDownload {
    iGmMediaProps props;
    uint64_t      numBytes;
    iTime         startTime;
    uint32_t      rateStartTime;
    size_t        rateNumBytes;
    float         currentRate;
    iString *     path;
    iFile *       file;
};

static iBool openFile_GmDownload_(iGmDownload *d) {
    iAssert(!isEmpty_String(&d->props.url));
    d->path = copy_String(downloadPathForUrl_App(&d->props.url, &d->props.mime));
    d->file = new_File(d->path);
    if (!open_File(d->file, writeOnly_FileMode)) {
        return iFalse;
    }
    return iTrue;
}

static void closeFile_GmDownload_(iGmDownload *d) {
    d->currentRate = (float) (d->numBytes / elapsedSeconds_Time(&d->startTime));
    iReleasePtr(&d->file);
}

void init_GmDownload(iGmDownload *d) {
    init_GmMediaProps_(&d->props);
    initCurrent_Time(&d->startTime);
    d->numBytes      = 0;
    d->rateStartTime = SDL_GetTicks();
    d->rateNumBytes  = 0;
    d->currentRate   = 0.0f;
    d->path          = NULL;
    d->file          = NULL;
}

void deinit_GmDownload(iGmDownload *d) {
    closeFile_GmDownload_(d);
    deinit_GmMediaProps_(&d->props);
    delete_String(d->path);
}

static void writeToFile_GmDownload_(iGmDownload *d, const iBlock *data) {
    const static unsigned rateInterval_ = 1000;
    iAssert(d->file);
    writeData_File(d->file,
                   constBegin_Block(data) + d->numBytes,
                   size_Block(data) - d->numBytes);
    const size_t newBytes = size_Block(data) - d->numBytes;
    d->numBytes = size_Block(data);
    d->rateNumBytes += newBytes;
    const uint32_t now = SDL_GetTicks();
    if (now - d->rateStartTime > rateInterval_) {
        const double elapsed = (double) (now - d->rateStartTime) / 1000.0;
        d->rateStartTime     = now;
        d->currentRate       = (float) (d->rateNumBytes / elapsed);
        d->rateNumBytes      = 0;
    }
}

iDefineTypeConstruction(GmDownload)

/*----------------------------------------------------------------------------------------------*/

struct Impl_Media {
    iPtrArray images;
    iPtrArray audio;
    iPtrArray downloads;
};

iDefineTypeConstruction(Media)

void init_Media(iMedia *d) {
    init_PtrArray(&d->images);
    init_PtrArray(&d->audio);
    init_PtrArray(&d->downloads);
}

void deinit_Media(iMedia *d) {
    clear_Media(d);
    deinit_PtrArray(&d->downloads);
    deinit_PtrArray(&d->audio);
    deinit_PtrArray(&d->images);
}

void clear_Media(iMedia *d) {
    iForEach(PtrArray, i, &d->images) {
        deinit_GmImage(i.ptr);
    }
    clear_PtrArray(&d->images);
    iForEach(PtrArray, a, &d->audio) {
        deinit_GmAudio(a.ptr);
    }
    clear_PtrArray(&d->audio);
    iForEach(PtrArray, n, &d->downloads) {
        deinit_GmDownload(n.ptr);
    }
    clear_PtrArray(&d->downloads);
}

iBool setDownloadUrl_Media(iMedia *d, iGmLinkId linkId, const iString *url) {
    iGmDownload *dl       = NULL;
    iMediaId     existing = findLinkDownload_Media(d, linkId);
    iBool        isNew    = iFalse;
    if (!existing) {
        isNew = iTrue;
        dl = new_GmDownload();
        dl->props.linkId = linkId;
        dl->props.isPermanent = iTrue;
        set_String(&dl->props.url, url);
        pushBack_PtrArray(&d->downloads, dl);
    }
    else {
        iGmDownload *dl = at_PtrArray(&d->downloads, existing - 1);
        set_String(&dl->props.url, url);
    }
    return isNew;
}

iBool setData_Media(iMedia *d, iGmLinkId linkId, const iString *mime, const iBlock *data,
                    int flags) {
    const iBool isPartial  = (flags & partialData_MediaFlag) != 0;
    const iBool allowHide  = (flags & allowHide_MediaFlag) != 0;
    const iBool isDeleting = (!mime || !data);
    iMediaId    existing   = findLinkImage_Media(d, linkId);
    iBool       isNew      = iFalse;
    if (existing) {
        iGmImage *img;
        if (isDeleting) {
            take_PtrArray(&d->images, existing - 1, (void **) &img);
            delete_GmImage(img);
        }
        else {
            img = at_PtrArray(&d->images, existing - 1);
            iAssert(equal_String(&img->props.mime, mime)); /* MIME cannot change */
            set_Block(&img->partialData, data);
            if (!isPartial) {
                makeTexture_GmImage(img);
            }
        }
    }
    else if ((existing = findLinkAudio_Media(d, linkId)) != 0) {
        iGmAudio *audio;
        if (isDeleting) {
            take_PtrArray(&d->audio, existing - 1, (void **) &audio);
            delete_GmAudio(audio);
        }
        else {
            audio = at_PtrArray(&d->audio, existing - 1);
            iAssert(equal_String(&audio->props.mime, mime)); /* MIME cannot change */
            updateSourceData_Player(audio->player, mime, data, append_PlayerUpdate);
            if (!isStarted_Player(audio->player)) {
                /* Maybe the previous updates didn't have enough data. */
                start_Player(audio->player);
            }
            if (!isPartial) {
                updateSourceData_Player(audio->player, NULL, NULL, complete_PlayerUpdate);
            }
        }
    }
    else if ((existing = findLinkDownload_Media(d, linkId)) != 0) {
        iGmDownload *dl;
        if (isDeleting) {
            take_PtrArray(&d->downloads, existing - 1, (void **) &dl);
            delete_GmDownload(dl);
        }
        else {
            dl = at_PtrArray(&d->downloads, existing - 1);
            if (isEmpty_String(&dl->props.mime)) {
                set_String(&dl->props.mime, mime);
            }
            if (!dl->file) {
                openFile_GmDownload_(dl);
            }
            writeToFile_GmDownload_(dl, data);
            if (!isPartial) {
                closeFile_GmDownload_(dl);
            }
        }
    }
    else if (!isDeleting) {
        if (startsWith_String(mime, "image/")) {
            /* Copy the image to a texture. */
            iGmImage *img = new_GmImage(data);
            img->props.linkId = linkId; /* TODO: use a hash? */
            img->props.isPermanent = !allowHide;
            set_String(&img->props.mime, mime);
            pushBack_PtrArray(&d->images, img);
            if (!isPartial) {
                makeTexture_GmImage(img);
            }
            isNew = iTrue;
        }
        else if (startsWith_String(mime, "audio/")) {
            iGmAudio *audio = new_GmAudio();
            audio->props.linkId = linkId; /* TODO: use a hash? */
            audio->props.isPermanent = !allowHide;
            set_String(&audio->props.mime, mime);
            updateSourceData_Player(audio->player, mime, data, replace_PlayerUpdate);
            if (!isPartial) {
                updateSourceData_Player(audio->player, NULL, NULL, complete_PlayerUpdate);
            }
            pushBack_PtrArray(&d->audio, audio);
            /* Start playing right away. */
            start_Player(audio->player);
            postCommandf_App("media.player.started player:%p", audio->player);
            isNew = iTrue;
        }
    }
    return isNew;
}

iMediaId findLinkImage_Media(const iMedia *d, iGmLinkId linkId) {
    /* TODO: use a hash */
    iConstForEach(PtrArray, i, &d->images) {
        const iGmImage *img = i.ptr;
        if (img->props.linkId == linkId) {
            return index_PtrArrayConstIterator(&i) + 1;
        }
    }
    return 0;
}

size_t numAudio_Media(const iMedia *d) {
    return size_PtrArray(&d->audio);
}

iMediaId findLinkAudio_Media(const iMedia *d, iGmLinkId linkId) {
    /* TODO: use a hash */
    iConstForEach(PtrArray, i, &d->audio) {
        const iGmAudio *audio = i.ptr;
        if (audio->props.linkId == linkId) {
            return index_PtrArrayConstIterator(&i) + 1;
        }
    }
    return 0;
}

iMediaId findLinkDownload_Media(const iMedia *d, uint16_t linkId) {
    iConstForEach(PtrArray, i, &d->downloads) {
        const iGmDownload *dl = i.ptr;
        if (dl->props.linkId == linkId) {
            return index_PtrArrayConstIterator(&i) + 1;
        }
    }
    return 0;
}

iInt2 imageSize_Media(const iMedia *d, iMediaId imageId) {
    if (imageId > 0 && imageId <= size_PtrArray(&d->images)) {
        const iGmImage *img = constAt_PtrArray(&d->images, imageId - 1);
        return img->size;
    }
    return zero_I2();
}

SDL_Texture *imageTexture_Media(const iMedia *d, uint16_t imageId) {
    if (imageId > 0 && imageId <= size_PtrArray(&d->images)) {
        const iGmImage *img = constAt_PtrArray(&d->images, imageId - 1);
        return img->texture;
    }
    return NULL;
}

iBool imageInfo_Media(const iMedia *d, iMediaId imageId, iGmMediaInfo *info_out) {
    if (imageId > 0 && imageId <= size_PtrArray(&d->images)) {
        const iGmImage *img   = constAt_PtrArray(&d->images, imageId - 1);
        info_out->numBytes    = img->numBytes;
        info_out->type        = cstr_String(&img->props.mime);
        info_out->isPermanent = img->props.isPermanent;
        return iTrue;
    }
    iZap(*info_out);
    return iFalse;
}

iPlayer *audioData_Media(const iMedia *d, iMediaId audioId) {
    if (audioId > 0 && audioId <= size_PtrArray(&d->audio)) {
        const iGmAudio *audio = constAt_PtrArray(&d->audio, audioId - 1);
        return audio->player;
    }
    return NULL;
}

iBool audioInfo_Media(const iMedia *d, iMediaId audioId, iGmMediaInfo *info_out) {
    if (audioId > 0 && audioId <= size_PtrArray(&d->audio)) {
        const iGmAudio *audio = constAt_PtrArray(&d->audio, audioId - 1);
        info_out->type        = cstr_String(&audio->props.mime);
        info_out->isPermanent = audio->props.isPermanent;
        return iTrue;
    }
    iZap(*info_out);
    return iFalse;
}

iPlayer *audioPlayer_Media(const iMedia *d, iMediaId audioId) {
    if (audioId > 0 && audioId <= size_PtrArray(&d->audio)) {
        const iGmAudio *audio = constAt_PtrArray(&d->audio, audioId - 1);
        return audio->player;
    }
    return NULL;
}

iBool downloadInfo_Media(const iMedia *d, iMediaId downloadId, iGmMediaInfo *info_out) {
    if (downloadId > 0 && downloadId <= size_PtrArray(&d->downloads)) {
        const iGmDownload *dl = constAt_PtrArray(&d->downloads, downloadId - 1);
        info_out->type = cstr_String(&dl->props.mime);
        info_out->isPermanent = dl->props.isPermanent;
        info_out->numBytes = dl->numBytes;
        return iTrue;
    }
    iZap(*info_out);
    return iFalse;
}

void downloadStats_Media(const iMedia *d, iMediaId downloadId, const iString **path_out,
                         float *bytesPerSecond_out, iBool *isFinished_out) {
    *path_out = NULL;
    *bytesPerSecond_out = 0.0f;
    *isFinished_out = iFalse;
    if (downloadId > 0 && downloadId <= size_PtrArray(&d->downloads)) {
        const iGmDownload *dl = constAt_PtrArray(&d->downloads, downloadId - 1);
        if (dl->path) {
            *path_out = dl->path;
        }
        *bytesPerSecond_out = dl->currentRate;
        *isFinished_out = (dl->path && !dl->file);
    }
}

/*----------------------------------------------------------------------------------------------*/

static void updated_MediaRequest_(iAnyObject *obj) {
    iMediaRequest *d = obj;
    postCommandf_App("media.updated link:%u request:%p", d->linkId, d);
}

static void finished_MediaRequest_(iAnyObject *obj) {
    iMediaRequest *d = obj;
    postCommandf_App("media.finished link:%u request:%p", d->linkId, d);
}

void init_MediaRequest(iMediaRequest *d, iDocumentWidget *doc, unsigned int linkId, const iString *url) {
    d->doc    = doc;
    d->linkId = linkId;
    d->req    = new_GmRequest(certs_App());
    setUrl_GmRequest(d->req, url);
    iConnect(GmRequest, d->req, updated, d, updated_MediaRequest_);
    iConnect(GmRequest, d->req, finished, d, finished_MediaRequest_);
    submit_GmRequest(d->req);
}

void deinit_MediaRequest(iMediaRequest *d) {
    iDisconnect(GmRequest, d->req, updated, d, updated_MediaRequest_);
    iDisconnect(GmRequest, d->req, finished, d, finished_MediaRequest_);
    iRelease(d->req);
}

iDefineObjectConstructionArgs(MediaRequest,
                              (iDocumentWidget *doc, unsigned int linkId, const iString *url),
                              doc, linkId, url)
iDefineClass(MediaRequest)
