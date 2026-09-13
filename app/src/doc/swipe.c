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

#include "swipe.h"

#include "../app.h"
#include "../history.h"
#include "banner.h"
#include "documentview.h"
#include "inputprompts.h"
#include "ui/command.h"
#include "ui/documentwidget.h"
#include "ui/touch.h"
#include "ui/widget.h"
#include "ui/window.h"

#include <SDL_timer.h>

iDefineTypeConstruction(DocumentSwipe)

static iDocumentView *ownerView_(const iDocumentSwipe *d) {
    return view_DocumentWidget(d->owner);
}

static iBool checkTabletVerticalPosition_DocumentSwipe_(const iDocumentSwipe *d, int swipeY,
                                                        int edge) {
    /* Returns True if the the vertical position is valid for swiping the sidebar. */
    if (deviceType_App() != tablet_AppDeviceType) {
        return iFalse;
    }
    if (edge == 1 && isVisible_Widget(findWidget_App("sidebar"))) {
        return iFalse;
    }
    if (edge == 2 && isVisible_Widget(findWidget_App("sidebar2"))) {
        return iFalse;
    }
    const iWidget *w = constAs_Widget(d->owner);
    const int sidebarSwipeHgt = sidebarAreaHeight_DocumentSwipe(d);
    if (prefs_App()->bottomNavBar) {
        return swipeY > bottom_Rect(bounds_Widget(w)) - sidebarSwipeHgt;
    }
    else {
        return swipeY < top_Rect(bounds_Widget(w)) + sidebarSwipeHgt;
    }
}

iLocalDef int wheelSide_DocumentSwipe_(const iDocumentSwipe *d) {
    return (d->flags & rightWheel_DocumentSwipeFlag  ? 2
            : d->flags & leftWheel_DocumentSwipeFlag ? 1
                                                     : 0);
}

/*----------------------------------------------------------------------------------------------*/

void init_DocumentSwipe(iDocumentSwipe *d) {
    d->owner            = NULL;
    d->view             = NULL;
    d->banner           = NULL;
    d->flags            = 0;
    d->pinchZoomInitial = 0;
    d->pinchZoomPosted  = 0;
    d->speed            = 0.0f;
    d->lastTime         = 0;
    d->wheelDistance    = 0;
    d->wheelState       = none_WheelSwipeState;
    init_Anim(&d->offset, 0);
}

void deinit_DocumentSwipe(iDocumentSwipe *d) {
    iUnused(d); /* the outgoing view and banner are deleted in reset_DocumentSwipe() */
}

void setOwner_DocumentSwipe(iDocumentSwipe *d, iDocumentWidget *owner) {
    d->owner = owner;
}

void takeOutgoing_DocumentSwipe(iDocumentSwipe *d, iDocumentView *view, iBanner *banner) {
    d->view   = view;
    d->banner = banner;
}

void setOutgoingView_DocumentSwipe(iDocumentSwipe *d, iDocumentView *view) {
    iAssert(d->banner == NULL);
    d->view = view;
}


void reset_DocumentSwipe(iDocumentSwipe *d) {
    if (d->banner) {
        delete_Banner(d->banner);
        d->banner = NULL;
    }
    if (ownerView_(d) != d->view) {
        delete_DocumentView(d->view);
    }
    d->view = NULL;
    resetAfterSwipe_InputPrompts(inputPrompts_DocumentWidget(d->owner));
    setValue_Anim(&d->offset, 0, 0);
    iChangeFlags(d->flags,
                 viewOverlay_DocumentSwipeFlag | aborted_DocumentSwipeFlag |
                     deferredFinish_DocumentSwipeFlag | rubberband_DocumentSwipeFlag,
                 iFalse);
    /* Final positioning. */
    repositionInlinePrompts_DocumentWidget(d->owner, ownerView_(d));
}

void abort_DocumentSwipe(iDocumentSwipe *d) {
    reset_DocumentSwipe(d);
    iChangeFlags(d->flags, viewWasSwipedAway_DocumentSwipeFlag, iFalse);
    refresh_Widget(d->owner);
}

iBool isSwipingBack_DocumentSwipe(const iDocumentSwipe *d) {
    return (d->flags & viewOverlay_DocumentSwipeFlag) != 0;
}

void maybeFinish_DocumentSwipe(iDocumentSwipe *d) {
    if (~d->flags & begun_DocumentSwipeFlag &&
        ~d->flags & rubberband_DocumentSwipeFlag &&
        d->view && isFinished_Anim(&d->offset)) {
        /* When aborting a swipe, we must keep the animation active at the finish until
           the old page has been reloaded. */
        if (d->flags & aborted_DocumentSwipeFlag) {
            if (~d->flags & deferredFinish_DocumentSwipeFlag) {
                const iBool isBack = isSwipingBack_DocumentSwipe(d);
                if (isBack ? atNewest_History(history_DocumentWidget(d->owner)) : atOldest_History(history_DocumentWidget(d->owner))) {
                    /* The undo navigation would do nothing, so no new document is coming and
                       the animation would be left hanging. */
                    reset_DocumentSwipe(d);
                    return;
                }
                d->flags |= deferredFinish_DocumentSwipeFlag;
                postCommand_Widget(d->owner, isBack ? "navigate.forward" : "navigate.back");
            }
        }
        else {
            reset_DocumentSwipe(d);
        }
    }
}

int offsetOfView_DocumentSwipe(const iDocumentSwipe *d, const iDocumentView *view) {
    /* Must match the horizontal offset used to draw `view` in draw_DocumentWidget_(). */
    const int swipeValue = value_Anim(&d->offset);
    if (!d->view) {
        return view == ownerView_(d) ? swipeValue : 0;
    }
    const iBool          isBack = (d->flags & viewOverlay_DocumentSwipeFlag) != 0;
    const iDocumentView *over   = isBack ? d->view : ownerView_(d);
    const iDocumentView *under  = isBack ? ownerView_(d) : d->view;
    if (over == under) {
        return isBack ? swipeValue
                       : (int) (0.25f * (swipeValue - width_Rect(bounds_Widget(constAs_Widget(d->owner)))));
    }
    if (view == over) {
        return swipeValue;
    }
    if (view == under) {
        return 0.25f * (swipeValue - width_Rect(bounds_Widget(constAs_Widget(d->owner))));
    }
    return 0;
}

iBool handlePinch_DocumentSwipe(iDocumentSwipe *d, const char *cmd) {
    if (equal_Command(cmd, "pinch.began")) {
        d->pinchZoomInitial = d->pinchZoomPosted = prefs_App()->zoomPercent;
        d->flags |= pinchZoom_DocumentSwipeFlag;
        refresh_Widget(d->owner);
    }
    else if (equal_Command(cmd, "pinch.moved")) {
        const float rel = argf_Command(cmd);
        int zoom = iRound(d->pinchZoomInitial * rel / 5.0f) * 5;
        zoom = iClamp(zoom, 50, 200);
        if (d->pinchZoomPosted != zoom) {
#if defined (iPlatformAppleMobile)
            if (zoom == 100) {
                playHapticEffect_iOS(tap_HapticEffect);
            }
#endif
            d->pinchZoomPosted = zoom;
            postCommandf_App("zoom.set arg:%d", zoom);
        }
    }
    else if (equal_Command(cmd, "pinch.ended")) {
        d->flags &= ~pinchZoom_DocumentSwipeFlag;
        refresh_Widget(d->owner);
    }
    return iTrue;
}

int sidebarAreaHeight_DocumentSwipe(const iDocumentSwipe *d) {
    const iWindow *win = get_Window();
    return iMin(win->size.x, win->size.y) / 4;
}

iBool handleEdgeSwipe_DocumentSwipe(iDocumentSwipe *d, const char *cmd) {
    iWidget *w = as_Widget(d->owner);
    if (!prefs_App()->edgeSwipe &&
        startsWith_CStr(cmd, "edgeswipe.") && argLabel_Command(cmd, "edge")) {
        return iFalse;
    }
    if (equal_Command(cmd, "edgeswipe.moved")) {
        /* Edge swipes can also be used to show the sidebars. */
        const int edge = argLabel_Command(cmd, "edge");
        if ((deviceType_App() == tablet_AppDeviceType || isLandscapePhone_App()) &&
            edge &&
            checkTabletVerticalPosition_DocumentSwipe_(d,
                                                             argLabel_Command(cmd, "y"),
                                                             edge)) {
            /* This is an actual swipe from the edge of the device, we should let the sidebars
               handle it. */
            if (edge == 1) {
                transferAffinity_Touch(NULL, findWidget_App("sidebar"));
                return iTrue;
            }
            else if (edge == 2 && deviceType_App() == tablet_AppDeviceType) {
                transferAffinity_Touch(NULL, findWidget_App("sidebar2"));
                return iTrue;
            }
        }
        const int side = argLabel_Command(cmd, "side");
        int offset = arg_Command(cmd);
        if (~d->flags & begun_DocumentSwipeFlag) {
            if (side == 1) { /* left edge */
                if (atOldest_History(history_DocumentWidget(d->owner))) {
                    d->flags |= begun_DocumentSwipeFlag | rubberband_DocumentSwipeFlag;
                    return iTrue;
                }
            }
            if (side == 2) { /* right edge */
                if (offset < -get_Window()->pixelRatio * 10) {
                    if (atNewest_History(history_DocumentWidget(d->owner))) {
                        d->flags |= begun_DocumentSwipeFlag | rubberband_DocumentSwipeFlag;
                        return iTrue;
                    }
                }
                else {
                    return iTrue;
                }
            }
            SDL_RaiseWindow(window_Widget(w)->win); /* ensure events handled by the right window */
            d->flags |= begun_DocumentSwipeFlag;
            postCommand_Widget(d->owner, side == 1 ? "navigate.back swipe:1" : "navigate.forward swipe:1");
        }
        else if (d->flags & rubberband_DocumentSwipeFlag) {
            setValue_Anim(&d->offset, offset / 6, 10);
            animate_DocumentWidget(d->owner);
        }
        else if (d->view) {
            if (!isSwipingBack_DocumentSwipe(d)) {
                offset = width_Widget(w) + offset;
            }
            setFlags_Anim(&d->offset, easeOut_AnimFlag, iFalse);
            setValue_Anim(&d->offset, offset, 10);
            animate_DocumentWidget(d->owner);
        }
    }
//    const float maxSpeed = gap_UI * 2000;
//    const float minSpeed = gap_UI * 500;
    if (equal_Command(cmd, "edgeswipe.ended")) {
        if (~d->flags & begun_DocumentSwipeFlag && !d->view) {
            /* The swipe was not ours; it was handled by someone else (e.g., a sidebar) or
               rejected in "edgeswipe.moved". Moving the view now would leave it offset with
               nothing to animate it back. */
            return iTrue;
        }
        if (d->flags & rubberband_DocumentSwipeFlag) {
            iChangeFlags(d->flags,
                         rubberband_DocumentSwipeFlag | begun_DocumentSwipeFlag,
                         iFalse);
            setValue_Anim(&d->offset, 0, 100);
            animate_DocumentWidget(d->owner);
            return iTrue;
        }
        if (argLabel_Command(cmd, "side") == 2) {
            iChangeFlags(d->flags, begun_DocumentSwipeFlag, iFalse);
            if (argLabel_Command(cmd, "abort")) {
                if (d->view) {
                    d->flags |= aborted_DocumentSwipeFlag;
                    setValue_Anim(&d->offset, width_Widget(w), 100);
                    animate_DocumentWidget(d->owner);
                    return iTrue;
                }
            }
            setFlags_Anim(&d->offset, easeOut_AnimFlag, iTrue);
            setValue_Anim(&d->offset, 0, 150);
    //        float speed = currentSwipeSpeed_DocumentWidget_(d);
    //        speed = iClamp(speed, minSpeed, maxSpeed);
    //        setValueSpeed_Anim(&d->offset, 0, speed);
            animate_DocumentWidget(d->owner);
            maybeFinish_DocumentSwipe(d);
            stopWidgetMomentum_Touch(w);
        }
        else if (argLabel_Command(cmd, "side") == 1) {
            iChangeFlags(d->flags, begun_DocumentSwipeFlag, iFalse);
            if (argLabel_Command(cmd, "abort") || !d->view) {
                /* Without an outgoing view there is nothing to slide the current view away
                   for, so it must return to its normal position. */
                iChangeFlags(d->flags, aborted_DocumentSwipeFlag, d->view != NULL);
                setValue_Anim(&d->offset, 0, 100);
                animate_DocumentWidget(d->owner);
                return iTrue;
            }
            setFlags_Anim(&d->offset, easeOut_AnimFlag, iTrue);
            setValue_Anim(&d->offset, width_Widget(w), 150);
    //        float speed = currentSwipeSpeed_DocumentWidget_(d);
    //        speed = iClamp(speed, minSpeed, maxSpeed);
    //        setValueSpeed_Anim(&d->offset, width_Widget(w), speed);
            animate_DocumentWidget(d->owner);
            maybeFinish_DocumentSwipe(d);
            stopWidgetMomentum_Touch(w);
        }
        return iTrue;
    }
    return iFalse;
}

void finishWheelSwipe_DocumentSwipe(iDocumentSwipe *d, iBool aborted) {
    if (d->wheelState == direct_WheelSwipeState) {
        const int side = wheelSide_DocumentSwipe_(d);
        int abort = aborted || ((side == 1 && d->speed < 0) || (side == 2 && d->speed > 0));
        if (iAbs(d->wheelDistance) < 4 * gap_UI) {
            //printf("ABORTING: dist:%d speed:%f\n", d->wheelDistance, d->speed);
            abort = 1;
        }
        postCommand_Widget(d->owner, "edgeswipe.ended wheel:1 side:%d abort:%d", side, abort);
        d->flags &= ~eitherWheel_DocumentSwipeFlag;
        d->wheelState = none_WheelSwipeState;
    }
}

iBool handleWheelSwipe_DocumentSwipe(iDocumentSwipe *d, const SDL_MouseWheelEvent *ev) {
    iWidget *w = as_Widget(d->owner);
    if (~d->flags & navigable_DocumentSwipeFlag || !prefs_App()->pageSwipe) {
        return iFalse;
    }
//    printf("STATE:%d wheel x:%d inert:%d end:%d\n", d->wheelState,
//           ev->x, isInertia_MouseWheelEvent(ev),
//           isScrollFinished_MouseWheelEvent(ev));
//    fflush(stdout);
    switch (d->wheelState) {
        case none_WheelSwipeState:
            /* A new swipe starts. */
            if (!isInertia_MouseWheelEvent(ev) && !isScrollFinished_MouseWheelEvent(ev)) {
                int side = ev->x > 0 ? 1 : 2;
                d->wheelDistance = ev->x * 2;
                d->flags &= ~eitherWheel_DocumentSwipeFlag;
                d->flags |= (side == 1 ? leftWheel_DocumentSwipeFlag
                                       : rightWheel_DocumentSwipeFlag);
                //        printf("swipe starts at %d, side %d\n", d->wheelDistance, side);
                d->wheelState = direct_WheelSwipeState;
                d->speed = 0;
                postCommand_Widget(d->owner, "edgeswipe.moved arg:%d side:%d", d->wheelDistance, side);
                return iTrue;
            }
            break;
        case direct_WheelSwipeState:
            if (isInertia_MouseWheelEvent(ev) || isScrollFinished_MouseWheelEvent(ev)) {
                finishWheelSwipe_DocumentSwipe(d, iFalse);
            }
            else {
                int step = ev->x * (isMobile_Platform() ? 1 : 2);
                d->wheelDistance += step;
                /* Remember the maximum speed. */
                if (d->speed < 0 && step < 0) {
                    d->speed = iMin(d->speed, step);
                }
                else if (d->speed > 0 && step > 0) {
                    d->speed = iMax(d->speed, step);
                }
                else {
                    d->speed = step;
                }
                switch (wheelSide_DocumentSwipe_(d)) {
                    case 0:
                        d->wheelDistance = iClamp(d->wheelDistance,
                                                       -width_Widget(d->owner), width_Widget(d->owner));
                        break;
                    case 1:
                        d->wheelDistance = iMax(0, d->wheelDistance);
                        d->wheelDistance = iMin(width_Widget(d->owner), d->wheelDistance);
                        break;
                    case 2:
                        d->wheelDistance = iMin(0, d->wheelDistance);
                        d->wheelDistance = iMax(-width_Widget(d->owner), d->wheelDistance);
                        break;
                }
                /* TODO: calculate speed, remember direction */
                //printf("swipe moved to %d, side %d\n", d->wheelDistance, side);
                postCommand_Widget(d->owner, "edgeswipe.moved arg:%d side:%d", d->wheelDistance,
                                   wheelSide_DocumentSwipe_(d));
            }
            return iTrue;
    }
    return iFalse;
}