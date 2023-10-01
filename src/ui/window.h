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
#include <SDL_mouse.h>
#include <SDL_video.h>

extern const iMenuItem topLevelMenus_Window[7];

enum iWindowType {
    main_WindowType,
    extra_WindowType,
    popup_WindowType,
};

iDeclareType(MainWindow)
iDeclareType(Text)
iDeclareType(Window)

iDeclareTypeConstructionArgs(Window, enum iWindowType type, iRect rect, uint32_t flags)
iDeclareTypeConstructionArgs(MainWindow, iRect rect)

typedef iAny iAnyWindow;

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

enum iWindowSplit {
    vertical_WindowSplit = iBit(1),
    oneToTwo_WindowSplit = iBit(2),
    twoToOne_WindowSplit = iBit(3),
    equal_WindowSplit    = oneToTwo_WindowSplit | twoToOne_WindowSplit,
    /* meta */
    mode_WindowSplit     = vertical_WindowSplit | equal_WindowSplit,
    mask_WindowSplit     = equal_WindowSplit,
    merge_WindowSplit    = iBit(10),
    noEvents_WindowSplit = iBit(11),
};

struct Impl_Window {
    enum iWindowType type;
    uint32_t      serial; /* incrementing serial number; creation order */
    SDL_Window *  win;
    iBool         isExposed;
    iBool         isMinimized;
    iBool         isMouseInside;
    iBool         isInvalidated;
    iAtomicInt    isRefreshPending;
    iBool         ignoreClick; /* used on the Windows platform only */
    uint32_t      focusGainedAt;
    SDL_Renderer *render;
    iInt2         size;
    iWidget *     hover;
    iWidget *     lastHover;    /* cleared if deleted */
    iWidget *     mouseGrab;
    iWidget *     focus;
    float         pixelRatio;   /* conversion between points and pixels, e.g., coords, window size */
    float         displayScale; /* DPI-based scaling factor of current display, affects uiScale only */
    float         uiScale;
    uint32_t      frameTime;
    SDL_Cursor *  cursors[SDL_NUM_SYSTEM_CURSORS];
    SDL_Cursor *  pendingCursor;
    iRoot *       roots[2];     /* root widget and UI state; second one is for split mode */
    iRoot *       keyRoot;      /* root that has the current keyboard input focus */
    SDL_Texture * borderShadow;
    iText *       text;
    unsigned int  frameCount;
};

struct Impl_MainWindow {
    iWindow       base;
    iWindowPlacement place;
    iBool         isDrawFrozen; /* avoids premature draws while restoring window state */
    int           splitMode;
    int           pendingSplitMode;
    iString *     pendingSplitUrl; /* URL to open in a newly opened split */
    iString *     pendingSplitOrigin; /* tab from where split was initiated, if any */
    iString *     pendingSplitSetIdent;
    SDL_Texture * appIcon;
    SDL_Texture * logo;
    int           keyboardHeight; /* mobile software keyboards */
    int           maxDrawableHeight;
    iBool         enableBackBuf; /* only used on macOS with Metal (helps with refresh glitches for some reason??) */
    SDL_Texture * backBuf; /* enables refreshing the window without redrawing anything */
};

iLocalDef enum iWindowType type_Window(const iAnyWindow *d) {
    if (d) {
        return ((const iWindow *) d)->type;
    }
    return main_WindowType;
}

uint32_t        id_Window               (const iWindow *);
iInt2           size_Window             (const iWindow *);
iInt2           maxTextureSize_Window   (const iWindow *);
float           uiScale_Window          (const iWindow *);
iInt2           coord_Window            (const iWindow *, int x, int y);
iInt2           mouseCoord_Window       (const iWindow *, int whichDevice);
iAnyObject *    hitChild_Window         (const iWindow *, iInt2 coord);
uint32_t        frameTime_Window        (const iWindow *);
SDL_Renderer *  renderer_Window         (const iWindow *);
int             numRoots_Window         (const iWindow *);
//iRoot *         findRoot_Window         (const iWindow *, const iWidget *widget);
iRoot *         otherRoot_Window        (const iWindow *, iRoot *root);
void            rootOrder_Window        (const iWindow *, iRoot *roots[2]);

void        setTitle_Window         (iWindow *, const iString *title);
iBool       processEvent_Window     (iWindow *, const SDL_Event *);
iBool       dispatchEvent_Window    (iWindow *, const SDL_Event *);
void        invalidate_Window       (iAnyWindow *); /* discard all cached graphics */
void        draw_Window             (iWindow *);
void        setUiScale_Window       (iWindow *, float uiScale);
void        setCursor_Window        (iWindow *, int cursor);
iBool       setKeyRoot_Window       (iWindow *, iRoot *root);
iBool       postContextClick_Window (iWindow *, const SDL_MouseButtonEvent *);
void        updateHover_Window      (iWindow *);

iWindow *   get_Window              (void);
iBool       isOpenGLRenderer_Window (void);

void        setCurrent_Window       (iAnyWindow *);
void        postRefresh_Window      (iAnyWindow *);

iLocalDef iBool isExposed_Window(const iWindow *d) {
    iAssert(d);
    return d->isExposed;
}

iLocalDef iBool isDrawFrozen_Window(const iWindow *d) {
    if (d && d->type == main_WindowType) {
        return ((const iMainWindow *) d)->isDrawFrozen;
    }
    return iFalse;
}

iLocalDef iWindow *as_Window(iAnyWindow *d) {
    iAssert(type_Window(d) == main_WindowType || type_Window(d) == extra_WindowType ||
            type_Window(d) == popup_WindowType);
    return (iWindow *) d;
}

iLocalDef const iWindow *constAs_Window(const iAnyWindow *d) {
    iAssert(type_Window(d) == main_WindowType || type_Window(d) == extra_WindowType ||
            type_Window(d) == popup_WindowType);
    return (const iWindow *) d;
}

iLocalDef iText *text_Window(const iAnyWindow *d) {
    return constAs_Window(d)->text;
}

/*----------------------------------------------------------------------------------------------*/

iLocalDef iWindow *asWindow_MainWindow(iMainWindow *d) {
    iAssert(type_Window(d) == main_WindowType);
    return &d->base;
}

void        setSnap_MainWindow              (iMainWindow *, int snapMode);
void        setFreezeDraw_MainWindow        (iMainWindow *, iBool freezeDraw);
void        setKeyboardHeight_MainWindow    (iMainWindow *, int height);
iObjectList *listDocuments_MainWindow       (iMainWindow *, const iRoot *rootOrNull);
iBool       isAnyDocumentRequestOngoing_MainWindow   (iMainWindow *);
void        setSplitMode_MainWindow         (iMainWindow *, int splitMode);
void        checkPendingSplit_MainWindow    (iMainWindow *);
void        swapRoots_MainWindow            (iMainWindow *);
void        resize_MainWindow               (iMainWindow *, int w, int h);
void        resizeSplits_MainWindow         (iMainWindow *, iBool updateDocumentSize);
void        draw_MainWindow                 (iMainWindow *);
void        drawQuick_MainWindow            (iMainWindow *);
void        drawLogo_MainWindow             (iMainWindow *, iRect bounds);
void        drawWhileResizing_MainWindow    (iMainWindow *, int w, int h); /* workaround for SDL bug */

int         snap_MainWindow                 (const iMainWindow *);
iBool       isFullscreen_MainWindow         (const iMainWindow *);

iLocalDef int defaultSplitAxis_MainWindow(const iMainWindow *d) {
    return (float) d->base.size.x / (float) d->base.size.y < 0.7f ? 1 : 0;
}

#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
SDL_HitTestResult hitTest_MainWindow(const iMainWindow *d, iInt2 pos);
#endif

iMainWindow *   get_MainWindow  (void);

iLocalDef iMainWindow *as_MainWindow(iAnyWindow *d) {
    iAssert(type_Window(d) == main_WindowType);
    return (iMainWindow *) d;
}

iLocalDef const iMainWindow *constAs_MainWindow(const iAnyWindow *d) {
    iAssert(type_Window(d) == main_WindowType);
    return (const iMainWindow *) d;
}

/*----------------------------------------------------------------------------------------------*/

iWindow *   newPopup_Window     (iInt2 screenPos, iWidget *rootWidget);
iWindow *   newExtra_Window     (iWidget *rootWidget);

/*----------------------------------------------------------------------------------------------*/

const iArray *  updateBookmarksMenu_Widget  (iWidget *menu);
void            cleanupBookmarksMenu_Widget (iWidget *menu);
