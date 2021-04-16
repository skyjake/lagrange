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
    int origin = d->vis.start - d->texSize.y;
    iForIndices(i, d->buffers) {
        d->buffers[i].origin = origin;
        origin += d->texSize.y;
        iZap(d->buffers[i].validRange);
        if (d->bufferInvalidated) {
            d->bufferInvalidated(d, i);
        }
    }
}

void alloc_VisBuf(iVisBuf *d, const iInt2 size, int granularity) {
    const iInt2 texSize = init_I2(size.x, (size.y / 2 / granularity + 1) * granularity);
    if (!d->buffers[0].texture || !isEqual_I2(texSize, d->texSize)) {
        d->texSize = texSize;
        iForIndices(i, d->buffers) {
            iVisBufTexture *tex = &d->buffers[i];
            if (tex->texture) {
                SDL_DestroyTexture(tex->texture);
            }
            tex->texture =
                SDL_CreateTexture(renderer_Window(get_Window()),
                                  SDL_PIXELFORMAT_RGBA8888,
                                  SDL_TEXTUREACCESS_STATIC | SDL_TEXTUREACCESS_TARGET,
                                  texSize.x,
                                  texSize.y);
            SDL_SetTextureBlendMode(tex->texture, SDL_BLENDMODE_NONE);
//            tex->origin = i * texSize.y;
//            iZap(tex->validRange);
//          if (d->invalidUserData) {
//                d->invalidUserData(i, d->buffers[i].user);
//            }
        }
        invalidate_VisBuf(d);
    }
}

void dealloc_VisBuf(iVisBuf *d) {
    d->texSize = zero_I2();
    iForIndices(i, d->buffers) {
        SDL_DestroyTexture(d->buffers[i].texture);
        d->buffers[i].texture = NULL;
    }
}

#if 0
static size_t findMostDistant_VisBuf_(const iVisBuf *d, const size_t *avail, size_t numAvail,
                                      const iRangei vis) {
    size_t chosen = 0;
    int distChosen = iAbsi(d->buffers[0].origin - vis.start);
    printf("  avail (got %zu): %zu", numAvail, avail[0]);
    for (size_t i = 1; i < numAvail; i++) {
        printf(" %zu", avail[i]);
        const int dist = iAbsi(d->buffers[i].origin - vis.start);
        if (dist > distChosen) {
            chosen = i;
            distChosen = dist;
        }
    }
    printf("\n  chose index %zu (%d)\n", chosen, distChosen);
    return chosen;
}

static size_t take_(size_t *avail, size_t *numAvail, size_t index) {
    const size_t value = avail[index];
    memmove(avail + index, avail + index + 1, sizeof(size_t) * (*numAvail - index - 1));
    (*numAvail)--;
    return value;    
}
#endif

iBool reposition_VisBuf(iVisBuf *d, const iRangei vis) {
    if (equal_Rangei(vis, d->vis)) {
        return iFalse;
    }
    d->vis = vis;
    iBool wasChanged = iFalse;
    const size_t lastPos = iElemCount(d->buffers) - 1;
    if (d->buffers[0].origin > vis.end || d->buffers[lastPos].origin + d->texSize.y <= vis.start) {
        /* All buffers outside the visible region. */
        invalidate_VisBuf(d);
        wasChanged = iTrue;
    }
    else {
        /* Roll up. */
        while (d->buffers[0].origin > vis.start) {
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
            wasChanged = iTrue;
        }
        if (!wasChanged) {
            /* Roll down. */
            while (d->buffers[lastPos].origin + d->texSize.y < vis.end) {
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
                wasChanged = iTrue;
            }
        }
    }
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
            continue; /* Outside the clipping area. */
        }
#if defined (DEBUG_SCALE)
        dst.w *= DEBUG_SCALE;
        dst.h *= DEBUG_SCALE;
        dst.x *= DEBUG_SCALE;
        dst.y *= DEBUG_SCALE;
        dst.x += get_Window()->root->rect.size.x / 4;
        dst.y += get_Window()->root->rect.size.y / 4;
#endif
        SDL_RenderCopy(render, buf->texture, NULL, &dst);
    }    
}
