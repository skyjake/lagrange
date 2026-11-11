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

/* Handling swipe gestures that arrive as plain mouse wheel events. */

#pragma once

#include <the_Foundation/defs.h>
#include <SDL3/SDL_events.h>

#if defined (iPlatformEmscripten) || \
    (defined (iPlatformLinux) && !defined (iPlatformAndroid) && !defined (iPlatformTerminal))
#   define LAGRANGE_HAVE_WHEEL_GESTURE  /* synthesize wheel gestures */
#endif
#if defined (iPlatformEmscripten)
#   define LAGRANGE_WHEEL_GESTURE_SWIPE /* horizontal gestures may also swipe */
#endif

iBool   isOngoing_WheelGesture  (uint64_t timestampNs);
iBool   toPerPixel_WheelGesture (SDL_MouseWheelEvent *, float unitPoints, float pixelRatio);

#if defined (LAGRANGE_WHEEL_GESTURE_SWIPE)
iBool   processEnd_WheelGesture (SDL_Event *);
void    end_WheelGesture        (void); /* ignore the rest of a trackpad swipe */
#endif
