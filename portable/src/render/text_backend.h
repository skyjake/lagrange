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

/* Shared types and functions for the STB TrueType and FreeType raster backends.

   What each backend provides:
   - allocData_FontFile / deallocData_FontFile (font file loading)
   - findGlyphIndex_FontFile                   (char -> glyph index)
   - hbFont_FontFile                           (HarfBuzz font for shaping)
   - glyphByIndex_Font_ / resetGlyphCaches_    (glyph cache management)
   - rasterizeForCache_Font_                   (pixel rasterization)
   - horizKern_Font_                           (STB: Nunito tweaks; FreeType: 0)
   - initCache_* / deinitCache_*               (FreeType also inits colorCache)
   - allocate_Font_                            (slot assignment in grayscaleCache) */

#pragma once

#include "../fontpack.h"
#include "font.h"
#include "attributedtext.h"
#include "text.h"

#include <the_Foundation/array.h>
#include <the_Foundation/hash.h>
#include <SDL_render.h>

#if defined (LAGRANGE_ENABLE_HARFBUZZ)
#   include <hb.h>
#endif

extern int enableHalfPixelGlyphs_Text;
extern int enableKerning_Text;
extern int numOffsetSteps_Glyph_;
extern int rasterizedAll_GlyphFlag_;

iLocalDef float offsetStep_Glyph_(void) {
    return 1.0f / (float) numOffsetSteps_Glyph_;
}

/* Computes rasterizedAll_GlyphFlag_ for n subpixel offset steps. */
int makeRasterizedAll_GlyphFlag_(int n);

enum iGlyphFlag {
    rasterized0_GlyphFlag = iBit(1),   /* zero subpixel offset */
    rasterized1_GlyphFlag = iBit(2),   /* quarter subpixel offset */
    rasterized2_GlyphFlag = iBit(3),   /* half subpixel offset */
    rasterized3_GlyphFlag = iBit(4),   /* three-quarter subpixel offset */
    isColor_GlyphFlag     = iBit(5),   /* color Emoji in colorCache; only rect[0] used --
                                          subpixel variants are skipped because Emoji render at
                                          large sizes where sub-pixel shift is imperceptible,
                                          and CBDT/CBLC bitmaps are on a fixed pixel grid */
};

iDeclareType(Glyph)
iDeclareTypeConstructionArgs(Glyph, uint32_t glyphIndex)

struct Impl_Glyph {
    iHashNode node;       /* key = glyph index in the font */
    int       flags;
    iAnyFont *font;       /* iRasterFont *; may differ from lookup font (fallback) */
    float     advance;    /* scaled advance width */
    union {
        struct {
            iRect rect[4]; /* grayscale atlas: up to 4 subpixel-offset variants */
            iInt2 d[4];    /* drawing offsets for each subpixel variant */
        };
        iRect colorRect; /* color atlas position (only valid when isColor_GlyphFlag set) */
    };
};

iLocalDef uint32_t index_Glyph_(const iGlyph *d) {
    return (uint32_t) d->node.key;
}

iLocalDef iBool isRasterized_Glyph_(const iGlyph *d, int hoff) {
    return (d->flags & (rasterized0_GlyphFlag << hoff)) != 0;
}

iLocalDef void setRasterized_Glyph_(iGlyph *d, int hoff) {
    d->flags |= rasterized0_GlyphFlag << hoff;
}

iLocalDef iBool isFullyRasterized_Glyph_(const iGlyph *d) {
    return (d->flags & rasterizedAll_GlyphFlag_) == rasterizedAll_GlyphFlag_;
}

/*----------------------------------------------------------------------------------------------*/

iDeclareType(RasterFont)
iDeclareType(GlyphTable)
iDeclareType(GlyphCache)
iDeclareType(CacheRow)

struct Impl_GlyphTable {
    iHash    glyphs;               /* key is glyph index in the font */
    uint32_t indexTable[128 - 32]; /* fast lookup for ASCII printable range */
};

iDeclareTypeConstruction(GlyphTable)

void clearGlyphs_GlyphTable_ (iGlyphTable *);

struct Impl_CacheRow {
    int   height;  /* row height (step-aligned) */
    iInt2 pos;     /* current write position */
};

/* Bookkeeping for one SDL glyph atlas texture. Both the grayscale (INDEX8) and
   color (RGBA) atlases share this struct. */
struct Impl_GlyphCache {
    SDL_Texture *texture;       /* NULL means cache is inactive (e.g. STB colorCache) */
    iInt2        size;
    int          rowAllocStep;
    int          bottom;        /* y-coordinate of lowest allocated row */
    iArray       rows;          /* iCacheRow[] */
};

iDeclareTypeConstruction(GlyphCache)

iInt2  assignPos_GlyphCache (iGlyphCache *, iInt2 glyphSize); /* row allocator */
void   reset_GlyphCache     (iGlyphCache *); /* clear without destroying texture */

/*----------------------------------------------------------------------------------------------*/

/* A common font variant used by both STB and FreeType backends.
   Backend-specific data lives in iFontFile.data (iStbFontData / iFtFontData). */
struct Impl_RasterFont {
    iBaseFont    font;       /* spec, file, height, baseline */
    int          vertOffset;
    float        xScale, yScale;
    float        emAdvance;
    iGlyphTable *table;      /* per-font glyph cache; allocated on first use */
};

iLocalDef iBool isMonospaced_RasterFont(const iRasterFont *d) {
    return isMonospaced_Font(d);
}

void        init_Font   (iRasterFont *, const iFontSpec *, const iFontFile *,
                         enum iFontSize sizeId, float height);
void        deinit_Font (iRasterFont *);

uint32_t    glyphIndex_Font_        (iRasterFont *, iChar ch); /* with indexTable cache */
iRasterFont *characterFont_Font_    (iRasterFont *, iChar ch, uint32_t *glyphIndex_out);
iGlyph *    glyph_Font_             (iRasterFont *, iChar ch); /* fallback-aware lookup */
iChar       nextChar_               (const char **chPos, const char *end); /* UTF-8 advance */

iGlyph *    glyphByIndex_Font_      (iRasterFont *, uint32_t glyphIndex);
void        allocate_Font_          (iRasterFont *, iGlyph *, int hoff); /* grayscale atlas slot */
float       horizKern_Font_         (iRasterFont *, uint32_t glyph1, uint32_t glyph2);
float       nextTabStop_Font_       (const iRasterFont *, float x);
void        cacheGlyphs_Font_       (iRasterFont *, const uint32_t *glyphIndices, size_t n);
void        cacheSingleGlyph_Font_  (iRasterFont *, uint32_t glyphIndex);

/* Returns iTrue if this glyph has color data and belongs in colorCache only.
   STB always returns iFalse. FreeType checks for COLR/CBDT on the specific glyph. */
iBool   isColorGlyph_           (const iFontFile *, uint32_t glyphIndex);

/*
* Rasterize one glyph into up to 4 subpixel-offset SDL_Surfaces (grayscale path).
* If the glyph is a color emoji that has already been uploaded to the color atlas,
* returns iTrue and leaves surfaces[] untouched (caller skips the staging step).
* `palette` is the current grayscale/b&w palette to apply to each surface.
*/
iBool rasterizeForCache_Font_(iRasterFont *font, iGlyph *glyph, SDL_Surface *surfaces[4],
                              SDL_Palette *palette);

void resetGlyphCaches_(void); /* deinit+reinit all atlas textures; clears all glyph tables */

/*----------------------------------------------------------------------------------------------*/
/* RasterText: top-level text renderer struct shared by both backends.
   Backends cast iText * to iRasterText * after confirming backend type. */

iDeclareType(RasterText)
iDeclareType(FontRun) /* forward */

struct Impl_RasterText {
    iText        base;
    iArray       fonts;             /* iRasterFont[] */
    int          overrideFontId;    /* highest-priority override font (-1 if none) */
    iFontSpec    monoFallback;      /* copy of Iosevka as low-priority monospace spec */
    iArray       fontPriorityOrder; /* iPrioMapItem[] sorted by priority */
    iGlyphCache  grayscaleCache;    /* 8-bit INDEX8 atlas; used by both backends */
    iGlyphCache  colorCache;        /* RGBA atlas; texture==NULL in STB backend */
    SDL_Palette *grayscale;
    SDL_Palette *blackAndWhite;     /* for unsmoothed/bitmap glyphs */
    iBool        missingGlyphs;
    iChar        missingChars[20];  /* rotating buffer of recently-missing chars */
    iFontRun    *cachedFontRuns[16]; /* LRU cache of recently shaped FontRuns */
};

iLocalDef iRasterText *currentRaster_Text_(void) {
    return (iRasterText *) current_Text();
}

/* Common init/deinit for iRasterText fields shared by both backends.
   Each backend calls init_RasterText first, then its own cache/font init.
   For deinit, each backend tears down its cache/fonts first, then calls deinit_RasterText. */
int  maxGlyphHeight_Text_               (const iText *);
void initGrayscaleCache_RasterText_     (iRasterText *); /* create/populate grayscaleCache */
void deinitGrayscaleCache_RasterText_   (iRasterText *);

void init_RasterText        (iRasterText *, SDL_Renderer *, float documentFontSizeFactor);
void deinit_RasterText      (iRasterText *);
void initFonts_RasterText              (iRasterText *);
void deinitFonts_RasterText            (iRasterText *);
void clearCachedFontRuns_RasterText_   (iRasterText *);

/*----------------------------------------------------------------------------------------------*/

iDeclareType(RunLayer)

struct Impl_RunLayer {
    iRasterFont    *font;
    int             mode;
    iInt2           orig;
    iRect           bounds;
    const iFontRun *fontRun;
    const iArray   *runOrder;
    iRangei         wrapPosRange;
    float           xCursor;
    float           yCursor;
    float           xCursorMax;
};

enum iRunLayerType {
    background_RunLayerType = 0,
    foreground_RunLayerType = 1,
};

void  process_RunLayer_(iRunLayer *, int layerIndex);

/*----------------------------------------------------------------------------------------------*/
#if defined (LAGRANGE_ENABLE_HARFBUZZ)

iDeclareType(GlyphBuffer)

/* HarfBuzz-shaped glyphs for one attributed run. */
struct Impl_GlyphBuffer {
    hb_buffer_t         *hb;
    iRasterFont         *font;
    const iChar         *logicalText;
    hb_glyph_info_t     *glyphInfo;
    hb_glyph_position_t *glyphPos;
    unsigned int         glyphCount;
    hb_script_t          script;
};

void  init_GlyphBuffer_  (iGlyphBuffer *, iRasterFont *font, const iChar *logicalText);
void  deinit_GlyphBuffer_(iGlyphBuffer *);
void  shape_GlyphBuffer_ (iGlyphBuffer *); /* calls hbFont_FontFile(); may take time */

float advance_GlyphBuffer_                  (const iGlyphBuffer *, iRangei wrapPosRange);
void  evenMonospaceAdvances_GlyphBuffer_    (iGlyphBuffer *, iRasterFont *baseFont);
void  alignOtherFontsVertically_GlyphBuffer_(iGlyphBuffer *, iRasterFont *baseFont);
void  justify_GlyphBuffer_                  (iGlyphBuffer *buffers, size_t numBuffers,
                                             iRangei wrapPosRange, float *wrapAdvance,
                                             int available, iBool isLast);

/* Each raster backend implements this to return the HarfBuzz font for shaping. */
hb_font_t *hbFont_FontFile(const iFontFile *d);

iDeclareType(FontRunArgs)
iDeclareTypeConstructionArgs(FontRun, const iFontRunArgs *args, const iRangecc text, uint32_t crc)

struct Impl_FontRunArgs {
    size_t       maxLen;
    iRasterFont *font;
    int          color;
    int          baseDir;
    iRasterFont *baseFont;
    int          baseFgColorId;
    iChar        overrideChar;
};

iLocalDef iBool equal_FontRunArgs(const iFontRunArgs *a, const iFontRunArgs *b) {
    return memcmp(a, b, sizeof(iFontRunArgs)) == 0;
}

struct Impl_FontRun {
    uint32_t        textCrc32;
    iFontRunArgs    args;
    iAttributedText attrText;
    iArray          buffers; /* iGlyphBuffer[]; only populated with LAGRANGE_ENABLE_HARFBUZZ */
};

iLocalDef const iGlyphBuffer *buffer_FontRun(const iFontRun *d, size_t pos) {
    return constAt_Array(&d->buffers, pos);
}

iFontRun *makeOrFindCachedFontRun_(iRasterText *, const iFontRunArgs *,
                                   const iRangecc, iBool *wasFound_out);

#endif /* LAGRANGE_ENABLE_HARFBUZZ */
