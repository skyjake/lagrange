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

/* Backend-private header for the FreeType text rendering backend.
   Only include from freetype_text.c. */

#pragma once

#include "render/text_backend.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_TABLES_H
#include FT_COLOR_H        /* FT_Palette_Select, FT_Get_Color_Glyph_Layer */
#include FT_LCD_FILTER_H   /* FT_Library_SetLcdFilter */
#include FT_GLYPH_H        /* FT_Glyph_Get_CBox, FT_GLYPH_BBOX_PIXELS */
#include FT_OUTLINE_H      /* FT_Outline_Get_CBox */

#if defined (LAGRANGE_ENABLE_HARFBUZZ)
#   include <hb.h>
#   include <hb-ft.h>
#endif

/*----------------------------------------------------------------------------------------------*/

typedef struct {
#if defined (LAGRANGE_ENABLE_HARFBUZZ)
    hb_font_t *hbFont; /* created via hb_ft_font_create; matches system FT ABI */
#endif
    FT_Face    ftFace;
    iBool      hasColorGlyphs; /* FT_HAS_COLOR: CBDT/CBLC or COLR table */
    iBool      isFixedSize;    /* no scalable outline, only fixed bitmap strikes (e.g. Noto
                                   Color Emoji); FT_Set_Pixel_Sizes() only works at native size */
    int        fixedSizeIndex; /* best available_sizes[] index to select */
    int        fixedSizePpem;  /* native pixel height of that embedded strike */
} iFtFontData;

iLocalDef iFtFontData *ftData_FontFile(const iFontFile *d) {
    return (iFtFontData *) d->data;
}

/* Implemented in freetype_text.c. */

#if defined (LAGRANGE_ENABLE_HARFBUZZ)
/* Implemented in freetype_text.c; declared in text_backend.h. */
hb_font_t *hbFont_FontFile(const iFontFile *d);
#endif

/*----------------------------------------------------------------------------------------------*/

/* Alias for local use in freetype_text.c. */
typedef iRasterFont iFont;
typedef iRasterText iFtText;

iLocalDef iFtText *current_FtText_(void) {
    return currentRaster_Text_();
}

iLocalDef iFont *font_FtText_(enum iFontId id) {
    return at_Array(&current_FtText_()->fonts, id & mask_FontId);
}

/*----------------------------------------------------------------------------------------------*/

void init_FtText (iFtText *, SDL_Renderer *, float documentFontSizeFactor);
void deinit_FtText(iFtText *);

/* Shared FreeType library handle; initialized lazily on first use. */
FT_Library ftLibrary_FtText(void);
void       doneFtLibrary_FtText(void);
void       allocData_FontFile   (iFontFile *); /* backend-specific; declared here for fontcache use */
