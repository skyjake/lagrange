#include "color.h"

static const iColor transparent_;

iColor get_Color(int color) {
    static const iColor palette[] = {
        { 0,   0,   0,   255 },
        { 40,  40,  40,  255 },
        { 80,  80,  80,  255 },
        { 160, 160, 160, 255 },
        { 255, 255, 255, 255 },
        { 106, 80,  0,   255 },
        { 255, 192, 0,   255 },
        { 0,   96,  128, 255 },
        { 0,   192, 255, 255 },
        { 255, 255, 32,  255 },
        { 255, 64,  64,  255 },
        { 255, 0,   255, 255 },
        { 132, 132, 255, 255 },
        { 0,   200, 0,   255 },
    };
    const iColor *clr = &transparent_;
    if (color >= 0 && color < (int) iElemCount(palette)) {
        clr = &palette[color];
    }
    return *clr;
}
