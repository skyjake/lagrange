#pragma once

#include <the_Foundation/rect.h>
#include "color.h"
#include "text.h"
#include "window.h"

iDeclareType(Paint)

struct Impl_Paint {
    iWindow *dst;
    SDL_Texture *oldTarget;
};

void    init_Paint          (iPaint *);

void    beginTarget_Paint   (iPaint *, SDL_Texture *target);
void    endTarget_Paint     (iPaint *);

void    setClip_Paint       (iPaint *, iRect rect);
void    unsetClip_Paint     (iPaint *);

void    drawRect_Paint          (const iPaint *, iRect rect, int color);
void    drawRectThickness_Paint (const iPaint *, iRect rect, int thickness, int color);
void    fillRect_Paint          (const iPaint *, iRect rect, int color);

void    drawLines_Paint (const iPaint *, const iInt2 *points, size_t count, int color);

iLocalDef void drawLine_Paint(const iPaint *d, iInt2 a, iInt2 b, int color) {
    drawLines_Paint(d, (iInt2[]){ a, b }, 2, color);
}
iLocalDef void drawHLine_Paint(const iPaint *d, iInt2 pos, int len, int color) {
    drawLine_Paint(d, pos, addX_I2(pos, len), color);
}
iLocalDef void drawVLine_Paint(const iPaint *d, iInt2 pos, int len, int color) {
    drawLine_Paint(d, pos, addY_I2(pos, len), color);
}

iInt2   size_SDLTexture     (SDL_Texture *);
