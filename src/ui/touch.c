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

iDeclareType(Touch)
iDeclareType(TouchState)
iDeclareType(Momentum)

#define numHistory_Touch_   5
#define lastIndex_Touch_    (numHistory_Touch_ - 1)

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
    iWidget *edgeDragging;
    iBool hasMoved;
    iBool isTapBegun;
    iBool isTouchDrag;
    iBool isTapAndHold;
    enum iTouchEdge edge;
    uint32_t startTime;
    iFloat3 startPos;
    enum iTouchAxis axis;
    uint32_t posTime[numHistory_Touch_];
    iFloat3 pos[numHistory_Touch_];
    size_t posCount;
    iFloat3 accum;
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
    iFloat3 velocity;
    iFloat3 accum;
};

struct Impl_TouchState {
    iArray *touches;
    iArray *moms;
    double lastMomTime;
};

static iTouchState *touchState_(void) {
    static iTouchState state_;
    iTouchState *d = &state_;
    if (!d->touches) {
        d->touches = new_Array(sizeof(iTouch));
        d->moms = new_Array(sizeof(iMomentum));
        d->lastMomTime = SDL_GetTicks();
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

static const uint32_t longPressSpanMs_ = 500;
static const int      tapRadiusPt_     = 10;

iLocalDef float distance_Touch_(const iTouch *d) {
    return length_F3(sub_F3(d->pos[0], d->startPos));
}

static iBool isStationary_Touch_(const iTouch *d) {
    return !d->hasMoved && distance_Touch_(d) < tapRadiusPt_ * get_Window()->pixelRatio;
}

static void dispatchMotion_Touch_(iFloat3 pos, int buttonState) {
    dispatchEvent_Widget(get_Window()->root, (SDL_Event *) &(SDL_MouseMotionEvent){
        .type = SDL_MOUSEMOTION,
        .timestamp = SDL_GetTicks(),
        .which = SDL_TOUCH_MOUSEID,
        .state = buttonState,
        .x = x_F3(pos),
        .y = y_F3(pos)
    });
}

static iBool dispatchClick_Touch_(const iTouch *d, int button) {
    const iFloat3 tapPos = d->pos[0];
    iWindow *window = get_Window();
    SDL_MouseButtonEvent btn = {
        .type = SDL_MOUSEBUTTONDOWN,
        .button = button,
        .clicks = 1,
        .state = SDL_PRESSED,
        .timestamp = SDL_GetTicks(),
        .which = SDL_TOUCH_MOUSEID,
        .x = x_F3(tapPos),
        .y = y_F3(tapPos)
    };
    iBool wasUsed = dispatchEvent_Widget(window->root, (SDL_Event *) &btn);
    /* Immediately released, too. */
    btn.type = SDL_MOUSEBUTTONUP;
    btn.state = SDL_RELEASED;
    btn.timestamp = SDL_GetTicks();
    dispatchEvent_Widget(window->root, (SDL_Event *) &btn);
    if (!wasUsed && button == SDL_BUTTON_RIGHT) {
        postContextClick_Window(window, &btn);
    }
    return wasUsed;
}

static void clearWidgetMomentum_TouchState_(iTouchState *d, iWidget *widget) {
    if (!widget) return;
    iForEach(Array, m, d->moms) {
        iMomentum *mom = m.value;
        if (mom->affinity == widget) {
            remove_ArrayIterator(&m);
        }
    }
}

static void update_TouchState_(void *ptr) {
    iTouchState *d = ptr;
    const uint32_t nowTime = SDL_GetTicks();
    /* Check for long presses to simulate right clicks. */
    iForEach(Array, i, d->touches) {
        iTouch *touch = i.value;
        /* Holding a touch will reset previous momentum for this widget. */
        if (isStationary_Touch_(touch)) {
            const int elapsed = nowTime - touch->startTime;
            if (elapsed > 25) {
                clearWidgetMomentum_TouchState_(d, touch->affinity);
            }
            if (elapsed > 50 && !touch->isTapBegun) {
                /* Looks like a possible tap. */
                dispatchMotion_Touch_(touch->pos[0], 0);
                touch->isTapBegun = iTrue;
            }
            if (!touch->isTapAndHold && nowTime - touch->startTime >= longPressSpanMs_ &&
                touch->affinity) {
                dispatchClick_Touch_(touch, SDL_BUTTON_RIGHT);
                touch->isTapAndHold = iTrue;
                touch->hasMoved = iFalse;
                touch->startPos = touch->pos[0];
#if defined (iPlatformAppleMobile)
                playHapticEffect_iOS(tap_HapticEffect);
#endif
                dispatchMotion_Touch_(init_F3(-100, -100, 0), 0);
            }
        }
    }
    /* Update/cancel momentum scrolling. */ {
        const float minSpeed = 15.0f;
        const float momFriction = 0.985f; /* per step */
        const float stepDurationMs = 1000.0f / 120.0f;
        double momAvailMs = nowTime - d->lastMomTime;
        int numSteps = (int) (momAvailMs / stepDurationMs);
        d->lastMomTime += numSteps * stepDurationMs;
        numSteps = iMin(numSteps, 10); /* don't spend too much time here */
//        printf("mom steps:%d\n", numSteps);
        iWindow *window = get_Window();
        iForEach(Array, m, d->moms) {
            if (numSteps == 0) break;
            iMomentum *mom = m.value;
            if (!mom->affinity) {
                remove_ArrayIterator(&m);
                continue;
            }
            for (int step = 0; step < numSteps; step++) {
                mulvf_F3(&mom->velocity, momFriction);
                addv_F3(&mom->accum, mulf_F3(mom->velocity, stepDurationMs / 1000.0f));
            }
            const iInt2 pixels = initF3_I2(mom->accum);
            if (pixels.x || pixels.y) {
                subv_F3(&mom->accum, initI2_F3(pixels));
                dispatchEvent_Widget(mom->affinity, (SDL_Event *) &(SDL_MouseWheelEvent){
                    .type = SDL_MOUSEWHEEL,
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
        addTicker_App(update_TouchState_, ptr);
    }
}

void widgetDestroyed_Touch(iWidget *widget) {
    iTouchState *d = touchState_();
    iForEach(Array, i, d->touches) {
        iTouch *touch = i.value;
        if (touch->affinity == widget) {
            remove_ArrayIterator(&i);
        }
    }
    iForEach(Array, m, d->moms) {
        iMomentum *mom = m.value;
        if (mom->affinity == widget) {
            remove_ArrayIterator(&m);
        }
    }
}

static void dispatchButtonDown_Touch_(iFloat3 pos) {
    dispatchEvent_Widget(get_Window()->root, (SDL_Event *) &(SDL_MouseButtonEvent){
        .type = SDL_MOUSEBUTTONDOWN,
        .timestamp = SDL_GetTicks(),
        .clicks = 1,
        .state = SDL_PRESSED,
        .which = SDL_TOUCH_MOUSEID,
        .button = SDL_BUTTON_LEFT,
        .x = x_F3(pos),
        .y = y_F3(pos)
    });
}

static void dispatchButtonUp_Touch_(iFloat3 pos) {
    dispatchEvent_Widget(get_Window()->root, (SDL_Event *) &(SDL_MouseButtonEvent){
        .type = SDL_MOUSEBUTTONUP,
        .timestamp = SDL_GetTicks(),
        .clicks = 1,
        .state = SDL_RELEASED,
        .which = SDL_TOUCH_MOUSEID,
        .button = SDL_BUTTON_LEFT,
        .x = x_F3(pos),
        .y = y_F3(pos)
    });
}

static iWidget *findOverflowScrollable_Widget_(iWidget *d) {
    const iInt2 rootSize = rootSize_Window(get_Window());
    for (iWidget *w = d; w; w = parent_Widget(w)) {
        if (flags_Widget(w) & overflowScrollable_WidgetFlag) {
            if (height_Widget(w) > rootSize.y && !hasVisibleChildOnTop_Widget(w)) {
                return w;
            }
            return NULL;
        }
    }
    return NULL;
}

static iWidget *findSlidePanel_Widget_(iWidget *d) {
    for (iWidget *w = d; w; w = parent_Widget(w)) {
        if (isVisible_Widget(w) && flags_Widget(w) & horizontalOffset_WidgetFlag) {
            return w;
        }
    }
    return NULL;
}

iBool processEvent_Touch(const SDL_Event *ev) {
    /* We only handle finger events here. */
    if (ev->type != SDL_FINGERDOWN && ev->type != SDL_FINGERMOTION && ev->type != SDL_FINGERUP) {
        return iFalse;
    }
    iTouchState *d = touchState_();
    iWindow *window = get_Window();
    if (!isFinished_Anim(&window->rootOffset)) {
        return iFalse;
    }
    const iInt2 rootSize = rootSize_Window(window);
    const SDL_TouchFingerEvent *fing = &ev->tfinger;
    const iFloat3 pos = add_F3(init_F3(fing->x * rootSize.x, fing->y * rootSize.y, 0), /* pixels */
                               init_F3(0, -value_Anim(&window->rootOffset), 0));
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
        iWidget *aff = hitChild_Widget(window->root, init_I2(iRound(x), iRound(y_F3(pos))));
        if (edge == left_TouchEdge) {
            dragging = findSlidePanel_Widget_(aff);
            if (dragging) {
                setFlags_Widget(dragging, dragged_WidgetFlag, iTrue);
            }
        }
        /* TODO: We must retain a reference to the affinity widget, or otherwise it might
           be destroyed during the gesture. */
//        printf("aff:[%p] %s:'%s'\n", aff, aff ? class_Widget(aff)->name : "-",
//               cstr_String(id_Widget(aff)));
//        printf("drg:[%p] %s:'%s'\n", dragging, dragging ? class_Widget(dragging)->name : "-",
//               cstr_String(id_Widget(dragging)));
        iTouch newTouch = {
            .id = fing->fingerId,
            .affinity = aff,
            .edgeDragging = dragging,
//            .hasMoved = (flags_Widget(aff) & touchDrag_WidgetFlag) != 0,
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
        addTicker_App(update_TouchState_, d);
    }
    else if (ev->type == SDL_FINGERMOTION) {
        iTouch *touch = find_TouchState_(d, fing->fingerId);
        if (touch && touch->affinity) {
            if (touch->isTouchDrag) {
                dispatchMotion_Touch_(pos, SDL_BUTTON_LMASK);
                return iTrue;
            }
            if (touch->isTapAndHold) {
                pushPos_Touch_(touch, pos, fing->timestamp);
                if (!touch->hasMoved && !isStationary_Touch_(touch)) {
                    touch->hasMoved = iTrue;
                }
                dispatchMotion_Touch_(pos, 0);
                return iTrue;
            }
            /* Update touch position. */
            pushPos_Touch_(touch, pos, nowTime);
            if (!touch->isTouchDrag && !isStationary_Touch_(touch) &&
                flags_Widget(touch->affinity) & touchDrag_WidgetFlag) {
                touch->hasMoved = iTrue;
                touch->isTouchDrag = iTrue;
                touch->edge = none_TouchEdge;
                pushPos_Touch_(touch, pos, fing->timestamp);
                dispatchMotion_Touch_(touch->startPos, 0);
                dispatchEvent_Widget(window->root, (SDL_Event *) &(SDL_MouseButtonEvent){
                    .type = SDL_MOUSEBUTTONDOWN,
                    .timestamp = fing->timestamp,
                    .clicks = 1,
                    .state = SDL_PRESSED,
                    .which = SDL_TOUCH_MOUSEID,
                    .button = SDL_BUTTON_LEFT,
                    .x = x_F3(touch->startPos),
                    .y = y_F3(touch->startPos)
                });
                dispatchMotion_Touch_(pos, SDL_BUTTON_LMASK);
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
                    iWidget *flow = findOverflowScrollable_Widget_(touch->affinity);
                    if (flow) {
                        touch->affinity = flow;
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
            /* Edge swipe aborted? */
            if (touch->edge == left_TouchEdge) {
                if (fing->dx < 0) {
                    touch->edge = none_TouchEdge;
                    if (touch->edgeDragging) {
                        setFlags_Widget(touch->edgeDragging, dragged_WidgetFlag, iFalse);
                        setVisualOffset_Widget(touch->edgeDragging, 0, 200, easeOut_AnimFlag);
                        touch->edgeDragging = NULL;
                    }
                }
                else if (touch->edgeDragging) {
                    setVisualOffset_Widget(touch->edgeDragging, x_F3(pos) - x_F3(touch->startPos), 10, 0);
                }
            }
            if (touch->edge == right_TouchEdge && fing->dx > 0) {
                touch->edge = none_TouchEdge;
            }
            if (touch->edge) {
                pixels.y = 0;
            }
            if (touch->axis == x_TouchAxis) {
                pixels.y = 0;
            }
            if (touch->axis == y_TouchAxis) {
                pixels.x = 0;
            }
//            printf("%p (%s) py: %i wy: %f acc: %f\n",
//                   touch->affinity,
//                   class_Widget(touch->affinity)->name,
//                   pixels.y, y_F3(amount), y_F3(touch->accum));
            if (pixels.x || pixels.y) {
                setFocus_Widget(NULL);
                dispatchEvent_Widget(touch->affinity, (SDL_Event *) &(SDL_MouseWheelEvent){
                    .type = SDL_MOUSEWHEEL,
                    .timestamp = SDL_GetTicks(),
                    .x = pixels.x,
                    .y = pixels.y,
                    .direction = perPixel_MouseWheelFlag
                });
                /* TODO: Keep increasing movement if the direction is the same. */
                clearWidgetMomentum_TouchState_(d, touch->affinity);
            }
        }
    }
    else if (ev->type == SDL_FINGERUP) {
        iTouch *touch = find_TouchState_(d, fing->fingerId);
        iForEach(Array, i, d->touches) {
            iTouch *touch = i.value;
            if (touch->id != fing->fingerId) {
                continue;
            }
            if (touch->edgeDragging) {
                setFlags_Widget(touch->edgeDragging, dragged_WidgetFlag, iFalse);
            }
            if (touch->isTapAndHold) {
                if (!isStationary_Touch_(touch)) {
                    dispatchClick_Touch_(touch, SDL_BUTTON_LEFT);
                }
                setHover_Widget(NULL);
                remove_ArrayIterator(&i);
                continue;
            }
            if (flags_Widget(touch->affinity) & touchDrag_WidgetFlag) {
                if (!touch->isTouchDrag) {
                    dispatchButtonDown_Touch_(touch->startPos);
                }
                dispatchButtonUp_Touch_(pos);
//                setHover_Widget(NULL);
                remove_ArrayIterator(&i);
                continue;
            }
            /* Edge swipes do not generate momentum. */
            const uint32_t duration = nowTime - touch->startTime;
            const iFloat3 gestureVector = sub_F3(pos, touch->startPos);
            iFloat3 velocity = zero_F3();
            if (touch->edge && fabsf(2 * x_F3(gestureVector)) > fabsf(y_F3(gestureVector)) &&
                !isStationary_Touch_(touch)) {
                dispatchClick_Touch_(touch, touch->edge == left_TouchEdge ? SDL_BUTTON_X1
                                                                          : SDL_BUTTON_X2);
                setHover_Widget(NULL);
            }
            else {
                const size_t lastIndex = iMin(touch->posCount - 1, lastIndex_Touch_);
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
                        .velocity = velocity
                    };
                    if (isEmpty_Array(d->moms)) {
                        d->lastMomTime = nowTime;
                    }
                    pushBack_Array(d->moms, &mom);
                    //dispatchMotion_Touch_(touch->startPos, 0);
                }
                else {
                    dispatchButtonUp_Touch_(pos);
                    setHover_Widget(NULL);
                }
            }
            remove_ArrayIterator(&i);
        }
    }
    return iTrue;
}
