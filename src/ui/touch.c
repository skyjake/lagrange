/* Copyright 2021 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "touch.h"
#include "window.h"
#include "app.h"

#include <the_Foundation/array.h>
#include <the_Foundation/math.h>
#include <SDL_timer.h>

#if defined (iPlatformAppleMobile)
#   include "../ios.h"
#endif

iDeclareType(Momentum)
iDeclareType(Pinch)
iDeclareType(Touch)
iDeclareType(TouchState)

#define numHistory_Touch_   5
#define lastIndex_Touch_    (numHistory_Touch_ - 1)

static const uint32_t longPressSpanMs_  = 500;
static const uint32_t shortPressSpanMs_ = 250;
#if defined (iPlatformAndroidMobile)
static const int      tapRadiusPt_      = 30; /* inaccurate sensors? */
#else
static const int      tapRadiusPt_      = 10;
#endif

enum iTouchEdge {
    none_TouchEdge,
    left_TouchEdge,
    right_TouchEdge,
};

enum iTouchAxis {
    none_TouchAxis,
    x_TouchAxis,
    y_TouchAxis
};

struct Impl_Touch {
    SDL_FingerID id;
    iWidget *affinity; /* widget on which the touch started */
    iBool hasMoved;
    iBool isTapBegun;
    iBool isLeftDown;
    iBool isTouchDrag;
    iBool isTapAndHold;
    iBool didPostEdgeMove;
    iBool didBeginOnTouchDrag;
    int pinchId;
    enum iTouchEdge edge;
    uint32_t startTime;
    iFloat3 startPos;
    enum iTouchAxis axis;
    uint32_t posTime[numHistory_Touch_];
    iFloat3 pos[numHistory_Touch_];
    size_t posCount;
    iFloat3 accum;
    iInt2 pendingScroll[3]; /* SDL_FINGERMOTION sometimes arrives in clumps on iOS;
                               buffer the scrolls to post more evenly */
    int numPendingScroll;
    int pendingScrollThreshold;
};

iLocalDef void pushPos_Touch_(iTouch *d, const iFloat3 pos, uint32_t time) {
    memmove(d->posTime + 1, d->posTime, (numHistory_Touch_ - 1) * sizeof(d->posTime[0]));
    memmove(d->pos + 1, d->pos, (numHistory_Touch_ - 1) * sizeof(d->pos[0]));
    d->posTime[0] = time;
    d->pos[0]     = pos;
    d->posCount++;
}

struct Impl_Momentum {
    iWidget *affinity;
    uint32_t releaseTime;
    iFloat3 pos;
    iFloat3 velocity;
    iFloat3 accum;
};

struct Impl_Pinch {
    int id;
    SDL_FingerID touchIds[2];
    iWidget *affinity;
};

struct Impl_TouchState {
    iArray *touches;
    iArray *pinches;
    iArray *moms;
    double stepDurationMs;
    double momFrictionPerStep;
    double lastMomTime;
    iInt2 currentTouchPos; /* for emulating SDL_GetMouseState() */
    iInt2 latestLongPressStartPos;
};

static iTouchState *touchState_(void) {
    static iTouchState state_;
    iTouchState *d = &state_;
    if (!d->touches) {
        d->touches        = new_Array(sizeof(iTouch));
        d->pinches        = new_Array(sizeof(iPinch));
        d->moms           = new_Array(sizeof(iMomentum));
        d->lastMomTime    = 0.0;
        d->stepDurationMs = 1000.0 / 60.0; /* TODO: Ask SDL about the display refresh rate. */
#if defined (iPlatformAppleMobile)
        d->stepDurationMs = 1000.0 / (double) displayRefreshRate_iOS();
#endif
        d->momFrictionPerStep = pow(0.985, 120.0 / (1000.0 / d->stepDurationMs));
#if defined (iPlatformAndroidMobile)
        d->momFrictionPerStep = 10 * gap_UI;
#endif
    }
    return d;
}

static iTouch *find_TouchState_(iTouchState *d, SDL_FingerID id) {
    iConstForEach(Array, i, d->touches) {
        iTouch *touch = (iTouch *) i.value;
        if (touch->id == id) {
            return touch;
        }
    }
    return NULL;
}

iLocalDef float distance_Touch_(const iTouch *d) {
    return length_F3(sub_F3(d->pos[0], d->startPos));
}

static iBool isStationaryDistance_Touch_(const iTouch *d, int distance) {
    return !d->hasMoved && distance_Touch_(d) < distance * get_Window()->pixelRatio;
}

static iBool isStationary_Touch_(const iTouch *d) {
    return isStationaryDistance_Touch_(d, tapRadiusPt_);
}

static iBool clearWidgetMomentum_TouchState_(iTouchState *d, iWidget *widget) {
    if (!widget) return iFalse;
    iForEach(Array, m, d->moms) {
        iMomentum *mom = m.value;
        if (mom->affinity == widget) {
            remove_ArrayIterator(&m);
            return iTrue;
        }
    }
    return iFalse;
}

static void dispatchMotion_Touch_(iFloat3 pos, int buttonState) {
    touchState_()->currentTouchPos = initF3_I2(pos);
    dispatchEvent_Window(get_Window(), (SDL_Event *) &(SDL_MouseMotionEvent){
        .type = SDL_MOUSEMOTION,
        .timestamp = SDL_GetTicks(),
        .which = SDL_TOUCH_MOUSEID,
        .windowID = id_Window(get_Window()),
        .state = buttonState,
        .x = x_F3(pos),
        .y = y_F3(pos)
    });
}

static iBool dispatchClick_Touch_(const iTouch *d, int button) {
    const iFloat3 tapPos = d->pos[0];
    touchState_()->currentTouchPos = initF3_I2(tapPos);
    iWindow *window = get_Window();
    SDL_MouseButtonEvent btn = {
        .type = SDL_MOUSEBUTTONDOWN,
        .button = button,
        .clicks = 1,
        .state = SDL_PRESSED,
        .timestamp = SDL_GetTicks(),
        .which = SDL_TOUCH_MOUSEID,
        .windowID = id_Window(window),
        .x = x_F3(tapPos),
        .y = y_F3(tapPos)
    };
    iBool wasUsed = dispatchEvent_Window(window, (SDL_Event *) &btn);
    /* Immediately released, too. */
    btn.type = SDL_MOUSEBUTTONUP;
    btn.state = SDL_RELEASED;
    btn.timestamp = SDL_GetTicks();
    dispatchEvent_Window(window, (SDL_Event *) &btn);
    if (!wasUsed && button == SDL_BUTTON_RIGHT) {
        postContextClick_Window(window, &btn);
    }
    return wasUsed;
}

static void dispatchButtonDown_Touch_(iFloat3 pos) {
    touchState_()->currentTouchPos = initF3_I2(pos);
    dispatchEvent_Window(get_Window(), (SDL_Event *) &(SDL_MouseButtonEvent){
        .type = SDL_MOUSEBUTTONDOWN,
        .timestamp = SDL_GetTicks(),
        .clicks = 1,
        .state = SDL_PRESSED,
        .which = SDL_TOUCH_MOUSEID,
        .windowID = id_Window(get_Window()),
        .button = SDL_BUTTON_LEFT,
        .x = x_F3(pos),
        .y = y_F3(pos)
    });
}

static void dispatchButtonUp_Touch_(iFloat3 pos) {
    touchState_()->currentTouchPos = initF3_I2(pos);
    dispatchEvent_Window(get_Window(), (SDL_Event *) &(SDL_MouseButtonEvent){
        .type = SDL_MOUSEBUTTONUP,
        .timestamp = SDL_GetTicks(),
        .clicks = 1,
        .state = SDL_RELEASED,
        .which = SDL_TOUCH_MOUSEID,
        .windowID = id_Window(get_Window()),
        .button = SDL_BUTTON_LEFT,
        .x = x_F3(pos),
        .y = y_F3(pos)
    });
}

static void dispatchNotification_Touch_(const iTouch *d, int code) {
    if (d->affinity) {
        iRoot *oldRoot = current_Root();
        setCurrent_Root(d->affinity->root);
        dispatchEvent_Widget(d->affinity, (SDL_Event *) &(SDL_UserEvent){
            .type = SDL_USEREVENT,
            .timestamp = SDL_GetTicks(),
            .code = code,
            .data1 = d->affinity,
            .data2 = d->affinity->root,
            .windowID = id_Window(window_Widget(d->affinity)),
        });
        setCurrent_Root(oldRoot);
    }
}

iLocalDef double accurateTicks_(void) {
    const uint64_t freq  = SDL_GetPerformanceFrequency();
    const uint64_t count = SDL_GetPerformanceCounter();
    return 1000.0 * (double) count / (double) freq;
}

static iFloat3 gestureVector_Touch_(const iTouch *d) {
    const size_t lastIndex = iMin(d->posCount - 1, lastIndex_Touch_);
    return sub_F3(d->pos[0], d->pos[lastIndex]);
}

static uint32_t gestureSpan_Touch_(const iTouch *d) {
    const size_t lastIndex = iMin(d->posCount - 1, lastIndex_Touch_);
    return d->posTime[0] - d->posTime[lastIndex];
}

static void postPendingScroll_TouchState_(iTouchState *d, iTouch *touch) {
    if (touch->numPendingScroll > touch->pendingScrollThreshold) {
        const iInt2 pixels = touch->pendingScroll[0];
//            printf("%u :: (%d/%d) pending scroll %d,%d\n", nowTime, touch->numPendingScroll, touch->pendingScrollThreshold, pixels.x, pixels.y);
        memmove(touch->pendingScroll, touch->pendingScroll + 1,
                sizeof(touch->pendingScroll[0]) * (iElemCount(touch->pendingScroll) - 1));
        touch->numPendingScroll--;
        dispatchMotion_Touch_(touch->startPos, 0);
        setCurrent_Root(touch->affinity->root);
        dispatchEvent_Widget(touch->affinity, (SDL_Event *) &(SDL_MouseWheelEvent){
                .type = SDL_MOUSEWHEEL,
                .which = SDL_TOUCH_MOUSEID,
                .windowID = id_Window(window_Widget(touch->affinity)),
                .timestamp = SDL_GetTicks(),
                .x = pixels.x,
                .y = pixels.y,
                .direction = perPixel_MouseWheelFlag,
        });
        /* TODO: Keep increasing movement if the direction is the same. */
        clearWidgetMomentum_TouchState_(d, touch->affinity);
    }
}

static void update_TouchState_(void *ptr) {
    iWindow *win = get_Window();
    const iWidget *oldHover = win->hover;
    iTouchState *d = ptr;
    /* Check for long presses to simulate right clicks. */
    const uint32_t nowTime = SDL_GetTicks();
    iForEach(Array, i, d->touches) {
        iTouch *touch = i.value;
        postPendingScroll_TouchState_(d, touch);
        if (touch->pinchId || touch->isTouchDrag) {
            continue;
        }
        if (touch->edge) {
            const iFloat3 pos = touch->pos[0];
            /* Cancel the swipe if the finger doesn't move or moves mostly vertically. */
            const iFloat3 gestureVector = gestureVector_Touch_(touch);
            if (fabsf(2 * x_F3(gestureVector)) < fabsf(y_F3(gestureVector)) ||
                (isStationary_Touch_(touch) && nowTime - touch->startTime > shortPressSpanMs_)) {
                //const int swipeDir = x_F3(gestureVector) > 0 ? +1 : -1;
                //dispatchClick_Touch_(touch,
//                                     touch->edge == left_TouchEdge  && swipeDir > 0 ? SDL_BUTTON_X1 :
//                                     touch->edge == right_TouchEdge && swipeDir < 0 ? SDL_BUTTON_X2 : 0);
//                setHover_Widget(NULL);
                postCommandf_App("edgeswipe.ended abort:1 side:%d id:%llu", touch->edge, touch->id);
                touch->edge = none_TouchEdge;
                /* May be a regular drag along the edge so don't remove. */
                //remove_ArrayIterator(&i);
            }
            continue;
        }
        /* Holding a touch will reset previous momentum for this widget. */
        if (isStationary_Touch_(touch)) {
            const int elapsed = nowTime - touch->startTime;
            if (elapsed > 25) { /* TODO: Shouldn't this be done only once? */
                if (clearWidgetMomentum_TouchState_(d, touch->affinity)) {
                    touch->hasMoved = iTrue; /* resume scrolling */
                }
                clear_Array(d->moms); /* stop all ongoing momentum */
            }
            if (elapsed > 50 && !touch->isTapBegun) {
                /* Looks like a possible tap. */
                touchState_()->currentTouchPos = initF3_I2(touch->pos[0]);
                dispatchNotification_Touch_(touch, widgetTapBegins_UserEventCode);
                dispatchMotion_Touch_(touch->pos[0], 0);
                refresh_Widget(touch->affinity);
                touch->isTapBegun = iTrue;
            }
            if (!touch->isTapAndHold && nowTime - touch->startTime >= longPressSpanMs_ &&
                touch->affinity) {
                touchState_()->latestLongPressStartPos = initF3_I2(touch->pos[0]);
                dispatchClick_Touch_(touch, SDL_BUTTON_RIGHT);
                touch->isTapAndHold = iTrue;
                touch->hasMoved = iFalse;
                touch->startPos = touch->pos[0];
#if defined (iPlatformAppleMobile)
                playHapticEffect_iOS(tap_HapticEffect);
#endif
                dispatchMotion_Touch_(init_F3(-100, -100, 0), 0);
            }
            else if (!touch->didBeginOnTouchDrag && touch->isTapAndHold &&
                     touch->affinity && flags_Widget(touch->affinity) & touchDrag_WidgetFlag) {
                /* Convert to touch drag. */
                touch->isTouchDrag = iTrue;
                dispatchButtonDown_Touch_(touch->pos[0]);
                touch->isLeftDown = iTrue;
            }
        }
    }
    /* Update/cancel momentum scrolling. */ {
        const float minSpeed = 15.0f;
        if (d->lastMomTime < 0.001) {
            d->lastMomTime = accurateTicks_();
        }
        const double momAvailMs = accurateTicks_() - d->lastMomTime;
        /* Display refresh is vsynced and we'll be here at most once per frame.
           However, we may come here TOO early, which would cause a hiccup in the scrolling,
           so always do at least one step. */
        int numSteps = iMax(1, momAvailMs / d->stepDurationMs);
        d->lastMomTime += numSteps * d->stepDurationMs;
        numSteps = iMin(numSteps, 10); /* don't spend too much time here */
//        printf("mom steps:%d\n", numSteps);
        iForEach(Array, m, d->moms) {
            if (numSteps == 0) break;
            iMomentum *mom = m.value;
            if (!mom->affinity) {
                remove_ArrayIterator(&m);
                continue;
            }
            for (int step = 0; step < numSteps; step++) {
#if defined (iPlatformAndroidMobile)
                float vel[3];
                store_F3(mom->velocity, vel);
                if (iAbs(vel[0]) < d->momFrictionPerStep) {
                    setX_F3(&mom->velocity, 0.0f);
                }
                else {
                    setX_F3(&mom->velocity, vel[0] + (vel[0] > 0 ? -1 : 1) * d->momFrictionPerStep);
                }
                if (iAbs(vel[1]) < d->momFrictionPerStep) {
                    setY_F3(&mom->velocity, 0.0f);
                }
                else {
                    setY_F3(&mom->velocity, vel[1] + (vel[1] > 0 ? -1 : 1) * d->momFrictionPerStep);
                }
#else
                mulvf_F3(&mom->velocity, d->momFrictionPerStep);
#endif
                addv_F3(&mom->accum, mulf_F3(mom->velocity, d->stepDurationMs / 1000.0f));
            }
            const iInt2 pixels = initF3_I2(mom->accum);
            if (pixels.x || pixels.y) {
                subv_F3(&mom->accum, initI2_F3(pixels));
                dispatchMotion_Touch_(mom->pos, 0);
                iAssert(mom->affinity);
                setCurrent_Root(mom->affinity->root);
                dispatchEvent_Widget(mom->affinity, (SDL_Event *) &(SDL_MouseWheelEvent){
                                                        .type = SDL_MOUSEWHEEL,
                                                        .which = SDL_TOUCH_MOUSEID,
                                                        .windowID = id_Window(window_Widget(mom->affinity)),
                                                        .timestamp = nowTime,
                                                        .x = pixels.x,
                                                        .y = pixels.y,
                                                        .direction = perPixel_MouseWheelFlag
                                                    });
            }
            if (length_F3(mom->velocity) < minSpeed) {
                setHover_Widget(NULL);
                remove_ArrayIterator(&m);
            }
        }
    }
    /* Keep updating if interaction is still ongoing. */
    if (!isEmpty_Array(d->touches) || !isEmpty_Array(d->moms)) {
        addTickerRoot_App(update_TouchState_, NULL, ptr);
    }
    if (oldHover != win->hover) {
        refresh_Widget(oldHover);
        refresh_Widget(win->hover);
    }
}

#if 0
static iWidget *findSlidePanel_Widget_(iWidget *d) {
    for (iWidget *w = d; w; w = parent_Widget(w)) {
        if (isVisible_Widget(w) && flags_Widget(w) & edgeDraggable_WidgetFlag) {
            return w;
        }
    }
    return NULL;
}
#endif

static void checkNewPinch_TouchState_(iTouchState *d, iTouch *newTouch) {
    iWidget *affinity = newTouch->affinity;
    if (!affinity) {
        return;
    }
    iForEach(Array, i, d->touches) {
        iTouch *other = i.value;
        if (other->id == newTouch->id || other->pinchId || other->affinity != affinity) {
            continue;
        }
        /* A second finger on the same widget. */
        iPinch pinch = { .affinity = affinity, .id = SDL_GetTicks() };
        pinch.touchIds[0] = newTouch->id;
        pinch.touchIds[1] = other->id;
        newTouch->pinchId = other->pinchId = pinch.id;
        clearWidgetMomentum_TouchState_(d, affinity);
        if (other->edge && other->didPostEdgeMove) {
            postCommandf_App("edgeswipe.ended abort:1 side:%d id:%llu", other->edge, other->id);
            other->didPostEdgeMove = iFalse;
        }
        other->edge = none_TouchEdge;
        newTouch->edge = none_TouchEdge;
        /* Remember current positions to determine pinch amount. */
        newTouch->startPos = newTouch->pos[0];
        other->startPos    = other->pos[0];
        pushBack_Array(d->pinches, &pinch);
        /*printf("[Touch] pinch %d starts with fingers %lld and %lld\n", pinch.id,
               newTouch->id, other->id);*/
        postCommandf_App("pinch.began ptr:%p", affinity);
        break;
    }
}

static iPinch *findPinch_TouchState_(iTouchState *d, int pinchId) {
    iForEach(Array, i, d->pinches) {
        iPinch *pinch = i.value;
        if (pinch->id == pinchId) {
            return pinch;
        }
    }
    return NULL;
}

static void pinchMotion_TouchState_(iTouchState *d, int pinchId) {
    const iPinch *pinch = findPinch_TouchState_(d, pinchId);
    iAssert(pinch != NULL);
    if (!pinch) return;
    const iTouch *touch[2] = {
        find_TouchState_(d, pinch->touchIds[0]),
        find_TouchState_(d, pinch->touchIds[1])
    };
    iAssert(pinch->affinity == touch[0]->affinity);
    iAssert(pinch->affinity == touch[1]->affinity);
    const float startDist = length_F3(sub_F3(touch[1]->startPos, touch[0]->startPos));
    if (startDist < gap_UI) {
        return;
    }
    const float dist = length_F3(sub_F3(touch[1]->pos[0], touch[0]->pos[0]));
//    printf("[Touch] pinch %d motion: relative %f\n", pinchId, dist / startDist);
    postCommandf_App("pinch.moved arg:%f ptr:%p", dist / startDist, pinch->affinity);
}

static void endPinch_TouchState_(iTouchState *d, int pinchId) {
    //printf("[Touch] pinch %d ends\n", pinchId);
    iForEach(Array, i, d->pinches) {
        iPinch *pinch = i.value;
        if (pinch->id == pinchId) {
            postCommandf_App("pinch.ended ptr:%p", pinch->affinity);
            /* Cancel both touches. */
            iForEach(Array, j, d->touches) {
                iTouch *touch = j.value;
                if (touch->id == pinch->touchIds[0] || touch->id == pinch->touchIds[1]) {
                    remove_ArrayIterator(&j);
                }
            }
            remove_ArrayIterator(&i);
            break;
        }
    }
}

iBool processEvent_Touch(const SDL_Event *ev) {
    /* We only handle finger events here. */
    if (ev->type != SDL_FINGERDOWN && ev->type != SDL_FINGERMOTION && ev->type != SDL_FINGERUP) {
        return iFalse;
    }
    iTouchState *d = touchState_();
    iWindow *window = get_Window();    
    const iInt2 rootSize = size_Window(window);
    const SDL_TouchFingerEvent *fing = &ev->tfinger;
    const iFloat3 pos = init_F3(fing->x * rootSize.x, fing->y * rootSize.y, 0); /* pixels */
    const uint32_t nowTime = SDL_GetTicks();
    if (ev->type == SDL_FINGERDOWN) {
        /* Register the new touch. */
        const float x = x_F3(pos);
        enum iTouchEdge edge = none_TouchEdge;
        const int edgeWidth = 30 * window->pixelRatio;
        iWidget *dragging = NULL;
        if (x < edgeWidth) {
            edge = left_TouchEdge;
        }
        else if (x > rootSize.x - edgeWidth) {
            edge = right_TouchEdge;
        }
        iWidget *aff = hitChild_Window(window, init_I2(iRound(x), iRound(y_F3(pos))));
#if 0
        if (edge == left_TouchEdge) {
            dragging = findSlidePanel_Widget_(aff);
            if (dragging) {
//                printf("Selected for dragging: ");
//                identify_Widget(dragging);
                setFlags_Widget(dragging, dragged_WidgetFlag, iTrue);
            }
        }
#endif
        /* TODO: We must retain a reference to the affinity widget, or otherwise it might
           be destroyed during the gesture. */
//        printf("aff:[%p] %s:'%s'\n", aff, aff ? class_Widget(aff)->name : "-",
//               cstr_String(id_Widget(aff)));
//        printf("drg:[%p] %s:'%s'\n", dragging, dragging ? class_Widget(dragging)->name : "-",
//               cstr_String(id_Widget(dragging)));
        iTouch newTouch = {
            .id = fing->fingerId,
            .affinity = aff,
//            .edgeDragging = dragging,
            .didBeginOnTouchDrag = (flags_Widget(aff) & touchDrag_WidgetFlag) != 0,
            .edge = edge,
            .startTime = nowTime,
            .startPos = pos,
        };
        pushPos_Touch_(&newTouch, pos, fing->timestamp);
        pushBack_Array(d->touches, &newTouch);
        /* Some widgets rely on hover state for scrolling. */
        if (flags_Widget(aff) & hover_WidgetFlag && ~flags_Widget(aff) & touchDrag_WidgetFlag) {
            setHover_Widget(aff);
        }
        /* This may begin a pinch. */
        checkNewPinch_TouchState_(d, back_Array(d->touches));
        addTickerRoot_App(update_TouchState_, NULL, d);
    }
    else if (ev->type == SDL_FINGERMOTION) {
        iTouch *touch = find_TouchState_(d, fing->fingerId);
        if (touch && touch->edge) {
            clear_Array(d->moms);
            pushPos_Touch_(touch, pos, nowTime);
            postCommandf_App("edgeswipe.moved arg:%d side:%d id:%llu",
                             (int) (x_F3(pos) - x_F3(touch->startPos)),
                             touch->edge,
                             touch->id);
            touch->didPostEdgeMove = iTrue;
            return iTrue;
        }
        if (touch && touch->affinity) {
            if (touch->isTouchDrag) {
                dispatchMotion_Touch_(pos, SDL_BUTTON_LMASK);
                return iTrue;
            }
            if (touch->isTapAndHold) {
                pushPos_Touch_(touch, pos, fing->timestamp);
                if (!touch->hasMoved && !isStationaryDistance_Touch_(touch, tapRadiusPt_ * 3)) {
                    touch->hasMoved = iTrue;
                }
                if (touch->hasMoved) {
                    dispatchMotion_Touch_(pos, 0);
                }
                return iTrue;
            }
            /* Update touch position. */
            pushPos_Touch_(touch, pos, nowTime);
            if (touch->pinchId) {
                pinchMotion_TouchState_(d, touch->pinchId);
                return iTrue;
            }
            if (!touch->isTouchDrag && !isStationary_Touch_(touch) &&
                flags_Widget(touch->affinity) & touchDrag_WidgetFlag) {
                touch->hasMoved = iTrue;
                touch->isTouchDrag = iTrue;
                touch->edge = none_TouchEdge;
                pushPos_Touch_(touch, pos, fing->timestamp);
                dispatchMotion_Touch_(touch->startPos, 0);
                dispatchButtonDown_Touch_(touch->startPos);
                dispatchMotion_Touch_(pos, SDL_BUTTON_LMASK);
                touch->isLeftDown = iTrue;
                return iTrue;
            }
            const iFloat3 amount = mul_F3(init_F3(fing->dx, fing->dy, 0),
                                          init_F3(rootSize.x, rootSize.y, 0));
            addv_F3(&touch->accum, amount);
            iInt2 pixels = initF3_I2(touch->accum);
            /* We're reporting scrolling as full points, so keep track of the precise distance. */
            subv_F3(&touch->accum, initI2_F3(pixels));
            if (!touch->hasMoved) {
                if (!isStationary_Touch_(touch)) {
                    touch->hasMoved = iTrue;
                    /* The first FINGERMOTION seems to be larger than the subsequence ones.
                       Maybe SDL does its own stationary threshold? We'll counter by reducing
                       the first one. */
                    divvf_F3(&touch->accum, 6);
                    divfv_I2(&pixels, 6);
                    /* Allow scrolling a scrollable widget. */
                    if (touch->affinity && touch->affinity->flags2 & slidingSheetDraggable_WidgetFlag2) {
                        extern iWidgetClass Class_SidebarWidget; /* The only type of sliding sheet for now. */
                        iWidget *slider = findParentClass_Widget(touch->affinity, &Class_SidebarWidget);
                        if (slider) {
                            touch->affinity = slider;
                        }
                    }
                    else {
                        iWidget *flow = findOverflowScrollable_Widget(touch->affinity);
                        if (flow) {
                            touch->affinity = flow;
                        }
                    }
                }
                else {
                    touch->accum = zero_F3();
                    pixels       = zero_I2();
                }
            }
            else if (!touch->axis &&
                     distance_Touch_(touch) > tapRadiusPt_ * 3 * window->pixelRatio) {
                /* Lock swipe direction to an axis. */
                if (iAbs(x_F3(touch->startPos) - x_F3(pos)) >
                    iAbs(y_F3(touch->startPos) - y_F3(pos)) * 1.5f) {
                    touch->axis = x_TouchAxis;
                }
                else {
                    touch->axis = y_TouchAxis;
                }
            }
            iAssert(touch->edge == none_TouchEdge);
            if (touch->axis == x_TouchAxis) {
                pixels.y = 0;
            }
            if (touch->axis == y_TouchAxis) {
                pixels.x = 0;
            }
#if 0
            static uint32_t lastTime = 0;
            printf("%u [%u] :: %p (%s) py: %i wy: %f acc: %f edge: %d\n",
                   nowTime - lastTime,
                   ev->common.timestamp,
                   touch->affinity,
                   class_Widget(touch->affinity)->name,
                   pixels.y, y_F3(amount), y_F3(touch->accum),
                   touch->edge);
            lastTime = nowTime;
#endif
            if (pixels.x || pixels.y) {
                /* Finger events may not arrive at regular intervals (particularly with the SDL on
                   iOS, it seems!), so we won't post the scroll event immediately but instead
                   wait until next ticker iteration. This allows us to buffer events if too many
                   arrive at once. */
                const int maxPending = iElemCount(touch->pendingScroll);
                if (touch->numPendingScroll == maxPending) {
                    addv_I2(&touch->pendingScroll[maxPending - 1], pixels);
                }
                else {
                    touch->pendingScroll[touch->numPendingScroll] = pixels;
#if defined (iPlatformAppleMobile)
                    touch->pendingScrollThreshold = iMin(touch->numPendingScroll, 1);
#else
                    touch->pendingScrollThreshold = 0;
#endif
                    touch->numPendingScroll++;
#if defined (iPlatformAndroidMobile)
                    /* No need to wait. */
                    postPendingScroll_TouchState_(d, touch);
#endif
                }
            }
        }
    }
    else if (ev->type == SDL_FINGERUP) {
        iForEach(Array, i, d->touches) {
            iTouch *touch = i.value;
            if (touch->id != fing->fingerId) {
                continue;
            }
            if (touch->pinchId) {
                endPinch_TouchState_(d, touch->pinchId);
                break;
            }
            if (touch->edge && !isStationary_Touch_(touch)) {
                const iFloat3 gesture = gestureVector_Touch_(touch);
                const uint32_t duration = gestureSpan_Touch_(touch);
                const float pixel = window->pixelRatio;
                const int moveDir = x_F3(gesture) < -pixel ? -1 : x_F3(gesture) > pixel ? +1 : 0;
                const int didAbort = (touch->edge == left_TouchEdge  && moveDir < 0) ||
                                     (touch->edge == right_TouchEdge && moveDir > 0);
                postCommandf_App("edgeswipe.ended abort:%d side:%d id:%llu speed:%d", didAbort,
                                 touch->edge, touch->id,
                                 (int) (duration > 0 ? length_F3(gesture) / (duration / 1000.0f) : 0));
                remove_ArrayIterator(&i);
                continue;
            }
            if (flags_Widget(touch->affinity) & touchDrag_WidgetFlag) {
                if (!touch->isLeftDown && !touch->isTapAndHold) {
                    /* This will be a click on a touchDrag widget. */
                    dispatchButtonDown_Touch_(touch->startPos);
                }
                dispatchButtonUp_Touch_(pos);
                remove_ArrayIterator(&i);
                continue;
            }
            if (touch->isTapAndHold) {
                if (!isStationary_Touch_(touch)) {
                    /* Finger moved while holding, so click at the end position. */
                    dispatchClick_Touch_(touch, SDL_BUTTON_LEFT);
                }
                setHover_Widget(NULL);
                remove_ArrayIterator(&i);
                continue;
            }
            /* Edge swipes do not generate momentum. */
            const size_t lastIndex = iMin(touch->posCount - 1, lastIndex_Touch_);
            const iFloat3 gestureVector = sub_F3(pos, touch->pos[lastIndex]);
            const uint32_t duration = nowTime - touch->startTime;
            iFloat3 velocity = zero_F3();
#if 0
            if (touch->edge && fabsf(2 * x_F3(gestureVector)) > fabsf(y_F3(gestureVector)) &&
                !isStationary_Touch_(touch)) {
                const int swipeDir = x_F3(gestureVector) > 0 ? +1 : -1;
                dispatchClick_Touch_(touch,
                                     touch->edge == left_TouchEdge  && swipeDir > 0 ? SDL_BUTTON_X1 :
                                     touch->edge == right_TouchEdge && swipeDir < 0 ? SDL_BUTTON_X2 : 0);
                setHover_Widget(NULL);
            }
            else
#endif
            {
                const uint32_t elapsed = fing->timestamp - touch->posTime[lastIndex];
                const float minVelocity = 400.0f;
                if (elapsed < 150) {
                    velocity = divf_F3(sub_F3(pos, touch->pos[lastIndex]),
                                       (float) elapsed / 1000.0f);
                    if (touch->axis == y_TouchAxis || fabsf(x_F3(velocity)) < minVelocity) {
                        setX_F3(&velocity, 0.0f);
                    }
                    if (touch->axis == x_TouchAxis || fabsf(y_F3(velocity)) < minVelocity) {
                        setY_F3(&velocity, 0.0f);
                    }
                }
                //printf("elap:%ums vel:%f\n", elapsed, length_F3(velocity));
                pushPos_Touch_(touch, pos, nowTime);
                /* If short and didn't move far, do a tap (left click). */
                if (duration < longPressSpanMs_ && isStationary_Touch_(touch)) {
                    dispatchMotion_Touch_(pos, SDL_BUTTON_LMASK);
                    dispatchClick_Touch_(touch, SDL_BUTTON_LEFT);
                    dispatchMotion_Touch_(init_F3(-100, -100, 0), 0); /* out of screen */
                }
                else if (length_F3(velocity) > 0.0f) {
    //                printf("vel:%f\n", length_F3(velocity));
                    clearWidgetMomentum_TouchState_(d, touch->affinity);
                    iMomentum mom = {
                        .affinity = touch->affinity,
                        .releaseTime = nowTime,
                        .pos = touch->startPos, // pos[0],
                        .velocity = velocity
                    };
                    if (isEmpty_Array(d->moms)) {
                        d->lastMomTime = accurateTicks_();
                    }
                    pushBack_Array(d->moms, &mom);
                    //dispatchMotion_Touch_(touch->startPos, 0);
                }
                else {
                    if (touch->affinity) {
                        dispatchNotification_Touch_(touch, widgetTouchEnds_UserEventCode);
                    }
                    dispatchButtonUp_Touch_(pos);
                    setHover_Widget(NULL);
                }
            }
            remove_ArrayIterator(&i);
        }
    }
    return iTrue;
}

float stopWidgetMomentum_Touch(const iWidget *widget) {
    iTouchState *d = touchState_();
    float remaining = 0.0f;
    iForEach(Array, i, d->moms) {
        iMomentum *mom = i.value;
        if (mom->affinity == widget) {
            remaining = length_F3(mom->velocity);
            remove_ArrayIterator(&i);
        }
    }
    return remaining;
}

enum iWidgetTouchMode widgetMode_Touch(const iWidget *widget) {
    iTouchState *d = touchState_();
    iConstForEach(Array, i, d->touches) {
        const iTouch *touch = i.value;
        if (touch->affinity == widget) {
            return touch_WidgetTouchMode;
        }
    }
    iConstForEach(Array, j, d->moms) {
        const iMomentum *mom = j.value;
        if (mom->affinity == widget) {
            return momentum_WidgetTouchMode;
        }
    }
    return none_WidgetTouchMode;
}

void widgetDestroyed_Touch(iWidget *widget) {
    iTouchState *d = touchState_();
    iForEach(Array, i, d->touches) {
        iTouch *touch = i.value;
        if (touch->affinity == widget) {
            remove_ArrayIterator(&i);
        }
    }
    iForEach(Array, p, d->pinches) {
        iPinch *pinch = p.value;
        if (pinch->affinity == widget) {
            remove_ArrayIterator(&p);
        }
    }
    iForEach(Array, m, d->moms) {
        iMomentum *mom = m.value;
        if (mom->affinity == widget) {
            remove_ArrayIterator(&m);
        }
    }
}

void transferAffinity_Touch(iWidget *src, iWidget *dst) {
    iTouchState *d = touchState_();
    iForEach(Array, i, d->touches) {
        iTouch *touch = i.value;
        if (touch->affinity == src) {
            touch->affinity = dst;
        }
    }
}

iInt2 latestPosition_Touch(void) {
    return touchState_()->currentTouchPos;
}

iInt2 latestTapPosition_Touch(void) {
    return touchState_()->latestLongPressStartPos;
}

iBool isHovering_Touch(void) {
    iTouchState *d = touchState_();
    if (numFingers_Touch() == 1) {
        const iTouch *touch = constFront_Array(d->touches);
        if (touch->isTapBegun && isStationary_Touch_(touch)) {
            return iTrue;
        }
        if (touch->isTapAndHold) {
            return iTrue;
        }
    }
    return iFalse;
}

size_t numFingers_Touch(void) {
    return size_Array(touchState_()->touches);
}
