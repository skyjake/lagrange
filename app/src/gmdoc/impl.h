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

#pragma once

/* Private declarations shared by the GmDocument sources. */

#include "gmdocument.h"
#include "color.h"
#include "documentstyle.h"

#include <the_Foundation/ptrarray.h>
#include <the_Foundation/stringarray.h>
#include <the_Foundation/stringset.h>

iDeclareType(GmLink)

struct Impl_GmLink {
    iString  url;        /* resolved */
    iRangecc urlRange;   /* URL in the source */
    iRangecc labelRange; /* label in the source */
    iRangecc labelIcon;  /* special icon defined in the label text */
    iTime    when;
    int      flags;
};

iDeclareTypeConstruction(GmLink)

iDeclareType(GmPageBands)

struct Impl_GmPageBands {
    iRangecc header;
    iRangecc footer;
};

struct Impl_GmDocument {
    iObject object;
    enum iSourceFormat origFormat;
    enum iSourceFormat viewFormat; /* what the user prefers to see */
    enum iSourceFormat format;
    iString   origSource; /* original (unnormalized) source */
    iString   source;     /* normalized (possibly converted) source */
    iString   url;        /* for resolving relative links */
    iString   mimeType;
    iString   localHost;
    iScopeKey scopeKey;
    iDocPrefs docPrefs;   /* resolved for the URL and MIME type */
    iInt2     size;
    int       contentWidth; /* some runs may extend past the requested width */
    iGmDocumentMetrics metrics;
    int       wrapWidth;    /* if non-zero, overrides the line wrap width */
    iArray    layout;     /* contents of source, laid out in document space */
    iStringArray auxText; /* generated text that appears on page but is not part of the source */
    iPtrArray links;
    iString   title; /* the first top-level title */
    iArray    headings;
    iArray    pageBands; /* GmPageBands of each page in a paginated layout */
    iArray    preMeta; /* metadata about preformatted blocks */
    iDocumentStyle style; /* set up for the current layout */
    int       ansiEscapes;
    uint32_t  themeSeed;
    uint32_t  layoutFontKey; /* fonts used for the current layout */
    iChar     siteIcon;
    iMedia *  media;
    iStringSet *openURLs; /* currently open URLs for highlighting links */
    int       warnings;
    iColor    palette[tmMax_ColorId]; /* copy of the color palette */
    struct {
        iBool enableCommandLinks : 1; /* `about:command?` only allowed on selected pages */
        iBool isSpartan : 1;
        iBool isNex : 1;
        iBool isLayoutInvalidated : 1;
        iBool isPaletteValid : 1;
        iBool isGopherMenu : 1;
        iBool isConvertedMarkdown : 1;
        iBool isCoverPage : 1;
        iBool isDocPrefsPinned : 1;
    } flags;
};

/* gmdocument.c */
iBool                   isForcedMonospace_GmDocument_   (const iGmDocument *);
iBool                   isGopher_GmDocument_            (const iGmDocument *);
iBool                   shouldBeNormalized_GmDocument_  (const iGmDocument *);
enum iGmLineType        lineType_GmDocument_            (const iGmDocument *, const iRangecc line);
iRangecc                addLink_GmDocument_             (iGmDocument *, iRangecc line, iGmLinkId *linkId);
void                    clearLinks_GmDocument_          (iGmDocument *);
void                    updateOpenURLs_GmDocument_      (iGmDocument *);
