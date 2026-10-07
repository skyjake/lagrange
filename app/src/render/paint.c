/* Copyright 2020-2022 Jaakko Keränen <jaakko.keranen@iki.fi>

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
#include "app.h"

#include <the_Foundation/array.h>
#include <the_Foundation/math.h>
#include <SDL3/SDL_version.h>
#include <math.h>
#include <string.h>

iInt2 origin_Paint;

/*-----------------------------------------------------------------------------------------------*/

iDeclareType(ClipEntry);

/* Clip rectangle stack. Entries store the render target because switching targets
   changes to a different coordinate space. */
struct Impl_ClipEntry {
    iRect        rect;
    SDL_Texture *target;
};

static iArray clipStack_; /* ClipEntry[] */

static void initClipStack_(void) {
    if (!clipStack_.elementSize) {
        init_Array(&clipStack_, sizeof(iClipEntry));
    }
}

static iBool activeClip_(SDL_Texture *target, iRect *rect_out) {
    iReverseConstForEach(Array, i, &clipStack_) {
        const iClipEntry *entry = i.value;
        if (entry->target == target) {
            *rect_out = entry->rect;
            return iTrue;
        }
    }
    return iFalse;
}

/*-----------------------------------------------------------------------------------------------*/

iLocalDef SDL_Renderer *renderer_Paint_(const iPaint *d) {
    iAssert(d->dst);
    return d->dst->render;
}

static iRect rootClip_Paint_(const iPaint *d, iRect rect) {
    iRect        targetRect = zero_Rect();
    SDL_Texture *target     = SDL_GetRenderTarget(renderer_Paint_(d));
    if (target) {
        targetRect.size = init_I2(target->w, target->h);
        rect = intersect_Rect(rect, targetRect);
    }
    /* The origin is non-zero when drawing into a widget's own buffer. */
    if (isEqual_I2(zero_I2(), origin_Paint)) {
        rect = intersect_Rect(rect, rect_Root(get_Root()));
    }
    return rect;
}

static void applyRootClip_Paint_(const iPaint *d) {
    if (numRoots_Window(get_Window()) > 1) {
        iRect rect = rootClip_Paint_(d, moved_Rect(rect_Root(get_Root()), origin_Paint));
        if (isEmpty_Rect(rect)) {
            rect = init_Rect(0, 0, 1, 1);
        }
        SDL_SetRenderClipRect(renderer_Paint_(d), (const SDL_Rect *) &rect);
        return;
    }
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetRenderClipRect(renderer_Paint_(d), NULL);
#else
    const iRect rect =
        current_Root() ? rect_Root(get_Root()) : (iRect) { zero_I2(), get_Window()->size };
    SDL_SetRenderClipRect(renderer_Paint_(d), (const SDL_Rect *) &rect);
#endif
}

static void setColor_Paint_(const iPaint *d, int color) {
    const iColor clr = get_Color(color & mask_ColorId);
    SDL_SetRenderDrawColor(renderer_Paint_(d), clr.r, clr.g, clr.b,
                           (color & opaque_ColorId ? 255 : clr.a) * d->alpha / 255);
}

void init_Paint(iPaint *d) {
    d->dst       = get_Window();
    d->setTarget = NULL;
    d->oldTarget = NULL;
    d->oldOrigin = zero_I2();
    d->alpha     = 255;
}

void beginTarget_Paint(iPaint *d, SDL_Texture *target) {
    SDL_Renderer *rend = renderer_Paint_(d);
    if (!d->setTarget) {
        d->oldTarget = SDL_GetRenderTarget(rend);
        d->oldOrigin = origin_Paint;
        origin_Paint = zero_I2();
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
        origin_Paint = d->oldOrigin;
        d->oldOrigin = zero_I2();
        d->oldTarget = NULL;
        d->setTarget = NULL;
    }
}

void setClip_Paint(iPaint *d, iRect rect) {
    initClipStack_();
    addv_I2(&rect.pos, origin_Paint);
    SDL_Texture *target = SDL_GetRenderTarget(renderer_Paint_(d));
    iRect active;
    rect = activeClip_(target, &active) ? intersect_Rect(rect, active)
                                        : rootClip_Paint_(d, rect);
    if (isEmpty_Rect(rect)) {
        rect = init_Rect(0, 0, 1, 1);
    }
    pushBack_Array(&clipStack_, &(iClipEntry){ rect, target });
    SDL_SetRenderClipRect(renderer_Paint_(d), (const SDL_Rect *) &rect);
}

void unsetClip_Paint(iPaint *d) {
    initClipStack_();
    SDL_Texture *target = SDL_GetRenderTarget(renderer_Paint_(d));
    if (!isEmpty_Array(&clipStack_) &&
        ((const iClipEntry *) constBack_Array(&clipStack_))->target == target) {
        popBack_Array(&clipStack_);
    }
    iRect active;
    if (activeClip_(target, &active)) {
        SDL_SetRenderClipRect(renderer_Paint_(d), (const SDL_Rect *) &active);
    }
    else {
        applyRootClip_Paint_(d);
    }
}

void drawRect_Paint(const iPaint *d, iRect rect, int color) {
    addv_I2(&rect.pos, origin_Paint);
    iInt2 br = bottomRight_Rect(rect);
    /* Keep the right/bottom edge visible in the window. */
    if (br.x == d->dst->size.x) br.x--;
    if (br.y == d->dst->size.y) br.y--;
    SDL_FPoint edges[] = {
        { left_Rect(rect),  top_Rect(rect) },
        { br.x,             top_Rect(rect) },
        { br.x,             br.y },
        { left_Rect(rect),  br.y },
        { left_Rect(rect),  top_Rect(rect) }
    };
#if SDL_VERSION == SDL_VERSIONNUM(2, 0, 16)
    if (isOpenGLRenderer_Window()) {
        /* A very curious regression in SDL 2.0.16. */
        edges[3].y--;
    }
#endif
    setColor_Paint_(d, color);
    SDL_RenderLines(renderer_Paint_(d), edges, iElemCount(edges));
}

void drawRectThickness_Paint(const iPaint *d, iRect rect, int thickness, int color) {
    if (isTerminal_Platform() && thickness == 0) {
        addv_I2(&rect.size, one_I2());
    }
    thickness = iClamp(thickness, 1, 4);
    while (thickness--) {
        drawRect_Paint(d, rect, color);
        shrink_Rect(&rect, one_I2());
    }
}

void fillRect_Paint(const iPaint *d, iRect rect, int color) {
    addv_I2(&rect.pos, origin_Paint);
    setColor_Paint_(d, color);
    const SDL_FRect frect = { rect.pos.x, rect.pos.y, rect.size.x, rect.size.y };
    SDL_RenderFillRect(renderer_Paint_(d), &frect);
}

void fillRectColor_Paint(const iPaint *d, iRect rect, iColor color) {
    addv_I2(&rect.pos, origin_Paint);
    SDL_SetRenderDrawColor(renderer_Paint_(d), color.r, color.g, color.b, color.a * d->alpha / 255);
    const SDL_FRect frect = { rect.pos.x, rect.pos.y, rect.size.x, rect.size.y };
    SDL_RenderFillRect(renderer_Paint_(d), &frect);
}

enum { maxFrameThickness_RoundCorner = 8 };

#define lightElevation_RoundCorner  60.0f /* degrees above horizontal, from the left */

iLocalDef float discCoverage_(float dist, float radius) {
    return iClamp(radius - dist + 0.5f, 0.0f, 1.0f);
}

/* Atlas of 2r x 2r discs: the fill, then two frame layers per thickness. The layers are drawn
   in order (frame 2, frame 1) and together sum up to the ring's coverage. Frame 1 is lit from
   the top left, fading out perpendicular to the light direction. */
static SDL_Texture *roundCorners_Paint_(const iPaint *d, int radius) {
    iWindow           *win  = d->dst;
    iRoundCornerAtlas *slot = NULL;
    iForIndices(i, win->roundCorners) {
        iRoundCornerAtlas *atlas = &win->roundCorners[i];
        if (atlas->texture && atlas->radius == radius) {
            atlas->lastUsed = win->frameCount;
            return atlas->texture;
        }
        /* Replace an empty slot or the least recently used one. */
        if (!slot || (slot->texture && (!atlas->texture || atlas->lastUsed < slot->lastUsed))) {
            slot = atlas;
        }
    }
    if (slot->texture) {
        SDL_DestroyTexture(slot->texture);
        slot->texture = NULL;
    }
    const int numFrames = iMin(maxFrameThickness_RoundCorner, radius);
    const int dim       = 2 * radius;
    const int height    = dim * (1 + 2 * numFrames);
    const float fade    = 0.5f * radius; /* half-width of the frame color transition */
    const float lightX  = cosf(lightElevation_RoundCorner * iMathPif / 180.0f);
    const float lightY  = sinf(lightElevation_RoundCorner * iMathPif / 180.0f);
    uint8_t  *pixels    = malloc(4 * dim * height);
    memset(pixels, 0xff, 4 * dim * height);
    for (int y = 0; y < dim; y++) {
        for (int x = 0; x < dim; x++) {
            const float dx    = x + 0.5f - radius;
            const float dy    = y + 0.5f - radius;
            const float dist  = sqrtf(dx * dx + dy * dy);
            const float outer = discCoverage_(dist, radius);
            const float s     = iClamp((-dx * lightX - dy * lightY) / (2.0f * fade) + 0.5f, 0.0f, 1.0f);
            const float w1    = s * s * (3.0f - 2.0f * s);
            pixels[4 * (y * dim + x) + 3] = (uint8_t) (255.0f * outer + 0.5f);
            for (int i = 0; i < numFrames; i++) {
                /* Boosted so thin arcs match the visual weight of straight edges. */
                const float cov = powf(outer - discCoverage_(dist, radius - (i + 1)), 0.8f);
                const float a1  = cov * w1;
                const float a2  = a1 < 1.0f ? cov * (1.0f - w1) / (1.0f - a1) : 0.0f;
                pixels[4 * (((1 + 2 * i) * dim + y) * dim + x) + 3] = (uint8_t) (255.0f * a2 + 0.5f);
                pixels[4 * (((2 + 2 * i) * dim + y) * dim + x) + 3] = (uint8_t) (255.0f * a1 + 0.5f);
            }
        }
    }
    SDL_Surface *surf =
        SDL_CreateSurfaceFrom(dim, height, SDL_PIXELFORMAT_RGBA32, pixels, 4 * dim);
    slot->texture  = SDL_CreateTextureFromSurface(renderer_Paint_(d), surf);
    slot->radius   = radius;
    slot->lastUsed = win->frameCount;
    if (slot->texture) {
        SDL_SetTextureBlendMode(slot->texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(slot->texture, SDL_SCALEMODE_NEAREST);
    }
    SDL_DestroySurface(surf);
    free(pixels);
    return slot->texture;
}

static iColor paletteColor_Paint_(int color) {
    iColor clr = get_Color(color & mask_ColorId);
    if (color & opaque_ColorId) {
        clr.a = 255;
    }
    return clr;
}

static void drawCorners_Paint_(const iPaint *d, SDL_Texture *atlas, iRect rect, int corners,
                               int radius, int layer, iColor clr) {
    SDL_SetTextureColorMod(atlas, clr.r, clr.g, clr.b);
    SDL_SetTextureAlphaMod(atlas, clr.a * d->alpha / 255);
    addv_I2(&rect.pos, origin_Paint);
    for (int i = 0; i < 4; i++) {
        if (corners & iBit(i + 1)) {
            const iBool isRight  = (i == 1 || i == 2);
            const iBool isBottom = (i >= 2);
            SDL_RenderTexture(
                renderer_Paint_(d),
                atlas,
                &(SDL_FRect){ isRight * radius, (2 * layer + isBottom) * radius, radius, radius },
                &(SDL_FRect){ isRight ? right_Rect(rect) - radius : left_Rect(rect),
                              isBottom ? bottom_Rect(rect) - radius : top_Rect(rect),
                              radius,
                              radius });
        }
    }
}

/* Returns NULL if the corners cannot be rounded; `corners` is cleared in that case. */
static SDL_Texture *cornerAtlas_Paint_(const iPaint *d, iRect rect, int radius, int *corners,
                                       int thickness) {
    if (radius < 1 || thickness > radius || width_Rect(rect) < 2 * radius ||
        height_Rect(rect) < 2 * radius) {
        *corners = 0;
    }
    SDL_Texture *atlas = *corners ? roundCorners_Paint_(d, radius) : NULL;
    if (!atlas) {
        *corners = 0;
    }
    return atlas;
}

static void drawFrame_Paint_(const iPaint *d, SDL_Texture *atlas, iRect rect, int radius,
                             int corners, int t, iColor frame1, iColor frame2) {
    const int x   = left_Rect(rect);
    const int y   = top_Rect(rect);
    const int w   = width_Rect(rect);
    const int h   = height_Rect(rect);
    const int rTL = corners & topLeft_RoundCorner     ? radius : 0;
    const int rTR = corners & topRight_RoundCorner    ? radius : 0;
    const int rBR = corners & bottomRight_RoundCorner ? radius : 0;
    const int rBL = corners & bottomLeft_RoundCorner  ? radius : 0;
    /* Sharp corners belong to the horizontal edges. */
    const int lt = rTL ? rTL : t, lb = rBL ? rBL : t;
    const int rt = rTR ? rTR : t, rb = rBR ? rBR : t;
    fillRectColor_Paint(d, init_Rect(x + rTL, y, w - rTL - rTR, t), frame1);
    fillRectColor_Paint(d, init_Rect(x, y + lt, t, h - lt - lb), frame1);
    fillRectColor_Paint(d, init_Rect(x + rBL, y + h - t, w - rBL - rBR, t), frame2);
    fillRectColor_Paint(d, init_Rect(x + w - t, y + rt, t, h - rt - rb), frame2);
    if (corners) {
        drawCorners_Paint_(d, atlas, rect, corners, radius, 2 * t - 1, frame2);
        drawCorners_Paint_(d, atlas, rect, corners, radius, 2 * t, frame1);
    }
}

void drawRoundRectRadius_Paint(const iPaint *d, iRect rect, int radius, int corners,
                               int frameThickness, int fillColor, int frameColor1,
                               int frameColor2) {
    const int t = iClamp(frameThickness, 1, maxFrameThickness_RoundCorner);
    if (frameColor2 < 0) {
        frameColor2 = frameColor1;
    }
    if (isTerminal_Platform()) {
        if (fillColor >= 0) {
            fillRect_Paint(d, rect, fillColor);
        }
        if (frameColor1 >= 0) {
            const iRect frameRect = adjusted_Rect(rect, zero_I2(), init1_I2(-1));
            if (frameColor1 == frameColor2) {
                drawRectThickness_Paint(d, frameRect, frameThickness, frameColor1);
            }
            else {
                drawEmbossedFrame_Paint(d, frameRect, frameColor1, frameColor2,
                                        iFalse, iFalse);
            }
        }
        return;
    }
    SDL_Texture *atlas = cornerAtlas_Paint_(d, rect, radius, &corners, t);
    if (fillColor >= 0) {
        const int x   = left_Rect(rect);
        const int y   = top_Rect(rect);
        const int w   = width_Rect(rect);
        const int h   = height_Rect(rect);
        const int rTL = corners & topLeft_RoundCorner     ? radius : 0;
        const int rTR = corners & topRight_RoundCorner    ? radius : 0;
        const int rBR = corners & bottomRight_RoundCorner ? radius : 0;
        const int rBL = corners & bottomLeft_RoundCorner  ? radius : 0;
        const int cl  = iMax(rTL, rBL);
        const int cr  = iMax(rTR, rBR);
        fillRect_Paint(d, init_Rect(x + cl, y, w - cl - cr, h), fillColor);
        if (cl) {
            fillRect_Paint(d, init_Rect(x, y + rTL, cl, h - rTL - rBL), fillColor);
        }
        if (cr) {
            fillRect_Paint(d, init_Rect(x + w - cr, y + rTR, cr, h - rTR - rBR), fillColor);
        }
        if (corners) {
            drawCorners_Paint_(d, atlas, rect, corners, radius, 0, paletteColor_Paint_(fillColor));
        }
    }
    if (frameColor1 >= 0) {
        drawFrame_Paint_(d, atlas, rect, radius, corners, t,
                         paletteColor_Paint_(frameColor1), paletteColor_Paint_(frameColor2));
    }
}

void drawRoundFrameColor_Paint(const iPaint *d, iRect rect, int radius, int corners,
                               int frameThickness, iColor frame1, iColor frame2) {
    if (isTerminal_Platform()) {
        return;
    }
    const int    t     = iClamp(frameThickness, 1, maxFrameThickness_RoundCorner);
    SDL_Texture *atlas = cornerAtlas_Paint_(d, rect, radius, &corners, t);
    drawFrame_Paint_(d, atlas, rect, radius, corners, t, frame1, frame2);
}

void drawSoftShadow_Paint(const iPaint *d, iRect inner, int thickness, int color, int alpha) {
    addv_I2(&inner.pos, origin_Paint);
    SDL_Renderer *render = renderer_Paint_(d);
    SDL_Texture *shadow = get_Window()->borderShadow;
    if (!shadow) {
        return;
    }
    const iInt2 size = size_SDLTexture(shadow);
    const iRect outer = expanded_Rect(inner, init1_I2(thickness));
    const iColor clr = get_Color(color);
    SDL_SetTextureColorMod(shadow, clr.r, clr.g, clr.b);
    SDL_SetTextureAlphaMod(shadow, alpha);
    /* Classic stretched segmented border. */
    SDL_RenderTexture(render, shadow, &(SDL_FRect){ 0, 0, size.x / 2, size.y / 2},
                   &(SDL_FRect){ outer.pos.x, outer.pos.y, thickness, thickness });
    SDL_RenderTexture(render, shadow, &(SDL_FRect){ size.x / 2, 0, 1, size.y / 2},
                   &(SDL_FRect){ inner.pos.x, outer.pos.y, inner.size.x, thickness });
    SDL_RenderTexture(render, shadow, &(SDL_FRect){ size.x / 2, 0, size.x / 2, size.y / 2},
                   &(SDL_FRect){ right_Rect(outer) - thickness, outer.pos.y, thickness, thickness });
    SDL_RenderTexture(render, shadow, &(SDL_FRect){ size.x / 2, size.y / 2, size.x / 2, 1 },
                   &(SDL_FRect){ right_Rect(inner), inner.pos.y, thickness, inner.size.y });
    SDL_RenderTexture(render, shadow, &(SDL_FRect){ size.x / 2, size.y / 2, size.x / 2, size.y / 2},
                   &(SDL_FRect){ right_Rect(inner), bottom_Rect(inner), thickness, thickness });
    SDL_RenderTexture(render, shadow, &(SDL_FRect){ size.x / 2, size.y / 2, 1, size.y / 2},
                   &(SDL_FRect){ inner.pos.x, bottom_Rect(inner), inner.size.x, thickness });
    SDL_RenderTexture(render, shadow, &(SDL_FRect){ 0, size.y / 2, size.x / 2, size.y / 2},
                   &(SDL_FRect){ outer.pos.x, bottom_Rect(inner), thickness, thickness });
    SDL_RenderTexture(render, shadow, &(SDL_FRect){ 0, size.y / 2, size.x / 2, 1 },
                   &(SDL_FRect){ outer.pos.x, inner.pos.y, thickness, inner.size.y });
}

void drawSoftEdge_Paint(const iPaint *d, int x, iRangei ySpan, int thickness, iColor color,
                        int alpha) {
    SDL_Texture *shadow = get_Window()->borderShadow;
    if (!shadow || thickness == 0) {
        return;
    }
    x += origin_Paint.x;
    ySpan.start += origin_Paint.y;
    ySpan.end   += origin_Paint.y;
    const iInt2  size   = size_SDLTexture(shadow);
    SDL_SetTextureColorMod(shadow, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(shadow, alpha);
    /* The middle row of the shadow fades out from the center towards both edges. */
    SDL_RenderTexture(renderer_Paint_(d),
                      shadow,
                      &(SDL_FRect){ thickness > 0 ? size.x / 2 : 0, size.y / 2, size.x / 2, 1 },
                      &(SDL_FRect){ thickness > 0 ? x : x + thickness,
                                    ySpan.start,
                                    iAbs(thickness),
                                    size_Range(&ySpan) });
}

void drawSoftHorizontalEdge_Paint(const iPaint *d, int y, iRangei xSpan, int thickness, int color,
                                  int alpha) {
    SDL_Texture *shadow = get_Window()->borderShadow;
    if (!shadow || thickness == 0) {
        return;
    }
    y += origin_Paint.y;
    xSpan.start += origin_Paint.x;
    xSpan.end   += origin_Paint.x;
    const iInt2  size = size_SDLTexture(shadow);
    const iColor clr  = get_Color(color);
    SDL_SetTextureColorMod(shadow, clr.r, clr.g, clr.b);
    SDL_SetTextureAlphaMod(shadow, alpha);
    SDL_RenderTexture(renderer_Paint_(d),
                      shadow,
                      &(SDL_FRect){ size.x / 2, thickness > 0 ? size.y / 2 : 0, 1, size.y / 2 },
                      &(SDL_FRect){ xSpan.start,
                                    thickness > 0 ? y : y + thickness,
                                    size_Range(&xSpan),
                                    iAbs(thickness) });
}

void drawSoftCrease_Paint(const iPaint *d, int x, iRangei ySpan, int thickness, iColor color,
                          int alpha) {
    drawSoftEdge_Paint(d, x, ySpan, -thickness, color, alpha);
    drawSoftEdge_Paint(d, x, ySpan, thickness, color, alpha);
}

void drawFalloffShadow_Paint(const iPaint *d, int x, iRangei ySpan, int thickness, iColor color,
                            int alpha) {
    SDL_Texture *shadow = get_Window()->falloffShadow;
    if (!shadow || thickness == 0) {
        return;
    }
    x += origin_Paint.x;
    ySpan.start += origin_Paint.y;
    ySpan.end   += origin_Paint.y;
    const iInt2 size = size_SDLTexture(shadow);
    SDL_SetTextureColorMod(shadow, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(shadow, alpha);
    SDL_RenderTexture(renderer_Paint_(d),
                      shadow,
                      &(SDL_FRect){ thickness > 0 ? size.x / 2 : 0, 0, size.x / 2, 1 },
                      &(SDL_FRect){ thickness > 0 ? x : x + thickness,
                                    ySpan.start,
                                    iAbs(thickness),
                                    size_Range(&ySpan) });
}

void drawLines_Paint(const iPaint *d, const iInt2 *points, size_t n, int color) {
    setColor_Paint_(d, color);
    SDL_FPoint *offsetPoints = malloc(sizeof(SDL_FPoint) * n);
    for (size_t i = 0; i < n; i++) {
        const iInt2 p = add_I2(points[i], origin_Paint);
        offsetPoints[i] = (SDL_FPoint){ p.x, p.y };
    }
    SDL_RenderLines(renderer_Paint_(d), offsetPoints, (int) n);
    free(offsetPoints);
}

void drawEmbossedFrame_Paint(const iPaint *p, iRect rect, int color1, int color2, iBool noBottom,
                             iBool noTop) {
    iInt2 points[] = {
        bottomLeft_Rect(rect),
        topLeft_Rect(rect),
        topRight_Rect(rect),
        bottomRight_Rect(rect),
        bottomLeft_Rect(rect)
    };
#if SDL_VERSION == SDL_VERSIONNUM(2, 0, 16)
    if (isOpenGLRenderer_Window()) {
        /* A very curious regression in SDL 2.0.16. */
        points[3].x--;
    }
#endif
    if (noBottom) {
        drawLines_Paint(p, points + 2, 2, color2);
        drawLines_Paint(p, points, 3, color1);
    }
    else {
        drawLines_Paint(p, points + 2, 3, color2);
        drawLines_Paint(p, points, noTop ? 2 : 3, color1);
    }
}

void drawPin_Paint(iPaint *d, iRect rangeRect, int dir, int pinColor) {
    const int height = height_Rect(rangeRect);
    iRect pin;
    if (dir == 0) {
        pin = (iRect){ add_I2(topLeft_Rect(rangeRect), init_I2(-gap_UI / 4, -gap_UI)),
                       init_I2(gap_UI / 2, height + gap_UI) };
    }
    else {
        pin = (iRect){ addX_I2(topRight_Rect(rangeRect), -gap_UI / 4),
                       init_I2(gap_UI / 2, height + gap_UI) };
    }
    fillRect_Paint(d, pin, pinColor);
    fillRect_Paint(d, initCentered_Rect(dir == 0 ? topMid_Rect(pin) : bottomMid_Rect(pin),
                                        init1_I2(gap_UI * 2)), pinColor);
}

iInt2 size_SDLTexture(SDL_Texture *d) {
    return init_I2(d->w, d->h);
}
