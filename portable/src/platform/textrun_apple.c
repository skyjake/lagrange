/* Copyright 2026 Jaakko Keränen <jaakko.keranen@iki.fi>

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

/* CTTypesetter run construction with inline `\v` and ANSI escape parsing.
   Builds a CFMutableAttributedString with per-segment CoreText font and color
   attributes, enabling native CoreText styling. */

#include "text_apple.h"
#include "render/text.h"
#include "render/font.h"
#include "color.h"

#include <lagrange/defs.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/string.h>

#include <stdlib.h>
#include <string.h>

iDeclareType(TextSegment)

/* One attributed run between two escape sequences. */
struct Impl_TextSegment {
    CFIndex startUtf16;
    CFIndex endUtf16;
    int     fontId;
    int     fgColorId;
    iColor  fgColor;   /* .a > 0: use this RGB directly; .a == 0: use fgColorId */
    iColor  bgColor;   /* .a > 0: fill background with this RGB */
};

CFStringRef lagBgKey_ = NULL; /* initialized in new_Text(); declared extern in text_apple.h */

static CGColorRef cgColor_(int colorId) {
    const iColor    c        = get_Color(colorId);
    const CGFloat   comps[4] = { c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0 };
    CGColorSpaceRef cs       = CGColorSpaceCreateDeviceRGB();
    CGColorRef      ref      = CGColorCreate(cs, comps);
    CGColorSpaceRelease(cs);
    return ref; /* caller CFReleases */
}

static CGColorRef cgColorFromColor_(const iColor c) {
    const CGFloat   comps[4] = { c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0 };
    CGColorSpaceRef cs       = CGColorSpaceCreateDeviceRGB();
    CGColorRef      ref      = CGColorCreate(cs, comps);
    CGColorSpaceRelease(cs);
    return ref; /* caller CFReleases */
}

/*----------------------------------------------------------------------------------------------*/

iDefineTypeConstructionArgs(AppleTextRun,
    (const char *rawText, size_t rawLen, int fontId, int colorId, iAppleText *tx),
    rawText, rawLen, fontId, colorId, tx)

static iBool parseEscapes_AppleTextRun_(
    iAppleText                   *tx,
    const char                   *rawText,
    size_t                        rawLen,
    int                           baseFontId,
    int                           baseColorId,
    char                        **clean_out,
    size_t                       *cleanLen_out,
    size_t                      **utf16ToSrc_out,
    CFIndex                      *utf16Len_out,
    CFMutableAttributedStringRef *mAttrStr_out)
{
    *clean_out      = NULL;
    *cleanLen_out   = 0;
    *utf16ToSrc_out = NULL;
    *utf16Len_out   = 0;
    *mAttrStr_out   = NULL;

    /* Upper-bound allocations: each raw byte can produce at most 4 UTF-8 bytes in the
       clean buffer (won't happen in practice, but keeps the logic simple), and at most
       one UTF-16 unit per raw byte. */
    char     *cleanBuf = malloc(rawLen * 4 + 1);
    size_t   *utf16Map = malloc((rawLen * 4 + 2) * sizeof(size_t));
    iTextSegment *segments = malloc((rawLen + 1) * sizeof(iTextSegment));
    if (!cleanBuf || !utf16Map || !segments) {
        free(cleanBuf);
        free(utf16Map);
        free(segments);
        return iFalse;
    }

    size_t  cleanLen  = 0;
    CFIndex utf16Idx  = 0;
    size_t  segCount  = 0;
    CFIndex segStartUtf16 = 0;

    /* Current styling state. */
    int    curFontId    = baseFontId;
    int    curFgColorId = baseColorId;
    iColor curFgColor   = { 0, 0, 0, 0 }; /* .a == 0: use curFgColorId */
    iColor curBgColor   = { 0, 0, 0, 0 }; /* .a == 0: no background */

    const char *p   = rawText;
    const char *end = rawText + rawLen;

    while (p < end) {
        if ((uint8_t) *p == 0x0B) { /* \v color escape */
            /* Flush current segment before changing state. */
            if (utf16Idx > segStartUtf16) {
                segments[segCount++] = (iTextSegment) { segStartUtf16, utf16Idx,   curFontId,
                                                        curFgColorId,  curFgColor, curBgColor };
            }
            p++; /* skip the leading \v byte */
            if (p >= end) break;
            uint8_t esc = (uint8_t) *p++;
            if (esc == 0x0B) { /* double \v — extended color range */
                if (p < end) {
                    uint8_t nextByte = (uint8_t) *p++;
                    curFgColorId =
                        (int) nextByte + asciiExtended_ColorEscape - asciiBase_ColorEscape;
                }
                curFgColor.a = 0;
            }
            else if (esc == 0x24) { /* ASCII '$' — restore default colors */
                curFgColorId = baseColorId;
                curFgColor.a = 0;
                curBgColor.a = 0;
            }
            else { /* normal color index */
                curFgColorId = (int) esc - asciiBase_ColorEscape;
                curFgColor.a = 0;
            }
            segStartUtf16 = utf16Idx;
            continue;
        }

        if ((uint8_t) *p == 0x1b) {         /* ANSI escape */
            const char  *ansiStart = p + 1; /* skip past \x1b; pattern excludes it */
            iRegExpMatch m;
            init_RegExpMatch(&m);
            if (match_RegExp(
                    current_Text()->ansiEscape, ansiStart, (size_t) (end - ansiStart), &m)) {
                /* Flush current segment before changing state. */
                if (utf16Idx > segStartUtf16) {
                    segments[segCount++] = (iTextSegment) { segStartUtf16, utf16Idx,   curFontId,
                                                            curFgColorId,  curFgColor, curBgColor };
                }
                const int      ansi     = current_Text()->ansiFlags;
                const char     mode     = capturedRange_RegExpMatch(&m, 2).start[0];
                const iRangecc sequence = capturedRange_RegExpMatch(&m, 1);
                if (ansi && mode == 'm') { /* Select Graphic Rendition */
                    for (const char *seqPos = sequence.start; seqPos < sequence.end;) {
                        char     *argEnd;
                        const int arg = (int) strtoul(seqPos, &argEnd, 10);
                        if (arg == 0) { /* reset all */
                            curFontId    = baseFontId;
                            curFgColorId = baseColorId;
                            curFgColor.a = 0;
                            curBgColor.a = 0;
                        }
                        else if (arg == 1 && (ansi & allowFontStyle_AnsiFlag)) {
                            curFontId = fontWithStyle_Text(baseFontId, bold_FontStyle);
                        }
                        else if (arg == 2 && (ansi & allowFontStyle_AnsiFlag)) {
                            curFontId = fontWithStyle_Text(baseFontId, light_FontStyle);
                        }
                        else if (arg == 3 && (ansi & allowFontStyle_AnsiFlag)) {
                            curFontId = fontWithStyle_Text(baseFontId, italic_FontStyle);
                        }
                        else if (arg == 10 && (ansi & allowFontStyle_AnsiFlag)) {
                            curFontId = fontWithStyle_Text(baseFontId, regular_FontStyle);
                        }
                        else if (arg == 11 && (ansi & allowFontStyle_AnsiFlag)) {
                            curFontId = fontWithFamily_Text(baseFontId, monospace_FontId);
                        }
                        else {
                            /* Color code (30+, 38;2/5, etc.). */
                            iColor      fgColor = { 0, 0, 0, 0 };
                            iColor      bgColor = { 0, 0, 0, 0 };
                            const char *seqEnd  = seqPos;
                            ansiColors_Color((iRangecc) { seqPos, sequence.end },
                                             baseColorId,
                                             none_ColorId,
                                             iFalse,
                                             (ansi & allowFg_AnsiFlag) ? &fgColor : NULL,
                                             (ansi & allowBg_AnsiFlag) ? &bgColor : NULL,
                                             &seqEnd);
                            argEnd = (char *) seqEnd;
                            if (fgColor.a > 0) {
                                curFgColor = fgColor;
                            }
                            if (bgColor.a > 0) {
                                curBgColor = bgColor;
                            }
                        }
                        seqPos = argEnd;
                        if (seqPos < sequence.end) {
                            if (*seqPos == ';')
                                seqPos++;
                            else
                                break; /* malformed or unrecognized */
                        }
                    }
                }
                p             = ansiStart + length_Rangecc(capturedRange_RegExpMatch(&m, 0));
                segStartUtf16 = utf16Idx;
                continue;
            }
            /* Not a recognized ANSI escape: fall through and treat \x1b as a regular char. */
        }

        /* Regular character: decode UTF-8 codepoint and copy to clean buffer. */
        iChar ch     = 0;
        int   nbytes = decodeBytes_MultibyteChar(p, end, &ch);
        if (nbytes <= 0) {
            p++; /* skip unrecognized byte */
            continue;
        }
        /* Record source offset for each UTF-16 unit produced by this codepoint. */
        utf16Map[utf16Idx++] = (size_t)(p - rawText);
        if (ch >= 0x10000) {
            /* Supplementary character occupies two UTF-16 units (surrogate pair);
               both map to the same source offset. */
            utf16Map[utf16Idx++] = (size_t)(p - rawText);
        }
        /* Append raw UTF-8 bytes to clean buffer. */
        memcpy(cleanBuf + cleanLen, p, (size_t) nbytes);
        cleanLen += (size_t) nbytes;
        p += nbytes;
    }

    /* Flush the final segment. */
    if (utf16Idx > segStartUtf16) {
        segments[segCount++] = (iTextSegment) { segStartUtf16, utf16Idx,   curFontId,
                                                curFgColorId,  curFgColor, curBgColor };
    }
    utf16Map[utf16Idx] = rawLen; /* sentinel: past-end offset */
    cleanBuf[cleanLen] = '\0';

    /* Build a CFString from the clean UTF-8 buffer. */
    CFStringRef cfStr = CFStringCreateWithBytes(kCFAllocatorDefault,
                                                (const UInt8 *) cleanBuf,
                                                (CFIndex) cleanLen,
                                                kCFStringEncodingUTF8,
                                                false);
    if (!cfStr) {
        free(cleanBuf);
        free(utf16Map);
        free(segments);
        return iFalse;
    }

    /* Create a mutable attributed string from the clean text. */
    CFMutableAttributedStringRef mAttrStr = CFAttributedStringCreateMutable(kCFAllocatorDefault, 0);
    CFAttributedStringReplaceString(mAttrStr, CFRangeMake(0, 0), cfStr);
    CFRelease(cfStr);

    /* Apply per-segment font, foreground color, and optional background color attributes. */
    CFArrayRef cascadeList = cascadeList_AppleText_(tx);
    for (size_t si = 0; si < segCount; si++) {
        const iTextSegment *seg   = &segments[si];
        CFRange             range = CFRangeMake(seg->startUtf16, seg->endUtf16 - seg->startUtf16);
        if (range.length <= 0) continue;

        /* Resolve CTFont for this segment. */
        iAppleFont *af = appleFont_AppleText_(tx, seg->fontId);
        ensureCtFont_AppleFont_(af, cascadeList);
        if (!af->ctFont) continue;

        /* Build fg CGColor. */
        CGColorRef fgCg =
            (seg->fgColor.a > 0) ? cgColorFromColor_(seg->fgColor) : cgColor_(seg->fgColorId);

        /* Populate the attributes dict. */
        CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                                                 3,
                                                                 &kCFTypeDictionaryKeyCallBacks,
                                                                 &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(attrs, kCTFontAttributeName, af->ctFont);
        CFDictionarySetValue(attrs, kCTForegroundColorAttributeName, fgCg);
        if (seg->bgColor.a > 0) {
            CGColorRef bgCg = cgColorFromColor_(seg->bgColor);
            CFDictionarySetValue(attrs, lagBgKey_, bgCg);
            CFRelease(bgCg);
        }
        CFAttributedStringSetAttributes(mAttrStr, range, attrs, true);
        CFRelease(attrs);
        CFRelease(fgCg);
    }

    free(segments);
    *clean_out      = cleanBuf;
    *cleanLen_out   = cleanLen;
    *utf16ToSrc_out = utf16Map;
    *utf16Len_out   = utf16Idx;
    *mAttrStr_out   = mAttrStr;
    return iTrue;
}

void init_AppleTextRun(iAppleTextRun *d, const char *rawText, size_t rawLen,
                       int fontId, int colorId, iAppleText *tx) {
    iZap(*d);
    d->hash       = iCrc32(rawText, rawLen)
                    ^ (uint32_t)((unsigned)fontId << 8)
                    ^ (uint32_t)((unsigned)(colorId & mask_ColorId) << 16);
    d->rawTextLen = rawLen;
    d->fontId     = fontId;
    d->colorId    = colorId;
    CFMutableAttributedStringRef mAttrStr = NULL;
    if (!parseEscapes_AppleTextRun_(tx, rawText, rawLen, fontId, colorId,
                                    &d->text, &d->textLen,
                                    &d->utf16ToSrc, &d->utf16Len, &mAttrStr)) {
        return;
    }
    d->typesetter = CTTypesetterCreateWithAttributedString(mAttrStr);
    CFRelease(mAttrStr);
    if (!d->typesetter) deinit_AppleTextRun(d);
}

void deinit_AppleTextRun(iAppleTextRun *d) {
    if (d->typesetter) CFRelease(d->typesetter);
    if (d->utf16ToSrc) free(d->utf16ToSrc);
    if (d->text)       free(d->text);
}
