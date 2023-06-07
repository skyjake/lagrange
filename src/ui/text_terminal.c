/* Copyright 2022 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "text.h"
#include "prefs.h"
#include "paint.h" /* origin_Paint */
#include "app.h"

#include <SDL_render.h>
#include <the_Foundation/regexp.h>

iDeclareType(Font)
iDeclareType(Glyph)

int enableHalfPixelGlyphs_Text = false;

struct Impl_Glyph {
    iFont *font;
    float advance;
    iInt2 d[2];
    iRect rect[2];
};

struct Impl_Font {
    iBaseFont font;
    iFontSpec *spec;
    int baseline;
    iGlyph glyphs[4]; /* Glyphs with advance of 0..3. */
};

static const iGlyph *glyph_Font_(iFont *d, iChar ch) {
    int w = SDL_UnicodeWidth(get_Window()->render, ch);
    w = iMin(3, w);
    return &d->glyphs[w];   
}

static uint32_t index_Glyph_(const iGlyph *d) {
    return 0;
}

static iBool isRasterized_Glyph_(const iGlyph *d, int hoff) {
    iUnused(d, hoff);
    return iTrue;
}

static void init_Font(iFont *d, int height) {
    d->spec = new_FontSpec();    
    d->font.file = NULL;
    d->font.spec = d->spec;
    d->font.height = height;
    d->baseline = 0;
    for (unsigned i = 0; i < iElemCount(d->glyphs); i++) {
        iGlyph *glyph = &d->glyphs[i];
        glyph->font = d;
        glyph->advance = i;
        for (size_t j = 0; j < iElemCount(glyph->d); j++) {
            glyph->d[j]    = init_I2(0, 0); //height / 2);
            glyph->rect[j] = init_Rect(0, 0, i, height);
        }
    }
}

static void deinit_Font(iFont *d) {
    delete_FontSpec(d->spec);
}

static void cacheSingleGlyph_Font_(iFont *d, uint32_t glyphIndex) {}

/*----------------------------------------------------------------------------------------------*/

iDeclareType(TuiText)

struct Impl_TuiText {
    iText  base;
    iFont  fonts[2][3]; /* height / regular, bold, italic  */
};

iLocalDef iTuiText *current_TuiText_(void) {
    return (iTuiText *) current_Text();
}

iBaseFont *font_Text(enum iFontId id) {
    const enum iFontStyle style = style_FontId(id);
    const enum iFontSize  size  = size_FontId(id);    
    size_t sizeIndex = (size == contentHuge_FontSize ? 1 : 0);
    size_t index     = (style == bold_FontStyle || style == semiBold_FontStyle ? 1
                        : style == italic_FontStyle                            ? 2
                                                                               : 0);
    return &current_TuiText_()->fonts[sizeIndex][index].font;
}

enum iFontId fontId_Text(const iAnyFont *font) {
    for (size_t sizeIndex = 0; sizeIndex < 2; sizeIndex++) {
        const iTuiText *d = current_TuiText_();
        const enum iFontSize size = (sizeIndex == 1 ? contentHuge_FontSize : 0);
        if (font == &d->fonts[sizeIndex][2]) {
            return FONT_ID(default_FontId, italic_FontStyle, size);
        }
        if (font == &d->fonts[sizeIndex][1]) {
            return FONT_ID(default_FontId, bold_FontStyle, size);
        }
        if (font == &d->fonts[sizeIndex][0]) {
            return FONT_ID(default_FontId, regular_FontStyle, size);
        }
    }
    return default_FontId;
}

iBaseFont *characterFont_BaseFont(iBaseFont *d, iChar ch) {
    iUnused(ch);
    return d;
}

static void init_TuiText(iTuiText *d, SDL_Renderer *render, float documentFontSizeFactor) {
    init_Text(&d->base, render, documentFontSizeFactor);
    iForIndices(s, d->fonts) {
        iForIndices(i, d->fonts[s]) {
            init_Font(d->fonts[s] + i, s == 1 ? 2 : 1);
        }        
    }
    gap_Text = gap_UI;
}

static void deinit_TuiText(iTuiText *d) {
    iForIndices(s, d->fonts) {
        iForIndices(i, d->fonts[s]) {
            deinit_Font(d->fonts[s] + i);
        }
    }
    deinit_Text(&d->base);
}

iText *new_Text(SDL_Renderer *render, float documentFontSizeFactor) {
    iTuiText *d = iMalloc(TuiText);
    init_TuiText(d, render, documentFontSizeFactor);
    return (iText *) d;
}

void delete_Text(iText *d) {
    deinit_TuiText((iTuiText *) d);
    free(d);
}

void resetFonts_Text(iText *d) {}

void resetFontCache_Text(iText *d) {}

iChar missing_Text(size_t index) {
    iUnused(index);
    return 0;
}

void resetMissing_Text(iText *d) {}

iBool checkMissing_Text(void) {
    return iFalse;
}

SDL_Texture *glyphCache_Text(void) {
    return NULL;
}

void setOpacity_Text(float opacity) {
    iUnused(opacity);
}

void cache_Text(int fontId, iRangecc text) {}

static iChar nextChar_(const char **chPos, const char *end) {
    if (*chPos == end) {
        return 0;
    }
    iChar ch;
    int len = decodeBytes_MultibyteChar(*chPos, end, &ch);
    if (len <= 0) {
        (*chPos)++; /* skip it */
        return 0;
    }
    (*chPos) += len;
    return ch;
}

static float nextTabStop_Font_(const iFont *d, float x) {
    const float stop = prefs_App()->tabWidth;
    return floorf(x / stop) * stop + stop;
}

#include "text_simple.c"

void run_Font(iBaseFont *font, const iRunArgs *args) {
    runSimple_Font_((iFont *) font, args);
}
