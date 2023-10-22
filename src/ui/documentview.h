/* Copyright 2023 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "../gmdocument.h"
#include "util.h"
#include "visbuf.h"
#include <the_Foundation/ptrset.h>

iDeclareType(Banner)
iDeclareType(DocumentView)
iDeclareType(DocumentWidget)
iDeclareType(DrawBufs)
iDeclareType(VisBufMeta)

enum iDocumentViewFlags {
    centerVertically_DocumentViewFlag = iBit(1),
};

enum iDrawBufsFlag {
    updateSideBuf_DrawBufsFlag      = iBit(1),
    updateTimestampBuf_DrawBufsFlag = iBit(2),
};

struct Impl_DocumentView {
    iDocumentWidget *owner;         /* TODO: Convert to an abstract provider of metrics? */
    iBanner *       banner;
    iRangecc *      selectMark;     /* TODO: Should View own these? */
    iRangecc *      foundMark;
    int             flags;
    iGmDocument *   doc;
    int             pageMargin;
    iSmoothScroll   scrollY;
    iBool           userHasScrolled;
    iAnim           sideOpacity;
    iAnim           altTextOpacity;
    iGmRunRange     visibleRuns;
    iPtrArray       visibleLinks;
    iPtrArray       visiblePre;
    iPtrArray       visibleMedia;    /* currently playing audio / ongoing downloads */
    iPtrArray       visibleWideRuns; /* scrollable blocks; TODO: merge into `visiblePre` */
    const iGmRun *  hoverPre;        /* for clicking */
    const iGmRun *  hoverAltPre;     /* for drawing alt text */
    const iGmRun *  hoverLink;
    iArray          wideRunOffsets;
    iAnim           animWideRunOffset;
    uint16_t        animWideRunId;
    iGmRunRange     animWideRunRange;
    iDrawBufs *     drawBufs; /* dynamic state for drawing */
    iVisBuf *       visBuf;
    iVisBufMeta *   visBufMeta;
    iGmRunRange     renderRuns;
    iPtrSet *       invalidRuns;
};

iDeclareTypeConstruction(DocumentView)

void    setOwner_DocumentView           (iDocumentView *, iDocumentWidget *doc);
void    swap_DocumentView               (iDocumentView *, iDocumentView *swapBuffersWith); /* TODO: Remove this! */
void    allocVisBuffer_DocumentView     (const iDocumentView *);

void    invalidate_DocumentView         (iDocumentView *);
void    invalidateLink_DocumentView     (iDocumentView *, iGmLinkId id);
void    invalidateVisibleLinks_DocumentView(iDocumentView *);
void    documentRunsInvalidated_DocumentView(iDocumentView *);
void    updateVisible_DocumentView      (iDocumentView *);
void    updateHover_DocumentView        (iDocumentView *, iInt2 mouse);
void    updateHoverLinkInfo_DocumentView(iDocumentView *);
void    updateSideOpacity_DocumentView  (iDocumentView *, iBool isAnimated);
void    updateDrawBufs_DocumentView     (iDocumentView *, int drawBufsFlags);
iBool   updateWidth_DocumentView        (iDocumentView *);
iBool   updateDocumentWidthRetainingScrollPosition_DocumentView (iDocumentView *, iBool keepCenter);
int     updateScrollMax_DocumentView    (iDocumentView *);
void    clampScroll_DocumentView        (iDocumentView *);
void    immediateScroll_DocumentView    (iDocumentView *, int offset);
void    smoothScroll_DocumentView       (iDocumentView *, int offset, int duration);
void    scrollTo_DocumentView           (iDocumentView *, int documentY, iBool centered);
void    scrollToHeading_DocumentView    (iDocumentView *, const char *heading);
void    resetScroll_DocumentView        (iDocumentView *);
void    resetScrollPosition_DocumentView(iDocumentView *, float normScrollY);
iBool   isWideBlockScrollable_DocumentView(const iDocumentView *, const iRect docBounds, const iGmRun *run);
iBool   scrollWideBlock_DocumentView    (iDocumentView *, iInt2 mousePos, int delta, int duration, iBool *isAtEnd_out);
void    resetWideRuns_DocumentView      (iDocumentView *);
void    invalidateAndResetWideRunsWithNonzeroOffset_DocumentView(iDocumentView *);

int     documentWidth_DocumentView      (const iDocumentView *);
iRect   documentBounds_DocumentView     (const iDocumentView *);
int     documentTopPad_DocumentView     (const iDocumentView *);
int     pageHeight_DocumentView         (const iDocumentView *);
iBool   isCoveringTopSafeArea_DocumentView(const iDocumentView *);
int     viewPos_DocumentView            (const iDocumentView *);
iRangei visibleRange_DocumentView       (const iDocumentView *);
float   normScrollPos_DocumentView      (const iDocumentView *);
iRangecc sourceLoc_DocumentView         (const iDocumentView *, iInt2 pos);
iRect   runRect_DocumentView            (const iDocumentView *, const iGmRun *run);
const iGmRun *lastVisibleLink_DocumentView(const iDocumentView *);
size_t  visibleLinkOrdinal_DocumentView (const iDocumentView *, iGmLinkId linkId);

uint32_t lastRenderTime_DocumentView    (const iDocumentView *);
void    prerender_DocumentView          (iAny *); /* ticker */
void    draw_DocumentView               (const iDocumentView *, int horizOffset);
