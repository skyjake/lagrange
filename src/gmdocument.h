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

#include "gmutil.h"
#include "media.h"

#include <the_Foundation/array.h>
#include <the_Foundation/object.h>
#include <the_Foundation/rect.h>
#include <the_Foundation/string.h>
#include <the_Foundation/time.h>

iDeclareType(GmHeading)
iDeclareType(GmRun)

enum iGmDocumentTheme {
    colorfulDark_GmDocumentTheme,
    colorfulLight_GmDocumentTheme,
    black_GmDocumentTheme,
    gray_GmDocumentTheme,
    white_GmDocumentTheme,
    sepia_GmDocumentTheme,
    highContrast_GmDocumentTheme,
};

iLocalDef iBool isDark_GmDocumentTheme(enum iGmDocumentTheme d) {
    return d == colorfulDark_GmDocumentTheme || d == black_GmDocumentTheme ||
           d == gray_GmDocumentTheme;
}

typedef uint16_t iGmLinkId;

enum iGmLinkFlags {
    gemini_GmLinkFlag             = iBit(1),
    gopher_GmLinkFlag             = iBit(2),
    http_GmLinkFlag               = iBit(3),
    file_GmLinkFlag               = iBit(4),
    data_GmLinkFlag               = iBit(5),
    about_GmLinkFlag              = iBit(6),
    mailto_GmLinkFlag             = iBit(7),
    supportedProtocol_GmLinkFlag  = 0xff,
    remote_GmLinkFlag             = iBit(9),
    humanReadable_GmLinkFlag      = iBit(10), /* link has a human-readable description */
    imageFileExtension_GmLinkFlag = iBit(11),
    audioFileExtension_GmLinkFlag = iBit(12),
    content_GmLinkFlag            = iBit(13), /* content visible below */
    visited_GmLinkFlag            = iBit(14), /* in the history */
    permanent_GmLinkFlag          = iBit(15), /* content cannot be dismissed; media link */
    query_GmLinkFlag              = iBit(16), /* Gopher query link */
};

struct Impl_GmHeading {
    iRangecc text;
    int level; /* 0, 1, 2 */
};

enum iGmRunFlags {
    decoration_GmRunFlag  = iBit(1), /* not part of the source */
    startOfLine_GmRunFlag = iBit(2),
    endOfLine_GmRunFlag   = iBit(3),
    siteBanner_GmRunFlag  = iBit(4), /* area reserved for the site banner */
    quoteBorder_GmRunFlag = iBit(5),
    wide_GmRunFlag        = iBit(6), /* horizontally scrollable */
};

struct Impl_GmRun {
    iRangecc  text;
    uint8_t   font;
    uint8_t   color;
    uint8_t   flags;
    iRect     bounds;    /* used for hit testing, may extend to edges */
    iRect     visBounds; /* actual visual bounds */
    uint16_t  preId;     /* preformatted block ID (sequential) */
    iGmLinkId linkId;    /* zero for non-links */
    uint16_t  imageId;   /* zero if not an image */
    uint16_t  audioId;   /* zero if not audio */
};

iDeclareType(GmRunRange)

struct Impl_GmRunRange {
    const iGmRun *start;
    const iGmRun *end;
};

const char *    findLoc_GmRun   (const iGmRun *, iInt2 pos);

iDeclareClass(GmDocument)
iDeclareObjectConstruction(GmDocument)

enum iGmDocumentFormat {
    undefined_GmDocumentFormat = -1,
    gemini_GmDocumentFormat    = 0,
    plainText_GmDocumentFormat,
};

enum iGmDocumentBanner {
    none_GmDocumentBanner,
    siteDomain_GmDocumentBanner,
    certificateWarning_GmDocumentBanner,
};

void    setThemeSeed_GmDocument (iGmDocument *, const iBlock *seed);
void    setFormat_GmDocument    (iGmDocument *, enum iGmDocumentFormat format);
void    setBanner_GmDocument    (iGmDocument *, enum iGmDocumentBanner type);
void    setWidth_GmDocument     (iGmDocument *, int width);
void    redoLayout_GmDocument   (iGmDocument *);
void    setUrl_GmDocument       (iGmDocument *, const iString *url);
void    setSource_GmDocument    (iGmDocument *, const iString *source, int width);

void    reset_GmDocument        (iGmDocument *); /* free images */

typedef void (*iGmDocumentRenderFunc)(void *, const iGmRun *);

iMedia *        media_GmDocument            (iGmDocument *);
const iMedia *  constMedia_GmDocument       (const iGmDocument *);

void            render_GmDocument           (const iGmDocument *, iRangei visRangeY,
                                             iGmDocumentRenderFunc render, void *);
iInt2           size_GmDocument             (const iGmDocument *);
const iGmRun *  siteBanner_GmDocument       (const iGmDocument *);
iBool           hasSiteBanner_GmDocument    (const iGmDocument *);
enum iGmDocumentBanner bannerType_GmDocument(const iGmDocument *);
const iString * bannerText_GmDocument       (const iGmDocument *);
const iArray *  headings_GmDocument         (const iGmDocument *); /* array of GmHeadings */
const iString * source_GmDocument           (const iGmDocument *);

iRangecc        findText_GmDocument                 (const iGmDocument *, const iString *text, const char *start);
iRangecc        findTextBefore_GmDocument           (const iGmDocument *, const iString *text, const char *before);
iGmRunRange     findPreformattedRange_GmDocument    (const iGmDocument *, const iGmRun *run);

enum iGmLinkPart {
    icon_GmLinkPart,
    text_GmLinkPart,
    textHover_GmLinkPart,
    domain_GmLinkPart,
    visited_GmLinkPart,
};

const iGmRun *  findRun_GmDocument      (const iGmDocument *, iInt2 pos);
const char *    findLoc_GmDocument      (const iGmDocument *, iInt2 pos);
const iGmRun *  findRunAtLoc_GmDocument (const iGmDocument *, const char *loc);
const iString * linkUrl_GmDocument      (const iGmDocument *, iGmLinkId linkId);
iRangecc        linkUrlRange_GmDocument (const iGmDocument *, iGmLinkId linkId);
iMediaId        linkImage_GmDocument    (const iGmDocument *, iGmLinkId linkId);
iMediaId        linkAudio_GmDocument    (const iGmDocument *, iGmLinkId linkId);
int             linkFlags_GmDocument    (const iGmDocument *, iGmLinkId linkId);
enum iColorId   linkColor_GmDocument    (const iGmDocument *, iGmLinkId linkId, enum iGmLinkPart part);
const iTime *   linkTime_GmDocument     (const iGmDocument *, iGmLinkId linkId);
iBool           isMediaLink_GmDocument  (const iGmDocument *, iGmLinkId linkId);
const iString * title_GmDocument        (const iGmDocument *);
iChar           siteIcon_GmDocument     (const iGmDocument *);

