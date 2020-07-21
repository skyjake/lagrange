#pragma once

#include <the_Foundation/defs.h>

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
    max_ColorId
};

#define mask_ColorId        0x0f
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

struct Impl_Color {
    uint8_t r, g, b, a;
};

iColor  get_Color   (int color);
