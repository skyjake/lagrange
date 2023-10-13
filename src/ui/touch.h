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

#pragma once

#include <the_Foundation/vec2.h>
#include <SDL_events.h>

iDeclareType(Widget)

enum iWidgetTouchMode {
    none_WidgetTouchMode,
    touch_WidgetTouchMode,
    momentum_WidgetTouchMode,
};

iBool   processEvent_Touch      (const SDL_Event *);
void    update_Touch            (void);

void                    clear_Touch                 (void); /* forget all ongoing touches */
float                   stopWidgetMomentum_Touch    (const iWidget *widget); /* pixels per second */
enum iWidgetTouchMode   widgetMode_Touch            (const iWidget *widget);
void                    widgetDestroyed_Touch       (iWidget *widget);
void                    transferAffinity_Touch      (iWidget *src, iWidget *dst);
iBool                   hasAffinity_Touch           (const iWidget *);

iInt2   latestPosition_Touch    (void); /* valid during processing of current event */
iInt2   latestTapPosition_Touch (void);
iBool   isHovering_Touch        (void); /* stationary touch or a long-press drag ongoing */
size_t  numFingers_Touch        (void);
