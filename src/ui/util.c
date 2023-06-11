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

#include "util.h"

#include "app.h"
#include "bindingswidget.h"
#include "bookmarks.h"
#include "color.h"
#include "command.h"
#include "defs.h"
#include "documentwidget.h"
#include "export.h"
#include "feeds.h"
#include "gmutil.h"
#include "inputwidget.h"
#include "keys.h"
#include "labelwidget.h"
#include "root.h"
#include "sitespec.h"
#include "text.h"
#include "touch.h"
#include "uploadwidget.h"
#include "widget.h"
#include "window.h"

#if defined (iPlatformAppleMobile)
#   include "../ios.h"
#endif

#if defined (iPlatformAppleDesktop)
#   include "macos.h"
#endif

#if defined (LAGRANGE_ENABLE_X11_XLIB)
#   include "x11.h"
#endif

#include <the_Foundation/math.h>
#include <the_Foundation/path.h>
#include <SDL_timer.h>

iBool isCommand_SDLEvent(const SDL_Event *d) {
    return d->type == SDL_USEREVENT && d->user.code == command_UserEventCode;
}

iBool isCommand_UserEvent(const SDL_Event *d, const char *cmd) {
    return d->type == SDL_USEREVENT && d->user.code == command_UserEventCode &&
           equal_Command(d->user.data1, cmd);
}

const char *command_UserEvent(const SDL_Event *d) {
    if (d->type == SDL_USEREVENT && d->user.code == command_UserEventCode) {
        return d->user.data1;
    }
    return "";
}

void emulateMouseClickPos_Widget(const iWidget *d, int button, iInt2 clickPos) {
    iMainWindow *wnd = get_MainWindow();
    divfv_I2(&clickPos, wnd->base.pixelRatio); /* ratio is multiplied when processing events */
    SDL_MouseButtonEvent ev = { .type      = SDL_MOUSEBUTTONDOWN,
                                .timestamp = SDL_GetTicks(),
                                .windowID  = id_Window(as_Window(wnd)),
                                .which     = 1024,
                                .button    = button,
                                .state     = SDL_PRESSED,
                                1,
                                clickPos.x,
                                clickPos.y };
    SDL_PushEvent((SDL_Event *) &ev);
    ev.type = SDL_MOUSEBUTTONUP;
    ev.state = SDL_RELEASED;
    ev.timestamp++;
    SDL_PushEvent((SDL_Event *) &ev);
}

void emulateMouseClick_Widget(const iWidget *d, int button) {
    return emulateMouseClickPos_Widget(
        d, button, sub_I2(bottomRight_Rect(bounds_Widget(d)), muli_I2(gap2_UI, 2)));
}

iInt2 coord_MouseWheelEvent(const SDL_MouseWheelEvent *ev) {
    iWindow *win = get_Window(); /* may not be the focus window */
#if !defined (iPlatformTerminal)
    if (isDesktop_Platform()) {
        /* We need to figure out where the mouse is in relation to the currently active window.
           It may be outside the actual focus window. */
        iInt2 mousePos, winPos;
        SDL_GetGlobalMouseState(&mousePos.x, &mousePos.y);
        SDL_GetWindowPosition(win->win, &winPos.x, &winPos.y);
        subv_I2(&mousePos, winPos);
        return coord_Window(win, mousePos.x, mousePos.y);
    }
#endif
    return mouseCoord_Window(win, ev->which);
}

iInt2 mouseCoord_SDLEvent(const SDL_Event *ev) {
    switch (ev->type) {
        case SDL_MOUSEMOTION:
            return init_I2(ev->motion.x, ev->motion.y);
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            return init_I2(ev->button.x, ev->button.y);
        case SDL_MOUSEWHEEL:
            return coord_MouseWheelEvent(&ev->wheel);
    }
    return zero_I2();
}

static void removePlus_(iString *str) {
    if (endsWith_String(str, "+")) {
        removeEnd_String(str, 1);
        appendCStr_String(str, " ");
    }
}

void toString_Sym(int key, int kmods, iString *str) {
    if (isTerminal_Platform()) {
        if (kmods & KMOD_CTRL) {
            appendCStr_String(str, "^");
        }
        if (kmods & (KMOD_ALT | KMOD_GUI)) {
            appendCStr_String(str, "M-");
        }
        if (kmods & KMOD_SHIFT) {
            appendCStr_String(str, "Sh-");
        }
        if (key == SDLK_BACKSPACE) {
            removePlus_(str);
            appendCStr_String(str, "BSP"); /* Erase to the Left */
            return;
        }
        else if (key == 0x20) {
            appendCStr_String(str, "SPC");
            return;
        }
        else if (key == SDLK_ESCAPE) {
            removePlus_(str);
            appendCStr_String(str, "ESC"); /* Erase to the Right */
            return;
        }
        else if (key == SDLK_DELETE) {
            removePlus_(str);
            appendCStr_String(str, "DEL"); /* Erase to the Right */
            return;
        }
        else if (key == SDLK_RETURN) {
            removePlus_(str);
            appendCStr_String(str, "RET"); /* Leftwards arrow with a hook */
            return;
        }
    }
    else if (isApple_Platform()) {
        if (kmods & KMOD_CTRL) {
            appendChar_String(str, 0x2303);
        }
        if (kmods & KMOD_ALT) {
            appendChar_String(str, 0x2325);
        }
        if (kmods & KMOD_SHIFT) {
            appendCStr_String(str, shift_Icon);
        }
        if (kmods & KMOD_GUI) {
            appendChar_String(str, 0x2318);
        }
    }
    else {
        if (kmods & KMOD_CTRL) {
            appendCStr_String(str, "Ctrl+");
        }
        if (kmods & KMOD_ALT) {
            appendCStr_String(str, "Alt+");
        }
        if (kmods & KMOD_SHIFT) {
            appendCStr_String(str, shift_Icon "+");
        }
        if (kmods & KMOD_GUI) {
            appendCStr_String(str, "Meta+");
        }
    }
    if (kmods & KMOD_CAPS) {
        appendCStr_String(str, "Caps+");
    }
    if (key == 0x20) {
        appendCStr_String(str, "Space");
    }
    else if (key == SDLK_ESCAPE) {
        appendCStr_String(str, "Esc");
    }
    else if (key == SDLK_PAGEUP) {
        appendCStr_String(str, "PgUp");
    }
    else if (key == SDLK_PAGEDOWN) {
        appendCStr_String(str, "PgDn");
    }
    else if (key == SDLK_LEFT) {
        removePlus_(str);
        appendChar_String(str, 0x2190);
    }
    else if (key == SDLK_RIGHT) {
        removePlus_(str);
        appendChar_String(str, 0x2192);
    }
    else if (key == SDLK_UP) {
        removePlus_(str);
        appendChar_String(str, 0x2191);
    }
    else if (key == SDLK_DOWN) {
        removePlus_(str);
        appendChar_String(str, 0x2193);
    }
    else if (key < 128 && (isalnum(key) || ispunct(key))) {
        if (ispunct(key)) removePlus_(str);
        appendChar_String(str, upper_Char(key));
    }
    else if (key == SDLK_BACKSPACE) {
        removePlus_(str);
        appendChar_String(str, 0x232b); /* Erase to the Left */
    }
    else if (key == SDLK_DELETE) {
        removePlus_(str);
        appendChar_String(str, 0x2326); /* Erase to the Right */
    }
    else if (key == SDLK_RETURN) {
        removePlus_(str);
        appendCStr_String(str, return_Icon); /* Leftwards arrow with a hook */
    }
    else {
        appendCStr_String(str, SDL_GetKeyName(key));
    }
}

iBool isMod_Sym(int key) {
    return key == SDLK_LALT || key == SDLK_RALT || key == SDLK_LCTRL || key == SDLK_RCTRL ||
           key == SDLK_LGUI || key == SDLK_RGUI || key == SDLK_LSHIFT || key == SDLK_RSHIFT ||
           key == SDLK_CAPSLOCK;
}

int normalizedMod_Sym(int key) {
    if (key == SDLK_RSHIFT) key = SDLK_LSHIFT;
    if (key == SDLK_RCTRL) key = SDLK_LCTRL;
    if (key == SDLK_RALT && !isTextInputActive_App()) key = SDLK_LALT;
    if (key == SDLK_RGUI) key = SDLK_LGUI;
    return key;
}

int keyMods_Sym(int kmods) {
    kmods &= (KMOD_SHIFT | KMOD_ALT | KMOD_CTRL | KMOD_GUI | KMOD_CAPS);
    /* Don't treat left/right modifiers differently. */
    if (kmods & KMOD_SHIFT) kmods |= KMOD_SHIFT;
    if (kmods & KMOD_CTRL)  kmods |= KMOD_CTRL;
    if (kmods & KMOD_GUI)   kmods |= KMOD_GUI;
    if (!isTextInputActive_App()) {
        if (kmods & KMOD_ALT) kmods |= KMOD_ALT;
    }
    return kmods;
}

int keyMod_ReturnKeyFlag(int flag) {
    flag &= mask_ReturnKeyFlag;
    const int kmods[4] = { 0, KMOD_SHIFT, KMOD_CTRL, KMOD_GUI };
    if (flag < 0 || flag >= iElemCount(kmods)) return 0;
    return kmods[flag];
}

int openTabMode_Sym(int kmods) {
    const int km = keyMods_Sym(kmods);
    return (km == KMOD_SHIFT ? otherRoot_OpenTabFlag : 0) | /* open to the side */
           (((km & KMOD_PRIMARY) && (km & KMOD_SHIFT)) ? new_OpenTabFlag :
            (km & KMOD_PRIMARY) ? newBackground_OpenTabFlag : 0);
}

iRangei intersect_Rangei(iRangei a, iRangei b) {
    if (a.end < b.start || a.start > b.end) {
        return (iRangei){ 0, 0 };
    }
    return (iRangei){ iMax(a.start, b.start), iMin(a.end, b.end) };
}

iRangei union_Rangei(iRangei a, iRangei b) {
    if (isEmpty_Rangei(a)) return b;
    if (isEmpty_Rangei(b)) return a;
    return (iRangei){ iMin(a.start, b.start), iMax(a.end, b.end) };
}

iBool isSelectionBreaking_Char(iChar c) {
    return isSpace_Char(c) || (c == '@' || c == '-' || c == '/' || c == '\\' || c == ',');
}

static const char *moveBackward_(const char *pos, iRangecc bounds, int mode) {
    iChar ch;
    while (pos > bounds.start) {
        int len = decodePrecedingBytes_MultibyteChar(pos, bounds.start, &ch);
        if (len > 0) {
            if (mode & word_RangeExtension && isSelectionBreaking_Char(ch)) break;
            if (mode & line_RangeExtension && ch == '\n') break;
            pos -= len;
        }
        else break;
    }
    return pos;
}

static const char *moveForward_(const char *pos, iRangecc bounds, int mode) {
    iChar ch;
    while (pos < bounds.end) {
        int len = decodeBytes_MultibyteChar(pos, bounds.end, &ch);
        if (len > 0) {
            if (mode & word_RangeExtension && isSelectionBreaking_Char(ch)) break;
            if (mode & line_RangeExtension && ch == '\n') break;
            pos += len;
        }
        else break;
    }
    return pos;
}

void extendRange_Rangecc(iRangecc *d, iRangecc bounds, int mode) {
    if (!d->start) return;
    if (d->end >= d->start) {
        if (mode & moveStart_RangeExtension) {
            d->start = moveBackward_(d->start, bounds, mode);
        }
        if (mode & moveEnd_RangeExtension) {
            d->end = moveForward_(d->end, bounds, mode);
        }
    }
    else {
        if (mode & moveStart_RangeExtension) {
            d->start = moveForward_(d->start, bounds, mode);
        }
        if (mode & moveEnd_RangeExtension) {
            d->end = moveBackward_(d->end, bounds, mode);
        }
    }
}

/*----------------------------------------------------------------------------------------------*/

iBool isFinished_Anim(const iAnim *d) {
    return d->from == d->to || frameTime_Window(get_Window()) >= d->due;
}

void init_Anim(iAnim *d, float value) {
    d->due = d->when = SDL_GetTicks();
    d->from = d->to = value;
    d->bounce = 0.0f;
    d->flags = 0;
}

iLocalDef float pos_Anim_(const iAnim *d, uint32_t now) {
    return (float) (now - d->when) / (float) (d->due - d->when);
}

iLocalDef float easeIn_(float t) {
    return t * t;
}

iLocalDef float easeOut_(float t) {
    return t * (2.0f - t);
}

iLocalDef float easeBoth_(float t) {
    if (t < 0.5f) {
        return easeIn_(t * 2.0f) * 0.5f;
    }
    return 0.5f + easeOut_((t - 0.5f) * 2.0f) * 0.5f;
}

static float valueAt_Anim_(const iAnim *d, const uint32_t now) {
    if (now >= d->due) {
        return d->to;
    }
    if (now <= d->when) {
        return d->from;
    }
    float t = pos_Anim_(d, now);
    const iBool isSoft     = (d->flags & softer_AnimFlag) != 0;
    const iBool isVerySoft = (d->flags & muchSofter_AnimFlag) != 0;
    if ((d->flags & easeBoth_AnimFlag) == easeBoth_AnimFlag) {
        t = easeBoth_(t);
        if (isSoft) t = easeBoth_(t);
        if (isVerySoft) t = easeBoth_(easeBoth_(t));
    }
    else if (d->flags & easeIn_AnimFlag) {
        t = easeIn_(t);
        if (isSoft) t = easeIn_(t);
        if (isVerySoft) t = easeIn_(easeIn_(t));
    }
    else if (d->flags & easeOut_AnimFlag) {
        t = easeOut_(t);
        if (isSoft) t = easeOut_(t);
        if (isVerySoft) t = easeOut_(easeOut_(t));
    }
    float value = d->from * (1.0f - t) + d->to * t;
    if (d->flags & bounce_AnimFlag) {
        t = (1.0f - easeOut_(easeOut_(t))) * easeOut_(t);
        value += d->bounce * t;
    }
    return value;
}

void setValue_Anim(iAnim *d, float to, uint32_t span) {
    if (span == 0) {
        d->from = d->to = to;
        d->when = d->due = frameTime_Window(get_Window()); /* effectively in the past */
    }
    else if (fabsf(to - d->to) > 0.00001f) {
        const uint32_t now = SDL_GetTicks();
        d->from = valueAt_Anim_(d, now);
        d->to   = to;
        d->when = now;
        d->due  = now + span;
    }
    d->bounce = 0;
}

void setValueSpeed_Anim(iAnim *d, float to, float unitsPerSecond) {
    if (iAbs(d->to - to) > 0.0001f || !isFinished_Anim(d)) {
        const uint32_t now   = SDL_GetTicks();
        const float    from  = valueAt_Anim_(d, now);
        const float    delta = to - from;
        const uint32_t span  = (fabsf(delta) / unitsPerSecond) * 1000;
        d->from              = from;
        d->to                = to;
        d->when              = now;
        d->due               = d->when + span;
        d->bounce            = 0;
    }
}

void setValueEased_Anim(iAnim *d, float to, uint32_t span) {
    if (fabsf(to - d->to) <= 0.00001f) {
        d->to = to; /* Pretty much unchanged. */
        return;
    }
    const uint32_t now = SDL_GetTicks();
    if (isFinished_Anim(d)) {
        d->from  = d->to;
        d->flags = easeBoth_AnimFlag;
    }
    else {
        d->from  = valueAt_Anim_(d, now);
        d->flags = easeOut_AnimFlag;
    }
    d->to     = to;
    d->when   = now;
    d->due    = now + span;
    d->bounce = 0;
}

void setFlags_Anim(iAnim *d, int flags, iBool set) {
    iChangeFlags(d->flags, flags, set);
}

void stop_Anim(iAnim *d) {
    d->from = d->to = value_Anim(d);
    d->when = d->due = SDL_GetTicks();
}

float pos_Anim(const iAnim *d) {
    return pos_Anim_(d, frameTime_Window(get_Window()));
}

float value_Anim(const iAnim *d) {
    return valueAt_Anim_(d, frameTime_Window(get_Window()));
}

/*-----------------------------------------------------------------------------------------------*/

void init_Click(iClick *d, iAnyObject *widget, int button) {
    initButtons_Click(d, widget, button ? SDL_BUTTON(button) : 0);
}

void initButtons_Click(iClick *d, iAnyObject *widget, int buttonMask) {
    d->isActive    = iFalse;
    d->buttons     = buttonMask;
    d->clickButton = 0;
    d->bounds      = as_Widget(widget);
    d->minHeight   = 0;
    d->startPos    = zero_I2();
    d->pos         = zero_I2();
}

iBool contains_Click(const iClick *d, iInt2 coord) {
    if (d->minHeight) {
        iRect rect = bounds_Widget(d->bounds);
        rect.size.y = iMax(d->minHeight, rect.size.y);
        return contains_Rect(rect, coord);
    }
    return contains_Widget(d->bounds, coord);
}

enum iClickResult processEvent_Click(iClick *d, const SDL_Event *event) {
    if (event->type == SDL_MOUSEMOTION) {
        const iInt2 pos = init_I2(event->motion.x, event->motion.y);
        if (d->isActive) {
            d->pos = pos;
            return drag_ClickResult;
        }
    }
    if (event->type != SDL_MOUSEBUTTONDOWN && event->type != SDL_MOUSEBUTTONUP) {
        return none_ClickResult;
    }
    const SDL_MouseButtonEvent *mb = &event->button;
    if (!(SDL_BUTTON(mb->button) & d->buttons)) {
        return none_ClickResult;
    }
    const iInt2 pos = init_I2(mb->x, mb->y);
    if (event->type == SDL_MOUSEBUTTONDOWN && (!d->isActive || d->clickButton == mb->button)) {
        d->count = mb->clicks;
    }
    if (!d->isActive) {
        if (mb->state == SDL_PRESSED) {
            if (contains_Click(d, pos)) {
                d->isActive = iTrue;
                d->clickButton = mb->button;
                d->startPos = d->pos = pos;
                setMouseGrab_Widget(d->bounds);
                return started_ClickResult;
            }
        }
    }
    else { /* Active. */
        if (mb->state == SDL_RELEASED && mb->button == d->clickButton) {
            enum iClickResult result = contains_Click(d, pos)
                                           ? finished_ClickResult
                                           : aborted_ClickResult;
            d->isActive = iFalse;
            d->pos = pos;
            setMouseGrab_Widget(NULL);
            return result;
        }
    }
    return none_ClickResult;
}

void cancel_Click(iClick *d) {
    if (d->isActive) {
        d->isActive = iFalse;
        setMouseGrab_Widget(NULL);
    }
}

iBool isMoved_Click(const iClick *d) {
    return dist_I2(d->startPos, d->pos) > 2;
}

iInt2 pos_Click(const iClick *d) {
    return d->pos;
}

iRect rect_Click(const iClick *d) {
    return initCorners_Rect(min_I2(d->startPos, d->pos), max_I2(d->startPos, d->pos));
}

iInt2 delta_Click(const iClick *d) {
    return sub_I2(d->pos, d->startPos);
}

/*----------------------------------------------------------------------------------------------*/

void init_SmoothScroll(iSmoothScroll *d, iWidget *owner, iSmoothScrollNotifyFunc notify) {
    reset_SmoothScroll(d);
    d->widget = owner;
    d->notify = notify;
    d->pullActionTriggered = 0;
    d->flags = 0;
}

void reset_SmoothScroll(iSmoothScroll *d) {
    init_Anim(&d->pos, 0);
    d->max = 0;
    d->overscroll = (deviceType_App() != desktop_AppDeviceType ? 100 * gap_UI : 0);
    d->pullActionTriggered = 0;
}

void setMax_SmoothScroll(iSmoothScroll *d, int max) {
    max = iMax(0, max);
    if (max != d->max) {
        d->max = max;
        if (targetValue_Anim(&d->pos) > d->max) {
            d->pos.to = d->max;
        }
    }
}

static int overscroll_SmoothScroll_(const iSmoothScroll *d) {
    if (d->overscroll) {
        const int y = value_Anim(&d->pos);
        if (y <= 0) {
            return y;
        }
        if (y >= d->max) {
            return y - d->max;
        }
    }
    return 0;
}

float pos_SmoothScroll(const iSmoothScroll *d) {
    return value_Anim(&d->pos) - overscroll_SmoothScroll_(d) * 0.667f;
}

iBool isFinished_SmoothScroll(const iSmoothScroll *d) {
    return isFinished_Anim(&d->pos);
}

iLocalDef int pullActionThreshold_SmoothScroll_(const iSmoothScroll *d) {
    return d->overscroll * 6 / 10;
}

float pullActionPos_SmoothScroll(const iSmoothScroll *d) {
    if (d->pullActionTriggered >= 1) {
        return 1.0f;
    }
    float pos = overscroll_SmoothScroll_(d);
    if (pos >= 0.0f) {
        return 0.0f;
    }
    pos = -pos / (float) pullActionThreshold_SmoothScroll_(d);
    return iMin(pos, 1.0f);
}

static void checkPullAction_SmoothScroll_(iSmoothScroll *d) {
    if (d->pullActionTriggered == 1 && d->widget) {
        postCommand_Widget(d->widget, "pullaction");
        d->pullActionTriggered = 2; /* pending handling */
    }
}

void moveSpan_SmoothScroll(iSmoothScroll *d, int offset, uint32_t span) {
    if (!isMobile_Platform() && !prefs_App()->smoothScrolling) {
        span = 0; /* always instant */
    }
    int destY = targetValue_Anim(&d->pos) + offset;
    if (d->flags & pullDownAction_SmoothScrollFlag && destY < -pullActionThreshold_SmoothScroll_(d)) {
        if (d->pullActionTriggered == 0) {
            d->pullActionTriggered = iTrue;
#if defined (iPlatformAppleMobile)
            playHapticEffect_iOS(tap_HapticEffect);
#endif
        }
    }
    if (destY < -d->overscroll) {
        destY = -d->overscroll;
    }
    if (destY >= d->max + d->overscroll) {
        destY = d->max + d->overscroll;
    }
    if (span) {
        setValueEased_Anim(&d->pos, destY, span);
    }
    else {
        setValue_Anim(&d->pos, destY, 0);
    }
    if (d->overscroll && widgetMode_Touch(d->widget) == momentum_WidgetTouchMode) {
        const int osDelta = overscroll_SmoothScroll_(d);
        if (osDelta) {
            const float remaining = stopWidgetMomentum_Touch(d->widget);
            span = iMini(1000, 50 * sqrt(remaining / gap_UI));
            setValue_Anim(&d->pos, osDelta < 0 ? 0 : d->max, span);
            d->pos.flags = bounce_AnimFlag | easeOut_AnimFlag | softer_AnimFlag;
            //            printf("remaining: %f  dur: %d\n", remaining, duration);
            d->pos.bounce = (osDelta < 0 ? -1 : 1) *
                            iMini(5 * d->overscroll, remaining * remaining * 0.00005f);
            checkPullAction_SmoothScroll_(d);
        }
    }
    if (d->notify) {
        d->notify(d->widget, offset, span);
    }
}

void move_SmoothScroll(iSmoothScroll *d, int offset) {
    moveSpan_SmoothScroll(d, offset, 0 /* instantly */);
}

iBool processEvent_SmoothScroll(iSmoothScroll *d, const SDL_Event *ev) {
    if (ev->type == SDL_USEREVENT && ev->user.code == widgetTouchEnds_UserEventCode) {
        const int osDelta = overscroll_SmoothScroll_(d);
        if (osDelta) {
            moveSpan_SmoothScroll(d, -osDelta, 100 * sqrt(iAbs(osDelta) / gap_UI));
            d->pos.flags = easeOut_AnimFlag | muchSofter_AnimFlag;
        }
        checkPullAction_SmoothScroll_(d);
        return iTrue;
    }
    return iFalse;
}

/*-----------------------------------------------------------------------------------------------*/

iWidget *makePadding_Widget(int size) {
    iWidget *pad = new_Widget();
    setId_Widget(pad, "padding");
    setFixedSize_Widget(pad, init1_I2(size));
    return pad;
}

iLabelWidget *makeHeading_Widget(const char *text) {
    iLabelWidget *heading = new_LabelWidget(text, NULL);
    setFlags_Widget(as_Widget(heading), frameless_WidgetFlag | alignLeft_WidgetFlag, iTrue);
    setBackgroundColor_Widget(as_Widget(heading), none_ColorId);
    return heading;
}

iWidget *makeVDiv_Widget(void) {
    iWidget *div = new_Widget();
    setFlags_Widget(div, resizeChildren_WidgetFlag | arrangeVertical_WidgetFlag | unhittable_WidgetFlag, iTrue);
    return div;
}

iWidget *makeHDiv_Widget(void) {
    iWidget *div = new_Widget();
    setFlags_Widget(div, resizeChildren_WidgetFlag | arrangeHorizontal_WidgetFlag | unhittable_WidgetFlag, iTrue);
    return div;
}

iWidget *addAction_Widget(iWidget *parent, int key, int kmods, const char *command) {
    iLabelWidget *action = newKeyMods_LabelWidget("", key, kmods, command);
    setFixedSize_Widget(as_Widget(action), zero_I2());
    addChildFlags_Widget(parent, iClob(action), hidden_WidgetFlag);
    return as_Widget(action);
}

iBool isAction_Widget(const iWidget *d) {
    return isInstance_Object(d, &Class_LabelWidget) && isEqual_I2(d->rect.size, zero_I2());
}

iBool isButton_Widget(const iAnyObject *d) {
    return isInstance_Object(d, &Class_LabelWidget) && !isEmpty_String(command_LabelWidget(d));
}

/*-----------------------------------------------------------------------------------------------*/

static iBool isCommandIgnoredByMenus_(const char *cmd) {
    if (equal_Command(cmd, "window.focus.lost") ||
        equal_Command(cmd, "window.focus.gained")) return iTrue;
    /* TODO: Perhaps a common way of indicating which commands are notifications and should not
       be reacted to by menus?! A prefix character could do the trick. */
    return equal_Command(cmd, "media.updated") ||
           equal_Command(cmd, "media.player.update") ||
           startsWith_CStr(cmd, "feeds.update.") ||
           equal_Command(cmd, "bookmarks.request.started") ||
           equal_Command(cmd, "bookmarks.request.finished") ||
           equal_Command(cmd, "bookmarks.changed") ||
           equal_Command(cmd, "document.autoreload") ||
           equal_Command(cmd, "document.reload") ||
           equal_Command(cmd, "document.request.started") ||
           equal_Command(cmd, "document.request.updated") ||
           equal_Command(cmd, "document.request.finished") ||
           equal_Command(cmd, "document.changed") ||
           equal_Command(cmd, "android.keyboard.changed") ||
           equal_Command(cmd, "scrollbar.fade") ||
           equal_Command(cmd, "visited.changed") ||
           (deviceType_App() == desktop_AppDeviceType && equal_Command(cmd, "window.resized")) ||
           equal_Command(cmd, "widget.overflow") ||
           equal_Command(cmd, "metrics.changed") ||
           equal_Command(cmd, "window.reload.update") ||
           equal_Command(cmd, "window.mouse.exited") ||
           equal_Command(cmd, "window.mouse.entered") ||
           equal_Command(cmd, "input.backup") ||
           equal_Command(cmd, "input.ended") ||
           equal_Command(cmd, "focus.gained") ||
           equal_Command(cmd, "focus.lost") ||
           equal_Command(cmd, "tabs.changed") ||
           equal_Command(cmd, "menu.closed") ||
           equal_Command(cmd, "layout.changed") ||
           (equal_Command(cmd, "mouse.clicked") && !arg_Command(cmd)); /* button released */
}

static iLabelWidget *parentMenuButton_(const iWidget *menu) {
    if (isInstance_Object(menu->parent, &Class_LabelWidget)) {
        iLabelWidget *button = (iLabelWidget *) menu->parent;
        if (equal_Command(cstr_String(command_LabelWidget(button)), "menu.open")) {
            return button;
        }
    }
    return NULL;
}

iBool handleMenuCommand_Widget(iWidget *menu, const char *cmd) {
    if (isVisible_Widget(menu)) {
        if (equalWidget_Command(cmd, menu, "menu.opened")) {
            return iFalse;
        }
        if (equal_Command(cmd, "menu.open") && pointer_Command(cmd) == menu->parent) {
            /* Don't reopen self; instead, root will close the menu. */
            return iFalse;
        }
        if ((equal_Command(cmd, "mouse.clicked") || equal_Command(cmd, "mouse.missed")) &&
            arg_Command(cmd)) {
            /* Dismiss open menus when clicking outside them. */
            closeMenu_Widget(menu);
            return iTrue;
        }
        if (equal_Command(cmd, "cancel") && pointerLabel_Command(cmd, "menu") == menu) {
            return iFalse;
        }
        if (equal_Command(cmd, "contextclick") && pointer_Command(cmd) == menu) {
            return iFalse;
        }
        if (deviceType_App() == phone_AppDeviceType && equal_Command(cmd, "keyboard.changed") &&
            arg_Command(cmd) == 0) {
            /* May need to reposition the menu. */
            menu->rect.pos = windowToLocal_Widget(
                menu,
                init_I2(left_Rect(bounds_Widget(menu)),
                        bottom_Rect(safeRect_Root(menu->root)) - menu->rect.size.y));
            return iFalse;
        }
        if (!isCommandIgnoredByMenus_(cmd)) {
            //printf("closemenu being called on %p due to cmd: %s\n", menu, cmd);
            closeMenu_Widget(menu);
        }
    }
    return iFalse;
}

static iWidget *makeMenuSeparator_(void) {
    iWidget *sep = new_Widget();
    setBackgroundColor_Widget(sep, uiSeparator_ColorId);
    sep->rect.size.y = gap_UI / 3;
    if (deviceType_App() != desktop_AppDeviceType) {
        sep->rect.size.y = gap_UI / 2;
    }
    sep->rect.size.y = iMax(1, sep->rect.size.y);
    setFlags_Widget(sep, hover_WidgetFlag | fixedHeight_WidgetFlag, iTrue);
    return sep;
}

void makeMenuItems_Widget(iWidget *menu, const iMenuItem *items, size_t n) {
    const iBool isPortraitPhone = (deviceType_App() == phone_AppDeviceType && isPortrait_App());
    int64_t     itemFlags       = (deviceType_App() != desktop_AppDeviceType ? 0 : 0) |
                                  (isPortraitPhone ? extraPadding_WidgetFlag : 0);
    iBool    haveIcons  = iFalse;
    iWidget *horizGroup = NULL;
    for (size_t i = 0; i < n; ++i) {
        const iMenuItem *item = &items[i];
        if (!item->label) {
            break;
        }
        const char *labelText = item->label;
        if (!startsWith_CStr(labelText, ">>>")) {
            horizGroup = NULL;
        }
        if (equal_CStr(labelText, "---")) {
            addChild_Widget(menu, iClob(makeMenuSeparator_()));
        }
        else {
            iBool isInfo = iFalse;
            iBool isDisabled = iFalse;
            if (startsWith_CStr(labelText, ">>>")) {
                labelText += 3;
                if (!horizGroup) {
                    horizGroup = makeHDiv_Widget();
                    setFlags_Widget(horizGroup, resizeHeightOfChildren_WidgetFlag, iFalse);
                    setFlags_Widget(horizGroup, arrangeHeight_WidgetFlag, iTrue);
                    addChild_Widget(menu, iClob(horizGroup));
                }
            }
            if (startsWith_CStr(labelText, "```")) {
                labelText += 3;
                isInfo = iTrue;
            }
            if (startsWith_CStr(labelText, "///")) {
                labelText += 3;
                isDisabled = iTrue;
            }
            iString labelStr;
            initCStr_String(&labelStr, labelText);
            const iBool isIcon = length_String(&labelStr) == 1;
            iLabelWidget *label = addChildFlags_Widget(
                horizGroup ? horizGroup : menu,
                iClob(isIcon
                    ? newIcon_LabelWidget(labelText, item->key, item->kmods, item->command)
                    : newKeyMods_LabelWidget(labelText, item->key, item->kmods, item->command)),
                noBackground_WidgetFlag | frameless_WidgetFlag |
                    (!isIcon ? alignLeft_WidgetFlag | drawKey_WidgetFlag : 0) |
                    itemFlags);
            deinit_String(&labelStr);
            setWrap_LabelWidget(label, isInfo);
            if (!isInfo) {
                haveIcons |= checkIcon_LabelWidget(label);
            }
            if (isIcon) {
                setTextColor_LabelWidget(label, uiIcon_ColorId);
                setFont_LabelWidget(label, uiLabelMedium_FontId);
            }
            setFlags_Widget(as_Widget(label), disabled_WidgetFlag, isDisabled);
            if (isInfo) {
                setFlags_Widget(as_Widget(label), resizeToParentWidth_WidgetFlag |
                                fixedHeight_WidgetFlag, iTrue); /* wrap changes height */
                setTextColor_LabelWidget(label, uiTextAction_ColorId);
            }
            updateSize_LabelWidget(label); /* drawKey was set */
        }
    }
    if (deviceType_App() == phone_AppDeviceType) {
        addChild_Widget(menu, iClob(makeMenuSeparator_()));
        addChildFlags_Widget(menu,
                             iClob(new_LabelWidget("${cancel}", "cancel")),
                             itemFlags | noBackground_WidgetFlag | frameless_WidgetFlag |
                             alignLeft_WidgetFlag);
    }
    if (haveIcons) {
        /* All items must have icons if at least one of them has. */
        iForEach(ObjectList, i, children_Widget(menu)) {
            if (isInstance_Object(i.object, &Class_LabelWidget)) {
                iLabelWidget *label = i.object;
                if (!isWrapped_LabelWidget(label) && icon_LabelWidget(label) == 0) {
                    setIcon_LabelWidget(label, ' ');
                }
            }
        }
    }
}

static iArray *deepCopyMenuItems_(iWidget *menu, const iMenuItem *items, size_t n) {
    iArray *array = new_Array(sizeof(iMenuItem));
    iString cmd;
    init_String(&cmd);
    for (size_t i = 0; i < n; i++) {
        const iMenuItem *item = &items[i];
        const char *itemCommand = item->command;
#if 0
        if (itemCommand) {
            /* Make it appear the command is coming from the right widget. */
            setCStr_String(&cmd, itemCommand);
            if (!hasLabel_Command(itemCommand, "ptr")) {
                size_t firstSpace = indexOf_String(&cmd, ' ');
                iBlock ptr;
                init_Block(&ptr, 0);
                printf_Block(&ptr, " ptr:%p", menu);
                if (firstSpace != iInvalidPos) {
                    insertData_Block(&cmd.chars, firstSpace, data_Block(&ptr), size_Block(&ptr));
                }
                else {
                    append_Block(&cmd.chars, &ptr);
                }
                deinit_Block(&ptr);
            }
            itemCommand = cstr_String(&cmd);
        }
#endif
        pushBack_Array(array, &(iMenuItem){
            item->label ? iDupStr(item->label) : NULL,
            item->key,
            item->kmods,
            itemCommand ? iDupStr(itemCommand) : NULL /* NOTE: Only works with string commands. */
        });
        if (!item->label) break;
    }
    deinit_String(&cmd);
    return array;
}

static void deleteMenuItems_(iArray *items) {
    iForEach(Array, i, items) {
        iMenuItem *item = i.value;
        free((void *) item->label);
        free((void *) item->command);
    }
    delete_Array(items);
}

void releaseNativeMenu_Widget(iWidget *d) {
    if (flags_Widget(d) & nativeMenu_WidgetFlag) {
        iArray *items = userData_Object(d);
        if (items) {
            iAssert(items);
            releasePopup_SystemMenu(d);
            deleteMenuItems_(items);
            setUserData_Object(d, NULL);
        }
    }
}

void updateSystemMenuFromNativeItems_Widget(iWidget *dropButtonOrMenu) {
# if defined (iPlatformAppleMobile)
    iWidget *menu = dropButtonOrMenu;
    if (~flags_Widget(dropButtonOrMenu) & nativeMenu_WidgetFlag) {
        menu = findChild_Widget(dropButtonOrMenu, "menu");
        if (!menu) {
            return;
        }
    }
    iAssert(menu);
    const iArray *items = userData_Object(menu);
    iAssert(flags_Widget(menu) & nativeMenu_WidgetFlag);
    iAssert(items);
    updateItems_SystemMenu(menu, constData_Array(items), size_Array(items));
#endif
}

void setNativeMenuItems_Widget(iWidget *menu, const iMenuItem *items, size_t n) {
#if defined (LAGRANGE_NATIVE_MENU)
    iAssert(flags_Widget(menu) & nativeMenu_WidgetFlag);
    releaseNativeMenu_Widget(menu);
    setUserData_Object(menu, deepCopyMenuItems_(menu, items, n));
    /* Keyboard shortcuts still need to triggerable via the menu, although the items don't exist. */ {
        releaseChildren_Widget(menu);
        for (size_t i = 0; i < n && items[i].label; i++) {
            const iMenuItem *item = &items[i];
            if (item->key) {
                addAction_Widget(menu, item->key, item->kmods, item->command);
            }
        }
    }
    if (isSupported_SystemMenu()) {
        /* The previous popup was released, so we need a new one. */
        makePopup_SystemMenu(menu);
        updateSystemMenuFromNativeItems_Widget(menu);
    }
#endif
}

iWidget *parentMenu_Widget(iWidget *menuItem) {
    if (parent_Widget(menuItem)) {
        if (!cmp_String(id_Widget(parent_Widget(menuItem)), "menu")) {
            return parent_Widget(menuItem);
        }
        return !cmp_String(
                   id_Widget(as_Widget(back_ObjectList(children_Widget(parent_Widget(menuItem))))),
                   "menu.cancel")
                   ? menuItem->parent
                   : NULL;
    }
    return NULL;
}

iWidget *makeMenu_Widget(iWidget *parent, const iMenuItem *items, size_t n) {
    return makeMenuFlags_Widget(parent, items, n, iFalse);
}

iWidget *makeMenuFlags_Widget(iWidget *parent, const iMenuItem *items, size_t n, iBool allowNative) {
    iWidget *menu = new_Widget();
#if defined (LAGRANGE_NATIVE_MENU)
    if (isDesktop_Platform() || (allowNative && isSupported_SystemMenu())) {
        setFlags_Widget(menu, hidden_WidgetFlag | nativeMenu_WidgetFlag, iTrue);
        addChild_Widget(parent, menu);
        iRelease(menu); /* owned by parent now */
        setUserData_Object(menu, NULL);
        setNativeMenuItems_Widget(menu, items, n);
        if (isAppleMobile_Platform() && makePopup_SystemMenu(menu)) {
            updateItems_SystemMenu(menu, items, n);
        }
        return menu;
    }
#endif
    /* Non-native custom popup menu. This may still be displayed inside a separate window. */
    setDrawBufferEnabled_Widget(menu, iTrue);
    setFrameColor_Widget(menu, uiSeparator_ColorId);
    setBackgroundColor_Widget(menu, uiBackgroundMenu_ColorId);
    if (isTerminal_Platform()) {
        setPadding1_Widget(menu, 3);
    }
    else if (deviceType_App() != desktop_AppDeviceType) {
        setPadding1_Widget(menu, 2 * gap_UI);
    }
    else {
        setPadding1_Widget(menu, gap_UI / 2);
    }
    setFlags_Widget(menu,
                    keepOnTop_WidgetFlag | collapse_WidgetFlag | hidden_WidgetFlag |
                        arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag |
                        resizeChildrenToWidestChild_WidgetFlag | overflowScrollable_WidgetFlag,
                    iTrue);
    makeMenuItems_Widget(menu, items, n);
    addChild_Widget(parent, menu);
    iRelease(menu); /* owned by parent now */
    setCommandHandler_Widget(menu, handleMenuCommand_Widget);
    iWidget *cancel = addAction_Widget(menu, SDLK_ESCAPE, 0, "cancel");
    setId_Widget(cancel, "menu.cancel");
    setFlags_Widget(cancel, disabled_WidgetFlag, iTrue);
    return menu;
}

void openMenu_Widget(iWidget *d, iInt2 windowCoord) {
    openMenuFlags_Widget(d, windowCoord, postCommands_MenuOpenFlags);
}

static void updateMenuItemFonts_Widget_(iWidget *d) {
    const iBool isPortraitPhone = (deviceType_App() == phone_AppDeviceType && isPortrait_App());
    const iBool isMobile        = (deviceType_App() != desktop_AppDeviceType);
    const iBool isSlidePanel    = (flags_Widget(d) & horizontalOffset_WidgetFlag) != 0;
    iForEach(ObjectList, i, children_Widget(d)) {
        if (isInstance_Object(i.object, &Class_LabelWidget)) {
            iLabelWidget *label = i.object;
            const iBool isCaution = startsWith_String(text_LabelWidget(label), uiTextCaution_ColorEscape);
            if (isWrapped_LabelWidget(label)) {
                continue;
            }
            switch (deviceType_App()) {
                case desktop_AppDeviceType:
                    if (font_LabelWidget(label) != uiLabelMedium_FontId) { /* don't touch large icons */
                        setFont_LabelWidget(label, isCaution ? uiLabelBold_FontId : uiLabel_FontId);
                    }
                    break;
                case tablet_AppDeviceType:
                    setFont_LabelWidget(label, isCaution ? uiLabelMediumBold_FontId : uiLabelMedium_FontId);
                    break;
                case phone_AppDeviceType:
                    setFont_LabelWidget(label, isCaution ? uiLabelBigBold_FontId : uiLabelBig_FontId);
                    break;
            }
        }
        else if (childCount_Widget(i.object)) {
            updateMenuItemFonts_Widget_(i.object);
        }
    }
}

iMenuItem *findNativeMenuItem_Widget(iWidget *menu, const char *commandSuffix) {
    iAssert(flags_Widget(menu) & nativeMenu_WidgetFlag);
    iForEach(Array, i, userData_Object(menu)) {
        iMenuItem *item = i.value;
        if (item->command && endsWith_Rangecc(range_CStr(item->command), commandSuffix)) {
            return item;
        }
    }
    return NULL;
}

void setPrefix_NativeMenuItem(iMenuItem *item, const char *prefix, iBool set) {
    if (!item->label) {
        return;
    }
    const iBool hasPrefix = startsWith_CStr(item->label, prefix);
    if (hasPrefix && !set) {
        char *label = iDupStr(item->label + 3);
        free((char *) item->label);
        item->label = label;
    }
    else if (!hasPrefix && set) {
        char *label = malloc(strlen(item->label) + 4);
        memcpy(label, prefix, 3);
        strcpy(label + 3, item->label);
        free((char *) item->label);
        item->label = label;
    }
}

void setSelected_NativeMenuItem(iMenuItem *item, iBool isSelected) {
    if (item) {
        setPrefix_NativeMenuItem(item, "///", iFalse);
        setPrefix_NativeMenuItem(item, "###", isSelected);
    }
}

void setDisabled_NativeMenuItem(iMenuItem *item, iBool isDisabled) {
    if (item) {
        setPrefix_NativeMenuItem(item, "###", iFalse);
        setPrefix_NativeMenuItem(item, "///", isDisabled);
    }
}

void setLabel_NativeMenuItem(iMenuItem *item, const char *label) {
    free((char *) item->label);
    item->label = iDupStr(label);
}

void setMenuItemLabel_Widget(iWidget *menu, const char *command, const char *newLabel, iChar icon) {
    if (flags_Widget(menu) & nativeMenu_WidgetFlag) {
        iArray *items = userData_Object(menu);
        iAssert(items);
        iForEach(Array, i, items) {
            iMenuItem *item = i.value;
            if (item->command && !iCmpStr(item->command, command)) {
                setLabel_NativeMenuItem(item, newLabel);
                break;
            }
        }
        updateSystemMenuFromNativeItems_Widget(menu);
    }
    else {
        iLabelWidget *menuItem = findMenuItem_Widget(menu, command);
        if (menuItem) {
            updateTextCStr_LabelWidget(menuItem, newLabel);
            checkIcon_LabelWidget(menuItem);
            if (icon) {
                setIcon_LabelWidget(menuItem, icon);
                arrange_Widget(menu);
            }
        }
    }
}

void setMenuItemLabelByIndex_Widget(iWidget *menu, size_t index, const char *newLabel) {
    if (!menu) {
        return;
    }
    if (flags_Widget(menu) & nativeMenu_WidgetFlag) {
        iArray *items = userData_Object(menu);
        iAssert(items);
        iAssert(index < size_Array(items));
        setLabel_NativeMenuItem(at_Array(items, index), newLabel);
        updateSystemMenuFromNativeItems_Widget(menu);
    }
    else {
        iLabelWidget *menuItem = child_Widget(menu, index);
        iAssert(isInstance_Object(menuItem, &Class_LabelWidget));
        setTextCStr_LabelWidget(menuItem, newLabel);
        checkIcon_LabelWidget(menuItem);
    }
}

void unselectAllNativeMenuItems_Widget(iWidget *menu) {
    iArray *items = userData_Object(menu);
    iAssert(items);
    iForEach(Array, i, items) {
        setSelected_NativeMenuItem(i.value, iFalse);
    }
}

iLocalDef iBool isUsingMenuPopupWindows_(void) {
#if defined (LAGRANGE_ENABLE_POPUP_MENUS) && !defined (iPlatformTerminal)
#   if defined (LAGRANGE_ENABLE_X11_XLIB)
    if (!isXSession_X11()) {
        return iFalse; /* popup windows not supported on Wayland */
    }
#   endif
    return deviceType_App() == desktop_AppDeviceType;
#else
    return iFalse;
#endif
}

void openMenuFlags_Widget(iWidget *d, iInt2 windowCoord, int menuOpenFlags) {
    const iBool postCommands  = (menuOpenFlags & postCommands_MenuOpenFlags) != 0;
    const iBool isMenuFocused = ((menuOpenFlags & setFocus_MenuOpenFlags) ||
                                 focus_Widget() == parent_Widget(d));
    if (postCommands) {
        postCommandf_App("cancel menu:%p", d); /* dismiss any other menus */
    }
    /* Menu closes when commands are emitted, so handle any pending ones beforehand. */
    processEvents_App(postedEventsOnly_AppEventMode);
#if defined (iPlatformAppleDesktop)
    if (flags_Widget(d) & nativeMenu_WidgetFlag) {
        /* Open a native macOS menu. */
        const iArray *items = userData_Object(d);
        iAssert(items);
        showPopupMenu_MacOS(d, windowCoord, constData_Array(items), size_Array(items));
        return;
    }
#endif
    const iRect rootRect        = rect_Root(d->root);
    const iInt2 rootSize        = rootRect.size;
    const iBool isPhone         = (deviceType_App() == phone_AppDeviceType);
    const iBool isPortraitPhone = (isPhone && isPortrait_App());
    const iBool isSlidePanel    = (flags_Widget(d) & horizontalOffset_WidgetFlag) != 0;
    setFlags_Widget(d, hidden_WidgetFlag, iFalse);
    setFlags_Widget(d, commandOnMouseMiss_WidgetFlag, iTrue);
    setFlags_Widget(findChild_Widget(d, "menu.cancel"), disabled_WidgetFlag, iFalse);
    if (isPhone) {
        setFrameColor_Widget(d, isPortraitPhone ? none_ColorId : uiSeparator_ColorId);
    }
    arrange_Widget(d); /* need to know the height */
    iBool allowOverflow = iFalse;
    /* A vertical offset determined by a possible selected label in the menu. */
    if (deviceType_App() == desktop_AppDeviceType &&
        windowCoord.y < rootSize.y - lineHeight_Text(uiNormal_FontSize) * 3) {
        iConstForEach(ObjectList, child, children_Widget(d)) {
            const iWidget *item = constAs_Widget(child.object);
            if (flags_Widget(item) & selected_WidgetFlag) {
                windowCoord.y -= item->rect.pos.y;
                allowOverflow = iTrue;
            }
        }
    }
    if (isUsingMenuPopupWindows_()) {
        /* Determine total display bounds where the popup may appear. */
        iRect displayRect = zero_Rect();
        for (int i = 0; i < SDL_GetNumVideoDisplays(); i++) {
            SDL_Rect dispBounds;
            SDL_GetDisplayUsableBounds(i, &dispBounds);
            displayRect = union_Rect(
                displayRect, init_Rect(dispBounds.x, dispBounds.y, dispBounds.w, dispBounds.h));
        }
        iRect winRect;
        SDL_Window *sdlWin = get_Window()->win;
        const float pixelRatio = get_Window()->pixelRatio;
        iInt2 winPos;
        SDL_GetWindowPosition(sdlWin, &winPos.x, &winPos.y);
        winRect = rootRect;
        winRect.pos.x /= pixelRatio;
        winRect.pos.y /= pixelRatio;
        winRect.size.x /= pixelRatio;
        winRect.size.y /= pixelRatio;
        addv_I2(&winRect.pos, winPos);
        iRect visibleWinRect = intersect_Rect(winRect, displayRect);
        /* Only use a popup window if the menu can't fit inside the main window. */
        if (height_Widget(d) / pixelRatio > visibleWinRect.size.y ||
            (allowOverflow &&
             (windowCoord.y < 0 || windowCoord.y + height_Widget(d) > get_Window()->size.y))) {
            if (postCommands) {
                postCommand_Widget(d, "menu.opened");
            }
            updateMenuItemFonts_Widget_(d);
            iRoot *oldRoot = current_Root();
            setFlags_Widget(d, keepOnTop_WidgetFlag, iFalse);
            setUserData_Object(d, parent_Widget(d));
            iAssert(userData_Object(d));
            removeChild_Widget(parent_Widget(d), d); /* we'll borrow the widget for a while */
            iInt2 winPos;
            SDL_GetWindowPosition(sdlWin, &winPos.x, &winPos.y);
            iInt2 menuPos = add_I2(winPos,
                                   divf_I2(sub_I2(windowCoord, divi_I2(gap2_UI, 2)), pixelRatio));
            /* Check display bounds. */ {
                iInt2 menuSize = divf_I2(d->rect.size, pixelRatio);
                if (menuOpenFlags & center_MenuOpenFlags) {
                    iInt2 winSize;
                    SDL_GetWindowSize(sdlWin, &winSize.x, &winSize.y);
                    menuPos = sub_I2(add_I2(winPos, divi_I2(winSize, 2)), divi_I2(menuSize, 2));
                }
                menuPos.x = iMin(menuPos.x, right_Rect(displayRect) - menuSize.x);
                menuPos.y = iMax(0, iMin(menuPos.y, bottom_Rect(displayRect) - menuSize.y));
            }
            iWindow *win = newPopup_Window(menuPos, d); /* window takes the widget */
            setCurrent_Window(win);
            SDL_SetWindowTitle(win->win, "Menu");
            arrange_Widget(d);
            addPopup_App(win);
            SDL_ShowWindow(win->win);
            draw_Window(win);
            setCurrent_Window(mainWindow_App());
            setCurrent_Root(oldRoot);
            return;
        }
    }
    raise_Widget(d);
    if (deviceType_App() != desktop_AppDeviceType) {
        setFlags_Widget(d, arrangeWidth_WidgetFlag | resizeChildrenToWidestChild_WidgetFlag,
                        !isPhone);
        setFlags_Widget(d,
                        resizeWidthOfChildren_WidgetFlag | drawBackgroundToBottom_WidgetFlag |
                            drawBackgroundToVerticalSafeArea_WidgetFlag,
                        isPhone);
        if (isPhone) {
            setFlags_Widget(d, borderTop_WidgetFlag, !isSlidePanel && isPortrait_App()); /* menu is otherwise frameless */
            setFixedSize_Widget(d, init_I2(iMin(rootSize.x, rootSize.y), -1));
        }
        else {
            d->rect.size.x = 0;
        }
    }
    updateMenuItemFonts_Widget_(d);
    arrange_Widget(d);
    if (!isSlidePanel) {
        /* LAYOUT BUG: Height of wrapped menu items is incorrect with a single arrange! */
        arrange_Widget(d);
    }
    if (deviceType_App() == phone_AppDeviceType) {
        if (isSlidePanel) {
            d->rect.pos = zero_I2();
        }
        else {
            d->rect.pos = windowToLocal_Widget(d,
                                               init_I2(rootSize.x / 2 - d->rect.size.x / 2,
                                                       rootSize.y));
        }
    }
    else if (menuOpenFlags & center_MenuOpenFlags) {
        d->rect.pos = sub_I2(divi_I2(size_Root(d->root), 2), divi_I2(d->rect.size, 2));
    }
    else {
        d->rect.pos = windowToLocal_Widget(d, windowCoord);
    }
    /* Ensure the full menu is visible. */
    const iRect bounds       = bounds_Widget(d);
    int         leftExcess   = left_Rect(rootRect) - left_Rect(bounds);
    int         rightExcess  = right_Rect(bounds) - right_Rect(rootRect);
    int         topExcess    = top_Rect(rootRect) - top_Rect(bounds);
    int         bottomExcess = bottom_Rect(bounds) - bottom_Rect(rootRect);
#if defined (iPlatformAppleMobile)
    /* Reserve space for the system status bar. */ {
        float l, t, r, b;
        safeAreaInsets_iOS(&l, &t, &r, &b);
        topExcess    += t;
        bottomExcess += iMax(b, get_MainWindow()->keyboardHeight);
        leftExcess   += l;
        rightExcess  += r;
    }
#elif defined (iPlatformMobile)
    /* Reserve space for the keyboard. */
    bottomExcess += get_MainWindow()->keyboardHeight;
#endif
    if (!allowOverflow) {
        if (bottomExcess > 0 && (!isPortraitPhone || !isSlidePanel)) {
            d->rect.pos.y -= bottomExcess;
        }
        if (topExcess > 0) {
            d->rect.pos.y += topExcess;
        }
    }
    if (rightExcess > 0) {
        d->rect.pos.x -= rightExcess;
    }
    if (leftExcess > 0) {
        d->rect.pos.x += leftExcess;
    }
    postRefresh_App();
    if (postCommands) {
        postCommand_Widget(d, "menu.opened");
    }
    setupMenuTransition_Mobile(d, iTrue);
    if (isMenuFocused) {
        iForEach(ObjectList, i, children_Widget(d)) {
            if (flags_Widget(i.object) & focusable_WidgetFlag) {
                setFocus_Widget(i.object);
                break;
            }
        }
    }
}

void closeMenu_Widget(iWidget *d) {
    if (flags_Widget(d) & nativeMenu_WidgetFlag) {
        return; /* Handled natively. */
    }
    if (d == NULL || flags_Widget(d) & hidden_WidgetFlag) {
        return; /* Already closed. */
    }
    iWindow *win = window_Widget(d);
    if (type_Window(win) == popup_WindowType) {
        iWidget *originalParent = userData_Object(d);
        setUserData_Object(d, NULL);
        win->roots[0]->widget = NULL;
        setRoot_Widget(d, originalParent->root);
        addChild_Widget(originalParent, d);
        setFlags_Widget(d, keepOnTop_WidgetFlag, iTrue);
        SDL_HideWindow(win->win);
        collect_Garbage(win, (iDeleteFunc) delete_Window); /* get rid of it after event processing */
    }
    setFlags_Widget(d, hidden_WidgetFlag, iTrue);
    setFlags_Widget(findChild_Widget(d, "menu.cancel"), disabled_WidgetFlag, iTrue);
    iLabelWidget *button = parentMenuButton_(d);
    if (button) {
        setFlags_Widget(as_Widget(button), selected_WidgetFlag, iFalse);
    }
    postRefresh_App();
    postCommand_Widget(d, "menu.closed");
    setupMenuTransition_Mobile(d, iFalse);
}

iWindow *promoteDialogToWindow_Widget(iWidget *dlg) {
    arrange_Widget(dlg);
    removeChild_Widget(parent_Widget(dlg), dlg);
    setVisualOffset_Widget(dlg, 0, 0, 0);
    setFlags_Widget(dlg, horizontalOffset_WidgetFlag | visualOffset_WidgetFlag |
                    centerHorizontal_WidgetFlag | overflowScrollable_WidgetFlag, iFalse);
    /* Check for a dialog heading. */
    iWidget *child = child_Widget(dlg, 0);
    iWindow *x = newExtra_Window(dlg);
    if (isInstance_Object(child, &Class_LabelWidget)) {
        iLabelWidget *heading = (iLabelWidget *) child;
        iString *title = copy_String(sourceText_LabelWidget(heading));
        translate_Lang(title);
        setTitle_Window(x, title);
        delete_String(title);
        iRelease(removeChild_Widget(dlg, heading));
        arrange_Widget(dlg);
    }
    addExtraWindow_App(x);
    SDL_ShowWindow(x->win);
    SDL_RaiseWindow(x->win);
    setActiveWindow_App(x);
    return x;
}

iBool isPromoted_Widget(iWidget *dlg) {
    return type_Window(window_Widget(dlg)) == extra_WindowType;
}

void destroyDialog_Widget(iWidget *dlg) {
    if (isPromoted_Widget(dlg)) {
        if (~dlg->flags & destroyPending_WidgetFlag) {
            /* Let the dialog know that the end is nigh. Otherwise, its dismissal command
               will be automatically posted. */
            setFlags_Widget(dlg, destroyPending_WidgetFlag, iTrue);
            closeWindow_App(window_Widget(dlg));
        }
    }
    else {
        destroy_Widget(dlg);
    }
}

iLabelWidget *findMenuItem_Widget(iWidget *menu, const char *command) {
    iForEach(ObjectList, i, children_Widget(menu)) {
        if (isInstance_Object(i.object, &Class_LabelWidget)) {
            iLabelWidget *menuItem = i.object;
            if (!cmp_String(command_LabelWidget(menuItem), command)) {
                return menuItem;
            }
        }
    }
    return NULL;
}

iWidget *findUserData_Widget(iWidget *d, void *userData) {
    iForEach(ObjectList, i, children_Widget(d)) {
        if (userData_Object(i.object) == userData) {
            return i.object;
        }
    }
    return NULL;
}

void setMenuItemDisabled_Widget(iWidget *menu, const char *command, iBool disable) {
    if (flags_Widget(menu) & nativeMenu_WidgetFlag) {
        setDisabled_NativeMenuItem(findNativeMenuItem_Widget(menu, command), disable);
        updateSystemMenuFromNativeItems_Widget(menu);
    }
    else {
        iLabelWidget *item = findMenuItem_Widget(menu, command);
        if (item) {
            setFlags_Widget(as_Widget(item), disabled_WidgetFlag, disable);
            refresh_Widget(item);
        }
    }
}

void setMenuItemDisabledByIndex_Widget(iWidget *menu, size_t index, iBool disable) {
    if (!menu) {
        return;
    }
    if (flags_Widget(menu) & nativeMenu_WidgetFlag) {
        setDisabled_NativeMenuItem(at_Array(userData_Object(menu), index), disable);
        updateSystemMenuFromNativeItems_Widget(menu);
    }
    else {
        setFlags_Widget(child_Widget(menu, index), disabled_WidgetFlag, disable);
    }
}

int checkContextMenu_Widget(iWidget *menu, const SDL_Event *ev) {
    if (menu && ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_RIGHT) {
        if (isVisible_Widget(menu)) {
            closeMenu_Widget(menu);
            return 0x1;
        }
        const iInt2 mousePos = init_I2(ev->button.x, ev->button.y);
        if (contains_Widget(menu->parent, mousePos)) {
            openMenu_Widget(menu, mousePos);
            if (isEmulatedMouseDevice_UserEvent(ev)) {
                /* Move input focus to the menu since we're using the keyboard. */
                setFocus_Widget(child_Widget(menu, 0));
            }
            return 0x2;
        }
    }
    return 0;
}

iLabelWidget *makeMenuButton_LabelWidget(const char *label, const iMenuItem *items, size_t n) {
    iLabelWidget *button = new_LabelWidget(label, "menu.open");
    iWidget *menu = makeMenuFlags_Widget(as_Widget(button), items, n, iTrue /* allow native */);
    setFrameColor_Widget(menu, uiBackgroundSelected_ColorId);
    setId_Widget(menu, "menu");
    return button;
}

const iString *removeMenuItemLabelPrefixes_String(const iString *d) {
    iString *str = copy_String(d);
    for (;;) {
        if (startsWith_String(str, "###")) {
            remove_Block(&str->chars, 0, 3);
            continue;
        }
        if (startsWith_String(str, "///")) {
            remove_Block(&str->chars, 0, 3);
            continue;
        }
        if (startsWith_String(str, "```")) {
            remove_Block(&str->chars, 0, 3);
            continue;
        }
        break;
    }
    return collect_String(str);
}

static const iString *replaceNewlinesWithDash_(const iString *str) {
    iString *mod = copy_String(str);
    replace_String(mod, "\n", "  ");
    return collect_String(mod);
}

void updateDropdownSelection_LabelWidget(iLabelWidget *dropButton, const char *selectedCommand) {
    if (!dropButton) {
        return;
    }
    iWidget *menu = findChild_Widget(as_Widget(dropButton), "menu");
    if (flags_Widget(menu) & nativeMenu_WidgetFlag) {
        unselectAllNativeMenuItems_Widget(menu);
        iMenuItem *item = findNativeMenuItem_Widget(menu, selectedCommand);
        if (item) {
            setSelected_NativeMenuItem(item, iTrue);
            updateText_LabelWidget(dropButton,
                                   replaceNewlinesWithDash_(removeMenuItemLabelPrefixes_String(
                                       collectNewCStr_String(item->label))));
            checkIcon_LabelWidget(dropButton);
        }
        updateSystemMenuFromNativeItems_Widget(menu);
        return;
    }
    iForEach(ObjectList, i, children_Widget(menu)) {
        if (isInstance_Object(i.object, &Class_LabelWidget)) {
            iLabelWidget *item = i.object;
            const iBool isSelected = endsWith_String(command_LabelWidget(item), selectedCommand);
            setFlags_Widget(as_Widget(item), selected_WidgetFlag, isSelected);
            if (isSelected) {
                updateText_LabelWidget(dropButton,
                                       replaceNewlinesWithDash_(text_LabelWidget(item)));
                checkIcon_LabelWidget(dropButton);
                if (!icon_LabelWidget(dropButton)) {
                    setIcon_LabelWidget(dropButton, icon_LabelWidget(item));
                }
            }
        }
    }
}

const char *selectedDropdownCommand_LabelWidget(const iLabelWidget *dropButton) {
    if (!dropButton) {
        return "";
    }
    iWidget *menu = findChild_Widget(constAs_Widget(dropButton), "menu");
    if (flags_Widget(menu) & nativeMenu_WidgetFlag) {
        iConstForEach(Array, i, userData_Object(menu)) {
            const iMenuItem *item = i.value;
            if (item->label && startsWithCase_CStr(item->label, "###")) {
                return item->command ? item->command : "";
            }
        }
    }
    else {
        iForEach(ObjectList, i, children_Widget(menu)) {
            if (isInstance_Object(i.object, &Class_LabelWidget)) {
                iLabelWidget *item = i.object;
                if (flags_Widget(i.object) & selected_WidgetFlag) {
                    return cstr_String(command_LabelWidget(item));
                }
            }
        }
    }
    return "";
}

/*-----------------------------------------------------------------------------------------------*/

static iWidget *topLevelOpenMenu_(const iWidget *menuBar) {
    iForEach(ObjectList, i, menuBar->children) {
        iWidget *menu = findChild_Widget(i.object, "menu");
        if (isVisible_Widget(menu)) {
            return i.object;
        }
    }
    return NULL;
}

iBool handleTopLevelMenuBarCommand_Widget(iWidget *menuButton, const char *cmd) {
    if (equal_Command(cmd, "mouse.hovered")) {
        /* Only dispatched to the flagged widget. */
        iWidget *menuBar = parent_Widget(menuButton);
        iWidget *openSubmenu = topLevelOpenMenu_(menuBar);
        if (openSubmenu && openSubmenu != menuButton) {
            postCommand_Widget(menuButton, "menu.open under:1 bar:1");
        }
        return iTrue;
    }
    return iFalse;
}

iWidget *makeMenuBar_Widget(const iMenuItem *topLevelMenus, size_t num) {
    iWidget *bar = new_Widget();
    setFlags_Widget(bar, arrangeHorizontal_WidgetFlag | arrangeHeight_WidgetFlag, iTrue);
    setBackgroundColor_Widget(bar, uiBackground_ColorId);
    iString *submenuCmd = collectNewCStr_String("menu.open under:1 bar:1");
    for (size_t i = 0; i < num; i++) {
        const iMenuItem *item     = &topLevelMenus[i];
        const iMenuItem *subItems = item->data;
        iLabelWidget    *submenuButton  =
            makeMenuButton_LabelWidget(item->label, subItems, count_MenuItem(subItems));
        setCommand_LabelWidget(submenuButton, submenuCmd);
        iWidget *submenu = findChild_Widget(as_Widget(submenuButton), "menu");
        setFrameColor_Widget(submenu, uiSeparator_ColorId);
        as_Widget(submenuButton)->padding[0] = gap_UI;
        setCommandHandler_Widget(as_Widget(submenuButton), handleTopLevelMenuBarCommand_Widget);
        updateSize_LabelWidget(submenuButton);
        as_Widget(submenuButton)->flags2 |= commandOnHover_WidgetFlag2;
        addChildFlags_Widget(bar, iClob(submenuButton), frameless_WidgetFlag);
    }
    return bar;
}

/*-----------------------------------------------------------------------------------------------*/

static iBool isTabPage_Widget_(const iWidget *tabs, const iWidget *page) {
    return page && page->parent == findChild_Widget(tabs, "tabs.pages");
}

static void unfocusFocusInsideTabPage_(const iWidget *page) {
    iWidget *focus = focus_Widget();
    if (page && focus && hasParent_Widget(focus, page)) {
//        printf("unfocus inside page: %p\n", focus);
        setFocus_Widget(NULL);
    }
}

static void setFocusInsideTabPage_(iWidget *page) {
    iWidget *focus =
        flags_Widget(page) & focusable_WidgetFlag
            ? page
            : findFocusable_Widget(child_Widget(page, 0),
                                   forward_WidgetFocusDir | notInput_WidgetFocusFlag);
    if (focus == page || hasParent_Widget(focus, page)) {
        setFocus_Widget(focus);
    }
}

static iBool tabSwitcher_(iWidget *tabs, const char *cmd) {
    if (equal_Command(cmd, "tabs.switch")) {
        iWidget *target = pointerLabel_Command(cmd, "page");
        if (!target) {
            target = findChild_Widget(tabs, cstr_Command(cmd, "id"));
        }
        if (!target) return iFalse;
        unfocusFocusInsideTabPage_(currentTabPage_Widget(tabs));
        if (flags_Widget(target) & focusable_WidgetFlag) {
            setFocus_Widget(target);
        }
        if (isTabPage_Widget_(tabs, target)) {
            showTabPage_Widget(tabs, target);
            return iTrue;
        }
        else if (hasParent_Widget(target, tabs)) {
            /* Some widget on a page. */
            while (target && !isTabPage_Widget_(tabs, target)) {
                target = target->parent;
            }
            showTabPage_Widget(tabs, target);
            return iTrue;
        }
    }
    else if (equal_Command(cmd, "tabs.next") || equal_Command(cmd, "tabs.prev")) {
        unfocusFocusInsideTabPage_(currentTabPage_Widget(tabs));
        iWidget *pages = findChild_Widget(tabs, "tabs.pages");
        int tabIndex = 0;
        iConstForEach(ObjectList, i, pages->children) {
            const iWidget *child = constAs_Widget(i.object);
            if (isVisible_Widget(child)) break;
            tabIndex++;
        }
        const int dir = (equal_Command(cmd, "tabs.next") ? +1 : -1);
        /* If out of tabs, rotate to the next set of tabs if one is available. */
        if ((tabIndex == 0 && dir < 0) || (tabIndex == childCount_Widget(pages) - 1 && dir > 0)) {
            iWidget *nextTabs = findChild_Widget(otherRoot_Window(get_Window(), tabs->root)->widget,
                                                 "doctabs");
            iWidget *nextPages = findChild_Widget(nextTabs, "tabs.pages");
            if (nextPages) {
                tabIndex = (int) (dir < 0 ? childCount_Widget(nextPages) - 1 : 0);
                showTabPage_Widget(nextTabs, child_Widget(nextPages, tabIndex));
                postCommand_App("keyroot.next");
            }
        }
        else {
            showTabPage_Widget(tabs, child_Widget(pages, tabIndex + dir));
        }
        if (argLabel_Command(cmd, "keydown")) {
            setFocusInsideTabPage_((iWidget *) currentTabPage_Widget(tabs));
        }
        refresh_Widget(tabs);
        return iTrue;
    }
    return iFalse;
}

iWidget *makeTabs_Widget(iWidget *parent) {
    iWidget *tabs = makeVDiv_Widget();
    iWidget *buttons = addChild_Widget(tabs, iClob(new_Widget()));
    setFlags_Widget(buttons,
                    resizeWidthOfChildren_WidgetFlag | arrangeHorizontal_WidgetFlag |
                        arrangeHeight_WidgetFlag,
                    iTrue);
    setId_Widget(buttons, "tabs.buttons");
//    setBackgroundColor_Widget(buttons, red_ColorId);
    iWidget *content = addChildFlags_Widget(tabs, iClob(makeHDiv_Widget()), expand_WidgetFlag);
    setId_Widget(content, "tabs.content");
    iWidget *pages = addChildFlags_Widget(
        content, iClob(new_Widget()), expand_WidgetFlag | resizeChildren_WidgetFlag);
    setId_Widget(pages, "tabs.pages");
    addChild_Widget(parent, iClob(tabs));
    setCommandHandler_Widget(tabs, tabSwitcher_);
    return tabs;
}

void setTabBarPosition_Widget(iWidget *tabs, iBool atBottom) {
    if (tabs) {
        iWidget *buttons = findChild_Widget(tabs, "tabs.buttons");
        removeChild_Widget(tabs, buttons);
        addChildPos_Widget(tabs, buttons, atBottom ? back_WidgetAddPos : front_WidgetAddPos);
        iRelease(buttons);
    }
}

void setVerticalTabBar_Widget(iWidget *tabs) {
    iWidget *buttons = findChild_Widget(tabs, "tabs.buttons");
    iWidget *content = findChild_Widget(tabs, "tabs.content");
    setFlags_Widget(tabs, arrangeVertical_WidgetFlag, iFalse);
    setFlags_Widget(tabs, arrangeHorizontal_WidgetFlag, iTrue);
    setFlags_Widget(buttons, arrangeHorizontal_WidgetFlag | arrangeHeight_WidgetFlag |
                             resizeWidthOfChildren_WidgetFlag, iFalse);
    setFlags_Widget(buttons, arrangeVertical_WidgetFlag | arrangeWidth_WidgetFlag |
                    resizeChildrenToWidestChild_WidgetFlag, iTrue);
    buttons->flags2 |= centerChildrenVertical_WidgetFlag2;
    setFlags_Widget(content, arrangeHorizontal_WidgetFlag, iFalse);
    setFlags_Widget(content, arrangeVertical_WidgetFlag, iTrue);
}

iBool isVerticalTabBar_Widget(const iWidget *tabs) {
    return (tabs->flags & arrangeVertical_WidgetFlag) == 0;
}

static void addTabPage_Widget_(iWidget *tabs, enum iWidgetAddPos addPos, iWidget *page,
                               const char *label, int key, int kmods) {
    const iBool isVerticalTabs = isVerticalTabBar_Widget(tabs);
    iWidget *   pages   = findChild_Widget(tabs, "tabs.pages");
    const iBool isSel   = childCount_Widget(pages) == 0;
    iWidget *   buttons = findChild_Widget(tabs, "tabs.buttons");
    iWidget *   button  = addChildPos_Widget(
        buttons,
        iClob(newKeyMods_LabelWidget(label, key, kmods, format_CStr("tabs.switch page:%p", page))),
        addPos);
    checkIcon_LabelWidget((iLabelWidget *) button);
    setFlags_Widget(button, selected_WidgetFlag, isSel);
    setFlags_Widget(button,
                    commandOnClick_WidgetFlag |
                        (!isVerticalTabs ? horizontalOffset_WidgetFlag : 0) |
                        (isVerticalTabs ? alignLeft_WidgetFlag : expand_WidgetFlag),
                    iTrue);
    if (!cmp_String(id_Widget(tabs), "doctabs")) {
        /* Document tabs can be reordered.
           TODO: Maybe not hardcode the parent ID here? Could check a flag on `tabs`. */
        button->flags2 |= siblingOrderDraggable_WidgetFlag2;
        if (isMobile_Platform()) {
            button->flags |= touchDrag_WidgetFlag;
        }
    }
    if (prefs_App()->bottomTabBar) {
        setNoBottomFrame_LabelWidget((iLabelWidget *) button, iTrue);
    }
    else {
        setNoTopFrame_LabelWidget((iLabelWidget *) button, iTrue);
    }
    addChildPos_Widget(pages, page, addPos);
    if (tabCount_Widget(tabs) > 1) {
        setFlags_Widget(buttons, hidden_WidgetFlag, iFalse);
    }
    setFlags_Widget(page, hidden_WidgetFlag | disabled_WidgetFlag, !isSel);
}

void appendTabPage_Widget(iWidget *tabs, iWidget *page, const char *label, int key, int kmods) {
    addTabPage_Widget_(tabs, back_WidgetAddPos, page, label, key, kmods);
}

void prependTabPage_Widget(iWidget *tabs, iWidget *page, const char *label, int key, int kmods) {
    addTabPage_Widget_(tabs, front_WidgetAddPos, page, label, key, kmods);
}

void moveTabButtonToEnd_Widget(iWidget *tabButton) {
    iWidget *buttons = tabButton->parent;
    iWidget *tabs    = buttons->parent;
    removeChild_Widget(buttons, tabButton);
    addChild_Widget(buttons, iClob(tabButton));
    arrange_Widget(tabs);
}

iWidget *tabPage_Widget(iWidget *tabs, size_t index) {
    iWidget *pages = findChild_Widget(tabs, "tabs.pages");
    return child_Widget(pages, index);
}

iWidget *removeTabPage_Widget(iWidget *tabs, size_t index) {
    iWidget *buttons = findChild_Widget(tabs, "tabs.buttons");
    iWidget *pages   = findChild_Widget(tabs, "tabs.pages");
    iWidget *button  = removeChild_Widget(buttons, child_Widget(buttons, index));
    iRelease(button);
    iWidget *page = child_Widget(pages, index);
    setFlags_Widget(page, hidden_WidgetFlag | disabled_WidgetFlag, iFalse);
    removeChild_Widget(pages, page); /* `page` is now ours */
    if (tabCount_Widget(tabs) <= 1 && flags_Widget(buttons) & collapse_WidgetFlag) {
        setFlags_Widget(buttons, hidden_WidgetFlag, iTrue);
    }
    return page;
}

void moveTabPage_Widget(iWidget *tabs, size_t index, size_t newIndex) {
    const size_t count = tabCount_Widget(tabs);
    newIndex = iMin(newIndex, count - 1);
    if (index == newIndex) {
        return;
    }
    iWidget *buttons = findChild_Widget(tabs, "tabs.buttons");
    iWidget *pages   = findChild_Widget(tabs, "tabs.pages");
    iWidget *button  = child_Widget(buttons, index);
    iWidget *page    = child_Widget(pages, index);
    changeChildIndex_Widget(buttons, button, newIndex);
    changeChildIndex_Widget(pages, page, newIndex);
    arrange_Widget(tabs);
}

void resizeToLargestPage_Widget(iWidget *tabs) {
    if (!tabs) return;
//    puts("RESIZE TO LARGEST PAGE ...");
    iWidget *pages = findChild_Widget(tabs, "tabs.pages");
    iForEach(ObjectList, i, children_Widget(pages)) {
        setMinSize_Widget(i.object, zero_I2());
        iWidget *w = i.object;
        w->rect.size = zero_I2();
    }
    arrange_Widget(tabs);
    iInt2 largest = zero_I2();
    iConstForEach(ObjectList, j, children_Widget(pages)) {
        const iWidget *page = constAs_Widget(j.object);
        largest = max_I2(largest, page->rect.size);
    }
    iForEach(ObjectList, k, children_Widget(pages)) {
        setMinSize_Widget(k.object, largest);
    }
    const iWidget *buttons = findChild_Widget(tabs, "tabs.buttons");
    setFixedSize_Widget(tabs,
                        isVerticalTabBar_Widget(tabs) ? addX_I2(largest, width_Widget(buttons))
                                                      : addY_I2(largest, height_Widget(buttons)));
//    puts("... DONE WITH RESIZE TO LARGEST PAGE");
}

static iLabelWidget *tabButtonForPage_Widget_(iWidget *tabs, const iWidget *page) {
    iWidget *buttons = findChild_Widget(tabs, "tabs.buttons");
    iForEach(ObjectList, i, buttons->children) {
        iAssert(isInstance_Object(i.object, &Class_LabelWidget));
        iAny *label = i.object;
        if (pointerLabel_Command(cstr_String(command_LabelWidget(label)), "page") == page) {
            return label;
        }
    }
    return NULL;
}

void addTabCloseButton_Widget(iWidget *tabs, const iWidget *page, const char *command) {
    if (deviceType_App() == phone_AppDeviceType) {
        return; /* Close buttons not used on a phone due to lack of space. */
    }
    iLabelWidget *tabButton = tabButtonForPage_Widget_(tabs, page);
    setPadding_Widget(as_Widget(tabButton), 0, 0, 0, gap_UI / 4);
    setFlags_Widget(as_Widget(tabButton), arrangeVertical_WidgetFlag | resizeHeightOfChildren_WidgetFlag, iTrue);
#if defined (iPlatformApple)
    const int64_t edge = moveToParentLeftEdge_WidgetFlag;
#else
    const int64_t edge = moveToParentRightEdge_WidgetFlag;
#endif
    iLabelWidget *close = addChildFlags_Widget(
        as_Widget(tabButton),
        iClob(new_LabelWidget(close_Icon,
                              format_CStr("%s id:%s", command, cstr_String(id_Widget(page))))),
        edge | tight_WidgetFlag | frameless_WidgetFlag | noBackground_WidgetFlag |
            hidden_WidgetFlag | visibleOnParentHover_WidgetFlag);
    if (deviceType_App() != desktop_AppDeviceType) {
        setFlags_Widget(as_Widget(close),
                        hidden_WidgetFlag | visibleOnParentHover_WidgetFlag, iFalse);
    }
    if (deviceType_App() == tablet_AppDeviceType) {
        setFlags_Widget(as_Widget(close), hidden_WidgetFlag | disabledWhenHidden_WidgetFlag, iTrue);
        as_Widget(close)->flags2 |= visibleOnParentSelected_WidgetFlag2;
    }
    setNoAutoMinHeight_LabelWidget(close, iTrue);
    updateSize_LabelWidget(close);
}

void showTabPage_Widget(iWidget *tabs, const iAnyObject *page) {
    if (!page) {
        return;
    }
    /* Select the corresponding button. */ {
        iWidget *buttons = findChild_Widget(tabs, "tabs.buttons");
        iForEach(ObjectList, i, buttons->children) {
            iAssert(isInstance_Object(i.object, &Class_LabelWidget));
            iAny *label = i.object;
            const iBool isSel =
                (pointerLabel_Command(cstr_String(command_LabelWidget(label)), "page") == page);
            setFlags_Widget(label, selected_WidgetFlag, isSel);
        }
    }
    /* Show/hide pages. */ {
        iWidget *pages = findChild_Widget(tabs, "tabs.pages");
        iForEach(ObjectList, i, pages->children) {
            iWidget *child = as_Widget(i.object);
            setFlags_Widget(child, hidden_WidgetFlag | disabled_WidgetFlag, child != page);
        }
        postRefresh_App();
    }
    /* Notify. */
    if (!isEmpty_String(id_Widget(page))) {
        postCommandf_Root(constAs_Widget(page)->root,
                          "tabs.changed id:%s",
                          cstr_String(id_Widget(constAs_Widget(page))));
    }
}

iLabelWidget *tabPageButton_Widget(iWidget *tabs, const iAnyObject *page) {
    return tabButtonForPage_Widget_(tabs, page);
}

iBool isTabButton_Widget(const iWidget *d) {
    return d->parent && cmp_String(id_Widget(d->parent), "tabs.buttons") == 0;
}

void setTabPageLabel_Widget(iWidget *tabs, const iAnyObject *page, const iString *label) {
    iLabelWidget *button = tabButtonForPage_Widget_(tabs, page);
    setText_LabelWidget(button, label);
    arrange_Widget(tabs);
}

size_t tabPageIndex_Widget(const iWidget *tabs, const iAnyObject *page) {
    iWidget *pages = findChild_Widget(tabs, "tabs.pages");
    return indexOfChild_Widget(pages, page);
}

const iWidget *currentTabPage_Widget(const iWidget *tabs) {
    if (tabs) {
        iWidget *pages = findChild_Widget(tabs, "tabs.pages");
        iConstForEach(ObjectList, i, pages->children) {
            if (isVisible_Widget(i.object)) {
                return constAs_Widget(i.object);
            }
        }
    }
    return NULL;
}

size_t tabCount_Widget(const iWidget *tabs) {
    return childCount_Widget(findChild_Widget(tabs, "tabs.pages"));
}

/*-----------------------------------------------------------------------------------------------*/

iWidget *makeSheet_Widget(const char *id) {
    iWidget *sheet = new_Widget();
    setId_Widget(sheet, id);
    useSheetStyle_Widget(sheet);
    return sheet;
}

void useSheetStyle_Widget(iWidget *d) {
    setPadding1_Widget(d, 3 * gap_UI);
    setFrameColor_Widget(d, uiSeparator_ColorId);
    setBackgroundColor_Widget(d, uiBackground_ColorId);
    setFlags_Widget(d,
                    parentCannotResize_WidgetFlag | focusRoot_WidgetFlag | mouseModal_WidgetFlag |
                        keepOnTop_WidgetFlag | arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag |
                        centerHorizontal_WidgetFlag | overflowScrollable_WidgetFlag,
                    iTrue);
}

static iLabelWidget *addDialogTitle_(iWidget *dlg, const char *text, const char *id) {
    iLabelWidget *label = new_LabelWidget(text, NULL);
    addChildFlags_Widget(dlg, iClob(label), alignLeft_WidgetFlag | frameless_WidgetFlag |
                                                resizeToParentWidth_WidgetFlag);
    setAllCaps_LabelWidget(label, iTrue);
    setTextColor_LabelWidget(label, uiHeading_ColorId);
    if (id) {
        setId_Widget(as_Widget(label), id);
    }
    return label;
}

iLabelWidget *addDialogTitle_Widget(iWidget *dlg, const char *text, const char *idOrNull) {
    return addDialogTitle_(dlg, text, idOrNull);
}

static void acceptValueInput_(iWidget *dlg) {
    iInputWidget *input = findChild_Widget(dlg, "input");
    if (!isEmpty_String(id_Widget(dlg))) {
        const iString *val = text_InputWidget(input);
        postCommandf_App("%s arg:%d value:%s",
                         cstr_String(id_Widget(dlg)),
                         toInt_String(val),
                         cstr_String(val));
        setBackupFileName_InputWidget(input, NULL);
    }
}

iLocalDef int metricFromIndex_(int index) {
    const int sizes[3] = { 100, 115, 130 };
    return sizes[iClamp(index, 0, iElemCount(sizes) - 1)];
}

static void updateValueInputSizing_(iWidget *dlg) {
    const iRect safeRoot = safeRect_Root(dlg->root);
    const iInt2 rootSize = safeRoot.size;
    iWidget *   title    = findChild_Widget(dlg, "valueinput.title");
    iWidget *   prompt   = findChild_Widget(dlg, "valueinput.prompt");
    if (deviceType_App() == phone_AppDeviceType) {
        dlg->rect.size.x = rootSize.x;
    }
    else if (deviceType_App() == tablet_AppDeviceType) {
        dlg->rect.size.x = iMin(rootSize.x, rootSize.y);
    }
    else {
        dlg->rect.size.x = iMin(rootSize.x,
                                metricFromIndex_(prefs_App()->inputZoomLevel) * gap_UI);
                                            /*title ? title->rect.size.x : 0*//*,
                                      prompt->rect.size.x);*/
    }
    if (deviceType_App() != desktop_AppDeviceType) {
        dlg->minSize.y = get_MainWindow()->keyboardHeight == 0 ? 60 * gap_UI : 0;
    }
    /* Adjust the maximum number of visible lines. */
    int footer = 6 * gap_UI;
    iWidget *buttons = findChild_Widget(dlg, "dialogbuttons");
    if (buttons && deviceType_App() == desktop_AppDeviceType) {
        footer += height_Widget(buttons);
    }
    iInputWidget *input = findChild_Widget(dlg, "input");
    setLineLimits_InputWidget(input,
                              1,
                              (height_Rect(visibleRect_Root(dlg->root)) - footer -
                               height_Widget(buttons) - height_Widget(prompt)) /
                                  lineHeight_Text(font_InputWidget(input)));
}

static void animateToRootVisibleBottom_(iWidget *widget, uint32_t span) {
    /* Move bottom to visible area's bottom. */
    int curY = bounds_Widget(widget).pos.y;
    int dstY = bottom_Rect(visibleRect_Root(widget->root)) - height_Widget(widget);
    widget->rect.pos.y = windowToLocal_Widget(widget, init_I2(0, dstY)).y;
    setVisualOffset_Widget(widget, curY - dstY, 0, 0);
    setVisualOffset_Widget(widget, 0, span, easeOut_AnimFlag | softer_AnimFlag);
}

void animateToRootVisibleTop_Widget(iWidget *widget, uint32_t span) {
    int curY = bounds_Widget(widget).pos.y;
    int dstY = top_Rect(visibleRect_Root(widget->root));
    widget->rect.pos.y = windowToLocal_Widget(widget, init_I2(0, dstY)).y;
    setVisualOffset_Widget(widget, curY - dstY, 0, 0);
    setVisualOffset_Widget(widget, 0, span, easeOut_AnimFlag | softer_AnimFlag);
}

int dialogTransitionDir_Widget(const iWidget *dlg) {
    if (deviceType_App() == desktop_AppDeviceType) {
        return top_TransitionDir;
    }
    return isFullSizePanel_Mobile(dlg) ? right_TransitionDir : bottom_TransitionDir;
}

iBool valueInputHandler_(iWidget *dlg, const char *cmd) {
    iWidget *ptr = as_Widget(pointer_Command(cmd));
    if (equal_Command(cmd, "window.resized") || equal_Command(cmd, "keyboard.changed")) {
        if (isVisible_Widget(dlg)) {
            updateValueInputSizing_(dlg);
            arrange_Widget(dlg);
            if (deviceType_App() != desktop_AppDeviceType) {
                animateToRootVisibleBottom_(dlg, keyboardShowSpan_Mobile);
            }
        }
        return iFalse;
    }
    if (equal_Command(cmd, "input.resized")) {
        arrange_Widget(dlg);
        arrange_Widget(dlg);
        refresh_Widget(dlg);
        if (deviceType_App() != desktop_AppDeviceType) {
            animateToRootVisibleBottom_(dlg, 100);
        }
        return iTrue;
    }
    if (equal_Command(cmd, "input.ended")) {
        if (argLabel_Command(cmd, "enter") && hasParent_Widget(ptr, dlg)) {
            if (arg_Command(cmd)) {
                acceptValueInput_(dlg);
            }
            else {
                postCommandf_App("valueinput.cancelled id:%s", cstr_String(id_Widget(dlg)));
                setId_Widget(dlg, ""); /* no further commands to emit */
            }
            setupSheetTransition_Mobile(dlg, dialogTransitionDir_Widget(dlg));
            destroy_Widget(dlg);
            return iTrue;
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "valueinput.set")) {
        iInputWidget *input = findChild_Widget(dlg, "input");
        setTextUndoableCStr_InputWidget(input, suffixPtr_Command(cmd, "text"), iTrue);
        deselect_InputWidget(input);
        validate_InputWidget(input);
        updateValueInputSizing_(dlg);
        arrange_Widget(dlg);
        if (deviceType_App() != desktop_AppDeviceType) {
            animateToRootVisibleBottom_(dlg, 100);
        }
        if (argLabel_Command(cmd, "select")) {
            selectAll_InputWidget(input);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "valueinput.upload")) {
        setFocus_Widget(NULL);
        iInputWidget *input = findChild_Widget(dlg, "input");
        /* Contents of the editor are transferred via the backup file. */
        postCommand_Widget(input, "input.backup");
        processEvents_App(postedEventsOnly_AppEventMode); /* unfocus, save backup */
        const iString *url = collect_String(suffix_Command(cmd, "url"));
        iAssert(equalCase_Rangecc(urlScheme_String(url), "spartan"));
        iUploadWidget *upload = new_UploadWidget(spartan_UploadProtocol);
        setUrl_UploadWidget(upload, url);
        setResponseViewer_UploadWidget(upload, document_Command(cmd));
        addChild_Widget(get_Root()->widget, iClob(upload));
        setupSheetTransition_Mobile(dlg, dialogTransitionDir_Widget(dlg));
        destroy_Widget(dlg);
        return iTrue;
    }
    else if (equal_Command(cmd, "valueinput.cancel")) {
        postCommandf_App("valueinput.cancelled id:%s", cstr_String(id_Widget(dlg)));
        setId_Widget(dlg, ""); /* no further commands to emit */
        setupSheetTransition_Mobile(dlg, dialogTransitionDir_Widget(dlg));
        destroy_Widget(dlg);
        return iTrue;
    }
    else if (equal_Command(cmd, "valueinput.accept")) {
        acceptValueInput_(dlg);
        setupSheetTransition_Mobile(dlg, dialogTransitionDir_Widget(dlg));
        destroy_Widget(dlg);
        return iTrue;
    }
    else if (isDesktop_Platform() &&
             (equal_Command(cmd, "zoom.set") || equal_Command(cmd, "zoom.delta"))) {
        /* DocumentWidget sets an ID that gets used as the posted command when the
           dialog is accepted. An actual configurable flag might be useful here,
           if resizing in needed in other contexts. */
        if (startsWith_String(id_Widget(dlg), "!document.input.submit")) {
            iInputWidget *input = findChild_Widget(dlg, "input");
            int sizeIndex = prefs_App()->inputZoomLevel;
            if (equal_Command(cmd, "zoom.set")) {
                sizeIndex = 0;
            }
            else {
                sizeIndex += iSign(arg_Command(cmd));
                sizeIndex = iClamp(sizeIndex, 0, 2);
            }
            ((iPrefs *) prefs_App())->inputZoomLevel = sizeIndex; /* const cast... */
            setFont_InputWidget(input,
                                FONT_ID(default_FontId,
                                        regular_FontStyle,
                                        uiMedium_FontSize + sizeIndex));
            updateValueInputSizing_(dlg);
            arrange_Widget(dlg);
            arrange_Widget(dlg);
            refresh_Widget(dlg);
        }
        return iTrue;
    }
    return iFalse;
}

iWidget *makeDialogButtons_Widget(const iMenuItem *actions, size_t numActions) {
    iWidget *div = new_Widget();
    setId_Widget(div, "dialogbuttons");
    setFlags_Widget(div,
                    arrangeHorizontal_WidgetFlag | arrangeHeight_WidgetFlag |
                        resizeToParentWidth_WidgetFlag |
                        resizeWidthOfChildren_WidgetFlag,
                    iTrue);
    /* If there is no separator, align everything to the right. */
    iBool haveSep = iFalse;
    for (size_t i = 0; i < numActions; i++) {
        if (!iCmpStr(actions[i].label, "---")) {
            haveSep = iTrue;
            break;
        }
    }
    if (!haveSep) {
        addChildFlags_Widget(div, iClob(new_Widget()), expand_WidgetFlag);
    }
    int fonts[2] = { uiLabel_FontId, uiLabelBold_FontId };
    if (deviceType_App() != desktop_AppDeviceType) {
        fonts[0] = uiLabelBig_FontId;
        fonts[1] = uiLabelBigBold_FontId;
    }
    for (size_t i = 0; i < numActions; i++) {
        const char *label     = actions[i].label;
        const char *cmd       = actions[i].command;
        int         key       = actions[i].key;
        int         kmods     = actions[i].kmods;
        const iBool isDefault = (i == numActions - 1);
        if (*label == '*' || *label == '&') {
            continue; /* Special value selection items for a Question dialog. */
        }
        if (startsWith_CStr(label, "```")) {
            /* Annotation. */
            iLabelWidget *annotation = addChild_Widget(div, iClob(new_LabelWidget(label + 3, NULL)));
            setTextColor_LabelWidget(annotation, uiTextAction_ColorId);
            continue;
        }
        if (!iCmpStr(label, "---")) {
            /* Separator.*/
            addChildFlags_Widget(div, iClob(new_Widget()), expand_WidgetFlag);
            continue;
        }
        if (!iCmpStr(label, "${cancel}") && !cmd) {
            cmd = "cancel";
            key = SDLK_ESCAPE;
            kmods = 0;
        }
        if (isDefault) {
            if (!key) {
                key = SDLK_RETURN;
                kmods = 0;
            }
            if (label == NULL) {
                label = format_CStr(uiTextAction_ColorEscape "%s", cstr_Lang("dlg.default"));
            }
        }
        iLabelWidget *button =
            addChild_Widget(div, iClob(newKeyMods_LabelWidget(label, key, kmods, cmd)));
        if (isDefault) {
            setId_Widget(as_Widget(button), "default");
        }
        setFlags_Widget(as_Widget(button), alignLeft_WidgetFlag | drawKey_WidgetFlag, isDefault);
        if (key && key != SDLK_ESCAPE && deviceType_App() == desktop_AppDeviceType) {
            setFlags_Widget(as_Widget(button), alignLeft_WidgetFlag | drawKey_WidgetFlag, iTrue);
        }
        if (deviceType_App() != desktop_AppDeviceType) {
            setFlags_Widget(as_Widget(button), frameless_WidgetFlag | noBackground_WidgetFlag, iTrue);
            setTextColor_LabelWidget(button, uiTextAction_ColorId);
        }
        setFont_LabelWidget(button, isDefault ? fonts[1] : fonts[0]);
    }
    return div;
}

iWidget *makeValueInput_Widget(iWidget *parent, const iString *initialValue, const char *title,
                               const char *prompt, const char *acceptLabel,
                               const char *command){
    return makeValueInputWithAdditionalActions_Widget(
        parent, initialValue, title, prompt, acceptLabel, command, NULL, 0);
}

iWidget *makeValueInputWithAdditionalActions_Widget(iWidget *parent, const iString *initialValue,
                                                    const char *title, const char *prompt,
                                                    const char *acceptLabel, const char *command,
                                                    const iMenuItem *additionalActions,
                                                    size_t           numAdditionalActions) {
    if (parent) {
        setFocus_Widget(NULL);
    }
    iWidget *dlg = makeSheet_Widget(command);
    setCommandHandler_Widget(dlg, valueInputHandler_);
    if (parent) {
        addChild_Widget(parent, iClob(dlg));
    }
    if (deviceType_App() == desktop_AppDeviceType) { /* conserve space on mobile */
        addDialogTitle_(dlg, title, "valueinput.title");
    }
    iLabelWidget *promptLabel;
    setId_Widget(addChildFlags_Widget(
                     dlg, iClob(promptLabel = new_LabelWidget(prompt, NULL)), frameless_WidgetFlag
                     | resizeToParentWidth_WidgetFlag | fixedHeight_WidgetFlag),
                 "valueinput.prompt");
    setWrap_LabelWidget(promptLabel, iTrue);
    iInputWidget *input = addChildFlags_Widget(dlg, iClob(new_InputWidget(0)),
                                               resizeToParentWidth_WidgetFlag);
    setContentPadding_InputWidget(input, 0.5f * gap_UI, 0.5f * gap_UI);
    if (deviceType_App() == phone_AppDeviceType) {
        setFont_InputWidget(input, uiLabelBig_FontId);
        setBackgroundColor_Widget(dlg, uiBackgroundSidebar_ColorId);
        setContentPadding_InputWidget(input, gap_UI, gap_UI);
    }
    else if (isDesktop_Platform()) {
        /* The input prompt font is resizable. */
        setFont_InputWidget(input,
                            FONT_ID(default_FontId,
                                    regular_FontStyle,
                                    uiMedium_FontSize + prefs_App()->inputZoomLevel));
    }
    if (initialValue) {
        setText_InputWidget(input, initialValue);
    }
    setId_Widget(as_Widget(input), "input");
    addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
    /* On mobile, the actions are laid out a bit differently: buttons on top, on opposite edges. */
    iArray actions;
    init_Array(&actions, sizeof(iMenuItem));
    for (size_t i = 0; i < numAdditionalActions; i++) {
        pushBack_Array(&actions, &additionalActions[i]);
    }
    if (numAdditionalActions) {
        pushBack_Array(&actions, &(iMenuItem){ "---" });
    }
    pushBack_Array(&actions, &(iMenuItem){ "${cancel}", SDLK_ESCAPE, 0, "valueinput.cancel" });
    if (deviceType_App() != desktop_AppDeviceType) {
        pushBack_Array(&actions, &(iMenuItem){ "---" });
    }
    pushBack_Array(&actions,
                   &(iMenuItem){ acceptLabel,
                                 SDLK_RETURN,
                                 acceptKeyMod_ReturnKeyBehavior(prefs_App()->returnKey),
                                 "valueinput.accept" });
    addChildPos_Widget(dlg,
                       iClob(makeDialogButtons_Widget(constData_Array(&actions),
                                                      size_Array(&actions))),
                       deviceType_App() != desktop_AppDeviceType ?
                        front_WidgetAddPos : back_WidgetAddPos);
    deinit_Array(&actions);
    arrange_Widget(dlg);
    if (parent) {
        setFocus_Widget(as_Widget(input));
    }
    /* Check that the top is in the safe area. */
    if (deviceType_App() != desktop_AppDeviceType) {
        dlg->rect.pos.y = windowToLocal_Widget(dlg, init_I2(0, bottom_Rect(visibleRect_Root(dlg->root)) -
            dlg->rect.size.y)).y;
        setFlags_Widget(dlg, drawBackgroundToBottom_WidgetFlag, iTrue);
    }
    updateValueInputSizing_(dlg);
    setupSheetTransition_Mobile(dlg, incoming_TransitionFlag | dialogTransitionDir_Widget(dlg));
    return dlg;
}

void updateValueInput_Widget(iWidget *d, const char *title, const char *prompt) {
    setTextCStr_LabelWidget(findChild_Widget(d, "valueinput.title"), title);
    setTextCStr_LabelWidget(findChild_Widget(d, "valueinput.prompt"), prompt);
    updateValueInputSizing_(d);
}

static void updateQuestionWidth_(iWidget *dlg) {
    iWidget *title = findChild_Widget(dlg, "question.title");
    iWidget *msg   = findChild_Widget(dlg, "question.msg");
    if (title && msg) {
        const iRect safeRoot = safeRect_Root(dlg->root);
        const iInt2 rootSize = safeRoot.size;
        const int padding = 6 * gap_UI;
        dlg->rect.size.x =
            iMin(iMin(150 * gap_UI, rootSize.x),
                 iMaxi(iMaxi(100 * gap_UI, padding + title->rect.size.x),
                       padding + msg->rect.size.x));
    }
}

static iBool messageHandler_(iWidget *msg, const char *cmd) {
    /* Almost any command dismisses the sheet. */
    /* TODO: Add a "notification" type of user events to separate them from user actions. */
    if (!(equal_Command(cmd, "media.updated") ||
          equal_Command(cmd, "media.player.update") ||
          equal_Command(cmd, "bookmarks.request.finished") ||
          equal_Command(cmd, "bookmarks.changed") ||
          equal_Command(cmd, "document.autoreload") ||
          equal_Command(cmd, "document.reload") ||
          equal_Command(cmd, "document.request.updated") ||
          equal_Command(cmd, "document.linkkeys") ||
          equal_Command(cmd, "scrollbar.fade") ||
          equal_Command(cmd, "widget.overflow") ||
          equal_Command(cmd, "edgeswipe.ended") ||
          equal_Command(cmd, "layout.changed") ||
          equal_Command(cmd, "theme.changed") ||
          equal_Command(cmd, "focus.lost") ||
          equal_Command(cmd, "focus.gained") ||
          startsWith_CStr(cmd, "feeds.update.") ||
          startsWith_CStr(cmd, "window."))) {
        setupSheetTransition_Mobile(msg, dialogTransitionDir_Widget(msg));
        destroy_Widget(msg);
    }
    else if (equal_Command(cmd, "window.resized")) {
        updateQuestionWidth_(msg);
    }
    return iFalse;
}

iWidget *makeSimpleMessage_Widget(const char *title, const char *msg) {
    return makeMessage_Widget(title,
                              msg,
                              (iMenuItem[]){ { "${dlg.message.ok}", 0, 0, "message.ok" } },
                              1);
}

iWidget *makeMessage_Widget(const char *title, const char *msg, const iMenuItem *items,
                            size_t numItems) {
    iWidget *dlg = makeQuestion_Widget(title, msg, items, numItems);
    addAction_Widget(dlg, SDLK_ESCAPE, 0, "message.ok");
    addAction_Widget(dlg, SDLK_SPACE, 0, "message.ok");
    return dlg;
}

iWidget *makeQuestion_Widget(const char *title, const char *msg,
                             const iMenuItem *items, size_t numItems) {
    processEvents_App(postedEventsOnly_AppEventMode);
    if (isUsingPanelLayout_Mobile()) {
        iArray *panelItems = collectNew_Array(sizeof(iMenuItem));
        pushBackN_Array(panelItems, (iMenuItem[]){
            { format_CStr("title text:%s", title) },
            { format_CStr("label text:%s", msg) },
            { NULL }
        }, 3);
        for (size_t i = 0; i < numItems; i++) {
            const iMenuItem *item = &items[i];
            const char first = item->label[0];
            if (first == '*' || first == '&') {
                insert_Array(panelItems, size_Array(panelItems) - 1,
                             &(iMenuItem){ format_CStr("button selected:%d text:%s",
                                                       first == '&' ? 1 : 0, item->label + 1),
                                           0, 0, item->command });
            }
        }
        iWidget *dlg = makePanels_Mobile("", data_Array(panelItems), items, numItems);
        setCommandHandler_Widget(dlg, messageHandler_);
        setupSheetTransition_Mobile(dlg, incoming_TransitionFlag | dialogTransitionDir_Widget(dlg));
        return dlg;
    }
    iWidget *dlg = makeSheet_Widget("");
    setCommandHandler_Widget(dlg, messageHandler_);
    addDialogTitle_(dlg, title, "question.title");
    iLabelWidget *msgLabel;
    setId_Widget(addChildFlags_Widget(dlg,
                                      iClob(msgLabel = new_LabelWidget(msg, NULL)),
                                      frameless_WidgetFlag | fixedHeight_WidgetFlag |
                                          resizeToParentWidth_WidgetFlag),
                 "question.msg");
    setWrap_LabelWidget(msgLabel, iTrue);
    /* Check for value selections. */
    for (size_t i = 0; i < numItems; i++) {
        const iMenuItem *item = &items[i];
        const char first = item->label[0];
        if (first == '*' || first == '&') {
            iLabelWidget *option =
                addChildFlags_Widget(dlg,
                                 iClob(newKeyMods_LabelWidget(item->label + 1,
                                                              item->key,
                                                              item->kmods,
                                                              item->command)),
                                 resizeToParentWidth_WidgetFlag |
                                 (first == '&' ? selected_WidgetFlag : 0));
            if (deviceType_App() != desktop_AppDeviceType) {
                setFont_LabelWidget(option, uiLabelBig_FontId);
            }
        }
    }
    addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
    addChild_Widget(dlg, iClob(makeDialogButtons_Widget(items, numItems)));
    addChild_Widget(dlg->root->widget, iClob(dlg));
    updateQuestionWidth_(dlg);
    class_Widget(as_Widget(msgLabel))->sizeChanged(as_Widget(msgLabel));
    arrange_Widget(dlg); /* BUG: This extra arrange shouldn't be needed but the dialog won't
                            be arranged correctly unless it's here. */
    setupSheetTransition_Mobile(dlg, iTrue);
    return dlg;
}

void setToggle_Widget(iWidget *d, iBool active) {
    if (d) {
        setFlags_Widget(d, selected_WidgetFlag, active);
        iLabelWidget *label = (iLabelWidget *) d;
        if (!cmp_String(text_LabelWidget(label), toggleYes_Icon) ||
            !cmp_String(text_LabelWidget(label), toggleNo_Icon)) {
            updateText_LabelWidget(
                (iLabelWidget *) d,
                collectNewCStr_String(isSelected_Widget(d) ? toggleYes_Icon : toggleNo_Icon));
        }
        else {
            refresh_Widget(d);
        }
    }
}

static iBool toggleHandler_(iWidget *d, const char *cmd) {
    if (equal_Command(cmd, "toggle") && pointer_Command(cmd) == d) {
        setToggle_Widget(d, (flags_Widget(d) & selected_WidgetFlag) == 0);
        postCommand_Widget(d,
                           format_CStr("%s.changed arg:%d",
                                       cstr_String(id_Widget(d)),
                                       isSelected_Widget(d) ? 1 : 0));
        return iTrue;
    }
    else if (equal_Command(cmd, "lang.changed")) {
        /* TODO: Measure labels again. */
    }
    return iFalse;
}

iWidget *makeToggle_Widget(const char *id) {
    iWidget *toggle = as_Widget(new_LabelWidget(toggleYes_Icon, "toggle")); /* "YES" for sizing */
    setId_Widget(toggle, id);
    /* TODO: Measure both labels and use the larger of the two. */
    updateTextCStr_LabelWidget((iLabelWidget *) toggle, toggleNo_Icon); /* actual initial value */
    setFlags_Widget(toggle, fixedWidth_WidgetFlag, iTrue);
    setCommandHandler_Widget(toggle, toggleHandler_);
    return toggle;
}

void appendFramelessTabPage_Widget(iWidget *tabs, iWidget *page, const char *title, int iconColor,
                                   int shortcut, int kmods) {
    appendTabPage_Widget(tabs, page, title, shortcut, kmods);
    setFlags_Widget(
        (iWidget *) back_ObjectList(children_Widget(findChild_Widget(tabs, "tabs.buttons"))),
        frameless_WidgetFlag | noBackground_WidgetFlag,
        iTrue);
    if (iconColor != none_ColorId) {
        setIconColor_LabelWidget(tabPageButton_Widget(tabs, page), iconColor);
    }
}

iWidget *makeTwoColumns_Widget(iWidget **headings, iWidget **values) {
    iWidget *page = new_Widget();
    setFlags_Widget(page, arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
    *headings = addChildFlags_Widget(
        page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    *values = addChildFlags_Widget(
        page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    return page;
}

iLabelWidget *dialogAcceptButton_Widget(const iWidget *d) {
    iWidget *buttonParent = findChild_Widget(d, "dialogbuttons");
    if (!buttonParent) {
        iAssert(isUsingPanelLayout_Mobile());
        buttonParent = findChild_Widget(d, "navi.actions");
    }
    return (iLabelWidget *) lastChild_Widget(buttonParent);
}

iWidget *appendTwoColumnTabPage_Widget(iWidget *tabs, const char *title, int iconColor,
                                       int shortcut, iWidget **headings, iWidget **values) {
    /* TODO: Use `makeTwoColumnWidget_()`, see above. */
    iWidget *page = new_Widget();
    setFlags_Widget(page, arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
    addChildFlags_Widget(page, iClob(new_Widget()), expand_WidgetFlag);
    setPadding_Widget(page, 0, gap_UI, 0, gap_UI);
    iWidget *columns = new_Widget();
    addChildFlags_Widget(page, iClob(columns), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
    *headings = addChildFlags_Widget(
        columns, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    *values = addChildFlags_Widget(
        columns, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    addChildFlags_Widget(page, iClob(new_Widget()), expand_WidgetFlag);
    appendFramelessTabPage_Widget(tabs, iClob(page), title, iconColor, shortcut, shortcut ? KMOD_PRIMARY : 0);
    return page;
}

static void addDialogPaddingSize_(iWidget *headings, iWidget *values, int size) {
    addChild_Widget(headings, iClob(makePadding_Widget(size)));
    addChild_Widget(values,   iClob(makePadding_Widget(size)));
}

static void addDialogPadding_(iWidget *headings, iWidget *values) {
    addDialogPaddingSize_(headings, values, iMaxi(1, lineHeight_Text(uiLabel_FontId) * 3 / 4));
}

static void makeTwoColumnHeading_(const char *title, iWidget *headings, iWidget *values) {
    if (isTerminal_Platform()) {
        addDialogPadding_(headings, values);
    }
    else {
        addDialogPaddingSize_(headings, values, 2 * gap_UI);
    }
    setFont_LabelWidget(addChildFlags_Widget(headings,
                                             iClob(makeHeading_Widget(
                                                 format_CStr(uiHeading_ColorEscape "%s", title))),
                                             ignoreForParentWidth_WidgetFlag),
                        uiLabelBold_FontId);
    addChild_Widget(values, iClob(makeHeading_Widget("")));
}

static void expandInputFieldWidth_(iInputWidget *input) {
    if (!input) return;
    iWidget *page = as_Widget(input)->parent->parent->parent->parent; /* tabs > page > values > input */
    as_Widget(input)->rect.size.x =
        right_Rect(bounds_Widget(page)) - left_Rect(bounds_Widget(constAs_Widget(input)));
}

static void addRadioButton_(iWidget *parent, const char *id, const char *label, const char *cmd) {
    setId_Widget(
        addChildFlags_Widget(parent, iClob(new_LabelWidget(label, cmd)), radio_WidgetFlag),
        id);
}

static iBool proportionalFonts_(const iFontSpec *spec) {
    return (spec->flags & monospace_FontSpecFlag) == 0 && ~spec->flags & auxiliary_FontSpecFlag;
}

static iBool monospaceFonts_(const iFontSpec *spec) {
    return (spec->flags & monospace_FontSpecFlag) != 0 && ~spec->flags & auxiliary_FontSpecFlag;
}

static const iArray *makeFontItems_(const char *id) {
    iArray *items = collectNew_Array(sizeof(iMenuItem));
    if (!startsWith_CStr(id, "mono")) {
        iConstForEach(PtrArray, i, listSpecs_Fonts(proportionalFonts_)) {
            const iFontSpec *spec = i.ptr;
            pushBack_Array(
                items,
                &(iMenuItem){ cstr_String(&spec->name),
                              0,
                              0,
                              format_CStr("!font.set %s:%s", id, cstr_String(&spec->id)) });
        }
        pushBack_Array(items, &(iMenuItem){ "---" });
    }
    iConstForEach(PtrArray, j, listSpecs_Fonts(monospaceFonts_)) {
        const iFontSpec *spec = j.ptr;
        pushBack_Array(
            items,
            &(iMenuItem){ cstr_String(&spec->name),
                          0,
                          0,
                          format_CStr("!font.set %s:%s", id, cstr_String(&spec->id)) });
    }
    pushBack_Array(items, &(iMenuItem){ NULL }); /* terminator */
    return items;
}

static void addFontButtons_(iWidget *parent, const char *id) {
    const iArray *items = makeFontItems_(id);
    size_t widestIndex = findWidestLabel_MenuItem(constData_Array(items), size_Array(items));
    iLabelWidget *button = makeMenuButton_LabelWidget(constValue_Array(items, widestIndex, iMenuItem).label,
                                                      constData_Array(items), size_Array(items));
    setBackgroundColor_Widget(findChild_Widget(as_Widget(button), "menu"),
                              uiBackgroundMenu_ColorId);
    setId_Widget(as_Widget(button), format_CStr("prefs.font.%s", id));
    addChildFlags_Widget(parent, iClob(button), alignLeft_WidgetFlag);
}

void updatePreferencesLayout_Widget(iWidget *prefs) {
    if (!prefs || deviceType_App() != desktop_AppDeviceType) {
        return;
    }
    /* Doing manual layout here because the widget arranging logic isn't sophisticated enough. */
    /* TODO: Make the arranging more sophisticated to automate this. */
    static const char *inputIds[] = {
        "prefs.searchurl",
        "prefs.downloads",
        "prefs.userfont",
        "prefs.ca.file",
        "prefs.ca.path",
        "prefs.proxy.gemini",
        "prefs.proxy.gopher",
        "prefs.proxy.http"
    };
    iWidget *tabs = findChild_Widget(prefs, "prefs.tabs");
    tabs->rect.size = zero_I2();
    /* Input fields expand to the right edge. */
    /* TODO: Add an arrangement flag for this. */
    iForIndices(i, inputIds) {
        iInputWidget *input = findChild_Widget(tabs, inputIds[i]);
        if (input) {
            as_Widget(input)->rect.size.x = 0;
        }
    }
    iWidget *bindings = findChild_Widget(prefs, "bindings");
    if (bindings) {
        bindings->rect.size.x = 0;
    }
    resizeToLargestPage_Widget(tabs);
    arrange_Widget(prefs);
    iForIndices(i, inputIds) {
        expandInputFieldWidth_(findChild_Widget(tabs, inputIds[i]));
    }
}

static void addDialogInputWithHeadingAndFlags_(iWidget *headings, iWidget *values, const char *labelText,
                                               const char *inputId, iInputWidget *input, int64_t flags) {
    iLabelWidget *head = addChild_Widget(headings, iClob(makeHeading_Widget(labelText)));
    if (isMobile_Platform()) {
        /* On mobile, inputs have 2 gaps of extra padding. */
        setFixedSize_Widget(as_Widget(head), init_I2(-1, height_Widget(input)));
        setPadding_Widget(as_Widget(head), 0, gap_UI, 0, 0);
    }
    setId_Widget(addChild_Widget(values, input), inputId);
    if (deviceType_App() != phone_AppDeviceType) {
        /* Ensure that the label has the same height as the input widget. */
        as_Widget(head)->sizeRef = as_Widget(input);
    }
    setFlags_Widget(as_Widget(head), flags, iTrue);
    setFlags_Widget(as_Widget(input), flags, iTrue);
}

static void addDialogInputWithHeading_(iWidget *headings, iWidget *values, const char *labelText,
                                       const char *inputId, iInputWidget *input) {
    addDialogInputWithHeadingAndFlags_(headings, values, labelText, inputId, input, 0);
}

iInputWidget *addTwoColumnDialogInputField_Widget(iWidget *headings, iWidget *values,
                                                  const char *labelText, const char *inputId,
                                                  iInputWidget *input) {
    addDialogInputWithHeading_(headings, values, labelText, inputId, input);
    return input;
}

static void addPrefsInputWithHeading_(iWidget *headings, iWidget *values,
                                      const char *id, iInputWidget *input) {
    addDialogInputWithHeading_(headings, values, format_CStr("${%s}", id), id, input);
}

static void addDialogToggle_(iWidget *headings, iWidget *values,
                             const char *heading, const char *toggleId) {
    addChild_Widget(headings, iClob(makeHeading_Widget(heading)));
    addChild_Widget(values, iClob(makeToggle_Widget(toggleId)));
}

static void addDialogToggleGroup_(iWidget *headings, iWidget *values, const char *title,
                                  const char *toggleIds[], size_t n) {
    addChild_Widget(headings, iClob(makeHeading_Widget(title)));
    iWidget *group = new_Widget();
    for (size_t i = 0; i < n && toggleIds[i]; i++) {
        iWidget *tog;
        setTextCStr_LabelWidget(
            addChild_Widget(group, tog = iClob(makeToggle_Widget(toggleIds[i]))),
            format_CStr("${%s}", toggleIds[i]));
        setFlags_Widget(tog, fixedWidth_WidgetFlag, iFalse);
        updateSize_LabelWidget((iLabelWidget *) tog);
    }
    addChildFlags_Widget(
        values, iClob(group), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
}

static iLabelWidget *addDialogDropMenu_(iWidget *headings, iWidget *values, const char *title,
                                        const iMenuItem *items, size_t numItems, const char *id) {
    addChild_Widget(headings, iClob(makeHeading_Widget(title)));
    iLabelWidget *button = makeMenuButton_LabelWidget(
        items[findWidestLabel_MenuItem(items, numItems)].label, items, numItems);
    setBackgroundColor_Widget(findChild_Widget(as_Widget(button), "menu"),
                              uiBackgroundMenu_ColorId);
    setId_Widget(addChildFlags_Widget(values, iClob(button), alignLeft_WidgetFlag), id);
    return button;
}

size_t findWidestLabel_MenuItem(const iMenuItem *items, size_t num) {
    int widest = 0;
    size_t widestPos = iInvalidPos;
    for (size_t i = 0; i < num && items[i].label; i++) {
        const int width =
            measure_Text(uiLabel_FontId,
                         translateCStr_Lang(items[i].label))
                .advance.x;
        if (widestPos == iInvalidPos || width > widest) {
            widest = width;
            widestPos = i;
        }
    }
    return widestPos;
}

size_t findCommand_MenuItem(const iMenuItem *items, size_t num, const char *command) {
    for (size_t i = 0; i < num && items[i].label; i++) {
        if (!iCmpStr(items[i].command, command)) {
            return i;
        }
    }
    return iInvalidPos;
}

const char *widestLabel_MenuItemArray(const iArray *items) {
    size_t index = findWidestLabel_MenuItem(constData_Array(items), size_Array(items));
    if (index == iInvalidPos) {
        return "";
    }
    return constValue_Array(items, index, iMenuItem).label;
}

iChar removeIconPrefix_String(iString *d) {
    if (isEmpty_String(d)) {
        return 0;
    }
    iStringConstIterator iter;
    init_StringConstIterator(&iter, d);
    iChar icon = iter.value;
    next_StringConstIterator(&iter);
    if (iter.value == ' ' && icon >= 0x100) {
        remove_Block(&d->chars, 0, iter.next - constBegin_String(d));
        return icon;
    }
    return 0;
}

enum iColorId removeColorEscapes_String(iString *d) {
    enum iColorId color = none_ColorId;
    for (;;) {
        const char *esc = strchr(cstr_String(d), '\v');
        if (esc) {
            const char *endp;
            color = parseEscape_Color(esc, &endp);
            remove_Block(&d->chars, esc - cstr_String(d), endp - esc);
        }
        else break;
    }
    return color;
}

iWidget *makeDialog_Widget(const char *id,
                           const iMenuItem *itemsNullTerminated,
                           const iMenuItem *actions, size_t numActions) {
    iWidget *dlg = makeSheet_Widget(id);
    /* TODO: Construct desktop dialogs using NULL-terminated item arrays, like mobile panels. */
    addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
    addChild_Widget(dlg, iClob(makeDialogButtons_Widget(actions, numActions)));
    addChild_Widget(dlg->root->widget, iClob(dlg));
    arrange_Widget(dlg);
    setupSheetTransition_Mobile(dlg, iTrue);
    return dlg;
}

static const char *returnKeyBehaviorStr_(int behavior) {
    iString *nl = collectNew_String();
    iString *ac = collectNew_String();
    toString_Sym(SDLK_RETURN, lineBreakKeyMod_ReturnKeyBehavior(behavior), nl);
    toString_Sym(SDLK_RETURN, acceptKeyMod_ReturnKeyBehavior(behavior), ac);
    return format_CStr("${prefs.returnkey.linebreak} " uiTextAction_ColorEscape
                       "%s" restore_ColorEscape
                       "    ${prefs.returnkey.accept} " uiTextAction_ColorEscape "%s",
                       cstr_String(nl),
                       cstr_String(ac));
}

iWidget *makePreferences_Widget(void) {
    /* Common items. */
    const iMenuItem langItems[] = { { u8"Čeština - cs", 0, 0, "uilang id:cs" },
                                    { u8"Deutsch - de", 0, 0, "uilang id:de" },
                                    { u8"English - en", 0, 0, "uilang id:en" },
                                    { u8"Español - es", 0, 0, "uilang id:es" },
                                    { u8"Español (México) - es", 0, 0, "uilang id:es_MX" },
                                    { u8"Euskara - eu", 0, 0, "uilang id:eu" },
                                    { u8"Esperanto - eo", 0, 0, "uilang id:eo" },
                                    { u8"Suomi - fi", 0, 0, "uilang id:fi" },
                                    { u8"Français - fr", 0, 0, "uilang id:fr" },
                                    { u8"Galego - gl", 0, 0, "uilang id:gl" },
                                    { u8"Magyar - hu", 0, 0, "uilang id:hu" },
                                    { u8"Interlingua - ia", 0, 0, "uilang id:ia" },
                                    { u8"Interlingue - ie", 0, 0, "uilang id:ie" },
                                    { u8"Interslavic - isv", 0, 0, "uilang id:isv" },
                                    { u8"Italiano - it", 0, 0, "uilang id:it" },
                                    { u8"日本語 - ja", 0, 0, "uilang id:ja" },
                                    { u8"Nederlands - nl", 0, 0, "uilang id:nl" },
                                    { u8"Polski - pl", 0, 0, "uilang id:pl" },
                                    { u8"Русский - ru", 0, 0, "uilang id:ru" },
                                    { u8"Slovak - sk", 0, 0, "uilang id:sk" },
                                    { u8"Српски - sr", 0, 0, "uilang id:sr" },
                                    { u8"Toki pona - tok", 0, 0, "uilang id:tok" },
                                    { u8"Türkçe - tr", 0, 0, "uilang id:tr" },
                                    { u8"Українська - uk", 0, 0, "uilang id:uk" },
                                    { u8"简体中文 - zh", 0, 0, "uilang id:zh_Hans" },
                                    { u8"繁體/正體中文 - zh", 0, 0, "uilang id:zh_Hant" },
                                    { NULL } };
    const iMenuItem returnKeyBehaviors[] = {
        { returnKeyBehaviorStr_(default_ReturnKeyBehavior),
          0,
          0,
          format_CStr("returnkey.set arg:%d", default_ReturnKeyBehavior) },
#if !defined (iPlatformTerminal)
        { returnKeyBehaviorStr_(RETURN_KEY_BEHAVIOR(0, shift_ReturnKeyFlag)),
          0,
          0,
          format_CStr("returnkey.set arg:%d", RETURN_KEY_BEHAVIOR(0, shift_ReturnKeyFlag)) },
        { returnKeyBehaviorStr_(acceptWithPrimaryMod_ReturnKeyBehavior),
          0,
          0,
          format_CStr("returnkey.set arg:%d", acceptWithPrimaryMod_ReturnKeyBehavior) },
#else
        { returnKeyBehaviorStr_(RETURN_KEY_BEHAVIOR(gui_ReturnKeyFlag, 0)),
          0,
          0,
          format_CStr("returnkey.set arg:%d", RETURN_KEY_BEHAVIOR(gui_ReturnKeyFlag, 0)) },
#endif
        { NULL }
    };
    iMenuItem toolbarActionItems[2][max_ToolbarAction];
    iZap(toolbarActionItems);
    for (int j = 0; j < 2; j++) {
        for (int i = 0; i < sidebar_ToolbarAction; i++) {
            toolbarActionItems[j][i].label = toolbarActions_Mobile[i].label;
            toolbarActionItems[j][i].command =
                format_CStr("toolbar.action.set arg:%d button:%d", i, j);
        }
    }
    iMenuItem docThemes[2][max_GmDocumentTheme + 1];
    for (int i = 0; i < 2; ++i) {
        const iBool isDark = (i == 0);
        const char *mode = isDark ? "dark" : "light";
        const iMenuItem items[max_GmDocumentTheme + 1] = {
            { "${prefs.doctheme.name.colorfuldark}", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, colorfulDark_GmDocumentTheme) },
            { "${prefs.doctheme.name.colorfullight}", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, colorfulLight_GmDocumentTheme) },
            { "${prefs.doctheme.name.black}", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, black_GmDocumentTheme) },
            { "${prefs.doctheme.name.gray}", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, gray_GmDocumentTheme) },
            { "${prefs.doctheme.name.white}", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, white_GmDocumentTheme) },
            { "${prefs.doctheme.name.sepia}", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, sepia_GmDocumentTheme) },
            { "${prefs.doctheme.name.oceanic}", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, oceanic_GmDocumentTheme) },
            { "${prefs.doctheme.name.highcontrast}", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, highContrast_GmDocumentTheme) },
            { NULL }
        };
        memcpy(docThemes[i], items, sizeof(items));
    }
    const iMenuItem accentItems[max_ColorAccent] = {
        { circle_Icon " ${prefs.accent.teal}", 0, 0, "accent.set arg:0" },
        { circle_Icon " ${prefs.accent.orange}", 0, 0, "accent.set arg:1" },
        { circle_Icon " ${prefs.accent.red}", 0, 0, "accent.set arg:2" },
        { circle_Icon " ${prefs.accent.green}", 0, 0, "accent.set arg:3" },
        { circle_Icon " ${prefs.accent.blue}", 0, 0, "accent.set arg:4" },
        { circle_Icon " ${prefs.accent.gray}", 0, 0, "accent.set arg:5" }
    };
    const iMenuItem imgStyles[] = {
        { "${prefs.imagestyle.original}",  0, 0, format_CStr("imagestyle.set arg:%d", original_ImageStyle) },
        { "${prefs.imagestyle.grayscale}", 0, 0, format_CStr("imagestyle.set arg:%d", grayscale_ImageStyle) },
        { "${prefs.imagestyle.bgfg}",      0, 0, format_CStr("imagestyle.set arg:%d", bgFg_ImageStyle) },
        { "${prefs.imagestyle.text}",      0, 0, format_CStr("imagestyle.set arg:%d", textColorized_ImageStyle) },
        { "${prefs.imagestyle.preformat}", 0, 0, format_CStr("imagestyle.set arg:%d", preformatColorized_ImageStyle) },
        { NULL }
    };
    const iMenuItem lineWidthItems[] = {
        { "button id:prefs.linewidth.30 text:\u20132",                 0, 0, "linewidth.set arg:30" },
        { "button id:prefs.linewidth.34 text:\u20131",                 0, 0, "linewidth.set arg:34" },
        { "button id:prefs.linewidth.38 label:prefs.linewidth.normal", 0, 0, "linewidth.set arg:38" },
        { "button id:prefs.linewidth.43 text:+1",                      0, 0, "linewidth.set arg:43" },
        { "button id:prefs.linewidth.48 text:+2",                      0, 0, "linewidth.set arg:48" },
        { "button id:prefs.linewidth.1000 label:prefs.linewidth.fill", 0, 0, "linewidth.set arg:1000" },
        { NULL }
    };
    /* Create the Preferences UI. */
    if (isUsingPanelLayout_Mobile()) {
        const iMenuItem pinSplitItems[] = {
            { "button id:prefs.pinsplit.0 label:prefs.pinsplit.none",  0, 0, "pinsplit.set arg:0" },
            { "button id:prefs.pinsplit.1 label:prefs.pinsplit.left",  0, 0, "pinsplit.set arg:1" },
            { "button id:prefs.pinsplit.2 label:prefs.pinsplit.right", 0, 0, "pinsplit.set arg:2" },
            { NULL }
        };
        const iMenuItem themeItems[] = {
            { "button id:prefs.theme.0 label:prefs.theme.black", 0, 0, "theme.set arg:0" },
            { "button id:prefs.theme.1 label:prefs.theme.dark",  0, 0, "theme.set arg:1" },
            { "button id:prefs.theme.2 label:prefs.theme.light", 0, 0, "theme.set arg:2" },
            { "button id:prefs.theme.3 label:prefs.theme.white", 0, 0, "theme.set arg:3" },
            { NULL }
        };
        const iMenuItem accentItems[] = {
            { "button id:prefs.accent.0 label:prefs.accent.teal", 0, 0, "accent.set arg:0" },
            { "button id:prefs.accent.1 label:prefs.accent.orange", 0, 0, "accent.set arg:1" },
            { "button id:prefs.accent.2 label:prefs.accent.red", 0, 0, "accent.set arg:2" },
            { "button id:prefs.accent.3 label:prefs.accent.green", 0, 0, "accent.set arg:3" },
            { "button id:prefs.accent.4 label:prefs.accent.blue", 0, 0, "accent.set arg:4" },
            { "button id:prefs.accent.5 label:prefs.accent.gray", 0, 0, "accent.set arg:5" },
            { NULL }
        };
        const iMenuItem satItems[] = {
            { "button id:prefs.saturation.3 text:100 %", 0, 0, "saturation.set arg:100" },
            { "button id:prefs.saturation.2 text:66 %", 0, 0, "saturation.set arg:66" },
            { "button id:prefs.saturation.1 text:33 %", 0, 0, "saturation.set arg:33" },
            { "button id:prefs.saturation.0 text:0 %", 0, 0, "saturation.set arg:0" },
            { NULL }
        };
        const iMenuItem monoFontItems[] = {
            { "button id:prefs.mono.gemini" },
            { "button id:prefs.mono.gopher" },
            { NULL }
        };
        const iMenuItem boldLinkItems[] = {
            { "button id:prefs.boldlink.visited" },
            { "button id:prefs.boldlink.dark" },
            { "button id:prefs.boldlink.light" },
            { NULL }
        };
        const iMenuItem quoteItems[] = {
            { "button id:prefs.quoteicon.1 label:prefs.quoteicon.icon", 0, 0, "quoteicon.set arg:1" },
            { "button id:prefs.quoteicon.0 label:prefs.quoteicon.line", 0, 0, "quoteicon.set arg:0" },
            { NULL }
        };
        const iMenuItem generalPanelItems[] = {
            { "title id:heading.prefs.general" },
            { "heading text:${prefs.searchurl}" },
            { "input id:prefs.searchurl url:1 noheading:1" },
            { "padding" },
            { "toggle id:prefs.bookmarks.addbottom" },
            { "toggle id:prefs.dataurl.openimages" },
            { "toggle id:prefs.archive.openindex" },
            { "toggle id:prefs.markdown.viewsource" },
            { "radio device:1 id:prefs.pinsplit", 0, 0, (const void *) pinSplitItems },
            { "padding" },
            { "dropdown id:prefs.uilang", 0, 0, (const void *) langItems },
            { "toggle id:prefs.time.24h" },
            { "padding" },
            { NULL }
        };
        const iMenuItem uiPanelItems[] = {
            { "title id:heading.prefs.interface" },
            { "padding arg:0.667" },
            { "dropdown device:0 id:prefs.returnkey", 0, 0, (const void *) returnKeyBehaviors },
            { "toggle device:2 id:prefs.hidetoolbarscroll" },
            { "toggle id:prefs.bottomnavbar text:${LC:prefs.bottomnavbar}" },
            { "toggle id:prefs.bottomtabbar text:${LC:prefs.bottomtabbar}" },
            { "padding" },
            { "toggle id:prefs.swipe.edge" },
            { "toggle id:prefs.swipe.page" },
            { "heading device:2 id:heading.prefs.toolbaractions" },
            { "dropdown device:2 id:prefs.toolbaraction1", 0, 0, (const void *) toolbarActionItems[0] },
            { "dropdown device:2 id:prefs.toolbaraction2", 0, 0, (const void *) toolbarActionItems[1] },
            { "heading id:heading.prefs.sizing" },
            { "input id:prefs.uiscale maxlen:5" },
            { "padding" },
            { NULL }
        };
        const iMenuItem colorPanelItems[] = {
            { "title id:heading.prefs.colors" },
            { "padding arg:0.667" },
#if !defined (iPlatformAndroidMobile)
            { "toggle id:prefs.ostheme" },
#endif
            { "radio id:prefs.theme", 0, 0, (const void *) themeItems },
            { "radio horizontal:1 rowlen:3 id:prefs.accent", 0, 0, (const void *) accentItems },
            { "heading id:heading.prefs.pagecontent" },
            { "dropdown id:prefs.doctheme.dark", 0, 0, (const void *) docThemes[0] },
            { "dropdown id:prefs.doctheme.light", 0, 0, (const void *) docThemes[1] },
            { "radio horizontal:1 id:prefs.saturation", 0, 0, (const void *) satItems },
            { "padding" },
            { "dropdown id:prefs.imagestyle", 0, 0, (const void *) imgStyles },
            { "padding" },
            { NULL }
        };
        const iMenuItem fontPanelItems[] = {
            { "title id:heading.prefs.fonts" },
            { "padding arg:0.667" },
            { "dropdown id:prefs.font.heading", 0, 0, (const void *) constData_Array(makeFontItems_("heading")) },
            { "dropdown id:prefs.font.body", 0, 0, (const void *) constData_Array(makeFontItems_("body")) },
            { "dropdown id:prefs.font.mono", 0, 0, (const void *) constData_Array(makeFontItems_("mono")) },
            { "buttons id:prefs.mono", 0, 0, (const void *) monoFontItems },
            { "padding" },
            { "dropdown id:prefs.font.monodoc", 0, 0, (const void *) constData_Array(makeFontItems_("monodoc")) },
            { "padding" },
            { "toggle id:prefs.font.warnmissing" },
            { "heading id:prefs.gemtext.ansi" },
            { "toggle id:prefs.gemtext.ansi.fg" },
            { "toggle id:prefs.gemtext.ansi.bg" },
            { "toggle id:prefs.gemtext.ansi.fontstyle" },
//            { "padding android:1" },
//            { "dropdown android:1 id:prefs.font.ui", 0, 0, (const void *) constData_Array(makeFontItems_("ui")) },
            { "padding" },
            { "button text:" fontpack_Icon " " uiTextAction_ColorEscape "${menu.fonts}", 0, 0, "!open url:about:fonts" },
            { "padding" },
            { NULL }
        };
        const iMenuItem stylePanelItems[] = {
            { "title id:heading.prefs.style" },
            { "radio horizontal:1 id:prefs.linewidth", 0, 0, (const void *) lineWidthItems },
            { "padding" },
            { "toggle id:prefs.justify" },
            { "toggle id:prefs.biglede" },
            { "toggle id:prefs.plaintext.wrap" },
            { "toggle id:prefs.gopher.gemstyle" },
            { "padding" },
            { "input id:prefs.linespacing maxlen:5" },
            { "input id:prefs.tabwidth maxlen:3" },
            { "radio id:prefs.quoteicon", 0, 0, (const void *) quoteItems },
            { "buttons id:prefs.boldlink", 0, 0, (const void *) boldLinkItems },
            { "padding" },
            { "toggle id:prefs.sideicon" },
            { "toggle id:prefs.centershort" },
            { "toggle id:prefs.collapsepreonload" },
            { "padding" },
            { NULL }
        };
        const iMenuItem networkPanelItems[] = {
            { "title id:heading.prefs.network" },
            { "heading text:${prefs.proxy.gemini}" },
            { "input id:prefs.proxy.gemini noheading:1" },
            { "heading text:${prefs.proxy.gopher}" },
            { "input id:prefs.proxy.gopher noheading:1" },
            { "heading text:${prefs.proxy.http}" },
            { "input id:prefs.proxy.http noheading:1" },
            { "padding" },
            { "input id:prefs.cachesize maxlen:4 selectall:1 unit:mb" },
            { "input id:prefs.memorysize maxlen:4 selectall:1 unit:mb" },
            { "padding" },
            { "toggle id:prefs.decodeurls" },
            { "input id:prefs.urlsize maxlen:7 selectall:1" },
            { "padding" },
            { "toggle id:prefs.redirect.allowscheme" },
            { "padding" },
            { NULL }
        };
        const iMenuItem identityPanelItems[] = {
            { "title id:sidebar.identities" },
            { "certlist" },
            { "navi.action id:prefs.ident.import text:" import_Icon, 0, 0, "ident.import" },
            { "navi.action id:prefs.ident.new text:" add_Icon, 0, 0, "ident.new" },
            { NULL }
        };
        iString *aboutText = collectNew_String(); {
            setCStr_String(aboutText, "Lagrange " LAGRANGE_APP_VERSION);
#if defined (iPlatformAppleMobile)
            appendFormat_String(aboutText, " (" LAGRANGE_IOS_VERSION ") %s" LAGRANGE_IOS_BUILD_DATE,
                                escape_Color(uiTextDim_ColorId));
#endif
#if defined (iPlatformAndroidMobile)
            appendFormat_String(aboutText, " (" LAGRANGE_ANDROID_VERSION ") %s" LAGRANGE_ANDROID_BUILD_DATE,
                                escape_Color(uiTextDim_ColorId));
#endif
        }
        const iMenuItem userPanelItems[] = {
            { "title id:heading.prefs.user" },
            { "padding arg:0.667" },
            { "button text:" export_Icon " " uiTextAction_ColorEscape "${menu.export}", 0, 0, "export" },
            { "button text:" import_Icon " " uiTextAction_ColorEscape "${menu.import}", 0, 0, "file.open" },
            { "padding" },
            { "button text:" book_Icon " ${menu.bookmarks.list}", 0, 0, "!open url:about:bookmarks" },
            { "button text:" bookmark_Icon " ${menu.bookmarks.bytag}", 0, 0, "!open url:about:bookmarks?tags" },
            { "button text:" clock_Icon " ${macos.menu.bookmarks.bytime}", 0, 0, "!open url:about:bookmarks?created" },
            { "padding" },
            { "button text:" star_Icon " ${menu.feeds.entrylist}", 0, 0, "!open url:about:feeds" },
            { "padding" },
            { "button text:" download_Icon " ${menu.downloads}", 0, 0, "downloads.open" },
            { NULL }
        };
        const iMenuItem aboutPanelItems[] = {
            { format_CStr("heading text:%s", cstr_String(aboutText)) },
            { "button text:" clock_Icon " ${menu.releasenotes}", 0, 0, "!open url:about:version" },
            { "padding" },
            { "button text:" globe_Icon " " uiTextAction_ColorEscape "${menu.website}", 0, 0, "!open url:https://gmi.skyjake.fi/lagrange" },
            { "button text:" person_Icon " " uiTextAction_ColorEscape "@jk@skyjake.fi", 0, 0, "!open default:1 url:https://skyjake.fi/@jk" },
            { "button text:" envelope_Icon " " uiTextAction_ColorEscape "${menu.email}", 0, 0, "!open default:1 url:mailto:jaakko.keranen@iki.fi" },
            { "padding" },
            { "button text:" info_Icon " ${menu.aboutpages}", 0, 0, "!open url:about:about" },
            { "button text:" bug_Icon " ${menu.debug}", 0, 0, "!open url:about:debug" },
            { NULL }
        };
        iWidget *dlg = makePanels_Mobile("prefs", (iMenuItem[]){
            { "padding arg:0.333" },
            { "title id:heading.settings" },
            { "padding arg:0.167" },
            { "panel text:" gear_Icon " ${heading.prefs.general}", 0, 0, (const void *) generalPanelItems },
            { "panel icon:0x1f4f1 id:heading.prefs.interface", 0, 0, (const void *) uiPanelItems },
            { "panel icon:0x1f5a7 id:heading.prefs.network", 0, 0, (const void *) networkPanelItems },
            { "panel noscroll:1 text:" person_Icon " ${sidebar.identities}", 0, 0, (const void *) identityPanelItems },
            { "padding" },
            { "panel icon:0x1f3a8 id:heading.prefs.colors", 0, 0, (const void *) colorPanelItems },
            { "panel icon:0x1f5da id:heading.prefs.fonts", 0, 0, (const void *) fontPanelItems },
            { "panel icon:0x1f660 id:heading.prefs.style", 0, 0, (const void *) stylePanelItems },
            { "padding" },
            { "panel icon:0x1f4e6 id:heading.prefs.user", 0, 0, (const void *) userPanelItems },
            { "heading id:heading.prefs.support" },
            { "button text:" info_Icon " ${menu.help}", 0, 0, "!open url:about:help" },
            { "panel text:" planet_Icon " ${menu.about}", 0, 0, (const void *) aboutPanelItems },
            { NULL }
        }, NULL, 0);
        setupSheetTransition_Mobile(dlg, iTrue);
        return dlg;
    }
    iWidget *dlg = makeSheet_Widget("prefs");
    addDialogTitle_(dlg, "${heading.prefs}", NULL);
    iWidget *tabs = makeTabs_Widget(dlg);
    setVerticalTabBar_Widget(tabs);
    setBackgroundColor_Widget(findChild_Widget(tabs, "tabs.buttons"), uiBackgroundSidebar_ColorId);
    setId_Widget(tabs, "prefs.tabs");
    iWidget *headings, *values;
    /* General settings. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs,
                                                   gear_Icon " ${heading.prefs.general}",
                                                   uiIcon_ColorId,
                                                   '1',
                                                   &headings,
                                                   &values),
                     "prefs.page.general");
#if defined (LAGRANGE_ENABLE_DOWNLOAD_EDIT)
        addPrefsInputWithHeading_(headings, values, "prefs.downloads", iClob(new_InputWidget(0)));
#endif
        iInputWidget *searchUrl;
        addPrefsInputWithHeading_(headings, values, "prefs.searchurl", iClob(searchUrl = new_InputWidget(0)));
        setUrlContent_InputWidget(searchUrl, iTrue);
        addDialogPadding_(headings, values);
        addDialogToggle_(headings, values, "${prefs.retaintabs}", "prefs.retaintabs");
        if (deviceType_App() != phone_AppDeviceType) {
            addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.pinsplit}")));
            iWidget *pinSplit = new_Widget();
            /* Split mode document pinning. */ {
                addRadioButton_(pinSplit, "prefs.pinsplit.0", "${prefs.pinsplit.none}", "pinsplit.set arg:0");
                addRadioButton_(pinSplit, "prefs.pinsplit.1", "${prefs.pinsplit.left}", "pinsplit.set arg:1");
                addRadioButton_(pinSplit, "prefs.pinsplit.2", "${prefs.pinsplit.right}", "pinsplit.set arg:2");
            }
            addChildFlags_Widget(values, iClob(pinSplit), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        }
        addDialogPadding_(headings, values);
        /* UI languages. */ {
            iArray *uiLangs = collectNew_Array(sizeof(iMenuItem));
            pushBackN_Array(uiLangs, langItems, iElemCount(langItems) - 1);
            /* TODO: Add an arrange flag for resizing parent to widest child. */
            /*
            size_t widestPos = findWidestLabel_MenuItem(data_Array(uiLangs), size_Array(uiLangs));
            addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.uilang}")));
            setId_Widget(addChildFlags_Widget(values,
                                              iClob(makeMenuButton_LabelWidget(
                                                  value_Array(uiLangs, widestPos, iMenuItem).label,
                                                  data_Array(uiLangs),
                                                  size_Array(uiLangs))),
                                              algnLeft_WidgetFlag),
                         "prefs.uilang");*/
            addDialogDropMenu_(headings, values, "${prefs.uilang}",
                               constData_Array(uiLangs), size_Array(uiLangs),
                               "prefs.uilang");
        }
    }
    /* Appearance. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs, palette_Icon " ${heading.prefs.appearance}", red_ColorId, '2', &headings, &values),
                     "prefs.page.appearance");
//        makeTwoColumnHeading_("${heading.prefs.uitheme}", headings, values);
#if (defined (iPlatformApple) || defined (iPlatformMSys)) && !defined (iPlatformTerminal)
        addDialogToggle_(headings, values, "${prefs.ostheme}", "prefs.ostheme");
#endif
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.theme}")));
        iWidget *themes = new_Widget();
        /* Themes. */ {
            setId_Widget(addChild_Widget(themes, iClob(new_LabelWidget("${prefs.theme.black}", "theme.set arg:0"))), "prefs.theme.0");
            setId_Widget(addChild_Widget(themes, iClob(new_LabelWidget("${prefs.theme.dark}", "theme.set arg:1"))), "prefs.theme.1");
            setId_Widget(addChild_Widget(themes, iClob(new_LabelWidget("${prefs.theme.light}", "theme.set arg:2"))), "prefs.theme.2");
            setId_Widget(addChild_Widget(themes, iClob(new_LabelWidget("${prefs.theme.white}", "theme.set arg:3"))), "prefs.theme.3");
        }
        addChildFlags_Widget(values, iClob(themes), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        /* Accents. */ {
            iLabelWidget *accentMenu = addDialogDropMenu_(headings,
                                                          values,
                                                          "${prefs.accent}",
                                                          accentItems,
                                                          iElemCount(accentItems),
                                                          "prefs.accent");
            int accentId = 0;
            iForEach(ObjectList,
                     i,
                     children_Widget(findChild_Widget(constAs_Widget(accentMenu), "menu"))) {
                if (isInstance_Object(i.object, &Class_LabelWidget)) {
                    setIconColor_LabelWidget(i.object, color_ColorAccent(accentId++, iTrue));
                }
            }
        }
        addDialogPadding_(headings, values);
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
        addDialogToggle_(headings, values, "${prefs.customframe}", "prefs.customframe");
#endif
        addDialogToggleGroup_(
            headings,
            values,
            "${prefs.uilayout}",
            (const char *[]) {
#if defined (LAGRANGE_MAC_MENUBAR)
                "prefs.bottomnavbar", "prefs.bottomtabbar", NULL
#else
                "prefs.bottomnavbar", "prefs.bottomtabbar", "prefs.menubar", NULL
#endif
            },
            iInvalidSize);
        addDialogToggle_(headings, values, "${prefs.evensplit}", "prefs.evensplit");
        if (!isTerminal_Platform()) {
            addDialogPadding_(headings, values);
//            makeTwoColumnHeading_("${heading.prefs.sizing}", headings, values);
            addPrefsInputWithHeading_(headings, values, "prefs.uiscale", iClob(new_InputWidget(5)));
            addDialogToggle_(headings, values, "${prefs.retainwindow}", "prefs.retainwindow");
            addDialogPadding_(headings, values);
            addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.font.ui}")));
            addFontButtons_(values, "ui");
            addDialogToggle_(headings, values, "${prefs.font.smooth}", "prefs.font.smooth");
        }
    }
    /* Page style. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs,
                                                   fonts_Icon " ${heading.prefs.style}",
                                                   orange_ColorId,
                                                   '3',
                                                   &headings,
                                                   &values),
                     "prefs.page.style");
        //        makeTwoColumnHeading_("${heading.prefs.paragraph}", headings, values);
        //        makeTwoColumnHeading_("${heading.prefs.pagecontent}", headings, values);
        for (int i = 0; i < 2; ++i) {
            const iBool isDark = (i == 0);
            const char *mode = isDark ? "dark" : "light";
            addChild_Widget(headings, iClob(makeHeading_Widget(isDark ? "${prefs.doctheme.dark}" : "${prefs.doctheme.light}")));
            iLabelWidget *button = makeMenuButton_LabelWidget(
                docThemes[i][findWidestLabel_MenuItem(docThemes[i], max_GmDocumentTheme)].label,
                docThemes[i],
                max_GmDocumentTheme);
            setBackgroundColor_Widget(findChild_Widget(as_Widget(button), "menu"), uiBackgroundMenu_ColorId);
            setId_Widget(addChildFlags_Widget(values, iClob(button), alignLeft_WidgetFlag),
                         format_CStr("prefs.doctheme.%s", mode));
        }
        addDialogPadding_(headings, values);
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.saturation}")));
        iWidget *sats = new_Widget();
        /* Saturation levels. */ {
            addRadioButton_(sats, "prefs.saturation.3", "100 %", "saturation.set arg:100");
            addRadioButton_(sats, "prefs.saturation.2", "66 %", "saturation.set arg:66");
            addRadioButton_(sats, "prefs.saturation.1", "33 %", "saturation.set arg:33");
            addRadioButton_(sats, "prefs.saturation.0", "0 %", "saturation.set arg:0");
        }
        addChildFlags_Widget(values, iClob(sats), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        /* Colorize images. */ {
            addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.imagestyle}")));
            iLabelWidget *button = makeMenuButton_LabelWidget(
                imgStyles[findWidestLabel_MenuItem(imgStyles, iElemCount(imgStyles) - 1)].label,
                imgStyles,
                iElemCount(imgStyles) - 1);
            setBackgroundColor_Widget(findChild_Widget(as_Widget(button), "menu"),
                                      uiBackgroundMenu_ColorId);
            setId_Widget(addChildFlags_Widget(values, iClob(button), alignLeft_WidgetFlag),
                         "prefs.imagestyle");
        }
//        addDialogPadding_(headings, values);
        /* Text settings. */
        if (!isTerminal_Platform()) {
            makeTwoColumnHeading_("${heading.prefs.fonts}", headings, values);
            addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.font.heading}")));
            addFontButtons_(values, "heading");
            addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.font.body}")));
            addFontButtons_(values, "body");
            addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.font.mono}")));
            addFontButtons_(values, "mono");
            addDialogPadding_(headings, values);
            addDialogToggleGroup_(headings,
                                  values,
                                  "${prefs.mono}",
                                  (const char *[]){ "prefs.mono.gemini", "prefs.mono.gopher" },
                                  2);
            addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.font.monodoc}")));
            addFontButtons_(values, "monodoc");
        }
        else {
            /* Terminal font settings. */
            addDialogToggle_(headings, values, "${prefs.tui.simple}", "prefs.tui.simple");
        }
        addDialogPadding_(headings, values);
        addDialogToggleGroup_(headings,
                              values,
                              "${prefs.boldlink}",
                              (const char *[]){ "prefs.boldlink.visited",
                                                "prefs.boldlink.dark",
                                                "prefs.boldlink.light" },
                              3);
    }
    /* Page layout. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs,
                                                   pageLayout_Icon " ${heading.prefs.layout}",
                                                   orange_ColorId,
                                                   '4',
                                                   &headings,
                                                   &values),
                     "prefs.page.layout");
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.linewidth}")));
        iWidget *widths = new_Widget();
        /* Line widths. */ {
            /* TODO: Make this a utility function to build radio buttons from items. */
            for (size_t i = 0; lineWidthItems[i].label; i++) {
                const iMenuItem *lw = &lineWidthItems[i];
                addRadioButton_(widths,
                                cstr_Command(lw->label, "id"),
                                hasLabel_Command(lw->label, "label")
                                    ? cstr_Lang(cstr_Command(lw->label, "label"))
                                    : cstr_Command(lw->label, "text"),
                                lw->command);
            }
        }
        addChildFlags_Widget(values, iClob(widths), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        addPrefsInputWithHeading_(headings, values, "prefs.linespacing", iClob(new_InputWidget(5)));
        addPrefsInputWithHeading_(headings, values, "prefs.tabwidth", iClob(new_InputWidget(5)));
#if defined (LAGRANGE_ENABLE_HARFBUZZ)
        addDialogToggle_(headings, values, "${prefs.justify}", "prefs.justify");
#endif
        addDialogToggle_(headings, values, "${prefs.biglede}", "prefs.biglede");
        addDialogPadding_(headings, values);
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.quoteicon}")));
        iWidget *quote = new_Widget(); {
            addRadioButton_(quote, "prefs.quoteicon.1", "${prefs.quoteicon.icon}", "quoteicon.set arg:1");
            addRadioButton_(quote, "prefs.quoteicon.0", "${prefs.quoteicon.line}", "quoteicon.set arg:0");
        }
        addChildFlags_Widget(values, iClob(quote), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        addDialogToggle_(headings, values, "${prefs.plaintext.wrap}", "prefs.plaintext.wrap");
        addDialogPadding_(headings, values);
        addDialogToggle_(headings, values, "${prefs.centershort}", "prefs.centershort");
        addDialogToggle_(headings, values, "${prefs.sideicon}", "prefs.sideicon");
        addDialogToggle_(headings, values, "${prefs.collapsepreonload}", "prefs.collapsepreonload");
    }
    /* Content. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs, photo_Icon " ${heading.prefs.content}",
                                                   green_ColorId, '5', &headings, &values),
                     "prefs.page.content");
        /* Cache size. */ {
            iInputWidget *cache = new_InputWidget(4);
            setSelectAllOnFocus_InputWidget(cache, iTrue);
            addPrefsInputWithHeading_(headings, values, "prefs.cachesize", iClob(cache));
            iWidget *unit =
                addChildFlags_Widget(as_Widget(cache),
                                     iClob(new_LabelWidget("${mb}", NULL)),
                                     frameless_WidgetFlag | moveToParentRightEdge_WidgetFlag |
                                         resizeToParentHeight_WidgetFlag);
            setContentPadding_InputWidget(cache, 0, width_Widget(unit) - 4 * gap_UI);
        }
        /* Memory size. */ {
            iInputWidget *mem = new_InputWidget(4);
            setSelectAllOnFocus_InputWidget(mem, iTrue);
            addPrefsInputWithHeading_(headings, values, "prefs.memorysize", iClob(mem));
            iWidget *unit =
                addChildFlags_Widget(as_Widget(mem),
                                     iClob(new_LabelWidget("${mb}", NULL)),
                                     frameless_WidgetFlag | moveToParentRightEdge_WidgetFlag |
                                         resizeToParentHeight_WidgetFlag);
            setContentPadding_InputWidget(mem, 0, width_Widget(unit) - 4 * gap_UI);
        }
        addDialogPadding_(headings, values);
        addDialogToggleGroup_(headings,
                              values,
                              "${prefs.gemtext.ansi}",
                              (const char *[]){ "prefs.gemtext.ansi.fg",
                                                "prefs.gemtext.ansi.bg",
                                                "prefs.gemtext.ansi.fontstyle" },
                              3);
        addDialogToggle_(headings, values, "${prefs.gopher.gemstyle}", "prefs.gopher.gemstyle");
        addDialogToggle_(headings, values, "${prefs.markdown.viewsource}", "prefs.markdown.viewsource");
        addDialogPadding_(headings, values);
        addDialogToggle_(headings, values, "${prefs.dataurl.openimages}", "prefs.dataurl.openimages");
        addDialogToggle_(headings, values, "${prefs.archive.openindex}", "prefs.archive.openindex");
        addDialogPadding_(headings, values);
        addDialogToggle_(headings, values, "${prefs.font.warnmissing}", "prefs.font.warnmissing");
    }
    /* User Interface. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs, computer_Icon " ${heading.prefs.interface}", cyan_ColorId, '6', &headings, &values),
                     "prefs.page.ui");
        addDialogToggle_(headings, values, "${prefs.hoverlink}", "prefs.hoverlink");
        addDialogToggle_(headings, values, "${prefs.bookmarks.addbottom}", "prefs.bookmarks.addbottom");
        /* Return key behaviors. */ {
            addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.returnkey}")));
            iLabelWidget *returnKey = makeMenuButton_LabelWidget(
                returnKeyBehaviors[findWidestLabel_MenuItem(returnKeyBehaviors,
                                                            iElemCount(returnKeyBehaviors) - 1)]
                    .label,
                returnKeyBehaviors,
                iElemCount(returnKeyBehaviors) - 1);
            setBackgroundColor_Widget(findChild_Widget(as_Widget(returnKey), "menu"),
                                      uiBackgroundMenu_ColorId);
            setId_Widget(addChildFlags_Widget(values, iClob(returnKey), alignLeft_WidgetFlag),
                         "prefs.returnkey");
        }
        if (!isTerminal_Platform()) {
            addDialogToggle_(headings, values, "${prefs.imageloadscroll}", "prefs.imageloadscroll");
        }
        addDialogToggle_(headings, values, "${prefs.time.24h}", "prefs.time.24h");
        addDialogPadding_(headings, values);
        addDialogToggle_(headings, values, "${prefs.animate}", "prefs.animate");
        if (!isTerminal_Platform()) {
            addDialogToggle_(headings, values, "${prefs.blink}", "prefs.blink");
        }
//        makeTwoColumnHeading_("${heading.prefs.scrolling}", headings, values);
        addDialogToggle_(headings, values, "${prefs.smoothscroll}", "prefs.smoothscroll");
        /* Scroll speeds. */ {
            for (int type = 0; type < max_ScrollType; type++) {
                const char *typeStr = (type == mouse_ScrollType ? "mouse" : "keyboard");
                addChild_Widget(headings,
                                iClob(makeHeading_Widget(type == mouse_ScrollType
                                                             ? "${prefs.scrollspeed.mouse}"
                                                             : "${prefs.scrollspeed.keyboard}")));
                /* TODO: Make a SliderWidget. */
                iWidget *scrollSpeed = new_Widget();
                addRadioButton_(scrollSpeed, format_CStr("prefs.scrollspeed.%s.7",  typeStr), "0", format_CStr("scrollspeed arg:7  type:%d", type));
                addRadioButton_(scrollSpeed, format_CStr("prefs.scrollspeed.%s.10", typeStr), "1", format_CStr("scrollspeed arg:10 type:%d", type));
                addRadioButton_(scrollSpeed, format_CStr("prefs.scrollspeed.%s.13", typeStr), "2", format_CStr("scrollspeed arg:13 type:%d", type));
                addRadioButton_(scrollSpeed, format_CStr("prefs.scrollspeed.%s.17", typeStr), "3", format_CStr("scrollspeed arg:17 type:%d", type));
                addRadioButton_(scrollSpeed, format_CStr("prefs.scrollspeed.%s.23", typeStr), "4", format_CStr("scrollspeed arg:23 type:%d", type));
                addRadioButton_(scrollSpeed, format_CStr("prefs.scrollspeed.%s.30", typeStr), "5", format_CStr("scrollspeed arg:30 type:%d", type));
                addRadioButton_(scrollSpeed, format_CStr("prefs.scrollspeed.%s.40", typeStr), "6", format_CStr("scrollspeed arg:40 type:%d", type));
                addChildFlags_Widget(
                    values, iClob(scrollSpeed), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
            }
        }
//        if (deviceType_App() == phone_AppDeviceType) {
//            addDialogToggle_(headings, values, "${prefs.hidetoolbarscroll}", "prefs.hidetoolbarscroll");
//        }
    }
    /* Keybindings. */ {
        iBindingsWidget *bind = new_BindingsWidget();
        appendFramelessTabPage_Widget(tabs, iClob(bind), keyboard_Icon " ${heading.prefs.keys}",
                                      cyan_ColorId, '7', KMOD_PRIMARY);
    }
    /* Network. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs,
                                                   network_Icon " ${heading.prefs.network}",
                                                   blue_ColorId,
                                                   '8',
                                                   &headings,
                                                   &values),
                     "prefs.page.network");
        addDialogToggle_(headings, values, "${prefs.redirect.allowscheme}", "prefs.redirect.allowscheme");
        addDialogToggle_(headings, values, "${prefs.decodeurls}", "prefs.decodeurls");
        addPrefsInputWithHeading_(headings, values, "prefs.urlsize", iClob(new_InputWidget(10)));
        makeTwoColumnHeading_("${heading.prefs.proxies}", headings, values);
        addPrefsInputWithHeading_(headings, values, "prefs.proxy.gemini", iClob(new_InputWidget(0)));
        addPrefsInputWithHeading_(headings, values, "prefs.proxy.gopher", iClob(new_InputWidget(0)));
        addPrefsInputWithHeading_(headings, values, "prefs.proxy.http", iClob(new_InputWidget(0)));
        makeTwoColumnHeading_("${heading.prefs.certs}", headings, values);
        addPrefsInputWithHeading_(headings, values, "prefs.ca.file", iClob(new_InputWidget(0)));
        addPrefsInputWithHeading_(headings, values, "prefs.ca.path", iClob(new_InputWidget(0)));
    }
    addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
    updatePreferencesLayout_Widget(dlg);
    const iMenuItem actions[] = { { "${menu.fonts}", 0, 0, "!open url:about:fonts" },
                                { "---" },
                                { "${close}", SDLK_ESCAPE, 0, "prefs.dismiss" } };
    const size_t actOffset = (isTerminal_Platform() ? 2 : 0);
    iWidget *buttons = addChild_Widget(
        dlg, iClob(makeDialogButtons_Widget(actions + actOffset, iElemCount(actions) - actOffset)));
    setId_Widget(child_Widget(buttons, 0), "prefs.aboutfonts");
//    setFlags_Widget(findChild_Widget(dlg, "prefs.aboutfonts"), hidden_WidgetFlag, iTrue);
    addChild_Widget(dlg->root->widget, iClob(dlg));
    setupSheetTransition_Mobile(dlg, iTrue);
    return dlg;
}

static iBool isBookmarkFolder_(void *context, const iBookmark *bm) {
    iUnused(context);
    return isFolder_Bookmark(bm);
}

static const iArray *makeBookmarkFolderItems_(iBool withNullTerminator, uint32_t omitFolderId) {
    iArray *folders = new_Array(sizeof(iMenuItem));
    pushBack_Array(folders, &(iMenuItem){ "\u2014", 0, 0, "dlg.bookmark.setfolder arg:0" });
    iConstForEach(
        PtrArray,
        i,
        list_Bookmarks(bookmarks_App(), cmpTree_Bookmark, isBookmarkFolder_, NULL)) {
        const iBookmark *bm = i.ptr;
        if (id_Bookmark(bm) == omitFolderId || hasParent_Bookmark(bm, omitFolderId)) {
            continue;
        }
        iString *title = collect_String(copy_String(&bm->title));
        for (const iBookmark *j = bm; j && j->parentId; ) {
            j = get_Bookmarks(bookmarks_App(), j->parentId);
            prependCStr_String(title, " > ");
            prepend_String(title, &j->title);
        }
        pushBack_Array(
            folders,
            &(iMenuItem){ cstr_String(title),
                          0,
                          0,
                          format_CStr("dlg.bookmark.setfolder arg:%u", id_Bookmark(bm)) });
    }
    if (withNullTerminator) {
        pushBack_Array(folders, &(iMenuItem){ NULL });
    }
    return collect_Array(folders);
}

iWidget *makeBookmarkEditor_Widget(uint32_t folderId, iBool withDup) {
    const iBool isFolder = (folderId != 0);
    if (isFolder) {
        withDup = iFalse;
    }
    const iMenuItem dupActions[] = {
        { "${menu.dup}", 0, 0, "bmed.dup" },
        { "---" },
        { "${cancel}", SDLK_ESCAPE, 0, "bmed.cancel" },
        { uiTextAction_ColorEscape "${dlg.bookmark.save}", SDLK_RETURN, KMOD_ACCEPT, "bmed.accept" }
    };
    const iMenuItem actions[] = { dupActions[2], dupActions[3] };
    /* List of identities, if changing identity is necessary for opening the bookmark. */
    iArray *identItems = collectNew_Array(sizeof(iMenuItem)); {
        pushBack_Array(identItems, &(iMenuItem){ "\u2014", 0, 0, "bmed.setident fp:" });
        pushBack_Array(identItems, &(iMenuItem){ "---" });
        appendIdentities_MenuItem(identItems, "bmed.setident");
        pushBack_Array(identItems, &(iMenuItem){ NULL });
    }
    iWidget *dlg = NULL;
    if (isUsingPanelLayout_Mobile()) {
        const iArray *parentFolderItems = makeBookmarkFolderItems_(iTrue, folderId);
        const iMenuItem folderItems[] = {
            { "title id:bmed.heading text:${heading.bookmark.editfolder}" },
            { "input id:bmed.title text:${dlg.bookmark.title}" },
            { "dropdown id:bmed.folder text:${dlg.bookmark.parentfolder}", 0, 0,
                  (const void *) constData_Array(parentFolderItems) },
            { "padding" },
            { NULL }
        };
        const iMenuItem items[] = {
            { "title id:bmed.heading text:${heading.bookmark.edit}" },
            { "input id:bmed.title noheading:1 hint:dlg.bookmark.title" },
            { "input id:bmed.url url:1 noheading:1 hint:dlg.bookmark.url" },
            { "padding" },
            { "dropdown id:bmed.folder text:"
              uiTextAction_ColorEscape folder_Icon uiTextStrong_ColorEscape
              " ${dlg.bookmark.folder}", 0, 0,
              (const void *) constData_Array(parentFolderItems) },
            { "padding" },
            { "dropdown id:bmed.setident text:${LC:dlg.bookmark.identity}", 0, 0,
              (const void *) constData_Array(identItems) },
            { "padding" },
            { "input id:bmed.tags hint:hint.dlg.bookmark.tags text:${dlg.bookmark.tags}" },
            { "input id:bmed.notes hint:hint.dlg.bookmark.notes text:${dlg.bookmark.notes}" },
            { "input id:bmed.icon maxlen:1 text:${dlg.bookmark.icon}" },
            { "heading text:${heading.bookmark.tags}" },
            { "toggle id:bmed.tag.home text:${LC:bookmark.tag.home}" },
            { "toggle id:bmed.tag.remote text:${LC:bookmark.tag.remote}" },
            { "toggle device:0 id:bmed.tag.linksplit text:${LC:bookmark.tag.linksplit}" },
            { "toggle device:1 id:bmed.tag.linksplit text:${LC:bookmark.tag.linksplit}" },
            { "padding" },
            { NULL }
        };
        dlg = makePanels_Mobile("bmed",
                                isFolder ? folderItems : items,
                                withDup ? dupActions : actions,
                                withDup ? iElemCount(dupActions) : iElemCount(actions));
        setupSheetTransition_Mobile(dlg, incoming_TransitionFlag | dialogTransitionDir_Widget(dlg));
    }
    else {
        dlg = makeSheet_Widget("bmed");
        addDialogTitle_(dlg,
                        isFolder ? "${heading.bookmark.editfolder}" : "${heading.bookmark.edit}",
                        "bmed.heading");
        iWidget *headings, *values;
        addChild_Widget(dlg, iClob(makeTwoColumns_Widget(&headings, &values)));
        iInputWidget *inputs[5];
        iZap(inputs);
        /* Folder to add to. */ {
            addChild_Widget(headings,
                            iClob(makeHeading_Widget(isFolder ? "${dlg.bookmark.parentfolder}"
                                                              : "${dlg.bookmark.folder}")));
            const iArray *folderItems = makeBookmarkFolderItems_(iFalse, folderId);
            iLabelWidget *folderButton;
            setId_Widget(addChildFlags_Widget(values,
                                         iClob(folderButton = makeMenuButton_LabelWidget(
                                                   widestLabel_MenuItemArray(folderItems),
                                                   constData_Array(folderItems),
                                                   size_Array(folderItems))), alignLeft_WidgetFlag),
                         "bmed.folder");
        }
        addDialogInputWithHeading_(headings, values, "${dlg.bookmark.title}", "bmed.title", iClob(inputs[0] = new_InputWidget(0)));
        if (!isFolder) {
            addDialogInputWithHeading_(headings, values, "${dlg.bookmark.url}",   "bmed.url",   iClob(inputs[1] = new_InputWidget(0)));
            setUrlContent_InputWidget(inputs[1], iTrue);
            addDialogPadding_(headings, values);
            makeIdentityDropdown_LabelWidget(
                headings, values, identItems, "${dlg.bookmark.identity}", "bmed.setident");
            addDialogInputWithHeading_(headings, values, "${dlg.bookmark.tags}",  "bmed.tags",  iClob(inputs[2] = new_InputWidget(0)));
            addDialogInputWithHeading_(headings, values, "${dlg.bookmark.notes}",  "bmed.notes",  iClob(inputs[3] = new_InputWidget(0)));
            setHint_InputWidget(inputs[2], "${hint.dlg.bookmark.tags}");
            setHint_InputWidget(inputs[3], "${hint.dlg.bookmark.notes}");
            addDialogInputWithHeading_(headings, values, "${dlg.bookmark.icon}",  "bmed.icon",  iClob(inputs[4] = new_InputWidget(1)));
            /* Buttons for special tags. */
//            addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
            iWidget *special = addChild_Widget(dlg, iClob(makeTwoColumns_Widget(&headings, &values)));
            setFlags_Widget(special, collapse_WidgetFlag, iTrue);
            setId_Widget(special, "bmed.special");
            makeTwoColumnHeading_("${heading.bookmark.tags}", headings, values);
            addDialogToggle_(headings, values, "${LC:bookmark.tag.home}:", "bmed.tag.home");
            addDialogToggle_(headings, values, "${LC:bookmark.tag.remote}:", "bmed.tag.remote");
            addDialogToggle_(headings, values, "${bookmark.tag.linksplit}:", "bmed.tag.linksplit");
        }
        arrange_Widget(dlg);
        const int inputWidth = 100 * gap_UI - headings->rect.size.x;
        for (int i = 0; i < 4; ++i) {
            if (inputs[i]) {
                as_Widget(inputs[i])->rect.size.x = inputWidth;
            }
        }
        iWidget *setIdent = findChild_Widget(dlg, "bmed.setident");
        if (setIdent) {
            setIdent->rect.size.x = inputWidth;
        }
        addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
        addChild_Widget(dlg,
                        iClob(makeDialogButtons_Widget(withDup ? dupActions : actions,
                                                       withDup ? iElemCount(dupActions)
                                                               : iElemCount(actions))));
        addChild_Widget(get_Root()->widget, iClob(dlg));
        setupSheetTransition_Mobile(dlg, iTrue);
    }
    /* Use a recently accessed folder as the default. */
    const uint32_t recentFolderId = recentFolder_Bookmarks(bookmarks_App());
    iLabelWidget *folderDrop = findChild_Widget(dlg, "bmed.folder");
    updateDropdownSelection_LabelWidget(folderDrop, format_CStr(" arg:%u", recentFolderId));
    setUserData_Object(folderDrop, get_Bookmarks(bookmarks_App(), recentFolderId));
    updateDropdownSelection_LabelWidget(findChild_Widget(dlg, "bmed.setident"), "bmed.setident fp:");
    return dlg;
}

void setBookmarkEditorParentFolder_Widget(iWidget *editor, uint32_t folderId) {
    iLabelWidget *button = findChild_Widget(editor, "bmed.folder");
    updateDropdownSelection_LabelWidget(button, format_CStr(" arg:%u", folderId));
    setUserData_Object(button, get_Bookmarks(bookmarks_App(), folderId));
}

static iBool handleBookmarkCreationCommands_SidebarWidget_(iWidget *editor, const char *cmd) {
    if (equal_Command(cmd, "dlg.bookmark.setfolder")) {
        setBookmarkEditorParentFolder_Widget(editor, arg_Command(cmd));
        return iTrue;
    }
    else if (equalWidget_Command(cmd, editor, "bmed.setident")) {
        const iString *fp = string_Command(cmd, "fp");
        iLabelWidget *setident = findChild_Widget(editor, "bmed.setident");
        set_String(&as_Widget(setident)->data, fp);
        updateDropdownSelection_LabelWidget(setident, cstr_String(fp));
        return iTrue;
    }
    if (equal_Command(cmd, "bmed.accept") || equal_Command(cmd, "bmed.cancel")) {
        if (equal_Command(cmd, "bmed.accept")) {
            const iString *title = text_InputWidget(findChild_Widget(editor, "bmed.title"));
            const iString *url   = text_InputWidget(findChild_Widget(editor, "bmed.url"));
            const iString *tags  = text_InputWidget(findChild_Widget(editor, "bmed.tags"));
            const iString *notes = text_InputWidget(findChild_Widget(editor, "bmed.notes"));
            const iString *ident = &as_Widget(findChild_Widget(editor, "bmed.setident"))->data;
            const iBookmark *folder = userData_Object(findChild_Widget(editor, "bmed.folder"));
            const iString *icon  = collect_String(trimmed_String(text_InputWidget(findChild_Widget(editor, "bmed.icon"))));
            const uint32_t id    = add_Bookmarks(bookmarks_App(), url, title, tags, first_String(icon));
            iBookmark *    bm    = get_Bookmarks(bookmarks_App(), id);
            set_String(&bm->notes, notes);
            set_String(&bm->identity, ident);
            if (!isEmpty_String(icon)) {
                bm->flags |= userIcon_BookmarkFlag;
            }
            if (isSelected_Widget(findChild_Widget(editor, "bmed.tag.home"))) {
                bm->flags |= homepage_BookmarkFlag;
            }
            if (isSelected_Widget(findChild_Widget(editor, "bmed.tag.remote"))) {
                bm->flags |= remoteSource_BookmarkFlag;
            }
            if (isSelected_Widget(findChild_Widget(editor, "bmed.tag.linksplit"))) {
                bm->flags |= linkSplit_BookmarkFlag;
            }
            bm->parentId = folder ? id_Bookmark(folder) : 0;
            setRecentFolder_Bookmarks(bookmarks_App(), bm->parentId);
            postCommandf_App("bookmarks.changed added:%zu", id);
        }
        setupSheetTransition_Mobile(editor, dialogTransitionDir_Widget(editor));
        destroy_Widget(editor);
        return iTrue;
    }
    return iFalse;
}

iWidget *makeBookmarkCreation_Widget(const iString *url, const iString *title, iChar icon) {
    iWidget *dlg = makeBookmarkEditor_Widget(0, iFalse);
    setId_Widget(dlg, "bmed.create");
    setTextCStr_LabelWidget(findChild_Widget(dlg, "bmed.heading"),
                            uiHeading_ColorEscape "${heading.bookmark.add}");
    iUrl parts;
    init_Url(&parts, url);
    setTextCStr_InputWidget(findChild_Widget(dlg, "bmed.title"),
                            title ? cstr_String(title) : cstr_Rangecc(parts.host));
    setText_InputWidget(findChild_Widget(dlg, "bmed.url"), url);
    if (icon) {
        setText_InputWidget(findChild_Widget(dlg, "bmed.icon"),
                            collect_String(newUnicodeN_String(&icon, 1)));
    }
    setCommandHandler_Widget(dlg, handleBookmarkCreationCommands_SidebarWidget_);
    return dlg;
}

static iBool handleFeedSettingCommands_(iWidget *dlg, const char *cmd) {
    if (equal_Command(cmd, "cancel")) {
        setupSheetTransition_Mobile(dlg, dialogTransitionDir_Widget(dlg));
        destroy_Widget(dlg);
        return iTrue;
    }
    if (equal_Command(cmd, "feedcfg.accept")) {
        iString *feedTitle =
            collect_String(copy_String(text_InputWidget(findChild_Widget(dlg, "feedcfg.title"))));
        trim_String(feedTitle);
        if (isEmpty_String(feedTitle)) {
            return iTrue;
        }
        int id = argLabel_Command(cmd, "bmid");
        const iBool headings = isSelected_Widget(findChild_Widget(dlg, "feedcfg.type.headings"));
        const iBool ignoreWeb = isSelected_Widget(findChild_Widget(dlg, "feedcfg.ignoreweb"));
        if (!id) {
            const size_t numSubs = numSubscribed_Feeds();
            const iString *url   = url_DocumentWidget(document_App());
            id = add_Bookmarks(bookmarks_App(),
                               url,
                               feedTitle,
                               NULL,
                               siteIcon_GmDocument(document_DocumentWidget(document_App())));
            if (numSubs == 0) {
                /* Auto-refresh after first addition. */
                /* TODO: Also when settings changed? */
                postCommand_App("feeds.refresh");
            }
        }
        iBookmark *bm = get_Bookmarks(bookmarks_App(), id);
        iAssert(bm);
        set_String(&bm->title, feedTitle);
        bm->flags |= subscribed_BookmarkFlag;
        iChangeFlags(bm->flags, headings_BookmarkFlag, headings);
        iChangeFlags(bm->flags, ignoreWeb_BookmarkFlag, ignoreWeb);
        postCommand_App("bookmarks.changed");
        setupSheetTransition_Mobile(dlg, dialogTransitionDir_Widget(dlg));
        destroy_Widget(dlg);
        return iTrue;
    }
    return iFalse;
}

iWidget *makeFeedSettings_Widget(uint32_t bookmarkId) {
    iWidget        *dlg;
    const char     *headingText = bookmarkId ? "${heading.feedcfg}" : "${heading.subscribe}";
    const iMenuItem actions[]   = { { "${cancel}" },
                                    { bookmarkId ? uiTextAction_ColorEscape "${dlg.feed.save}"
                                                 : uiTextAction_ColorEscape "${dlg.feed.sub}",
                                      SDLK_RETURN,
                                      KMOD_ACCEPT,
                                      format_CStr("feedcfg.accept bmid:%d", bookmarkId) } };
    if (isUsingPanelLayout_Mobile()) {
        const iMenuItem typeItems[] = {
            { "button id:feedcfg.type.gemini label:dlg.feed.type.gemini", 0, 0, "feedcfg.type arg:0" },
            { "button id:feedcfg.type.headings label:dlg.feed.type.headings", 0, 0, "feedcfg.type arg:1" },
            { NULL }
        };
        dlg = makePanels_Mobile("feedcfg", (iMenuItem[]){
            { format_CStr("title id:feedcfg.heading text:%s", headingText) },
            { "input id:feedcfg.title text:${dlg.feed.title}" },
            { "radio id:dlg.feed.entrytype", 0, 0, (const void *) typeItems },
            { "padding" },
            { "toggle id:feedcfg.ignoreweb text:${dlg.feed.ignoreweb}" },
            { "padding" },
            { NULL }
        }, actions, iElemCount(actions));
    }
    else {
        dlg = makeSheet_Widget("feedcfg");
        addDialogTitle_(dlg, headingText, "feedcfg.heading");
        iWidget *headings, *values;
        addChild_Widget(dlg, iClob(makeTwoColumns_Widget(&headings, &values)));
        iInputWidget *input = new_InputWidget(0);
        addDialogInputWithHeading_(headings, values, "${dlg.feed.title}", "feedcfg.title", iClob(input));
        addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.feed.entrytype}")));
        iWidget *types = new_Widget(); {
            addRadioButton_(types, "feedcfg.type.gemini", "${dlg.feed.type.gemini}", "feedcfg.type arg:0");
            addRadioButton_(types, "feedcfg.type.headings", "${dlg.feed.type.headings}", "feedcfg.type arg:1");
        }
        addChildFlags_Widget(values, iClob(types), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.feed.ignoreweb}")));
        addChild_Widget(values, iClob(makeToggle_Widget("feedcfg.ignoreweb")));
        iWidget *buttons =
            addChild_Widget(dlg, iClob(makeDialogButtons_Widget(actions, iElemCount(actions))));
        setId_Widget(child_Widget(buttons, childCount_Widget(buttons) - 1), "feedcfg.save");
        arrange_Widget(dlg);
        as_Widget(input)->rect.size.x = 100 * gap_UI - headings->rect.size.x;
        addChild_Widget(get_Root()->widget, iClob(dlg));
    }
    /* Initialize. */ {
        const iBookmark *bm  = bookmarkId ? get_Bookmarks(bookmarks_App(), bookmarkId) : NULL;
        setText_InputWidget(findChild_Widget(dlg, "feedcfg.title"),
                            bm ? &bm->title : feedTitle_DocumentWidget(document_App()));
        setFlags_Widget(findChild_Widget(dlg,
                                         bm && bm->flags & headings_BookmarkFlag
                                             ? "feedcfg.type.headings"
                                             : "feedcfg.type.gemini"),
                        selected_WidgetFlag,
                        iTrue);
        setToggle_Widget(findChild_Widget(dlg, "feedcfg.ignoreweb"),
                         bm && bm->flags & ignoreWeb_BookmarkFlag);
        setCommandHandler_Widget(dlg, handleFeedSettingCommands_);
    }
    setupSheetTransition_Mobile(dlg, incoming_TransitionFlag | dialogTransitionDir_Widget(dlg));
    return dlg;
}

/*----------------------------------------------------------------------------------------------*/

static void siteSpecificThemeChanged_(const iWidget *dlg) {
    iDocumentWidget *doc = document_App();
    setThemeSeed_GmDocument((iGmDocument *) document_DocumentWidget(doc),
                            urlPaletteSeed_String(url_DocumentWidget(doc)),
                            urlThemeSeed_String(url_DocumentWidget(doc)));
    postCommand_App("theme.changed");
}

static const iString *siteSpecificRoot_(const iWidget *dlg) {
    return collect_String(suffix_Command(cstr_String(id_Widget(dlg)), "site"));
}

static void updateSiteSpecificTheme_(iInputWidget *palSeed, void *context) {
    iWidget *dlg = context;
    const iString *siteRoot = siteSpecificRoot_(dlg);
    setValueString_SiteSpec(siteRoot, paletteSeed_SiteSpecKey, text_InputWidget(palSeed));
    siteSpecificThemeChanged_(dlg);
    /* Allow seeing the new theme. */
    setFlags_Widget(dlg, noFadeBackground_WidgetFlag, iTrue);
}

static void closeSiteSpecific_(iWidget *dlg) {
    setupSheetTransition_Mobile(dlg, dialogTransitionDir_Widget(dlg));
    delete_String(userData_Object(dlg)); /* saved original palette seed */
    destroy_Widget(dlg);
}

static iBool siteSpecificSettingsHandler_(iWidget *dlg, const char *cmd) {
    if (equal_Command(cmd, "cancel")) {
        const iBool wasNoFade = (flags_Widget(dlg) & noFadeBackground_WidgetFlag) != 0;
        iInputWidget *palSeed = findChild_Widget(dlg, "sitespec.palette");
        setText_InputWidget(palSeed, userData_Object(dlg));
        updateSiteSpecificTheme_(palSeed, dlg);
        setFlags_Widget(dlg, noFadeBackground_WidgetFlag, wasNoFade);
        closeSiteSpecific_(dlg);
        return iTrue;
    }
    if (equalArg_Command(cmd, "input.ended", "id", "sitespec.palette")) {
        setFlags_Widget(dlg, noFadeBackground_WidgetFlag, iFalse);
        refresh_Widget(dlg);
        siteSpecificThemeChanged_(dlg);
        return iTrue;
    }
    if (equal_Command(cmd, "sitespec.accept")) {
        const iInputWidget *palSeed   = findChild_Widget(dlg, "sitespec.palette");
        const iBool         warnAnsi  = isSelected_Widget(findChild_Widget(dlg, "sitespec.ansi"));
        const iString      *siteRoot  = siteSpecificRoot_(dlg);
        int                 dismissed = value_SiteSpec(siteRoot, dismissWarnings_SiteSpecKey);
        iChangeFlags(dismissed, ansiEscapes_GmDocumentWarning, !warnAnsi);
        setValue_SiteSpec(siteRoot, dismissWarnings_SiteSpecKey, dismissed);
        setValue_SiteSpec(siteRoot,
                          tlsSessionCache_SiteSpeckey,
                          isSelected_Widget(findChild_Widget(dlg, "sitespec.tlscache")));
        setValueString_SiteSpec(siteRoot, paletteSeed_SiteSpecKey, text_InputWidget(palSeed));
        siteSpecificThemeChanged_(dlg);
        /* Note: The active DocumentWidget may actually be different than when opening the dialog. */
        closeSiteSpecific_(dlg);
        return iTrue;
    }
    return iFalse;
}

iWidget *makeSiteSpecificSettings_Widget(const iString *url) {
    iWidget *dlg;
    const char *sheetId = format_CStr("sitespec site:%s", cstr_Rangecc(urlRoot_String(url)));
    const iMenuItem actions[] = {
        { "${cancel}" },
        { uiTextAction_ColorEscape "${sitespec.accept}", SDLK_RETURN, KMOD_ACCEPT, "sitespec.accept" }
    };
    if (isUsingPanelLayout_Mobile()) {
        dlg = makePanels_Mobile(sheetId, (iMenuItem[]){
            { "title id:heading.sitespec" },
            { "input id:sitespec.palette" },
            { "padding" },
            { "toggle id:sitespec.ansi" },
            { "toggle id:sitespec.tlscache" },
            { "padding" },
            { NULL }
        }, actions, iElemCount(actions));
    }
    else {
        iWidget *headings, *values;
        dlg = makeSheet_Widget(sheetId);
        addDialogTitle_(dlg, "${heading.sitespec}", "heading.sitespec");
        addChild_Widget(dlg, iClob(makeTwoColumns_Widget(&headings, &values)));
        iInputWidget *palSeed = new_InputWidget(0);
        setHint_InputWidget(palSeed, cstr_Block(urlThemeSeed_String(url)));
        addPrefsInputWithHeading_(headings, values, "sitespec.palette", iClob(palSeed));
        addDialogToggle_(headings, values, "${sitespec.ansi}", "sitespec.ansi");
        addDialogToggle_(headings, values, "${sitespec.tlscache}", "sitespec.tlscache");
        addChild_Widget(dlg, iClob(makeDialogButtons_Widget(actions, iElemCount(actions))));
        addChild_Widget(get_Root()->widget, iClob(dlg));
        as_Widget(palSeed)->rect.size.x = 60 * gap_UI;
        arrange_Widget(dlg);
    }
    /* Initialize. */ {
        const iString *site = collectNewRange_String(urlRoot_String(url));
        setToggle_Widget(findChild_Widget(dlg, "sitespec.ansi"),
                         ~value_SiteSpec(site, dismissWarnings_SiteSpecKey) & ansiEscapes_GmDocumentWarning);
        setToggle_Widget(findChild_Widget(dlg, "sitespec.tlscache"),
                         value_SiteSpec(site, tlsSessionCache_SiteSpeckey));
        iInputWidget *palSeed = findChild_Widget(dlg, "sitespec.palette");
        setText_InputWidget(palSeed, valueString_SiteSpec(site, paletteSeed_SiteSpecKey));
        setHint_InputWidget(palSeed, cstr_Block(urlThemeSeed_String(url)));
        /* Keep a copy of the original palette seed for restoring on cancel. */
        setUserData_Object(dlg, copy_String(valueString_SiteSpec(site, paletteSeed_SiteSpecKey)));
        if (!isUsingPanelLayout_Mobile()) {
            setValidator_InputWidget(findChild_Widget(dlg, "sitespec.palette"),
                                     updateSiteSpecificTheme_, dlg);
        }
    }
    setCommandHandler_Widget(dlg, siteSpecificSettingsHandler_);
    setupSheetTransition_Mobile(dlg, incoming_TransitionFlag | dialogTransitionDir_Widget(dlg));
    setFocus_Widget(findChild_Widget(dlg, "sitespec.palette"));
    return dlg;
}

/*----------------------------------------------------------------------------------------------*/

iWidget *makeIdentityCreation_Widget(void) {
    const iMenuItem actions[] = { { "${dlg.newident.more}", 0, 0, "ident.showmore" },
                                  { "---" },
                                  { "${cancel}", SDLK_ESCAPE, 0, "ident.cancel" },
                                  { uiTextAction_ColorEscape "${dlg.newident.create}",
                                    SDLK_RETURN,
                                    KMOD_ACCEPT,
                                    "ident.accept" } };
    const iString *docUrl = url_DocumentWidget(document_App());
    iUrl url;
    init_Url(&url, docUrl);
    const iMenuItem scopeItems[] = {
        { "${dlg.newident.scope.none}", 0, 0, "ident.scope arg:0" },
        { format_CStr("${dlg.newident.scope.domain}:\n%s", cstr_Rangecc(url.host)), 0, 0, "ident.scope arg:1" },
        { format_CStr("${dlg.newident.scope.dir}:\n%s", cstr_Rangecc(urlDirectory_String(docUrl))), 0, 0, "ident.scope arg:2" },
        { format_CStr("${dlg.newident.scope.page}:\n%s", cstr_Rangecc(url.path)), 0, 0, "ident.scope arg:3" },
        { NULL }
    };
    iWidget *dlg;
    if (isUsingPanelLayout_Mobile()) {
        dlg = makePanels_Mobile("ident", (iMenuItem[]){
            { "title id:ident.heading text:${heading.newident}" },
            { "label text:${dlg.newident.rsa.selfsign}" },
            { "dropdown id:ident.scope text:${dlg.newident.scope}", 0, 0,
              (const void *) scopeItems },
            { "input id:ident.until hint:hint.newident.date maxlen:10 text:${dlg.newident.until}" },
            //{ "padding" },
            //{ "toggle id:ident.temp text:${dlg.newident.temp}" },
            //{ "label text:${help.ident.temp}" },
            { "heading id:dlg.newident.commonname" },
            { "input id:ident.common noheading:1" },
            { "padding collapse:1" },
            { "input collapse:1 id:ident.email hint:hint.newident.optional text:${dlg.newident.email}" },
            { "input collapse:1 id:ident.userid hint:hint.newident.optional text:${dlg.newident.userid}" },
            { "input collapse:1 id:ident.domain hint:hint.newident.optional text:${dlg.newident.domain}" },
            { "input collapse:1 id:ident.org hint:hint.newident.optional text:${dlg.newident.org}" },
            { "input collapse:1 id:ident.country hint:hint.newident.optional text:${dlg.newident.country}" },
            { NULL }
        }, actions, iElemCount(actions));
    }
    else {
        dlg = makeSheet_Widget("ident");
        addDialogTitle_(dlg, "${heading.newident}", "ident.heading");
        iWidget *page = new_Widget();
        addChildFlags_Widget(
            dlg, iClob(new_LabelWidget("${dlg.newident.rsa.selfsign}", NULL)), frameless_WidgetFlag);
        /* TODO: Use makeTwoColumnWidget_? */
        addChild_Widget(dlg, iClob(page));
        setFlags_Widget(page, arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
        iWidget *headings = addChildFlags_Widget(
            page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
        iWidget *values = addChildFlags_Widget(
            page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
        setId_Widget(headings, "headings");
        setId_Widget(values, "values");
        iInputWidget *inputs[6];
        /* Where will the new identity be active on? */ {
            iWidget *head = addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.newident.scope}")));
            iWidget *val;
            setId_Widget(
                addChild_Widget(values,
                                val = iClob(makeMenuButton_LabelWidget(
                                    scopeItems[0].label, scopeItems, iElemCount(scopeItems)))),
                "ident.scope");
            head->sizeRef = val;
        }
        addDialogInputWithHeading_(headings,
                                   values,
                                   "${dlg.newident.until}",
                                   "ident.until",
                                   iClob(newHint_InputWidget(19, "${hint.newident.date}")));
        addDialogInputWithHeading_(headings,
                                   values,
                                   "${dlg.newident.commonname}",
                                   "ident.common",
                                   iClob(inputs[0] = new_InputWidget(0)));
        /* Temporary? */ {
            addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.newident.temp}")));
            iWidget *tmpGroup = new_Widget();
            setFlags_Widget(tmpGroup, arrangeSize_WidgetFlag | arrangeHorizontal_WidgetFlag, iTrue);
            addChild_Widget(tmpGroup, iClob(makeToggle_Widget("ident.temp")));
            setId_Widget(
                addChildFlags_Widget(tmpGroup,
                                     iClob(new_LabelWidget(uiTextCaution_ColorEscape warning_Icon
                                                           "  ${dlg.newident.notsaved}",
                                                           NULL)),
                                     hidden_WidgetFlag | frameless_WidgetFlag),
                "ident.temp.note");
            addChild_Widget(values, iClob(tmpGroup));
        }
        addChildFlags_Widget(headings, iClob(makePadding_Widget(gap_UI)), collapse_WidgetFlag | hidden_WidgetFlag);
        addChildFlags_Widget(values, iClob(makePadding_Widget(gap_UI)), collapse_WidgetFlag | hidden_WidgetFlag);
        addDialogInputWithHeadingAndFlags_(headings, values, "${dlg.newident.email}",   "ident.email",   iClob(inputs[1] = newHint_InputWidget(0, "${hint.newident.optional}")), collapse_WidgetFlag | hidden_WidgetFlag);
        addDialogInputWithHeadingAndFlags_(headings, values, "${dlg.newident.userid}",  "ident.userid",  iClob(inputs[2] = newHint_InputWidget(0, "${hint.newident.optional}")), collapse_WidgetFlag | hidden_WidgetFlag);
        addDialogInputWithHeadingAndFlags_(headings, values, "${dlg.newident.domain}",  "ident.domain",  iClob(inputs[3] = newHint_InputWidget(0, "${hint.newident.optional}")), collapse_WidgetFlag | hidden_WidgetFlag);
        addDialogInputWithHeadingAndFlags_(headings, values, "${dlg.newident.org}",     "ident.org",     iClob(inputs[4] = newHint_InputWidget(0, "${hint.newident.optional}")), collapse_WidgetFlag | hidden_WidgetFlag);
        addDialogInputWithHeadingAndFlags_(headings, values, "${dlg.newident.country}", "ident.country", iClob(inputs[5] = newHint_InputWidget(0, "${hint.newident.optional}")), collapse_WidgetFlag | hidden_WidgetFlag);
        arrange_Widget(dlg);
        for (size_t i = 0; i < iElemCount(inputs); ++i) {
            as_Widget(inputs[i])->rect.size.x = 100 * gap_UI - headings->rect.size.x;
        }
        addChild_Widget(dlg, iClob(makeDialogButtons_Widget(actions, iElemCount(actions))));
        addChild_Widget(get_Root()->widget, iClob(dlg));
    }
    setupSheetTransition_Mobile(dlg, incoming_TransitionFlag | dialogTransitionDir_Widget(dlg));
    return dlg;
}

static const iMenuItem languages[] = {
    { "${lang.auto}", 0, 0, "xlt.lang id:auto" },
    { "${lang.ar} - ar", 0, 0, "xlt.lang id:ar" },
    { "${lang.az} - az", 0, 0, "xlt.lang id:az" },
    { "${lang.ca} - ca", 0, 0, "xlt.lang id:ca" },
    { "${lang.cs} - cs", 0, 0, "xlt.lang id:cs" },
    { "${lang.da} - da", 0, 0, "xlt.lang id:da" },
    { "${lang.de} - de", 0, 0, "xlt.lang id:de" },
    { "${lang.el} - el", 0, 0, "xlt.lang id:el" },
    { "${lang.en} - en", 0, 0, "xlt.lang id:en" },
    { "${lang.eo} - eo", 0, 0, "xlt.lang id:eo" },
    { "${lang.es} - es", 0, 0, "xlt.lang id:es" },
    { "${lang.fa} - fa", 0, 0, "xlt.lang id:fa" },
    { "${lang.fi} - fi", 0, 0, "xlt.lang id:fi" },
    { "${lang.fr} - fr", 0, 0, "xlt.lang id:fr" },
    { "${lang.ga} - ga", 0, 0, "xlt.lang id:ga" },
    { "${lang.he} - he", 0, 0, "xlt.lang id:he" },
    { "${lang.hi} - hi", 0, 0, "xlt.lang id:hi" },
    { "${lang.hu} - hu", 0, 0, "xlt.lang id:hu" },
    { "${lang.id} - id", 0, 0, "xlt.lang id:id" },
    { "${lang.it} - it", 0, 0, "xlt.lang id:it" },
    { "${lang.ja} - ja", 0, 0, "xlt.lang id:ja" },
    { "${lang.ko} - ko", 0, 0, "xlt.lang id:ko" },
    { "${lang.nl} - nl", 0, 0, "xlt.lang id:nl" },
    { "${lang.pl} - pl", 0, 0, "xlt.lang id:pl" },
    { "${lang.pt} - pt", 0, 0, "xlt.lang id:pt" },
    { "${lang.ru} - ru", 0, 0, "xlt.lang id:ru" },
    { "${lang.sk} - sk", 0, 0, "xlt.lang id:sk" },
    { "${lang.sv} - sv", 0, 0, "xlt.lang id:sv" },
    { "${lang.tr} - tr", 0, 0, "xlt.lang id:tr" },
    { "${lang.uk} - uk", 0, 0, "xlt.lang id:uk" },
    { "${lang.zh} - zh", 0, 0, "xlt.lang id:zh" },
    { NULL }
};

static iBool translationHandler_(iWidget *dlg, const char *cmd) {
    iUnused(dlg);
    if (equal_Command(cmd, "xlt.lang")) {
        const iMenuItem *langItem = &languages[languageIndex_CStr(cstr_Command(cmd, "id"))];
        iWidget *widget = pointer_Command(cmd);
        iLabelWidget *drop;
        /* TODO: Add a utility to find the menu button of a dropdown. */
        /* This is a bit convoluted because finding the menu button of a particular menu item
           depends on whether the menu is native or not, and if it was shown in a popup window
           or in the same window. */
        if (flags_Widget(widget) & nativeMenu_WidgetFlag) {
            drop = (iLabelWidget *) parent_Widget(widget);
        }
        else if (isInstance_Object(widget, &Class_LabelWidget) &&
                 !cmp_String(command_LabelWidget((iLabelWidget *) widget), "menu.open")) {
            /* When a selection is made via a detached popup menu, the command is re-sent
               as if it was from the menu button itself, so it gets handled in the
               correct root.  */
            drop = (iLabelWidget *) widget;
        }
        else {
            iWidget *menu = parent_Widget(widget);
            iAssert(!cmp_String(id_Widget(menu), "menu"));
            drop = (iLabelWidget *) parent_Widget(menu);
        }
        iAssert(isInstance_Object(drop, &Class_LabelWidget));
        updateDropdownSelection_LabelWidget(drop, langItem->command);
        return iTrue;
    }
    return iFalse;
}

const char *languageId_String(const iString *menuItemLabel) {
    iForIndices(i, languages) {
        if (!languages[i].label) break;
        if (!cmp_String(menuItemLabel, translateCStr_Lang(languages[i].label))) {
            return cstr_Command(languages[i].command, "id");
        }
    }
    return "";
}

int languageIndex_CStr(const char *langId) {
    iForIndices(i, languages) {
        if (!languages[i].label) break;
        if (equal_Rangecc(range_Command(languages[i].command, "id"), langId)) {
            return (int) i;
        }
    }
    return -1;
}

iWidget *makeTranslation_Widget(iWidget *parent) {
    const iMenuItem actions[] = {
        { "${cancel}", SDLK_ESCAPE, 0, "translation.cancel" },
        { uiTextAction_ColorEscape "${dlg.translate}", SDLK_RETURN, 0, "translation.submit" }
    };
    iWidget *dlg;
    if (isUsingPanelLayout_Mobile()) {
        dlg = makePanelsParent_Mobile(parent, "xlt", (iMenuItem[]){
            { "title id:heading.translate" },
            { "dropdown id:xlt.from text:${dlg.translate.from}", 0, 0, (const void *) languages },
            { "dropdown id:xlt.to text:${dlg.translate.to}",     0, 0, (const void *) languages },
            { "padding" },
            { "toggle id:xlt.preskip text:${dlg.translate.pre}" },
            //{ "padding arg:3" },
            { NULL }
        }, actions, iElemCount(actions));
        setFlags_Widget(dlg, keepOnTop_WidgetFlag, iTrue);
        arrange_Widget(dlg);
    }
    else {
        dlg = makeSheet_Widget("xlt");
        setFlags_Widget(dlg, keepOnTop_WidgetFlag, iFalse);
        dlg->minSize.x = 70 * gap_UI;
        addDialogTitle_(dlg, "${heading.translate}", NULL);
        addChild_Widget(dlg, iClob(makePadding_Widget(lineHeight_Text(uiLabel_FontId))));
        iWidget *headings, *values;
        iWidget *page;
        addChild_Widget(dlg, iClob(page = makeTwoColumns_Widget(&headings, &values)));
        setId_Widget(page, "xlt.langs");
        iLabelWidget *fromLang, *toLang;
        const size_t numLangs = iElemCount(languages) - 1;
        const char *widestLabel = languages[findWidestLabel_MenuItem(languages, numLangs)].label;
        /* Source language. */ {
            addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.translate.from}")));
            setId_Widget(addChildFlags_Widget(values,
                                              iClob(fromLang = makeMenuButton_LabelWidget(
                                                        widestLabel, languages, numLangs)),
                                              alignLeft_WidgetFlag),
                         "xlt.from");
            setBackgroundColor_Widget(findChild_Widget(as_Widget(fromLang), "menu"),
                                      uiBackgroundMenu_ColorId);
        }
        /* Target language. */ {
            addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.translate.to}")));
            setId_Widget(addChildFlags_Widget(values,
                                              iClob(toLang = makeMenuButton_LabelWidget(
                                                        widestLabel, languages, numLangs)),
                                              alignLeft_WidgetFlag),
                         "xlt.to");
            setBackgroundColor_Widget(findChild_Widget(as_Widget(toLang), "menu"),
                                      uiBackgroundMenu_ColorId);
        }
        /* Options. */ {
            //addChild_Widget(dlg, iClob(page = makeTwoColumns_Widget(&headings, &values)));
            addDialogPadding_(headings, values);
            addDialogToggle_(headings, values, "${dlg.translate.pre}", "xlt.preskip");
        }
        addChild_Widget(dlg, iClob(makePadding_Widget(lineHeight_Text(uiLabel_FontId))));
        addChild_Widget(dlg, iClob(makeDialogButtons_Widget(actions, iElemCount(actions))));
        addChild_Widget(parent, iClob(dlg));
        arrange_Widget(dlg);
        arrange_Widget(dlg); /* TODO: Augh, another layout bug: two arranges required. */
    }
    /* Update choices. */
    setToggle_Widget(findChild_Widget(dlg, "xlt.preskip"), prefs_App()->translationIgnorePre);
    updateDropdownSelection_LabelWidget(findChild_Widget(dlg, "xlt.from"),
                                        languages[prefs_App()->langFrom].command);
    updateDropdownSelection_LabelWidget(findChild_Widget(dlg, "xlt.to"),
                                        languages[prefs_App()->langTo].command);
    setCommandHandler_Widget(dlg, translationHandler_);
    setupSheetTransition_Mobile(dlg, incoming_TransitionFlag | dialogTransitionDir_Widget(dlg));
    return dlg;
}

iWidget *makeGlyphFinder_Widget(void) {
    iString msg;
    iString command;
    init_String(&msg);
    initCStr_String(&command, "!font.find chars:");
    for (size_t i = 0; ; i++) {
        iChar ch = missing_Text(i);
        if (!ch) break;
        appendFormat_String(&msg, " U+%04X", ch);
        appendChar_String(&command, ch);
    }
    iArray items;
    init_Array(&items, sizeof(iMenuItem));
    if (!isEmpty_String(&msg)) {
        prependCStr_String(&msg, "${dlg.glyphfinder.missing} ");
        appendCStr_String(&msg, "\n\n${dlg.glyphfinder.help}");
        pushBackN_Array(
            &items,
            (iMenuItem[]){
                { "${menu.fonts}", 0, 0, "!open newtab:1 url:about:fonts" },
                { "${dlg.glyphfinder.disable}", 0, 0, "prefs.font.warnmissing.changed arg:0" },
                { "---" },
                { uiTextAction_ColorEscape magnifyingGlass_Icon " ${dlg.glyphfinder.search}",
                  0,
                  0,
                  cstr_String(&command) },
                { "${close}", 0, 0, "cancel" } },
            5);
    }
    else {
        setCStr_String(&msg, "${dlg.glyphfinder.help.empty}");
        pushBackN_Array(&items,
                        (iMenuItem[]){ { "${menu.reload}", 0, 0, "navigate.reload" },
                                       { "${close}", 0, 0, "cancel" } },
                        2);
    }
    iWidget *dlg = makeQuestion_Widget("${heading.glyphfinder}", cstr_String(&msg),
                                       constData_Array(&items),
                                       size_Array(&items));
    arrange_Widget(dlg);
    deinit_Array(&items);
    deinit_String(&command);
    deinit_String(&msg);
    return dlg;
}

static enum iImportMethod checkImportMethod_(const iWidget *dlg, const char *id) {
    return isSelected_Widget(findChild_Widget(dlg, format_CStr("%s.0", id))) ? none_ImportMethod
           : isSelected_Widget(findChild_Widget(dlg, format_CStr("%s.1", id)))
               ? ifMissing_ImportMethod
               : all_ImportMethod;
}

static iBool handleUserDataImporterCommands_(iWidget *dlg, const char *cmd) {
    if (equalWidget_Command(cmd, dlg, "importer.cancel") ||
        equalWidget_Command(cmd, dlg, "importer.accept")) {
        if (equal_Command(cmd, "importer.accept")) {
            /* Compose the final import command. */
            enum iImportMethod bookmarkMethod = checkImportMethod_(dlg, "importer.bookmark");
            enum iImportMethod identMethod =
                isSelected_Widget(findChild_Widget(dlg, "importer.idents")) ? ifMissing_ImportMethod
                                                                            : none_ImportMethod;
            enum iImportMethod trustedMethod  = checkImportMethod_(dlg, "importer.trusted");
            enum iImportMethod sitespecMethod = checkImportMethod_(dlg, "importer.sitespec");
            enum iImportMethod visitedMethod =
                isSelected_Widget(findChild_Widget(dlg, "importer.history")) ? all_ImportMethod
                                                                             : none_ImportMethod;
            postCommandf_App(
                "import arg:1 bookmarks:%d idents:%d trusted:%d visited:%d sitespec:%d path:%s",
                bookmarkMethod,
                identMethod,
                trustedMethod,
                visitedMethod,
                sitespecMethod,
                suffixPtr_Command(cmd, "path"));
        }
        setupSheetTransition_Mobile(dlg, dialogTransitionDir_Widget(dlg));
        destroy_Widget(dlg);
        return iTrue;
    }
    else if (equalWidget_Command(cmd, dlg, "importer.selectall")) {
        postCommand_Widget(findChild_Widget(dlg, "importer.bookmark.1"), "trigger");
        postCommand_Widget(findChild_Widget(dlg, "importer.trusted.1"), "trigger");
        postCommand_Widget(findChild_Widget(dlg, "importer.sitespec.1"), "trigger");
        setToggle_Widget(findChild_Widget(dlg, "importer.history"), iTrue);
        setToggle_Widget(findChild_Widget(dlg, "importer.idents"), iTrue);
        return iTrue;
    }
    return iFalse;
}

iWidget *makeUserDataImporter_Dialog(const iString *archivePath) {
    iWidget *dlg;
    const iMenuItem actions[] = {
        { "${menu.selectall}", 0, 0, "importer.selectall" },
        { "---" },
        { "${cancel}", SDLK_ESCAPE, 0, "importer.cancel" },
        { uiTextAction_ColorEscape "${import.userdata}",
          SDLK_RETURN, KMOD_ACCEPT,
          format_CStr("importer.accept path:%s", cstr_String(archivePath)) },
    };
    if (isUsingPanelLayout_Mobile()) {
        const iMenuItem bookmarkItems[] = {
            { "button id:importer.bookmark.0 label:dlg.userdata.no", 0, 0, "." },
            { "button id:importer.bookmark.1 label:dlg.userdata.missing", 0, 0, "." },
            { "button id:importer.bookmark.2 label:dlg.userdata.alldup", 0, 0, "." },
            { NULL }
        };
        const iMenuItem sitespecItems[] = {
            { "button id:importer.sitespec.0 label:dlg.userdata.no", 0, 0, "." },
            { "button id:importer.sitespec.1 label:dlg.userdata.missing", 0, 0, "." },
            { "button id:importer.sitespec.2 label:dlg.userdata.all", 0, 0, "." },
            { NULL }
        };
        const iMenuItem trustedItems[] = {
            { "button id:importer.trusted.0 label:dlg.userdata.no", 0, 0, "." },
            { "button id:importer.trusted.1 label:dlg.userdata.missing", 0, 0, "." },
            { "button id:importer.trusted.2 label:dlg.userdata.all", 0, 0, "." },
            { NULL }
        };
        dlg = makePanels_Mobile(
            "importer",
            (iMenuItem[]){ { "title id:heading.import.userdata" },
                           { "toggle id:importer.history text:${import.userdata.history}" },
                           { "toggle id:importer.idents text:${import.userdata.idents}" },
                           { "radio id:import.userdata.bookmarks", 0, 0, (const void *) bookmarkItems },
                           { "radio id:import.userdata.sitespec", 0, 0, (const void *) sitespecItems },
                           { "radio id:import.userdata.trusted", 0, 0, (const void *) trustedItems },
                           { NULL } },
            actions,
            iElemCount(actions));
    }
    else {
        dlg = makeSheet_Widget("importer");
        addDialogTitle_(dlg, "${heading.import.userdata}", NULL);
        iWidget *headings, *values;
        addChild_Widget(dlg, iClob(makeTwoColumns_Widget(&headings, &values)));
        addDialogToggle_(headings, values, "${import.userdata.history}", "importer.history");
        addDialogToggle_(headings, values, "${import.userdata.idents}", "importer.idents");
        /* Bookmarks. */
        addChild_Widget(headings, iClob(makeHeading_Widget("${import.userdata.bookmarks}")));
        iWidget *radio = new_Widget(); {
            addRadioButton_(radio, "importer.bookmark.0", "${dlg.userdata.no}", ".");
            addRadioButton_(radio, "importer.bookmark.1", "${dlg.userdata.missing}", ".");
            addRadioButton_(radio, "importer.bookmark.2", "${dlg.userdata.alldup}", ".");
        }
        addChildFlags_Widget(values, iClob(radio), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        /* Site-specific. */
        addChild_Widget(headings, iClob(makeHeading_Widget("${import.userdata.sitespec}")));
        radio = new_Widget(); {
            addRadioButton_(radio, "importer.sitespec.0", "${dlg.userdata.no}", ".");
            addRadioButton_(radio, "importer.sitespec.1", "${dlg.userdata.missing}", ".");
            addRadioButton_(radio, "importer.sitespec.2", "${dlg.userdata.all}", ".");
        }
        addChildFlags_Widget(values, iClob(radio), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        /* Trusted certs. */
        addChild_Widget(headings, iClob(makeHeading_Widget("${import.userdata.trusted}")));
        radio = new_Widget(); {
            addRadioButton_(radio, "importer.trusted.0", "${dlg.userdata.no}", ".");
            addRadioButton_(radio, "importer.trusted.1", "${dlg.userdata.missing}", ".");
            addRadioButton_(radio, "importer.trusted.2", "${dlg.userdata.all}", ".");
        }
        addChildFlags_Widget(values, iClob(radio), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        addDialogPadding_(headings, values);
        addChild_Widget(dlg, iClob(makeDialogButtons_Widget(actions, iElemCount(actions))));
        addChild_Widget(dlg->root->widget, iClob(dlg));
        arrange_Widget(dlg);
        arrange_Widget(dlg);
    }
    /* Initialize. */
    setToggle_Widget(findChild_Widget(dlg, "importer.bookmark.0"), iTrue);
    setToggle_Widget(findChild_Widget(dlg, "importer.idents.0"), iTrue);
    setToggle_Widget(findChild_Widget(dlg, "importer.sitespec.0"), iTrue);
    setToggle_Widget(findChild_Widget(dlg, "importer.trusted.0"), iTrue);
    setCommandHandler_Widget(dlg, handleUserDataImporterCommands_);
    setupSheetTransition_Mobile(dlg, incoming_TransitionFlag | dialogTransitionDir_Widget(dlg));
    return dlg;
}

/*----------------------------------------------------------------------------------------------*/

void init_PerfTimer(iPerfTimer *d) {
    d->ticks = SDL_GetPerformanceCounter();
}

uint64_t elapsedMicroseconds_PerfTimer(const iPerfTimer *d) {
    const uint64_t now = SDL_GetPerformanceCounter();
    return (uint64_t) (((double) (now - d->ticks)) / (double) SDL_GetPerformanceFrequency() * 1.0e6);
}

void print_PerfTimer(const iPerfTimer *d, const char *msg) {
    printf("[%s] %llu \u03bcs\n", msg, (unsigned long long) elapsedMicroseconds_PerfTimer(d));
}
