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

#include <the_Foundation/defs.h>

/* Set LAGRANGE_FRAME_TIMING to log the main thread's busy time per frame. The value is the
   frame budget in milliseconds (default: 0, one frame at the display refresh rate). Waiting
   for events and presenting a frame are not counted as busy time. */

enum iFramePhase {
    events_FramePhase,
    tickers_FramePhase,
    draw_FramePhase,
    other_FramePhase,
    max_FramePhase,
};

enum iFrameIdle {
    wait_FrameIdle,
    present_FrameIdle,
};

#if defined (LAGRANGE_ENABLE_FRAME_TIMING)

iBool   isEnabled_FrameTiming   (void);
void    beginFrame_FrameTiming  (void);
void    endPhase_FrameTiming    (enum iFramePhase phase);
void    endFrame_FrameTiming    (void);
void    addFrame_FrameTiming    (const char *label, double seconds, double presentSeconds); /* drawn outside the loop */
double  now_FrameTiming         (void); /* seconds */
double  totalPresent_FrameTiming(void); /* seconds spent presenting so far */
void    beginIdle_FrameTiming   (void);
void    endIdle_FrameTiming     (enum iFrameIdle idle);

#else

#define isEnabled_FrameTiming()                 iFalse
#define beginFrame_FrameTiming()
#define endPhase_FrameTiming(phase)
#define endFrame_FrameTiming()
#define addFrame_FrameTiming(label, sec, pres)  iUnused(label, sec, pres)
#define now_FrameTiming()                       0.0
#define totalPresent_FrameTiming()              0.0
#define beginIdle_FrameTiming()
#define endIdle_FrameTiming(idle)

#endif
