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
    size_t avail[3], numAvail = 0;
    /* Check which buffers are available for reuse. */ {
        iForIndices(i, d->buffers) {
            iVisBufTexture *buf = d->buffers + i;
            const iRangei region = { buf->origin, buf->origin + d->texSize.y };
            if (region.start >= vis.end || region.end <= vis.start) {
                avail[numAvail++] = i;
                iZap(buf->validRange);
            }
            else {
                good = union_Rangei(good, region);
            }
        }
    }
    if (numAvail == iElemCount(d->buffers)) {
        /* All buffers are outside the visible range, do a reset. */
        d->buffers[0].origin = vis.start;
        d->buffers[1].origin = vis.start + d->texSize.y;
        d->buffers[2].origin = vis.start + 2 * d->texSize.y;
    }
    else {
        /* Extend to cover the visible range. */
        while (vis.start < good.start) {
            iAssert(numAvail > 0);
            d->buffers[avail[--numAvail]].origin = good.start - d->texSize.y;
            good.start -= d->texSize.y;
        }
        while (vis.end > good.end) {
            iAssert(numAvail > 0);
            d->buffers[avail[--numAvail]].origin = good.end;
            good.end += d->texSize.y;
        }
    }
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

void draw_VisBuf(const iVisBuf *d, iInt2 topLeft) {
    SDL_Renderer *render = renderer_Window(get_Window());
    iForIndices(i, d->buffers) {
        const iVisBufTexture *buf = d->buffers + i;
        SDL_RenderCopy(render,
                       buf->texture,
                       NULL,
                       &(SDL_Rect){ topLeft.x,
                                    topLeft.y + buf->origin,
                                    d->texSize.x,
                                    d->texSize.y });
    }
}
