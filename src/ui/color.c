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

#include "color.h"

#include <the_Foundation/string.h>

static const iColor transparent_;

static const iColor darkPalette_[] = {
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

static const iColor lightPalette_[] = {
    { 0,   0,   0,   255 },
    { 75,  75,  75,  255 },
    { 150, 150, 150, 255 },
    { 235, 235, 235, 255 },
    { 255, 255, 255, 255 },

    { 112, 80,  20,  255 },
    { 255, 200, 86,  255 },
    { 0,   80,  118, 255 },
    { 120, 200, 220, 255 },

    { 255, 255, 32,  255 },
    { 255, 64,  64,  255 },
    { 255, 0,   255, 255 },
    { 132, 132, 255, 255 },
    { 0,   150, 0,   255 },
};

static iColor palette_[max_ColorId];

iLocalDef void copy_(enum iColorId dst, enum iColorId src) {
    set_Color(dst, get_Color(src));
}

void setThemePalette_Color(enum iColorTheme theme) {
    memcpy(palette_, isDark_ColorTheme(theme) ? darkPalette_ : lightPalette_, sizeof(darkPalette_));
    switch (theme) {
        case pureBlack_ColorTheme:
            copy_(uiBackground_ColorId, black_ColorId);
            copy_(uiBackgroundHover_ColorId, black_ColorId);
            copy_(uiBackgroundPressed_ColorId, orange_ColorId);
            copy_(uiBackgroundSelected_ColorId, teal_ColorId);
            copy_(uiBackgroundFramelessHover_ColorId, teal_ColorId);
            copy_(uiText_ColorId, gray75_ColorId);
            copy_(uiTextPressed_ColorId, black_ColorId);
            copy_(uiTextStrong_ColorId, white_ColorId);
            copy_(uiTextSelected_ColorId, white_ColorId);
            copy_(uiTextFramelessHover_ColorId, white_ColorId);
            copy_(uiTextDisabled_ColorId, gray25_ColorId);
            copy_(uiTextShortcut_ColorId, cyan_ColorId);
            copy_(uiTextAction_ColorId, cyan_ColorId);
            copy_(uiTextCaution_ColorId, orange_ColorId);
            copy_(uiFrame_ColorId, black_ColorId);
            copy_(uiEmboss1_ColorId, gray25_ColorId);
            copy_(uiEmboss2_ColorId, black_ColorId);
            copy_(uiEmbossHover1_ColorId, cyan_ColorId);
            copy_(uiEmbossHover2_ColorId, teal_ColorId);
            copy_(uiEmbossPressed1_ColorId, brown_ColorId);
            copy_(uiEmbossPressed2_ColorId, gray75_ColorId);
            copy_(uiEmbossSelected1_ColorId, cyan_ColorId);
            copy_(uiEmbossSelected2_ColorId, black_ColorId);
            copy_(uiEmbossSelectedHover1_ColorId, white_ColorId);
            copy_(uiEmbossSelectedHover2_ColorId, cyan_ColorId);
            copy_(uiInputBackground_ColorId, black_ColorId);
            copy_(uiInputBackgroundFocused_ColorId, black_ColorId);
            copy_(uiInputText_ColorId, gray75_ColorId);
            copy_(uiInputTextFocused_ColorId, white_ColorId);
            copy_(uiInputFrame_ColorId, gray25_ColorId);
            copy_(uiInputFrameHover_ColorId, cyan_ColorId);
            copy_(uiInputFrameFocused_ColorId, orange_ColorId);
            copy_(uiInputCursor_ColorId, orange_ColorId);
            copy_(uiInputCursorText_ColorId, black_ColorId);
            copy_(uiHeading_ColorId, cyan_ColorId);
            copy_(uiAnnotation_ColorId, teal_ColorId);
            copy_(uiIcon_ColorId, cyan_ColorId);
            copy_(uiIconHover_ColorId, cyan_ColorId);
            copy_(uiSeparator_ColorId, gray25_ColorId);
            copy_(uiMarked_ColorId, brown_ColorId);
            copy_(uiMatching_ColorId, teal_ColorId);
            break;
        default:
        case dark_ColorTheme:
            copy_(uiBackground_ColorId, gray25_ColorId);
            copy_(uiBackgroundHover_ColorId, gray25_ColorId);
            copy_(uiBackgroundPressed_ColorId, orange_ColorId);
            copy_(uiBackgroundSelected_ColorId, teal_ColorId);
            copy_(uiBackgroundFramelessHover_ColorId, teal_ColorId);
            copy_(uiText_ColorId, gray75_ColorId);
            copy_(uiTextPressed_ColorId, black_ColorId);
            copy_(uiTextStrong_ColorId, white_ColorId);
            copy_(uiTextSelected_ColorId, white_ColorId);
            copy_(uiTextDisabled_ColorId, gray50_ColorId);
            copy_(uiTextFramelessHover_ColorId, white_ColorId);
            copy_(uiTextShortcut_ColorId, cyan_ColorId);
            copy_(uiTextAction_ColorId, cyan_ColorId);
            copy_(uiTextCaution_ColorId, orange_ColorId);
            copy_(uiFrame_ColorId, gray25_ColorId);
            copy_(uiEmboss1_ColorId, gray50_ColorId);
            copy_(uiEmboss2_ColorId, black_ColorId);
            copy_(uiEmbossHover1_ColorId, cyan_ColorId);
            copy_(uiEmbossHover2_ColorId, teal_ColorId);
            copy_(uiEmbossPressed1_ColorId, brown_ColorId);
            copy_(uiEmbossPressed2_ColorId, white_ColorId);
            copy_(uiEmbossSelected1_ColorId, cyan_ColorId);
            copy_(uiEmbossSelected2_ColorId, black_ColorId);
            copy_(uiEmbossSelectedHover1_ColorId, white_ColorId);
            copy_(uiEmbossSelectedHover2_ColorId, cyan_ColorId);
            copy_(uiInputBackground_ColorId, black_ColorId);
            copy_(uiInputBackgroundFocused_ColorId, black_ColorId);
            copy_(uiInputText_ColorId, gray75_ColorId);
            copy_(uiInputTextFocused_ColorId, white_ColorId);
            copy_(uiInputFrame_ColorId, gray50_ColorId);
            copy_(uiInputFrameHover_ColorId, cyan_ColorId);
            copy_(uiInputFrameFocused_ColorId, orange_ColorId);
            copy_(uiInputCursor_ColorId, orange_ColorId);
            copy_(uiInputCursorText_ColorId, black_ColorId);
            copy_(uiHeading_ColorId, cyan_ColorId);
            copy_(uiAnnotation_ColorId, teal_ColorId);
            copy_(uiIcon_ColorId, cyan_ColorId);
            copy_(uiIconHover_ColorId, cyan_ColorId);
            copy_(uiSeparator_ColorId, black_ColorId);
            copy_(uiMarked_ColorId, brown_ColorId);
            copy_(uiMatching_ColorId, teal_ColorId);
            break;
        case light_ColorTheme:
            copy_(uiBackground_ColorId, gray75_ColorId);
            copy_(uiBackgroundHover_ColorId, gray75_ColorId);
            copy_(uiBackgroundSelected_ColorId, orange_ColorId);
            copy_(uiBackgroundPressed_ColorId, cyan_ColorId);
            copy_(uiBackgroundFramelessHover_ColorId, orange_ColorId);
            copy_(uiText_ColorId, black_ColorId);
            copy_(uiTextStrong_ColorId, teal_ColorId);
            copy_(uiTextPressed_ColorId, black_ColorId);
            copy_(uiTextSelected_ColorId, black_ColorId);
            copy_(uiTextDisabled_ColorId, gray50_ColorId);
            copy_(uiTextFramelessHover_ColorId, black_ColorId);
            copy_(uiTextShortcut_ColorId, brown_ColorId);
            copy_(uiTextAction_ColorId, brown_ColorId);
            copy_(uiTextCaution_ColorId, teal_ColorId);
            copy_(uiFrame_ColorId, gray50_ColorId);
            copy_(uiEmboss1_ColorId, white_ColorId);
            copy_(uiEmboss2_ColorId, gray50_ColorId);
            copy_(uiEmbossHover1_ColorId, gray50_ColorId);
            copy_(uiEmbossHover2_ColorId, gray25_ColorId);
            copy_(uiEmbossPressed1_ColorId, black_ColorId);
            copy_(uiEmbossPressed2_ColorId, white_ColorId);
            copy_(uiEmbossSelected1_ColorId, white_ColorId);
            copy_(uiEmbossSelected2_ColorId, brown_ColorId);
            copy_(uiEmbossSelectedHover1_ColorId, brown_ColorId);
            copy_(uiEmbossSelectedHover2_ColorId, brown_ColorId);
            copy_(uiInputBackground_ColorId, white_ColorId);
            copy_(uiInputBackgroundFocused_ColorId, white_ColorId);
            copy_(uiInputText_ColorId, gray25_ColorId);
            copy_(uiInputTextFocused_ColorId, black_ColorId);
            copy_(uiInputFrame_ColorId, gray50_ColorId);
            copy_(uiInputFrameHover_ColorId, brown_ColorId);
            copy_(uiInputFrameFocused_ColorId, teal_ColorId);
            copy_(uiInputCursor_ColorId, teal_ColorId);
            copy_(uiInputCursorText_ColorId, white_ColorId);
            copy_(uiHeading_ColorId, brown_ColorId);
            copy_(uiAnnotation_ColorId, gray50_ColorId);
            copy_(uiIcon_ColorId, brown_ColorId);
            copy_(uiIconHover_ColorId, brown_ColorId);
            copy_(uiSeparator_ColorId, gray50_ColorId);
            copy_(uiMarked_ColorId, cyan_ColorId);
            copy_(uiMatching_ColorId, orange_ColorId);
            break;
        case pureWhite_ColorTheme:
            copy_(uiBackground_ColorId, white_ColorId);
            copy_(uiBackgroundHover_ColorId, gray75_ColorId);
            copy_(uiBackgroundSelected_ColorId, orange_ColorId);
            copy_(uiBackgroundPressed_ColorId, cyan_ColorId);
            copy_(uiBackgroundFramelessHover_ColorId, orange_ColorId);
            copy_(uiText_ColorId, gray25_ColorId);
            copy_(uiTextPressed_ColorId, black_ColorId);
            copy_(uiTextDisabled_ColorId, gray75_ColorId);
            copy_(uiTextStrong_ColorId, black_ColorId);
            copy_(uiTextSelected_ColorId, black_ColorId);
            copy_(uiTextFramelessHover_ColorId, black_ColorId);
            copy_(uiTextShortcut_ColorId, brown_ColorId);
            copy_(uiTextAction_ColorId, brown_ColorId);
            copy_(uiTextCaution_ColorId, teal_ColorId);
            copy_(uiFrame_ColorId, gray75_ColorId);
            copy_(uiEmboss1_ColorId, white_ColorId);
            copy_(uiEmboss2_ColorId, white_ColorId);
            copy_(uiEmbossHover1_ColorId, gray25_ColorId);
            copy_(uiEmbossHover2_ColorId, gray25_ColorId);
            copy_(uiEmbossPressed1_ColorId, black_ColorId);
            copy_(uiEmbossPressed2_ColorId, teal_ColorId);
            copy_(uiEmbossSelected1_ColorId, white_ColorId);
            copy_(uiEmbossSelected2_ColorId, brown_ColorId);
            copy_(uiEmbossSelectedHover1_ColorId, gray50_ColorId);
            copy_(uiEmbossSelectedHover2_ColorId, gray50_ColorId);
            copy_(uiInputBackground_ColorId, white_ColorId);
            copy_(uiInputBackgroundFocused_ColorId, white_ColorId);
            copy_(uiInputText_ColorId, gray25_ColorId);
            copy_(uiInputTextFocused_ColorId, black_ColorId);
            copy_(uiInputFrame_ColorId, gray50_ColorId);
            copy_(uiInputFrameHover_ColorId, brown_ColorId);
            copy_(uiInputFrameFocused_ColorId, teal_ColorId);
            copy_(uiInputCursor_ColorId, teal_ColorId);
            copy_(uiInputCursorText_ColorId, white_ColorId);
            copy_(uiHeading_ColorId, brown_ColorId);
            copy_(uiAnnotation_ColorId, gray50_ColorId);
            copy_(uiIcon_ColorId, brown_ColorId);
            copy_(uiIconHover_ColorId, brown_ColorId);
            copy_(uiSeparator_ColorId, gray50_ColorId);
            copy_(uiMarked_ColorId, cyan_ColorId);
            copy_(uiMatching_ColorId, orange_ColorId);
            break;
    }
    palette_[uiMarked_ColorId].a = 128;
    palette_[uiMatching_ColorId].a = 128;
}

iColor get_Color(int color) {
    const iColor *rgba = &transparent_;
    if (color >= 0 && color < max_ColorId) {
        rgba = &palette_[color];
    }
    return *rgba;
}

void set_Color(int color, iColor rgba) {
    if (color >= uiBackground_ColorId && color < max_ColorId) {
        palette_[color] = rgba;
    }
}

iColor mix_Color(iColor c1, iColor c2, float t) {
    t = iClamp(t, 0.0f, 1.0f);
    return (iColor){ c1.r * (1 - t) + c2.r * t,
                     c1.g * (1 - t) + c2.g * t,
                     c1.b * (1 - t) + c2.b * t,
                     c1.a * (1 - t) + c2.a * t };
}

int delta_Color(iColor c1, iColor c2) {
    return iAbs(c1.r - c2.r) + iAbs(c1.g - c2.g) + iAbs(c1.b - c2.b);
}

iLocalDef iBool equal_Color_(const iColor *x, const iColor *y) {
    return memcmp(x, y, sizeof(iColor)) == 0;
}

int darker_Color(int color) {
    const iColor rgb = get_Color(color);
    for (int i = 0; i < uiFirst_ColorId; i++) {
        if (equal_Color_(&rgb, &palette_[i])) {
            return i > 0 ? i - 1 : i;
        }
    }
    return color;
}

int lighter_Color(int color) {
    const iColor rgb = get_Color(color);
    for (int i = 0; i < uiFirst_ColorId; i++) {
        if (equal_Color_(&rgb, &palette_[i])) {
            return i < uiFirst_ColorId - 1 ? i + 1 : i;
        }
    }
    return color;
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
    iAssert(asciiBase_ColorEscape + color <= 127);
    return format_CStr("\r%c", asciiBase_ColorEscape + color);
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
