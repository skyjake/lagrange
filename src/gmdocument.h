/* Copyright 2020 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "defs.h"
#include "gmutil.h"
#include "media.h"

#include <the_Foundation/array.h>
#include <the_Foundation/object.h>
#include <the_Foundation/rect.h>
#include <the_Foundation/string.h>
#include <the_Foundation/time.h>

iDeclareType(GmHeading)
iDeclareType(GmPreMeta)
iDeclareType(GmRun)

enum iGmLineType {
    undefined_GmLineType = -1,
    text_GmLineType = 0,
    bullet_GmLineType,
    preformatted_GmLineType,
    quote_GmLineType,
    heading1_GmLineType,
    heading2_GmLineType,
    heading3_GmLineType,
    link_GmLineType,
    max_GmLineType,
};

enum iGmLineType    lineType_Rangecc   (const iRangecc line);
void                trimLine_Rangecc   (iRangecc *line, enum iGmLineType type, iBool normalize);

enum iGmDocumentTheme {
    colorfulDark_GmDocumentTheme,
    colorfulLight_GmDocumentTheme,
    black_GmDocumentTheme,
    gray_GmDocumentTheme,
    white_GmDocumentTheme,
    sepia_GmDocumentTheme,
    highContrast_GmDocumentTheme,
    oceanic_GmDocumentTheme,
    vibrantLight_GmDocumentTheme,
    max_GmDocumentTheme
};

iBool isDark_GmDocumentTheme(enum iGmDocumentTheme);

typedef uint16_t iGmLinkId;

enum iGmLinkScheme {
    gemini_GmLinkScheme = 1,
    titan_GmLinkScheme,
    gopher_GmLinkScheme,
    finger_GmLinkScheme,
    http_GmLinkScheme,
    file_GmLinkScheme,
    data_GmLinkScheme,
    about_GmLinkScheme,
    mailto_GmLinkScheme,
    spartan_GmLinkScheme,
    nex_GmLinkScheme,
};

enum iGmLinkFlag {
    supportedScheme_GmLinkFlag    = 0x3f, /* mask of bits 1...6 */
    remote_GmLinkFlag             = iBit(7),
    humanReadable_GmLinkFlag      = iBit(8), /* link has a human-readable description */
    imageFileExtension_GmLinkFlag = iBit(9),
    audioFileExtension_GmLinkFlag = iBit(10),
    content_GmLinkFlag            = iBit(11), /* content visible below */
    visited_GmLinkFlag            = iBit(12), /* in the history */
    permanent_GmLinkFlag          = iBit(13), /* content cannot be dismissed; media link */
    query_GmLinkFlag              = iBit(14), /* Gopher/Spartan query link */
    iconFromLabel_GmLinkFlag      = iBit(15), /* use an Emoji/special character from label */
    isOpen_GmLinkFlag             = iBit(16), /* currently open in a tab */
    fontpackFileExtension_GmLinkFlag = iBit(17),
    inline_GmLinkFlag             = iBit(18),
};

iLocalDef enum iGmLinkScheme scheme_GmLinkFlag(int flags) {
    return flags & supportedScheme_GmLinkFlag;
}

struct Impl_GmHeading {
    iRangecc text;
    int level; /* 0, 1, 2 */
};

enum iGmRunFlags {
    decoration_GmRunFlag   = iBit(1), /* not part of the source */
    startOfLine_GmRunFlag  = iBit(2),
    endOfLine_GmRunFlag    = iBit(3),
    notJustified_GmRunFlag = iBit(4),
    ruler_GmRunFlag        = iBit(5),
    wide_GmRunFlag         = iBit(6), /* horizontally scrollable */
    caption_GmRunFlag      = iBit(7),
    altText_GmRunFlag      = iBit(8),
};

/* This structure is tightly packed because GmDocuments are mostly composed of
   a large number of GmRuns. */
struct Impl_GmRun {
    iRangecc  text;
    iRect     bounds;    /* used for hit testing, may extend to edges */
    iRect     visBounds; /* actual visual bounds */
    struct {
        uint32_t linkId    : 16; /* GmLinkId; zero for non-links */
        uint32_t flags     : 8; /* GmRunFlags */
        uint32_t isRTL     : 1;
        uint32_t color     : 7; /* see max_ColorId */

        uint32_t font      : 14;
        uint32_t mediaType : 3; /* note: max_MediaType means preformatted block */
        uint32_t mediaId   : 11;
        uint32_t lineType  : 3;
        uint32_t isLede    : 1;
    };
};

iDeclareType(GmRunRange)

struct Impl_GmRunRange {
    const iGmRun *start;
    const iGmRun *end;
};

iLocalDef iBool isMedia_GmRun(const iGmRun *d) {
    return d->mediaType > 0 && d->mediaType < max_MediaType;
}
iLocalDef iMediaId mediaId_GmRun(const iGmRun *d) {
    if (d->mediaType < max_MediaType) {
        return (iMediaId){ .type = d->mediaType, .id = d->mediaId };
    }
    return iInvalidMediaId;
}
iLocalDef uint32_t preId_GmRun(const iGmRun *d) {
    return d->mediaType == max_MediaType ? d->mediaId : 0;
}

iBool       isJustified_GmRun       (const iGmRun *);
int         drawBoundWidth_GmRun    (const iGmRun *);
iRangecc    findLoc_GmRun           (const iGmRun *, iInt2 pos);

enum iGmPreMetaFlag {
    folded_GmPreMetaFlag   = iBit(1),
    topLeft_GmPreMetaFlag  = iBit(2),
};

struct Impl_GmPreMeta {
    iRangecc bounds;   /* including ``` markers */
    iRangecc altText;  /* range in source */
    iRangecc contents; /* just the content lines */
    iGmRunRange runRange;
    int      flags;
    /* TODO: refactor old code to incorporate wide scroll handling here */
    int      initialOffset;
    iRect    pixelRect;
};

/*----------------------------------------------------------------------------------------------*/

iDeclareClass(GmDocument)
iDeclareObjectConstruction(GmDocument)

enum iGmDocumentWarning {
    ansiEscapes_GmDocumentWarning = iBit(1),
    missingGlyphs_GmDocumentWarning = iBit(2),
    unsupportedMediaTypeShownAsUtf8_GmDocumentWarning = iBit(3),
};

enum iGmDocumentUpdate {
    partial_GmDocumentUpdate, /* appending more content */
    final_GmDocumentUpdate,   /* process all lines, including the last one if not terminated */
};

void    setThemeSeed_GmDocument (iGmDocument *,
                                 const iBlock *paletteSeed,
                                 const iBlock *iconSeed); /* seeds may be NULL; NULL iconSeed will use paletteSeed instead */
void    setFormat_GmDocument    (iGmDocument *, enum iSourceFormat sourceFormat);
iBool   setViewFormat_GmDocument(iGmDocument *, enum iSourceFormat viewFormat); /* returns True if changed */
void    setWidth_GmDocument     (iGmDocument *, int width, int canvasWidth);
iBool   updateWidth_GmDocument  (iGmDocument *, int width, int canvasWidth);
void    redoLayout_GmDocument   (iGmDocument *);
void    invalidateLayout_GmDocument(iGmDocument *); /* will have to be redone later */
iBool   updateOpenURLs_GmDocument(iGmDocument *);
void    setUrl_GmDocument       (iGmDocument *, const iString *url);
void    setSource_GmDocument    (iGmDocument *, const iString *source, int width, int canvasWidth,
                                 enum iGmDocumentUpdate updateType);
void    setWarning_GmDocument   (iGmDocument *, int warning, iBool set);
void    foldPre_GmDocument      (iGmDocument *, uint16_t preId);

void    updateVisitedLinks_GmDocument   (iGmDocument *); /* check all links for visited status */
void    invalidatePalette_GmDocument    (iGmDocument *);
void    makePaletteGlobal_GmDocument    (const iGmDocument *); /* copies document colors to the global palette */

typedef void (*iGmDocumentRenderFunc)(void *, const iGmRun *);

iMedia *        media_GmDocument            (iGmDocument *);
const iMedia *  constMedia_GmDocument       (const iGmDocument *);
const iString * url_GmDocument              (const iGmDocument *);

void            render_GmDocument           (const iGmDocument *, iRangei visRangeY,
                                             iGmDocumentRenderFunc render, void *); /* includes partial overlaps */
const iGmRun *  renderProgressive_GmDocument(const iGmDocument *d, const iGmRun *first, int dir,
                                             size_t maxCount,
                                             iRangei visRangeY, iGmDocumentRenderFunc render,
                                             void *context);
enum iSourceFormat format_GmDocument        (const iGmDocument *);
iInt2           size_GmDocument             (const iGmDocument *);
const iArray *  headings_GmDocument         (const iGmDocument *); /* array of GmHeadings */
const iString * source_GmDocument           (const iGmDocument *);
iGmRunRange     runRange_GmDocument         (const iGmDocument *);
size_t          memorySize_GmDocument       (const iGmDocument *); /* bytes */
int             warnings_GmDocument         (const iGmDocument *);

iRangecc        findText_GmDocument                 (const iGmDocument *, const iString *text, const char *start);
iRangecc        findTextBefore_GmDocument           (const iGmDocument *, const iString *text, const char *before);
iGmRunRange     findPreformattedRange_GmDocument    (const iGmDocument *, const iGmRun *run);

int             ansiEscapes_GmDocument              (const iGmDocument *);
void            runBaseAttributes_GmDocument        (const iGmDocument *, const iGmRun *run,
                                                     int *fontId_out, int *colorId_out);

enum iGmLinkPart {
    icon_GmLinkPart,
    text_GmLinkPart,
    textHover_GmLinkPart,
//    domain_GmLinkPart,
//    visited_GmLinkPart,
};

const iGmRun *  findRun_GmDocument      (const iGmDocument *, iInt2 pos);
iRangecc        findLoc_GmDocument      (const iGmDocument *, iInt2 pos);
const iGmRun *  findRunAtLoc_GmDocument (const iGmDocument *, const char *loc);
size_t          numLinks_GmDocument     (const iGmDocument *); /* link IDs: 1...numLinks (inclusive) */
const iString * linkUrl_GmDocument      (const iGmDocument *, iGmLinkId linkId);
iRangecc        linkUrlRange_GmDocument (const iGmDocument *, iGmLinkId linkId);
iRangecc        linkLabel_GmDocument    (const iGmDocument *, iGmLinkId linkId);
iMediaId        linkImage_GmDocument    (const iGmDocument *, iGmLinkId linkId);
iMediaId        linkAudio_GmDocument    (const iGmDocument *, iGmLinkId linkId);
int             linkFlags_GmDocument    (const iGmDocument *, iGmLinkId linkId);
enum iColorId   linkColor_GmDocument    (const iGmDocument *, iGmLinkId linkId, enum iGmLinkPart part);
const iTime *   linkTime_GmDocument     (const iGmDocument *, iGmLinkId linkId);
iBool           isMediaLink_GmDocument  (const iGmDocument *, iGmLinkId linkId);
const iString * title_GmDocument        (const iGmDocument *);
iChar           siteIcon_GmDocument     (const iGmDocument *);
size_t          numPre_GmDocument       (const iGmDocument *);
const iGmPreMeta *preMeta_GmDocument    (const iGmDocument *, uint16_t preId);
iInt2           preRunMargin_GmDocument (const iGmDocument *, uint16_t preId);
iBool           preIsFolded_GmDocument  (const iGmDocument *, uint16_t preId);
iBool           preHasAltText_GmDocument(const iGmDocument *, uint16_t preId);

