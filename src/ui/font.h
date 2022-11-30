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

#pragma once

#include "color.h"
#include "fontpack.h"

#include <the_Foundation/rect.h>

iDeclareType(BaseFont)

/* Content sizes: regular (1x) -> medium (1.2x) -> big (1.33x) -> large (1.67x) -> huge (2x) */

#define FONT_ID(name, style, size)    ((name) + ((style) * max_FontSize) + (size))

enum iFontId {
    default_FontId           = 0,                     /* default is always the first font */
    monospace_FontId         = maxVariants_Fonts,     /* 2nd font is always the monospace font */
    documentHeading_FontId   = maxVariants_Fonts * 2, /* heading font */
    documentBody_FontId      = maxVariants_Fonts * 3, /* body font */
    documentMonospace_FontId = maxVariants_Fonts * 4,
    auxiliary_FontId         = maxVariants_Fonts * 5, /* the first auxiliary font (e.g., symbols) */

    /* Meta: */
    mask_FontId               = 0x0000ffff, /* font IDs are 16-bit; see GmRun's packing */
    alwaysVariableFlag_FontId = 0x00010000,

    /* UI fonts: */
    uiLabelTiny_FontId        = FONT_ID(default_FontId,   semiBold_FontStyle, uiTiny_FontSize),
    uiLabelSmall_FontId       = FONT_ID(default_FontId,   regular_FontStyle,  uiSmall_FontSize),
    uiLabel_FontId            = FONT_ID(default_FontId,   regular_FontStyle,  uiNormal_FontSize),
    uiLabelMedium_FontId      = FONT_ID(default_FontId,   regular_FontStyle,  uiMedium_FontSize),
    uiLabelMediumBold_FontId  = FONT_ID(default_FontId,   bold_FontStyle,     uiMedium_FontSize),
    uiLabelBig_FontId         = FONT_ID(default_FontId,   regular_FontStyle,  uiBig_FontSize),
    uiLabelBold_FontId        = FONT_ID(default_FontId,   bold_FontStyle,     uiNormal_FontSize),
    uiLabelBigBold_FontId     = FONT_ID(default_FontId,   bold_FontStyle,     uiBig_FontSize),
    uiLabelLarge_FontId       = FONT_ID(default_FontId,   regular_FontStyle,  uiLarge_FontSize),
    uiLabelLargeBold_FontId   = FONT_ID(default_FontId,   bold_FontStyle,     uiLarge_FontSize),
    uiLabelSymbols_FontId     = FONT_ID(auxiliary_FontId, regular_FontStyle,  uiNormal_FontSize),
    uiShortcuts_FontId        = FONT_ID(default_FontId,   regular_FontStyle,  uiNormal_FontSize),
    uiInput_FontId            = FONT_ID(default_FontId,   regular_FontStyle,  uiMedium_FontSize),
    uiContent_FontId          = FONT_ID(default_FontId,   regular_FontStyle,  uiMedium_FontSize),
    uiContentBold_FontId      = FONT_ID(default_FontId,   bold_FontStyle,     uiMedium_FontSize),
    uiContentSymbols_FontId   = FONT_ID(auxiliary_FontId, regular_FontStyle,  uiMedium_FontSize),
    
    /* Document fonts: */     
    paragraph_FontId          = FONT_ID(documentBody_FontId,      regular_FontStyle,  contentRegular_FontSize),
    bold_FontId               = FONT_ID(documentBody_FontId,      semiBold_FontStyle, contentRegular_FontSize),
    firstParagraph_FontId     = FONT_ID(documentBody_FontId,      regular_FontStyle,  contentMedium_FontSize),
    preformatted_FontId       = FONT_ID(monospace_FontId,         regular_FontStyle,  contentSmall_FontSize),
    preformattedSmall_FontId  = FONT_ID(monospace_FontId,         regular_FontStyle,  contentTiny_FontSize),
    quote_FontId              = FONT_ID(documentBody_FontId,      italic_FontStyle,   contentRegular_FontSize),
    heading1_FontId           = FONT_ID(documentHeading_FontId,   bold_FontStyle,     contentHuge_FontSize),
    heading2_FontId           = FONT_ID(documentHeading_FontId,   regular_FontStyle,  contentLarge_FontSize),
    heading3_FontId           = FONT_ID(documentHeading_FontId,   bold_FontStyle,     contentBig_FontSize),
    banner_FontId             = FONT_ID(documentHeading_FontId,   light_FontStyle,    contentLarge_FontSize),
    monospaceParagraph_FontId = FONT_ID(documentMonospace_FontId, regular_FontStyle,  contentRegular_FontSize),
    monospaceBold_FontId      = FONT_ID(documentMonospace_FontId, semiBold_FontStyle, contentRegular_FontSize),
    plainText_FontId          = FONT_ID(documentMonospace_FontId, regular_FontStyle,  contentRegular_FontSize),
};

iLocalDef enum iFontStyle style_FontId(enum iFontId id) {
    return (id / max_FontSize) % max_FontStyle;
}

iLocalDef enum iFontSize size_FontId(enum iFontId id) {
    return id % max_FontSize;
}

iLocalDef iBool isControl_Char(iChar c) {
    return isDefaultIgnorable_Char(c) || isVariationSelector_Char(c) || isFitzpatrickType_Char(c);
}

struct Impl_BaseFont {
    const iFontSpec *spec;
    const iFontFile *file;
    int              height;
};

typedef void iAnyFont;

iLocalDef iBool isMonospaced_Font(const iAnyFont *d) {
    return (((const iBaseFont *) d)->spec->flags & monospace_FontSpecFlag) != 0;
}

iBaseFont *     font_Text       (enum iFontId id);
enum iFontId    fontId_Text     (const iAnyFont *font);

iBaseFont *     characterFont_BaseFont  (iBaseFont *, iChar ch);

/*----------------------------------------------------------------------------------------------*/

iDeclareType(TextAttrib)
iDeclareType(AttributedRun)
iDeclareType(AttributedText)

/* Initial attributes at the start of a text string. These may be modified by control
   sequences inside a text run. */
struct Impl_TextAttrib {
    int16_t fgColorId;
    int16_t bgColorId;
    struct {
        uint16_t regular   : 1;
        uint16_t bold      : 1;
        uint16_t light     : 1;
        uint16_t italic    : 1;
        uint16_t monospace : 1;
        uint16_t isBaseRTL : 1;
        uint16_t isRTL     : 1;
    }; 
};

enum iScript {
    unspecified_Script,
    arabic_Script,
    bengali_Script,
    devanagari_Script,
    han_Script,
    hiragana_Script,
    katakana_Script,
    oriya_Script,
    tamil_Script,
    max_Script
};

iLocalDef iBool isCJK_Script(enum iScript d) {
    return d == han_Script || d == hiragana_Script || d == katakana_Script;
}

struct Impl_AttributedRun {
    iRangei     logical; /* UTF-32 codepoint indices in the logical-order text */
    iTextAttrib attrib;
    iBaseFont  *font;
    iColor      fgColor_; /* any RGB color; A > 0 */
    iColor      bgColor_; /* any RGB color; A > 0 */
    struct {
        uint8_t isLineBreak : 1;
        uint8_t script      : 7; /* if script detected */
    } flags;
};

const char *sourcePtr_AttributedText(const iAttributedText *, int logicalPos);
iColor      fgColor_AttributedRun   (const iAttributedRun *);
iColor      bgColor_AttributedRun   (const iAttributedRun *);

iDeclareTypeConstructionArgs(AttributedText, iRangecc text, size_t maxLen, iAnyFont *font,
                             int colorId, int baseDir, iAnyFont *baseFont, int baseFgColorId,
                             iChar overrideChar)

struct Impl_AttributedText {
    iRangecc source; /* original source text */
    size_t   maxLen;
    iBaseFont *font;
    int      fgColorId;
    iBaseFont *baseFont;
    int      baseFgColorId;
    iBool    isBaseRTL;
    iArray   runs;
    iArray   logical;         /* UTF-32 text in logical order (mixed directions; matches source) */
    iArray   visual;          /* UTF-32 text in visual order (LTR) */
    iArray   logicalToVisual; /* map visual index to logical index */
    iArray   visualToLogical;
    iArray   logicalToSourceOffset; /* map logical character to an UTF-8 offset in the source text */
    char *   bidiLevels;
};

/*----------------------------------------------------------------------------------------------*/

iDeclareType(WrapText)
iDeclareType(TextMetrics)

enum iWrapTextMode {
    anyCharacter_WrapTextMode,
    word_WrapTextMode,
};

struct Impl_WrapText {
    /* arguments */
    iRangecc    text;
    int         maxWidth;
    size_t      maxLines;     /* 0: unlimited */
    enum iWrapTextMode mode;
    iBool       justify;
    iBool     (*wrapFunc)(iWrapText *, iRangecc wrappedText, iTextAttrib attrib, int origin,
                          int advance);
    void *      context;
    iChar       overrideChar; /* use this for all characters instead of the real ones */
    int         baseDir;      /* set to +1 for LTR, -1 for RTL */
    iInt2       hitPoint;     /* sets hitChar_out */
    const char *hitChar;      /* sets hitAdvance_out */
    /* output */
    const char *hitChar_out;
    iInt2       hitAdvance_out;
    float       hitGlyphNormX_out; /* normalized X inside the glyph */
    /* internal */
    iRangecc    wrapRange_;
};

struct Impl_TextMetrics {
    iRect bounds;  /* logical bounds: multiples of line height, horiz. advance */
    iInt2 advance; /* cursor offset */
};

iLocalDef int maxWidth_TextMetrics(const iTextMetrics d) {
    return iMax(width_Rect(d.bounds), d.advance.x);
}

iTextMetrics    measure_WrapText    (iWrapText *, int fontId);
iTextMetrics    draw_WrapText       (iWrapText *, int fontId, iInt2 pos, int color);

iBool           notify_WrapText     (iWrapText *, const char *ending, iTextAttrib attrib, int origin, int advance);

/*----------------------------------------------------------------------------------------------*/

iDeclareType(RunArgs)

enum iRunMode {
    measure_RunMode                 = 0,
    draw_RunMode                    = 1,
    modeMask_RunMode                = 0x00ff,
    flagsMask_RunMode               = 0xff00,
    visualFlag_RunMode              = iBit(10), /* actual visible bounding box of the glyph,
                                                   e.g., for icons */
    permanentColorFlag_RunMode      = iBit(11),
    alwaysVariableWidthFlag_RunMode = iBit(12),
    fillBackground_RunMode          = iBit(13),
    underline_RunMode               = iBit(14),
};

int     runFlags_FontId (enum iFontId fontId);

struct Impl_RunArgs {
    enum iRunMode mode;
    iRangecc      text;
    size_t        maxLen; /* max characters to process */
    iInt2         pos;
    iWrapText *   wrap;
    int           layoutBound;
    iBool         justify;    
    int           color;
    int           baseDir;
    iTextMetrics *metrics_out;
};

void    run_Font        (iBaseFont *d, const iRunArgs *args);
