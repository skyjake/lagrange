#include "color.h"

#include <the_Foundation/string.h>

static const iColor transparent_;

iColor get_Color(int color) {
    static const iColor palette[] = {
        { 0,   0,   0,   255 },
        { 24,  24,  24,  255 },
        { 40,  40,  40,  255 },
        { 80,  80,  80,  255 },
        { 160, 160, 160, 255 },
        { 208, 208, 208, 255 },
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

const char *escape_Color(int color) {
    static const char *esc[] = {
        black_ColorEscape,
        gray15_ColorEscape,
        gray25_ColorEscape,
        gray50_ColorEscape,
        gray75_ColorEscape,
        gray88_ColorEscape,
        white_ColorEscape,
        brown_ColorEscape,
        orange_ColorEscape,
        teal_ColorEscape,
        cyan_ColorEscape,
        yellow_ColorEscape,
        red_ColorEscape,
        magenta_ColorEscape,
        blue_ColorEscape,
        green_ColorEscape,
    };
    if (color >= 0 && color < max_ColorId) {
        return esc[color];
    }
    return white_ColorEscape;
}

iColor ansi_Color(iRangecc escapeSequence, int fallback) {
    iColor clr = get_Color(fallback);
    for (const char *ch = escapeSequence.start; ch < escapeSequence.end; ch++) {
        char *endPtr;
        unsigned long arg = strtoul(ch, &endPtr, 10);
        ch = endPtr;
        switch (arg) {
            default:
                break;
            case 30:
                clr = (iColor){ 0, 0, 0, 255 };
                break;
            case 31:
                clr = (iColor){ 170, 0, 0, 255 };
                break;
            case 32:
                clr = (iColor){ 0, 170, 0, 255 };
                break;
            case 33:
                clr = (iColor){ 170, 85, 0, 255 };
                break;
            case 34:
                clr = (iColor){ 0, 0, 170, 255 };
                break;
            case 35:
                clr = (iColor){ 170, 0, 170, 255 };
                break;
            case 36:
                clr = (iColor){ 0, 170, 170, 255 };
                break;
            case 37:
                clr = (iColor){ 170, 170, 170, 255 };
                break;
            case 90:
                clr = (iColor){ 85, 85, 85, 255 };
                break;
            case 91:
                clr = (iColor){ 255, 85, 85, 255 };
                break;
            case 92:
                clr = (iColor){ 85, 255, 85, 255 };
                break;
            case 93:
                clr = (iColor){ 255, 255, 85, 255 };
                break;
            case 94:
                clr = (iColor){ 85, 85, 255, 255 };
                break;
            case 95:
                clr = (iColor){ 255, 85, 255, 255 };
                break;
            case 96:
                clr = (iColor){ 85, 255, 255, 255 };
                break;
            case 97:
                clr = (iColor){ 255, 255, 255, 255 };
                break;
        }
    }
    return clr;
}
