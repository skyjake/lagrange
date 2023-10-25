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
#   include <fribidi/fribidi.h>
#endif

iColor fgColor_AttributedRun(const iAttributedRun *d) {
    if (d->fgColor_.a) {
        /* Ensure legibility if only the foreground color is set. */
        if (!d->bgColor_.a) {
            iColor fg = d->fgColor_;
            const iHSLColor themeBg = get_HSLColor(tmBackground_ColorId);
            const float bgLuminance = luma_Color(get_Color(tmBackground_ColorId));
            /* TODO: Actually this should check if the FG is too close to the BG, and
               either darken or brighten the FG. Now it only accounts for nearly black/white
               backgrounds. */
            if (bgLuminance < 0.1f) {
                /* Background is dark. Lighten the foreground. */
                iHSLColor fgHsl = hsl_Color(fg);
                fgHsl.lum = iMax(0.2f, fgHsl.lum);
                return rgb_HSLColor(fgHsl);
            }
            if (bgLuminance > 0.4f) {
                float dim = (bgLuminance - 0.4f);
                fg.r *= 1.0f * dim;
                fg.g *= 1.0f * dim;
                fg.b *= 1.0f * dim;
            }
            if (themeBg.sat > 0.15f && themeBg.lum >= 0.5f) {
                iHSLColor fgHsl = hsl_Color(fg);
                fgHsl.hue = themeBg.hue;
                fgHsl.lum = themeBg.lum * 0.5f;
                fg = rgb_HSLColor(fgHsl);
            }
            return fg;
        }
        return d->fgColor_;
    }
    if (d->attrib.fgColorId == none_ColorId) {
        return (iColor){ 255, 255, 255, 255 };
    }
    return get_Color(d->attrib.fgColorId);
}

iColor bgColor_AttributedRun(const iAttributedRun *d) {
    if (d->bgColor_.a) {
        return d->bgColor_;
    }
    return (iColor){ 255, 255, 255, 0 };
    if (d->attrib.bgColorId == none_ColorId) {
        return (iColor){ 255, 255, 255, 0 };
    }
    return get_Color(d->attrib.bgColorId);
}

static void setFgColor_AttributedRun_(iAttributedRun *d, int colorId) {
    d->attrib.fgColorId = colorId;
    d->fgColor_.a = 0;
}

static void setBgColor_AttributedRun_(iAttributedRun *d, int colorId) {
    d->attrib.bgColorId = colorId;
    d->bgColor_.a = 0;
}

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

iDefineTypeConstructionArgs(AttributedText,
                            (iRangecc text, size_t maxLen, iAnyFont *font, int colorId,
                             int baseDir, iAnyFont *baseFont, int baseFgColorId,
                             iChar overrideChar),
                            text, maxLen, font, colorId, baseDir, baseFont, baseFgColorId,
                            overrideChar)

const char *sourcePtr_AttributedText(const iAttributedText *d, int logicalPos) {
    const int *logToSource = constData_Array(&d->logicalToSourceOffset);
    return d->source.start + logToSource[logicalPos];
}

static iRangecc sourceRange_AttributedText_(const iAttributedText *d, iRangei logical) {
    const int *logToSource = constData_Array(&d->logicalToSourceOffset);
    iRangecc range = {
        d->source.start + logToSource[logical.start],
        d->source.start + logToSource[logical.end]
    };
    iAssert(range.start <= range.end);
    return range;
}

static void finishRun_AttributedText_(iAttributedText *d, iAttributedRun *run, int endAt) {
    iAttributedRun finishedRun = *run;
    iAssert(endAt >= 0 && endAt <= size_Array(&d->logical));
    finishedRun.logical.end = endAt;
    if (!isEmpty_Range(&finishedRun.logical)) {
#if 0
        /* Colorize individual runs to see boundaries. */
        static int dbg;
        static const int dbgClr[3] = { red_ColorId, green_ColorId, blue_ColorId };
        finishedRun.attrib.colorId = dbgClr[dbg++ % 3];
#endif
        pushBack_Array(&d->runs, &finishedRun);
        run->flags.isLineBreak = iFalse;
        run->flags.script      = unspecified_Script;
    }
    run->logical.start = endAt;
}

static void prepare_AttributedText_(iAttributedText *d, int overrideBaseDir, iChar overrideChar) {
    iAssert(isEmpty_Array(&d->runs));
    size_t length = 0;
    /* Prepare the UTF-32 logical string. */ {
        for (const char *ch = d->source.start; ch < d->source.end; ) {
            iChar u32;
            int len = decodeBytes_MultibyteChar(ch, d->source.end, &u32);
            if (len <= 0) break;
            if (overrideChar) {
                u32 = overrideChar;
            }
            pushBack_Array(&d->logical, &u32);
            length++;
            if (length == d->maxLen) {
                /* TODO: Check the combining class; only count base characters here. */
                break;
            }
            /* Remember the byte offset to each character. We will need this to communicate
               back the wrapped UTF-8 ranges. */
            pushBack_Array(&d->logicalToSourceOffset, &(int){ ch - d->source.start });
            ch += len;
        }
        iBool bidiOk = iFalse;
#if defined (LAGRANGE_ENABLE_FRIBIDI)
        /* Use FriBidi to reorder the codepoints. */
        resize_Array(&d->visual, length);
        resize_Array(&d->logicalToVisual, length);
        resize_Array(&d->visualToLogical, length);
        d->bidiLevels = length ? malloc(length) : NULL;
        FriBidiParType baseDir = (FriBidiParType) FRIBIDI_TYPE_ON;
        bidiOk = fribidi_log2vis(constData_Array(&d->logical),
                                 (FriBidiStrIndex) length,
                                 &baseDir,
                                 data_Array(&d->visual),
                                 data_Array(&d->logicalToVisual),
                                 data_Array(&d->visualToLogical),
                                 (FriBidiLevel *) d->bidiLevels) > 0;
        d->isBaseRTL = (overrideBaseDir == 0 ? FRIBIDI_IS_RTL(baseDir) : (overrideBaseDir < 0));
#endif
        if (!bidiOk) {
            /* 1:1 mapping. */
            setCopy_Array(&d->visual, &d->logical);
            resize_Array(&d->logicalToVisual, length);
            for (size_t i = 0; i < length; i++) {
                set_Array(&d->logicalToVisual, i, &(int){ i });
            }
            setCopy_Array(&d->visualToLogical, &d->logicalToVisual);
            d->isBaseRTL = iFalse;
        }
    }
    /* The mapping needs to include the terminating NULL position. */ {
        pushBack_Array(&d->logicalToSourceOffset, &(int){ d->source.end - d->source.start });
        pushBack_Array(&d->logicalToVisual, &(int){ length });
        pushBack_Array(&d->visualToLogical, &(int){ length });
    }
    iAttributedRun run = {
        .logical = { 0, length },
        .attrib  = { .fgColorId = d->fgColorId, .bgColorId = none_ColorId,
                     .isBaseRTL = d->isBaseRTL },
        .font    = d->font,
    };
    const int     *logToSource = constData_Array(&d->logicalToSourceOffset);
    const iChar *  logicalText = constData_Array(&d->logical);
    iBool          isRTL       = d->isBaseRTL;
    int            numNonSpace = 0;
    iBaseFont *    attribFont  = d->font;
    for (int pos = 0; pos < length; pos++) {
        const iChar ch = logicalText[pos];
#if defined (LAGRANGE_ENABLE_FRIBIDI)
        if (d->bidiLevels) {
            const char lev = d->bidiLevels[pos];
            const iBool isNeutral = FRIBIDI_IS_NEUTRAL(lev);
            if (!isNeutral) {
                iBool rtl = FRIBIDI_IS_RTL(lev) != 0;
                if (rtl != isRTL) {
                    /* Direction changes; must end the current run. */
    //                printf("dir change at %zu: %lc U+%04X\n", pos, ch, ch);
                    finishRun_AttributedText_(d, &run, pos);
                    isRTL = rtl;
                }
            }
        }
#else
        const iBool isNeutral = iTrue;
#endif
        run.attrib.isRTL = isRTL;
        if (ch == 0x1b) { /* ANSI escape. */
            pos++;
            const char *srcPos = d->source.start + logToSource[pos];
            /* Do a regexp match in the source text. */
            iRegExpMatch m;
            init_RegExpMatch(&m);
            if (match_RegExp(current_Text()->ansiEscape, srcPos, d->source.end - srcPos, &m)) {
                finishRun_AttributedText_(d, &run, pos - 1);
                const int ansi = current_Text()->ansiFlags; /* styling enabled */
                const char mode = capturedRange_RegExpMatch(&m, 2).start[0];
                const iRangecc sequence = capturedRange_RegExpMatch(&m, 1);
                if (ansi && mode == 'm' /* Select Graphic Rendition */) {
                    for (const char *seqPos = sequence.start; seqPos < sequence.end; ) {
                        /* One sequence may have multiple codes. */
                        char *argEnd;
                        const int arg = strtoul(seqPos, &argEnd, 10);
                        /* Note: This styling is hardcoded to match `typesetOneLine_RunTypesetter_()`. */
                        if (arg == 1) {
                            if (ansi & allowFontStyle_AnsiFlag) {
                                run.attrib.bold = iTrue;
                                run.attrib.regular = iFalse;
                                run.attrib.light = iFalse;
                                if (d->baseFgColorId == tmParagraph_ColorId) {
                                    setFgColor_AttributedRun_(&run, tmFirstParagraph_ColorId);
                                }
                                attribFont = font_Text(fontWithStyle_Text(fontId_Text(d->baseFont),
                                                                           bold_FontStyle));
                            }
                        }
                        else if (arg == 2) {
                            if (ansi & allowFontStyle_AnsiFlag) {
                                run.attrib.light = iTrue;
                                run.attrib.regular = iFalse;
                                run.attrib.bold = iFalse;
                                attribFont = font_Text(fontWithStyle_Text(fontId_Text(d->baseFont),
                                                                           light_FontStyle));
                            }
                        }
                        else if (arg == 3) {
                            if (ansi & allowFontStyle_AnsiFlag) {
                                run.attrib.italic = iTrue;
                                attribFont = font_Text(fontWithStyle_Text(fontId_Text(d->baseFont),
                                                                           italic_FontStyle));
                            }
                        }
                        else if (arg == 10) {
                            if (ansi & allowFontStyle_AnsiFlag) {
                                run.attrib.regular = iTrue;
                                run.attrib.bold = iFalse;
                                run.attrib.light = iFalse;
                                run.attrib.italic = iFalse;
                                attribFont = font_Text(fontWithStyle_Text(fontId_Text(d->baseFont),
                                                                           regular_FontStyle));
                            }
                        }
                        else if (arg == 11) {
                            if (ansi & allowFontStyle_AnsiFlag) {
                                run.attrib.monospace = iTrue;
                                setFgColor_AttributedRun_(&run, tmPreformatted_ColorId);
                                attribFont = font_Text(fontWithFamily_Text(fontId_Text(d->baseFont),
                                                                            monospace_FontId));
                            }
                        }
                        else if (arg == 0) {
                            run.attrib.regular = iFalse;
                            run.attrib.bold = iFalse;
                            run.attrib.light = iFalse;
                            run.attrib.italic = iFalse;
                            run.attrib.monospace = iFalse;
                            attribFont = run.font = d->baseFont;
                            setFgColor_AttributedRun_(&run, d->baseFgColorId);
                            setBgColor_AttributedRun_(&run, none_ColorId);
                        }
                        else {
                            const char *end;
                            ansiColors_Color((iRangecc){ seqPos, sequence.end },
                                             d->baseFgColorId,
                                             none_ColorId,
                                             run.attrib.bold != 0,
                                             ansi & allowFg_AnsiFlag ? &run.fgColor_ : NULL,
                                             ansi & allowBg_AnsiFlag ? &run.bgColor_ : NULL,
                                             &end);
                            argEnd = (char *) end;
                        }
                        seqPos = argEnd;
                        if (seqPos < sequence.end) {
                            if (*seqPos == ';') {
                                seqPos++;
                            }
                            else break; /* malformed or didn't understand */
                        }
                    }
                }
                pos += length_Rangecc(capturedRange_RegExpMatch(&m, 0));
//                iAssert(logToSource[pos] == end_RegExpMatch(&m) - d->source.start);
                /* The run continues after the escape sequence. */
                run.logical.start = pos--; /* loop increments `pos` */
                continue;
            }
        }
        if (ch == '\v') {
            finishRun_AttributedText_(d, &run, pos);
            /* An internal color escape. */
            iChar esc = logicalText[++pos];
            int colorNum = none_ColorId; /* default color */
            if (esc == '\v') { /* Extended range. */
                esc = logicalText[++pos] + asciiExtended_ColorEscape;
                colorNum = esc - asciiBase_ColorEscape;
            }
            else if (esc != 0x24) { /* ASCII Cancel */
                colorNum = esc - asciiBase_ColorEscape;
            }
            run.logical.start = pos + 1;
            setFgColor_AttributedRun_(&run, colorNum >= 0 ? colorNum : d->fgColorId);
            continue;
        }
        if (ch == '\n') {
            finishRun_AttributedText_(d, &run, pos);
            /* A separate run for the newline. */
            run.logical.start = pos;
            run.flags.isLineBreak = iTrue;
            finishRun_AttributedText_(d, &run, pos + 1);
            continue;
        }
        if (isControl_Char(ch) || ch == 0x202f /* NNBSP */) {
            continue;
        }
        iAssert(run.font != NULL);
        if (ch == 0x20) {
            if (run.font->spec->flags & auxiliary_FontSpecFlag &&
                ~run.font->spec->flags & allowSpacePunct_FontSpecFlag) {
                finishRun_AttributedText_(d, &run, pos);
                run.font = d->font; /* auxilitary font space not allowed, could be wrong width */
            }
            continue;
        }
        iBaseFont *currentFont = attribFont;
        if (run.font->spec->flags & auxiliary_FontSpecFlag &&
            run.font->spec->flags & allowSpacePunct_FontSpecFlag &&
            isPunct_Char(ch)) {
            currentFont = run.font; /* keep the current font */
        }
        iBaseFont *chFont = characterFont_BaseFont(currentFont, ch);
//        const iGlyph *glyph = glyph_Font_(currentFont, ch);
        if (chFont && chFont != run.font) {
            /* A different font is being used for this character. */
            finishRun_AttributedText_(d, &run, pos);
            run.font = chFont;
#if 0
            printf("changing font to %d at pos %u (%lc) U+%04X\n", fontId_Text_(run.font), pos, (int)logicalText[pos],
                   (int)logicalText[pos]);
#endif
        }
        /* Detect the script. */
#if defined (LAGRANGE_ENABLE_FRIBIDI)
        if (fribidi_get_bidi_type(ch) == FRIBIDI_TYPE_AL) {
            run.flags.script = arabic_Script;
        }
        else
#endif
        {
            const char *scr = script_Char(ch);
//            printf("Char %08x %lc => %s\n", ch, (int) ch, scr);
            if (!iCmpStr(scr, "Bengali")) {
                run.flags.script = bengali_Script;
            }
            else if (!iCmpStr(scr, "Devanagari")) {
                run.flags.script = devanagari_Script;
            }
            else if (!iCmpStr(scr, "Han")) {
                run.flags.script = han_Script;
            }
            else if (!iCmpStr(scr, "Hiragana")) {
                run.flags.script = hiragana_Script;
            }
            else if (!iCmpStr(scr, "Katakana")) {
                run.flags.script = katakana_Script;
            }
            else if (!iCmpStr(scr, "Oriya")) {
                run.flags.script = oriya_Script;
            }
            else if (!iCmpStr(scr, "Tamil")) {
                run.flags.script = tamil_Script;
            }
        }
    }
    if (!isEmpty_Range(&run.logical)) {
        pushBack_Array(&d->runs, &run);
    }
#if 0
    const int *logToVis = constData_Array(&d->logicalToVisual);
    printf("[AttributedText] %zu runs:\n", size_Array(&d->runs));
    iConstForEach(Array, i, &d->runs) {
        const iAttributedRun *run = i.value;
        printf("  %zu %s fnt:%d log:%d...%d vis:%d...%d {%s}\n",
               index_ArrayConstIterator(&i),
               run->attrib.isRTL ? "<-" : "->",
               fontId_Text_(run->font),
               run->logical.start, run->logical.end - 1,
               logToVis[run->logical.start], logToVis[run->logical.end - 1],
               cstr_Rangecc(sourceRange_AttributedText_(d, run->logical)));
    }
#endif
}

void init_AttributedText(iAttributedText *d, iRangecc text, size_t maxLen, iAnyFont *font,
                         int colorId, int baseDir, iAnyFont *baseFont, int baseFgColorId,
                         iChar overrideChar) {
    d->source        = text;
    d->maxLen        = maxLen ? maxLen : iInvalidSize;
    d->font          = font;
    d->fgColorId     = colorId;
    d->baseFont      = baseFont;
    d->baseFgColorId = baseFgColorId;
    d->isBaseRTL     = iFalse;
    init_Array(&d->runs, sizeof(iAttributedRun));
    init_Array(&d->logical, sizeof(iChar));
    init_Array(&d->visual, sizeof(iChar));
    init_Array(&d->logicalToVisual, sizeof(int));
    init_Array(&d->visualToLogical, sizeof(int));
    init_Array(&d->logicalToSourceOffset, sizeof(int));
    d->bidiLevels = NULL;
    prepare_AttributedText_(d, baseDir, overrideChar);
}

void deinit_AttributedText(iAttributedText *d) {
    free(d->bidiLevels);
    deinit_Array(&d->logicalToSourceOffset);
    deinit_Array(&d->logicalToVisual);
    deinit_Array(&d->visualToLogical);
    deinit_Array(&d->visual);
    deinit_Array(&d->logical);
    deinit_Array(&d->runs);
}

iTextMetrics measure_WrapText(iWrapText *d, int fontId) {
    iTextMetrics tm;
    run_Font(font_Text(fontId),
             &(iRunArgs){ .mode        = measure_RunMode | runFlags_FontId(fontId),
                          .text        = d->text,
                          .wrap        = d,
                          .justify     = d->justify,
                          .layoutBound = d->justify ? d->maxWidth : 0,
                          .metrics_out = &tm });
    return tm;
}

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
