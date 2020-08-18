#include "paint.h"

#include <SDL_version.h>

iLocalDef SDL_Renderer *renderer_Paint_(const iPaint *d) {
    iAssert(d->dst);
    return d->dst->render;
}

static void setColor_Paint_(const iPaint *d, int color) {
    const iColor clr = get_Color(color & mask_ColorId);
    SDL_SetRenderDrawColor(renderer_Paint_(d), clr.r, clr.g, clr.b, clr.a);
}

void init_Paint(iPaint *d) {
    d->dst = get_Window();
    d->oldTarget = NULL;
}

void beginTarget_Paint(iPaint *d, SDL_Texture *target) {
    SDL_Renderer *rend = renderer_Paint_(d);
    d->oldTarget = SDL_GetRenderTarget(rend);
    SDL_SetRenderTarget(rend, target);
}

void endTarget_Paint(iPaint *d) {
    SDL_SetRenderTarget(renderer_Paint_(d), d->oldTarget);
    d->oldTarget = NULL;
}

void setClip_Paint(iPaint *d, iRect rect) {
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
    if (br.x == d->dst->root->rect.size.x) br.x--;
    if (br.y == d->dst->root->rect.size.y) br.y--;
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

void drawLines_Paint(const iPaint *d, const iInt2 *points, size_t count, int color) {
    setColor_Paint_(d, color);
    SDL_RenderDrawLines(renderer_Paint_(d), (const SDL_Point *) points, count);
}
