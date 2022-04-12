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

#include <the_Foundation/range.h>
#include <the_Foundation/math.h>

enum iColorTheme {
    pureBlack_ColorTheme,
    dark_ColorTheme,
    light_ColorTheme,
    pureWhite_ColorTheme,
    max_ColorTheme
};

enum iColorAccent {
    cyan_ColorAccent,
    orange_ColorAccent,
    red_ColorAccent,
    green_ColorAccent,
    blue_ColorAccent,
    gray_ColorAccent,
    max_ColorAccent
};

iLocalDef iBool isDark_ColorTheme(enum iColorTheme d) {
    return d == pureBlack_ColorTheme || d == dark_ColorTheme;
}
iLocalDef iBool isLight_ColorTheme(enum iColorTheme d) {
    return !isDark_ColorTheme(d);
}

enum iColorId {
    none_ColorId = -1,

    black_ColorId,
    gray25_ColorId,
    gray50_ColorId,
    gray75_ColorId,
    white_ColorId,
    brown_ColorId,
    orange_ColorId,
    teal_ColorId,
    cyan_ColorId,
    maroon_ColorId,
    red_ColorId,
    darkGreen_ColorId,
    green_ColorId,
    indigo_ColorId,
    blue_ColorId,

    /* user interface colors for semantic use */
    uiFirst_ColorId,
    uiBackground_ColorId = uiFirst_ColorId,
    uiBackgroundHover_ColorId,
    uiBackgroundPressed_ColorId,
    uiBackgroundSelected_ColorId,
    uiBackgroundFramelessHover_ColorId,
    uiText_ColorId,
    uiTextPressed_ColorId,
    uiTextSelected_ColorId,
    uiTextDisabled_ColorId,
    uiTextFramelessHover_ColorId,
    uiTextFramelessSelected_ColorId,
    uiTextStrong_ColorId,
    uiTextShortcut_ColorId,
    uiTextAction_ColorId,
    uiTextCaution_ColorId,
    uiFrame_ColorId,
    uiEmboss1_ColorId,
    uiEmboss2_ColorId,
    uiEmbossHover1_ColorId,
    uiEmbossHover2_ColorId,
    uiEmbossPressed1_ColorId,
    uiEmbossPressed2_ColorId,
    uiEmbossSelected1_ColorId,
    uiEmbossSelected2_ColorId,
    uiEmbossSelectedHover1_ColorId,
    uiEmbossSelectedHover2_ColorId,
    uiInputBackground_ColorId,
    uiInputBackgroundFocused_ColorId,
    uiInputText_ColorId,
    uiInputTextFocused_ColorId,
    uiInputFrame_ColorId,
    uiInputFrameHover_ColorId,
    uiInputFrameFocused_ColorId,
    uiInputCursor_ColorId,
    uiInputCursorText_ColorId,
    uiHeading_ColorId,
    uiAnnotation_ColorId,
    uiIcon_ColorId,
    uiIconHover_ColorId,
    uiSeparator_ColorId,
    uiMarked_ColorId,
    uiMatching_ColorId,
    uiBackgroundUnfocusedSelection_ColorId,
    uiBackgroundFolder_ColorId,
    uiTextDim_ColorId,
    uiSubheading_ColorId,
    uiTextAppTitle_ColorId,
    uiBackgroundSidebar_ColorId,
    uiBackgroundMenu_ColorId,
    
    /* content theme colors */
    tmFirst_ColorId,
    tmBackground_ColorId = tmFirst_ColorId,
    tmParagraph_ColorId,
    tmFirstParagraph_ColorId,
    tmQuote_ColorId,
    tmQuoteIcon_ColorId,
    tmPreformatted_ColorId,
    tmHeading1_ColorId,
    tmHeading2_ColorId,
    tmHeading3_ColorId,
    tmBannerBackground_ColorId,
    tmBannerTitle_ColorId,
    tmBannerIcon_ColorId,
    tmBannerSideTitle_ColorId,       /* derived from other theme colors */
    tmInlineContentMetadata_ColorId,
    tmBackgroundAltText_ColorId,     /* derived from other theme colors */
    tmFrameAltText_ColorId,          /* derived from other theme colors */
    tmBackgroundOpenLink_ColorId,    /* derived from other theme colors */
    tmLinkFeedEntryDate_ColorId,     /* derived from other theme colors */
    tmLinkCustomIconVisited_ColorId, /* derived from other theme colors */
    tmBadLink_ColorId,

    tmLinkIcon_ColorId,
    tmLinkIconVisited_ColorId,
    tmLinkText_ColorId,
    tmLinkTextHover_ColorId,
    tmLinkDomain_ColorId_OBSOLETE,
    tmLinkLastVisitDate_ColorId_OBSOLETE,

    tmHypertextLinkIcon_ColorId,
    tmHypertextLinkIconVisited_ColorId,
    tmHypertextLinkText_ColorId,
    tmHypertextLinkTextHover_ColorId,
    tmHypertextLinkDomain_ColorId_OBSOLETE,
    tmHypertextLinkLastVisitDate_ColorId_OBSOLETE,

    tmGopherLinkIcon_ColorId,
    tmGopherLinkIconVisited_ColorId,
    tmGopherLinkText_ColorId,
    tmGopherLinkTextHover_ColorId,
    tmGopherLinkDomain_ColorId_OBSOLETE,
    tmGopherLinkLastVisitDate_ColorId_OBSOLETE,
    
    tmBannerItemBackground_ColorId,
    tmBannerItemFrame_ColorId,
    tmBannerItemTitle_ColorId,
    tmBannerItemText_ColorId,

    max_ColorId, /* note: GmRun packs color into limited number of bits */
    tmMax_ColorId = max_ColorId - tmFirst_ColorId
};

iLocalDef iBool isLink_ColorId(enum iColorId d) {
    return d >= tmBadLink_ColorId;
}
iLocalDef iBool isBackground_ColorId(enum iColorId d) {
    return d == tmBackground_ColorId || d == tmBannerBackground_ColorId;
}
iLocalDef iBool isText_ColorId(enum iColorId d) {
    return d >= tmFirst_ColorId && !isBackground_ColorId(d);
}
iLocalDef iBool isLinkText_ColorId(enum iColorId d) {
    return d == tmLinkText_ColorId || d == tmHypertextLinkText_ColorId ||
           d == tmGopherLinkText_ColorId;
}
iLocalDef iBool isRegularText_ColorId(enum iColorId d) {
    return isLinkText_ColorId(d) || d == tmParagraph_ColorId || d == tmFirstParagraph_ColorId;
}

#define mask_ColorId                0x7f
#define permanent_ColorId           0x80  /* cannot be changed via escapes */
#define fillBackground_ColorId      0x100 /* fill background with same color, but alpha 0 */
#define opaque_ColorId              0x200
#define underline_ColorId           0x400

#define asciiBase_ColorEscape       33
#define asciiExtended_ColorEscape   (128 - asciiBase_ColorEscape)

#define restore_ColorEscape         "\v\x24" /* ASCII Cancel */
#define black_ColorEscape           "\v!"
#define gray25_ColorEscape          "\v\""
#define gray50_ColorEscape          "\v#"
#define gray75_ColorEscape          "\v$"
#define white_ColorEscape           "\v%"
#define brown_ColorEscape           "\v&"
#define orange_ColorEscape          "\v'"
#define teal_ColorEscape            "\v("
#define cyan_ColorEscape            "\v)"
#define maroon_ColorEscape          "\v*"
#define red_ColorEscape             "\v+"
#define darkGreen_ColorEscape       "\v,"
#define green_ColorEscape           "\v-"
#define indigo_ColorEscape          "\v."
#define blue_ColorEscape            "\v/"
#define uiText_ColorEscape          "\v5"
#define uiTextAction_ColorEscape    "\v="
#define uiTextCaution_ColorEscape   "\v>"
#define uiTextStrong_ColorEscape    "\v;"
#define uiHeading_ColorEscape       "\vS"

iDeclareType(Color)
iDeclareType(HSLColor)

struct Impl_Color {
    uint8_t r, g, b, a;
};

iLocalDef iBool equal_Color(const iColor a, const iColor b) {
    return memcmp(&a, &b, sizeof(a)) == 0;
}

struct Impl_HSLColor {
    float hue, sat, lum, a;
};

#define minSat_HSLColor 0.013f   /* Conversion to 8-bit RGB may result in saturation dropping to zero. */

iHSLColor       hsl_Color       (iColor);
iColor          rgb_HSLColor    (iHSLColor);
float           luma_Color      (iColor);

iHSLColor       setSat_HSLColor     (iHSLColor, float sat);
iHSLColor       setLum_HSLColor     (iHSLColor, float lum);
iHSLColor       addSatLum_HSLColor  (iHSLColor, float sat, float lum);

iColor          get_Color       (int color);
int             darker_Color    (int color);
int             lighter_Color   (int color);
void            set_Color       (int color, iColor rgba);
iColor          mix_Color       (iColor c1, iColor c2, float t);
iColor          getMixed_Color  (int color1, int color2, float t);
int             delta_Color     (iColor c1, iColor c2);

iLocalDef iHSLColor get_HSLColor(int color) {
    return hsl_Color(get_Color(color));
}
iLocalDef void setHsl_Color(int color, iHSLColor hsl) {
    set_Color(color, rgb_HSLColor(hsl));
}

iBool           loadPalette_Color       (const char *path);
void            setThemePalette_Color   (enum iColorTheme theme);

void            ansiColors_Color        (iRangecc escapeSequence, int fgDefault, int bgDefault,
                                         iColor *fg_out, iColor *bg_out); /* can be NULL */
const char *    escape_Color            (int color);
enum iColorId   parseEscape_Color       (const char *cstr, const char **endp);

int             accent_Color            (iBool isBright);
iColor          systemAccent_Color      (void); /* platform-specific impl */
