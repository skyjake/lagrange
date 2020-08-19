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
    iBool         isDrawFrozen; /* avoids premature draws while restoring window state */
    SDL_Renderer *render;
    iWidget *     root;
    float         pixelRatio;
    float         uiScale;
    uint32_t      frameTime;
    double        presentTime;
    SDL_Cursor *  cursors[SDL_NUM_SYSTEM_CURSORS];
};

iBool       processEvent_Window     (iWindow *, const SDL_Event *);
void        draw_Window             (iWindow *);
void        resize_Window           (iWindow *, int w, int h);
void        setTitle_Window         (iWindow *, const iString *title);
void        setUiScale_Window       (iWindow *, float uiScale);
void        setFreezeDraw_Window    (iWindow *, iBool freezeDraw);
void        setCursor_Window        (iWindow *, int cursor);

iInt2       rootSize_Window         (const iWindow *);
float       uiScale_Window          (const iWindow *);
iInt2       coord_Window            (const iWindow *, int x, int y);
iInt2       mouseCoord_Window       (const iWindow *);
uint32_t    frameTime_Window        (const iWindow *);
SDL_Renderer *renderer_Window       (const iWindow *);

iWindow *   get_Window              (void);
