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
#include "gmutil.h"
#include "labelwidget.h"
#include "inputwidget.h"
#include "keys.h"
#include "widget.h"
#include "text.h"
#include "window.h"

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
    if (key == 0x20) {
        appendCStr_String(str, "Space");
    }
    else if (key == SDLK_LEFT) {
        appendChar_String(str, 0x2190);
    }
    else if (key == SDLK_RIGHT) {
        appendChar_String(str, 0x2192);
    }
    else if (key < 128 && (isalnum(key) || ispunct(key))) {
        appendChar_String(str, upper_Char(key));
    }
    else if (key == SDLK_BACKSPACE) {
        appendChar_String(str, 0x232b); /* Erase to the Left */
    }
    else if (key == SDLK_DELETE) {
        appendChar_String(str, 0x2326); /* Erase to the Right */
    }
    else {
        appendCStr_String(str, SDL_GetKeyName(key));
    }
}

int keyMods_Sym(int kmods) {
    kmods &= (KMOD_SHIFT | KMOD_ALT | KMOD_CTRL | KMOD_GUI);
    /* Don't treat left/right modifiers differently. */
    if (kmods & KMOD_SHIFT) kmods |= KMOD_SHIFT;
    if (kmods & KMOD_ALT)   kmods |= KMOD_ALT;
    if (kmods & KMOD_CTRL)  kmods |= KMOD_CTRL;
    if (kmods & KMOD_GUI)   kmods |= KMOD_GUI;
    return kmods;
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

/*----------------------------------------------------------------------------------------------*/

iBool isFinished_Anim(const iAnim *d) {
    return frameTime_Window(get_Window()) >= d->due;
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
    if ((d->flags & easeBoth_AnimFlag) == easeBoth_AnimFlag) {
        t = easeBoth_(t);
    }
    else if (d->flags & easeIn_AnimFlag) {
        t = easeIn_(t);
    }
    else if (d->flags & easeOut_AnimFlag) {
        t = easeOut_(t);
    }
    return d->from * (1.0f - t) + d->to * t;
}

void setValue_Anim(iAnim *d, float to, uint32_t span) {
    if (span == 0) {
        d->from = d->to = to;
        d->when = d->due = SDL_GetTicks();
    }
    else if (fabsf(to - d->to) > 0.00001f) {
        const uint32_t now = SDL_GetTicks();
        d->from = valueAt_Anim_(d, now);
        d->to   = to;
        d->when = now;
        d->due  = now + span;
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
    if (event->type == SDL_MOUSEBUTTONDOWN && mb->clicks == 2) {
        if (contains_Widget(d->bounds, pos)) {
            d->pos = pos;
            setMouseGrab_Widget(NULL);
            return double_ClickResult;
        }
    }
    if (!d->isActive) {
        if (mb->state == SDL_PRESSED) {
            if (contains_Widget(d->bounds, pos)) {
                d->isActive = iTrue;
                d->startPos = d->pos = pos;
                //setFlags_Widget(d->bounds, hover_WidgetFlag, iFalse);
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
    setSize_Widget(pad, init1_I2(size));
    return pad;
}

iLabelWidget *makeHeading_Widget(const char *text) {
    iLabelWidget *heading = new_LabelWidget(text, NULL);
    setFlags_Widget(as_Widget(heading), frameless_WidgetFlag | fixedSize_WidgetFlag, iTrue);
    setBackgroundColor_Widget(as_Widget(heading), none_ColorId);
    return heading;
}

iWidget *makeVDiv_Widget(void) {
    iWidget *div = new_Widget();
    setFlags_Widget(div, resizeChildren_WidgetFlag | arrangeVertical_WidgetFlag, iTrue);
    return div;
}

iWidget *makeHDiv_Widget(void) {
    iWidget *div = new_Widget();
    setFlags_Widget(div, resizeChildren_WidgetFlag | arrangeHorizontal_WidgetFlag, iTrue);
    return div;
}

iWidget *addAction_Widget(iWidget *parent, int key, int kmods, const char *command) {
    iLabelWidget *action = newKeyMods_LabelWidget("", key, kmods, command);
    setSize_Widget(as_Widget(action), zero_I2());
    addChildFlags_Widget(parent, iClob(action), hidden_WidgetFlag);
    return as_Widget(action);
}

/*-----------------------------------------------------------------------------------------------*/

static iBool isCommandIgnoredByMenus_(const char *cmd) {
    return equal_Command(cmd, "media.updated") || equal_Command(cmd, "document.request.updated") ||
           equal_Command(cmd, "window.resized") ||
           (equal_Command(cmd, "mouse.clicked") && !arg_Command(cmd)); /* button released */
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

iWidget *makeMenu_Widget(iWidget *parent, const iMenuItem *items, size_t n) {
    iWidget *menu = new_Widget();
    setFrameColor_Widget(menu, uiSeparator_ColorId);
    setBackgroundColor_Widget(menu, uiBackground_ColorId);
    setFlags_Widget(menu,
                    keepOnTop_WidgetFlag | collapse_WidgetFlag | hidden_WidgetFlag |
                        arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag |
                        resizeChildrenToWidestChild_WidgetFlag,
                    iTrue);
    for (size_t i = 0; i < n; ++i) {
        const iMenuItem *item = &items[i];
        if (equal_CStr(item->label, "---")) {
            iWidget *sep = addChild_Widget(menu, iClob(new_Widget()));
            setBackgroundColor_Widget(sep, uiSeparator_ColorId);
            sep->rect.size.y = gap_UI / 3;
            setFlags_Widget(sep, hover_WidgetFlag | fixedHeight_WidgetFlag, iTrue);
        }
        else {
            iLabelWidget *label = addChildFlags_Widget(
                menu,
                iClob(newKeyMods_LabelWidget(item->label, item->key, item->kmods, item->command)),
                frameless_WidgetFlag | alignLeft_WidgetFlag | drawKey_WidgetFlag);
            updateSize_LabelWidget(label); /* drawKey was set */
        }
    }
    addChild_Widget(parent, iClob(menu));
    setCommandHandler_Widget(menu, menuHandler_);
    iWidget *cancel = addAction_Widget(menu, SDLK_ESCAPE, 0, "cancel");
    setId_Widget(cancel, "menu.cancel");
    setFlags_Widget(cancel, disabled_WidgetFlag, iTrue);
    return menu;
}

void openMenu_Widget(iWidget *d, iInt2 coord) {
    /* Menu closes when commands are emitted, so handle any pending ones beforehand. */
    postCommand_App("cancel"); /* dismiss any other menus */
    processEvents_App(postedEventsOnly_AppEventMode);
    setFlags_Widget(d, hidden_WidgetFlag, iFalse);
    setFlags_Widget(d, commandOnMouseMiss_WidgetFlag, iTrue);
    setFlags_Widget(findChild_Widget(d, "menu.cancel"), disabled_WidgetFlag, iFalse);
    arrange_Widget(d);
    d->rect.pos = coord;
    /* Ensure the full menu is visible. */
    const iInt2 rootSize     = rootSize_Window(get_Window());
    const iRect bounds       = bounds_Widget(d);
    const int   leftExcess   = -left_Rect(bounds);
    const int   rightExcess  = right_Rect(bounds) - rootSize.x;
    const int   topExcess    = -top_Rect(bounds);
    const int   bottomExcess = bottom_Rect(bounds) - rootSize.y;
    if (bottomExcess > 0) {
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
    refresh_App();
    postCommand_Widget(d, "menu.opened");
}

void closeMenu_Widget(iWidget *d) {
    setFlags_Widget(d, hidden_WidgetFlag, iTrue);
    setFlags_Widget(findChild_Widget(d, "menu.cancel"), disabled_WidgetFlag, iTrue);
    refresh_App();
    postCommand_Widget(d, "menu.closed");
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
    setFlags_Widget(button, commandOnClick_WidgetFlag | expand_WidgetFlag, iTrue);
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
    arrange_Widget(tabs);
    iInt2 largest = zero_I2();
    iWidget *pages = findChild_Widget(tabs, "tabs.pages");
    iConstForEach(ObjectList, i, children_Widget(pages)) {
        largest = max_I2(largest, ((const iWidget *) i.object)->rect.size);
    }
    iForEach(ObjectList, j, children_Widget(pages)) {
        setSize_Widget(j.object, largest);
    }
    setSize_Widget(tabs, addY_I2(largest, height_Widget(findChild_Widget(tabs, "tabs.buttons"))));
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
    setPadding1_Widget(sheet, 3 * gap_UI);
    setId_Widget(sheet, id);
    setFrameColor_Widget(sheet, uiSeparator_ColorId);
    setBackgroundColor_Widget(sheet, uiBackground_ColorId);
    setFlags_Widget(sheet,
                    mouseModal_WidgetFlag | keepOnTop_WidgetFlag | arrangeVertical_WidgetFlag |
                        arrangeSize_WidgetFlag | centerHorizontal_WidgetFlag,
                    iTrue);
    return sheet;
}

void centerSheet_Widget(iWidget *sheet) {
    arrange_Widget(sheet->parent);
//    const iInt2 rootSize = rootSize_Window(get_Window());
//    const iInt2 orig     = localCoord_Widget(
//        sheet->parent,
//        init_I2(rootSize.x / 2 - sheet->rect.size.x / 2, bounds_Widget(sheet).pos.y));
//    sheet->rect.pos = orig;
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
        addChild_Widget(div, iClob(newKeyMods_LabelWidget("Cancel", SDLK_ESCAPE, 0, "filepath.cancel")));
        addChild_Widget(div, iClob(newKeyMods_LabelWidget(acceptLabel, SDLK_RETURN, 0, "filepath.accept")));
    }
    addChild_Widget(dlg, iClob(div));
    centerSheet_Widget(dlg);
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
    const iInt2 rootSize = rootSize_Window(get_Window());
    iWidget *   title    = findChild_Widget(dlg, "valueinput.title");
    iWidget *   prompt   = findChild_Widget(dlg, "valueinput.prompt");
    dlg->rect.size.x     = iMaxi(iMaxi(rootSize.x / 2, title->rect.size.x), prompt->rect.size.x);
    as_Widget(findChild_Widget(dlg, "input"))->rect.size.x = dlg->rect.size.x;
    centerSheet_Widget(dlg);
}

iBool valueInputHandler_(iWidget *dlg, const char *cmd) {
    iWidget *ptr = as_Widget(pointer_Command(cmd));
    if (equal_Command(cmd, "window.resized")) {
        if (isVisible_Widget(dlg)) {
            updateValueInputWidth_(dlg);
        }
        return iFalse;
    }
    if (equal_Command(cmd, "input.ended")) {
        if (hasParent_Widget(ptr, dlg)) {
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

iWidget *makeValueInput_Widget(iWidget *parent, const iString *initialValue, const char *title,
                               const char *prompt, const char *acceptLabel, const char *command) {
    if (parent) {
        setFocus_Widget(NULL);
//        processEvents_App(postedEventsOnly_AppEventMode);
    }
    iWidget *dlg = makeSheet_Widget(command);
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
    iInputWidget *input = addChild_Widget(dlg, iClob(new_InputWidget(0)));
    if (initialValue) {
        setText_InputWidget(input, initialValue);
    }
    setId_Widget(as_Widget(input), "input");
    updateValueInputWidth_(dlg);
    addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
    iWidget *div = new_Widget(); {
        setFlags_Widget(div, arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
        addChild_Widget(div, iClob(newKeyMods_LabelWidget("Cancel", SDLK_ESCAPE, 0, "cancel")));
        addChild_Widget(
            div,
            iClob(newKeyMods_LabelWidget(acceptLabel ? acceptLabel : uiTextAction_ColorEscape "OK",
                                         SDLK_RETURN,
                                         0,
                                         "valueinput.accept")));
    }
    addChild_Widget(dlg, iClob(div));
    centerSheet_Widget(dlg);
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
    if (!(equal_Command(cmd, "media.updated") || equal_Command(cmd, "document.request.updated"))) {
        destroy_Widget(msg);
    }
    return iFalse;
}

iWidget *makeMessage_Widget(const char *title, const char *msg) {
    iWidget *dlg = makeQuestion_Widget(
        title, msg, (const char *[]){ "Continue" }, (const char *[]){ "message.ok" }, 1);
    addAction_Widget(dlg, SDLK_ESCAPE, 0, "message.ok");
    addAction_Widget(dlg, SDLK_SPACE, 0, "message.ok");
    return dlg;
}

iWidget *makeQuestion_Widget(const char *title, const char *msg, const char *labels[],
                             const char *commands[], size_t count) {
    processEvents_App(postedEventsOnly_AppEventMode);
    iWidget *dlg = makeSheet_Widget("");
    setCommandHandler_Widget(dlg, messageHandler_);
    addChildFlags_Widget(dlg, iClob(new_LabelWidget(title, NULL)), frameless_WidgetFlag);
    addChildFlags_Widget(dlg, iClob(new_LabelWidget(msg, NULL)), frameless_WidgetFlag);
    addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
    iWidget *div = new_Widget(); {
        setFlags_Widget(div, arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
        for (size_t i = 0; i < count; ++i) {
            /* The last one is the default option. */
            const int key = (i == count - 1 ? SDLK_RETURN : 0);
            addChild_Widget(div, iClob(newKeyMods_LabelWidget(labels[i], key, 0, commands[i])));
        }
    }
    addChild_Widget(dlg, iClob(div));
    addChild_Widget(get_Window()->root, iClob(dlg));
    centerSheet_Widget(dlg);
    return dlg;
}

void setToggle_Widget(iWidget *d, iBool active) {
    if (d) {
        setFlags_Widget(d, selected_WidgetFlag, active);
        updateText_LabelWidget((iLabelWidget *) d,
                               collectNewFormat_String("%s", isSelected_Widget(d) ? "YES" : "NO"));
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
    return iFalse;
}

iWidget *makeToggle_Widget(const char *id) {
    iWidget *toggle = as_Widget(new_LabelWidget("YES", "toggle")); /* "YES" for sizing */
    setId_Widget(toggle, id);
    updateTextCStr_LabelWidget((iLabelWidget *) toggle, "NO"); /* actual initial value */
    setCommandHandler_Widget(toggle, toggleHandler_);
    return toggle;
}

static iWidget *appendTwoColumnPage_(iWidget *tabs, const char *title, int shortcut, iWidget **headings,
                                     iWidget **values) {
    iWidget *page = new_Widget();
    setFlags_Widget(page, arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag |
                    resizeHeightOfChildren_WidgetFlag | borderTop_WidgetFlag, iTrue);
    addChildFlags_Widget(page, iClob(new_Widget()), expand_WidgetFlag);
    iWidget *columns = new_Widget();
    addChildFlags_Widget(page, iClob(columns), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
    *headings = addChildFlags_Widget(
        columns, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    *values = addChildFlags_Widget(
        columns, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    addChildFlags_Widget(page, iClob(new_Widget()), expand_WidgetFlag);
    appendTabPage_Widget(tabs, page, title, shortcut, shortcut ? KMOD_PRIMARY : 0);
    setFlags_Widget(
        (iWidget *) back_ObjectList(children_Widget(findChild_Widget(tabs, "tabs.buttons"))),
        frameless_WidgetFlag,
        iTrue);
    return page;
}

static void makeTwoColumnHeading_(const char *title, iWidget *headings, iWidget *values) {
    addChild_Widget(headings,
                    iClob(makeHeading_Widget(format_CStr(uiHeading_ColorEscape "%s", title))));
    addChild_Widget(values, iClob(makeHeading_Widget("")));
}

static void expandInputFieldWidth_(iInputWidget *input) {
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
        "Nunito", "Fira Sans", "Literata", "EB Garamond"
    };
    iForIndices(i, fontNames) {
        addRadioButton_(parent,
                        format_CStr("prefs.%s.%u", id, i),
                        fontNames[i],
                        format_CStr("%s.set arg:%u", id, i));
    }
}

iWidget *makePreferences_Widget(void) {
    iWidget *dlg = makeSheet_Widget("prefs");
    addChildFlags_Widget(dlg,
                         iClob(new_LabelWidget(uiHeading_ColorEscape "PREFERENCES", NULL)),
                         frameless_WidgetFlag);
    iWidget *tabs = makeTabs_Widget(dlg);
    setId_Widget(tabs, "prefs.tabs");
    iWidget *headings, *values;
    /* General preferences. */ {
        appendTwoColumnPage_(tabs, "General", '1', &headings, &values);
        addChild_Widget(headings, iClob(makeHeading_Widget("Downloads folder:")));
        setId_Widget(addChild_Widget(values, iClob(new_InputWidget(0))), "prefs.downloads");
        addChild_Widget(headings, iClob(makeHeading_Widget("Outline on scrollbar:")));
        addChild_Widget(values, iClob(makeToggle_Widget("prefs.hoveroutline")));
        makeTwoColumnHeading_("WINDOW", headings, values);
#if defined (iPlatformApple) || defined (iPlatformMSys)
        addChild_Widget(headings, iClob(makeHeading_Widget("Use system theme:")));
        addChild_Widget(values, iClob(makeToggle_Widget("prefs.ostheme")));
#endif
        addChild_Widget(headings, iClob(makeHeading_Widget("Theme:")));
        iWidget *themes = new_Widget();
        /* Themes. */ {
            setId_Widget(addChild_Widget(themes, iClob(new_LabelWidget("Pure Black", "theme.set arg:0"))), "prefs.theme.0");
            setId_Widget(addChild_Widget(themes, iClob(new_LabelWidget("Dark", "theme.set arg:1"))), "prefs.theme.1");
            setId_Widget(addChild_Widget(themes, iClob(new_LabelWidget("Light", "theme.set arg:2"))), "prefs.theme.2");
            setId_Widget(addChild_Widget(themes, iClob(new_LabelWidget("Pure White", "theme.set arg:3"))), "prefs.theme.3");
        }
        addChildFlags_Widget(values, iClob(themes), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        addChild_Widget(headings, iClob(makeHeading_Widget("Retain window size:")));
        addChild_Widget(values, iClob(makeToggle_Widget("prefs.retainwindow")));
        addChild_Widget(headings, iClob(makeHeading_Widget("UI scale factor:")));
        setId_Widget(addChild_Widget(values, iClob(new_InputWidget(8))), "prefs.uiscale");
    }
    /* Colors. */ {
        appendTwoColumnPage_(tabs, "Colors", '2', &headings, &values);
        makeTwoColumnHeading_("PAGE CONTENTS", headings, values);
        for (int i = 0; i < 2; ++i) {
            const iBool isDark = (i == 0);
            const char *mode = isDark ? "dark" : "light";
            const iMenuItem themes[] = {
                { "Colorful Dark", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, colorfulDark_GmDocumentTheme) },
                { "Colorful Light", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, colorfulLight_GmDocumentTheme) },
                { "Black", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, black_GmDocumentTheme) },
                { "Gray", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, gray_GmDocumentTheme) },
                { "White", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, white_GmDocumentTheme) },
                { "Sepia", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, sepia_GmDocumentTheme) },
                { "High Contrast", 0, 0, format_CStr("doctheme.%s.set arg:%d", mode, highContrast_GmDocumentTheme) },
            };
            addChild_Widget(headings, iClob(makeHeading_Widget(isDark ? "Dark theme:" : "Light theme:")));
            setId_Widget(addChild_Widget(values,
                                         iClob(makeMenuButton_LabelWidget(
                                             themes[0].label, themes, iElemCount(themes)))),
                         format_CStr("prefs.doctheme.%s", mode));
        }
        addChild_Widget(headings, iClob(makeHeading_Widget("Saturation:")));
        iWidget *sats = new_Widget();
        /* Saturation levels. */ {
            addRadioButton_(sats, "prefs.saturation.3", "Full", "saturation.set arg:100");
            addRadioButton_(sats, "prefs.saturation.2", "Reduced", "saturation.set arg:66");
            addRadioButton_(sats, "prefs.saturation.1", "Minimal", "saturation.set arg:33");
            addRadioButton_(sats, "prefs.saturation.0", "Monochrome", "saturation.set arg:0");
        }
        addChildFlags_Widget(values, iClob(sats), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
    }
    /* Layout. */ {
        appendTwoColumnPage_(tabs, "Style", '3', &headings, &values);
        /* Fonts. */ {
            iWidget *fonts;
            addChild_Widget(headings, iClob(makeHeading_Widget("Heading font:")));
            fonts = new_Widget();
            addFontButtons_(fonts, "headingfont");
            addChildFlags_Widget(values, iClob(fonts), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
            addChild_Widget(headings, iClob(makeHeading_Widget("Body font:")));
            fonts = new_Widget();
            addFontButtons_(fonts, "font");
            addChildFlags_Widget(values, iClob(fonts), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        }
        addChild_Widget(headings, iClob(makePadding_Widget(2 * gap_UI)));
        addChild_Widget(values, iClob(makePadding_Widget(2 * gap_UI)));
        addChild_Widget(headings, iClob(makeHeading_Widget("Line width:")));
        iWidget *widths = new_Widget();
        /* Line widths. */ {
            addRadioButton_(widths, "prefs.linewidth.30", "\u20132", "linewidth.set arg:30");
            addRadioButton_(widths, "prefs.linewidth.35", "\u20131", "linewidth.set arg:35");
            addRadioButton_(widths, "prefs.linewidth.40", "Normal", "linewidth.set arg:40");
            addRadioButton_(widths, "prefs.linewidth.45", "+1", "linewidth.set arg:45");
            addRadioButton_(widths, "prefs.linewidth.50", "+2", "linewidth.set arg:50");
            addRadioButton_(widths, "prefs.linewidth.1000", "Window", "linewidth.set arg:1000");
        }
        addChildFlags_Widget(values, iClob(widths), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        addChild_Widget(headings, iClob(makeHeading_Widget("Quote indicator:")));
        iWidget *quote = new_Widget(); {
            addRadioButton_(quote, "prefs.quoteicon.1", "Icon", "quoteicon.set arg:1");
            addRadioButton_(quote, "prefs.quoteicon.0", "Line", "quoteicon.set arg:0");
        }
        addChildFlags_Widget(values, iClob(quote), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        addChild_Widget(headings, iClob(makeHeading_Widget("Big 1st paragaph:")));
        addChild_Widget(values, iClob(makeToggle_Widget("prefs.biglede")));
        makeTwoColumnHeading_("WIDE LAYOUT", headings, values);
        addChild_Widget(headings, iClob(makeHeading_Widget("Site icon:")));
        addChild_Widget(values, iClob(makeToggle_Widget("prefs.sideicon")));
    }
    /* Proxies. */ {
        appendTwoColumnPage_(tabs, "Proxies", '4', &headings, &values);
        addChild_Widget(headings, iClob(makeHeading_Widget("Gopher proxy:")));
        setId_Widget(addChild_Widget(values, iClob(new_InputWidget(0))), "prefs.proxy.gopher");
        addChild_Widget(headings, iClob(makeHeading_Widget("HTTP proxy:")));
        setId_Widget(addChild_Widget(values, iClob(new_InputWidget(0))), "prefs.proxy.http");
    }
    resizeToLargestPage_Widget(tabs);
    arrange_Widget(dlg);
    /* Set input field sizes. */ {
        expandInputFieldWidth_(findChild_Widget(tabs, "prefs.downloads"));
        expandInputFieldWidth_(findChild_Widget(tabs, "prefs.proxy.http"));
        expandInputFieldWidth_(findChild_Widget(tabs, "prefs.proxy.gopher"));
    }
    iWidget *div = new_Widget(); {
        setFlags_Widget(div, arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
        addChild_Widget(div, iClob(newKeyMods_LabelWidget("Dismiss", SDLK_ESCAPE, 0, "prefs.dismiss")));
    }
    addChild_Widget(dlg, iClob(div));
    addAction_Widget(dlg, prevTab_KeyShortcut, "tabs.prev");
    addAction_Widget(dlg, nextTab_KeyShortcut, "tabs.next");
    addChild_Widget(get_Window()->root, iClob(dlg));
    centerSheet_Widget(dlg);    
    return dlg;
}

iWidget *makeBookmarkEditor_Widget(void) {
    iWidget *dlg = makeSheet_Widget("bmed");
    setId_Widget(addChildFlags_Widget(
                     dlg,
                     iClob(new_LabelWidget(uiHeading_ColorEscape "EDIT BOOKMARK", NULL)),
                     frameless_WidgetFlag),
                 "bmed.heading");
    iWidget *page = new_Widget();
    addChild_Widget(dlg, iClob(page));
    setFlags_Widget(page, arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
    iWidget *headings = addChildFlags_Widget(
        page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    iWidget *values = addChildFlags_Widget(
        page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    iInputWidget *inputs[3];
    addChild_Widget(headings, iClob(makeHeading_Widget("Title:")));
    setId_Widget(addChild_Widget(values, iClob(inputs[0] = new_InputWidget(0))), "bmed.title");
    addChild_Widget(headings, iClob(makeHeading_Widget("URL:")));
    setId_Widget(addChild_Widget(values, iClob(inputs[1] = new_InputWidget(0))), "bmed.url");
    addChild_Widget(headings, iClob(makeHeading_Widget("Tags:")));
    setId_Widget(addChild_Widget(values, iClob(inputs[2] = new_InputWidget(0))), "bmed.tags");
    arrange_Widget(dlg);
    for (int i = 0; i < 3; ++i) {
        as_Widget(inputs[i])->rect.size.x = 100 * gap_UI - headings->rect.size.x;
    }
    iWidget *div = new_Widget(); {
        setFlags_Widget(div, arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
        addChild_Widget(div, iClob(newKeyMods_LabelWidget("Cancel", SDLK_ESCAPE, 0, "cancel")));
        addChild_Widget(
            div,
            iClob(newKeyMods_LabelWidget(
                uiTextCaution_ColorEscape "Save Bookmark", SDLK_RETURN, KMOD_PRIMARY, "bmed.accept")));
    }
    addChild_Widget(dlg, iClob(div));
    addChild_Widget(get_Window()->root, iClob(dlg));
    centerSheet_Widget(dlg);
    return dlg;
}

static iBool handleBookmarkCreationCommands_SidebarWidget_(iWidget *editor, const char *cmd) {
    if (equal_Command(cmd, "bmed.accept") || equal_Command(cmd, "cancel")) {
        if (equal_Command(cmd, "bmed.accept")) {
            const iString *title = text_InputWidget(findChild_Widget(editor, "bmed.title"));
            const iString *url   = text_InputWidget(findChild_Widget(editor, "bmed.url"));
            const iString *tags  = text_InputWidget(findChild_Widget(editor, "bmed.tags"));
            add_Bookmarks(bookmarks_App(),
                          url,
                          title,
                          tags,
                          first_String(label_LabelWidget(findChild_Widget(editor, "bmed.icon"))));
            postCommand_App("bookmarks.changed");
        }
        destroy_Widget(editor);
        return iTrue;
    }
    return iFalse;
}

iWidget *makeBookmarkCreation_Widget(const iString *url, const iString *title, iChar icon) {
    iWidget *dlg = makeBookmarkEditor_Widget();
    setId_Widget(dlg, "bmed.create");
    setTextCStr_LabelWidget(findChild_Widget(dlg, "bmed.heading"),
                            uiHeading_ColorEscape "ADD BOOKMARK");
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

iWidget *makeIdentityCreation_Widget(void) {
    iWidget *dlg = makeSheet_Widget("ident");
    setId_Widget(addChildFlags_Widget(
                     dlg,
                     iClob(new_LabelWidget(uiHeading_ColorEscape "NEW IDENTITY", NULL)),
                     frameless_WidgetFlag),
                 "ident.heading");
    iWidget *page = new_Widget();
    addChildFlags_Widget(
        dlg,
        iClob(
            new_LabelWidget("Creating a 2048-bit self-signed RSA certificate.", NULL)),
        frameless_WidgetFlag);
    addChild_Widget(dlg, iClob(page));
    setFlags_Widget(page, arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
    iWidget *headings = addChildFlags_Widget(
        page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    iWidget *values = addChildFlags_Widget(
        page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    iInputWidget *inputs[6];
    addChild_Widget(headings, iClob(makeHeading_Widget("Common name:")));
    setId_Widget(addChild_Widget(values, iClob(inputs[0] = new_InputWidget(0))), "ident.common");
    addChild_Widget(headings, iClob(makeHeading_Widget("Email:")));
    setId_Widget(addChild_Widget(values, iClob(inputs[1] = newHint_InputWidget(0, "optional"))), "ident.email");
    addChild_Widget(headings, iClob(makeHeading_Widget("User ID:")));
    setId_Widget(addChild_Widget(values, iClob(inputs[2] = newHint_InputWidget(0, "optional"))), "ident.userid");
    addChild_Widget(headings, iClob(makeHeading_Widget("Domain:")));
    setId_Widget(addChild_Widget(values, iClob(inputs[3] = newHint_InputWidget(0, "optional"))), "ident.domain");
    addChild_Widget(headings, iClob(makeHeading_Widget("Organization:")));
    setId_Widget(addChild_Widget(values, iClob(inputs[4] = newHint_InputWidget(0, "optional"))), "ident.org");
    addChild_Widget(headings, iClob(makeHeading_Widget("Country:")));
    setId_Widget(addChild_Widget(values, iClob(inputs[5] = newHint_InputWidget(0, "optional"))), "ident.country");
    addChild_Widget(headings, iClob(makeHeading_Widget("Valid until:")));
    setId_Widget(addChild_Widget(values, iClob(newHint_InputWidget(19, "YYYY-MM-DD HH:MM:SS"))), "ident.until");
    addChild_Widget(headings, iClob(makeHeading_Widget("Temporary:")));
    addChild_Widget(values, iClob(makeToggle_Widget("ident.temp")));
    arrange_Widget(dlg);
    for (size_t i = 0; i < iElemCount(inputs); ++i) {
        as_Widget(inputs[i])->rect.size.x = 100 * gap_UI - headings->rect.size.x;
    }
    iWidget *div = new_Widget(); {
        setFlags_Widget(div, arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
        addChild_Widget(div, iClob(newKeyMods_LabelWidget("Cancel", SDLK_ESCAPE, 0, "cancel")));
        addChild_Widget(
            div,
            iClob(newKeyMods_LabelWidget(
                uiTextAction_ColorEscape "Create Identity", SDLK_RETURN, KMOD_PRIMARY, "ident.accept")));
    }
    addChild_Widget(dlg, iClob(div));
    addChild_Widget(get_Window()->root, iClob(dlg));
    centerSheet_Widget(dlg);
    return dlg;
}
