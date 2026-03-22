/* Copyright 2024 Jaakko Keränen <jaakko.keranen@iki.fi>

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

/* Core Text text rendering backend for Apple platforms (macOS, iOS).
   This replaces the HarfBuzz + FriBidi + STB TrueType rendering pipeline.
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
#include <the_Foundation/math.h>
#include <the_Foundation/string.h>
#include <the_Foundation/block.h>
#include <the_Foundation/ptrarray.h>

#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>
#include <SDL_render.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*----------------------------------------------------------------------------------------------*/

iDeclareType(AppleFont)

struct Impl_AppleFont {
    iBaseFont font;      /* spec, file, height, baseline */
    CTFontRef ctFont;    /* NULL until first use (lazy) */
    float     pointSize; /* target point size; computed at init, used when creating ctFont */
};

static void init_AppleFont_(iAppleFont *d, const iFontSpec *spec, const iFontFile *file,
                            enum iFontSize sizeId, float baseHeight) {
    const int   scaleType   = scaleType_FontSpec(sizeId);
    const float heightScale = spec->heightScale[scaleType];
    const float glyphScale  = spec->glyphScale[scaleType];
    d->font.spec  = spec;
    d->font.file  = file;
    d->font.height = (int) (baseHeight * heightScale);
    d->ctFont     = NULL;
    d->pointSize  = 0.0f;
    if (!file || !file->data) {
        /* No usable font data; baseline is a rough estimate. */
        d->font.baseline = d->font.height * 3 / 4;
        return;
    }
    /* Compute the target point size and estimate the baseline from design metrics.
       CTFont creation is deferred until first use (ensureCtFont_AppleFont_). */
    const int totalEm = file->ascent - file->descent;
    d->pointSize = (totalEm > 0)
                       ? d->font.height * glyphScale * (float) file->emAdvance / (float) totalEm
                       : (float) d->font.height;
    if (d->pointSize < 1.0f) d->pointSize = 1.0f;
    /* Baseline ≈ ascent fraction of height, derived from the same design metrics. */
    d->font.baseline = (totalEm > 0)
                           ? (int) roundf((float) d->font.height * glyphScale *
                                          (float) file->ascent / (float) totalEm)
                           : d->font.height * 3 / 4;
    if (d->font.baseline >= d->font.height) {
        d->font.baseline = d->font.height - 1;
    }
}

static void ensureCtFont_AppleFont_(iAppleFont *d, CFArrayRef cascadeList) {
    if (d->ctFont || !d->font.file || !d->font.file->data) return;
    CTFontRef ref = (CTFontRef) (uintptr_t) d->font.file->data;
    if (cascadeList) {
        CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(
            kCFAllocatorDefault, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
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
/* Run cache: caches CTTypesetters and UTF-16 -> UTF-8 source mappings for recently
   shaped text runs. A run is keyed by (CRC of UTF-8 text, fontId). */

#define maxRunCache_AppleText_ 16

static const char **buildUtf16ToSrc_(const char *textStart, const char *textEnd,
                                     CFIndex *utf16LenOut) {
    /* Build a mapping from UTF-16 indices to UTF-8 source pointers. */
    const size_t byteLen = (size_t) (textEnd - textStart);
    const char **map     = malloc((byteLen + 2) * sizeof(const char *)); /* upper bound */
    CFIndex      idx     = 0;
    const char  *p       = textStart;
    while (p < textEnd) {
        iChar ch = 0;
        int   n  = decodeBytes_MultibyteChar(p, textEnd, &ch);
        if (n <= 0) {
            p++;
            continue;
        }
        map[idx++] = p;
        if (ch >= 0x10000) {
            /* Supplementary character: occupies two UTF-16 units (surrogate pair).
               Both units map to the same source position (start of the char). */
            map[idx++] = p;
        }
        p += n;
    }
    map[idx]     = textEnd; /* sentinel: past-end pointer */
    *utf16LenOut = idx;
    return map;
}

iDeclareType(AppleTextRun)

struct Impl_AppleTextRun {
    uint32_t        hash;
    int             fontId;
    char           *text;        /* copy of UTF-8 text */
    size_t          textLen;
    CTTypesetterRef typesetter;
    const char    **utf16ToSrc;  /* [utf16Idx] = pointer into `text` */
    CFIndex         utf16Len;    /* number of UTF-16 code units */
    uint32_t        lastUsed;    /* LRU serial */
};

iDeclareTypeConstructionArgs(AppleTextRun, const char *text, size_t textLen, int fontId, CTFontRef ctFont)

void init_AppleTextRun(iAppleTextRun *d, const char *textStart, size_t textLen, int fontId,
                       CTFontRef ctFont) {
    /* Create a new CTTypesetter run for the given text and font. */
    iZap(d);
    d->hash    = iCrc32(textStart, textLen) ^ (uint32_t)((unsigned)fontId << 8);
    d->fontId  = fontId;
    d->textLen = textLen;
    d->text    = malloc(textLen + 1);
    memcpy(d->text, textStart, textLen);
    d->text[textLen] = '\0';
    /* Build CFString from UTF-8. */
    CFStringRef cfStr = CFStringCreateWithBytes(
        kCFAllocatorDefault,
        (const UInt8 *) d->text, (CFIndex) textLen,
        kCFStringEncodingUTF8, false);
    if (!cfStr) {
        deinit_AppleTextRun(d);
        return;
    }
    d->utf16Len = CFStringGetLength(cfStr);
    d->utf16ToSrc = buildUtf16ToSrc_(d->text, d->text + textLen, &d->utf16Len);
    /* Build attributed string with font and "use context foreground color" attributes. */
    CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 2,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attrs, kCTFontAttributeName, ctFont);
    CFDictionarySetValue(attrs, kCTForegroundColorFromContextAttributeName, kCFBooleanTrue);
    CFAttributedStringRef attrStr = CFAttributedStringCreate(kCFAllocatorDefault, cfStr, attrs);
    CFRelease(attrs);
    CFRelease(cfStr);
    if (!attrStr) {
        deinit_AppleTextRun(d);
        return;
    }
    d->typesetter = CTTypesetterCreateWithAttributedString(attrStr);
    CFRelease(attrStr);
    if (!d->typesetter) {
        deinit_AppleTextRun(d);
        return;
    }
}

void deinit_AppleTextRun(iAppleTextRun *d) {
    if (d->typesetter) CFRelease(d->typesetter);
    if (d->utf16ToSrc) free(d->utf16ToSrc);
    if (d->text) free(d->text);
}

iDefineTypeConstructionArgs(AppleTextRun,
                             (const char *text, size_t textLen, int fontId, CTFontRef ctFont),
                             text, textLen, fontId, ctFont)

/*----------------------------------------------------------------------------------------------*/

iDeclareType(AppleText)
static void clearRunCache_AppleText_(iAppleText *);

struct Impl_AppleText {
    iText          base;
    iArray         fonts;              /* iAppleFont array */
    int            overrideFontId;     /* always checked first for glyphs */
    iFontSpec      monoFallback;       /* copy of Iosevka spec, low priority fallback */
    iArray         fontPriorityOrder;  /* iPrioMapItem array, sorted descending */
    CFArrayRef     cascadeList;        /* retained; shared by all lazily-created CTFonts */
    iBool          missingGlyphs;
    iChar          missingChars[20];   /* rotating buffer of missing characters */
    float          opacity;
    iAppleTextRun *runCache[maxRunCache_AppleText_];
    uint32_t       runCacheSerial;
};

iLocalDef iAppleText *current_AppleText_(void) {
    return (iAppleText *) current_Text();
}

iLocalDef iAppleFont *appleFont_AppleText_(iAppleText *d, int id) {
    return at_Array(&d->fonts, id & mask_FontId);
}

iLocalDef iAppleFont *appleFont_Text_(int id) {
    return appleFont_AppleText_(current_AppleText_(), id);
}

/*----------------------------------------------------------------------------------------------*/

static CFArrayRef buildCascadeList_AppleText_(iAppleText *d) {
    /* Build a cascade list (CFArrayRef of CTFontDescriptors) from user-installed fontpack fonts
       in priority order. System-enumerated fonts (apple-* IDs) are intentionally excluded:
       the system UI font appended at the end already gives full Unicode coverage via CoreText's
       own fallback chain. Each descriptor is derived from the 12pt reference CTFont held by
       the iFontFile, so no sized ctFont needs to exist yet. */
    CFMutableArrayRef list = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    iConstForEach(Array, it, &d->fontPriorityOrder) {
        const iPrioMapItem *item = it.value;
        const iAppleFont   *af   = appleFont_AppleText_(
            d, FONT_ID(item->fontIndex, regular_FontStyle, uiNormal_FontSize));
        if (!af || !af->font.spec || !af->font.file || !af->font.file->data) continue;
        /* Skip fonts that are not intended to participate in the cascade fallback chain. */
        if (af->font.spec->flags & ignoreAsFallback_FontSpecFlag) continue;
        CTFontRef           ref  = (CTFontRef) (uintptr_t) af->font.file->data;
        CTFontDescriptorRef desc = CTFontCopyFontDescriptor(ref);
        CFArrayAppendValue(list, desc);
        CFRelease(desc);
    }
    /* Append the default system UI font as the final fallback so Core Text can draw any
       Unicode character the fontpack fonts may not cover (emoji, CJK, symbols, etc.). */
    CTFontRef sysFont = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 0.0, NULL);
    if (sysFont) {
        CTFontDescriptorRef sysDesc = CTFontCopyFontDescriptor(sysFont);
        CFArrayAppendValue(list, sysDesc);
        CFRelease(sysDesc);
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
    setupFontVariants_AppleText_((iAppleText *) base, spec, baseId, uiSize, textSize);
}

static iBool hasVariant_AppleText_(iText *base, const iFontSpec *spec) {
    const iAppleText *d = (const iAppleText *) base;
    for (size_t i = 0; i < size_Array(&d->fonts); i += maxVariants_Fonts) {
        if (((const iAppleFont *) constAt_Array(&d->fonts, i))->font.spec == spec) return iTrue;
    }
    return iFalse;
}

static int allocateSlot_AppleText_(iText *base) {
    iAppleText *d = (iAppleText *) base;
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
                                              CTFontRef ctFont) {
    /* Find or create a run cache entry for the given text and font. */
    const size_t   textLen = (size_t) (text.end - text.start);
    const uint32_t hash    = iCrc32(text.start, textLen) ^ (uint32_t) ((unsigned) fontId << 8);
    /* Search for an existing entry. */
    for (int i = 0; i < maxRunCache_AppleText_; i++) {
        iAppleTextRun *r = d->runCache[i];
        if (r && r->hash == hash && r->fontId == fontId && r->textLen == textLen &&
            memcmp(r->text, text.start, textLen) == 0) {
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
    iAppleTextRun *r = new_AppleTextRun(text.start, textLen, fontId, ctFont);
    if (r) {
        r->lastUsed          = ++d->runCacheSerial;
        d->runCache[lruSlot] = r;
    }
    return r;
}

static void drawLine_AppleText_(iAppleText *d, CTLineRef line, iAppleFont *af, iInt2 pos,
                                int color, int runMode) {
    /* Draw a CTLine into an SDL texture and render it at the given position. */
    double ascentD, descentD, leadingD;
    double lineWidth = CTLineGetTypographicBounds(line, &ascentD, &descentD, &leadingD);
    int    w         = (int) ceilf((float) lineWidth) + 2; /* +2 avoids clipping at right edge */
    int    h         = af->font.height;
    if (w <= 0 || h <= 0) return;
    /* Allocate a pixel buffer for white-on-transparent ARGB8888 rasterization. */
    const size_t stride = (size_t) w * 4;
    uint32_t    *pixels = calloc(h, stride);
    if (!pixels) return;
    /* Create CGBitmapContext: ARGB premultiplied, 32-bit little-endian (=
     * SDL_PIXELFORMAT_ARGB8888). */
    CGColorSpaceRef cs  = CGColorSpaceCreateDeviceRGB();
    CGContextRef    ctx = CGBitmapContextCreate(
        pixels, w, h, 8, stride, cs, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
    CGColorSpaceRelease(cs);
    if (!ctx) {
        free(pixels);
        return;
    }
    /* Flip the y-axis so that y=0 is at the top of the bitmap (matching SDL memory layout).
       After translate(0,h) + scale(1,-1):  CG y = Y  maps to bitmap row = h - 1 - Y.
       We want text baseline at bitmap row `baseline`, so we set CG text position to
       y = h - baseline. */
    CGContextTranslateCTM(ctx, 0, (CGFloat) h);
    CGContextScaleCTM(ctx, 1, -1);
    CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0); /* white; colored via SDL tinting */
    CGContextSetTextPosition(ctx, 0.0, (CGFloat) (h - af->font.baseline));
    CTLineDraw(line, ctx);
    CGContextRelease(ctx);
    /* Upload to an SDL texture and blit it. */
    SDL_Renderer *render = current_Text()->render;
    SDL_Texture  *tex =
        SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (tex) {
        SDL_UpdateTexture(tex, NULL, pixels, (int) stride);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        const iColor clr = get_Color(color);
        SDL_SetTextureColorMod(tex, clr.r, clr.g, clr.b);
        SDL_SetTextureAlphaMod(tex, (uint8_t) (d->opacity * 255.0f + 0.5f));
        const iInt2 orig = origin_Paint;
        SDL_RenderCopy(render,
                       tex,
                       &(SDL_Rect) { 0, 0, w, h },
                       &(SDL_Rect) { pos.x + orig.x, pos.y + orig.y, w, h });
        /* Underline. */
        if (runMode & underline_RunMode) {
            SDL_SetRenderDrawColor(render, clr.r, clr.g, clr.b, clr.a);
            SDL_RenderFillRect(render,
                               &(SDL_Rect) { pos.x + orig.x,
                                             pos.y + orig.y + af->font.baseline + 1,
                                             (int) ceilf((float) lineWidth),
                                             iMax(1, h / 20) });
        }
        SDL_DestroyTexture(tex);
    }
    free(pixels);
}

/*----------------------------------------------------------------------------------------------*/

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
    ensureCtFont_AppleFont_(af, tx->cascadeList);
    if (!af->ctFont) {
        if (args->metrics_out) {
            args->metrics_out->bounds  = init_Rect(0, 0, 0, d->height);
            args->metrics_out->advance = zero_I2();
        }
        return;
    }

    const int fontId = fontId_Text(d) & (int) mask_FontId;

    /* Get or create the CTTypesetter run from the cache. */
    iAppleTextRun *run = maybeMakeRun_AppleText_(tx, text, fontId, af->ctFont);
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

    iInt2   pos        = args->pos;
    int     totalWidth = 0;
    int     lastLineW  = 0;
    int     lineCount  = 0;
    CFIndex startIdx   = 0;
    iBool   keepGoing  = iTrue;

    while (startIdx < run->utf16Len && keepGoing) {
        /* Determine how many UTF-16 units fit on this line. */
        const double wrapWidth = wrap ? (double) wrap->maxWidth : 0.0;
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

        /* Source text pointers for the line boundaries. */
        const CFIndex endIdx    = startIdx + lineLen;
        const char   *lineStart = run->utf16ToSrc[startIdx];
        const char   *lineEnd   = (endIdx <= run->utf16Len) ? run->utf16ToSrc[endIdx] : text.end;

        /* Draw the line (only when in draw mode). */
        if (isDraw) {
            CTLineRef drawLine = line;
            /* Justified drawing: create a wider version of the line if requested. */
            if (args->justify && args->layoutBound > 0) {
                CTLineRef justified =
                    CTLineCreateJustifiedLine(line, 1.0, (double) args->layoutBound);
                if (justified) drawLine = justified;
            }
            drawLine_AppleText_(tx, drawLine, af, pos, args->color, args->mode);
            if (drawLine != line) CFRelease(drawLine);
        }

        /* WrapText callback: notify about this line. */
        if (wrap && wrap->wrapRange_.start) {
            iTextAttrib attrib = { .fgColorId = args->color };
            keepGoing          = notify_WrapText(wrap, lineEnd, attrib, 0, lastLineW);
        }

        /* Hit testing: find the character at hitPoint (screen coordinate -> source pointer). */
        if (wrap && !wrap->hitChar_out) {
            const int lineTop    = pos.y;
            const int lineBottom = pos.y + d->height;
            if (wrap->hitPoint.y >= lineTop && wrap->hitPoint.y < lineBottom) {
                int localX = wrap->hitPoint.x - pos.x;
                if (localX < 0) localX = 0;
                CFIndex idx =
                    CTLineGetStringIndexForPosition(line, CGPointMake((CGFloat) localX, 0.0));
                /* Clamp to the line's range. */
                if (idx < startIdx) idx = startIdx;
                if (idx > endIdx) idx = endIdx;
                /* Guard against out-of-bounds access. */
                if (idx <= run->utf16Len) {
                    wrap->hitChar_out = run->utf16ToSrc[idx];
                }
                else {
                    wrap->hitChar_out = lineEnd;
                }
                /* Normalized X position within the glyph. */
                CGFloat thisOff = CTLineGetOffsetForStringIndex(line, idx, NULL);
                CGFloat nextOff = CTLineGetOffsetForStringIndex(line, idx + 1, NULL);
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
                if (run->utf16ToSrc[i] >= wrap->hitChar) {
                    hitIdx = i;
                    break;
                }
            }
            CGFloat off          = CTLineGetOffsetForStringIndex(line, hitIdx, NULL);
            wrap->hitAdvance_out = init_I2((int) (pos.x - args->pos.x + off), pos.y - args->pos.y);
            wrap->hitChar        = NULL; /* mark as found */
        }

        /* Update running metrics. */
        totalWidth = iMax(totalWidth, lastLineW);
        lineCount++;

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
        args->metrics_out->bounds = init_Rect(0, 0, totalWidth, lineCount * d->height);
        args->metrics_out->advance =
            init_I2(lastLineW, (lineCount > 1 ? lineCount - 1 : 0) * d->height);
    }
}

/*----------------------------------------------------------------------------------------------*/

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
        tmpFont = CTFontCreateWithGraphicsFont(cgFont, 12.0, NULL, NULL);
        CGFontRelease(cgFont);
    }
    else if (!isEmpty_String(&d->id)) {
        /* Named system font: look up by PostScript name. */
        CFStringRef psName = CFStringCreateWithCString(kCFAllocatorDefault,
                                                       cstr_String(&d->id),
                                                       kCFStringEncodingUTF8);
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
            d->ascent  = (int) roundf((float) (CTFontGetAscent(tmpFont)  * upm / 12.0));
            d->descent = -(int) roundf((float) (CTFontGetDescent(tmpFont) * upm / 12.0));
        }
    }
    if (!tmpFont) return;
    /* Read the 'M' advance and convert to design units for emAdvance. */
    UniChar mChar  = 'M';
    CGGlyph mGlyph = 0;
    CTFontGetGlyphsForCharacters(tmpFont, &mChar, &mGlyph, 1);
    if (mGlyph) {
        CGSize  adv       = CGSizeZero;
        CTFontGetAdvancesForGlyphs(tmpFont, kCTFontOrientationDefault, &mGlyph, &adv, 1);
        CGFloat unitsPerEm = (CGFloat) CTFontGetUnitsPerEm(tmpFont);
        CGFloat pointSize  = CTFontGetSize(tmpFont);
        d->emAdvance       = (int) roundf((float) (adv.width * unitsPerEm / pointSize));
    }
    else {
        d->emAdvance = (int) CTFontGetUnitsPerEm(tmpFont);
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
/* System font enumeration                                                                      */

static void setCFStringRef_String_(iString *d, CFStringRef src) {
    if (!src) {
        clear_String(d);
        return;
    }
    /* Determine the maximum required buffer size for a UTF-8 conversion. */
    const CFIndex len =
        CFStringGetMaximumSizeForEncoding(CFStringGetLength(src), kCFStringEncodingUTF8);
    resize_Block(&d->chars, len);
    if (CFStringGetCString(
            src, data_Block(&d->chars), size_Block(&d->chars) + 1, kCFStringEncodingUTF8)) {
        /* Trim down to the actual size. */
        resize_Block(&d->chars, strlen(cstr_String(d)));
    }
    else {
        clear_String(d);
    }
}

static iFontFile *namedFontFile_(CTFontRef font) {
    /* Create a named FontFile from a CTFont; id = PostScript name, no sourceData.
       Returns NULL if the font is inaccessible or has no metrics. */
    if (!font) return NULL;
    CFStringRef ps = CTFontCopyName(font, kCTFontPostScriptNameKey);
    if (!ps) return NULL;
    char        buf[256]; /* PostScript names are pretty short */
    const iBool ok = CFStringGetCString(ps, buf, sizeof(buf), kCFStringEncodingUTF8);
    CFRelease(ps);
    if (!ok || buf[0] == '\0') return NULL;
    iFontFile *ff = new_FontFile();
    setCStr_String(&ff->id, buf);
    allocData_FontFile(ff); /* fills ascent/descent/emAdvance from named lookup */
    if (!ff->data) {
        iRelease(ff);
        return NULL;
    }
    return ff;
}

static CTFontRef styleVariant_(CTFontRef base, CTFontSymbolicTraits traits) {
    /* Return a CTFont with the requested symbolic traits, or NULL if the family has no such
       variant (or the result is the same font as `base`). Caller must CFRelease result. */
    CTFontRef var = CTFontCreateCopyWithSymbolicTraits(base, 0.0, NULL, traits, traits);
    if (!var) return NULL;
    CFStringRef vn   = CTFontCopyName(var, kCTFontPostScriptNameKey);
    CFStringRef bn   = CTFontCopyName(base, kCTFontPostScriptNameKey);
    const iBool same = (vn && bn && CFStringCompare(vn, bn, 0) == kCFCompareEqualTo);
    if (vn) CFRelease(vn);
    if (bn) CFRelease(bn);
    if (same) {
        CFRelease(var);
        return NULL;
    }
    return var;
}

static CTFontRef weightVariant_(CFStringRef familyName, CTFontRef base, CGFloat weight) {
    /* Return a CTFont matching a target weight for the given family, verifying it is a
       different variant from `base`. Caller must CFRelease result. */
    CFNumberRef wNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberCGFloatType, &weight);
    CFMutableDictionaryRef traitDict = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(traitDict, kCTFontWeightTrait, wNum);
    CFRelease(wNum);
    CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attrs, kCTFontFamilyNameAttribute, familyName);
    CFDictionarySetValue(attrs, kCTFontTraitsAttribute, traitDict);
    CFRelease(traitDict);
    CTFontDescriptorRef desc = CTFontDescriptorCreateWithAttributes(attrs);
    CFRelease(attrs);
    CTFontRef var = CTFontCreateWithFontDescriptor(desc, 12.0, NULL);
    CFRelease(desc);
    if (!var) return NULL;
    /* Verify it is from the same family and is distinct from base. */
    CFStringRef vFamily = CTFontCopyName(var, kCTFontFamilyNameKey);
    const iBool sameFamily =
        vFamily &&
        CFStringCompare(vFamily, familyName, kCFCompareCaseInsensitive) == kCFCompareEqualTo;
    if (vFamily) CFRelease(vFamily);
    if (!sameFamily) {
        CFRelease(var);
        return NULL;
    }
    CFStringRef vn   = CTFontCopyName(var, kCTFontPostScriptNameKey);
    CFStringRef bn   = CTFontCopyName(base, kCTFontPostScriptNameKey);
    const iBool same = (vn && bn && CFStringCompare(vn, bn, 0) == kCFCompareEqualTo);
    if (vn) CFRelease(vn);
    if (bn) CFRelease(bn);
    if (same) {
        CFRelease(var);
        return NULL;
    }
    return var;
}

void enumeratePlatformFonts_FontPack_(iFontPack *pack) {
    CFArrayRef families = CTFontManagerCopyAvailableFontFamilyNames();
    if (!families) return;
    const CFIndex n = CFArrayGetCount(families);
    iString id;
    iString familyName;
    init_String(&id);
    init_String(&familyName);
    for (CFIndex i = 0; i < n; i++) {
        CFStringRef family = CFArrayGetValueAtIndex(families, i);
        /* Skip private/internal fonts whose names begin with '.'. */
        if (CFStringGetLength(family) == 0 ||
            CFStringGetCharacterAtIndex(family, 0) == '.') {
            continue;
        }
        /* Get the family name as an iString (handles any length, full UTF-8). */
        setCFStringRef_String_(&familyName, family);
        if (isEmpty_String(&familyName)) {
            continue;
        }
        /* Build the apple-* spec ID: lowercase ASCII alphanumeric, non-alphanum -> hyphen.
           The needSep flag defers hyphens so no trailing separator is possible. */
        setCStr_String(&id, "apple-");
        iBool needSep = iFalse;
        iConstForEach(String, it, &familyName) {
            const iChar ch = it.value;
            if (isAlphaNumeric_Char(ch)) {
                if (needSep) { appendChar_String(&id, '-'); needSep = iFalse; }
                appendChar_String(&id, lower_Char(ch));
            }
            else if (size_String(&id) > 6) {
                needSep = iTrue;
            }
        }
        /* Create the base (regular) CTFont for this family. */
        CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(
            kCFAllocatorDefault, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(attrs, kCTFontFamilyNameAttribute, family);
        CTFontDescriptorRef baseDesc = CTFontDescriptorCreateWithAttributes(attrs);
        CFRelease(attrs);
        CTFontRef baseFont = CTFontCreateWithFontDescriptor(baseDesc, 12.0, NULL);
        CFRelease(baseDesc);
        if (!baseFont) {
            continue;
        }
        /* Find style variants: bold, italic, light (~-0.4), semibold (~0.3). */
        CTFontRef boldFont     = styleVariant_(baseFont, kCTFontTraitBold);
        CTFontRef italicFont   = styleVariant_(baseFont, kCTFontTraitItalic);
        CTFontRef lightFont    = weightVariant_(family, baseFont, -0.4); /* light */
        CTFontRef semiboldFont = weightVariant_(family, baseFont,  0.3); /* semibold */
        /* Create iFontFile entries (PostScript-name based). */
        iFontFile *files[max_FontStyle];
        files[regular_FontStyle]  = namedFontFile_(baseFont);
        if (!files[regular_FontStyle]) {
            /* Font is inaccessible; skip this family. */
            if (boldFont)     CFRelease(boldFont);
            if (italicFont)   CFRelease(italicFont);
            if (lightFont)    CFRelease(lightFont);
            if (semiboldFont) CFRelease(semiboldFont);
            CFRelease(baseFont);
            continue;
        }
        files[bold_FontStyle]     = namedFontFile_(boldFont);
        files[italic_FontStyle]   = namedFontFile_(italicFont);
        files[light_FontStyle]    = namedFontFile_(lightFont);
        files[semiBold_FontStyle] = namedFontFile_(semiboldFont);
        /* Fill any missing styles with a reference to the regular file. */
        for (int s = 0; s < max_FontStyle; s++) {
            if (!files[s]) {
                files[s] = ref_Object(files[regular_FontStyle]);
            }
        }
        /* Build the iFontSpec. */
        iBool isMono = (CTFontGetSymbolicTraits(baseFont) & kCTFontTraitMonoSpace) != 0;
        iFontSpec *spec = new_FontSpec();
        set_String(&spec->id, &id);
        set_String(&spec->name, &familyName);
        spec->priority = 1;
        spec->flags |= ignoreAsFallback_FontSpecFlag; /* excluded from cascade; selected explicitly */
        if (isMono) spec->flags |= monospace_FontSpecFlag;
        for (int s = 0; s < max_FontStyle; s++) {
            spec->styles[s] = files[s]; /* iFontSpec takes ownership of each ref */
        }
        addSpec_FontPack(pack, spec);
        /* Release CTFont variant refs (iFontFile already holds its own via data). */
        if (boldFont)     CFRelease(boldFont);
        if (italicFont)   CFRelease(italicFont);
        if (lightFont)    CFRelease(lightFont);
        if (semiboldFont) CFRelease(semiboldFont);
        CFRelease(baseFont);
    }
    deinit_String(&familyName);
    deinit_String(&id);
    CFRelease(families);
}

/*----------------------------------------------------------------------------------------------*/

iText *new_Text(SDL_Renderer *render, float documentFontSizeFactor) {
    iAppleText *d = iMalloc(AppleText);
    init_Text(&d->base, render, documentFontSizeFactor);
    iText *oldActive = current_Text();
    setCurrent_Text(&d->base);
    init_Array(&d->fonts, sizeof(iAppleFont));
    init_Array(&d->fontPriorityOrder, sizeof(iPrioMapItem));
    d->overrideFontId  = -1;
    d->cascadeList     = NULL;
    d->missingGlyphs   = iFalse;
    d->opacity         = 1.0f;
    iZap(d->missingChars);
    iZap(d->runCache);
    d->runCacheSerial = 0;
    initFonts_AppleText_(d);
    setCurrent_Text(oldActive);
    return (iText *) d;
}

void delete_Text(iText *d) {
    iAppleText *at = (iAppleText *) d;
    clearRunCache_AppleText_(at);
    deinitFonts_AppleText_(at);
    deinit_Array(&at->fontPriorityOrder);
    deinit_Array(&at->fonts);
    deinit_Text(d);
    free(d);
}

void setOpacity_Text(float opacity) {
    current_AppleText_()->opacity = iClamp(opacity, 0.0f, 1.0f);
}

void resetFonts_Text(iText *d) {
    iAppleText *at = (iAppleText *) d;
    iText *oldActive = current_Text();
    setCurrent_Text(d);
    clearRunCache_AppleText_(at);
    deinitFonts_AppleText_(at);
    initFonts_AppleText_(at);
    setCurrent_Text(oldActive);
}

void resetFontCache_Text(iText *d) {
    iAppleText *at = (iAppleText *) d;
    iText *oldActive = current_Text();
    setCurrent_Text(d);
    clearRunCache_AppleText_(at);
    setCurrent_Text(oldActive);
}

iBool checkMissing_Text(void) {
    /* Core Text handles missing glyphs internally via the cascade list, so the missing-glyph
       machinery is mostly a no-op here. We can assume the OS will find the glyph. */
    iAppleText *d       = current_AppleText_();
    const iBool missing = d->missingGlyphs;
    d->missingGlyphs    = iFalse;
    return missing;
}

iChar missing_Text(size_t index) {
    iAppleText *d = current_AppleText_();
    if (index >= iElemCount(d->missingChars)) return 0;
    return d->missingChars[index];
}

void resetMissing_Text(iText *d) {
    iAppleText *at = (iAppleText *) d;
    at->missingGlyphs = iFalse;
    iZap(at->missingChars);
}

SDL_Texture *glyphCache_Text(void) {
    /* There is no separate glyph cache texture in the Core Text backend; returns NULL. */
    return NULL;
}

void cache_Text(int fontId, iRangecc text) {
    /* Pre-rendering is a no-op in the Core Text backend; runs are cached lazily. */
    iUnused(fontId, text);
}
