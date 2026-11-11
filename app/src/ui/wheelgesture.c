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

#include "wheelgesture.h"
#include "util.h"
#include "app.h"

#if defined (LAGRANGE_HAVE_WHEEL_GESTURE)

#include <SDL3/SDL_timer.h>
#include <math.h>

enum { wheelGesturePause_Ms = 200 };

static struct {
    int                 axis;       /* 0: undecided, 1: horizontal, 2: vertical */
    uint64_t            lastTime;   /* for detecting a pause (ends the gesture) */
    float               pending[2]; /* movement while the axis is undecided, in points */
    float               accum[2];   /* fractional pixels carried over */
#if defined (LAGRANGE_WHEEL_GESTURE_SWIPE)
    iBool               isEnded;    /* the rest of the gesture only scrolls vertically */
    SDL_MouseWheelEvent last;       /* last horizontal event */
    int                 seq;        /* identifies the pending end timer */
#endif
} wheelGesture_;

static int lockScrollAxis_WheelGesture_(SDL_MouseWheelEvent *ev) {
    /* Only scroll on one axis at a time. */
    if (!isOngoing_WheelGesture(ev->timestamp)) {
        wheelGesture_.axis = 0;
        iZap(wheelGesture_.pending);
#if defined (LAGRANGE_WHEEL_GESTURE_SWIPE)
        wheelGesture_.isEnded = iFalse;
#endif
    }
    wheelGesture_.lastTime = ev->timestamp;
#if defined (LAGRANGE_WHEEL_GESTURE_SWIPE)
    if (wheelGesture_.isEnded) {
        ev->x = 0.0f; /* remaining momentum */
    }
#endif
    if (wheelGesture_.axis == 0) {
        /* Decide direction after some movement has been observed. */
        float *pending = wheelGesture_.pending;
        pending[0] += ev->x;
        pending[1] += ev->y;
        if (fabsf(pending[0]) + fabsf(pending[1]) < 8.0f) {
            ev->x = 0.0f;
            return 0;
        }
        wheelGesture_.axis = fabsf(pending[0]) > 2 * fabsf(pending[1]) ? 1 : 2;
        ev->x = pending[0];
        iZap(wheelGesture_.pending);
    }
    if (wheelGesture_.axis == 1) {
        ev->y = 0.0f;
    }
    else if (wheelGesture_.axis == 2) {
        ev->x = 0.0f;
    }
    return wheelGesture_.axis;
}

#if defined (LAGRANGE_WHEEL_GESTURE_SWIPE)

static Uint32 postEnd_WheelGesture_(void *seq, SDL_TimerID id, Uint32 interval) {
    /* Finishing a swipe requires an end event. */
    iUnused(id, interval);
    SDL_Event ev = { .user = { .type  = SDL_EVENT_USER,
                               .code  = wheelGestureEnds_UserEventCode,
                               .data1 = seq } };
    SDL_PushEvent(&ev);
    return 0;
}

static void continue_WheelGesture_(const SDL_MouseWheelEvent *ev) {
    wheelGesture_.last = *ev;
    SDL_AddTimer(wheelGesturePause_Ms, postEnd_WheelGesture_,
                 (void *) (intptr_t) ++wheelGesture_.seq);
}

iBool processEnd_WheelGesture(SDL_Event *ev) {
    if (ev->type != SDL_EVENT_USER || ev->user.code != wheelGestureEnds_UserEventCode) {
        return iFalse;
    }
    if ((intptr_t) ev->user.data1 != wheelGesture_.seq) {
        return iTrue; /* the gesture has continued, or was ended already */
    }
    ev->wheel           = wheelGesture_.last;
    ev->wheel.timestamp = SDL_GetTicksNS();
    ev->wheel.x         = 0.0f;
    ev->wheel.y         = 0.0f;
    setInertia_MouseWheelEvent(&ev->wheel, iFalse);
    setScrollFinished_MouseWheelEvent(&ev->wheel, iTrue);
    return iFalse;
}

void end_WheelGesture(void) {
    wheelGesture_.axis    = 0;
    wheelGesture_.isEnded = iTrue;
    iZap(wheelGesture_.pending);
    wheelGesture_.seq++;
}

#endif /* LAGRANGE_WHEEL_GESTURE_SWIPE */

iBool isOngoing_WheelGesture(uint64_t timestampNs) {
    return wheelGesture_.lastTime &&
           timestampNs - wheelGesture_.lastTime <= SDL_MS_TO_NS(wheelGesturePause_Ms);
}

iBool toPerPixel_WheelGesture(SDL_MouseWheelEvent *ev, float unitPoints, float pixelRatio) {
    ev->x *= unitPoints;
    ev->y *= unitPoints;
    const int axis = lockScrollAxis_WheelGesture_(ev);
    float *accum = wheelGesture_.accum;
    accum[0] += ev->x * pixelRatio;
    accum[1] += ev->y * pixelRatio;
    ev->x = (int) accum[0];
    ev->y = (int) accum[1];
    accum[0] -= ev->x;
    accum[1] -= ev->y;
    setPerPixel_MouseWheelEvent(ev, iTrue);
#if defined (LAGRANGE_WHEEL_GESTURE_SWIPE)
    if (axis == 1) {
        continue_WheelGesture_(ev);
    }
#else
    iUnused(axis);
#endif
    return ev->x == 0 && ev->y == 0;
}

#endif /* LAGRANGE_HAVE_WHEEL_GESTURE */
