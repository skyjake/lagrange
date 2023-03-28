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
#include "ui/paint.h" /* size_SDLTexture */
#include "audio/player.h"
#include "app.h"
#include "stb_image.h"
#include "stb_image_resize.h"

#if defined (LAGRANGE_ENABLE_WEBP)
#   include <webp/decode.h>
#endif

#include <the_Foundation/file.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/stringlist.h>
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

static void applyImageStyle_(enum iImageStyle style, iInt2 size, uint8_t *imgData) {
    if (style == original_ImageStyle) {
        return;
    }
    uint8_t *pos       = imgData;
    size_t   numPixels = size.x * size.y;
    float    brighten  = 0.0f;
    if (style == bgFg_ImageStyle) {
        iColor dark  = get_Color(tmBackground_ColorId);
        iColor light = get_Color(tmParagraph_ColorId);
        if (hsl_Color(dark).lum > hsl_Color(light).lum) {
            iSwap(iColor, dark, light);
        }        
        while (numPixels-- > 0) {
            iHSLColor hsl = hsl_Color((iColor){ pos[0], pos[1], pos[2], 255 });
            const float s = 1.0f - hsl.lum;
            const float t = hsl.lum;
            pos[0] = dark.r * s + light.r * t;
            pos[1] = dark.g * s + light.g * t;
            pos[2] = dark.b * s + light.b * t;
            pos += 4;
        }        
        return;
    }
    iColor colorize = (iColor){ 255, 255, 255, 255 };
    if (style != grayscale_ImageStyle) {
        colorize = get_Color(style == textColorized_ImageStyle ? tmParagraph_ColorId
                                                               : tmPreformatted_ColorId);
        /* Compensate for change in mid-tones. */
        const int colMax = iMax(iMax(colorize.r, colorize.g), colorize.b);
        brighten = iClamp(1.0f - (colorize.r + colorize.g + colorize.b) / (colMax * 3), 0.0f, 0.5f);
    }
    iHSLColor hslColorize = hsl_Color(colorize);
    while (numPixels-- > 0) {
        iHSLColor hsl = hsl_Color((iColor){ pos[0], pos[1], pos[2], 255 });
        iHSLColor out = { hslColorize.hue, hslColorize.sat, hsl.lum, 1.0f };
        out.lum = powf(out.lum, 1.0f + brighten * 2);
        iColor outRgb = rgb_HSLColor(out);
        pos[0] = powf(outRgb.r / 255.0f, 1.0f - brighten * 0.75f) * 255;
        pos[1] = powf(outRgb.g / 255.0f, 1.0f - brighten * 0.75f) * 255;
        pos[2] = powf(outRgb.b / 255.0f, 1.0f - brighten * 0.75f) * 255;
        pos += 4;
    }
}

void makeTexture_GmImage(iGmImage *d) {
    iBlock *data     = &d->partialData;
    d->numBytes      = size_Block(data);
    uint8_t *imgData = NULL;
    if (cmp_String(&d->props.mime, "image/webp") == 0) {
#if defined (LAGRANGE_ENABLE_WEBP)
        imgData = WebPDecodeRGBA(constData_Block(data), size_Block(data), &d->size.x, &d->size.y);
#endif        
    }
    else {
        imgData = stbi_load_from_memory(
            constData_Block(data), (int) size_Block(data), &d->size.x, &d->size.y, NULL, 4);
        if (!imgData) {
            fprintf(stderr, "[media] image load failed: %s\n", stbi_failure_reason());
        }
    }
    if (!imgData) {
        d->size    = zero_I2();
        d->texture = NULL;
    }
    else {
        applyImageStyle_(prefs_App()->imageStyle, d->size, imgData);        
        /* TODO: Save some memory by checking if the alpha channel is actually in use. */
        iWindow *window  = get_Window();
        iInt2    texSize = d->size;
        /* Resize down to min(maximum texture size, window size). */ {
            SDL_Rect dispRect;
            SDL_GetDisplayBounds(SDL_GetWindowDisplayIndex(window->win), &dispRect);
            const iInt2 maxSize = min_I2(isEqual_I2(maxTextureSize_Window(window), zero_I2()) ?
                                         texSize : maxTextureSize_Window(window),
                                         coord_Window(window, dispRect.w, dispRect.h));
            iInt2 scaled = d->size;
            if (scaled.x > maxSize.x) {
                scaled.y = scaled.y * maxSize.x / scaled.x;
                scaled.x = maxSize.x;
            }
            if (scaled.y > maxSize.y) {
                scaled.x = scaled.x * maxSize.y / scaled.y;
                scaled.y = maxSize.y;
            }
            if (!isEqual_I2(scaled, d->size)) {
                uint8_t *scaledImgData = malloc(scaled.x * scaled.y * 4);
                stbir_resize_uint8(imgData, d->size.x, d->size.y, 4 * d->size.x,
                                   scaledImgData, scaled.x, scaled.y, scaled.x * 4, 4);
                free(imgData);
                imgData = scaledImgData;
                texSize = scaled;
                /* We keep d->size for the UI. */
            }
        }
        /* Create the texture. */
        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
            imgData, texSize.x, texSize.y, 32, texSize.x * 4, SDL_PIXELFORMAT_ABGR8888);
        /* TODO: In multiwindow case, all windows must have the same shared renderer?
           Or at least a shared context. */
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1"); /* linear scaling */
        d->texture = SDL_CreateTextureFromSurface(renderer_Window(window), surface);
        SDL_FreeSurface(surface);
        free(imgData);
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
#if defined (LAGRANGE_ENABLE_AUDIO)    
    d->player = new_Player();
#endif
}

void deinit_GmAudio(iGmAudio *d) {
#if defined (LAGRANGE_ENABLE_AUDIO)
    delete_Player(d->player);
#endif
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
    iPtrArray items[max_MediaType];
    /* TODO: Add a hash to quickly look up a link's media. */
};

iDefineTypeConstruction(Media)

void init_Media(iMedia *d) {
    iForIndices(i, d->items) {
        init_PtrArray(&d->items[i]);
    }
}

void deinit_Media(iMedia *d) {
    clear_Media(d);
    iForIndices(i, d->items) {
        deinit_PtrArray(&d->items[i]);
    }
}

void clear_Media(iMedia *d) {
    iForEach(PtrArray, i, &d->items[image_MediaType]) {
        deinit_GmImage(i.ptr);
    }
    iForEach(PtrArray, a, &d->items[audio_MediaType]) {
        deinit_GmAudio(a.ptr);
    }
    iForEach(PtrArray, n, &d->items[download_MediaType]) {
        deinit_GmDownload(n.ptr);
    }
    iForIndices(type, d->items) {
        clear_PtrArray(&d->items[type]);
    }
}

size_t memorySize_Media(const iMedia *d) {
    size_t memSize = 0;
    iConstForEach(PtrArray, i, &d->items[image_MediaType]) {
        const iGmImage *img = i.ptr;
        if (img->texture) {
            const iInt2 texSize = size_SDLTexture(img->texture);
            memSize += 4 * texSize.x * texSize.y; /* RGBA */
        }
        else {
            memSize += size_Block(&img->partialData);
        }
    }
#if defined (LAGRANGE_ENABLE_AUDIO)
    iConstForEach(PtrArray, a, &d->items[audio_MediaType]) {
        const iGmAudio *audio = a.ptr;
        if (audio->player) {
            memSize += sourceDataSize_Player(audio->player);
        }
    }
#endif
    iConstForEach(PtrArray, n, &d->items[download_MediaType]) {
        const iGmDownload *down = n.ptr;
        memSize += down->numBytes;
    }
    return memSize; 
}

iBool setUrl_Media(iMedia *d, iGmLinkId linkId, enum iMediaType mediaType, const iString *url) {
    iMediaId existing = findMediaForLink_Media(d, linkId, mediaType);
    const iBool isNew = !existing.id;
    iGmMediaProps *props = NULL;
    if (mediaType == download_MediaType) {
        iGmDownload *dl = NULL;
        if (isNew) {
            dl = new_GmDownload();
            pushBack_PtrArray(&d->items[download_MediaType], dl);
        }
        else {
            dl = at_PtrArray(&d->items[download_MediaType], index_MediaId(existing));
        }
        props = &dl->props;
    }
    if (props) {
        props->linkId = linkId;
        props->isPermanent = iTrue;
        set_String(&props->url, url);
    }
    return isNew;
}

iBool setData_Media(iMedia *d, iGmLinkId linkId, const iString *mime, const iBlock *data,
                    int flags) {
    const iBool isPartial  = (flags & partialData_MediaFlag) != 0;
    const iBool allowHide  = (flags & allowHide_MediaFlag) != 0;
    const iBool isDeleting = (!mime || !data);
    iMediaId    existing   = findMediaForLink_Media(d, linkId, none_MediaType);
    const size_t existingIndex = index_MediaId(existing);
    iBool       isNew      = iFalse;
    if (existing.type == image_MediaType) {
        iGmImage *img;
        if (isDeleting) {
            take_PtrArray(&d->items[image_MediaType], existingIndex, (void **) &img);
            delete_GmImage(img);
        }
        else {
            img = at_PtrArray(&d->items[image_MediaType], existingIndex);
            iAssert(equal_String(&img->props.mime, mime)); /* MIME cannot change */
            set_Block(&img->partialData, data);
            if (!isPartial) {
                makeTexture_GmImage(img);
            }
        }
    }
    else if (existing.type == audio_MediaType) {
#if defined (LAGRANGE_ENABLE_AUDIO)
        iGmAudio *audio;
        if (isDeleting) {
            take_PtrArray(&d->items[audio_MediaType], existingIndex, (void **) &audio);
            delete_GmAudio(audio);
        }
        else {
            audio = at_PtrArray(&d->items[audio_MediaType], existingIndex);
            iAssert(equal_String(&audio->props.mime, mime)); /* MIME cannot change */
            updateSourceData_Player(audio->player, mime, data, append_PlayerUpdate);
            if (!isPartial) {
                updateSourceData_Player(audio->player, NULL, NULL, complete_PlayerUpdate);
            }
            if (!isStarted_Player(audio->player)) {
                /* Maybe the previous updates didn't have enough data. */
                start_Player(audio->player);
            }
        }
#endif
    }
    else if (existing.type == download_MediaType) {
        iGmDownload *dl;
        if (isDeleting) {
            take_PtrArray(&d->items[download_MediaType], existingIndex, (void **) &dl);
            delete_GmDownload(dl);
        }
        else {
            dl = at_PtrArray(&d->items[download_MediaType], existingIndex);
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
            pushBack_PtrArray(&d->items[image_MediaType], img);
            if (!isPartial) {
                makeTexture_GmImage(img);
            }
            isNew = iTrue;
        }
        else if (startsWith_String(mime, "audio/")) {
#if defined (LAGRANGE_ENABLE_AUDIO)
            iGmAudio *audio = new_GmAudio();
            audio->props.linkId = linkId; /* TODO: use a hash? */
            audio->props.isPermanent = !allowHide;
            set_String(&audio->props.mime, mime);
            updateSourceData_Player(audio->player, mime, data, replace_PlayerUpdate);
            if (!isPartial) {
                updateSourceData_Player(audio->player, NULL, NULL, complete_PlayerUpdate);
            }
            pushBack_PtrArray(&d->items[audio_MediaType], audio);
            /* Start playing right away. */
            start_Player(audio->player);
            postCommandf_App("media.player.started player:%p", audio->player);
            isNew = iTrue;
#endif /* LAGRANGE_ENABLE_AUDIO */
        }
    }
    return isNew;
}

static iMediaId findMediaPtr_Media_(const iPtrArray *items, enum iMediaType mediaType, iGmLinkId linkId) {
    iConstForEach(PtrArray, i, items) {
        const iGmMediaProps *props = i.ptr;
        if (props->linkId == linkId) {
            return (iMediaId){
                .type = mediaType,
                .id = index_PtrArrayConstIterator(&i) + 1
            };
        }
    }
    return iInvalidMediaId;
}

iMediaId findMediaForLink_Media(const iMedia *d, iGmLinkId linkId, enum iMediaType mediaType) {
    /* TODO: Use hashes, this will get very slow if there is a large number of media items. */
    iMediaId mid = iInvalidMediaId;
    for (int i = 0; i < max_MediaType; i++) {
        if (mediaType == i || !mediaType) {
            mid = findMediaPtr_Media_(&d->items[i], i, linkId);
            if (mid.type) {
                break;
            }
        }
    }
    return mid;
}

size_t numAudio_Media(const iMedia *d) {
    return size_PtrArray(&d->items[audio_MediaType]);
}

iInt2 imageSize_Media(const iMedia *d, iMediaId imageId) {
    iAssert(imageId.type == image_MediaType);
    const size_t index = index_MediaId(imageId);
    if (index < size_PtrArray(&d->items[image_MediaType])) {
        const iGmImage *img = constAt_PtrArray(&d->items[image_MediaType], index);
        return img->size;
    }
    return zero_I2();
}

SDL_Texture *imageTexture_Media(const iMedia *d, iMediaId imageId) {
    iAssert(imageId.type == image_MediaType);
    const size_t index = index_MediaId(imageId);
    if (index < size_PtrArray(&d->items[image_MediaType])) {
        const iGmImage *img = constAt_PtrArray(&d->items[image_MediaType], index);
        return img->texture;
    }
    return NULL;
}

iBool info_Media(const iMedia *d, iMediaId mediaId, iGmMediaInfo *info_out) {
    /* TODO: Use a hash. */
    const size_t index = index_MediaId(mediaId);
    switch (mediaId.type) {
        case image_MediaType:
            if (index < size_PtrArray(&d->items[image_MediaType])) {
                const iGmImage *img   = constAt_PtrArray(&d->items[image_MediaType], index);
                info_out->numBytes    = img->numBytes;
                info_out->type        = cstr_String(&img->props.mime);
                info_out->isPermanent = img->props.isPermanent;
                return iTrue;
            }
            break;
        case audio_MediaType:
            if (index < size_PtrArray(&d->items[audio_MediaType])) {
                const iGmAudio *audio = constAt_PtrArray(&d->items[audio_MediaType], index);
                info_out->type        = cstr_String(&audio->props.mime);
                info_out->isPermanent = audio->props.isPermanent;
                return iTrue;
            }
            break;
        case download_MediaType:
            if (index < size_PtrArray(&d->items[download_MediaType])) {
                const iGmDownload *dl = constAt_PtrArray(&d->items[download_MediaType], index);
                info_out->type = cstr_String(&dl->props.mime);
                info_out->isPermanent = dl->props.isPermanent;
                info_out->numBytes = dl->numBytes;
                return iTrue;
            }
            break;
        default:
            break;
    }
    iZap(*info_out);
    return iFalse;
}

iPlayer *audioData_Media(const iMedia *d, iMediaId audioId) {
    iAssert(audioId.type == audio_MediaType);
    const size_t index = index_MediaId(audioId);
    if (index < size_PtrArray(&d->items[audio_MediaType])) {
        const iGmAudio *audio = constAt_PtrArray(&d->items[audio_MediaType], index);
        return audio->player;
    }
    return NULL;
}

iPlayer *audioPlayer_Media(const iMedia *d, iMediaId audioId) {
    iAssert(audioId.type == audio_MediaType);
    const size_t index = index_MediaId(audioId);
    if (index < size_PtrArray(&d->items[audio_MediaType])) {
        const iGmAudio *audio = constAt_PtrArray(&d->items[audio_MediaType], index);
        return audio->player;
    }
    return NULL;
}

void pauseAllPlayers_Media(const iMedia *d, iBool setPaused) {
#if defined (LAGRANGE_ENABLE_AUDIO)
    for (size_t i = 0; i < size_PtrArray(&d->items[audio_MediaType]); ++i) {
        const iGmAudio *audio = constAt_PtrArray(&d->items[audio_MediaType], i);
        if (audio->player) {
            setPaused_Player(audio->player, setPaused);
        }
    }
#endif
}

void downloadStats_Media(const iMedia *d, iMediaId downloadId, const iString **path_out,
                         float *bytesPerSecond_out, iBool *isFinished_out) {
    iAssert(downloadId.type == download_MediaType);
    *path_out           = NULL;
    *bytesPerSecond_out = 0.0f;
    *isFinished_out     = iFalse;
    const size_t index  = index_MediaId(downloadId);
    if (index < size_PtrArray(&d->items[download_MediaType])) {
        const iGmDownload *dl = constAt_PtrArray(&d->items[download_MediaType], index);
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

void init_MediaRequest(iMediaRequest *d, iDocumentWidget *doc, unsigned int linkId,
                       const iString *url, iBool enableFilters,
                       const iGmIdentity *overrideDefaultIdentity) {
    d->doc    = doc;
    d->linkId = linkId;
    d->req    = new_GmRequest(certs_App());
    setUrl_GmRequest(d->req, url);
    enableFilters_GmRequest(d->req, enableFilters);
    if (overrideDefaultIdentity) {
        setIdentity_GmRequest(d->req, overrideDefaultIdentity);
    }
    iConnect(GmRequest, d->req, updated, d, updated_MediaRequest_);
    iConnect(GmRequest, d->req, finished, d, finished_MediaRequest_);
    submit_GmRequest(d->req);
}

void deinit_MediaRequest(iMediaRequest *d) {
    iDisconnect(GmRequest, d->req, updated, d, updated_MediaRequest_);
    iDisconnect(GmRequest, d->req, finished, d, finished_MediaRequest_);
    iRelease(d->req);
}

void resubmitWithUrl_MediaRequest(iMediaRequest *d, const iString *url) {
    iAssert(d->req);
    iAssert(isFinished_GmRequest(d->req));
    const iBool enableFilters = filtersEnabled_GmRequest(d->req);
    deinit_MediaRequest(d); /* release request, disconnect audiences */
    d->req = new_GmRequest(certs_App());
    setUrl_GmRequest(d->req, url);
    enableFilters_GmRequest(d->req, enableFilters);
    iConnect(GmRequest, d->req, updated, d, updated_MediaRequest_);
    iConnect(GmRequest, d->req, finished, d, finished_MediaRequest_);
    submit_GmRequest(d->req);
}

iMediaRequest *newReused_MediaRequest(iDocumentWidget *doc, unsigned int linkId,
                                      iGmRequest *request) {
    iMediaRequest *d = new_Object(&Class_MediaRequest);
    d->doc = doc;
    d->linkId = linkId;
    d->req = request; /* takes ownership */
    iConnect(GmRequest, d->req, updated, d, updated_MediaRequest_);
    iConnect(GmRequest, d->req, finished, d, finished_MediaRequest_);
    return d;
}

iDefineObjectConstructionArgs(MediaRequest,
                              (iDocumentWidget *doc, unsigned int linkId, const iString *url,
                               iBool enableFilters, const iGmIdentity *overrideDefaultIdentity),
                              doc, linkId, url, enableFilters, overrideDefaultIdentity)
iDefineClass(MediaRequest)
