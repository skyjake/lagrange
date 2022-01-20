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

#pragma once

#include <the_Foundation/rect.h>
#include "color.h"
#include "text.h"
#include "window.h"

iDeclareType(Paint)

struct Impl_Paint {
    iWindow *    dst;
    SDL_Texture *setTarget;
    SDL_Texture *oldTarget;
    iInt2        oldOrigin;
    uint8_t      alpha;
};

extern iInt2 origin_Paint; /* add this to all drawn positions so buffered graphics are correctly offset */

void    init_Paint          (iPaint *);

void    beginTarget_Paint   (iPaint *, SDL_Texture *target);
void    endTarget_Paint     (iPaint *);

void    setClip_Paint       (iPaint *, iRect rect);
void    unsetClip_Paint     (iPaint *);

void    drawRect_Paint          (const iPaint *, iRect rect, int color);
void    drawRectThickness_Paint (const iPaint *, iRect rect, int thickness, int color);
void    fillRect_Paint          (const iPaint *, iRect rect, int color);
void    drawSoftShadow_Paint    (const iPaint *, iRect rect, int thickness, int color, int alpha);

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

void    drawPin_Paint       (iPaint *, iRect rangeRect, int dir, int pinColor);

iInt2   size_SDLTexture     (SDL_Texture *);
