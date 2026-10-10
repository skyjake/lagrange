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

#include "documentview.h"

/* Paginated mode of DocumentView. The document is still a vertical strip of runs, but cut
   into columns of the view's height. A page has one or two columns, and a spread of one or
   two consecutive pages is shown side by side, so the visible document range remains
   contiguous. */

iBool   isPageViewAllowed_DocumentView  (const iDocumentView *); /* content may be paginated */
iBool   wantsPaged_DocumentView         (const iDocumentView *); /* prefs and content call for pages */
iBool   isPaged_DocumentView            (const iDocumentView *); /* current layout is paginated */
iRect   pageViewport_DocumentView       (const iDocumentView *); /* where the pages are shown */
int     pagedDocumentWidth_DocumentView (const iDocumentView *);
void    pagedMetrics_DocumentView       (const iDocumentView *, iGmDocumentMetrics *metrics);
int     numColumns_DocumentView         (const iDocumentView *); /* in a spread */
int     numPages_DocumentView           (const iDocumentView *); /* in a spread */
int     spreadWidth_DocumentView        (const iDocumentView *);
uint32_t pageTurnDuration_DocumentView (const iDocumentView *); /* depends on the animation style and width */
uint32_t beginPageTurn_DocumentView    (iDocumentView *, uint32_t duration); /* returns adapted duration */
void    settlePageTurn_DocumentView     (iDocumentView *); /* ongoing turn jumps to the end */
iBool   isCurling_DocumentView          (const iDocumentView *); /* curl animation is ongoing */
float   curlProgress_DocumentView       (const iDocumentView *); /* from the earlier spread to the later one */
size_t  currentColumn_DocumentView      (const iDocumentView *); /* first of the spread */
float   columnPosition_DocumentView     (const iDocumentView *); /* fractional during turns */
iRangei visibleColumns_DocumentView     (const iDocumentView *); /* includes an ongoing turn */
int     columnLeft_DocumentView         (const iDocumentView *, size_t column); /* window X of the column's left edge */
int     columnShift_DocumentView        (const iDocumentView *, size_t column); /* of the content from the column's left edge */
iInt2   columnOrigin_DocumentView       (const iDocumentView *, size_t column); /* window coords of the column's document top left */
iRangei columnSpan_DocumentView         (const iDocumentView *, size_t column); /* window X range the column covers */
iRect   pageTextRect_DocumentView       (const iDocumentView *, size_t page); /* window rect of the page's text */
int     pageHeaderTextTop_DocumentView  (const iDocumentView *); /* from the top of the viewport */
int     pageFooterTextTop_DocumentView  (const iDocumentView *); /* from the top of the viewport */
iInt2   pagedDocumentPos_DocumentView   (const iDocumentView *, iInt2 windowPos);
iRect   pagedRunRect_DocumentView       (const iDocumentView *, const iGmRun *run);
void    allocPagedVisBuffer_DocumentView(const iDocumentView *);

size_t  anchor_DocumentView             (const iDocumentView *); /* source offset of the reading position */
void    setAnchor_DocumentView          (iDocumentView *, size_t sourceOffset); /* for the next relayout */
float   sourcePosition_DocumentView     (const iDocumentView *); /* anchor as a fraction of the source */
void    showSourcePosition_DocumentView (iDocumentView *, float pos); /* independent of layout */
void    resetPage_DocumentView          (iDocumentView *);
void    animatePageTurn_DocumentView    (iAny *); /* ticker */
void    showColumn_DocumentView         (iDocumentView *, size_t column, iBool isAnimated); /* animated if adjacent */
void    showDocumentY_DocumentView      (iDocumentView *, int documentY); /* page that contains the point */
iBool   turnPage_DocumentView           (iDocumentView *, int dir); /* false if no more pages */
int     pageTurnZone_DocumentView       (const iDocumentView *, iInt2 windowPos); /* -1, 0, +1 */
void    dragPage_DocumentView           (iDocumentView *, int offset); /* from the start of the gesture */
void    endPageDrag_DocumentView        (iDocumentView *, iBool isCancelled);
iBool   isEdgeArmed_DocumentView        (const iDocumentView *, int dir); /* swiped past the edge before */
void    disarmEdge_DocumentView         (iDocumentView *);
void    wheelPage_DocumentView          (iDocumentView *, float amount); /* positive: backward */

void    finishPageTurn_Anim             (iAnim *, float to, float gestureSpeed, uint32_t lastMoveTime, uint32_t maxSpan); /* after a gesture */
