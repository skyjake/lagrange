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
}

void deinit_VisBuf(iVisBuf *d) {
    dealloc_VisBuf(d);
}

void invalidate_VisBuf(iVisBuf *d) {
    iForIndices(i, d->buffers) {
        d->buffers[i].origin = i * d->texSize.y;
        iZap(d->buffers[i].validRange);
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
            tex->origin = i * texSize.y;
            iZap(tex->validRange);
        }
    }
}

void dealloc_VisBuf(iVisBuf *d) {
    d->texSize = zero_I2();
    iForIndices(i, d->buffers) {
        SDL_DestroyTexture(d->buffers[i].texture);
        d->buffers[i].texture = NULL;
    }
}

void reposition_VisBuf(iVisBuf *d, const iRangei vis) {
    d->vis = vis;
    iRangei good = { 0, 0 };
    size_t avail[iElemCount(d->buffers)], numAvail = 0;
    /* Check which buffers are available for reuse. */ {
        iForIndices(i, d->buffers) {
            iVisBufTexture *buf    = d->buffers + i;
            const iRangei   region = { buf->origin, buf->origin + d->texSize.y };
            if (isEmpty_Rangei(buf->validRange) ||
                buf->validRange.start >= vis.end || buf->validRange.end <= vis.start) {
                avail[numAvail++] = i;
                iZap(buf->validRange);
            }
            else {
                good = union_Rangei(good, region);
            }
        }
    }
    iBool wasChanged = iFalse;
    iBool doReset    = (numAvail == iElemCount(d->buffers));
    /* Try to extend to cover the visible range. */
    while (!doReset && vis.start < good.start) {
        if (numAvail == 0) {
            doReset = iTrue;
            break;
        }
        good.start -= d->texSize.y;
        d->buffers[avail[--numAvail]].origin = good.start;
        wasChanged = iTrue;
    }
    while (!doReset && vis.end > good.end) {
        if (numAvail == 0) {
            doReset = iTrue;
            break;
        }
        d->buffers[avail[--numAvail]].origin = good.end;
        good.end += d->texSize.y;
        wasChanged = iTrue;
    }
    if (doReset) {
//        puts("VisBuf reset!");
//        fflush(stdout);
        wasChanged = iTrue;
        int pos = -1;
        iForIndices(i, d->buffers) {
            iZap(d->buffers[i].validRange);
            d->buffers[i].origin = vis.start + pos++ * d->texSize.y;
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

void draw_VisBuf(const iVisBuf *d, iInt2 topLeft) {
    SDL_Renderer *render = renderer_Window(get_Window());
    iForIndices(i, d->buffers) {
        const iVisBufTexture *buf = d->buffers + i;
        SDL_Rect dst = { topLeft.x,
                         topLeft.y + buf->origin,
                         d->texSize.x,
                         d->texSize.y };
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
