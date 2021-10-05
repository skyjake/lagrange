/* Copyright 2021 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include <the_Foundation/archive.h>
#include "stb_truetype.h"

#if defined (LAGRANGE_ENABLE_HARFBUZZ)
#   include <hb.h>
#endif

/* Fontpacks are ZIP archives that contain a configuration file and one of more font
files. The fontpack format is used instead of plain TTF/OTF because the text renderer
uses additional metadata about each font.

All the available fontpacks are loaded and used for looking up glyphs for rendering.
The user may install new fontpacks via the GUI. The user's fontpacks are stored inside
the config directory. There may also be fontpacks available from system-wide locations. */

enum iFontSize {
    uiTiny_FontSize,   /* 0.800 */
    uiSmall_FontSize,  /* 0.900 */
    uiNormal_FontSize, /* 1.000 */
    uiMedium_FontSize, /* 1.125 */
    uiBig_FontSize,    /* 1.333 */
    uiLarge_FontSize,  /* 1.666 */
    contentRegular_FontSize,
    contentMedium_FontSize,
    contentBig_FontSize,
    contentLarge_FontSize,
    contentHuge_FontSize,
    contentMonoSmall_FontSize,
    contentMono_FontSize,
    max_FontSize
};

enum iFontStyle {
    regular_FontStyle,
    italic_FontStyle,
    light_FontStyle,
    semiBold_FontStyle,
    bold_FontStyle,
    max_FontStyle
};

iLocalDef enum iFontSize larger_FontSize(enum iFontSize size) {
    if (size == uiLarge_FontSize || size == contentHuge_FontSize || size == contentMono_FontSize) {
        return size; /* largest available */
    }
    return size + 1;
}

iDeclareType(FontSpec)
iDeclareTypeConstruction(FontSpec)

enum iFontSpecFlags {
    monospace_FontSpecFlag = iBit(1), /* can be used in preformatted content */
    auxiliary_FontSpecFlag = iBit(2), /* only used for looking up glyphs missing from other fonts */
    arabic_FontSpecFlag    = iBit(3),
};

iDeclareType(FontFile)
iDeclareTypeConstruction(FontFile)
    
struct Impl_FontFile {
    enum iFontStyle style;
    iBlock          sourceData;
    stbtt_fontinfo  stbInfo;
#if defined (LAGRANGE_ENABLE_HARFBUZZ)
    hb_blob_t *hbBlob;
    hb_face_t *hbFace;
    hb_font_t *hbFont;
#endif
};

struct Impl_FontSpec {
    iString id;   /* unique ID */
    iString name; /* human-readable label */
    int     flags;
    int     priority;
    float   scaling;
    const iFontFile *styles[max_FontSize];
};
 
void    init_Fonts      (const char *userDir);
void    deinit_Fonts    (void);

