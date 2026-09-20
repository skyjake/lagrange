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

#include "bookmarkdialogs.h"
#include "dialog.h"
#include "importdialogs.h"
#include "menu.h"
#include "miscdialogs.h"
#include "mobile.h"
#include "tabs.h"
#include <lagrange/gmcerts.h>

#include <the_Foundation/array.h>
#include <the_Foundation/string.h>
#include <the_Foundation/rect.h>
#include <the_Foundation/vec2.h>
#include <SDL3/SDL_events.h>
#include <ctype.h>

iDeclareType(Click)
iDeclareType(GmIdentity)
iDeclareType(LabelWidget)
iDeclareType(InputWidget)
iDeclareType(Widget)
iDeclareType(Window)

iBool           isCommand_SDLEvent  (const SDL_Event *d);
iBool           isCommand_UserEvent (const SDL_Event *, const char *cmd);
const char *    command_UserEvent   (const SDL_Event *);

iLocalDef iBool isResize_UserEvent(const SDL_Event *d) {
    return isCommand_UserEvent(d, "window.resized");
}
iLocalDef iBool isMetricsChange_UserEvent(const SDL_Event *d) {
    return isCommand_UserEvent(d, "metrics.changed");
}

iLocalDef iBool isEmulatedMouseDevice_UserEvent (const SDL_Event *d) {
    return (d->type == SDL_EVENT_MOUSE_BUTTON_DOWN || d->type == SDL_EVENT_MOUSE_BUTTON_UP) &&
           d->button.which & 1024;
}

void    emulateMouseClick_Widget    (const iWidget *, int button);
void    emulateMouseClickPos_Widget (const iWidget *, int button, iInt2 clickPos);

enum iMouseWheelFlag {
    /* Note: A future version of SDL may support per-pixel scrolling, but 2.0.x doesn't. */
    perPixel_MouseWheelFlag       = iBit(9), /* e.g., trackpad or finger scroll; applied to `direction` */
    inertia_MouseWheelFlag        = iBit(10),
    scrollFinished_MouseWheelFlag = iBit(11),
};

iLocalDef void setPerPixel_MouseWheelEvent(SDL_MouseWheelEvent *ev, iBool set) {
    iChangeFlags(ev->direction, perPixel_MouseWheelFlag, set);
}
iLocalDef void setInertia_MouseWheelEvent(SDL_MouseWheelEvent *ev, iBool set) {
    iChangeFlags(ev->direction, inertia_MouseWheelFlag, set);
}
iLocalDef void setScrollFinished_MouseWheelEvent(SDL_MouseWheelEvent *ev, iBool set) {
    iChangeFlags(ev->direction, scrollFinished_MouseWheelFlag, set);
}

iLocalDef iBool isPerPixel_MouseWheelEvent(const SDL_MouseWheelEvent *ev) {
    return (ev->direction & perPixel_MouseWheelFlag) != 0;
}
iLocalDef iBool isInertia_MouseWheelEvent(const SDL_MouseWheelEvent *ev) {
    return (ev->direction & inertia_MouseWheelFlag) != 0;
}
iLocalDef iBool isScrollFinished_MouseWheelEvent(const SDL_MouseWheelEvent *ev) {
    return (ev->direction & scrollFinished_MouseWheelFlag) != 0;
}

iInt2   mouseCoord_SDLEvent     (const SDL_Event *);
iInt2   coord_MouseWheelEvent   (const SDL_MouseWheelEvent *);

#if defined (iPlatformTerminal)
#   define KMOD_PRIMARY     SDL_KMOD_CTRL
#   define KMOD_SECONDARY   SDL_KMOD_ALT
#   define KMOD_TERTIARY    SDL_KMOD_CTRL | SDL_KMOD_ALT  /* TODO: does this work? */
#   define KMOD_ACCEPT      SDL_KMOD_ALT
#   define KMOD_UNDO        SDL_KMOD_ALT
#   define KMOD_ZOOM        0
#elif defined (iPlatformApple)
#   define KMOD_PRIMARY     SDL_KMOD_GUI
#   define KMOD_SECONDARY   SDL_KMOD_GUI | SDL_KMOD_SHIFT
#   define KMOD_TERTIARY    SDL_KMOD_GUI | SDL_KMOD_SHIFT | SDL_KMOD_ALT
#   define KMOD_ACCEPT      KMOD_PRIMARY
#   define KMOD_UNDO        KMOD_PRIMARY
#   define KMOD_ZOOM        KMOD_PRIMARY
#else
#   define KMOD_PRIMARY     SDL_KMOD_CTRL
#   define KMOD_SECONDARY   SDL_KMOD_CTRL | SDL_KMOD_SHIFT
#   define KMOD_TERTIARY    SDL_KMOD_CTRL | SDL_KMOD_SHIFT | SDL_KMOD_ALT
#   define KMOD_ACCEPT      KMOD_PRIMARY
#   define KMOD_UNDO        KMOD_PRIMARY
#   define KMOD_ZOOM        KMOD_PRIMARY
#endif

enum iOpenTabFlag {
    new_OpenTabFlag           = iBit(1),
    newBackground_OpenTabFlag = iBit(2),
    newTabMask_OpenTabFlag    = new_OpenTabFlag | newBackground_OpenTabFlag,
    otherRoot_OpenTabFlag     = iBit(3),
};

iBool       isMod_Sym           (int key);
int         normalizedMod_Sym   (int key);
int         keyMods_Sym         (int kmods); /* shift, alt, control, or gui */
void        toString_Sym        (int key, int kmods, iString *str);
int         openTabMode_Sym     (int kmods); /* returns OpenTabFlags */

iRangei     intersect_Rangei    (iRangei a, iRangei b);
iRangei     union_Rangei        (iRangei a, iRangei b);

iLocalDef iBool equal_Rangei(iRangei a, iRangei b) {
    return a.start == b.start && a.end == b.end;
}
iLocalDef iBool isEmpty_Rangei(iRangei d) {
    return size_Range(&d) == 0;
}
iLocalDef iBool contains_Rangei(iRangei a, int b) {
    return b >= a.start && b < a.end;
}
iLocalDef iBool isOverlapping_Rangei(iRangei a, iRangei b) {
    return !isEmpty_Rangei(intersect_Rangei(a, b));
}

enum iRangeExtension {
    word_RangeExtension            = iBit(1),
    line_RangeExtension            = iBit(2),
    moveStart_RangeExtension       = iBit(3),
    moveEnd_RangeExtension         = iBit(4),
    bothStartAndEnd_RangeExtension = moveStart_RangeExtension | moveEnd_RangeExtension,
};

void        extendRange_Rangecc     (iRangecc *, iRangecc bounds, int mode);

iBool       isSelectionBreaking_Char(iChar);

/*-----------------------------------------------------------------------------------------------*/

iDeclareType(Anim)

enum iAnimFlag {
    indefinite_AnimFlag = iBit(1), /* does not end; must be linear */
    easeIn_AnimFlag     = iBit(2),
    easeOut_AnimFlag    = iBit(3),
    easeBoth_AnimFlag   = easeIn_AnimFlag | easeOut_AnimFlag,
    softer_AnimFlag     = iBit(4),
    muchSofter_AnimFlag = iBit(5),
    bounce_AnimFlag     = iBit(6),
};

struct Impl_Anim {
    float    from, to, bounce;
    uint32_t when, due;
    int      flags;
};

void    init_Anim           (iAnim *, float value);
void    setValue_Anim       (iAnim *, float to, uint32_t span);
void    setValueSpeed_Anim  (iAnim *, float to, float unitsPerSecond);
void    setValueEased_Anim  (iAnim *, float to, uint32_t span);
void    setFlags_Anim       (iAnim *, int flags, iBool set);
void    stop_Anim           (iAnim *);

iBool   isFinished_Anim     (const iAnim *);
float   pos_Anim            (const iAnim *);
float   value_Anim          (const iAnim *);

iLocalDef float targetValue_Anim(const iAnim *d) {
    return d->to;
}
iLocalDef iBool isLinear_Anim(const iAnim *d) {
    return (d->flags & (easeIn_AnimFlag | easeOut_AnimFlag)) == 0;
}

/*-----------------------------------------------------------------------------------------------*/

enum iClickResult {
    none_ClickResult,
    started_ClickResult,
    drag_ClickResult,
    finished_ClickResult,
    aborted_ClickResult,
};

struct Impl_Click {
    int      buttons; /* all recognized buttons */
    int      clickButton; /* currently active click */
    iBool    isActive;
    iBool    isDragging;
    int      count;
    iWidget *bounds;
    int      minHeight;
    int      minDrag;
    iInt2    startPos;
    iInt2    pos;
};

void                init_Click          (iClick *, iAnyObject *widget, int button);
void                initButtons_Click   (iClick *, iAnyObject *widget, int buttonMask);
enum iClickResult   processEvent_Click  (iClick *, const SDL_Event *event);
void                cancel_Click        (iClick *);

iBool               isMoved_Click       (const iClick *);
iInt2               pos_Click           (const iClick *);
iRect               rect_Click          (const iClick *);
iInt2               delta_Click         (const iClick *);
iBool               contains_Click      (const iClick *, iInt2 coord);

/*-----------------------------------------------------------------------------------------------*/

iDeclareType(SmoothScroll)

typedef void (*iSmoothScrollNotifyFunc)(iAnyObject *, int offset, uint32_t span);

enum iSmoothScrollFlags {
    pullDownAction_SmoothScrollFlag = iBit(1),
    pullUpAction_SmoothScrollFlag = iBit(2),
};

struct Impl_SmoothScroll {
    iAnim    pos;
    int      max;
    int      overscroll;
    iWidget *widget;
    int      flags;
    int      pullActionTriggered;
    iSmoothScrollNotifyFunc notify;
};

void    init_SmoothScroll           (iSmoothScroll *, iWidget *owner, iSmoothScrollNotifyFunc notify);

void    reset_SmoothScroll          (iSmoothScroll *);
void    setMax_SmoothScroll         (iSmoothScroll *, int max);
void    move_SmoothScroll           (iSmoothScroll *, int offset);
void    moveSpan_SmoothScroll       (iSmoothScroll *, int offset, uint32_t span);
iBool   processEvent_SmoothScroll   (iSmoothScroll *, const SDL_Event *ev);

float   pos_SmoothScroll            (const iSmoothScroll *);
iBool   isFinished_SmoothScroll     (const iSmoothScroll *);
float   pullActionPos_SmoothScroll  (const iSmoothScroll *); /* 0...1 */

/*-----------------------------------------------------------------------------------------------*/

iWidget *       makePadding_Widget  (int size);
iLabelWidget *  makeHeading_Widget  (const char *text);
iWidget *       makeHDiv_Widget     (void);
iWidget *       makeVDiv_Widget     (void);
iWidget *       addAction_Widget    (iWidget *parent, int key, int kmods, const char *command);
iBool           isAction_Widget     (const iWidget *);
iBool           isButton_Widget     (const iAnyObject *);

/*-----------------------------------------------------------------------------------------------*/

iWidget *       makeToggle_Widget       (const char *id);
void            setToggle_Widget        (iWidget *toggle, iBool active);

/*-----------------------------------------------------------------------------------------------*/

iChar           removeIconPrefix_String         (iString *);
enum iColorId   removeColorEscapes_String       (iString *);

/*-----------------------------------------------------------------------------------------------*/

iDeclareType(PerfTimer)

struct Impl_PerfTimer {
    uint64_t ticks;
};

void        init_PerfTimer                  (iPerfTimer *);
uint64_t    elapsedMicroseconds_PerfTimer   (const iPerfTimer *);
void        print_PerfTimer                 (const iPerfTimer *, const char *msg);

#define start_PerfTimer(name) iPerfTimer _##name##_PerfTimer; init_PerfTimer(&_##name##_PerfTimer)
#define stop_PerfTimer(name)  print_PerfTimer(&_##name##_PerfTimer, #name)

/*-----------------------------------------------------------------------------------------------*/

#if defined (iPlatformApple)

const char *    systemImageName_Apple   (iChar ch);

#endif
