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
#include "ui/window.h"
#include "audio/player.h"

#include <the_Foundation/ptrarray.h>
#include <stb_image.h>
#include <SDL_hints.h>
#include <SDL_render.h>

iDeclareType(GmMediaProps)

struct Impl_GmMediaProps {
    iGmLinkId linkId;
    iString   mime;
    iBool     isPermanent;
};

static void init_GmMediaProps_(iGmMediaProps *d) {
    d->linkId = 0;
    init_String(&d->mime);
    d->isPermanent = iFalse;
}

static void deinit_GmMediaProps_(iGmMediaProps *d) {
    deinit_String(&d->mime);
}

/*----------------------------------------------------------------------------------------------*/

iDeclareType(GmImage)

struct Impl_GmImage {
    iGmMediaProps props;
    iInt2        size;
    size_t       numBytes;
    SDL_Texture *texture;
};

void init_GmImage(iGmImage *d, const iBlock *data) {
    init_GmMediaProps_(&d->props);
    d->numBytes      = size_Block(data);
    uint8_t *imgData = stbi_load_from_memory(
        constData_Block(data), size_Block(data), &d->size.x, &d->size.y, NULL, 4);
    if (!imgData) {
        d->size    = zero_I2();
        d->texture = NULL;
    }
    else {
        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
            imgData, d->size.x, d->size.y, 32, d->size.x * 4, SDL_PIXELFORMAT_ABGR8888);
        /* TODO: In multiwindow case, all windows must have the same shared renderer?
           Or at least a shared context. */
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1"); /* linear scaling */
        d->texture = SDL_CreateTextureFromSurface(renderer_Window(get_Window()), surface);
        SDL_FreeSurface(surface);
        stbi_image_free(imgData);
    }
}

void deinit_GmImage(iGmImage *d) {
    SDL_DestroyTexture(d->texture);
    deinit_GmMediaProps_(&d->props);
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

struct Impl_Media {
    iPtrArray images;
    iPtrArray audio;   
};

iDefineTypeConstruction(Media)

void init_Media(iMedia *d) {
    init_PtrArray(&d->images);
    init_PtrArray(&d->audio);
}

void deinit_Media(iMedia *d) {
    clear_Media(d);
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
}

void setData_Media(iMedia *d, iGmLinkId linkId, const iString *mime, const iBlock *data,
                   iBool allowHide) {
    if (!mime || !data) {
        iMediaId existing = findLinkImage_Media(d, linkId);
        if (existing) {
            iGmImage *img;
            take_PtrArray(&d->images, existing - 1, (void **) &img);
            delete_GmImage(img);
        }
        else if ((existing = findLinkAudio_Media(d, linkId)) != 0) {
            iGmAudio *audio;
            take_PtrArray(&d->audio, existing - 1, (void **) &audio);
            delete_GmAudio(audio);
        }
    }
    else {
        if (startsWith_String(mime, "image/")) {
            /* Copy the image to a texture. */
            /* TODO: Resize down to min(maximum texture size, window size). */
            iGmImage *img = new_GmImage(data);
            img->props.linkId      = linkId; /* TODO: use a hash? */
            img->props.isPermanent = !allowHide;
            set_String(&img->props.mime, mime);
            if (img->texture) {
                pushBack_PtrArray(&d->images, img);
            }
            else {
                delete_GmImage(img);
            }
        }
        else if (startsWith_String(mime, "audio/")) {
            iGmAudio *audio = new_GmAudio();
            audio->props.linkId = linkId;
            audio->props.isPermanent = !allowHide;
            set_String(&audio->props.mime, mime);
            /* TODO: What about update/complete, for streaming? */
            updateSourceData_Player(audio->player, data, replace_PlayerUpdate);
            pushBack_PtrArray(&d->audio, audio);

            /* TEST: Start playing right away. */
            start_Player(audio->player);
        }
    }
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

SDL_Texture *imageTexture_Media(const iMedia *d, uint16_t imageId) {
    if (imageId > 0 && imageId <= size_PtrArray(&d->images)) {
        const iGmImage *img = constAt_PtrArray(&d->images, imageId - 1);
        return img->texture;
    }
    return NULL;
}

iBool imageInfo_Media(const iMedia *d, iMediaId imageId, iGmImageInfo *info_out) {
    if (imageId > 0 && imageId <= size_PtrArray(&d->images)) {
        const iGmImage *img   = constAt_PtrArray(&d->images, imageId - 1);
        info_out->size        = img->size;
        info_out->numBytes    = img->numBytes;
        info_out->mime        = cstr_String(&img->props.mime);
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

iBool audioInfo_Media(const iMedia *d, iMediaId audioId, iGmAudioInfo *info_out) {
    if (audioId > 0 && audioId <= size_PtrArray(&d->audio)) {
        const iGmAudio *audio = constAt_PtrArray(&d->audio, audioId - 1);
        info_out->mime        = cstr_String(&audio->props.mime);
        info_out->isPermanent = audio->props.isPermanent;
        return iTrue;
    }
    iZap(*info_out);
    return iFalse;
}
