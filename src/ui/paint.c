#include "paint.h"

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
}

void setClip_Paint(iPaint *d, iRect rect) {
    SDL_RenderSetClipRect(renderer_Paint_(d), (const SDL_Rect *) &rect);
}

void clearClip_Paint(iPaint *d) {
    const SDL_Rect winRect = { 0, 0, d->dst->root->rect.size.x, d->dst->root->rect.size.y };
    SDL_RenderSetClipRect(renderer_Paint_(d), &winRect);
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
