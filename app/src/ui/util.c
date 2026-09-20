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
#include "command.h"
#include "defs.h"
#include "labelwidget.h"
#include "touch.h"
#include "widget.h"
#include "window.h"

#include <the_Foundation/math.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_version.h>

iBool isCommand_SDLEvent(const SDL_Event *d) {
    return d->type == SDL_EVENT_USER && d->user.code == command_UserEventCode;
}

iBool isCommand_UserEvent(const SDL_Event *d, const char *cmd) {
    return d->type == SDL_EVENT_USER && d->user.code == command_UserEventCode &&
           equal_Command(d->user.data1, cmd);
}

const char *command_UserEvent(const SDL_Event *d) {
    if (d->type == SDL_EVENT_USER && d->user.code == command_UserEventCode) {
        return d->user.data1;
    }
    return "";
}

void emulateMouseClickPos_Widget(const iWidget *d, int button, iInt2 clickPos) {
    iMainWindow *wnd = get_MainWindow();
    divfv_I2(&clickPos, wnd->base.pixelRatio); /* ratio is multiplied when processing events */
    SDL_MouseButtonEvent ev = { .type      = SDL_EVENT_MOUSE_BUTTON_DOWN,
                                .timestamp = SDL_GetTicksNS(),
                                .windowID  = id_Window(as_Window(wnd)),
                                .which     = 1024,
                                .button    = button,
                                .down      = true,
                                .clicks    = 1,
                                .x         = clickPos.x,
                                .y         = clickPos.y };
    SDL_PushEvent((SDL_Event *) &ev);
    ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
    ev.down = false;
    ev.timestamp++;
    SDL_PushEvent((SDL_Event *) &ev);
}

void emulateMouseClick_Widget(const iWidget *d, int button) {
    const iInt2 pos = max_I2(mid_Rect(bounds_Widget(d)),
                             sub_I2(bottomRight_Rect(bounds_Widget(d)), muli_I2(gap2_UI, 2)));
    return emulateMouseClickPos_Widget(d, button, pos);
}

iInt2 coord_MouseWheelEvent(const SDL_MouseWheelEvent *ev) {
    iWindow *win = get_Window(); /* may not be the focus window */
#if !defined (iPlatformTerminal)
    if (isDesktop_Platform()) {
# if SDL_VERSION_ATLEAST(2, 26, 0) && !defined (iPlatformApple)
        return coord_Window(win, ev->mouse_x, ev->mouse_y);
# else
        /* We need to figure out where the mouse is in relation to the currently active window.
           It may be outside the actual focus window. */
        float mouseX, mouseY;
        iInt2 winPos;
        SDL_GetGlobalMouseState(&mouseX, &mouseY);
        SDL_GetWindowPosition(win->win, &winPos.x, &winPos.y);
        return coord_Window(win, (int) mouseX - winPos.x, (int) mouseY - winPos.y);
# endif
    }
#endif
    return mouseCoord_Window(win, ev->which);
}

iInt2 mouseCoord_SDLEvent(const SDL_Event *ev) {
    switch (ev->type) {
        case SDL_EVENT_MOUSE_MOTION:
            return init_I2(ev->motion.x, ev->motion.y);
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            return init_I2(ev->button.x, ev->button.y);
        case SDL_EVENT_MOUSE_WHEEL:
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
        if (kmods & SDL_KMOD_CTRL) {
            appendCStr_String(str, "^");
        }
        if (kmods & (SDL_KMOD_ALT | SDL_KMOD_GUI)) {
            appendCStr_String(str, "M-");
        }
        if (kmods & SDL_KMOD_SHIFT) {
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
        if (kmods & SDL_KMOD_CTRL) {
            appendChar_String(str, 0x2303);
        }
        if (kmods & SDL_KMOD_ALT) {
            appendChar_String(str, 0x2325);
        }
        if (kmods & SDL_KMOD_SHIFT) {
            appendCStr_String(str, shift_Icon);
        }
        if (kmods & SDL_KMOD_GUI) {
            appendChar_String(str, 0x2318);
        }
    }
    else {
        if (kmods & SDL_KMOD_CTRL) {
            appendCStr_String(str, "Ctrl+");
        }
        if (kmods & SDL_KMOD_ALT) {
            appendCStr_String(str, "Alt+");
        }
        if (kmods & SDL_KMOD_SHIFT) {
            appendCStr_String(str, shift_Icon "+");
        }
        if (kmods & SDL_KMOD_GUI) {
            appendCStr_String(str, "Meta+");
        }
    }
    if (kmods & SDL_KMOD_CAPS) {
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
    kmods &= (SDL_KMOD_SHIFT | SDL_KMOD_ALT | SDL_KMOD_CTRL | SDL_KMOD_GUI | SDL_KMOD_CAPS);
    /* Don't treat left/right modifiers differently. */
    if (kmods & SDL_KMOD_SHIFT) kmods |= SDL_KMOD_SHIFT;
    if (kmods & SDL_KMOD_CTRL)  kmods |= SDL_KMOD_CTRL;
    if (kmods & SDL_KMOD_GUI)   kmods |= SDL_KMOD_GUI;
    if (!isTextInputActive_App()) {
        if (kmods & SDL_KMOD_ALT) kmods |= SDL_KMOD_ALT;
    }
    return kmods;
}

int keyMod_ReturnKeyFlag(int flag) {
    flag &= mask_ReturnKeyFlag;
    const int kmods[4] = { 0, SDL_KMOD_SHIFT, SDL_KMOD_CTRL, SDL_KMOD_GUI };
    if (flag < 0 || flag >= iElemCount(kmods)) return 0;
    return kmods[flag];
}

int openTabMode_Sym(int kmods) {
    const int km = keyMods_Sym(kmods);
    return (km == SDL_KMOD_SHIFT ? otherRoot_OpenTabFlag : 0) | /* open to the side */
           (((km & KMOD_PRIMARY) && (km & SDL_KMOD_SHIFT)) ? new_OpenTabFlag :
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
        const uint32_t span  = iMinf(fabsf(delta) / unitsPerSecond * 1000.0f, 2.0e9f);
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
    initButtons_Click(d, widget, button ? SDL_BUTTON_MASK(button) : 0);
}

void initButtons_Click(iClick *d, iAnyObject *widget, int buttonMask) {
    d->isActive    = iFalse;
    d->isDragging  = iFalse;
    d->buttons     = buttonMask;
    d->clickButton = 0;
    d->bounds      = as_Widget(widget);
    d->minHeight   = 0;
    d->minDrag     = gap_UI * 2 / 3; /* require definite movement of the cursor */
    d->startPos    = zero_I2();
    d->pos         = zero_I2();
}

iBool contains_Click(const iClick *d, iInt2 coord) {
    if (!d->bounds) {
        return iTrue;
    }
    if (d->minHeight) {
        iRect rect = bounds_Widget(d->bounds);
        rect.size.y = iMax(d->minHeight, rect.size.y);
        return contains_Rect(rect, coord);
    }
    return contains_Widget(d->bounds, coord);
}

enum iClickResult processEvent_Click(iClick *d, const SDL_Event *event) {
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        if (!d->isActive) {
            return none_ClickResult;
        }
        const iInt2 pos = init_I2(event->motion.x, event->motion.y);
        if (!d->isDragging && manhattan_I2(pos, d->pos) > d->minDrag) {
            d->isDragging = iTrue;
        }
        if (d->isDragging) {
            d->pos = pos;
            return drag_ClickResult;
        }
    }
    if (event->type != SDL_EVENT_MOUSE_BUTTON_DOWN && event->type != SDL_EVENT_MOUSE_BUTTON_UP) {
        return none_ClickResult;
    }
    const SDL_MouseButtonEvent *mb = &event->button;
    if (!(SDL_BUTTON_MASK(mb->button) & d->buttons)) {
        return none_ClickResult;
    }
    const iInt2 pos = init_I2(mb->x, mb->y);
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && (!d->isActive || d->clickButton == mb->button)) {
        d->count = mb->clicks;
    }
    if (!d->isActive) {
        if (mb->down == true) {
            if (contains_Click(d, pos)) {
                d->isActive = iTrue;
                d->isDragging = iFalse;
                d->clickButton = mb->button;
                d->startPos = d->pos = pos;
                if (d->bounds) {
                    setMouseGrab_Widget(d->bounds);
                }
                return started_ClickResult;
            }
        }
    }
    else { /* Active. */
        if (mb->down == false && mb->button == d->clickButton) {
            enum iClickResult result = contains_Click(d, pos)
                                           ? finished_ClickResult
                                           : aborted_ClickResult;
            d->isActive = iFalse;
            d->pos = pos;
            if (d->bounds) {
                setMouseGrab_Widget(NULL);
            }
            return result;
        }
    }
    return none_ClickResult;
}

void cancel_Click(iClick *d) {
    if (d->isActive) {
        d->isActive = iFalse;
        if (d->bounds) {
            setMouseGrab_Widget(NULL);
        }
    }
}

iBool isMoved_Click(const iClick *d) {
    return dist_I2(d->startPos, d->pos) > d->minDrag;
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
    d->overscroll =
        (deviceType_App() != desktop_AppDeviceType && !isHandheld_Platform() ? 100 * gap_UI : 0);
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
    if (ev->type == SDL_EVENT_USER && ev->user.code == widgetTouchEnds_UserEventCode) {
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
    iLabelWidget *action = newKeyMods_LabelWidget(NULL, key, kmods, command);
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
                           format_CStr("!%s.changed arg:%d",
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

/*-----------------------------------------------------------------------------------------------*/

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

/*-----------------------------------------------------------------------------------------------*/

#if defined(iPlatformApple)

const char *systemImageName_Apple(iChar ch) {
    /* clang-format off */
    static const struct { iChar c; const char *name; } sysImages[] = {
        { 0x10117,  "list.bullet" },
        { 0x1f310,  "globe" },
        { 0x1f3e0,  "house.fill" },
        { 0x1f464,  "person.fill" },
        { 0x1f4c1,  "folder" },
        { 0x1f4e4,  "square.and.arrow.up" },
        { 0x1f503,  "arrow.clockwise" },
        { 0x1f50d,  "magnifyingglass" },
        { 0x1f516,  "bookmark" },
        { 0x1f553,  "clock" },
        { 0x1f56e,  "book" },
        { 0x1f871,  "arrow.up" },
        { 0x22f0,   "list.bullet.indent" },
        { 0x23f2,   "clock.arrow.2.circlepath" },
        { 0x2398,   "doc.on.doc.fill" },
        { 0x25e7,   "rectangle.lefthalf.filled" },
        { 0x25e8,   "rectangle.righthalf.filled" },
        { 0x2605,   "star" },
        { 0x2606,   "star.fill" },
        { 0x2699,   "gear" },
        { 0x270e,   "pencil" },
        { 0x2750,   "square.on.square" },
        { 0x2795,   "plus" },
        { 0x2912,   "arrow.up.to.line" },
        { 0x2913,   "arrow.down.to.line" },
        { 0x2a2f,   "xmark" },
        { 0x2b71,   "arrow.up.to.line" },
        { 0x2ba5,   "paperplane" },
        { 0x2ba7,   "square.and.arrow.down" },
    };
    /* clang-format on */
    iForIndices(i, sysImages) {
        if (sysImages[i].c == ch) {
            return sysImages[i].name;
        }
    }
    return NULL;
}

#endif
