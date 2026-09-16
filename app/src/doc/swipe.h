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
#include <SDL3/SDL_events.h>

iDeclareType(Banner)
iDeclareType(DocumentView)
iDeclareType(DocumentWidget)

enum iWheelSwipeState {
    none_WheelSwipeState,
    direct_WheelSwipeState,
};

enum iDocumentSwipeFlag {
    navigable_DocumentSwipeFlag         = iBit(1), /* responds to touch swipes (or Mac trackpad) */
    begun_DocumentSwipeFlag             = iBit(2), /* a swipe is ongoing; swipe events affect
                                                      view offset */
    aborted_DocumentSwipeFlag           = iBit(3), /* swipe was finished by returning back to
                                                      the beginning */
    deferredFinish_DocumentSwipeFlag    = iBit(4), /* keep the view even after the animation
                                                      has finished */
    rubberband_DocumentSwipeFlag        = iBit(5),
    viewOverlay_DocumentSwipeFlag       = iBit(6), /* the outgoing view is drawn over the
                                                      actual view */
    viewWasSwipedAway_DocumentSwipeFlag = iBit(7), /* view has been swiped away and should be
                                                      drawn as empty placeholder */
    leftWheel_DocumentSwipeFlag         = iBit(8), /* swipe state flags are used on desktop */
    rightWheel_DocumentSwipeFlag        = iBit(9),
    eitherWheel_DocumentSwipeFlag       = leftWheel_DocumentSwipeFlag |
                                          rightWheel_DocumentSwipeFlag,
    pinchZoom_DocumentSwipeFlag         = iBit(10),
};

iDeclareType(DocumentSwipe)
iDeclareTypeConstruction(DocumentSwipe)

struct Impl_DocumentSwipe {
    iDocumentWidget      *owner;
    iDocumentView        *view;   /* outgoing old view (not owned) */
    iBanner              *banner; /* used by the outgoing view only (not owned) */
    int                   flags;
    int                   pinchZoomInitial;
    int                   pinchZoomPosted;
    float                 speed; /* points/sec */
    uint32_t              lastTime;
    int                   wheelDistance;
    enum iWheelSwipeState wheelState;
    iAnim                 offset; /* applies to both views */
};

void    setOwner_DocumentSwipe      (iDocumentSwipe *, iDocumentWidget *owner);

void    reset_DocumentSwipe         (iDocumentSwipe *);
void    finishWheelSwipe_DocumentSwipe(iDocumentSwipe *, iBool aborted);
int     sidebarAreaHeight_DocumentSwipe(const iDocumentSwipe *);
void    abort_DocumentSwipe         (iDocumentSwipe *);
void    maybeFinish_DocumentSwipe   (iDocumentSwipe *);
iBool   isSwipingBack_DocumentSwipe (const iDocumentSwipe *);
int     offsetOfView_DocumentSwipe  (const iDocumentSwipe *, const iDocumentView *);

iBool   handlePinch_DocumentSwipe   (iDocumentSwipe *, const char *cmd);
iBool   handleEdgeSwipe_DocumentSwipe(iDocumentSwipe *, const char *cmd);
iBool   handleWheelSwipe_DocumentSwipe(iDocumentSwipe *, const SDL_MouseWheelEvent *);

/* The outgoing view and banner are handed over when the document is replaced mid-swipe. */
void    takeOutgoing_DocumentSwipe  (iDocumentSwipe *, iDocumentView *view, iBanner *banner);

/* Reuse the current view for the animation; it keeps its own banner. */
void    setOutgoingView_DocumentSwipe(iDocumentSwipe *, iDocumentView *view);

iLocalDef iDocumentView *   view_DocumentSwipe      (const iDocumentSwipe *d) { return d->view; }
iLocalDef iBanner *         banner_DocumentSwipe    (const iDocumentSwipe *d) { return d->banner; }
iLocalDef const iAnim *     offset_DocumentSwipe    (const iDocumentSwipe *d) { return &d->offset; }

iLocalDef enum iWheelSwipeState wheelState_DocumentSwipe(const iDocumentSwipe *d) {
    return d->wheelState;
}
iLocalDef iBool isOverlay_DocumentSwipe(const iDocumentSwipe *d) {
    return (d->flags & viewOverlay_DocumentSwipeFlag) != 0;
}
iLocalDef iBool isBegun_DocumentSwipe(const iDocumentSwipe *d) {
    return (d->flags & begun_DocumentSwipeFlag) != 0;
}
iLocalDef iBool isAborted_DocumentSwipe(const iDocumentSwipe *d) {
    return (d->flags & aborted_DocumentSwipeFlag) != 0;
}
iLocalDef int pinchZoomPercent_DocumentSwipe(const iDocumentSwipe *d) {
    return d->pinchZoomPosted;
}
iLocalDef void setOffset_DocumentSwipe(iDocumentSwipe *d, int offset) {
    setValue_Anim(&d->offset, offset, 0);
}
iLocalDef iBool isPinchZooming_DocumentSwipe(const iDocumentSwipe *d) {
    return (d->flags & pinchZoom_DocumentSwipeFlag) != 0;
}
iLocalDef iBool wasViewSwipedAway_DocumentSwipe(const iDocumentSwipe *d) {
    return (d->flags & viewWasSwipedAway_DocumentSwipeFlag) != 0;
}
iLocalDef void setViewSwipedAway_DocumentSwipe(iDocumentSwipe *d, iBool swipedAway) {
    iChangeFlags(d->flags, viewWasSwipedAway_DocumentSwipeFlag, swipedAway);
}
iLocalDef void setOverlay_DocumentSwipe(iDocumentSwipe *d, iBool overlay) {
    iChangeFlags(d->flags, viewOverlay_DocumentSwipeFlag, overlay);
}
iLocalDef void setNavigable_DocumentSwipe(iDocumentSwipe *d, iBool navigable) {
    iChangeFlags(d->flags, navigable_DocumentSwipeFlag, navigable);
}
iLocalDef iBool isNavigable_DocumentSwipe(const iDocumentSwipe *d) {
    return (d->flags & navigable_DocumentSwipeFlag) != 0;
}
