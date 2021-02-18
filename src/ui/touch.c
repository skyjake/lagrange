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

struct Impl_Touch {
    SDL_FingerID id;
    iWidget *affinity; /* widget on which the touch started */
    iBool hasMoved;
    uint32_t startTime;
    iFloat3 startPos;
    uint32_t posTime[2];
    iFloat3 pos[2];
    iFloat3 remainder;
};

iLocalDef void pushPos_Touch_(iTouch *d, const iFloat3 pos, uint32_t time) {
    d->posTime[1] = d->posTime[0];
    d->posTime[0] = time;
    d->pos[1]     = d->pos[0];
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

static uint32_t longPressSpanMs_ = 500;
static int      tapRadiusPt_     = 15;

static iBool isStationary_Touch_(const iTouch *d) {
    return !d->hasMoved &&
           length_F3(sub_F3(d->pos[0], d->startPos)) < tapRadiusPt_ * get_Window()->pixelRatio;
}

static void dispatchClick_Touch_(const iTouch *d, int button) {
    const iFloat3 tapPos = divf_F3(add_F3(d->pos[0], d->startPos), 2);
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
        const float minSpeed = 10.0f;
        const float momFriction = 0.975f;
        const float stepDurationMs = 1000.0f / 120.0f;
        double momAvailMs = nowTime - d->lastMomTime;
        int numSteps = iMin((int) (momAvailMs / stepDurationMs), 10);
        d->lastMomTime += numSteps * stepDurationMs;
//        printf("mom steps:%d\n", numSteps);
        iForEach(Array, m, d->moms) {
            if (numSteps == 0) break;
            iMomentum *mom = m.value;
            for (int step = 0; step < numSteps; step++) {
                mulvf_F3(&mom->velocity, momFriction);
                addv_F3(&mom->accum, mulf_F3(mom->velocity, stepDurationMs / 1000.0f));
            }
            const iInt2 pixels = initF3_I2(mom->accum);
            if (pixels.x || pixels.y) {
                subv_F3(&mom->accum, initI2_F3(pixels));
                dispatchEvent_Widget(mom->affinity, (SDL_Event *) &(SDL_MouseWheelEvent){
                    .type = SDL_MOUSEWHEEL,
                    .timestamp = SDL_GetTicks(),
                    .which = 0, /* means "precise scrolling" in DocumentWidget */
                    .x = pixels.x,
                    .y = pixels.y
                });
            }
            //printf("mom vel:%f\n", length_F3(mom->velocity));
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

iBool processEvent_Touch(const SDL_Event *ev) {
    /* We only handle finger events here. */
    if (ev->type != SDL_FINGERDOWN && ev->type != SDL_FINGERMOTION && ev->type != SDL_FINGERUP) {
        return iFalse;
    }
    iTouchState *d = touchState_();
    const SDL_TouchFingerEvent *fing = &ev->tfinger;
    iWindow *window = get_Window();
    const iInt2 rootSize = rootSize_Window(window);
    const iFloat3 pos = init_F3(fing->x * rootSize.x, fing->y * rootSize.y, 0);
    //printf("%2d: %f: touch %f, %f\n", ev->type, z_F3(pos), x_F3(pos), y_F3(pos));
    //fflush(stdout);
    const uint32_t nowTime = SDL_GetTicks();
    if (ev->type == SDL_FINGERDOWN) {
        /* Register the new touch. */
        iWidget *aff = hitChild_Widget(window->root, init_I2(iRound(x_F3(pos)), iRound(y_F3(pos))));
        pushBack_Array(d->touches, &(iTouch){
            .id = fing->fingerId,
            .affinity = aff,
            .startTime = nowTime,
            .startPos = pos,
            .pos = pos
        });
        /* Some widgets rely on hover state. */
        dispatchEvent_Widget(window->root, (SDL_Event *) &(SDL_MouseMotionEvent){
            .type = SDL_MOUSEMOTION,
            .timestamp = SDL_GetTicks(),
            .which = SDL_TOUCH_MOUSEID,
            .x = x_F3(pos),
            .y = y_F3(pos)
        });
        addTicker_App(update_TouchState_, d);
    }
    else if (ev->type == SDL_FINGERMOTION) {
        iTouch *touch = find_TouchState_(d, fing->fingerId);
        if (touch && touch->affinity) {
            /* TODO: Update touch position. */
            const iFloat3 amount = add_F3(touch->remainder,
                                          divf_F3(mul_F3(init_F3(fing->dx, fing->dy, 0),
                                                         init_F3(rootSize.x, rootSize.y, 0)),
                                                  window->pixelRatio));
            const iInt2 pixels = init_I2(iRound(x_F3(amount)), iRound(y_F3(amount)));
            iFloat3 remainder = sub_F3(amount, initI2_F3(pixels));
            touch->remainder = remainder;
            pushPos_Touch_(touch, pos, nowTime);
            if (!touch->hasMoved && !isStationary_Touch_(touch)) {
                touch->hasMoved = iTrue;
            }
            if (pixels.x || pixels.y) {
//                printf("%p (%s) wy: %f\n", touch->affinity, class_Widget(touch->affinity)->name,
//                       fing->dy * rootSize.y / window->pixelRatio);
                dispatchEvent_Widget(touch->affinity, (SDL_Event *) &(SDL_MouseWheelEvent){
                    .type = SDL_MOUSEWHEEL,
                    .timestamp = SDL_GetTicks(),
                    .which = 0, /* means "precise scrolling" in DocumentWidget */
                    .x = pixels.x,
                    .y = pixels.y
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
            const uint32_t elapsed = nowTime - touch->posTime[1];
            iFloat3 velocity = zero_F3();
            if (elapsed < 50) {
                velocity = divf_F3(sub_F3(pos, touch->pos[1]), (float) elapsed / 1000.0f);
            }
            pushPos_Touch_(touch, pos, nowTime);
            iBool wasUsed = iFalse;
            const uint32_t duration = nowTime - touch->startTime;
            /* If short and didn't move far, do a tap (left click). */
            if (duration < longPressSpanMs_ && isStationary_Touch_(touch)) {
                dispatchClick_Touch_(touch, SDL_BUTTON_LEFT);
            }
            else if (length_F3(velocity) > 10.0f) {
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
            remove_ArrayIterator(&i);
        }
    }
    return iTrue;
}
