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

#include "visbuf.h"
#include "paint.h"
#include "window.h"
#include "util.h"

iDefineTypeConstruction(VisBuf)

void init_VisBuf(iVisBuf *d) {
    d->texSize = zero_I2();
    iZap(d->buffers);
    iZap(d->vis);
    d->bufferInvalidated = NULL;
}

void deinit_VisBuf(iVisBuf *d) {
    dealloc_VisBuf(d);
}

void invalidate_VisBuf(iVisBuf *d) {
    int origin = iMax(0, d->vis.start - d->texSize.y);
    iForIndices(i, d->buffers) {
        d->buffers[i].origin = origin;
        origin += d->texSize.y;
        iZap(d->buffers[i].validRange);
        if (d->bufferInvalidated) {
            d->bufferInvalidated(d, i);
        }
    }
}

iBool alloc_VisBuf(iVisBuf *d, const iInt2 size, int granularity) {
    const iInt2 texSize = init_I2(size.x, (size.y / 2 / granularity + 1) * granularity);
    if (!d->buffers[0].texture || !isEqual_I2(texSize, d->texSize)) {
        d->texSize = texSize;
        iForIndices(i, d->buffers) {
            iVisBufTexture *tex = &d->buffers[i];
            if (tex->texture) {
                SDL_DestroyTexture(tex->texture);
            }
            SDL_Renderer *rend = renderer_Window(get_Window());
            tex->texture =
                SDL_CreateTexture(rend,
                                  SDL_PIXELFORMAT_RGBA8888,
                                  SDL_TEXTUREACCESS_STATIC | SDL_TEXTUREACCESS_TARGET,
                                  texSize.x,
                                  texSize.y);
            SDL_SetTextureBlendMode(tex->texture, SDL_BLENDMODE_NONE);
        }
        invalidate_VisBuf(d);
        return iTrue;
    }
    return iFalse;
}

void dealloc_VisBuf(iVisBuf *d) {
    d->texSize = zero_I2();
    iForIndices(i, d->buffers) {
        SDL_DestroyTexture(d->buffers[i].texture);
        d->buffers[i].texture = NULL;
    }
}

static void roll_VisBuf_(iVisBuf *d, int dir) {
    const size_t lastPos = iElemCount(d->buffers) - 1;
    if (dir < 0) {
        /* Last buffer is moved to the beginning. */
        SDL_Texture *last = d->buffers[lastPos].texture;
        void *       user = d->buffers[lastPos].user;
        memmove(d->buffers + 1, d->buffers, sizeof(iVisBufTexture) * lastPos);
        d->buffers[0].texture = last;
        d->buffers[0].user    = user;
        d->buffers[0].origin  = d->buffers[1].origin - d->texSize.y;
        iZap(d->buffers[0].validRange);
        if (d->bufferInvalidated) {
            d->bufferInvalidated(d, 0);
        }
    }
    else {
        /* First buffer is moved to the end. */
        SDL_Texture *first = d->buffers[0].texture;
        void *       user  = d->buffers[0].user;
        memmove(d->buffers, d->buffers + 1, sizeof(iVisBufTexture) * lastPos);
        d->buffers[lastPos].texture = first;
        d->buffers[lastPos].user    = user;
        d->buffers[lastPos].origin  = d->buffers[lastPos - 1].origin + d->texSize.y;
        iZap(d->buffers[lastPos].validRange);
        if (d->bufferInvalidated) {
            d->bufferInvalidated(d, lastPos);
        }
    }
}

iBool reposition_VisBuf(iVisBuf *d, const iRangei vis) {
    if (equal_Rangei(vis, d->vis)) {
        return iFalse;
    }
    const int moveDir = vis.end > d->vis.end ? +1 : -1;
    d->vis = vis;
    iBool wasChanged = iFalse;
    const size_t lastPos = iElemCount(d->buffers) - 1;
    if (d->buffers[0].origin > vis.end || d->buffers[lastPos].origin + d->texSize.y <= vis.start) {
        /* All buffers outside the visible region. */
        invalidate_VisBuf(d);
        wasChanged = iTrue;
    }
    else {
        /* Check for mandatory rolls. */
        while (d->buffers[0].origin > vis.start) {
            roll_VisBuf_(d, -1);
            wasChanged = iTrue;
        }
        if (!wasChanged) {
            while (d->buffers[lastPos].origin + d->texSize.y < vis.end) {
                roll_VisBuf_(d, +1);
                wasChanged = iTrue;
            }
        }
        /* Scroll-direction dependent optional rolls, with a bit of overscroll allowed. */
        if (moveDir > 0 && d->buffers[0].origin + d->texSize.y + d->texSize.y / 4 < vis.start) {
            roll_VisBuf_(d, +1);
            wasChanged = iTrue;
        }
        else if (moveDir < 0 && d->buffers[lastPos].origin - d->texSize.y / 4 > vis.end) {
            roll_VisBuf_(d, -1);
            wasChanged = iTrue;
        }
    }
    iUnused(wasChanged);
#if 0
    if (wasChanged) {
        printf("\nVISIBLE RANGE: %d ... %d\n", vis.start, vis.end);
        iForIndices(i, d->buffers) {
            const iVisBufTexture *bt = &d->buffers[i];
            printf(" %zu: buf %5d ... %5d  valid %5d ... %5d\n", i, bt->origin,
                   bt->origin + d->texSize.y,
                   bt->validRange.start,
                   bt->validRange.end);
        }
        fflush(stdout);
    }
#endif
#if !defined (NDEBUG)
    /* Buffers must not overlap. */
    iForIndices(m, d->buffers) {
        const iRangei M = { d->buffers[m].origin, d->buffers[m].origin + d->texSize.y };
        iForIndices(n, d->buffers) {
            if (m == n) continue;
            const iRangei N = { d->buffers[n].origin, d->buffers[n].origin + d->texSize.y };
            const iRangei is = intersect_Rangei(M, N);
            if (size_Range(&is) != 0) {
                printf("buffers %zu (%i) and %zu (%i) overlap\n",
                       m, M.start, n, N.start);
                fflush(stdout);
            }
            iAssert(size_Range(&is) == 0);
        }
    }
#endif
    return iTrue; /* at least the visible range changed */
}

iRangei allocRange_VisBuf(const iVisBuf *d) {
    return (iRangei){ d->buffers[0].origin,
                      d->buffers[iElemCount(d->buffers) - 1].origin + d->texSize.y };
}

iRangei bufferRange_VisBuf(const iVisBuf *d, size_t index) {
    return (iRangei){ d->buffers[index].origin, d->buffers[index].origin + d->texSize.y };
}

void invalidRanges_VisBuf(const iVisBuf *d, const iRangei full, iRangei *out_invalidRanges) {
    iForIndices(i, d->buffers) {
        const iVisBufTexture *buf = d->buffers + i;
        const iRangei before = { full.start, buf->validRange.start };
        const iRangei after  = { buf->validRange.end, full.end };
        const iRangei region = intersect_Rangei(d->vis, (iRangei){ buf->origin,
                                                                   buf->origin + d->texSize.y });
        out_invalidRanges[i] = intersect_Rangei(before, region);
        if (isEmpty_Rangei(out_invalidRanges[i])) {
            out_invalidRanges[i] = intersect_Rangei(after, region);
        }
    }
}

void validate_VisBuf(iVisBuf *d) {
    iForIndices(i, d->buffers) {
        iVisBufTexture *buf = &d->buffers[i];
        buf->validRange =
            intersect_Rangei(d->vis, (iRangei){ buf->origin, buf->origin + d->texSize.y });
    }
}

//#define DEBUG_SCALE 0.5f

void draw_VisBuf(const iVisBuf *d, const iInt2 topLeft, const iRangei yClipBounds) {
    SDL_Renderer *render = renderer_Window(get_Window());
    iForIndices(i, d->buffers) {
        const iVisBufTexture *buf = d->buffers + i;
        SDL_Rect dst = { topLeft.x,
                         topLeft.y + buf->origin,
                         d->texSize.x,
                         d->texSize.y };
        if (dst.y >= yClipBounds.end || dst.y + dst.h < yClipBounds.start) {
#if !defined (DEBUG_SCALE)
            continue; /* Outside the clipping area. */
#endif
        }
        dst.x += origin_Paint.x;
        dst.y += origin_Paint.y;
#if defined (DEBUG_SCALE)
        dst.w *= DEBUG_SCALE;
        dst.h *= DEBUG_SCALE;
        dst.x *= DEBUG_SCALE;
        dst.y *= DEBUG_SCALE;
        dst.x += get_Window()->root->rect.size.x / 4;
        dst.y += get_Window()->root->rect.size.y / 4;
#endif
        SDL_RenderCopy(render, buf->texture, NULL, &dst);
#if defined (DEBUG_SCALE)
        SDL_SetRenderDrawColor(render, 0, 0, 255, 255);
        SDL_RenderDrawRect(render, &dst);
#endif
    }
#if defined (DEBUG_SCALE)
    SDL_Rect dst = { topLeft.x, yClipBounds.start, d->texSize.x, 2 * d->texSize.y };
    dst.w *= DEBUG_SCALE;
    dst.h *= DEBUG_SCALE;
    dst.x *= DEBUG_SCALE;
    dst.y *= DEBUG_SCALE;
    dst.x += get_Window()->root->rect.size.x / 4;
    dst.y += get_Window()->root->rect.size.y / 4;
    SDL_SetRenderDrawColor(render, 255, 255, 255, 255);
    SDL_RenderDrawRect(render, &dst);
#endif
}
