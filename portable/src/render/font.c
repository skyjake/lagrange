/* Copyright 2020-2022 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "font.h"
#include "text.h"

#include <the_Foundation/regexp.h>

#if defined (LAGRANGE_ENABLE_HARFBUZZ)
#   include <hb.h>
#endif

#if defined (LAGRANGE_ENABLE_FRIBIDI)
#   include <fribidi.h>
#endif

int fontWithSize_Text(int font, enum iFontSize sizeId) {
    const int familyId = (font / maxVariants_Fonts) * maxVariants_Fonts;
    const int styleId  = (font / max_FontSize) % max_FontStyle;
    return FONT_ID(familyId, styleId, sizeId);
}

int fontWithStyle_Text(int font, enum iFontStyle styleId) {
    const int familyId = (font / maxVariants_Fonts) * maxVariants_Fonts;
    const int sizeId   = font % max_FontSize;
    return FONT_ID(familyId, styleId, sizeId);
}

int fontWithFamily_Text(int font, enum iFontId familyId) {
    const int styleId = (font / max_FontSize) % max_FontStyle;
    const int sizeId  = font % max_FontSize;
    return FONT_ID(familyId, styleId, sizeId);
}

/*----------------------------------------------------------------------------------------------*/

iTextMetrics draw_WrapText(iWrapText *d, int fontId, iInt2 pos, int color) {
    iTextMetrics tm;
#if !defined (LAGRANGE_ENABLE_HARFBUZZ)
    /* In simple mode, each line must be wrapped first so we can break at the right points
       and do wrap notifications before drawing. */
    iRangecc text = d->text;
    iZap(tm);
    d->wrapRange_ = (iRangecc){ d->text.start, d->text.start };
    const iInt2 orig = pos;
    while (!isEmpty_Range(&text)) {
        const char *endPos;
        const int width = d->mode == word_WrapTextMode
                              ? tryAdvance_Text(fontId, text, d->maxWidth, &endPos).x
                              : tryAdvanceNoWrap_Text(fontId, text, d->maxWidth, &endPos).x;
        if (endPos == text.start) {
            break; /* too tight for even a single character */
        }
        notify_WrapText(d, endPos, (iTextAttrib){ .fgColorId = color }, 0, width);
        drawRange_Text(fontId, pos, color, (iRangecc){ text.start, endPos });
        text.start = endPos;
        pos.y += lineHeight_Text(fontId);
        tm.bounds.size.x = iMax(tm.bounds.size.x, width);
        tm.bounds.size.y = pos.y - orig.y;
    }
    tm.advance = sub_I2(pos, orig);
#else
    run_Font(font_Text(fontId),
             &(iRunArgs){
                 .mode = draw_RunMode | runFlags_FontId(fontId) |
                         (color & permanent_ColorId ? permanentColorFlag_RunMode : 0) |
                         (color & fillBackground_ColorId ? fillBackground_RunMode : 0),
                 .text = d->text,
                 .pos = pos,
                 .wrap = d,
                 .justify = d->justify,
                 .layoutBound = d->justify ? d->maxWidth : 0,
                 .color = color & mask_ColorId,
                 .metrics_out = &tm,
             });
#endif
    return tm;
}

iBool notify_WrapText(iWrapText *d, const char *ending, iTextAttrib attrib, int origin,
                      int advance) {
    if (d && d->wrapFunc && d->wrapRange_.start) {
        /* `wrapRange_` uses logical indices. */
        const char *end   = ending ? ending : d->wrapRange_.end;
        iRangecc    range = { d->wrapRange_.start, end };
        iAssert(range.start <= range.end);
        const iBool result = d->wrapFunc(d, range, attrib, origin, advance);
        if (result) {
            d->wrapRange_.start = end;
        }
        else {
            d->wrapRange_ = iNullRange;
        }
        return result;
    }
    return iTrue;
}
