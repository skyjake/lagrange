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

#include "root.h"

#include <the_Foundation/rect.h>
#include <SDL_events.h>
#include <SDL_render.h>
#include <SDL_video.h>

iDeclareType(Window)
iDeclareTypeConstructionArgs(Window, iRect rect)

enum iWindowSnap {
    none_WindowSnap       = 0,
    left_WindowSnap       = 1,
    right_WindowSnap      = 2,
    maximized_WindowSnap  = 3,
    yMaximized_WindowSnap = 4,
    fullscreen_WindowSnap = 5,
    mask_WindowSnap       = 0xff,
    topBit_WindowSnap     = iBit(9),
    bottomBit_WindowSnap  = iBit(10),
    redo_WindowSnap       = iBit(11),
};

iDeclareType(WindowPlacement)

/* Tracking of window placement. */
struct Impl_WindowPlacement {
    iInt2 initialPos;
    iRect normalRect;       /* updated when window is moved/resized */
    iInt2 lastNotifiedSize; /* keep track of horizontal/vertical notifications */
    int   snap;             /* LAGRANGE_ENABLE_CUSTOM_FRAME */
    int   lastHit;
};

struct Impl_Window {
    SDL_Window *  win;
    iWindowPlacement place;
    iBool         isDrawFrozen; /* avoids premature draws while restoring window state */
    iBool         isExposed;
    iBool         isMinimized;
    iBool         isMouseInside;
    iBool         ignoreClick;
    uint32_t      focusGainedAt;
    SDL_Renderer *render;
    iInt2         size;
    iRoot *       roots[2];     /* root widget and UI state; second one is for split mode */
    float         pixelRatio;   /* conversion between points and pixels, e.g., coords, window size */
    float         displayScale; /* DPI-based scaling factor of current display, affects uiScale only */
    float         uiScale;
    uint32_t      frameTime;
    double        presentTime;
    SDL_Texture * appIcon;
    SDL_Texture * borderShadow;
    SDL_Cursor *  cursors[SDL_NUM_SYSTEM_CURSORS];
    SDL_Cursor *  pendingCursor;
    int           loadAnimTimer;
    iAnim         rootOffset;
    int           keyboardHeight; /* mobile software keyboards */
};

iBool       processEvent_Window     (iWindow *, const SDL_Event *);
iBool       dispatchEvent_Window    (iWindow *, const SDL_Event *);
void        draw_Window             (iWindow *);
void        drawWhileResizing_Window(iWindow *d, int w, int h); /* workaround for SDL bug */
void        resize_Window           (iWindow *, int w, int h);
void        setTitle_Window         (iWindow *, const iString *title);
void        setUiScale_Window       (iWindow *, float uiScale);
void        setFreezeDraw_Window    (iWindow *, iBool freezeDraw);
void        setCursor_Window        (iWindow *, int cursor);
void        setSnap_Window          (iWindow *, int snapMode);
void        setKeyboardHeight_Window(iWindow *, int height);
void        showToolbars_Window     (iWindow *, iBool show);
iBool       postContextClick_Window (iWindow *, const SDL_MouseButtonEvent *);

uint32_t    id_Window               (const iWindow *);
iInt2       size_Window             (const iWindow *);
iInt2       maxTextureSize_Window   (const iWindow *);
float       uiScale_Window          (const iWindow *);
iInt2       coord_Window            (const iWindow *, int x, int y);
iInt2       mouseCoord_Window       (const iWindow *);
iAnyObject *hitChild_Window         (const iWindow *, iInt2 coord);
uint32_t    frameTime_Window        (const iWindow *);
SDL_Renderer *renderer_Window       (const iWindow *);
int         snap_Window             (const iWindow *);
iBool       isFullscreen_Window     (const iWindow *);
iRoot *     findRoot_Window         (const iWindow *, const iWidget *widget);

iWindow *   get_Window              (void);

#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
SDL_HitTestResult hitTest_Window(const iWindow *d, iInt2 pos);
#endif

iLocalDef iBool isExposed_Window(const iWindow *d) {
    iAssert(d);
    return d->isExposed;
}
