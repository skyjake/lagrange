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

#include "frametiming.h"

#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>

static struct {
    int      isEnabled; /* -1: not checked yet */
    double   budget;
    double   tickSeconds;
    uint64_t phaseStart;
    uint64_t idleStart[4]; /* nested idle periods, e.g., resize drawing while waiting */
    int      idleDepth;
    uint64_t idleTicks; /* during the current phase */
    double   phases[max_FramePhase];
    double   present;
    double   totalPresent;
    /* Summary of the current period. */
    uint64_t periodStart;
    unsigned numFrames;
    unsigned numOver;
    double   maxBusy;
    double   sumBusy;
} timing_ = { .isEnabled = -1 };

static double seconds_(uint64_t ticks) {
    return (double) ticks * timing_.tickSeconds;
}

static void logPeriod_(uint64_t now) {
    if (timing_.maxBusy >= 0.001) {
        fprintf(stderr,
                "[Frame] %4u frames, max %6.2f ms, avg %5.2f ms, %u over budget\n",
                timing_.numFrames,
                timing_.maxBusy * 1000.0,
                timing_.sumBusy / timing_.numFrames * 1000.0,
                timing_.numOver);
    }
    timing_.periodStart = now;
    timing_.numFrames   = 0;
    timing_.numOver     = 0;
    timing_.maxBusy     = 0.0;
    timing_.sumBusy     = 0.0;
}

static void count_(double busy) {
    timing_.numFrames++;
    timing_.sumBusy += busy;
    if (busy > timing_.maxBusy) {
        timing_.maxBusy = busy;
    }
    const uint64_t now = SDL_GetPerformanceCounter();
    if (seconds_(now - timing_.periodStart) >= 1.0) {
        logPeriod_(now);
    }
}

iBool isEnabled_FrameTiming(void) {
    if (timing_.isEnabled < 0) {
        const char *env = getenv("LAGRANGE_FRAME_TIMING");
        const double ms = env ? atof(env) : 0.0;
        timing_.isEnabled   = (env != NULL);
        timing_.budget      = (ms > 0.0 ? ms : 1000.0 / 60.0) / 1000.0;
        timing_.tickSeconds = 1.0 / (double) SDL_GetPerformanceFrequency();
        timing_.periodStart = SDL_GetPerformanceCounter();
    }
    return timing_.isEnabled;
}

void beginFrame_FrameTiming(void) {
    if (!isEnabled_FrameTiming()) {
        return;
    }
    timing_.phaseStart = SDL_GetPerformanceCounter();
    timing_.idleTicks  = 0;
    timing_.present    = 0.0;
    for (int i = 0; i < max_FramePhase; i++) {
        timing_.phases[i] = 0.0;
    }
}

void endPhase_FrameTiming(enum iFramePhase phase) {
    if (!timing_.isEnabled) {
        return;
    }
    const uint64_t now = SDL_GetPerformanceCounter();
    timing_.phases[phase] += seconds_(now - timing_.phaseStart - timing_.idleTicks);
    timing_.phaseStart = now;
    timing_.idleTicks  = 0;
}

void endFrame_FrameTiming(void) {
    if (!timing_.isEnabled) {
        return;
    }
    endPhase_FrameTiming(other_FramePhase);
    double busy = 0.0;
    for (int i = 0; i < max_FramePhase; i++) {
        busy += timing_.phases[i];
    }
    if (busy >= timing_.budget) {
        timing_.numOver++;
        fprintf(stderr,
                "[Frame] %6.2f ms: events %.2f, tickers %.2f, draw %.2f, other %.2f "
                "(present %.2f)\n",
                busy * 1000.0,
                timing_.phases[events_FramePhase] * 1000.0,
                timing_.phases[tickers_FramePhase] * 1000.0,
                timing_.phases[draw_FramePhase] * 1000.0,
                timing_.phases[other_FramePhase] * 1000.0,
                timing_.present * 1000.0);
    }
    count_(busy);
}

void addFrame_FrameTiming(const char *label, double seconds, double presentSeconds) {
    if (!isEnabled_FrameTiming()) {
        return;
    }
    seconds -= presentSeconds;
    if (seconds >= timing_.budget) {
        timing_.numOver++;
        fprintf(stderr, "[Frame] %6.2f ms: %s\n", seconds * 1000.0, label);
    }
    count_(seconds);
}

double now_FrameTiming(void) {
    return seconds_(SDL_GetPerformanceCounter());
}

double totalPresent_FrameTiming(void) {
    return timing_.totalPresent;
}

void beginIdle_FrameTiming(void) {
    if (timing_.isEnabled > 0 && timing_.idleDepth < 4) {
        timing_.idleStart[timing_.idleDepth] = SDL_GetPerformanceCounter();
    }
    timing_.idleDepth++;
}

void endIdle_FrameTiming(enum iFrameIdle idle) {
    timing_.idleDepth--;
    if (timing_.isEnabled > 0 && timing_.idleDepth < 4) {
        const uint64_t ticks = SDL_GetPerformanceCounter() - timing_.idleStart[timing_.idleDepth];
        if (timing_.idleDepth == 0) {
            timing_.idleTicks += ticks;
        }
        if (idle == present_FrameIdle) {
            timing_.present += seconds_(ticks);
            timing_.totalPresent += seconds_(ticks);
        }
    }
}
