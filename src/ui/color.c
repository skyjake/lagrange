#include "color.h"

#include <the_Foundation/string.h>

static const iColor transparent_;

static iColor palette_[max_ColorId] = {
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
    /* theme colors left black until theme is seeded */
};

iColor get_Color(int color) {
    const iColor *rgba = &transparent_;
    if (color >= 0 && color < max_ColorId) {
        rgba = &palette_[color];
    }
    return *rgba;
}

void set_Color(int color, iColor rgba) {
    if (color >= tmFirst_ColorId && color < max_ColorId) {
        palette_[color] = rgba;
    }
}

iLocalDef iFloat4 normalize_(iColor d) {
    return divf_F4(init_F4(d.r, d.g, d.b, d.a), 255.0f);
}

iLocalDef iColor toColor_(iFloat4 d) {
    const iFloat4 i = addf_F4(mulf_F4(d, 255.0f), 0.5f);
    return (iColor){ (uint8_t) x_F4(i),
                     (uint8_t) y_F4(i),
                     (uint8_t) z_F4(i),
                     (uint8_t) w_F4(i) };
}

iHSLColor hsl_Color(iColor color) {
    float rgb[4];
    store_F4(normalize_(color), rgb);
    int compMax, compMin;
    if (rgb[0] >= rgb[1] && rgb[0] >= rgb[2]) {
        compMax = 0;
    }
    else if (rgb[1] >= rgb[0] && rgb[1] >= rgb[2]) {
        compMax = 1;
    }
    else {
        compMax = 2;
    }
    if (rgb[0] <= rgb[1] && rgb[0] <= rgb[2]) {
        compMin = 0;
    }
    else if (rgb[1] <= rgb[0] && rgb[1] <= rgb[2]) {
        compMin = 1;
    }
    else {
        compMin = 2;
    }
    const float rgbMax = rgb[compMax];
    const float rgbMin = rgb[compMin];
    const float lum = (rgbMax + rgbMin) / 2.0f;
    float hue = 0.0f;
    float sat = 0.0f;
    if (fabsf(rgbMax - rgbMin) > 0.00001f) {
        float chr = rgbMax - rgbMin;
        sat = chr / (1.0f - fabsf(2.0f * lum - 1.0f));
        if (compMax == 0) {
            hue = (rgb[1] - rgb[2]) / chr + (rgb[1] < rgb[2] ? 6 : 0);
        }
        else if (compMax == 1) {
            hue = (rgb[2] - rgb[0]) / chr + 2;
        }
        else {
            hue = (rgb[0] - rgb[1]) / chr + 4;
        }
    }
    return (iHSLColor){ hue * 60, sat, lum, rgb[3] }; /* hue in degrees */
}

static float hueToRgb_(float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f / 2.0f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

iColor rgb_HSLColor(iHSLColor hsl) {
    float r, g, b;
    hsl.hue /= 360.0f;
    hsl.hue = iWrapf(hsl.hue, 0, 1);
    hsl.sat = iClamp(hsl.sat, 0.0f, 1.0f);
    hsl.lum = iClamp(hsl.lum, 0.0f, 1.0f);
    if (hsl.sat < 0.00001f) {
        r = g = b = hsl.lum;
    }
    else {
        const float q = hsl.lum < 0.5f ? hsl.lum * (1 + hsl.sat)
                                       : (hsl.lum + hsl.sat - hsl.lum * hsl.sat);
        const float p = 2 * hsl.lum - q;
        r = hueToRgb_(p, q, hsl.hue + 1.0f / 3.0f);
        g = hueToRgb_(p, q, hsl.hue);
        b = hueToRgb_(p, q, hsl.hue - 1.0f / 3.0f);
    }
    return toColor_(init_F4(r, g, b, hsl.a));
}

const char *escape_Color(int color) {
    static const char *esc[] = {
        black_ColorEscape,
        gray25_ColorEscape,
        gray50_ColorEscape,
        gray75_ColorEscape,
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
    if (color >= 0 && color < (int) iElemCount(esc)) {
        return esc[color];
    }
    return format_CStr("\r%c", color + '0');
}

iHSLColor setSat_HSLColor(iHSLColor d, float sat) {
    d.sat = iClamp(sat, 0, 1);
    return d;
}

iHSLColor setLum_HSLColor(iHSLColor d, float lum) {
    d.lum = iClamp(lum, 0, 1);
    return d;
}

iHSLColor addSatLum_HSLColor(iHSLColor d, float sat, float lum) {
    d.sat = iClamp(d.sat + sat, 0, 1);
    d.lum = iClamp(d.lum + lum, 0, 1);
    return d;
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
