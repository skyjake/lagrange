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
#include <the_Foundation/ptrarray.h>

#if defined (LAGRANGE_ENABLE_STB_TRUETYPE)
#   include "stb_truetype.h"
#endif

#if defined (LAGRANGE_ENABLE_HARFBUZZ)
#   include <hb.h>
#endif

extern const char *mimeType_FontPack;

/* Fontpacks are ZIP archives that contain a configuration file and one of more font
files. The fontpack format is used instead of plain TTF/OTF because the text renderer
uses additional metadata about each font.

All the available fontpacks are loaded and used for looking up glyphs for rendering.
The user may install new fontpacks via the GUI. The user's fontpacks are stored inside
the config directory. There may also be fontpacks available from system-wide locations. */

enum iFontSize {
    uiNormal_FontSize, /* 1.000 -- keep at index 0 for convenience */
    uiMedium_FontSize, /* 1.125 */
    uiBig_FontSize,    /* 1.333 */
    uiLarge_FontSize,  /* 1.666 */
    uiTiny_FontSize,   /* 0.800 */
    uiSmall_FontSize,  /* 0.900 */
    contentRegular_FontSize,
    contentMedium_FontSize,
    contentBig_FontSize,
    contentLarge_FontSize,
    contentHuge_FontSize,
    contentTiny_FontSize,
    contentSmall_FontSize, /* e.g., preformatted block scaled smaller to fit */
    max_FontSize
};

enum iFontStyle {
    regular_FontStyle,
    italic_FontStyle,
    light_FontStyle,
    semiBold_FontStyle,
    bold_FontStyle,
    max_FontStyle,
    /* all permutations: */
    maxVariants_Fonts = max_FontStyle * max_FontSize
};

float   scale_FontSize  (enum iFontSize size);

/*----------------------------------------------------------------------------------------------*/

iDeclareClass(FontFile)
iDeclareObjectConstruction(FontFile)
    
struct Impl_FontFile {
    iObject         object; /* reference-counted */
    iString         id; /* for detecting when the same file is used in many places */
    int             colIndex;
    enum iFontStyle style;
    iBlock          sourceData;
#if defined (LAGRANGE_ENABLE_STB_TRUETYPE)
    stbtt_fontinfo  stbInfo;
#endif
#if defined (LAGRANGE_ENABLE_HARFBUZZ)
    hb_blob_t *hbBlob;
    hb_face_t *hbFace;
    hb_font_t *hbFont;
#endif
    /* Metrics: */
    int ascent, descent, emAdvance;
};

#if defined (LAGRANGE_ENABLE_STB_TRUETYPE)
iLocalDef uint32_t findGlyphIndex_FontFile(const iFontFile *d, iChar ch) {
    return stbtt_FindGlyphIndex(&d->stbInfo, ch);
}
#endif

float       scaleForPixelHeight_FontFile(const iFontFile *, int pixelHeight);
int         glyphAdvance_FontFile       (const iFontFile *, uint32_t glyphIndex);
void        measureGlyph_FontFile       (const iFontFile *, uint32_t glyphIndex,
                                         float xScale, float yScale, float xShift,
                                         int *x0, int *y0, int *x1, int *y1);
uint8_t *   rasterizeGlyph_FontFile     (const iFontFile *, float xScale,
                                         float yScale, float xShift, uint32_t glyphIndex,
                                         int *w, int *h); /* caller must free() the returned bitmap */

/*----------------------------------------------------------------------------------------------*/

/* FontSpec describes a typeface, combining multiple fonts into a group.
   The user will be choosing FontSpecs instead of individual font files. */
iDeclareType(FontSpec)
iDeclareTypeConstruction(FontSpec)

enum iFontSpecFlags {
    user_FontSpecFlag             = iBit(1),  /* user's standalone font, can be used for anything */
    override_FontSpecFlag         = iBit(2),
    monospace_FontSpecFlag        = iBit(3),  /* can be used in preformatted content */
    auxiliary_FontSpecFlag        = iBit(4),  /* only used for looking up glyphs missing from other fonts */
    allowSpacePunct_FontSpecFlag  = iBit(5),  /* space/punctuation glyphs from this auxiliary font can be used */
    fixNunitoKerning_FontSpecFlag = iBit(31), /* manual hardcoded kerning tweaks for Nunito */
};

struct Impl_FontSpec {
    iString          id;         /* unique ID */
    iString          name;       /* human-readable label */
    iString          sourcePath; /* file where the path was loaded, could be a .fontpack */
    int              flags;
    int              priority;
    float            heightScale[2];     /* overall height scaling; ui, document */
    float            glyphScale[2];      /* ui, document */
    float            vertOffsetScale[2]; /* ui, document */
    const iFontFile *styles[max_FontStyle];
};

iLocalDef int scaleType_FontSpec(enum iFontSize sizeId) {
    iAssert(sizeId >= 0 && sizeId < max_FontSize);
    return sizeId < contentRegular_FontSize ? 0 : 1;
}

/*----------------------------------------------------------------------------------------------*/

iDeclareType(FontPack)
iDeclareTypeConstruction(FontPack)

iDeclareType(FontPackId)

struct Impl_FontPackId {
    const iString *id;
    int version;
};

void                setReadOnly_FontPack    (iFontPack *, iBool readOnly);
void                setStandalone_FontPack  (iFontPack *, iBool standalone);
void                setLoadPath_FontPack    (iFontPack *, const iString *path);
void                setUrl_FontPack         (iFontPack *, const iString *url);
iBool               loadArchive_FontPack    (iFontPack *, const iArchive *zip);
iBool               detect_FontPack         (const iBlock *data);

iFontPackId         id_FontPack             (const iFontPack *);
const iString *     loadPath_FontPack       (const iFontPack *); /* may return NULL */
iBool               isDisabled_FontPack     (const iFontPack *);
iBool               isReadOnly_FontPack     (const iFontPack *);
const iPtrArray *   listSpecs_FontPack      (const iFontPack *);
iString *           infoText_FontPack       (const iFontPack *, iBool isFull);
const iArray *      actions_FontPack        (const iFontPack *, iBool showInstalled);

const iString *     idFromUrl_FontPack      (const iString *url);

/*----------------------------------------------------------------------------------------------*/

void    init_Fonts      (const char *userDir);
void    deinit_Fonts    (void);

void                enablePack_Fonts            (const iString *packId, iBool enable);
void                updateActive_Fonts          (void);
const iFontPack *   pack_Fonts                  (const char *packId);
const iFontPack *   packByPath_Fonts            (const iString *path);
const iFontSpec *   findSpec_Fonts              (const char *fontId);
const iPtrArray *   listPacks_Fonts             (void);
const iPtrArray *   listSpecs_Fonts             (iBool (*filterFunc)(const iFontSpec *));
const iPtrArray *   listSpecsByPriority_Fonts   (void);
const iString *     infoPage_Fonts              (iRangecc query);
void                install_Fonts               (const iString *fontId, const iBlock *data);
void                installFontFile_Fonts       (const iString *fileName, const iBlock *data);
void                reload_Fonts                (void);

iLocalDef iBool isInstalled_Fonts(const char *packId) {
    return pack_Fonts(packId) != NULL;
}

void                searchOnlineLibraryForCharacters_Fonts  (const iString *chars);
