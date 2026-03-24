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

/* Text rendering backend using Core Text for Apple platforms (macOS, iOS).
   Provides functionality equivalent to HarfBuzz + FriBidi + stb_truetype.
   Core Text handles shaping, Unicode BiDi, and per-character font fallback
   via CTFont cascade lists built from the app's fontpack priority order. */

#include "render/text.h"
#include "render/font.h"
#include "render/paint.h"
#include "fontpack.h"
#include "color.h"
#include "app.h"
#include "ui/metrics.h"
#include "ui/window.h"
#include <lagrange/prefs.h>
#include <lagrange/defs.h>
#include <lagrange/resources.h>

#include <the_Foundation/array.h>
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/path.h>
#include <the_Foundation/math.h>
#include <the_Foundation/string.h>
#include <the_Foundation/block.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/thread.h>
#include <the_Foundation/time.h>

#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>
#include <SDL_render.h>

#include "apple_text.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void init_AppleFont_(iAppleFont *d, const iFontSpec *spec, const iFontFile *file,
                            enum iFontSize sizeId, float baseHeight) {
    const int   scaleType   = scaleType_FontSpec(sizeId);
    const float heightScale = spec->heightScale[scaleType];
    const float glyphScale  = spec->glyphScale[scaleType];
    d->font.spec            = spec;
    d->font.file            = file;
    d->font.height          = (int) (baseHeight * heightScale);
    d->ctFont               = NULL;
    d->pointSize            = 0.0f;
    if (!file) {
        /* No font at all; baseline is a rough estimate. */
        d->font.baseline = d->font.height * 3 / 4;
        return;
    }
    /* Compute the target point size and estimate the baseline from design metrics.
       For system fonts loaded from the font cache, file->data is NULL here and will be
       created lazily by ensureCtFont_AppleFont_; metrics are already set from the cache. */
    const int totalEm = file->ascent - file->descent;
    d->pointSize = (totalEm > 0 && file->unitsPerEm > 0)
                       ? d->font.height * glyphScale * (float) file->unitsPerEm / (float) totalEm
                       : (float) d->font.height;
    if (d->pointSize < 1.0f) d->pointSize = 1.0f;
    /* Baseline ≈ ascent fraction of height, derived from the same design metrics. */
    d->font.baseline = (totalEm > 0) ? (int) roundf((float) d->font.height * glyphScale *
                                                    (float) file->ascent / (float) totalEm)
                                     : d->font.height * 3 / 4;
    if (d->font.baseline >= d->font.height) {
        d->font.baseline = d->font.height - 1;
    }
    d->vertOffset = (int) roundf(d->font.height * (1.0f - glyphScale) / 2.0f *
                                 spec->vertOffsetScale[scaleType]);
}

void ensureCtFont_AppleFont_(iAppleFont *d, CFArrayRef cascadeList) {
    if (d->ctFont || d->pointSize <= 0.0f) return;
    if (!d->font.file) return;
    if (!d->font.file->data) {
        /* Font was loaded from cache: create the reference CTFont lazily now. */
        allocData_FontFile((iFontFile *) d->font.file);
        if (!d->font.file->data) {
            d->pointSize = 0.0f; /* permanently unavailable; skip future attempts */
            return;
        }
    }
    CTFontRef ref = (CTFontRef) (uintptr_t) d->font.file->data;
    if (cascadeList) {
        CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                                                 1,
                                                                 &kCFTypeDictionaryKeyCallBacks,
                                                                 &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(attrs, kCTFontCascadeListAttribute, cascadeList);
        CTFontDescriptorRef desc = CTFontDescriptorCreateWithAttributes(attrs);
        CFRelease(attrs);
        d->ctFont = CTFontCreateCopyWithAttributes(ref, (CGFloat) d->pointSize, NULL, desc);
        CFRelease(desc);
    }
    else {
        d->ctFont = CTFontCreateCopyWithAttributes(ref, (CGFloat) d->pointSize, NULL, NULL);
    }
}

static void deinit_AppleFont_(iAppleFont *d) {
    if (d->ctFont) {
        CFRelease(d->ctFont);
        d->ctFont = NULL;
    }
}

/*----------------------------------------------------------------------------------------------*/

#define maxRunCache_AppleText_ 16

struct Impl_AppleText {
    iObject        object;
    iText          base;               /* public iText instance */
    iArray         fonts;              /* initialized fonts ready for use */
    int            overrideFontId;     /* always checked first for glyphs */
    iFontSpec      monoFallback;       /* copy of Iosevka spec, low priority fallback */
    iArray         fontPriorityOrder;  /* PrioMapItem[] (descending priority) */
    CFArrayRef     cascadeList;        /* retained; shared by all lazily-created CTFonts */
    float          opacity;

    /* Run cache: stores CTTypesetters and UTF16-to-source mappings for recently shaped text.
       A run is keyed by (CRC of raw text, fontId, colorId). */
    iAppleTextRun *runCache[maxRunCache_AppleText_];
    uint32_t       runCacheSerial;
};

iLocalDef iAppleText *appleText_(iText *t) {
    /* `t is a member of the class intance, so back up to find the instance itself. */
    return (iAppleText *) ((char *) t - offsetof(iAppleText, base));
}

iLocalDef iAppleText *current_AppleText_(void) {
    return appleText_(current_Text());
}

iAppleFont *appleFont_AppleText_(iAppleText *d, int id) {
    return at_Array(&d->fonts, id & mask_FontId);
}

CFArrayRef cascadeList_AppleText_(const iAppleText *d) {
    return d->cascadeList;
}

iLocalDef iAppleFont *appleFont_Text_(int id) {
    return appleFont_AppleText_(current_AppleText_(), id);
}

iBaseFont *font_Text(enum iFontId id) {
    return (iBaseFont *) appleFont_Text_(id);
}

enum iFontId fontId_Text(const void *font) {
    const iAppleFont *af = font;
    return (enum iFontId)(af - (const iAppleFont *) constData_Array(&current_AppleText_()->fonts));
}

iBaseFont *characterFont_BaseFont(iBaseFont *d, iChar ch) {
    /* Core Text handles per-character fallback internally via the cascade list.
       Return d unchanged; the cascade list ensures correct glyph selection. */
    iUnused(ch);
    return d;
}

/*----------------------------------------------------------------------------------------------*/

static void clearRunCache_AppleText_(iAppleText *);

static CFArrayRef buildCascadeList_AppleText_(iAppleText *d) {
    /* Build a cascade list (CFArrayRef of CTFontDescriptors) from user-installed FontPack fonts
       in priority order. System-enumerated fonts ("apple-*"" IDs) are intentionally excluded.
       The colorEmoji preference controls whether Apple Color Emoji leads the cascade
       or is filtered out of the system cascade (disable/B&W mode). */
    CFMutableArrayRef list = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    if (get_Prefs()->colorEmoji) {
        CTFontRef ref = CTFontCreateWithName(CFSTR("AppleColorEmoji"), 0.0, NULL);
        if (ref) {
            CTFontDescriptorRef desc = CTFontCopyFontDescriptor(ref);
            CFArrayAppendValue(list, desc);
            CFRelease(desc);
            CFRelease(ref);
        }
    }
    iConstForEach(Array, it, &d->fontPriorityOrder) {
        const iPrioMapItem *item = it.value;
        const iAppleFont   *af =
            appleFont_AppleText_(d, FONT_ID(item->fontIndex, regular_FontStyle, uiNormal_FontSize));
        if (!af || !af->font.spec || !af->font.file || !af->font.file->data) continue;
        /* Skip fonts that are not intended to participate in the cascade fallback chain. */
        if (af->font.spec->flags & ignoreAsFallback_FontSpecFlag) continue;
        CTFontRef           ref  = (CTFontRef) (uintptr_t) af->font.file->data;
        CTFontDescriptorRef desc = CTFontCopyFontDescriptor(ref);
        CFArrayAppendValue(list, desc);
        CFRelease(desc);
    }
    /* Append the full system cascade for Unicode coverage, except for Color Emoji since
       that was added manually if desired. */
    CTFontRef sysFont = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 0.0, NULL);
    if (sysFont) {
        CFArrayRef sysCascade = CTFontCopyDefaultCascadeListForLanguages(sysFont, NULL);
        if (sysCascade) {
            for (CFIndex i = 0; i < CFArrayGetCount(sysCascade); i++) {
                CTFontDescriptorRef fd = CFArrayGetValueAtIndex(sysCascade, i);
                CFStringRef psName     = CTFontDescriptorCopyAttribute(fd, kCTFontNameAttribute);
                const iBool isColorEmoji =
                    psName &&
                    (CFStringCompare(psName, CFSTR("AppleColorEmoji"), 0) == kCFCompareEqualTo);
                if (psName) CFRelease(psName);
                if (isColorEmoji) continue;
                CFArrayAppendValue(list, fd);
            }
            CFRelease(sysCascade);
        }
        CFRelease(sysFont);
    }
    return list;
}

static void applyCascadeList_AppleText_(iAppleText *d) {
    /* Rebuild the cascade list and discard all existing CTFonts so they are recreated lazily
       with the updated cascade on next use. */
    if (d->cascadeList) {
        CFRelease(d->cascadeList);
    }
    d->cascadeList = buildCascadeList_AppleText_(d);
    iForEach(Array, it, &d->fonts) {
        iAppleFont *af = it.value;
        if (af->ctFont) {
            CFRelease(af->ctFont);
            af->ctFont = NULL;
        }
    }
    clearRunCache_AppleText_(d);
}

static void setupFontVariants_AppleText_(iAppleText *d, const iFontSpec *spec, int baseId,
                                         float uiSize, float textSize) {
    if (!spec) return;
    if (spec->flags & override_FontSpecFlag && d->overrideFontId < 0) {
        d->overrideFontId = baseId;
    }
    pushBack_Array(&d->fontPriorityOrder,
                   &(iPrioMapItem){ spec->priority, (uint32_t) baseId });
    for (enum iFontStyle style = 0; style < max_FontStyle; style++) {
        const iFontFile *file = spec->styles[style];
        for (enum iFontSize sizeId = 0; sizeId < max_FontSize; sizeId++) {
            const float base = (sizeId < contentRegular_FontSize ? uiSize : textSize);
            init_AppleFont_(appleFont_AppleText_(d, FONT_ID(baseId, style, sizeId)),
                           spec, file, sizeId, base * scale_FontSize(sizeId));
        }
    }
}

static void setupVariants_AppleText_(iText *base, const iFontSpec *spec, int baseId,
                                       float uiSize, float textSize) {
    setupFontVariants_AppleText_(appleText_(base), spec, baseId, uiSize, textSize);
}

static iBool hasVariant_AppleText_(iText *base, const iFontSpec *spec) {
    const iAppleText *d = appleText_(base);
    for (size_t i = 0; i < size_Array(&d->fonts); i += maxVariants_Fonts) {
        if (((const iAppleFont *) constAt_Array(&d->fonts, i))->font.spec == spec) return iTrue;
    }
    return iFalse;
}

static int allocateSlot_AppleText_(iText *base) {
    iAppleText *d = appleText_(base);
    const int fontId = (int) size_Array(&d->fonts);
    resize_Array(&d->fonts, fontId + maxVariants_Fonts);
    return fontId;
}

static void initFonts_AppleText_(iAppleText *d) {
    resize_Array(&d->fonts, auxiliary_FontId); /* pre-size for mandatory font slots */
    initFonts_Text(&d->base,
                   &d->fontPriorityOrder,
                   &d->overrideFontId,
                   &d->monoFallback,
                   &(iFontInitCallbacks) {
                       .setupSpec = setupVariants_AppleText_,
                       .hasSpec   = hasVariant_AppleText_,
                       .alloc     = allocateSlot_AppleText_,
                   });
    applyCascadeList_AppleText_(d);
#if !defined (NDEBUG)
    printf("[Text] %zu font variants ready\n", size_Array(&d->fonts));
#endif
}

static void deinitFonts_AppleText_(iAppleText *d) {
    iForEach(Array, it, &d->fonts) {
        deinit_AppleFont_(it.value);
    }
    clear_Array(&d->fonts);
    if (d->cascadeList) {
        CFRelease(d->cascadeList);
        d->cascadeList = NULL;
    }
}

static void clearRunCache_AppleText_(iAppleText *d) {
    iForIndices(i, d->runCache) {
        if (d->runCache[i]) {
            delete_AppleTextRun(d->runCache[i]);
            d->runCache[i] = NULL;
        }
    }
    d->runCacheSerial = 0;
}

static iAppleTextRun *maybeMakeRun_AppleText_(iAppleText *d, iRangecc text, int fontId,
                                              int colorId) {
    /* Find or create a run cache entry for the given raw text, font, and base color. */
    const size_t   rawLen = (size_t) (text.end - text.start);
    const uint32_t hash   = iCrc32(text.start, rawLen)
                            ^ (uint32_t) ((unsigned) fontId << 8)
                            ^ (uint32_t) ((unsigned) (colorId & mask_ColorId) << 16);
    /* Search for an existing entry. */
    for (int i = 0; i < maxRunCache_AppleText_; i++) {
        iAppleTextRun *r = d->runCache[i];
        if (r && r->hash == hash && r->fontId == fontId && r->colorId == colorId &&
            r->rawTextLen == rawLen) {
            r->lastUsed = ++d->runCacheSerial;
            return r;
        }
    }
    /* Find the least recently used slot. */
    int      lruSlot   = 0;
    uint32_t lruSerial = UINT32_MAX;
    for (int i = 0; i < maxRunCache_AppleText_; i++) {
        if (!d->runCache[i]) {
            lruSlot   = i;
            lruSerial = 0;
            break;
        }
        if (d->runCache[i]->lastUsed < lruSerial) {
            lruSerial = d->runCache[i]->lastUsed;
            lruSlot   = i;
        }
    }
    /* Evict the LRU entry. */
    if (d->runCache[lruSlot]) {
        delete_AppleTextRun(d->runCache[lruSlot]);
        d->runCache[lruSlot] = NULL;
    }
    /* Create a new run. */
    iAppleTextRun *r = new_AppleTextRun(text.start, rawLen, fontId, colorId, d);
    if (r) {
        r->lastUsed          = ++d->runCacheSerial;
        d->runCache[lruSlot] = r;
    }
    return r;
}

static void drawLine_AppleText_(iAppleText *d, CTLineRef line, iAppleFont *af, iInt2 pos,
                                int color, int runMode) {
    /* Draw a CTLine into an SDL texture and render it at the given position.
       Foreground colors are baked into the bitmap via kCTForegroundColorAttributeName;
       background colors are drawn as CG fills before CTLineDraw. SDL is used only for
       the final texture copy. */
    iUnused(color, runMode);
    double ascentD, descentD, leadingD;
    double lineWidth = CTLineGetTypographicBounds(line, &ascentD, &descentD, &leadingD);
    int    w         = (int) ceilf((float) lineWidth) + 2; /* +2 avoids clipping at right edge */
    int    h         = af->font.height;
    if (w <= 0 || h <= 0) return;
    /* Allocate a pixel buffer for ARGB8888 premultiplied rasterization. */
    const size_t stride = (size_t) w * 4;
    uint32_t    *pixels = calloc(h, stride);
    if (!pixels) return;
    /* Create CGBitmapContext: ARGB premultiplied, 32-bit little-endian (=
       SDL_PIXELFORMAT_ARGB8888). */
    CGColorSpaceRef cs  = CGColorSpaceCreateDeviceRGB();
    CGContextRef    ctx = CGBitmapContextCreate(
        pixels, w, h, 8, stride, cs, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
    CGColorSpaceRelease(cs);
    if (!ctx) {
        free(pixels);
        return;
    }
    /* Enable fractional glyph positioning: glyphs are placed at their natural
       sub-pixel advances rather than being quantized to integer pixel boundaries.
       This is the CoreText equivalent of the STB backend's per-glyph subpixel variants. */
    CGContextSetShouldAntialias(ctx, true);
    CGContextSetShouldSubpixelPositionFonts(ctx, true);
    CGContextSetShouldSubpixelQuantizeFonts(ctx, false);
    /* CG origin is at bottom-left (y-up). `baseline` is the distance from the top of the
       line box to the baseline; vertOffset shifts the glyph down to center it. */
    CGContextSetTextPosition(ctx, 0.0, (CGFloat) (h - af->font.baseline - af->vertOffset));
    /* Draw background color fills (before glyphs) for any runs that carry lagBgKey_. */
    CFArrayRef glyphRuns = CTLineGetGlyphRuns(line);
    CFIndex    runCount  = CFArrayGetCount(glyphRuns);
    for (CFIndex ri = 0; ri < runCount; ri++) {
        CTRunRef        ctRun = (CTRunRef) CFArrayGetValueAtIndex(glyphRuns, ri);
        CFDictionaryRef ra    = CTRunGetAttributes(ctRun);
        CGColorRef      bgCg  = (CGColorRef) CFDictionaryGetValue(ra, lagBgKey_);
        if (!bgCg) continue;
        CFRange  sr   = CTRunGetStringRange(ctRun);
        CGFloat  xOff = CTLineGetOffsetForStringIndex(line, sr.location, NULL);
        double   rW   = CTRunGetTypographicBounds(ctRun, CFRangeMake(0, 0), NULL, NULL, NULL);
        CGContextSetFillColorWithColor(ctx, bgCg);
        CGContextFillRect(ctx, CGRectMake(xOff, (CGFloat) af->vertOffset,
                                              (CGFloat) rW, (CGFloat) (h - af->vertOffset)));
    }
    /* Draw glyphs with baked-in foreground colors from kCTForegroundColorAttributeName. */
    CTLineDraw(line, ctx);
    CGContextRelease(ctx);
    /* Upload to an SDL texture and blit it. Fg colors are already baked in; only
       apply opacity via the alpha modulator. Use STREAMING access + Lock/Unlock to
       avoid breaking the Metal render encoder when a render target is active. */
    SDL_Renderer *render = get_Window()->render;
    SDL_Texture  *tex =
        SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (tex) {
        void *texPixels = NULL;
        int   texPitch  = 0;
        if (SDL_LockTexture(tex, NULL, &texPixels, &texPitch) == 0) {
            for (int row = 0; row < h; row++) {
                memcpy((char *) texPixels + row * texPitch,
                       (char *) pixels    + row * (int) stride,
                       (size_t) w * 4);
            }
            SDL_UnlockTexture(tex);
        }
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(tex, (uint8_t) (d->opacity * 255.0f + 0.5f));
        const iInt2 orig = origin_Paint;
        SDL_RenderCopy(render,
                       tex,
                       &(SDL_Rect) { 0, 0, w, h },
                       &(SDL_Rect) { pos.x + orig.x, pos.y + orig.y, w, h });
        SDL_DestroyTexture(tex);
    }
    free(pixels);
}

/*----------------------------------------------------------------------------------------------*/

void run_Font(iBaseFont *d, const iRunArgs *args) {
    iAppleFont *af = (iAppleFont *) d;
    iAppleText *tx = current_AppleText_();

    const iBool isMeasure = (args->mode & modeMask_RunMode) == measure_RunMode;
    const iBool isDraw    = (args->mode & modeMask_RunMode) == draw_RunMode;

    /* Limit text to maxLen characters. */
    iRangecc text = args->text;
    if (args->maxLen > 0) {
        const char *p = text.start;
        for (size_t i = 0; i < args->maxLen && p < text.end; i++) {
            iChar ch;
            int   n = decodeBytes_MultibyteChar(p, text.end, &ch);
            if (n <= 0) break;
            p += n;
        }
        text.end = p;
    }
    if (text.start >= text.end) {
        if (args->metrics_out) {
            args->metrics_out->bounds  = init_Rect(0, 0, 0, d->height);
            args->metrics_out->advance = zero_I2();
        }
        return;
    }
#define srcPtr_(idx_) (text.start + run->utf16ToSrc[(idx_)])
    /* Get or create the CTTypesetter run from the cache. Font and color attributes are
       resolved inside the run constructor (textrun_apple.c). */
    const int fontId = fontId_Text(d) & (int) mask_FontId;
    iAppleTextRun *run = maybeMakeRun_AppleText_(tx, text, fontId, args->color);
    if (!run) {
        if (args->metrics_out) {
            args->metrics_out->bounds  = init_Rect(0, 0, 0, d->height);
            args->metrics_out->advance = zero_I2();
        }
        return;
    }
    /* Initialize the wrap range before starting the line loop. */
    iWrapText *wrap = args->wrap;
    if (wrap) {
        wrap->wrapRange_ = (iRangecc) { text.start, text.start };
    }

    const iBool isVisual = (args->mode & visualFlag_RunMode) != 0;

    iInt2   pos          = args->pos;
    int     totalWidth   = 0;
    int     lastLineW    = 0;
    int     lineCount    = 0;
    CFIndex startIdx     = 0;
    iBool   keepGoing    = iTrue;
    iRect   visualBounds = { zero_I2(), zero_I2() }; /* accumulated when isVisual */

    while (startIdx < run->utf16Len && keepGoing) {
        /* Determine how many UTF-16 units fit on this line. */
        const double wrapWidth = !wrap             ? 0.0
                               : wrap->maxWidth > 0 ? (double) wrap->maxWidth
                                                    : 1e9; /* no width limit: still break at \n */
        CFIndex      lineLen;
        if (wrapWidth > 0.5) {
            if (!wrap || wrap->mode == word_WrapTextMode) {
                lineLen = CTTypesetterSuggestLineBreak(run->typesetter, startIdx, wrapWidth);
            }
            else { /* anyCharacter_WrapTextMode */
                lineLen = CTTypesetterSuggestClusterBreak(run->typesetter, startIdx, wrapWidth);
            }
        }
        else {
            lineLen = run->utf16Len - startIdx;
        }
        if (lineLen <= 0) break;
        /* Shape the line. */
        CTLineRef line = CTTypesetterCreateLine(run->typesetter, CFRangeMake(startIdx, lineLen));
        /* Measure the line. */
        double ascentD, descentD, leadingD;
        double lineWidth = CTLineGetTypographicBounds(line, &ascentD, &descentD, &leadingD);
        lastLineW        = (int) ceilf((float) lineWidth);
        /* Visual (ink) bounds: tight glyph path box, converted to screen coords (y-down). */
        if (isVisual && args->metrics_out) {
            CGRect vb = CTLineGetBoundsWithOptions(line, kCTLineBoundsUseGlyphPathBounds);
            /* CoreText coords: origin.y = descent below baseline (negative); y increases up.
               Screen coords: y increases down, baseline at pos.y + af->font.baseline. */
            int vx = pos.x + (int) floor(vb.origin.x);
            int vy = pos.y + af->font.baseline + af->vertOffset
                     - (int) ceil(vb.origin.y + vb.size.height);
            int vw = (int) ceil(vb.size.width);
            int vh = (int) ceil(vb.size.height);
            iRect lineVisual = init_Rect(vx, vy, vw, vh);
            visualBounds = isEmpty_Rect(visualBounds) ? lineVisual
                                                      : union_Rect(visualBounds, lineVisual);
        }
        /* Source text pointers for the line boundaries. */
        const CFIndex endIdx    = startIdx + lineLen;
        const char   *lineStart = srcPtr_(startIdx);
        const char   *lineEnd   = (endIdx <= run->utf16Len) ? srcPtr_(endIdx) : text.end;
        /* WrapText callback: notify about this line — must come before drawing so that
           any SDL fills (e.g. mark/selection background) land beneath the text. */
        if (wrap && wrap->wrapRange_.start) {
            iTextAttrib attrib = { .fgColorId = args->color };
            keepGoing          = notify_WrapText(wrap, lineEnd, attrib, 0, lastLineW);
        }
        /* Create a justified version of the line when justification is requested.
           This must be used for both drawing and offset/hit queries so that
           CTLineGetOffsetForStringIndex returns positions matching the drawn glyphs. */
        CTLineRef justifiedLine = NULL;
        if (args->justify && args->layoutBound > 0) {
            justifiedLine = CTLineCreateJustifiedLine(line, 1.0, (double) args->layoutBound);
        }
        CTLineRef activeLine = justifiedLine ? justifiedLine : line;
        /* Draw the line (only when in draw mode). */
        if (isDraw) {
            drawLine_AppleText_(tx, activeLine, af, pos, args->color, args->mode);
        }
        /* Hit testing: find the character at hitPoint (screen coordinate -> source pointer). */
        if (wrap && !wrap->hitChar_out) {
            const int lineTop    = pos.y;
            const int lineBottom = pos.y + d->height;
            if (wrap->hitPoint.y >= lineTop && wrap->hitPoint.y < lineBottom) {
                int localX = wrap->hitPoint.x - pos.x;
                if (localX < 0) localX = 0;
                CFIndex idx =
                    CTLineGetStringIndexForPosition(activeLine, CGPointMake((CGFloat) localX, 0.0));
                /* Clamp to the line's range. */
                if (idx < startIdx) idx = startIdx;
                if (idx > endIdx) idx = endIdx;
                /* Guard against out-of-bounds access. */
                if (idx <= run->utf16Len) {
                    wrap->hitChar_out = srcPtr_(idx);
                }
                else {
                    wrap->hitChar_out = lineEnd;
                }
                /* Normalized X position within the glyph. */
                CGFloat thisOff = CTLineGetOffsetForStringIndex(activeLine, idx, NULL);
                CGFloat nextOff = CTLineGetOffsetForStringIndex(activeLine, idx + 1, NULL);
                if (nextOff > thisOff + 0.5f) {
                    wrap->hitGlyphNormX_out = iClamp(
                        (float) (localX - thisOff) / (float) (nextOff - thisOff), 0.0f, 1.0f);
                }
            }
        }
        /* Hit testing: find the pixel advance to hitChar (source pointer -> screen offset). */
        if (wrap && wrap->hitChar && wrap->hitChar >= lineStart && wrap->hitChar <= lineEnd) {
            /* Scan for the UTF-16 index that corresponds to the source pointer. */
            CFIndex hitIdx = endIdx;
            for (CFIndex i = startIdx; i <= endIdx && i <= run->utf16Len; i++) {
                if (srcPtr_(i) >= wrap->hitChar) {
                    hitIdx = i;
                    break;
                }
            }
            CGFloat off          = CTLineGetOffsetForStringIndex(activeLine, hitIdx, NULL);
            wrap->hitAdvance_out = init_I2((int) (pos.x - args->pos.x + off), pos.y - args->pos.y);
            wrap->hitChar        = NULL; /* mark as found */
        }
        /* Update running metrics. */
        totalWidth = iMax(totalWidth, lastLineW);
        lineCount++;
        if (justifiedLine) CFRelease(justifiedLine);
        CFRelease(line);
        startIdx += lineLen;
        pos.y += d->height;
        /* Stop when wrapping limit or max lines is reached. */
        if (wrap && wrap->maxLines > 0 && (size_t) lineCount >= wrap->maxLines) {
            break;
        }
        /* Without a WrapText (single-line call), process only one line. */
        if (!wrap) break;
    }
    if (args->metrics_out) {
        args->metrics_out->bounds = isVisual ? visualBounds
                                             : init_Rect(0, 0, totalWidth, lineCount * d->height);
        args->metrics_out->advance =
            init_I2(lastLineW, (lineCount > 1 ? lineCount - 1 : 0) * d->height);
    }
#undef srcPtr_
}

void allocData_FontFile(iFontFile *d) {
    CTFontRef tmpFont = NULL;
    if (size_Block(&d->sourceData) > 0) {
        /* Create a CTFont from raw font data. */
        const void       *bytes    = constData_Block(&d->sourceData);
        size_t            sz       = size_Block(&d->sourceData);
        CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, bytes, sz, NULL);
        CGFontRef         cgFont   = CGFontCreateWithDataProvider(provider);
        CGDataProviderRelease(provider);
        if (!cgFont) return;
        /* Read ascent/descent from CGFont (in design units). */
        d->ascent  = CGFontGetAscent(cgFont);  /* positive */
        d->descent = CGFontGetDescent(cgFont); /* negative (same sign convention as stbtt) */
        tmpFont    = CTFontCreateWithGraphicsFont(cgFont, 12.0, NULL, NULL);
        CGFontRelease(cgFont);
    }
    else if (!isEmpty_String(&d->id)) {
        /* Named system font: look up by PostScript name. */
        CFStringRef psName = CFStringCreateWithCString(
            kCFAllocatorDefault, cstr_String(&d->id), kCFStringEncodingUTF8);
        tmpFont = CTFontCreateWithName(psName, 12.0, NULL);
        CFRelease(psName);
        if (!tmpFont) return;
        /* Read design-unit metrics via CGFont. */
        CGFontRef cgFont = CTFontCopyGraphicsFont(tmpFont, NULL);
        if (cgFont) {
            d->ascent  = CGFontGetAscent(cgFont);
            d->descent = CGFontGetDescent(cgFont);
            CGFontRelease(cgFont);
        }
        else {
            /* Fallback: scale pixel metrics at 12pt to design units. */
            CGFloat upm = (CGFloat) CTFontGetUnitsPerEm(tmpFont);
            d->ascent   = (int) roundf((float) (CTFontGetAscent(tmpFont) * upm / 12.0));
            d->descent  = -(int) roundf((float) (CTFontGetDescent(tmpFont) * upm / 12.0));
        }
    }
    if (!tmpFont) return;
    /* Read the 'M' advance and convert to design units for emAdvance. */
    UniChar mChar  = 'M';
    CGGlyph mGlyph = 0;
    CTFontGetGlyphsForCharacters(tmpFont, &mChar, &mGlyph, 1);
    if (mGlyph) {
        CGSize adv = CGSizeZero;
        CTFontGetAdvancesForGlyphs(tmpFont, kCTFontOrientationDefault, &mGlyph, &adv, 1);
        CGFloat unitsPerEm = (CGFloat) CTFontGetUnitsPerEm(tmpFont);
        CGFloat pointSize  = CTFontGetSize(tmpFont);
        d->emAdvance       = (int) roundf((float) (adv.width * unitsPerEm / pointSize));
        d->unitsPerEm      = (int) unitsPerEm;
    }
    else {
        d->unitsPerEm = (int) CTFontGetUnitsPerEm(tmpFont);
        d->emAdvance  = d->unitsPerEm;
    }
    d->data = (void *) (uintptr_t) tmpFont; /* retained; released by deallocData_FontFile */
}

void deallocData_FontFile(iFontFile *d) {
    if (d->data) {
        CFRelease((CTFontRef) d->data);
        d->data = NULL;
    }
}

iBool isMonospace_FontFile(const iFontFile *d) {
    CTFontRef font = (CTFontRef) d->data;
    if (!font) return iFalse;
    UniChar chars[3]  = { 'M', 'i', '.' };
    CGGlyph glyphs[3] = { 0, 0, 0 };
    CTFontGetGlyphsForCharacters(font, chars, glyphs, 3);
    CGSize adv[3];
    CTFontGetAdvancesForGlyphs(font, kCTFontOrientationDefault, glyphs, adv, 3);
    return fabsf((float) (adv[0].width - adv[1].width)) < 0.01f &&
           fabsf((float) (adv[0].width - adv[2].width)) < 0.01f;
}

/*----------------------------------------------------------------------------------------------*/

static iAppleText *sharedText_;

iText *new_Text(SDL_Renderer *render, float documentFontSizeFactor) {
    if (!lagBgKey_) {
        lagBgKey_ = CFSTR("LagrangeBgColor"); /* custom attribute key for background colors */
    }
    if (!sharedText_) {
        sharedText_ = iNew(AppleText);
        init_Text(&sharedText_->base, render, documentFontSizeFactor);
        iText *oldActive = current_Text();
        setCurrent_Text(&sharedText_->base);
        init_Array(&sharedText_->fonts, sizeof(iAppleFont));
        init_Array(&sharedText_->fontPriorityOrder, sizeof(iPrioMapItem));
        sharedText_->overrideFontId = -1;
        sharedText_->cascadeList    = NULL;
        sharedText_->opacity        = 1.0f;
        sharedText_->runCacheSerial = 0;
        iZap(sharedText_->runCache);
        initFonts_AppleText_(sharedText_);
        setCurrent_Text(oldActive);
    }
    else {
        ref_Object(sharedText_); /* all windows use the same one */
    }
    return &sharedText_->base;
}

void deinit_AppleText(iAppleText *d) {
    clearRunCache_AppleText_(d);
    deinitFonts_AppleText_(d);
    deinit_Array(&d->fontPriorityOrder);
    deinit_Array(&d->fonts);
    deinit_Text(&d->base);
    iAssert(sharedText_ == d);
    sharedText_ = NULL;
}

void delete_Text(iText *d) {
    deref_Object(appleText_(d));
}

void setOpacity_Text(float opacity) {
    current_AppleText_()->opacity = iClamp(opacity, 0.0f, 1.0f);
}

void resetFontsIfNeeded_Text(iText *d) {
    iAppleText *at = appleText_(d);
    if (d->needRefresh) {
        iText *oldActive = current_Text();
        setCurrent_Text(d);
        clearRunCache_AppleText_(at);
        deinitFonts_AppleText_(at);
        initFonts_AppleText_(at);
        setCurrent_Text(oldActive);
        d->needRefresh = iFalse;
    }
}

void resetFontCache_Text(iText *d) {
    iAppleText *at = appleText_(d);
    iText *oldActive = current_Text();
    setCurrent_Text(d);
    clearRunCache_AppleText_(at);
    setCurrent_Text(oldActive);
}

iBool checkMissing_Text(void) {
    /* Core Text handles missing glyphs internally via the cascade list, so the missing-glyph
       machinery is mostly a no-op here. We can assume the OS will find the glyph. */
    return iFalse;
}

iChar missing_Text(size_t index) {
    iUnused(index);
    return 0;
}

void resetMissing_Text(iText *d) {
    iUnused(d);
}

SDL_Texture *glyphCache_Text(void) {
    /* There is no separate glyph cache texture in the Core Text backend; returns NULL. */
    return NULL;
}

void cache_Text(int fontId, iRangecc text) {
    /* Pre-rendering is a no-op in the Core Text backend; runs are cached lazily. */
    iUnused(fontId, text);
}

iDefineClass(AppleText)
