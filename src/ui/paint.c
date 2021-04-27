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

#include "paint.h"

#include <SDL_version.h>

iLocalDef SDL_Renderer *renderer_Paint_(const iPaint *d) {
    iAssert(d->dst);
    return d->dst->render;
}

static void setColor_Paint_(const iPaint *d, int color) {
    const iColor clr = get_Color(color & mask_ColorId);
    SDL_SetRenderDrawColor(renderer_Paint_(d), clr.r, clr.g, clr.b, clr.a * d->alpha / 255);
}

void init_Paint(iPaint *d) {
    d->dst       = get_Window();
    d->setTarget = NULL;
    d->oldTarget = NULL;
    d->alpha     = 255;
}

void beginTarget_Paint(iPaint *d, SDL_Texture *target) {
    SDL_Renderer *rend = renderer_Paint_(d);
    if (!d->setTarget) {
        d->oldTarget = SDL_GetRenderTarget(rend);
        SDL_SetRenderTarget(rend, target);
        d->setTarget = target;
    }
    else {
        iAssert(d->setTarget == target);
    }
}

void endTarget_Paint(iPaint *d) {
    if (d->setTarget) {
        SDL_SetRenderTarget(renderer_Paint_(d), d->oldTarget);
        d->oldTarget = NULL;
        d->setTarget = NULL;
    }
}

void setClip_Paint(iPaint *d, iRect rect) {
    if (rect.pos.y < 0) {
        const int off = rect.pos.y;
        rect.pos.y -= off;
        rect.size.y = iMax(0, rect.size.y + off);
    }
    if (rect.pos.x < 0) {
        const int off = rect.pos.x;
        rect.pos.x -= off;
        rect.size.x = iMax(0, rect.size.x + off);
    }
    SDL_RenderSetClipRect(renderer_Paint_(d), (const SDL_Rect *) &rect);
}

void unsetClip_Paint(iPaint *d) {
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_RenderSetClipRect(renderer_Paint_(d), NULL);
#else
    const SDL_Rect winRect = { 0, 0, d->dst->root->rect.size.x, d->dst->root->rect.size.y };
    SDL_RenderSetClipRect(renderer_Paint_(d), &winRect);
#endif
}

void drawRect_Paint(const iPaint *d, iRect rect, int color) {
    iInt2 br = bottomRight_Rect(rect);
    /* Keep the right/bottom edge visible in the window. */
    if (br.x == d->dst->root.widget->rect.size.x) br.x--;
    if (br.y == d->dst->root.widget->rect.size.y) br.y--;
    const SDL_Point edges[] = {
        { left_Rect(rect),  top_Rect(rect) },
        { br.x, top_Rect(rect) },
        { br.x, br.y },
        { left_Rect(rect),  br.y },
        { left_Rect(rect),  top_Rect(rect) }
    };
    setColor_Paint_(d, color);
    SDL_RenderDrawLines(renderer_Paint_(d), edges, iElemCount(edges));
}

void drawRectThickness_Paint(const iPaint *d, iRect rect, int thickness, int color) {
    thickness = iClamp(thickness, 1, 4);
    while (thickness--) {
        drawRect_Paint(d, rect, color);
        shrink_Rect(&rect, one_I2());
    }
}

void fillRect_Paint(const iPaint *d, iRect rect, int color) {
    setColor_Paint_(d, color);
    SDL_RenderFillRect(renderer_Paint_(d), (SDL_Rect *) &rect);
}

void drawSoftShadow_Paint(const iPaint *d, iRect inner, int thickness, int color, int alpha) {
    SDL_Renderer *render = renderer_Paint_(d);
    SDL_Texture *shadow = get_Window()->borderShadow;
    const iInt2 size = size_SDLTexture(shadow);
    const iRect outer = expanded_Rect(inner, init1_I2(thickness));
    const iColor clr = get_Color(color);
    SDL_SetTextureColorMod(shadow, clr.r, clr.g, clr.b);
    SDL_SetTextureAlphaMod(shadow, alpha);
    /* Classic stretched segmented border. */
    SDL_RenderCopy(render, shadow, &(SDL_Rect){ 0, 0, size.x / 2, size.y / 2},
                   &(SDL_Rect){ outer.pos.x, outer.pos.y, thickness, thickness });
    SDL_RenderCopy(render, shadow, &(SDL_Rect){ size.x / 2, 0, 1, size.y / 2},
                   &(SDL_Rect){ inner.pos.x, outer.pos.y, inner.size.x, thickness });
    SDL_RenderCopy(render, shadow, &(SDL_Rect){ size.x / 2, 0, size.x / 2, size.y / 2},
                   &(SDL_Rect){ right_Rect(outer) - thickness, outer.pos.y, thickness, thickness });
    SDL_RenderCopy(render, shadow, &(SDL_Rect){ size.x / 2, size.y / 2, size.x / 2, 1 },
                   &(SDL_Rect){ right_Rect(inner), inner.pos.y, thickness, inner.size.y });
    SDL_RenderCopy(render, shadow, &(SDL_Rect){ size.x / 2, size.y / 2, size.x / 2, size.y / 2},
                   &(SDL_Rect){ right_Rect(inner), bottom_Rect(inner), thickness, thickness });
    SDL_RenderCopy(render, shadow, &(SDL_Rect){ size.x / 2, size.y / 2, 1, size.y / 2},
                   &(SDL_Rect){ inner.pos.x, bottom_Rect(inner), inner.size.x, thickness });
    SDL_RenderCopy(render, shadow, &(SDL_Rect){ 0, size.y / 2, size.x / 2, size.y / 2},
                   &(SDL_Rect){ outer.pos.x, bottom_Rect(inner), thickness, thickness });
    SDL_RenderCopy(render, shadow, &(SDL_Rect){ 0, size.y / 2, size.x / 2, 1 },
                   &(SDL_Rect){ outer.pos.x, inner.pos.y, thickness, inner.size.y });
}

void drawLines_Paint(const iPaint *d, const iInt2 *points, size_t count, int color) {
    setColor_Paint_(d, color);
    SDL_RenderDrawLines(renderer_Paint_(d), (const SDL_Point *) points, count);
}

iInt2 size_SDLTexture(SDL_Texture *d) {
    iInt2 size;
    SDL_QueryTexture(d, NULL, NULL, &size.x, &size.y);
    return size;
}
