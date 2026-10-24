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

#include "ui/util.h"

iDeclareType(DocumentWidget)
iDeclareType(Widget)

/* Floating toolbar in the top right corner of the document, for the tab's view settings. */

iDeclareType(ViewToolbar)
iDeclareTypeConstruction(ViewToolbar)

struct Impl_ViewToolbar {
    iDocumentWidget *owner;
    iWidget *        widget;         /* child of the owner; NULL if not available */
    iAnim            opacity;
    iBool            isScrolledAway; /* dimmed after scrolling down */
    iBool            wasAvailable;   /* during the previous arrangement */
    iBool            isCompact;      /* icons only */
    int              fullWidth;      /* with labels */
    iRect            headerArea;     /* hovering reveals the toolbar */
    int              shownBarsEdge;  /* phone's bottom bars when settled while shown; 0 if unknown */
    iBool            followsScroll;  /* being scrolled by the user; programmatic jumps are ignored */
    int              ownerWidth;     /* during the previous arrangement */
    int              top;            /* in window coordinates; kept even if the owner moves */
    int              barsPush;       /* paged: offset below the top reading bars, also while hidden */
    iRect            nativeMenuRect; /* identity button as given to the native menu; zero if unusable */
};

void    setOwner_ViewToolbar    (iViewToolbar *, iDocumentWidget *owner);
void    update_ViewToolbar      (iViewToolbar *); /* button states */
void    updateHover_ViewToolbar (iViewToolbar *); /* after the mouse moves */
void    scrollBegan_ViewToolbar (iViewToolbar *, int offset, iBool isAtTop);
void    reveal_ViewToolbar      (iViewToolbar *); /* undo dimming by scrolling */
void    arrange_ViewToolbar     (iViewToolbar *); /* position and opacity, before drawing */
int     bottomSpace_ViewToolbar (const iViewToolbar *); /* reserved under the page's footer */
