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

iDeclareType(Touch)
iDeclareType(TouchState)
iDeclareType(Momentum)

#define numHistory_Touch_   3
#define lastIndex_Touch_    (numHistory_Touch_ - 1)

enum iTouchEdge {
    none_TouchEdge,
    left_TouchEdge,
    right_TouchEdge,
};

struct Impl_Touch {
    SDL_FingerID id;
    iWidget *affinity; /* widget on which the touch started */
    iBool hasMoved;
    enum iTouchEdge edge;
    uint32_t startTime;
    iFloat3 startPos;
    uint32_t posTime[numHistory_Touch_];
    iFloat3 pos[numHistory_Touch_];
    iFloat3 accum;
};

iLocalDef void pushPos_Touch_(iTouch *d, const iFloat3 pos, uint32_t time) {
    memmove(d->posTime + 1, d->posTime, (numHistory_Touch_ - 1) * sizeof(d->posTime[0]));
    memmove(d->pos + 1, d->pos, (numHistory_Touch_ - 1) * sizeof(d->pos[0]));
    d->posTime[0] = time;
    d->pos[0]     = pos;
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
static const int      tapRadiusPt_     = 15;

static iBool isStationary_Touch_(const iTouch *d) {
    return !d->hasMoved &&
           length_F3(sub_F3(d->pos[0], d->startPos)) < tapRadiusPt_ * get_Window()->pixelRatio;
}

static void dispatchClick_Touch_(const iTouch *d, int button) {
    const iFloat3 tapPos = d->pos[0];
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
    dispatchEvent_Widget(get_Window()->root, (SDL_Event *) &btn);
    /* Immediately released, too. */
    btn.type = SDL_MOUSEBUTTONUP;
    btn.state = SDL_RELEASED;
    btn.timestamp = SDL_GetTicks();
    dispatchEvent_Widget(get_Window()->root, (SDL_Event *) &btn);
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
            if (nowTime - touch->startTime > 25) {
                clearWidgetMomentum_TouchState_(d, touch->affinity);
            }
            if (nowTime - touch->startTime >= longPressSpanMs_ && touch->affinity) {
                dispatchClick_Touch_(touch, SDL_BUTTON_RIGHT);
                remove_ArrayIterator(&i);
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
            for (int step = 0; step < numSteps; step++) {
                mulvf_F3(&mom->velocity, momFriction);
                addv_F3(&mom->accum, mulf_F3(mom->velocity, stepDurationMs / 1000.0f));
            }
            const iInt2 points = initF3_I2(mom->accum);
            if (points.x || points.y) {
                subv_F3(&mom->accum, initI2_F3(points));
                dispatchEvent_Widget(mom->affinity, (SDL_Event *) &(SDL_MouseWheelEvent){
                    .type = SDL_MOUSEWHEEL,
                    .timestamp = nowTime,
                    .which = 0, /* means "precise scrolling" in DocumentWidget */
                    .x = points.x,
                    .y = points.y
                });
            }
            if (length_F3(mom->velocity) < minSpeed) {
                remove_ArrayIterator(&m);
            }
        }
    }
    /* Keep updating if interaction is still ongoing. */
    if (!isEmpty_Array(d->touches) || !isEmpty_Array(d->moms)) {
        addTicker_App(update_TouchState_, ptr);
    }
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

iBool processEvent_Touch(const SDL_Event *ev) {
    /* We only handle finger events here. */
    if (ev->type != SDL_FINGERDOWN && ev->type != SDL_FINGERMOTION && ev->type != SDL_FINGERUP) {
        return iFalse;
    }
    iTouchState *d = touchState_();
    iWindow *window = get_Window();
    const iInt2 rootSize = rootSize_Window(window);
    const SDL_TouchFingerEvent *fing = &ev->tfinger;
    const iFloat3 pos = init_F3(fing->x * rootSize.x, fing->y * rootSize.y, 0); /* pixels */
    const uint32_t nowTime = SDL_GetTicks();
    if (ev->type == SDL_FINGERDOWN) {
        /* Register the new touch. */
        const float x = x_F3(pos);
        enum iTouchEdge edge = none_TouchEdge;
        const int edgeWidth = 30 * window->pixelRatio;
        if (x < edgeWidth) {
            edge = left_TouchEdge;
        }
        else if (x > rootSize.x - edgeWidth) {
            edge = right_TouchEdge;
        }
        iWidget *aff = hitChild_Widget(window->root, init_I2(iRound(x), iRound(y_F3(pos))));
        /* TODO: We must retain a reference to the affinity widget, or otherwise it might
           be destroyed during the gesture. */
//        printf("aff:%p (%s)\n", aff, aff ? class_Widget(aff)->name : "-");
        if (flags_Widget(aff) & touchDrag_WidgetFlag) {
            dispatchEvent_Widget(window->root, (SDL_Event *) &(SDL_MouseButtonEvent){
                .type = SDL_MOUSEBUTTONDOWN,
                .timestamp = fing->timestamp,
                .clicks = 1,
                .state = SDL_PRESSED,
                .which = SDL_TOUCH_MOUSEID,
                .button = SDL_BUTTON_LEFT,
                .x = x_F3(pos),
                .y = y_F3(pos)
            });
            edge = none_TouchEdge;
        }
        pushBack_Array(d->touches, &(iTouch){
            .id = fing->fingerId,
            .affinity = aff,
            .hasMoved = (flags_Widget(aff) & touchDrag_WidgetFlag) != 0,
            .edge = edge,
            .startTime = nowTime,
            .startPos = pos,
            .pos = pos
        });
        /* Some widgets rely on hover state. */
        dispatchMotion_Touch_(pos, 0);
        addTicker_App(update_TouchState_, d);
    }
    else if (ev->type == SDL_FINGERMOTION) {
        iTouch *touch = find_TouchState_(d, fing->fingerId);
        if (touch && touch->affinity) {
            if (flags_Widget(touch->affinity) & touchDrag_WidgetFlag) {
                dispatchMotion_Touch_(pos, SDL_BUTTON_LMASK);
                return iTrue;
            }
            /* Update touch position. */
            pushPos_Touch_(touch, pos, nowTime);
            const iFloat3 amount = mul_F3(init_F3(fing->dx, fing->dy, 0),
                                          init_F3(rootSize.x, rootSize.y, 0));
            addv_F3(&touch->accum, amount);
            iInt2 pixels = initF3_I2(touch->accum);
            /* We're reporting scrolling as full points, so keep track of the precise distance. */
            subv_F3(&touch->accum, initI2_F3(pixels));
            if (!touch->hasMoved && !isStationary_Touch_(touch)) {
                touch->hasMoved = iTrue;
            }
            /* Edge swipe aborted? */
            if (touch->edge == left_TouchEdge && fing->dx < 0) {
                touch->edge = none_TouchEdge;
            }
            if (touch->edge == right_TouchEdge && fing->dx > 0) {
                touch->edge = none_TouchEdge;
            }
            if (touch->edge) {
                pixels.y = 0;
            }
//            printf("%p (%s) py: %i wy: %f acc: %f\n",
//                   touch->affinity,
//                   class_Widget(touch->affinity)->name,
//                   pixels.y, y_F3(amount), y_F3(touch->accum));
            if (pixels.x || pixels.y) {
                dispatchEvent_Widget(touch->affinity, (SDL_Event *) &(SDL_MouseWheelEvent){
                    .type = SDL_MOUSEWHEEL,
                    .timestamp = SDL_GetTicks(),
                    .which = 0, /* means "precise scrolling" in DocumentWidget */
                    .x = pixels.x,
                    .y = pixels.y,
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
            if (flags_Widget(touch->affinity) & touchDrag_WidgetFlag) {
                dispatchButtonUp_Touch_(pos);
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
            }
            else {
                const uint32_t elapsed = fing->timestamp - touch->posTime[lastIndex_Touch_];
                const float minVelocity = 400.0f;
                if (elapsed < 75) {
                    velocity = divf_F3(sub_F3(pos, touch->pos[lastIndex_Touch_]),
                                       (float) elapsed / 1000.0f);
                    if (fabsf(x_F3(velocity)) < minVelocity) {
                        setX_F3(&velocity, 0.0f);
                    }
                    if (fabsf(y_F3(velocity)) < minVelocity) {
                        setY_F3(&velocity, 0.0f);
                    }
                }
//                printf("elap:%ums vel:%f\n", elapsed, length_F3(velocity));
                pushPos_Touch_(touch, pos, nowTime);
                /* If short and didn't move far, do a tap (left click). */
                if (duration < longPressSpanMs_ && isStationary_Touch_(touch)) {
                    dispatchMotion_Touch_(pos, SDL_BUTTON_LMASK);
                    dispatchClick_Touch_(touch, SDL_BUTTON_LEFT);
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
                }
                else {
                    dispatchButtonUp_Touch_(pos);
                }
            }
            remove_ArrayIterator(&i);
        }
    }
    return iTrue;
}
