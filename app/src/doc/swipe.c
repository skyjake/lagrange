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
#include "impl.h"
#include "pagedview.h"

#include "../app.h"
#include "../history.h"
#include "banner.h"
#include "documentview.h"
#include "query.h"
#include "render/paint.h"
#include "ui/command.h"
#include "ui/touch.h"
#include "ui/widget.h"
#include "ui/window.h"

#include <SDL3/SDL_timer.h>

iDefineTypeConstruction(DocumentSwipe)

static iDocumentView *ownerView_(const iDocumentSwipe *d) {
    return d->owner->view;
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
    const iRangei span = sidebarAreaSpan_DocumentSwipe(d);
    return prefs_App()->bottomNavBar ? swipeY > span.start : swipeY < span.end;
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
    d->speed            = 0.0f;
    d->lastTime         = 0;
    d->curlSpeed        = 0.0f;
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
    resetAfterSwipe_Query(d->owner->query);
    setValue_Anim(&d->offset, 0, 0);
    iChangeFlags(d->flags,
                 viewOverlay_DocumentSwipeFlag | aborted_DocumentSwipeFlag |
                     deferredFinish_DocumentSwipeFlag | rubberband_DocumentSwipeFlag |
                     pageCurl_DocumentSwipeFlag,
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

void settle_DocumentSwipe(iDocumentSwipe *d) {
    if (!d->view || isFinished_Anim(&d->offset) ||
        d->flags & (begun_DocumentSwipeFlag | aborted_DocumentSwipeFlag |
                    rubberband_DocumentSwipeFlag)) {
        return;
    }
    init_Anim(&d->offset, targetValue_Anim(&d->offset));
    maybeFinish_DocumentSwipe(d);
    refresh_Widget(d->owner);
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

int sidebarAreaHeight_DocumentSwipe(const iDocumentSwipe *d) {
    const iWindow *win = get_Window();
    return iMin(win->size.x, win->size.y) / 4;
}

iRangei sidebarAreaSpan_DocumentSwipe(const iDocumentSwipe *d) {
    /* At the end nearer to the navbar. The document may extend under the safe area
       (e.g., when the reading bars are hidden). */
    const iWidget *w      = constAs_Widget(d->owner);
    const iRect    bounds = bounds_Widget(w);
    const iRect    safe   = safeRect_Root(w->root);
    const int      hgt    = sidebarAreaHeight_DocumentSwipe(d);
    if (prefs_App()->bottomNavBar) {
        const int bottom = iMin(bottom_Rect(bounds), bottom_Rect(safe));
        return (iRangei){ bottom - hgt, bottom };
    }
    const int top = iMax(top_Rect(bounds), top_Rect(safe));
    return (iRangei){ top, top + hgt };
}

iBool handleSidebarSwipe_DocumentSwipe(iDocumentSwipe *d, const char *cmd) {
    /* Edge swipes can also be used to show the sidebars. */
    const int edge = argLabel_Command(cmd, "edge");
    if ((deviceType_App() == tablet_AppDeviceType || isLandscapePhone_App()) &&
        edge &&
        checkTabletVerticalPosition_DocumentSwipe_(d, argLabel_Command(cmd, "y"), edge)) {
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
    return iFalse;
}

static iBool finishCurl_DocumentSwipe_(iDocumentSwipe *d, int to) {
    /* A page curl finishes like a page turn. */
    if (d->flags & pageCurl_DocumentSwipeFlag) {
        finishPageTurn_Anim(&d->offset,
                            to,
                            d->curlSpeed,
                            d->lastTime,
                            pageTurnDuration_DocumentView(ownerView_(d)));
        return iTrue;
    }
    return iFalse;
}

iBool handleEdgeSwipe_DocumentSwipe(iDocumentSwipe *d, const char *cmd) {
    iWidget *w = as_Widget(d->owner);
    if (!prefs_App()->edgeSwipe &&
        startsWith_Command(cmd, "edgeswipe.") && argLabel_Command(cmd, "edge")) {
        return iFalse;
    }
    if (equal_Command(cmd, "edgeswipe.moved")) {
        if (handleSidebarSwipe_DocumentSwipe(d, cmd)) {
            return iTrue;
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
            if (d->flags & pageCurl_DocumentSwipeFlag) {
                const uint32_t now = SDL_GetTicks();
                const uint32_t dt  = now - d->lastTime;
                if (dt > 0) {
                    const float speed = (offset - targetValue_Anim(&d->offset)) * 1000.0f / dt;
                    /* After a pause, only the latest movement counts. */
                    d->curlSpeed = dt > 100 ? speed : (d->curlSpeed + speed) / 2;
                    d->lastTime  = now;
                }
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
                    if (!finishCurl_DocumentSwipe_(d, width_Widget(w))) {
                        setValue_Anim(&d->offset, width_Widget(w), 100);
                    }
                    animate_DocumentWidget(d->owner);
                    return iTrue;
                }
            }
            if (!finishCurl_DocumentSwipe_(d, 0)) {
                setFlags_Anim(&d->offset, easeOut_AnimFlag, iTrue);
                setValue_Anim(&d->offset, 0, 150);
            }
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
                if (!finishCurl_DocumentSwipe_(d, 0)) {
                    setValue_Anim(&d->offset, 0, 100);
                }
                animate_DocumentWidget(d->owner);
                return iTrue;
            }
            if (!finishCurl_DocumentSwipe_(d, width_Widget(w))) {
                setFlags_Anim(&d->offset, easeOut_AnimFlag, iTrue);
                setValue_Anim(&d->offset, width_Widget(w), 150);
            }
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
        notify_Widget(d->owner, "edgeswipe.ended wheel:1 side:%d abort:%d", side, abort);
        d->flags &= ~eitherWheel_DocumentSwipeFlag;
        d->wheelState = none_WheelSwipeState;
    }
}

iBool handleWheelSwipe_DocumentSwipe(iDocumentSwipe *d, const SDL_MouseWheelEvent *ev) {
    iWidget *w = as_Widget(d->owner);
    if (!isPaged_DocumentView(ownerView_(d)) &&
        (~d->flags & navigable_DocumentSwipeFlag || !prefs_App()->pageSwipe)) {
        return iFalse; /* paginated documents always turn pages */
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

static void drawViewOrBlank_DocumentSwipe_(const iDocumentSwipe *d, const iDocumentView *view,
                                           int offset, iBool isBlank) {
    iRect bounds = bounds_Widget(constAs_Widget(d->owner));
    if (view && !isBlank) {
        draw_DocumentView(view, offset);
    }
    else {
        if (isCoveringTopSafeArea_DocumentView(d->owner->view)) {
            adjustEdges_Rect(&bounds, -top_Rect(bounds), 0, 0, 0);
        }
        iPaint p;
        init_Paint(&p);
        fillRect_Paint(&p,
                       intersect_Rect(moved_Rect(bounds, init_I2(offset, 0)), bounds),
                       uiBackground_ColorId);
        drawLogo_MainWindow(get_MainWindow(), moved_Rect(bounds, init_I2(offset, 0)));
    }
}

static iBool drawCurl_DocumentSwipe_(const iDocumentSwipe *d) {
    /* The outgoing and the incoming chapter are both paginated. */
    if (!isCurling_DocumentSwipe(d)) {
        return iFalse;
    }
    const iDocumentView *current  = d->owner->view;
    const iDocumentView *outgoing = d->view;
    if (outgoing == current) {
        draw_DocumentView(outgoing, 0); /* the next chapter is still loading */
        return iTrue;
    }
    const iBool          isBack  = isOverlay_DocumentSwipe(d);
    const iDocumentView *earlier = isBack ? current : outgoing;
    const iDocumentView *later   = isBack ? outgoing : current;
    if (!isPaged_DocumentView(earlier) || !isPaged_DocumentView(later)) {
        return iFalse;
    }
    drawCurlBetween_DocumentView(
        earlier,
        later,
        1.0f - value_Anim(&d->offset) / (float) width_Widget(d->owner));
    return iTrue;
}

void draw_DocumentSwipe(const iDocumentSwipe *d) {
    const iDocumentView *view       = d->owner->view;
    const iDocumentView *swipeView  = d->view;
    iQuery              *query      = d->owner->query;
    const iRect          bounds     = bounds_Widget(constAs_Widget(d->owner));
    const iRect          clipBounds = bounds;
    iPaint p;
    init_Paint(&p);
    if (drawCurl_DocumentSwipe_(d)) {
        /* Drawn as a page turn. */
    }
    else if (swipeView) {
        const int swipeOffset    = value_Anim(&d->offset);
        const int underlayOffset = 0.25f * (swipeOffset - width_Rect(bounds));
        const int overlayOffset  = swipeOffset;
        const iDocumentView *under, *over;
        if (isOverlay_DocumentSwipe(d)) {
            over  = swipeView;
            under = view;
            if (over == under) {
                under = NULL;
            }
        }
        else {
            over  = view;
            under = swipeView;
            if (over == under) {
                over = NULL;
            }
        }
        drawViewOrBlank_DocumentSwipe_(d, under, underlayOffset, iFalse);
        if (under == swipeView) {
            drawOutgoing_Query(query, &clipBounds);
        }
        else if (under == view) {
            drawCovered_Query(query, &clipBounds);
        }
        if (overlayOffset > 0) {
            /* Dim the occluded view with a soft shadow. */
            iRect safeBounds = bounds;
            if (isCoveringTopSafeArea_DocumentView(view)) {
                adjustEdges_Rect(&safeBounds, -top_Rect(safeBounds), 0, 0, 0);
            }
            SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
            iRect dimRect = initCorners_Rect(
                topLeft_Rect(safeBounds),
                addX_I2(bottomLeft_Rect(safeBounds), overlayOffset)
            );
            setClip_Paint(&p, dimRect);
            const float relativeOffset =
                iClamp(swipeOffset / (float) width_Rect(safeBounds), 0, 1);
            const float darkness = isDark_ColorTheme(prefs_App()->theme) ? 0.4f : 0.25f;
            drawSoftShadow_Paint(&p, moved_Rect(safeBounds, init_I2(overlayOffset, 0)),
                                 gap_UI * 80,
                                 black_ColorId,
                                 255 * darkness * (1.0f - relativeOffset));
            unsetClip_Paint(&p);
            SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
        }
        drawViewOrBlank_DocumentSwipe_(d, over, overlayOffset, iFalse);
        if (over == swipeView) {
            drawOutgoing_Query(query, &clipBounds);
        }
    }
    else {
        int offset = value_Anim(&d->offset);
        if (offset && isCoveringTopSafeArea_DocumentView(view)) {
            /* Blank the safe area as well. */
            fillRect_Paint(&p,
                           initCorners_Rect(zero_I2(), topRight_Rect(bounds)),
                           uiBackground_ColorId);
        }
        drawViewOrBlank_DocumentSwipe_(d, view, offset, wasViewSwipedAway_DocumentSwipe(d));
        if (offset) {
            /* Due to offset, inline prompts may shift outside the document area. */
            drawCovered_Query(query, &clipBounds);
        }
    }
    /* The outgoing view may have been drawn last; widgets drawn after use the current palette. */
    makePaletteGlobal_GmDocument(view->doc);
}

void drawSidebarIndicator_DocumentSwipe(const iDocumentSwipe *d) {
    const iWidget *w      = constAs_Widget(d->owner);
    const iRect    bounds = bounds_Widget(w);
    iWindow       *win    = get_Window();
    const int      gap    = 4 * win->pixelRatio; /* not dependent on UI scaling; cf. Home indicator */
    const int      indGap = 6 * gap;
    const int      indMar = 6 * gap / 3;
    const iRangei  span   = sidebarAreaSpan_DocumentSwipe(d);
    const int      indHgt = size_Range(&span) - 2 * indGap;
    const int      indPos = span.start + indGap;
    const int    indThick = 5 * gap / 3;
    iRect indRect = (iRect){ init_I2(left_Rect(bounds) + indMar, indPos),
                             init_I2(indThick, indHgt) };
    iRoot *leftSideRoot  = win->roots[0];
    iRoot *rightSideRoot = (win->roots[1] ? win->roots[1] : win->roots[0]);
    /* TODO: Could look up and save these pointers ahead of time, they don't change. */
    iWidget *sbar[2] = {
        findChild_Widget(leftSideRoot->widget, "sidebar"),
        findChild_Widget(rightSideRoot->widget, "sidebar2")
    };
    iPaint p;
    init_Paint(&p);
    SDL_SetRenderDrawBlendMode(renderer_Window(win), SDL_BLENDMODE_BLEND);
    p.alpha = isDark_ColorTheme(prefs_App()->theme) ? 0x60 : 0x80;
    if (w->root == leftSideRoot && !isVisible_Widget(sbar[0])) {
        fillRect_Paint(&p, indRect, tmQuoteIcon_ColorId);
    }
    if (w->root == rightSideRoot && !isVisible_Widget(sbar[1])) {
        indRect.pos.x = right_Rect(bounds) - indMar - indThick;
        fillRect_Paint(&p, indRect, tmQuoteIcon_ColorId);
    }
    SDL_SetRenderDrawBlendMode(renderer_Window(win), SDL_BLENDMODE_NONE);
}
