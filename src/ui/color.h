#pragma once

#include <the_Foundation/range.h>
#include <the_Foundation/math.h>

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
    yellow_ColorId,
    red_ColorId,
    magenta_ColorId,
    blue_ColorId,
    green_ColorId,
    /* content theme colors */
    tmFirst_ColorId,
    tmBackground_ColorId = tmFirst_ColorId,
    tmParagraph_ColorId,
    tmFirstParagraph_ColorId,
    tmQuote_ColorId,
    tmPreformatted_ColorId,
    tmHeader1_ColorId,
    tmHeader2_ColorId,
    tmHeader3_ColorId,

    tmLinkIcon_ColorId,
    tmLinkIconVisited_ColorId,
    tmLinkText_ColorId,
    tmLinkTextHover_ColorId,
    tmLinkDomain_ColorId,
    tmLinkLastVisitDate_ColorId,

    tmHypertextLinkIcon_ColorId,
    tmHypertextLinkIconVisited_ColorId,
    tmHypertextLinkText_ColorId,
    tmHypertextLinkTextHover_ColorId,
    tmHypertextLinkDomain_ColorId,
    tmHypertextLinkLastVisitDate_ColorId,

    tmGopherLinkIcon_ColorId,
    tmGopherLinkIconVisited_ColorId,
    tmGopherLinkText_ColorId,
    tmGopherLinkTextHover_ColorId,
    tmGopherLinkDomain_ColorId,
    tmGopherLinkLastVisitDate_ColorId,

    tmInlineContentMetadata_ColorId,
    tmBannerBackground_ColorId,
    tmBannerTitle_ColorId,
    tmBannerIcon_ColorId,
    max_ColorId
};

#define mask_ColorId        0x3f
#define permanent_ColorId   0x80 /* cannot be changed via escapes */

#define black_ColorEscape   "\r0"
#define gray25_ColorEscape  "\r1"
#define gray50_ColorEscape  "\r2"
#define gray75_ColorEscape  "\r3"
#define white_ColorEscape   "\r4"
#define brown_ColorEscape   "\r5"
#define orange_ColorEscape  "\r6"
#define teal_ColorEscape    "\r7"
#define cyan_ColorEscape    "\r8"
#define yellow_ColorEscape  "\r9"
#define red_ColorEscape     "\r:"
#define magenta_ColorEscape "\r;"
#define blue_ColorEscape    "\r<"
#define green_ColorEscape   "\r="

iDeclareType(Color)
iDeclareType(HSLColor)

struct Impl_Color {
    uint8_t r, g, b, a;
};

struct Impl_HSLColor {
    float hue, sat, lum, a;
};

iColor          get_Color       (int color);
void            set_Color       (int color, iColor rgba);

iHSLColor       hsl_Color       (iColor rgba);
iColor          fromHsl_Color   (iHSLColor hsl);

iColor          ansi_Color      (iRangecc escapeSequence, int fallback);
const char *    escape_Color    (int color);
