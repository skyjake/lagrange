#pragma once

#include <the_Foundation/range.h>
#include <the_Foundation/vec2.h>
#include <SDL_render.h>

iDeclareType(VisBuf)
iDeclareType(VisBufTexture)

struct Impl_VisBufTexture {
    SDL_Texture *texture;
    int origin;
    iRangei validRange;
};

struct Impl_VisBuf {
    iInt2 texSize;
    iRangei vis;
    iVisBufTexture buffers[3];
};

iDeclareTypeConstruction(VisBuf)

void    invalidate_VisBuf       (iVisBuf *);
void    alloc_VisBuf            (iVisBuf *, const iInt2 size, int granularity);
void    dealloc_VisBuf          (iVisBuf *);
void    reposition_VisBuf       (iVisBuf *, const iRangei vis);

void    invalidRanges_VisBuf    (const iVisBuf *, const iRangei full, iRangei *out_invalidRanges);
void    draw_VisBuf             (const iVisBuf *, iInt2 topLeft);
