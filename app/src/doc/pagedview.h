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
   into pages of the view's height. A spread of one or two consecutive pages is shown side
   by side, so the visible document range remains contiguous. */

iBool   wantsPaged_DocumentView         (const iDocumentView *); /* prefs and content call for pages */
iBool   isPaged_DocumentView            (const iDocumentView *); /* current layout is paginated */
iRect   pageViewport_DocumentView       (const iDocumentView *); /* where the pages are shown */
int     pagedDocumentWidth_DocumentView (const iDocumentView *);
void    pagedMetrics_DocumentView       (const iDocumentView *, iGmDocumentMetrics *metrics);
int     numColumns_DocumentView         (const iDocumentView *);
size_t  currentPage_DocumentView        (const iDocumentView *);
iRangei visiblePages_DocumentView       (const iDocumentView *); /* includes an ongoing turn */
int     pageLeft_DocumentView           (const iDocumentView *, size_t page); /* window X of the page's left edge */
iInt2   pageOrigin_DocumentView         (const iDocumentView *, size_t page); /* window coords of the page's document top left */
iInt2   pagedDocumentPos_DocumentView   (const iDocumentView *, iInt2 windowPos);
iRect   pagedRunRect_DocumentView       (const iDocumentView *, const iGmRun *run);
void    allocPagedVisBuffer_DocumentView(const iDocumentView *);

void    resetPage_DocumentView          (iDocumentView *);
void    animatePageTurn_DocumentView    (iAny *); /* ticker */
void    showPage_DocumentView           (iDocumentView *, size_t page, iBool isAnimated); /* animated if adjacent */
void    showDocumentY_DocumentView      (iDocumentView *, int documentY); /* page that contains the point */
iBool   turnPage_DocumentView           (iDocumentView *, int dir); /* false if no more pages */
int     pageTurnZone_DocumentView       (const iDocumentView *, iInt2 windowPos); /* -1, 0, +1 */
