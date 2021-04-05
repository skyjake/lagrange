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
#include "bookmarks.h"
#include "color.h"
#include "command.h"
#include "defs.h"
#include "documentwidget.h"
#include "gmutil.h"
#include "feeds.h"
#include "labelwidget.h"
#include "inputwidget.h"
#include "bindingswidget.h"
#include "keys.h"
#include "widget.h"
#include "text.h"
#include "window.h"

#if defined (iPlatformAppleMobile)
#   include "../ios.h"
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

static void removePlus_(iString *str) {
    if (endsWith_String(str, "+")) {
        removeEnd_String(str, 1);
        appendCStr_String(str, " ");
    }
}

void toString_Sym(int key, int kmods, iString *str) {
#if defined (iPlatformApple)
    if (kmods & KMOD_CTRL) {
        appendChar_String(str, 0x2303);
    }
    if (kmods & KMOD_ALT) {
        appendChar_String(str, 0x2325);
    }
    if (kmods & KMOD_SHIFT) {
        appendChar_String(str, 0x21e7);
    }
    if (kmods & KMOD_GUI) {
        appendChar_String(str, 0x2318);
    }
#else
    if (kmods & KMOD_CTRL) {
        appendCStr_String(str, "Ctrl+");
    }
    if (kmods & KMOD_ALT) {
        appendCStr_String(str, "Alt+");
    }
    if (kmods & KMOD_SHIFT) {
        appendCStr_String(str, "Shift+");
    }
    if (kmods & KMOD_GUI) {
        appendCStr_String(str, "Meta+");
    }
#endif
    if (kmods & KMOD_CAPS) {
        appendCStr_String(str, "Caps+");
    }
    if (key == 0x20) {
        appendCStr_String(str, "Space");
    }
    else if (key == SDLK_ESCAPE) {
        appendCStr_String(str, "Esc");
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
        appendChar_String(str, 0x21a9); /* Leftwards arrow with a hook */
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
    if (key == SDLK_RALT) key = SDLK_LALT;
    if (key == SDLK_RGUI) key = SDLK_LGUI;
    return key;
}

int keyMods_Sym(int kmods) {
    kmods &= (KMOD_SHIFT | KMOD_ALT | KMOD_CTRL | KMOD_GUI | KMOD_CAPS);
    /* Don't treat left/right modifiers differently. */
    if (kmods & KMOD_SHIFT) kmods |= KMOD_SHIFT;
    if (kmods & KMOD_ALT)   kmods |= KMOD_ALT;
    if (kmods & KMOD_CTRL)  kmods |= KMOD_CTRL;
    if (kmods & KMOD_GUI)   kmods |= KMOD_GUI;
    return kmods;
}

int openTabMode_Sym(int kmods) {
    const int km = keyMods_Sym(kmods);
    return ((km & KMOD_PRIMARY) && (km & KMOD_SHIFT)) ? 1 : (km & KMOD_PRIMARY) ? 2 : 0;
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
        if (mode & bothStartAndEnd_RangeExtension) {
            d->start = moveBackward_(d->start, bounds, mode);
            d->end   = moveForward_(d->end, bounds, mode);
        }
        else {
            d->end = moveForward_(d->end, bounds, mode);
        }
    }
    else {
        if (mode & bothStartAndEnd_RangeExtension) {
            d->start = moveForward_(d->start, bounds, mode);
            d->end   = moveBackward_(d->end, bounds, mode);
        }
        else {
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
    const iBool isSoft = (d->flags & softer_AnimFlag) != 0;
    if ((d->flags & easeBoth_AnimFlag) == easeBoth_AnimFlag) {
        t = easeBoth_(t);
        if (isSoft) t = easeBoth_(t);
    }
    else if (d->flags & easeIn_AnimFlag) {
        t = easeIn_(t);
        if (isSoft) t = easeIn_(t);
    }
    else if (d->flags & easeOut_AnimFlag) {
        t = easeOut_(t);
        if (isSoft) t = easeOut_(t);
    }
    return d->from * (1.0f - t) + d->to * t;
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
}

void setValueSpeed_Anim(iAnim *d, float to, float unitsPerSecond) {
    if (iAbs(d->to - to) > 0.0001f) {
        const uint32_t now   = SDL_GetTicks();
        const float    from  = valueAt_Anim_(d, now);
        const float    delta = to - from;
        const uint32_t span  = (fabsf(delta) / unitsPerSecond) * 1000;
        d->from              = from;
        d->to                = to;
        d->when              = now;
        d->due               = d->when + span;
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
    d->to   = to;
    d->when = now;
    d->due  = now + span;
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
    d->isActive = iFalse;
    d->button   = button;
    d->bounds   = as_Widget(widget);
    d->startPos = zero_I2();
    d->pos      = zero_I2();
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
    if (mb->button != d->button) {
        return none_ClickResult;
    }
    const iInt2 pos = init_I2(mb->x, mb->y);
    if (event->type == SDL_MOUSEBUTTONDOWN) {
        d->count = mb->clicks;
    }
    if (!d->isActive) {
        if (mb->state == SDL_PRESSED) {
            if (contains_Widget(d->bounds, pos)) {
                d->isActive = iTrue;
                d->startPos = d->pos = pos;
                setMouseGrab_Widget(d->bounds);
                return started_ClickResult;
            }
        }
    }
    else { /* Active. */
        if (mb->state == SDL_RELEASED) {
            enum iClickResult result = contains_Widget(d->bounds, pos)
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

/*-----------------------------------------------------------------------------------------------*/

static iBool isCommandIgnoredByMenus_(const char *cmd) {
    /* TODO: Perhaps a common way of indicating which commands are notifications and should not
       be reacted to by menus? */
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
           equal_Command(cmd, "scrollbar.fade") ||
           equal_Command(cmd, "visited.changed") ||
           (deviceType_App() == desktop_AppDeviceType && equal_Command(cmd, "window.resized")) ||
           equal_Command(cmd, "widget.overflow") ||
           equal_Command(cmd, "window.reload.update") ||
           equal_Command(cmd, "window.mouse.exited") ||
           equal_Command(cmd, "window.mouse.entered") ||
           (equal_Command(cmd, "mouse.clicked") && !arg_Command(cmd)); /* button released */
}

static iLabelWidget *parentMenuButton_(const iWidget *menu) {
    if (isInstance_Object(menu->parent, &Class_LabelWidget)) {
        iLabelWidget *button = (iLabelWidget *) menu->parent;
        if (!cmp_String(command_LabelWidget(button), "menu.open")) {
            return button;
        }
    }
    return NULL;
}

static iBool menuHandler_(iWidget *menu, const char *cmd) {
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
            if (hitChild_Widget(get_Window()->root, coord_Command(cmd)) == parentMenuButton_(menu)) {
                return iFalse;
            }
            /* Dismiss open menus when clicking outside them. */
            closeMenu_Widget(menu);
            return iTrue;
        }
        if (!isCommandIgnoredByMenus_(cmd)) {
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
    setFlags_Widget(sep, hover_WidgetFlag | fixedHeight_WidgetFlag, iTrue);
    return sep;
}

iWidget *makeMenu_Widget(iWidget *parent, const iMenuItem *items, size_t n) {
    iWidget *menu = new_Widget();
    setBackgroundColor_Widget(menu, uiBackgroundMenu_ColorId);
    if (deviceType_App() != desktop_AppDeviceType) {
        setPadding1_Widget(menu, 2 * gap_UI);
    }
    else {
        setPadding1_Widget(menu, gap_UI / 2);
    }
    const iBool isPortraitPhone = (deviceType_App() == phone_AppDeviceType && isPortrait_App());
    int64_t itemFlags = (deviceType_App() != desktop_AppDeviceType ? 0 : 0) |
                        (isPortraitPhone ? extraPadding_WidgetFlag : 0);
    setFlags_Widget(menu,
                    keepOnTop_WidgetFlag | collapse_WidgetFlag | hidden_WidgetFlag |
                        arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag |
                        resizeChildrenToWidestChild_WidgetFlag | overflowScrollable_WidgetFlag |
                        (isPortraitPhone ? drawBackgroundToVerticalSafeArea_WidgetFlag : 0),
                    iTrue);
    if (!isPortraitPhone) {
        setFrameColor_Widget(menu, uiSeparator_ColorId);
    }
    iBool haveIcons = iFalse;
    for (size_t i = 0; i < n; ++i) {
        const iMenuItem *item = &items[i];
        if (equal_CStr(item->label, "---")) {
            addChild_Widget(menu, iClob(makeMenuSeparator_()));
        }
        else {
            iLabelWidget *label = addChildFlags_Widget(
                menu,
                iClob(newKeyMods_LabelWidget(item->label, item->key, item->kmods, item->command)),
                noBackground_WidgetFlag | frameless_WidgetFlag | alignLeft_WidgetFlag |
                                                       drawKey_WidgetFlag | itemFlags);
            haveIcons |= checkIcon_LabelWidget(label);
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
                if (icon_LabelWidget(label) == 0) {
                    setIcon_LabelWidget(label, ' ');
                }
            }
        }
    }
    addChild_Widget(parent, menu);
    iRelease(menu); /* owned by parent now */
    setCommandHandler_Widget(menu, menuHandler_);
    iWidget *cancel = addAction_Widget(menu, SDLK_ESCAPE, 0, "cancel");
    setId_Widget(cancel, "menu.cancel");
    setFlags_Widget(cancel, disabled_WidgetFlag, iTrue);
    return menu;
}

void openMenu_Widget(iWidget *d, iInt2 coord) {
    openMenuFlags_Widget(d, coord, iTrue);
}

void openMenuFlags_Widget(iWidget *d, iInt2 coord, iBool postCommands) {
    const iInt2 rootSize        = rootSize_Window(get_Window());
    const iBool isPortraitPhone = (deviceType_App() == phone_AppDeviceType && isPortrait_App());
    const iBool isSlidePanel    = (flags_Widget(d) & horizontalOffset_WidgetFlag) != 0;
    if (postCommands) {
        postCommand_App("cancel"); /* dismiss any other menus */
    }
    /* Menu closes when commands are emitted, so handle any pending ones beforehand. */
    processEvents_App(postedEventsOnly_AppEventMode);
    setFlags_Widget(d, hidden_WidgetFlag, iFalse);
    setFlags_Widget(d, commandOnMouseMiss_WidgetFlag, iTrue);
    raise_Widget(d);
    setFlags_Widget(findChild_Widget(d, "menu.cancel"), disabled_WidgetFlag, iFalse);
    if (isPortraitPhone) {
        setFlags_Widget(d, arrangeWidth_WidgetFlag | resizeChildrenToWidestChild_WidgetFlag, iFalse);
        setFlags_Widget(d, resizeWidthOfChildren_WidgetFlag | drawBackgroundToBottom_WidgetFlag, iTrue);
        if (!isSlidePanel) {
            setFlags_Widget(d, borderTop_WidgetFlag, iTrue);
        }
        d->rect.size.x = rootSize_Window(get_Window()).x;
    }
    /* Update item fonts. */ {
        iForEach(ObjectList, i, children_Widget(d)) {
            if (isInstance_Object(i.object, &Class_LabelWidget)) {
                iLabelWidget *label = i.object;
                const iBool isCaution = startsWith_String(text_LabelWidget(label), uiTextCaution_ColorEscape);
                if (deviceType_App() == desktop_AppDeviceType) {
                    setFont_LabelWidget(label, isCaution ? uiLabelBold_FontId : uiLabel_FontId);
                }
                else if (isPortraitPhone) {
                    if (!isSlidePanel) {
                        setFont_LabelWidget(label, isCaution ? defaultBigBold_FontId : defaultBig_FontId);
                    }
                }
                else {
                    setFont_LabelWidget(label, isCaution ? uiContentBold_FontId : uiContent_FontId);
                }
            }
        }
    }
    arrange_Widget(d);
    if (isPortraitPhone) {
        if (isSlidePanel) {
            d->rect.pos = zero_I2(); //neg_I2(bounds_Widget(parent_Widget(d)).pos);
        }
        else {
            d->rect.pos = init_I2(0, rootSize.y);
        }
    }
    else {
        d->rect.pos = coord;
    }
    /* Ensure the full menu is visible. */
    const iRect bounds       = bounds_Widget(d);
    int         leftExcess   = -left_Rect(bounds);
    int         rightExcess  = right_Rect(bounds) - rootSize.x;
    int         topExcess    = -top_Rect(bounds);
    int         bottomExcess = bottom_Rect(bounds) - rootSize.y;
#if defined (iPlatformAppleMobile)
    /* Reserve space for the system status bar. */ {
        float l, t, r, b;
        safeAreaInsets_iOS(&l, &t, &r, &b);
        topExcess    += t;
        bottomExcess += iMax(b, get_Window()->keyboardHeight);
        leftExcess   += l;
        rightExcess  += r;
    }
#endif
    if (bottomExcess > 0 && (!isPortraitPhone || !isSlidePanel)) {
        d->rect.pos.y -= bottomExcess;
    }
    if (topExcess > 0) {
        d->rect.pos.y += topExcess;
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
    if (isPortraitPhone) {
        setVisualOffset_Widget(d, isSlidePanel ? width_Widget(d) : height_Widget(d), 0, 0);
        setVisualOffset_Widget(d, 0, 330, easeOut_AnimFlag | softer_AnimFlag);
    }
}

void closeMenu_Widget(iWidget *d) {
    setFlags_Widget(d, hidden_WidgetFlag, iTrue);
    setFlags_Widget(findChild_Widget(d, "menu.cancel"), disabled_WidgetFlag, iTrue);
    postRefresh_App();
    postCommand_Widget(d, "menu.closed");
    if (isPortrait_App() && deviceType_App() == phone_AppDeviceType) {
        const iBool wasDragged = iAbs(value_Anim(&d->visualOffset) - 0) > 1;
        setVisualOffset_Widget(d,
                               flags_Widget(d) & horizontalOffset_WidgetFlag ?
                                 width_Widget(d) : height_Widget(d),
                               wasDragged ? 100 : 200,
                               wasDragged ? 0 : easeIn_AnimFlag | softer_AnimFlag);
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

int checkContextMenu_Widget(iWidget *menu, const SDL_Event *ev) {
    if (menu && ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_RIGHT) {
        if (isVisible_Widget(menu)) {
            closeMenu_Widget(menu);
            return 0x1;
        }
        const iInt2 mousePos = init_I2(ev->button.x, ev->button.y);
        if (contains_Widget(menu->parent, mousePos)) {
            openMenu_Widget(menu, localCoord_Widget(menu->parent, mousePos));
            return 0x2;
        }
    }
    return 0;
}

iLabelWidget *makeMenuButton_LabelWidget(const char *label, const iMenuItem *items, size_t n) {
    iLabelWidget *button = new_LabelWidget(label, "menu.open");
    iWidget *menu = makeMenu_Widget(as_Widget(button), items, n);
    setId_Widget(menu, "menu");
    return button;
}

/*-----------------------------------------------------------------------------------------------*/

static iBool isTabPage_Widget_(const iWidget *tabs, const iWidget *page) {
    return page->parent == findChild_Widget(tabs, "tabs.pages");
}

static iBool tabSwitcher_(iWidget *tabs, const char *cmd) {
    if (equal_Command(cmd, "tabs.switch")) {
        iWidget *target = pointerLabel_Command(cmd, "page");
        if (!target) {
            target = findChild_Widget(tabs, cstr_Rangecc(range_Command(cmd, "id")));
        }
        if (!target) return iFalse;
        if (flags_Widget(target) & focusable_WidgetFlag) {
            setFocus_Widget(target);
        }
        if (isTabPage_Widget_(tabs, target)) {
            showTabPage_Widget(tabs, target);
            return iTrue;
        }
        else if (hasParent_Widget(target, tabs)) {
            /* Some widget on a page. */
            while (!isTabPage_Widget_(tabs, target)) {
                target = target->parent;
            }
            showTabPage_Widget(tabs, target);
            return iTrue;
        }
    }
    else if (equal_Command(cmd, "tabs.next") || equal_Command(cmd, "tabs.prev")) {
        iWidget *pages = findChild_Widget(tabs, "tabs.pages");
        int tabIndex = 0;
        iConstForEach(ObjectList, i, pages->children) {
            const iWidget *child = constAs_Widget(i.object);
            if (isVisible_Widget(child)) break;
            tabIndex++;
        }
        tabIndex += (equal_Command(cmd, "tabs.next") ? +1 : -1);
        showTabPage_Widget(tabs, child_Widget(pages, iWrap(tabIndex, 0, childCount_Widget(pages))));
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
    iWidget *content = addChildFlags_Widget(tabs, iClob(makeHDiv_Widget()), expand_WidgetFlag);
    setId_Widget(content, "tabs.content");
    iWidget *pages = addChildFlags_Widget(
        content, iClob(new_Widget()), expand_WidgetFlag | resizeChildren_WidgetFlag);
    setId_Widget(pages, "tabs.pages");
    addChild_Widget(parent, iClob(tabs));
    setCommandHandler_Widget(tabs, tabSwitcher_);
    return tabs;
}

static void addTabPage_Widget_(iWidget *tabs, enum iWidgetAddPos addPos, iWidget *page,
                               const char *label, int key, int kmods) {
    iWidget *   pages   = findChild_Widget(tabs, "tabs.pages");
    const iBool isSel   = childCount_Widget(pages) == 0;
    iWidget *   buttons = findChild_Widget(tabs, "tabs.buttons");
    iWidget *   button  = addChildPos_Widget(
        buttons,
        iClob(newKeyMods_LabelWidget(label, key, kmods, format_CStr("tabs.switch page:%p", page))),
        addPos);
    setFlags_Widget(buttons, hidden_WidgetFlag, iFalse);
    setFlags_Widget(button, selected_WidgetFlag, isSel);
    setFlags_Widget(
        button, noTopFrame_WidgetFlag | commandOnClick_WidgetFlag | expand_WidgetFlag, iTrue);
    addChildPos_Widget(pages, page, addPos);
    setFlags_Widget(page, hidden_WidgetFlag | disabled_WidgetFlag, !isSel);
}

void appendTabPage_Widget(iWidget *tabs, iWidget *page, const char *label, int key, int kmods) {
    addTabPage_Widget_(tabs, back_WidgetAddPos, page, label, key, kmods);
}

void prependTabPage_Widget(iWidget *tabs, iWidget *page, const char *label, int key, int kmods) {
    addTabPage_Widget_(tabs, front_WidgetAddPos, page, label, key, kmods);
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

void resizeToLargestPage_Widget(iWidget *tabs) {
//    puts("RESIZE TO LARGEST PAGE ...");
    iWidget *pages = findChild_Widget(tabs, "tabs.pages");
    iForEach(ObjectList, i, children_Widget(pages)) {
        setMinSize_Widget(i.object, zero_I2());
//        resetSize_Widget(i.object);
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
    setFixedSize_Widget(tabs, addY_I2(largest, height_Widget(findChild_Widget(tabs, "tabs.buttons"))));
//    puts("... DONE WITH RESIZE TO LARGEST PAGE");
}

iLabelWidget *tabButtonForPage_Widget_(iWidget *tabs, const iWidget *page) {
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

void showTabPage_Widget(iWidget *tabs, const iWidget *page) {
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
    }
    /* Notify. */
    if (!isEmpty_String(id_Widget(page))) {
        postCommandf_App("tabs.changed id:%s", cstr_String(id_Widget(page)));
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
    return childIndex_Widget(pages, page);
}

const iWidget *currentTabPage_Widget(const iWidget *tabs) {
    iWidget *pages = findChild_Widget(tabs, "tabs.pages");
    iConstForEach(ObjectList, i, pages->children) {
        if (isVisible_Widget(i.object)) {
            return constAs_Widget(i.object);
        }
    }
    return NULL;
}

size_t tabCount_Widget(const iWidget *tabs) {
    return childCount_Widget(findChild_Widget(tabs, "tabs.pages"));
}

/*-----------------------------------------------------------------------------------------------*/

static void acceptFilePath_(iWidget *dlg) {
    iInputWidget *input = findChild_Widget(dlg, "input");
    iString *path = makeAbsolute_Path(text_InputWidget(input));
    postCommandf_App("%s path:%s", cstr_String(id_Widget(dlg)), cstr_String(path));
    destroy_Widget(dlg);
    delete_String(path);
}

iBool filePathHandler_(iWidget *dlg, const char *cmd) {
    iWidget *ptr = as_Widget(pointer_Command(cmd));
    if (equal_Command(cmd, "input.ended")) {
        if (hasParent_Widget(ptr, dlg)) {
            if (arg_Command(cmd)) {
                acceptFilePath_(dlg);
            }
            else {
                destroy_Widget(dlg);
            }
            return iTrue;
        }
        return iFalse;
    }
    else if (ptr && !hasParent_Widget(ptr, dlg)) {
        /* Command from outside the dialog, so dismiss the dialog. */
        if (!equal_Command(cmd, "focus.lost")) {
            destroy_Widget(dlg);
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "filepath.cancel")) {
        end_InputWidget(findChild_Widget(dlg, "input"), iFalse);
        destroy_Widget(dlg);
        return iTrue;
    }
    else if (equal_Command(cmd, "filepath.accept")) {
        acceptFilePath_(dlg);
        return iTrue;
    }
    return iFalse;
}

iWidget *makeSheet_Widget(const char *id) {
    iWidget *sheet = new_Widget();
    setId_Widget(sheet, id);
    setPadding1_Widget(sheet, 3 * gap_UI);
    setFrameColor_Widget(sheet, uiSeparator_ColorId);
    setBackgroundColor_Widget(sheet, uiBackground_ColorId);
    setFlags_Widget(sheet,
                    parentCannotResize_WidgetFlag |
                        focusRoot_WidgetFlag | mouseModal_WidgetFlag | keepOnTop_WidgetFlag |
                        arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag |
                        centerHorizontal_WidgetFlag | overflowScrollable_WidgetFlag,
                    iTrue);
    return sheet;
}

static void updateSheetPanelMetrics_(iWidget *sheet) {
    iWidget *navi       = findChild_Widget(sheet, "panel.navi");
    iWidget *naviPad    = child_Widget(navi, 0);
    int      naviHeight = lineHeight_Text(defaultBig_FontId) + 4 * gap_UI;
#if defined (iPlatformAppleMobile)
    float left, right, top, bottom;
    safeAreaInsets_iOS(&left, &top, &right, &bottom);
    setPadding_Widget(sheet, left, 0, right, 0);
    navi->rect.pos = init_I2(left, top);
    iConstForEach(PtrArray, i, findChildren_Widget(sheet, "panel.toppad")) {
        iWidget *pad = *i.value;
        setSize_Widget(pad, init1_I2(naviHeight));
    }
#endif
    setFixedSize_Widget(navi, init_I2(-1, naviHeight));
}

static iBool slidePanelHandler_(iWidget *d, const char *cmd) {
    if (equal_Command(cmd, "panel.open")) {
        iWidget *button = pointer_Command(cmd);
        iWidget *panel = userData_Object(button);
        openMenu_Widget(panel, zero_I2());
//        updateTextCStr_LabelWidget(findWidget_App("panel.back"), );
        return iTrue;
    }
    if (equal_Command(cmd, "mouse.clicked") && arg_Command(cmd) &&
        argLabel_Command(cmd, "button") == SDL_BUTTON_X1) {
        postCommand_App("panel.close");
        return iTrue;
    }
    if (equal_Command(cmd, "panel.close")) {
        iBool wasClosed = iFalse;
        iForEach(ObjectList, i, children_Widget(parent_Widget(d))) {
            iWidget *child = i.object;
            if (!cmp_String(id_Widget(child), "panel") && isVisible_Widget(child)) {
                closeMenu_Widget(child);
                setFocus_Widget(NULL);
                updateTextCStr_LabelWidget(findWidget_App("panel.back"), "Back");
                wasClosed = iTrue;
            }
        }
        if (!wasClosed) {
            postCommand_App("prefs.dismiss");
        }
        return iTrue;
    }
    if (equal_Command(cmd, "panel.showhelp")) {
        postCommand_App("prefs.dismiss");
        postCommand_App("open url:about:help");
        return iTrue;
    }
    if (equal_Command(cmd, "window.resized")) {
        updateSheetPanelMetrics_(parent_Widget(d));
    }
    return iFalse;
}

static iBool isTwoColumnPage_(iWidget *d) {
    if (cmp_String(id_Widget(d), "dialogbuttons") == 0 ||
        cmp_String(id_Widget(d), "prefs.tabs") == 0) {
        return iFalse;
    }
    if (class_Widget(d) == &Class_Widget && childCount_Widget(d) == 2) {
        return class_Widget(child_Widget(d, 0)) == &Class_Widget &&
               class_Widget(child_Widget(d, 1)) == &Class_Widget;
    }
    return iFalse;
}

static iBool isOmittedPref_(const iString *id) {
    static const char *omittedPrefs[] = {
        "prefs.smoothscroll",
        "prefs.imageloadscroll",
        "prefs.retainwindow",
        "prefs.ca.file",
        "prefs.ca.path",
    };
    iForIndices(i, omittedPrefs) {
        if (cmp_String(id, omittedPrefs[i]) == 0) {
            return iTrue;
        }
    }
    return iFalse;
}

enum iPrefsElement {
    panelTitle_PrefsElement,
    heading_PrefsElement,
    toggle_PrefsElement,
    dropdown_PrefsElement,
    radioButton_PrefsElement,
    textInput_PrefsElement,
};

static iAnyObject *addPanelChild_(iWidget *panel, iAnyObject *child, int64_t flags,
                                  enum iPrefsElement elementType,
                                  enum iPrefsElement precedingElementType) {
    /* Erase redundant/unused headings. */
    if (precedingElementType == heading_PrefsElement &&
        (!child || elementType == heading_PrefsElement)) {
        iRelease(removeChild_Widget(panel, lastChild_Widget(panel)));
        if (!cmp_String(id_Widget(constAs_Widget(lastChild_Widget(panel))), "padding")) {
            iRelease(removeChild_Widget(panel, lastChild_Widget(panel)));
        }
    }
    if (child) {
        /* Insert padding between different element types. */
        if (precedingElementType != panelTitle_PrefsElement) {
            if (elementType == heading_PrefsElement ||
                (elementType == toggle_PrefsElement &&
                 precedingElementType != toggle_PrefsElement &&
                 precedingElementType != heading_PrefsElement)) {
                addChild_Widget(panel, iClob(makePadding_Widget(lineHeight_Text(defaultBig_FontId))));
            }
        }
        if (elementType == toggle_PrefsElement &&
            precedingElementType != toggle_PrefsElement) {
            flags |= borderTop_WidgetFlag;
        }
        return addChildFlags_Widget(panel, child, flags);
    }
    return NULL;
}

static void stripTrailingColon_(iLabelWidget *label) {
    const iString *text = text_LabelWidget(label);
    if (endsWith_String(text, ":")) {
        iString *mod = copy_String(text);
        removeEnd_String(mod, 1);
        updateText_LabelWidget(label, mod);
        delete_String(mod);
    }
}

static iLabelWidget *makePanelButton_(const char *text, const char *command) {
    iLabelWidget *btn = new_LabelWidget(text, command);
    setFlags_Widget(as_Widget(btn),
                    borderBottom_WidgetFlag | alignLeft_WidgetFlag |
                    frameless_WidgetFlag | extraPadding_WidgetFlag,
                    iTrue);
    checkIcon_LabelWidget(btn);
    setFont_LabelWidget(btn, defaultBig_FontId);
    setBackgroundColor_Widget(as_Widget(btn), uiBackgroundSidebar_ColorId);
    return btn;
}

static iWidget *makeValuePadding_(iWidget *value) {
    iInputWidget *input = isInstance_Object(value, &Class_InputWidget) ? (iInputWidget *) value : NULL;
    if (input) {
        setFont_InputWidget(input, defaultBig_FontId);
        setContentPadding_InputWidget(input, 3 * gap_UI, 3 * gap_UI);
    }
    iWidget *pad = new_Widget();
    setBackgroundColor_Widget(pad, uiBackgroundSidebar_ColorId);
    setPadding_Widget(pad, 0, 1 * gap_UI, 0, 1 * gap_UI);
    addChild_Widget(pad, iClob(value));
    setFlags_Widget(pad,
                    borderBottom_WidgetFlag |
                    arrangeVertical_WidgetFlag |
                    resizeToParentWidth_WidgetFlag |
                    resizeWidthOfChildren_WidgetFlag |
                    arrangeHeight_WidgetFlag,
                    iTrue);
    return pad;
}

void finalizeSheet_Widget(iWidget *sheet) {
    /* The sheet contents are completely rearranged and restyled on a phone.
       We'll set up a linear fullscreen arrangement of the widgets. Sheets are already
       scrollable so they can be taller than the display. In hindsight, it may have been
       easier to create phone versions of each dialog, but at least this works with any
       future changes to the UI (..."works"). At least this way it is possible to enforce
       a consistent styling. */
    if (deviceType_App() == phone_AppDeviceType && parent_Widget(sheet) == get_Window()->root) {
        if (~flags_Widget(sheet) & keepOnTop_WidgetFlag) {
            /* Already finalized. */
            arrange_Widget(sheet);
            postRefresh_App();
            return;
        }
        /* Modify the top sheet to act as a fullscreen background. */
        setPadding1_Widget(sheet, 0);
        setBackgroundColor_Widget(sheet, uiBackground_ColorId);
        setFlags_Widget(sheet,
                        keepOnTop_WidgetFlag |
                        parentCannotResize_WidgetFlag |
                        arrangeSize_WidgetFlag |
                        centerHorizontal_WidgetFlag |
                        arrangeVertical_WidgetFlag |
                        arrangeHorizontal_WidgetFlag |
                        overflowScrollable_WidgetFlag,
                        iFalse);
        setFlags_Widget(sheet,
                        commandOnClick_WidgetFlag |
                        frameless_WidgetFlag |
                        resizeWidthOfChildren_WidgetFlag,
                        iTrue);
        iPtrArray *   contents         = collect_PtrArray(new_PtrArray()); /* two-column pages */
        iPtrArray *   panelButtons     = collect_PtrArray(new_PtrArray());
        iWidget *     prefsTabs        = findChild_Widget(sheet, "prefs.tabs");
        iWidget *     dialogHeading    = (prefsTabs ? NULL : child_Widget(sheet, 0));
        const iBool   isPrefs          = (prefsTabs != NULL);
        const int64_t panelButtonFlags = borderBottom_WidgetFlag | alignLeft_WidgetFlag |
                                         frameless_WidgetFlag | extraPadding_WidgetFlag;
        iWidget *topPanel = new_Widget();
        setId_Widget(topPanel, "panel.top");
        addChild_Widget(topPanel, iClob(makePadding_Widget(lineHeight_Text(defaultBig_FontId))));
        if (prefsTabs) {
            iRelease(removeChild_Widget(sheet, child_Widget(sheet, 0))); /* heading */
            iRelease(removeChild_Widget(sheet, findChild_Widget(sheet, "dialogbuttons")));
            /* Pull out the pages and make them panels. */
            iWidget *pages = findChild_Widget(prefsTabs, "tabs.pages");
            size_t pageCount = tabCount_Widget(prefsTabs);
            for (size_t i = 0; i < pageCount; i++) {
                iString *text = copy_String(text_LabelWidget(tabPageButton_Widget(prefsTabs, tabPage_Widget(prefsTabs, 0))));
                iWidget *page = removeTabPage_Widget(prefsTabs, 0);
                iWidget *pageContent = child_Widget(page, 1); /* surrounded by padding widgets */
                pushBack_PtrArray(contents, ref_Object(pageContent));
                iLabelWidget *panelButton;
                pushBack_PtrArray(panelButtons,
                                  addChildFlags_Widget(topPanel,
                                                       iClob(panelButton = makePanelButton_(
                                                             i == 1 ? "${heading.prefs.userinterface}" : cstr_String(text),
                                                             "panel.open")),
                                                       (i == 0 ? borderTop_WidgetFlag : 0) |
                                                       chevron_WidgetFlag));
                const iChar icons[] = {
                    0x02699, /* gear */
                    0x1f4f1, /* mobile phone */
                    0x1f3a8, /* palette */
                    0x1f523,
                    0x1f5a7, /* computer network */
                };
                setIcon_LabelWidget(panelButton, icons[i]);
//                setFont_LabelWidget(panelButton, defaultBig_FontId);
//                setBackgroundColor_Widget(as_Widget(panelButton), uiBackgroundSidebar_ColorId);
                iRelease(page);
                delete_String(text);
            }
            destroy_Widget(prefsTabs);
        }
        iForEach(ObjectList, i, children_Widget(sheet)) {
            iWidget *child = i.object;
            if (isTwoColumnPage_(child)) {
                pushBack_PtrArray(contents, removeChild_Widget(sheet, child));
            }
            else {
                removeChild_Widget(sheet, child);
                addChild_Widget(topPanel, child);
                iRelease(child);
            }
        }
        const iBool useSlidePanels = (size_PtrArray(contents) == size_PtrArray(panelButtons));
        addChildFlags_Widget(sheet, iClob(topPanel),
                             arrangeVertical_WidgetFlag |
                             resizeWidthOfChildren_WidgetFlag | arrangeHeight_WidgetFlag |
                             overflowScrollable_WidgetFlag |
                             commandOnClick_WidgetFlag);
        setCommandHandler_Widget(topPanel, slidePanelHandler_);
        iForEach(PtrArray, j, contents) {
            iWidget *owner = topPanel;
            if (useSlidePanels) {
                /* Create a new child panel. */
                iLabelWidget *button = at_PtrArray(panelButtons, index_PtrArrayIterator(&j));
                owner = new_Widget();
                setId_Widget(owner, "panel");
                setUserData_Object(button, owner);
                setBackgroundColor_Widget(owner, uiBackground_ColorId);
                setId_Widget(addChild_Widget(owner, iClob(makePadding_Widget(0))), "panel.toppad");
                iLabelWidget *title = addChildFlags_Widget(owner,
                                                           iClob(new_LabelWidget(cstrCollect_String(upper_String(text_LabelWidget(button))), NULL)), alignLeft_WidgetFlag | frameless_WidgetFlag);
                setFont_LabelWidget(title, uiLabelLargeBold_FontId);
                setTextColor_LabelWidget(title, uiHeading_ColorId);
                addChildFlags_Widget(sheet,
                                     iClob(owner),
                                     focusRoot_WidgetFlag |
                                     hidden_WidgetFlag |
                                     disabled_WidgetFlag |
                                     //safePadding_WidgetFlag |
                                     arrangeVertical_WidgetFlag |
                                     resizeWidthOfChildren_WidgetFlag |
                                     arrangeHeight_WidgetFlag |
                                     overflowScrollable_WidgetFlag |
                                     horizontalOffset_WidgetFlag |
                                     commandOnClick_WidgetFlag);
            }
            iWidget *pageContent = j.ptr;
            iWidget *headings = child_Widget(pageContent, 0);
            iWidget *values   = child_Widget(pageContent, 1);
            enum iPrefsElement prevElement = panelTitle_PrefsElement;
            while (!isEmpty_ObjectList(children_Widget(headings))) {
                iWidget *heading = child_Widget(headings, 0);
                iWidget *value   = child_Widget(values, 0);
                removeChild_Widget(headings, heading);
                removeChild_Widget(values, value);
                if (isOmittedPref_(id_Widget(value))) {
                    iRelease(heading);
                    iRelease(value);
                    continue;
                }
                enum iPrefsElement element = toggle_PrefsElement;
                iLabelWidget *headingLabel = NULL;
                iLabelWidget *valueLabel = NULL;
                iInputWidget *valueInput = NULL;
                if (isInstance_Object(heading, &Class_LabelWidget)) {
                    headingLabel = (iLabelWidget *) heading;
                    stripTrailingColon_(headingLabel);
                }
                if (isInstance_Object(value, &Class_LabelWidget)) {
                    valueLabel = (iLabelWidget *) value;
                }
                if (isInstance_Object(value, &Class_InputWidget)) {
                    valueInput = (iInputWidget *) value;
                    element = textInput_PrefsElement;
                }
                if (valueLabel) {
                    setFont_LabelWidget(valueLabel, defaultBig_FontId);
                }
                /* Toggles have the button on the right. */
                if (valueLabel && cmp_String(command_LabelWidget(valueLabel), "toggle") == 0) {
                    element = toggle_PrefsElement;
                    iWidget *div = new_Widget();
                    setBackgroundColor_Widget(div, uiBackgroundSidebar_ColorId);
                    setPadding_Widget(div, gap_UI, gap_UI, 4 * gap_UI, gap_UI);
                    addChildFlags_Widget(div, iClob(heading), 0);
                    setFont_LabelWidget((iLabelWidget *) heading, defaultBig_FontId);
                    addChildFlags_Widget(div, iClob(new_Widget()), expand_WidgetFlag);
                    addChild_Widget(div, iClob(value));
                    addPanelChild_(owner,
                                   iClob(div),
                                   borderBottom_WidgetFlag | arrangeHeight_WidgetFlag |
                                       resizeWidthOfChildren_WidgetFlag |
                                       arrangeHorizontal_WidgetFlag,
                                   element, prevElement);
                }
                else {
                    if (valueLabel && isEmpty_String(text_LabelWidget(valueLabel))) {
                        element = heading_PrefsElement;
                        iRelease(value);
                        addPanelChild_(owner, iClob(heading), 0, element, prevElement);
                        setFont_LabelWidget(headingLabel, uiLabelBold_FontId);
                    }
                    else {
                        addChildFlags_Widget(owner, iClob(heading), borderBottom_WidgetFlag);
                        if (headingLabel) {
                            setTextColor_LabelWidget(headingLabel, uiSubheading_ColorId);
                            setText_LabelWidget(headingLabel,
                                                collect_String(upper_String(text_LabelWidget(headingLabel))));
                        }
                        const iBool isMenuButton = findChild_Widget(value, "menu") != NULL;
                        if (isMenuButton) {
                            element = dropdown_PrefsElement;
                            setFlags_Widget(value, noBackground_WidgetFlag | frameless_WidgetFlag, iTrue);
                            setFlags_Widget(value, alignLeft_WidgetFlag, iFalse);
                        }
                        if (childCount_Widget(value) >= 2) {
                            if (isInstance_Object(child_Widget(value, 0), &Class_InputWidget)) {
                                element = textInput_PrefsElement;
                                setPadding_Widget(value, 0, 0, gap_UI, 0);
                                valueInput = child_Widget(value, 0);
                                setFlags_Widget(as_Widget(valueInput), fixedWidth_WidgetFlag, iFalse);
                                setFlags_Widget(as_Widget(valueInput), expand_WidgetFlag, iTrue);
                                setFlags_Widget(value, resizeWidthOfChildren_WidgetFlag |
                                                resizeToParentWidth_WidgetFlag, iTrue);
                                setFont_LabelWidget(child_Widget(value, 1), defaultBig_FontId);
                                setTextColor_LabelWidget(child_Widget(value, 1), uiAnnotation_ColorId);
                            }
                            else {
                                element = radioButton_PrefsElement;
                            }
                        }
                        if (valueInput) {
                            setFont_InputWidget(valueInput, defaultBig_FontId);
                            setContentPadding_InputWidget(valueInput, 3 * gap_UI, 3 * gap_UI);
                        }
                        if (element == textInput_PrefsElement || isMenuButton) {
                            setFlags_Widget(value, borderBottom_WidgetFlag, iFalse);
                            //iWidget *pad = new_Widget();
                            //setBackgroundColor_Widget(pad, uiBackgroundSidebar_ColorId);
                            //setPadding_Widget(pad, 0, 1 * gap_UI, 0, 1 * gap_UI);
                            //addChild_Widget(pad, iClob(value));
                            addPanelChild_(owner, iClob(makeValuePadding_(value)), 0,
                                           element, prevElement);
                        }
                        else {
                            addPanelChild_(owner, iClob(value), 0, element, prevElement);
                        }
                        /* Radio buttons expand to fill the space. */
                        if (element == radioButton_PrefsElement) {
                            setBackgroundColor_Widget(value, uiBackgroundSidebar_ColorId);
                            setPadding_Widget(value, 4 * gap_UI, 2 * gap_UI, 4 * gap_UI, 2 * gap_UI);
                            setFlags_Widget(value,
                                            borderBottom_WidgetFlag |
                                            resizeToParentWidth_WidgetFlag |
                                            resizeWidthOfChildren_WidgetFlag,
                                            iTrue);
                            iForEach(ObjectList, sub, children_Widget(value)) {
                                if (isInstance_Object(sub.object, &Class_LabelWidget)) {
                                    iLabelWidget *opt = sub.object;
                                    setFont_LabelWidget(opt, defaultMedium_FontId);
                                    setFlags_Widget(as_Widget(opt), noBackground_WidgetFlag, iTrue);
                                }
                            }
                        }
                    }
                }
                prevElement = element;
            }
            addPanelChild_(owner, NULL, 0, 0, prevElement);
            destroy_Widget(pageContent);
            setFlags_Widget(owner, drawBackgroundToBottom_WidgetFlag, iTrue);
        }
        destroyPending_Widget();
        /* Additional elements for preferences. */
        if (isPrefs) {
            addChild_Widget(topPanel, iClob(makePadding_Widget(lineHeight_Text(defaultBig_FontId))));
            addChildFlags_Widget(topPanel,
                                 iClob(makePanelButton_(info_Icon " ${menu.help}", "panel.showhelp")),
                                 borderTop_WidgetFlag);
            addChildFlags_Widget(topPanel,
                                 iClob(makePanelButton_(planet_Icon " ${menu.about}", "panel.about")),
                                 chevron_WidgetFlag);
        }
        else {
            setFlags_Widget(topPanel, overflowScrollable_WidgetFlag, iTrue);
            /* Update heading style. */
            setFont_LabelWidget((iLabelWidget *) dialogHeading, uiLabelLargeBold_FontId);
            setFlags_Widget(dialogHeading, alignLeft_WidgetFlag, iTrue);
        }
        if (findChild_Widget(sheet, "valueinput.prompt")) {
            iWidget *prompt = findChild_Widget(sheet, "valueinput.prompt");
            setFlags_Widget(prompt, alignLeft_WidgetFlag, iTrue);
            iInputWidget *input = findChild_Widget(sheet, "input");
            removeChild_Widget(parent_Widget(input), input);
            addChild_Widget(topPanel, iClob(makeValuePadding_(as_Widget(input))));
        }
        /* Top padding for each panel, to account for the overlaid navbar. */ {
            setId_Widget(addChildPos_Widget(topPanel,
                                            iClob(makePadding_Widget(0)), front_WidgetAddPos),
                         "panel.toppad");
        }
        /* Navbar. */ {
            iWidget *navi = new_Widget();
            setId_Widget(navi, "panel.navi");
            setBackgroundColor_Widget(navi, uiBackground_ColorId);
            addChild_Widget(navi, iClob(makePadding_Widget(0)));
            iLabelWidget *back = addChildFlags_Widget(navi,
                                                      iClob(new_LabelWidget(leftAngle_Icon " ${panel.back}", "panel.close")),
                                                      noBackground_WidgetFlag | frameless_WidgetFlag |
                                                      alignLeft_WidgetFlag | extraPadding_WidgetFlag);
            checkIcon_LabelWidget(back);
            setId_Widget(as_Widget(back), "panel.back");
            setFont_LabelWidget(back, defaultBig_FontId);
            if (!isPrefs) {
                /* Pick up the dialog buttons for the navbar. */
                iWidget *buttons = findChild_Widget(sheet, "dialogbuttons");
                iLabelWidget *cancel = findMenuItem_Widget(buttons, "cancel");
//                if (!cancel) {
//                    cancel = findMenuItem_Widget(buttons, "translation.cancel");
//                }
                if (cancel) {
                    updateText_LabelWidget(back, text_LabelWidget(cancel));
                    setCommand_LabelWidget(back, command_LabelWidget(cancel));
                }
                iLabelWidget *def = (iLabelWidget *) lastChild_Widget(buttons);
                if (def && !cancel) {
                    updateText_LabelWidget(back, text_LabelWidget(def));
                    setCommand_LabelWidget(back, command_LabelWidget(def));
                    setFlags_Widget(as_Widget(back), alignLeft_WidgetFlag, iFalse);
                    setFlags_Widget(as_Widget(back), alignRight_WidgetFlag, iTrue);
                    setIcon_LabelWidget(back, 0);
                    setFont_LabelWidget(back, defaultBigBold_FontId);
                }
                else if (def != cancel) {
                    removeChild_Widget(buttons, def);
                    setFont_LabelWidget(def, defaultBigBold_FontId);
                    setFlags_Widget(as_Widget(def),
                                    frameless_WidgetFlag | extraPadding_WidgetFlag |
                                    noBackground_WidgetFlag, iTrue);
                    addChildFlags_Widget(as_Widget(back), iClob(def), moveToParentRightEdge_WidgetFlag);
                    updateSize_LabelWidget(def);
                }
                /* Action buttons are added in the bottom as extra buttons. */ {
                    iBool isFirstAction = iTrue;
                    iForEach(ObjectList, i, children_Widget(buttons)) {
                        if (isInstance_Object(i.object, &Class_LabelWidget) &&
                            i.object != cancel && i.object != def) {
                            iLabelWidget *item = i.object;
                            setBackgroundColor_Widget(i.object, uiBackgroundSidebar_ColorId);
                            setFont_LabelWidget(item, defaultBig_FontId);
                            removeChild_Widget(buttons, item);
                            addChildFlags_Widget(topPanel, iClob(item), panelButtonFlags |
                                                 (isFirstAction ? borderTop_WidgetFlag : 0));
                            updateSize_LabelWidget(item);
                            isFirstAction = iFalse;
                        }
                    }
                }
                iRelease(removeChild_Widget(parent_Widget(buttons), buttons));
                /* Styling for remaining elements. */
                iForEach(ObjectList, i, children_Widget(topPanel)) {
                    if (isInstance_Object(i.object, &Class_LabelWidget) &&
                        isEmpty_String(command_LabelWidget(i.object)) &&
                        isEmpty_String(id_Widget(i.object))) {
                        setFlags_Widget(i.object, alignLeft_WidgetFlag, iTrue);
                        if (font_LabelWidget(i.object) == uiLabel_FontId) {
                            setFont_LabelWidget(i.object, uiContent_FontId);
                        }
                    }
                }
            }
            addChildFlags_Widget(sheet, iClob(navi),
                                 drawBackgroundToVerticalSafeArea_WidgetFlag |
                                 arrangeHeight_WidgetFlag | resizeWidthOfChildren_WidgetFlag |
                                 resizeToParentWidth_WidgetFlag | arrangeVertical_WidgetFlag);
        }
        updateSheetPanelMetrics_(sheet);
        arrange_Widget(sheet->parent);
        postCommand_App("widget.overflow"); /* with the correct dimensions */
//        printTree_Widget(sheet);
    }
    else {
        arrange_Widget(sheet);
    }
    postRefresh_App();
}

void makeFilePath_Widget(iWidget *      parent,
                         const iString *initialPath,
                         const char *   title,
                         const char *   acceptLabel,
                         const char *   command) {
    setFocus_Widget(NULL);
//    processEvents_App(postedEventsOnly_AppEventMode);
    iWidget *dlg = makeSheet_Widget(command);
    setCommandHandler_Widget(dlg, filePathHandler_);
    addChild_Widget(parent, iClob(dlg));
    addChildFlags_Widget(dlg, iClob(new_LabelWidget(title, NULL)), frameless_WidgetFlag);
    iInputWidget *input = addChild_Widget(dlg, iClob(new_InputWidget(0)));
    if (initialPath) {
        setText_InputWidget(input, collect_String(makeRelative_Path(initialPath)));
    }
    setId_Widget(as_Widget(input), "input");
    as_Widget(input)->rect.size.x = dlg->rect.size.x;
    addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
    iWidget *div = new_Widget(); {
        setFlags_Widget(div, arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
        addChild_Widget(div, iClob(newKeyMods_LabelWidget("${cancel}", SDLK_ESCAPE, 0, "filepath.cancel")));
        addChild_Widget(div, iClob(newKeyMods_LabelWidget(acceptLabel, SDLK_RETURN, 0, "filepath.accept")));
    }
    addChild_Widget(dlg, iClob(div));
    finalizeSheet_Widget(dlg);
    setFocus_Widget(as_Widget(input));
}

static void acceptValueInput_(iWidget *dlg) {
    const iInputWidget *input = findChild_Widget(dlg, "input");
    if (!isEmpty_String(id_Widget(dlg))) {
        const iString *val = text_InputWidget(input);
        postCommandf_App("%s arg:%d value:%s",
                         cstr_String(id_Widget(dlg)),
                         toInt_String(val),
                         cstr_String(val));
    }
}

static void updateValueInputWidth_(iWidget *dlg) {
    const iRect safeRoot = safeRootRect_Window(get_Window());
    const iInt2 rootSize = safeRoot.size;
    iWidget *   title    = findChild_Widget(dlg, "valueinput.title");
    iWidget *   prompt   = findChild_Widget(dlg, "valueinput.prompt");
    if (deviceType_App() == phone_AppDeviceType) {
        dlg->rect.size.x = rootSize.x;
    }
    else {
        dlg->rect.size.x = iMaxi(iMaxi(rootSize.x / 2, title->rect.size.x), prompt->rect.size.x);
    }
}

iBool valueInputHandler_(iWidget *dlg, const char *cmd) {
    iWidget *ptr = as_Widget(pointer_Command(cmd));
    if (equal_Command(cmd, "window.resized")) {
        if (isVisible_Widget(dlg)) {
            updateValueInputWidth_(dlg);
            arrange_Widget(dlg);
        }
        return iFalse;
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
            destroy_Widget(dlg);
            return iTrue;
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "cancel")) {
        postCommandf_App("valueinput.cancelled id:%s", cstr_String(id_Widget(dlg)));
        setId_Widget(dlg, ""); /* no further commands to emit */
        destroy_Widget(dlg);
        return iTrue;
    }
    else if (equal_Command(cmd, "valueinput.accept")) {
        acceptValueInput_(dlg);
        destroy_Widget(dlg);
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
    if (deviceType_App() == phone_AppDeviceType) {
        fonts[0] = defaultMedium_FontId;
        fonts[1] = defaultMediumBold_FontId;
    }
    for (size_t i = 0; i < numActions; i++) {
        const char *label     = actions[i].label;
        const char *cmd       = actions[i].command;
        int         key       = actions[i].key;
        int         kmods     = actions[i].kmods;
        const iBool isDefault = (i == numActions - 1);
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
        setFlags_Widget(as_Widget(button), alignLeft_WidgetFlag | drawKey_WidgetFlag, isDefault);
        setFont_LabelWidget(button, isDefault ? fonts[1] : fonts[0]);
    }
    return div;
}

iWidget *makeValueInput_Widget(iWidget *parent, const iString *initialValue, const char *title,
                               const char *prompt, const char *acceptLabel, const char *command) {
    if (parent) {
        setFocus_Widget(NULL);
    }
    iWidget *dlg = makeSheet_Widget(command);
//    setFlags_Widget(dlg, horizontalSafeAreaPadding_Widget, iTrue);
    setCommandHandler_Widget(dlg, valueInputHandler_);
    if (parent) {
        addChild_Widget(parent, iClob(dlg));
    }
    setId_Widget(
        addChildFlags_Widget(dlg, iClob(new_LabelWidget(title, NULL)), frameless_WidgetFlag),
        "valueinput.title");
    setId_Widget(
        addChildFlags_Widget(dlg, iClob(new_LabelWidget(prompt, NULL)), frameless_WidgetFlag),
        "valueinput.prompt");
    iInputWidget *input = addChildFlags_Widget(dlg, iClob(new_InputWidget(0)),
                                               resizeToParentWidth_WidgetFlag);
    setContentPadding_InputWidget(input, 0.5f * gap_UI, 0.5f * gap_UI);
    if (deviceType_App() == phone_AppDeviceType) {
        setFont_InputWidget(input, defaultBig_FontId);
        setBackgroundColor_Widget(dlg, uiBackgroundSidebar_ColorId);
        setContentPadding_InputWidget(input, gap_UI, gap_UI);
    }
    if (initialValue) {
        setText_InputWidget(input, initialValue);
    }
    setId_Widget(as_Widget(input), "input");
    updateValueInputWidth_(dlg);
    addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
    addChild_Widget(
        dlg,
        iClob(makeDialogButtons_Widget(
            (iMenuItem[]){ { "${cancel}", 0, 0, NULL }, { acceptLabel, 0, 0, "valueinput.accept" } },
            2)));
    finalizeSheet_Widget(dlg);
    if (parent) {
        setFocus_Widget(as_Widget(input));
    }
    return dlg;
}

void updateValueInput_Widget(iWidget *d, const char *title, const char *prompt) {
    setTextCStr_LabelWidget(findChild_Widget(d, "valueinput.title"), title);
    setTextCStr_LabelWidget(findChild_Widget(d, "valueinput.prompt"), prompt);
    updateValueInputWidth_(d);
}

static iBool messageHandler_(iWidget *msg, const char *cmd) {
    /* Almost any command dismisses the sheet. */
    /* TODO: Use a "notification" prefix (like `) to ignore all types of commands line this? */
    if (!(equal_Command(cmd, "media.updated") ||
          equal_Command(cmd, "media.player.update") ||
          equal_Command(cmd, "bookmarks.request.finished") ||
          equal_Command(cmd, "document.autoreload") ||
          equal_Command(cmd, "document.reload") ||
          equal_Command(cmd, "document.request.updated") ||
          equal_Command(cmd, "scrollbar.fade") ||
          equal_Command(cmd, "widget.overflow") ||
          startsWith_CStr(cmd, "window."))) {
        destroy_Widget(msg);
    }
    return iFalse;
}

iWidget *makeMessage_Widget(const char *title, const char *msg) {
    iWidget *dlg =
        makeQuestion_Widget(title, msg, (iMenuItem[]){ { "${dlg.message.ok}", 0, 0, "message.ok" } }, 1);
    addAction_Widget(dlg, SDLK_ESCAPE, 0, "message.ok");
    addAction_Widget(dlg, SDLK_SPACE, 0, "message.ok");
    return dlg;
}

iWidget *makeQuestion_Widget(const char *title, const char *msg,
                             const iMenuItem *items, size_t numItems) {
    processEvents_App(postedEventsOnly_AppEventMode);
    iWidget *dlg = makeSheet_Widget("");
    setCommandHandler_Widget(dlg, messageHandler_);
    addChildFlags_Widget(dlg, iClob(new_LabelWidget(title, NULL)), frameless_WidgetFlag);
    addChildFlags_Widget(dlg, iClob(new_LabelWidget(msg, NULL)), frameless_WidgetFlag);
    addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
    addChild_Widget(dlg, iClob(makeDialogButtons_Widget(items, numItems)));
    addChild_Widget(get_Window()->root, iClob(dlg));
    arrange_Widget(dlg); /* BUG: This extra arrange shouldn't be needed but the dialog won't
                            be arranged correctly unless it's here. */
    finalizeSheet_Widget(dlg);
    return dlg;
}

void setToggle_Widget(iWidget *d, iBool active) {
    if (d) {
        setFlags_Widget(d, selected_WidgetFlag, active);
        iLabelWidget *label = (iLabelWidget *) d;
        if (!cmp_String(text_LabelWidget(label), cstr_Lang("toggle.yes")) ||
            !cmp_String(text_LabelWidget(label), cstr_Lang("toggle.no"))) {
            updateText_LabelWidget(
                (iLabelWidget *) d,
                collectNewCStr_String(isSelected_Widget(d) ? "${toggle.yes}" : "${toggle.no}"));
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
    iWidget *toggle = as_Widget(new_LabelWidget("${toggle.yes}", "toggle")); /* "YES" for sizing */
    setId_Widget(toggle, id);
    /* TODO: Measure both labels and use the larger of the two. */
    updateTextCStr_LabelWidget((iLabelWidget *) toggle, "${toggle.no}"); /* actual initial value */
    setFlags_Widget(toggle, fixedWidth_WidgetFlag, iTrue);
    setCommandHandler_Widget(toggle, toggleHandler_);
    return toggle;
}

static void appendFramelessTabPage_(iWidget *tabs, iWidget *page, const char *title, int shortcut,
                                    int kmods) {
    appendTabPage_Widget(tabs, page, title, shortcut, kmods);
    setFlags_Widget(
        (iWidget *) back_ObjectList(children_Widget(findChild_Widget(tabs, "tabs.buttons"))),
        frameless_WidgetFlag | noBackground_WidgetFlag,
        iTrue);
}

static iWidget *makeTwoColumnWidget_(iWidget **headings, iWidget **values) {
    iWidget *page = new_Widget();
    setFlags_Widget(page, arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
    *headings = addChildFlags_Widget(
        page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    *values = addChildFlags_Widget(
        page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    return page;
}

static iWidget *appendTwoColumnPage_(iWidget *tabs, const char *title, int shortcut, iWidget **headings,
                                     iWidget **values) {
    /* TODO: Use `makeTwoColumnWidget_()`, see above. */
    iWidget *page = new_Widget();
    setFlags_Widget(page, arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag |
                    resizeHeightOfChildren_WidgetFlag, iTrue);
    addChildFlags_Widget(page, iClob(new_Widget()), expand_WidgetFlag);
    setPadding_Widget(page, 0, gap_UI, 0, gap_UI);
    iWidget *columns = new_Widget();
    addChildFlags_Widget(page, iClob(columns), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
    *headings = addChildFlags_Widget(
        columns, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    *values = addChildFlags_Widget(
        columns, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    addChildFlags_Widget(page, iClob(new_Widget()), expand_WidgetFlag);
    appendFramelessTabPage_(tabs, iClob(page), title, shortcut, shortcut ? KMOD_PRIMARY : 0);
    return page;
}

static void makeTwoColumnHeading_(const char *title, iWidget *headings, iWidget *values) {
    addChildFlags_Widget(headings,
                         iClob(makeHeading_Widget(format_CStr(uiHeading_ColorEscape "%s", title))),
                         ignoreForParentWidth_WidgetFlag);
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

static void addFontButtons_(iWidget *parent, const char *id) {
    const char *fontNames[] = {
        "Nunito", "Fira Sans", "Literata", "Tinos", "Source Sans Pro", "Iosevka"
    };
    iArray *items = new_Array(sizeof(iMenuItem));
    iForIndices(i, fontNames) {
        pushBack_Array(items,
                       &(iMenuItem){ fontNames[i], 0, 0, format_CStr("!%s.set arg:%d", id, i) });
    }
    iLabelWidget *button = makeMenuButton_LabelWidget("Source Sans Pro", data_Array(items), size_Array(items));
    setBackgroundColor_Widget(findChild_Widget(as_Widget(button), "menu"), uiBackgroundMenu_ColorId);
    setId_Widget(as_Widget(button), format_CStr("prefs.%s", id));
    addChildFlags_Widget(parent, iClob(button), alignLeft_WidgetFlag);
    delete_Array(items);
}

#if 0
static int cmp_MenuItem_(const void *e1, const void *e2) {
    const iMenuItem *a = e1, *b = e2;
    return iCmpStr(a->label, b->label);
}
#endif

void updatePreferencesLayout_Widget(iWidget *prefs) {
    if (!prefs) return;
    /* Doing manual layout here because the widget arranging logic isn't sophisticated enough. */
    /* TODO: Make the arranging more sophisticated to automate this. */
    static const char *inputIds[] = {
        "prefs.searchurl",
        "prefs.downloads",
        "prefs.ca.file",
        "prefs.ca.path",
        "prefs.proxy.gemini",
        "prefs.proxy.gopher",
        "prefs.proxy.http"
    };
    iWidget *tabs = findChild_Widget(prefs, "prefs.tabs");
    /* Input fields expand to the right edge. */
    /* TODO: Add an arrangement flag for this. */
    iForIndices(i, inputIds) {
        iInputWidget *input = findChild_Widget(tabs, inputIds[i]);
        if (input) {
            as_Widget(input)->rect.size.x = 0;
        }
    }
    as_Widget(findChild_Widget(prefs, "bindings"))->rect.size.x = 0;
    resizeToLargestPage_Widget(tabs);
    arrange_Widget(prefs);
    iForIndices(i, inputIds) {
        expandInputFieldWidth_(findChild_Widget(tabs, inputIds[i]));
    }
}

iWidget *makePreferences_Widget(void) {
    iWidget *dlg = makeSheet_Widget("prefs");
    addChildFlags_Widget(dlg,
                         iClob(new_LabelWidget(uiHeading_ColorEscape "${heading.prefs}", NULL)),
                         frameless_WidgetFlag);
    iWidget *tabs = makeTabs_Widget(dlg);
    setBackgroundColor_Widget(findChild_Widget(tabs, "tabs.buttons"), uiBackgroundSidebar_ColorId);
    setId_Widget(tabs, "prefs.tabs");
    iWidget *headings, *values;
    const int bigGap = lineHeight_Text(uiLabel_FontId) * 3 / 4;
    /* General preferences. */ {
        appendTwoColumnPage_(tabs, "${heading.prefs.general}", '1', &headings, &values);
#if defined (LAGRANGE_DOWNLOAD_EDIT)
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.downloads}")));
        setId_Widget(addChild_Widget(values, iClob(new_InputWidget(0))), "prefs.downloads");
#endif
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.searchurl}")));
        setId_Widget(addChild_Widget(values, iClob(new_InputWidget(0))), "prefs.searchurl");
        addChild_Widget(headings, iClob(makePadding_Widget(bigGap)));
        addChild_Widget(values, iClob(makePadding_Widget(bigGap)));
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.collapsepreonload}")));
        addChild_Widget(values, iClob(makeToggle_Widget("prefs.collapsepreonload")));
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.plaintext.wrap}")));
        addChild_Widget(values, iClob(makeToggle_Widget("prefs.plaintext.wrap")));
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.centershort}")));
        addChild_Widget(values, iClob(makeToggle_Widget("prefs.centershort")));
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.hoverlink}")));
        addChild_Widget(values, iClob(makeToggle_Widget("prefs.hoverlink")));
        addChild_Widget(headings, iClob(makePadding_Widget(bigGap)));
        addChild_Widget(values, iClob(makePadding_Widget(bigGap)));
        /* UI languages. */ {
            iArray *uiLangs = collectNew_Array(sizeof(iMenuItem));
            const iMenuItem langItems[] = {
//                { "${lang.de} - de", 0, 0, "uilang id:de" },
                { "${lang.en} - en", 0, 0, "uilang id:en" },
                { "${lang.es} - es", 0, 0, "uilang id:es" },
                { "${lang.fi} - fi", 0, 0, "uilang id:fi" },
                { "${lang.ie} - ie", 0, 0, "uilang id:ie" },
                { "${lang.ru} - ru", 0, 0, "uilang id:ru" },
                { "${lang.sr} - sr", 0, 0, "uilang id:sr" },
                { "${lang.zh.hans} - zh", 0, 0, "uilang id:zh_Hans" },
                { "${lang.zh.hant} - zh", 0, 0, "uilang id:zh_Hant" },
            };
            pushBackN_Array(uiLangs, langItems, iElemCount(langItems));
            //sort_Array(uiLangs, cmp_MenuItem_);
            /* TODO: Add an arrange flag for resizing parent to widest child. */
            int widest = 0;
            size_t widestPos = iInvalidPos;
            iConstForEach(Array, i, uiLangs) {
                const int width =
                    advance_Text(uiLabel_FontId,
                                 translateCStr_Lang(((const iMenuItem *) i.value)->label))
                        .x;
                if (widestPos == iInvalidPos || width > widest) {
                    widest = width;
                    widestPos = index_ArrayConstIterator(&i);
                }
            }
            addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.uilang}")));
            setId_Widget(addChildFlags_Widget(values,
                                              iClob(makeMenuButton_LabelWidget(
                                                  value_Array(uiLangs, widestPos, iMenuItem).label,
                                                  data_Array(uiLangs),
                                                  size_Array(uiLangs))),
                                              alignLeft_WidgetFlag),
                         "prefs.uilang");
        }
    }
    /* User Interface. */ {
        appendTwoColumnPage_(tabs, "${heading.prefs.interface}", '2', &headings, &values);
#if defined (iPlatformApple) || defined (iPlatformMSys)
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.ostheme}")));
        addChild_Widget(values, iClob(makeToggle_Widget("prefs.ostheme")));
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
        /* Accents. */
        iWidget *accent = new_Widget(); {
            setId_Widget(addChild_Widget(accent, iClob(new_LabelWidget("${prefs.accent.teal}", "accent.set arg:0"))), "prefs.accent.0");
            setId_Widget(addChild_Widget(accent, iClob(new_LabelWidget("${prefs.accent.orange}", "accent.set arg:1"))), "prefs.accent.1");
        }
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.accent}")));
        addChildFlags_Widget(values, iClob(accent), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
#if defined (LAGRANGE_CUSTOM_FRAME)
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.customframe}")));
        addChild_Widget(values, iClob(makeToggle_Widget("prefs.customframe")));
#endif
        makeTwoColumnHeading_("${heading.prefs.scrolling}", headings, values);
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.smoothscroll}")));
        addChild_Widget(values, iClob(makeToggle_Widget("prefs.smoothscroll")));
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.imageloadscroll}")));
        addChild_Widget(values, iClob(makeToggle_Widget("prefs.imageloadscroll")));
        if (deviceType_App() == phone_AppDeviceType) {
            addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.hidetoolbarscroll}")));
            addChild_Widget(values, iClob(makeToggle_Widget("prefs.hidetoolbarscroll")));
        }
        makeTwoColumnHeading_("${heading.prefs.sizing}", headings, values);
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.uiscale}")));
        setId_Widget(addChild_Widget(values, iClob(new_InputWidget(8))), "prefs.uiscale");
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.retainwindow}")));
        addChild_Widget(values, iClob(makeToggle_Widget("prefs.retainwindow")));
    }
    /* Colors. */ {
        appendTwoColumnPage_(tabs, "${heading.prefs.colors}", '3', &headings, &values);
        makeTwoColumnHeading_("${heading.prefs.pagecontent}", headings, values);
        for (int i = 0; i < 2; ++i) {
            const iBool isDark = (i == 0);
            const char *mode = isDark ? "dark" : "light";
            const iMenuItem themes[] = {
                { "${prefs.doctheme.name.colorfuldark}", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, colorfulDark_GmDocumentTheme) },
                { "${prefs.doctheme.name.colorfullight}", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, colorfulLight_GmDocumentTheme) },
                { "${prefs.doctheme.name.black}", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, black_GmDocumentTheme) },
                { "${prefs.doctheme.name.gray}", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, gray_GmDocumentTheme) },
                { "${prefs.doctheme.name.white}", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, white_GmDocumentTheme) },
                { "${prefs.doctheme.name.sepia}", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, sepia_GmDocumentTheme) },
                { "${prefs.doctheme.name.highcontrast}", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, highContrast_GmDocumentTheme) },
            };
            addChild_Widget(headings, iClob(makeHeading_Widget(isDark ? "${prefs.doctheme.dark}" : "${prefs.doctheme.light}")));
            iLabelWidget *button =
                makeMenuButton_LabelWidget(themes[1].label, themes, iElemCount(themes));
//            setFrameColor_Widget(findChild_Widget(as_Widget(button), "menu"),
//                                 uiBackgroundSelected_ColorId);
            setBackgroundColor_Widget(findChild_Widget(as_Widget(button), "menu"), uiBackgroundMenu_ColorId);
            setId_Widget(addChildFlags_Widget(values, iClob(button), alignLeft_WidgetFlag),
                         format_CStr("prefs.doctheme.%s", mode));
        }
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.saturation}")));
        iWidget *sats = new_Widget();
        /* Saturation levels. */ {
            addRadioButton_(sats, "prefs.saturation.3", "100 %", "saturation.set arg:100");
            addRadioButton_(sats, "prefs.saturation.2", "66 %", "saturation.set arg:66");
            addRadioButton_(sats, "prefs.saturation.1", "33 %", "saturation.set arg:33");
            addRadioButton_(sats, "prefs.saturation.0", "0 %", "saturation.set arg:0");
        }
        addChildFlags_Widget(values, iClob(sats), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
    }
    /* Layout. */ {
        setId_Widget(appendTwoColumnPage_(tabs, "${heading.prefs.style}", '4', &headings, &values), "prefs.page.style");
        makeTwoColumnHeading_("${heading.prefs.fonts}", headings, values);
        /* Fonts. */ {
            addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.headingfont}")));
            addFontButtons_(values, "headingfont");
            addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.font}")));
            addFontButtons_(values, "font");
            addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.mono}")));
            iWidget *mono = new_Widget(); {
                iWidget *tog;
                setTextCStr_LabelWidget(
                    addChild_Widget(mono, tog = iClob(makeToggle_Widget("prefs.mono.gemini"))),
                    "${prefs.mono.gemini}");
                setFlags_Widget(tog, fixedWidth_WidgetFlag, iFalse);
                updateSize_LabelWidget((iLabelWidget *) tog);
                setTextCStr_LabelWidget(
                    addChild_Widget(mono, tog = iClob(makeToggle_Widget("prefs.mono.gopher"))),
                    "${prefs.mono.gopher}");
                setFlags_Widget(tog, fixedWidth_WidgetFlag, iFalse);
                updateSize_LabelWidget((iLabelWidget *) tog);
            }
            addChildFlags_Widget(values, iClob(mono), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
            addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.boldlink}")));
            iWidget *boldLink = new_Widget(); {
                /* TODO: Add a utility function for this type of toggles? (also for above) */
                iWidget *tog;
                setTextCStr_LabelWidget(
                    addChild_Widget(boldLink, tog = iClob(makeToggle_Widget("prefs.boldlink.dark"))),
                    "${prefs.boldlink.dark}");
                setFlags_Widget(tog, fixedWidth_WidgetFlag, iFalse);
                updateSize_LabelWidget((iLabelWidget *) tog);
                setTextCStr_LabelWidget(
                    addChild_Widget(boldLink, tog = iClob(makeToggle_Widget("prefs.boldlink.light"))),
                    "${prefs.boldlink.light}");
                setFlags_Widget(tog, fixedWidth_WidgetFlag, iFalse);
                updateSize_LabelWidget((iLabelWidget *) tog);
            }
            addChildFlags_Widget(values, iClob(boldLink), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        }
        makeTwoColumnHeading_("${heading.prefs.paragraph}", headings, values);
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.linewidth}")));
        iWidget *widths = new_Widget();
        /* Line widths. */ {
            addRadioButton_(widths, "prefs.linewidth.30", "\u20132", "linewidth.set arg:30");
            addRadioButton_(widths, "prefs.linewidth.34", "\u20131", "linewidth.set arg:34");
            addRadioButton_(widths, "prefs.linewidth.38", "${prefs.linewidth.normal}", "linewidth.set arg:38");
            addRadioButton_(widths, "prefs.linewidth.43", "+1", "linewidth.set arg:43");
            addRadioButton_(widths, "prefs.linewidth.48", "+2", "linewidth.set arg:48");
            addRadioButton_(widths, "prefs.linewidth.1000", "${prefs.linewidth.fill}", "linewidth.set arg:1000");
        }
        addChildFlags_Widget(values, iClob(widths), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.quoteicon}")));
        iWidget *quote = new_Widget(); {
            addRadioButton_(quote, "prefs.quoteicon.1", "${prefs.quoteicon.icon}", "quoteicon.set arg:1");
            addRadioButton_(quote, "prefs.quoteicon.0", "${prefs.quoteicon.line}", "quoteicon.set arg:0");
        }
        addChildFlags_Widget(values, iClob(quote), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.biglede}")));
        addChild_Widget(values, iClob(makeToggle_Widget("prefs.biglede")));
//        makeTwoColumnHeading_("${heading.prefs.widelayout}", headings, values);
        addChild_Widget(headings, iClob(makePadding_Widget(bigGap)));
        addChild_Widget(values, iClob(makePadding_Widget(bigGap)));
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.sideicon}")));
        addChild_Widget(values, iClob(makeToggle_Widget("prefs.sideicon")));
    }
    /* Network. */ {
        appendTwoColumnPage_(tabs, "${heading.prefs.network}", '5', &headings, &values);
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.decodeurls}")));
        addChild_Widget(values, iClob(makeToggle_Widget("prefs.decodeurls")));
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.cachesize}")));
        iWidget *cacheGroup = new_Widget(); {
            iInputWidget *cache = new_InputWidget(4);
            setSelectAllOnFocus_InputWidget(cache, iTrue);
            setId_Widget(addChild_Widget(cacheGroup, iClob(cache)), "prefs.cachesize");
            addChildFlags_Widget(cacheGroup, iClob(new_LabelWidget("${mb}", NULL)), frameless_WidgetFlag);
        }
        addChildFlags_Widget(values, iClob(cacheGroup), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        makeTwoColumnHeading_("${heading.prefs.certs}", headings, values);
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.ca.file}")));
        setId_Widget(addChild_Widget(values, iClob(new_InputWidget(0))), "prefs.ca.file");
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.ca.path}")));
        setId_Widget(addChild_Widget(values, iClob(new_InputWidget(0))), "prefs.ca.path");
        makeTwoColumnHeading_("${heading.prefs.proxies}", headings, values);
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.proxy.gemini}")));
        setId_Widget(addChild_Widget(values, iClob(new_InputWidget(0))), "prefs.proxy.gemini");
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.proxy.gopher}")));
        setId_Widget(addChild_Widget(values, iClob(new_InputWidget(0))), "prefs.proxy.gopher");
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.proxy.http}")));
        setId_Widget(addChild_Widget(values, iClob(new_InputWidget(0))), "prefs.proxy.http");
    }
    /* Keybindings. */
    if (deviceType_App() == desktop_AppDeviceType) {
        iBindingsWidget *bind = new_BindingsWidget();
        appendFramelessTabPage_(tabs, iClob(bind), "${heading.prefs.keys}", '6', KMOD_PRIMARY);
    }
    addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
    updatePreferencesLayout_Widget(dlg);
    addChild_Widget(dlg,
                    iClob(makeDialogButtons_Widget(
                        (iMenuItem[]){ { "${dismiss}", SDLK_ESCAPE, 0, "prefs.dismiss" } }, 1)));
    addChild_Widget(get_Window()->root, iClob(dlg));
    finalizeSheet_Widget(dlg);
    //printTree_Widget(dlg);
    return dlg;
}

iWidget *makeBookmarkEditor_Widget(void) {
    iWidget *dlg = makeSheet_Widget("bmed");
    setId_Widget(addChildFlags_Widget(
                     dlg,
                     iClob(new_LabelWidget(uiHeading_ColorEscape "${heading.bookmark.edit}", NULL)),
                     frameless_WidgetFlag),
                 "bmed.heading");
    iWidget *headings, *values;
    addChild_Widget(dlg, iClob(makeTwoColumnWidget_(&headings, &values)));
    iInputWidget *inputs[4];
    addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.bookmark.title}")));
    setId_Widget(addChild_Widget(values, iClob(inputs[0] = new_InputWidget(0))), "bmed.title");
    addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.bookmark.url}")));
    setId_Widget(addChild_Widget(values, iClob(inputs[1] = new_InputWidget(0))), "bmed.url");
    setUrlContent_InputWidget(inputs[1], iTrue);
    addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.bookmark.tags}")));
    setId_Widget(addChild_Widget(values, iClob(inputs[2] = new_InputWidget(0))), "bmed.tags");
    addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.bookmark.icon}")));
    setId_Widget(addChild_Widget(values, iClob(inputs[3] = new_InputWidget(1))), "bmed.icon");
    arrange_Widget(dlg);
    for (int i = 0; i < 3; ++i) {
        as_Widget(inputs[i])->rect.size.x = 100 * gap_UI - headings->rect.size.x;
    }
    addChild_Widget(
        dlg,
        iClob(makeDialogButtons_Widget((iMenuItem[]){ { "${cancel}", 0, 0, NULL },
                                                { uiTextCaution_ColorEscape "${dlg.bookmark.save}",
                                                  SDLK_RETURN,
                                                  KMOD_PRIMARY,
                                                  "bmed.accept" } },
                                 2)));
    addChild_Widget(get_Window()->root, iClob(dlg));
    finalizeSheet_Widget(dlg);
    return dlg;
}

static void enableSidebars_(void) {
    setFlags_Widget(findWidget_App("sidebar"), disabled_WidgetFlag, iFalse);
    setFlags_Widget(findWidget_App("sidebar2"), disabled_WidgetFlag, iFalse);
}

static iBool handleBookmarkCreationCommands_SidebarWidget_(iWidget *editor, const char *cmd) {
    if (equal_Command(cmd, "bmed.accept") || equal_Command(cmd, "cancel")) {
        if (equal_Command(cmd, "bmed.accept")) {
            const iString *title = text_InputWidget(findChild_Widget(editor, "bmed.title"));
            const iString *url   = text_InputWidget(findChild_Widget(editor, "bmed.url"));
            const iString *tags  = text_InputWidget(findChild_Widget(editor, "bmed.tags"));
            const iString *icon  = collect_String(trimmed_String(text_InputWidget(findChild_Widget(editor, "bmed.icon"))));
            const uint32_t id    = add_Bookmarks(bookmarks_App(), url, title, tags, first_String(icon));
            if (!isEmpty_String(icon)) {
                iBookmark *bm = get_Bookmarks(bookmarks_App(), id);
                if (!hasTag_Bookmark(bm, "usericon")) {
                    addTag_Bookmark(bm, "usericon");
                }
            }
            postCommand_App("bookmarks.changed");
        }
        destroy_Widget(editor);
        /* Sidebars are disabled when a dialog is opened. */
        enableSidebars_();
        return iTrue;
    }
    return iFalse;
}

iWidget *makeBookmarkCreation_Widget(const iString *url, const iString *title, iChar icon) {
    iWidget *dlg = makeBookmarkEditor_Widget();
    setId_Widget(dlg, "bmed.create");
    setTextCStr_LabelWidget(findChild_Widget(dlg, "bmed.heading"),
                            uiHeading_ColorEscape "${heading.bookmark.add}");
    iUrl parts;
    init_Url(&parts, url);
    setTextCStr_InputWidget(findChild_Widget(dlg, "bmed.title"),
                            title ? cstr_String(title) : cstr_Rangecc(parts.host));
    setText_InputWidget(findChild_Widget(dlg, "bmed.url"), url);
    setId_Widget(
        addChildFlags_Widget(
            dlg,
            iClob(new_LabelWidget(cstrCollect_String(newUnicodeN_String(&icon, 1)), NULL)),
            collapse_WidgetFlag | hidden_WidgetFlag | disabled_WidgetFlag),
        "bmed.icon");
    setCommandHandler_Widget(dlg, handleBookmarkCreationCommands_SidebarWidget_);
    return dlg;
}


static iBool handleFeedSettingCommands_(iWidget *dlg, const char *cmd) {
    if (equal_Command(cmd, "cancel")) {
        destroy_Widget(dlg);
        /* Sidebars are disabled when a dialog is opened. */
        enableSidebars_();
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
        const iString *tags = collectNewFormat_String("subscribed%s", headings ? " headings" : "");
        if (!id) {
            const size_t numSubs = numSubscribed_Feeds();
            const iString *url   = url_DocumentWidget(document_App());
            add_Bookmarks(bookmarks_App(),
                          url,
                          feedTitle,
                          tags,
                          siteIcon_GmDocument(document_DocumentWidget(document_App())));
            if (numSubs == 0) {
                /* Auto-refresh after first addition. */
                postCommand_App("feeds.refresh");
            }
        }
        else {
            iBookmark *bm = get_Bookmarks(bookmarks_App(), id);
            if (bm) {
                set_String(&bm->title, feedTitle);
                set_String(&bm->tags, tags);
            }
        }
        postCommand_App("bookmarks.changed");
        destroy_Widget(dlg);
        /* Sidebars are disabled when a dialog is opened. */
        enableSidebars_();
        return iTrue;
    }
    return iFalse;
}

iWidget *makeFeedSettings_Widget(uint32_t bookmarkId) {
    iWidget *dlg = makeSheet_Widget("feedcfg");
    setId_Widget(addChildFlags_Widget(
                     dlg,
                     iClob(new_LabelWidget(bookmarkId ? uiHeading_ColorEscape "${heading.feedcfg}"
                                                      : uiHeading_ColorEscape "${heading.subscribe}",
                                           NULL)),
                     frameless_WidgetFlag),
                 "feedcfg.heading");
    iWidget *headings, *values;
    addChild_Widget(dlg, iClob(makeTwoColumnWidget_(&headings, &values)));
    addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.feed.title}")));
    iInputWidget *input = new_InputWidget(0);
    setId_Widget(addChild_Widget(values, iClob(input)), "feedcfg.title");
    addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.feed.entrytype}")));
    iWidget *types = new_Widget(); {
        addRadioButton_(types, "feedcfg.type.gemini", "${dlg.feed.type.gemini}", "feedcfg.type arg:0");
        addRadioButton_(types, "feedcfg.type.headings", "${dlg.feed.type.headings}", "feedcfg.type arg:1");
    }
    addChildFlags_Widget(values, iClob(types), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
    iWidget *buttons =
        addChild_Widget(dlg,
                        iClob(makeDialogButtons_Widget(
                            (iMenuItem[]){ { "${cancel}", 0, 0, NULL },
                                           { bookmarkId ? uiTextCaution_ColorEscape "${dlg.feed.save}"
                                                        : uiTextCaution_ColorEscape "${dlg.feed.sub}",
                                             SDLK_RETURN,
                                             KMOD_PRIMARY,
                                             format_CStr("feedcfg.accept bmid:%d", bookmarkId) } },
                            2)));
    setId_Widget(child_Widget(buttons, childCount_Widget(buttons) - 1), "feedcfg.save");
    arrange_Widget(dlg);
    as_Widget(input)->rect.size.x = 100 * gap_UI - headings->rect.size.x;
    addChild_Widget(get_Window()->root, iClob(dlg));
    finalizeSheet_Widget(dlg);
    /* Initialize. */ {
        const iBookmark *bm  = bookmarkId ? get_Bookmarks(bookmarks_App(), bookmarkId) : NULL;
        setText_InputWidget(findChild_Widget(dlg, "feedcfg.title"),
                            bm ? &bm->title : feedTitle_DocumentWidget(document_App()));
        setFlags_Widget(findChild_Widget(dlg,
                                         hasTag_Bookmark(bm, "headings") ? "feedcfg.type.headings"
                                                                         : "feedcfg.type.gemini"),
                        selected_WidgetFlag,
                        iTrue);
        setCommandHandler_Widget(dlg, handleFeedSettingCommands_);
    }
    return dlg;
}

iWidget *makeIdentityCreation_Widget(void) {
    iWidget *dlg = makeSheet_Widget("ident");
    setId_Widget(addChildFlags_Widget(
                     dlg,
                     iClob(new_LabelWidget(uiHeading_ColorEscape "${heading.newident}", NULL)),
                     frameless_WidgetFlag),
                 "ident.heading");
    iWidget *page = new_Widget();
    addChildFlags_Widget(
        dlg,
        iClob(
            new_LabelWidget("${dlg.newident.rsa.selfsign}", NULL)),
        frameless_WidgetFlag);
    addChild_Widget(dlg, iClob(page));
    setFlags_Widget(page, arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
    iWidget *headings = addChildFlags_Widget(
        page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    iWidget *values = addChildFlags_Widget(
        page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    iInputWidget *inputs[6];
    addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.newident.until}")));
    setId_Widget(addChild_Widget(values, iClob(newHint_InputWidget(19, "${hint.newident.date}"))), "ident.until");
    addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.newident.commonname}")));
    setId_Widget(addChild_Widget(values, iClob(inputs[0] = new_InputWidget(0))), "ident.common");
    /* Temporary? */ {
        addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.newident.temp}")));
        iWidget *tmpGroup = new_Widget();
        setFlags_Widget(tmpGroup, arrangeSize_WidgetFlag | arrangeHorizontal_WidgetFlag, iTrue);
        addChild_Widget(tmpGroup, iClob(makeToggle_Widget("ident.temp")));
        setId_Widget(
            addChildFlags_Widget(
                tmpGroup,
                iClob(new_LabelWidget(uiTextCaution_ColorEscape "\u26a0  ${dlg.newident.notsaved}", NULL)),
                hidden_WidgetFlag | frameless_WidgetFlag),
            "ident.temp.note");
        addChild_Widget(values, iClob(tmpGroup));
    }
    addChild_Widget(headings, iClob(makePadding_Widget(gap_UI)));
    addChild_Widget(values, iClob(makePadding_Widget(gap_UI)));
    addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.newident.email}")));
    setId_Widget(addChild_Widget(values, iClob(inputs[1] = newHint_InputWidget(0, "${hint.newident.optional}"))), "ident.email");
    addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.newident.userid}")));
    setId_Widget(addChild_Widget(values, iClob(inputs[2] = newHint_InputWidget(0, "${hint.newident.optional}"))), "ident.userid");
    addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.newident.domain}")));
    setId_Widget(addChild_Widget(values, iClob(inputs[3] = newHint_InputWidget(0, "${hint.newident.optional}"))), "ident.domain");
    addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.newident.org}")));
    setId_Widget(addChild_Widget(values, iClob(inputs[4] = newHint_InputWidget(0, "${hint.newident.optional}"))), "ident.org");
    addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.newident.country}")));
    setId_Widget(addChild_Widget(values, iClob(inputs[5] = newHint_InputWidget(0, "${hint.newident.optional}"))), "ident.country");
    arrange_Widget(dlg);
    for (size_t i = 0; i < iElemCount(inputs); ++i) {
        as_Widget(inputs[i])->rect.size.x = 100 * gap_UI - headings->rect.size.x;
    }
    addChild_Widget(
        dlg,
        iClob(makeDialogButtons_Widget((iMenuItem[]){ { "${cancel}", 0, 0, NULL },
                                                { uiTextAction_ColorEscape "${dlg.newident.create}",
                                                  SDLK_RETURN,
                                                  KMOD_PRIMARY,
                                                  "ident.accept" } },
                                 2)));
    addChild_Widget(get_Window()->root, iClob(dlg));
    finalizeSheet_Widget(dlg);
    return dlg;
}

static const iMenuItem languages[] = {
    { "${lang.ar}", 0, 0, "xlt.lang id:ar" },
    { "${lang.zh}", 0, 0, "xlt.lang id:zh" },
    { "${lang.en}", 0, 0, "xlt.lang id:en" },
    { "${lang.fr}", 0, 0, "xlt.lang id:fr" },
    { "${lang.de}", 0, 0, "xlt.lang id:de" },
    { "${lang.hi}", 0, 0, "xlt.lang id:hi" },
    { "${lang.it}", 0, 0, "xlt.lang id:it" },
    { "${lang.ja}", 0, 0, "xlt.lang id:ja" },
    { "${lang.pt}", 0, 0, "xlt.lang id:pt" },
    { "${lang.ru}", 0, 0, "xlt.lang id:ru" },
    { "${lang.es}", 0, 0, "xlt.lang id:es" },
};

static iBool translationHandler_(iWidget *dlg, const char *cmd) {
    iUnused(dlg);
    if (equal_Command(cmd, "xlt.lang")) {
        iLabelWidget *menuItem = pointer_Command(cmd);
        iWidget *button = parent_Widget(parent_Widget(menuItem));
        iAssert(isInstance_Object(button, &Class_LabelWidget));
        updateText_LabelWidget((iLabelWidget *) button, text_LabelWidget(menuItem));
        return iTrue;
    }
    return iFalse;
}

const char *languageId_String(const iString *menuItemLabel) {
    iForIndices(i, languages) {
        if (!cmp_String(menuItemLabel, translateCStr_Lang(languages[i].label))) {
            return cstr_Rangecc(range_Command(languages[i].command, "id"));
        }
    }
    return "";
}

int languageIndex_CStr(const char *langId) {
    iForIndices(i, languages) {
        if (equal_Rangecc(range_Command(languages[i].command, "id"), langId)) {
            return i;
        }
    }
    return -1;
}

iWidget *makeTranslation_Widget(iWidget *parent) {
    iWidget *dlg = makeSheet_Widget("xlt");
    setFlags_Widget(dlg, keepOnTop_WidgetFlag, iFalse);
    setCommandHandler_Widget(dlg, translationHandler_);
    addChildFlags_Widget(dlg,
                         iClob(new_LabelWidget(uiHeading_ColorEscape "${heading.translate}", NULL)),
                         frameless_WidgetFlag);
    addChild_Widget(dlg, iClob(makePadding_Widget(lineHeight_Text(uiLabel_FontId))));
    iWidget *headings, *values;
    iWidget *page;
    addChild_Widget(dlg, iClob(page = makeTwoColumnWidget_(&headings, &values)));
    setId_Widget(page, "xlt.langs");
    iLabelWidget *fromLang, *toLang;
    /* Source language. */ {
        addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.translate.from}")));
        setId_Widget(
            addChildFlags_Widget(values,
                                 iClob(fromLang = makeMenuButton_LabelWidget(
                                           "${lang.pt}", languages, iElemCount(languages))),
                                 alignLeft_WidgetFlag),
            "xlt.from");
        iWidget *langMenu = findChild_Widget(as_Widget(fromLang), "menu");
        updateText_LabelWidget(fromLang,
                               text_LabelWidget(child_Widget(langMenu, prefs_App()->langFrom)));
        setBackgroundColor_Widget(langMenu, uiBackgroundMenu_ColorId);
    }
    /* Target language. */ {
        addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.translate.to}")));
        setId_Widget(addChildFlags_Widget(values,
                                          iClob(toLang = makeMenuButton_LabelWidget(
                                                    "${lang.pt}", languages, iElemCount(languages))),
                                          alignLeft_WidgetFlag),
                     "xlt.to");
        iWidget *langMenu = findChild_Widget(as_Widget(toLang), "menu");
        setBackgroundColor_Widget(langMenu, uiBackgroundMenu_ColorId);
        updateText_LabelWidget(toLang,
                               text_LabelWidget(child_Widget(langMenu, prefs_App()->langTo)));
    }
    addChild_Widget(dlg, iClob(makePadding_Widget(lineHeight_Text(uiLabel_FontId))));
    addChild_Widget(
        dlg,
        iClob(makeDialogButtons_Widget(
            (iMenuItem[]){
                { "${cancel}", SDLK_ESCAPE, 0, "translation.cancel" },
                { uiTextAction_ColorEscape "${dlg.translate}", SDLK_RETURN, 0, "translation.submit" } },
            2)));
    addChild_Widget(parent, iClob(dlg));
    arrange_Widget(dlg);
    finalizeSheet_Widget(dlg);
    return dlg;
}
