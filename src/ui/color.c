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

    { 142, 100,  20,  255 },
    { 215, 210, 200,  255 },
    { 10,  85,  112, 255 },
    { 150, 205, 220, 255 },

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
            set_Color(uiBackgroundSidebar_ColorId,
                      mix_Color(get_Color(black_ColorId), get_Color(gray25_ColorId), 0.66f));
            copy_(uiText_ColorId, gray75_ColorId);
            copy_(uiTextPressed_ColorId, black_ColorId);
            copy_(uiTextStrong_ColorId, white_ColorId);
            copy_(uiTextDim_ColorId, gray75_ColorId);
            copy_(uiTextSelected_ColorId, white_ColorId);
            copy_(uiTextFramelessHover_ColorId, white_ColorId);
            copy_(uiTextDisabled_ColorId, gray25_ColorId);
            copy_(uiTextShortcut_ColorId, cyan_ColorId);
            copy_(uiTextAction_ColorId, cyan_ColorId);
            copy_(uiTextCaution_ColorId, orange_ColorId);
            copy_(uiTextAppTitle_ColorId, cyan_ColorId);
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
            set_Color(uiBackgroundSidebar_ColorId,
                      mix_Color(get_Color(black_ColorId), get_Color(gray25_ColorId), 0.66f));
            copy_(uiText_ColorId, gray75_ColorId);
            copy_(uiTextPressed_ColorId, black_ColorId);
            copy_(uiTextStrong_ColorId, white_ColorId);
            copy_(uiTextDim_ColorId, gray75_ColorId);
            copy_(uiTextSelected_ColorId, white_ColorId);
            copy_(uiTextDisabled_ColorId, gray50_ColorId);
            copy_(uiTextFramelessHover_ColorId, white_ColorId);
            copy_(uiTextShortcut_ColorId, cyan_ColorId);
            copy_(uiTextAction_ColorId, cyan_ColorId);
            copy_(uiTextCaution_ColorId, orange_ColorId);
            copy_(uiTextAppTitle_ColorId, cyan_ColorId);
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
            set_Color(uiBackgroundSidebar_ColorId,
                      mix_Color(get_Color(white_ColorId), get_Color(gray75_ColorId), 0.5f));
            copy_(uiText_ColorId, black_ColorId);
            copy_(uiTextStrong_ColorId, black_ColorId);
            copy_(uiTextDim_ColorId, gray25_ColorId);
            copy_(uiTextPressed_ColorId, black_ColorId);
            copy_(uiTextSelected_ColorId, black_ColorId);
            copy_(uiTextDisabled_ColorId, gray50_ColorId);
            copy_(uiTextFramelessHover_ColorId, black_ColorId);
            copy_(uiTextShortcut_ColorId, brown_ColorId);
            copy_(uiTextAction_ColorId, brown_ColorId);
            copy_(uiTextCaution_ColorId, teal_ColorId);
            copy_(uiTextAppTitle_ColorId, teal_ColorId);
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
            set_Color(uiBackgroundSidebar_ColorId,
                      mix_Color(get_Color(white_ColorId), get_Color(gray75_ColorId), 0.5f));
            set_Color(uiText_ColorId,
                      mix_Color(get_Color(black_ColorId), get_Color(gray25_ColorId), 0.5f));
            copy_(uiTextPressed_ColorId, black_ColorId);
            copy_(uiTextDisabled_ColorId, gray75_ColorId);
            copy_(uiTextStrong_ColorId, black_ColorId);
            copy_(uiTextDim_ColorId, gray25_ColorId);
            copy_(uiTextSelected_ColorId, black_ColorId);
            copy_(uiTextFramelessHover_ColorId, black_ColorId);
            copy_(uiTextShortcut_ColorId, brown_ColorId);
            copy_(uiTextAction_ColorId, brown_ColorId);
            copy_(uiTextCaution_ColorId, teal_ColorId);
            copy_(uiTextAppTitle_ColorId, teal_ColorId);
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
            copy_(uiSeparator_ColorId, gray75_ColorId);
            copy_(uiMarked_ColorId, cyan_ColorId);
            copy_(uiMatching_ColorId, orange_ColorId);
            break;
    }
    set_Color(uiSubheading_ColorId,
              mix_Color(get_Color(uiText_ColorId),
                        get_Color(uiIcon_ColorId),
                        isDark_ColorTheme(theme) ? 0.5f : 0.75f));
    set_Color(uiBackgroundUnfocusedSelection_ColorId,
              mix_Color(get_Color(uiBackground_ColorId),
                        get_Color(uiBackgroundSelected_ColorId),
                        isDark_ColorTheme(theme) ? 0.25f : 0.66f));
    setHsl_Color(uiBackgroundFolder_ColorId,
                 addSatLum_HSLColor(get_HSLColor(uiBackgroundSidebar_ColorId),
                                    0,
                                    theme == pureBlack_ColorTheme ? -1
                                    : theme == dark_ColorTheme    ? -0.04
                                                                  : -0.075));
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
    /* Double-\r is used for range extension. */
    if (color + asciiBase_ColorEscape > 127) {
        iAssert(color - asciiExtended_ColorEscape + asciiBase_ColorEscape <= 127);
        return format_CStr("\r\r%c", color - asciiExtended_ColorEscape + asciiBase_ColorEscape);
    }
    return format_CStr("\r%c", color + asciiBase_ColorEscape);
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

static const iColor ansi8BitColors_[256] = {
    { 0, 0, 0, 255 },
    { 170, 0, 0, 255 },
    { 0, 170, 0, 255 },
    { 170, 85, 0, 255 },
    { 0, 0, 170, 255 },
    { 170, 0, 170, 255 },
    { 0, 170, 170, 255 },
    { 170, 170, 170, 255 },

    { 85, 85, 85, 255 },
    { 255, 85, 85, 255 },
    { 85, 255, 85, 255 },
    { 255, 255, 85, 255 },
    { 85, 85, 255, 255 },
    { 255, 85, 255, 255 },
    { 85, 255, 255, 255 },
    { 255, 255, 255, 255 },

    { 0, 0, 0, 255 },
    { 0, 0, 95, 255 },
    { 0, 0, 135, 255 },
    { 0, 0, 175, 255 },
    { 0, 0, 215, 255 },
    { 0, 0, 255, 255 },
    { 0, 95, 0, 255 },
    { 0, 95, 95, 255 },
    { 0, 95, 135, 255 },
    { 0, 95, 175, 255 },
    { 0, 95, 215, 255 },
    { 0, 95, 255, 255 },
    { 0, 135, 0, 255 },
    { 0, 135, 95, 255 },
    { 0, 135, 135, 255 },
    { 0, 135, 175, 255 },
    { 0, 135, 215, 255 },
    { 0, 135, 255, 255 },
    { 0, 175, 0, 255 },
    { 0, 175, 95, 255 },
    { 0, 175, 135, 255 },
    { 0, 175, 175, 255 },
    { 0, 175, 215, 255 },
    { 0, 175, 255, 255 },
    { 0, 215, 0, 255 },
    { 0, 215, 95, 255 },
    { 0, 215, 135, 255 },
    { 0, 215, 175, 255 },
    { 0, 215, 215, 255 },
    { 0, 215, 255, 255 },
    { 0, 255, 0, 255 },
    { 0, 255, 95, 255 },
    { 0, 255, 135, 255 },
    { 0, 255, 175, 255 },
    { 0, 255, 215, 255 },
    { 0, 255, 255, 255 },
    { 95, 0, 0, 255 },
    { 95, 0, 95, 255 },
    { 95, 0, 135, 255 },
    { 95, 0, 175, 255 },
    { 95, 0, 215, 255 },
    { 95, 0, 255, 255 },
    { 95, 95, 0, 255 },
    { 95, 95, 95, 255 },
    { 95, 95, 135, 255 },
    { 95, 95, 175, 255 },
    { 95, 95, 215, 255 },
    { 95, 95, 255, 255 },
    { 95, 135, 0, 255 },
    { 95, 135, 95, 255 },
    { 95, 135, 135, 255 },
    { 95, 135, 175, 255 },
    { 95, 135, 215, 255 },
    { 95, 135, 255, 255 },
    { 95, 175, 0, 255 },
    { 95, 175, 95, 255 },
    { 95, 175, 135, 255 },
    { 95, 175, 175, 255 },
    { 95, 175, 215, 255 },
    { 95, 175, 255, 255 },
    { 95, 215, 0, 255 },
    { 95, 215, 95, 255 },
    { 95, 215, 135, 255 },
    { 95, 215, 175, 255 },
    { 95, 215, 215, 255 },
    { 95, 215, 255, 255 },
    { 95, 255, 0, 255 },
    { 95, 255, 95, 255 },
    { 95, 255, 135, 255 },
    { 95, 255, 175, 255 },
    { 95, 255, 215, 255 },
    { 95, 255, 255, 255 },
    { 135, 0, 0, 255 },
    { 135, 0, 95, 255 },
    { 135, 0, 135, 255 },
    { 135, 0, 175, 255 },
    { 135, 0, 215, 255 },
    { 135, 0, 255, 255 },
    { 135, 95, 0, 255 },
    { 135, 95, 95, 255 },
    { 135, 95, 135, 255 },
    { 135, 95, 175, 255 },
    { 135, 95, 215, 255 },
    { 135, 95, 255, 255 },
    { 135, 135, 0, 255 },
    { 135, 135, 95, 255 },
    { 135, 135, 135, 255 },
    { 135, 135, 175, 255 },
    { 135, 135, 215, 255 },
    { 135, 135, 255, 255 },
    { 135, 175, 0, 255 },
    { 135, 175, 95, 255 },
    { 135, 175, 135, 255 },
    { 135, 175, 175, 255 },
    { 135, 175, 215, 255 },
    { 135, 175, 255, 255 },
    { 135, 215, 0, 255 },
    { 135, 215, 95, 255 },
    { 135, 215, 135, 255 },
    { 135, 215, 175, 255 },
    { 135, 215, 215, 255 },
    { 135, 215, 255, 255 },
    { 135, 255, 0, 255 },
    { 135, 255, 95, 255 },
    { 135, 255, 135, 255 },
    { 135, 255, 175, 255 },
    { 135, 255, 215, 255 },
    { 135, 255, 255, 255 },
    { 175, 0, 0, 255 },
    { 175, 0, 95, 255 },
    { 175, 0, 135, 255 },
    { 175, 0, 175, 255 },
    { 175, 0, 215, 255 },
    { 175, 0, 255, 255 },
    { 175, 95, 0, 255 },
    { 175, 95, 95, 255 },
    { 175, 95, 135, 255 },
    { 175, 95, 175, 255 },
    { 175, 95, 215, 255 },
    { 175, 95, 255, 255 },
    { 175, 135, 0, 255 },
    { 175, 135, 95, 255 },
    { 175, 135, 135, 255 },
    { 175, 135, 175, 255 },
    { 175, 135, 215, 255 },
    { 175, 135, 255, 255 },
    { 175, 175, 0, 255 },
    { 175, 175, 95, 255 },
    { 175, 175, 135, 255 },
    { 175, 175, 175, 255 },
    { 175, 175, 215, 255 },
    { 175, 175, 255, 255 },
    { 175, 215, 0, 255 },
    { 175, 215, 95, 255 },
    { 175, 215, 135, 255 },
    { 175, 215, 175, 255 },
    { 175, 215, 215, 255 },
    { 175, 215, 255, 255 },
    { 175, 255, 0, 255 },
    { 175, 255, 95, 255 },
    { 175, 255, 135, 255 },
    { 175, 255, 175, 255 },
    { 175, 255, 215, 255 },
    { 175, 255, 255, 255 },
    { 215, 0, 0, 255 },
    { 215, 0, 95, 255 },
    { 215, 0, 135, 255 },
    { 215, 0, 175, 255 },
    { 215, 0, 215, 255 },
    { 215, 0, 255, 255 },
    { 215, 95, 0, 255 },
    { 215, 95, 95, 255 },
    { 215, 95, 135, 255 },
    { 215, 95, 175, 255 },
    { 215, 95, 215, 255 },
    { 215, 95, 255, 255 },
    { 215, 135, 0, 255 },
    { 215, 135, 95, 255 },
    { 215, 135, 135, 255 },
    { 215, 135, 175, 255 },
    { 215, 135, 215, 255 },
    { 215, 135, 255, 255 },
    { 215, 175, 0, 255 },
    { 215, 175, 95, 255 },
    { 215, 175, 135, 255 },
    { 215, 175, 175, 255 },
    { 215, 175, 215, 255 },
    { 215, 175, 255, 255 },
    { 215, 215, 0, 255 },
    { 215, 215, 95, 255 },
    { 215, 215, 135, 255 },
    { 215, 215, 175, 255 },
    { 215, 215, 215, 255 },
    { 215, 215, 255, 255 },
    { 215, 255, 0, 255 },
    { 215, 255, 95, 255 },
    { 215, 255, 135, 255 },
    { 215, 255, 175, 255 },
    { 215, 255, 215, 255 },
    { 215, 255, 255, 255 },
    { 255, 0, 0, 255 },
    { 255, 0, 95, 255 },
    { 255, 0, 135, 255 },
    { 255, 0, 175, 255 },
    { 255, 0, 215, 255 },
    { 255, 0, 255, 255 },
    { 255, 95, 0, 255 },
    { 255, 95, 95, 255 },
    { 255, 95, 135, 255 },
    { 255, 95, 175, 255 },
    { 255, 95, 215, 255 },
    { 255, 95, 255, 255 },
    { 255, 135, 0, 255 },
    { 255, 135, 95, 255 },
    { 255, 135, 135, 255 },
    { 255, 135, 175, 255 },
    { 255, 135, 215, 255 },
    { 255, 135, 255, 255 },
    { 255, 175, 0, 255 },
    { 255, 175, 95, 255 },
    { 255, 175, 135, 255 },
    { 255, 175, 175, 255 },
    { 255, 175, 215, 255 },
    { 255, 175, 255, 255 },
    { 255, 215, 0, 255 },
    { 255, 215, 95, 255 },
    { 255, 215, 135, 255 },
    { 255, 215, 175, 255 },
    { 255, 215, 215, 255 },
    { 255, 215, 255, 255 },
    { 255, 255, 0, 255 },
    { 255, 255, 95, 255 },
    { 255, 255, 135, 255 },
    { 255, 255, 175, 255 },
    { 255, 255, 215, 255 },
    { 255, 255, 255, 255 },

    { 0, 0, 0, 255 },
    { 11, 11, 11, 255 },
    { 22, 22, 22, 255 },
    { 33, 33, 33, 255 },
    { 44, 44, 44, 255 },
    { 55, 55, 55, 255 },
    { 67, 67, 67, 255 },
    { 78, 78, 78, 255 },
    { 89, 89, 89, 255 },
    { 100, 100, 100, 255 },
    { 111, 111, 111, 255 },
    { 122, 122, 122, 255 },
    { 133, 133, 133, 255 },
    { 144, 144, 144, 255 },
    { 155, 155, 155, 255 },
    { 166, 166, 166, 255 },
    { 177, 177, 177, 255 },
    { 188, 188, 188, 255 },
    { 200, 200, 200, 255 },
    { 211, 211, 211, 255 },
    { 222, 222, 222, 255 },
    { 233, 233, 233, 255 },
    { 244, 244, 244, 255 },
    { 255, 255, 255, 255 }
};

iColor ansiForeground_Color(iRangecc escapeSequence, int fallback) {
    iColor clr = get_Color(fallback);
    for (const char *ch = escapeSequence.start; ch < escapeSequence.end; ch++) {
        char *endPtr;
        unsigned long arg = strtoul(ch, &endPtr, 10);
        ch = endPtr;
        switch (arg) {
            default:
                break;
            case 30:
            case 31:
            case 32:
            case 33:
            case 34:
            case 35:
            case 36:
            case 37:
                clr = ansi8BitColors_[arg - 30];
                break;
            case 38: {
                /* Extended foreground color. */
                arg = strtoul(ch + 1, &endPtr, 10);
                ch  = endPtr;
                if (arg == 5) /* 8-bit palette */ {
                    arg = strtoul(ch + 1, &endPtr, 10);
                    ch  = endPtr;
                    clr = ansi8BitColors_[iClamp(arg, 0, 255)];
                }
                break;
            }
            case 90:
            case 91:
            case 92:
            case 93:
            case 94:
            case 95:
            case 96:
            case 97:
                clr = ansi8BitColors_[8 + arg - 90];
                break;
        }
    }
    /* On light backgrounds, darken the colors to make them more legible. */
    if (get_HSLColor(tmBackground_ColorId).lum > 0.5f) {
        clr.r /= 2;
        clr.g /= 2;
        clr.b /= 2;
    }
    return clr;
}
