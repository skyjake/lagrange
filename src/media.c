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

#include <the_Foundation/ptrarray.h>
#include <stb_image.h>
#include <SDL_hints.h>
#include <SDL_render.h>

iDeclareType(GmImage)

struct Impl_GmImage {
    iInt2        size;
    size_t       numBytes;
    iString      mime;
    iGmLinkId    linkId;
    iBool        isPermanent;
    SDL_Texture *texture;
};

void init_GmImage(iGmImage *d, const iBlock *data) {
    init_String(&d->mime);
    d->isPermanent   = iFalse;
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
    d->linkId = 0;
}

void deinit_GmImage(iGmImage *d) {
    SDL_DestroyTexture(d->texture);
    deinit_String(&d->mime);
}

iDefineTypeConstructionArgs(GmImage, (const iBlock *data), data)

/*----------------------------------------------------------------------------------------------*/

struct Impl_Media {
    iPtrArray images;
};

iDefineTypeConstruction(Media)

void init_Media(iMedia *d) {
    init_PtrArray(&d->images);
}

void deinit_Media(iMedia *d) {
    clear_Media(d);
    deinit_PtrArray(&d->images);
}

void clear_Media(iMedia *d) {
    iForEach(PtrArray, i, &d->images) {
        deinit_GmImage(i.ptr);
    }
    clear_PtrArray(&d->images);
}

void setImage_Media(iMedia *d, iGmLinkId linkId, const iString *mime, const iBlock *data,
                    iBool allowHide) {
    if (!mime || !data) {
        iGmImage *img;
        const iMediaId existing = findLinkImage_Media(d, linkId);
        if (existing) {
            take_PtrArray(&d->images, existing - 1, (void **) &img);
            delete_GmImage(img);
        }
    }
    else {
        /* TODO: check if we know this MIME type */
        /* Upload the image. */ {
            iGmImage *img = new_GmImage(data);
            img->linkId = linkId; /* TODO: use a hash? */
            img->isPermanent = !allowHide;
            set_String(&img->mime, mime);
            if (img->texture) {
                pushBack_PtrArray(&d->images, img);
            }
            else {
                delete_GmImage(img);
            }
        }
    }
//    doLayout_GmDocument_(d);
}

iMediaId findLinkImage_Media(const iMedia *d, iGmLinkId linkId) {
    /* TODO: use a hash */
    iConstForEach(PtrArray, i, &d->images) {
        const iGmImage *img = i.ptr;
        if (img->linkId == linkId) {
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

void imageInfo_Media(const iMedia *d, uint16_t imageId, iGmImageInfo *info_out) {
    if (imageId > 0 && imageId <= size_PtrArray(&d->images)) {
        const iGmImage *img   = constAt_PtrArray(&d->images, imageId - 1);
        info_out->size        = img->size;
        info_out->numBytes    = img->numBytes;
        info_out->mime        = cstr_String(&img->mime);
        info_out->isPermanent = img->isPermanent;
    }
    else {
        iZap(*info_out);
    }
}
