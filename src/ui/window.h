/* Copyright 2020 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "widget.h"

#include <the_Foundation/rect.h>
#include <SDL_events.h>
#include <SDL_render.h>
#include <SDL_video.h>

iDeclareType(Window)
iDeclareTypeConstructionArgs(Window, iRect rect)

struct Impl_Window {
    SDL_Window *  win;
    iInt2         initialPos;
    iRect         lastRect; /* updated when window is moved/resized */
    iInt2         lastNotifiedSize; /* keep track of horizontal/vertical notifications */
    iBool         isDrawFrozen; /* avoids premature draws while restoring window state */
    iBool         isMouseInside;
    uint32_t      focusGainedAt;
    SDL_Renderer *render;
    iWidget *     root;
    float         pixelRatio;
    float         uiScale;
    uint32_t      frameTime;
    double        presentTime;
    SDL_Cursor *  cursors[SDL_NUM_SYSTEM_CURSORS];
    SDL_Cursor *  pendingCursor;
    int           loadAnimTimer;
};

iBool       processEvent_Window     (iWindow *, const SDL_Event *);
void        draw_Window             (iWindow *);
void        drawWhileResizing_Window(iWindow *d, int w, int h); /* workaround for SDL bug */
void        resize_Window           (iWindow *, int w, int h);
void        setTitle_Window         (iWindow *, const iString *title);
void        setUiScale_Window       (iWindow *, float uiScale);
void        setFreezeDraw_Window    (iWindow *, iBool freezeDraw);
void        setCursor_Window        (iWindow *, int cursor);

uint32_t    id_Window               (const iWindow *);
iInt2       rootSize_Window         (const iWindow *);
float       uiScale_Window          (const iWindow *);
iInt2       coord_Window            (const iWindow *, int x, int y);
iInt2       mouseCoord_Window       (const iWindow *);
uint32_t    frameTime_Window        (const iWindow *);
SDL_Renderer *renderer_Window       (const iWindow *);

iWindow *   get_Window              (void);
