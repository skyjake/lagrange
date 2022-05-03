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

#include "bindingswidget.h"
#include "listwidget.h"
#include "keys.h"
#include "command.h"
#include "util.h"
#include "app.h"
#include "lang.h"
#if defined (iPlatformAppleDesktop)
#   include "macos.h"
#endif

iDeclareType(BindingItem)
typedef iListItemClass iBindingItemClass;

struct Impl_BindingItem {
    iListItem listItem;
    iString   label;
    iString   key;
    int       id;
    iBool     isWaitingForEvent;
};

void init_BindingItem(iBindingItem *d) {
    init_ListItem(&d->listItem);
    init_String(&d->label);
    init_String(&d->key);
    d->id = 0;
    d->isWaitingForEvent = iFalse;
}

void deinit_BindingItem(iBindingItem *d) {
    deinit_String(&d->key);
    deinit_String(&d->label);
}

static void setKey_BindingItem_(iBindingItem *d, int key, int mods) {
    setKey_Binding(d->id, key, mods);
    clear_String(&d->key);
    toString_Sym(key, mods, &d->key);
}

static void draw_BindingItem_(const iBindingItem *d, iPaint *p, iRect itemRect,
                              const iListWidget *list);

iBeginDefineSubclass(BindingItem, ListItem)
    .draw = (iAny *) draw_BindingItem_,
iEndDefineSubclass(BindingItem)

iDefineObjectConstruction(BindingItem)

/*----------------------------------------------------------------------------------------------*/

struct Impl_BindingsWidget {
    iWidget widget;
    iListWidget *list;
    size_t activePos;
    size_t contextPos;
    iWidget *menu;
};

iDefineObjectConstruction(BindingsWidget)

static int cmpId_BindingItem_(const iListItem **item1, const iListItem **item2) {
    const iBindingItem *d = (const iBindingItem *) *item1, *other = (const iBindingItem *) *item2;
    return iCmp(d->id, other->id);
}

static void updateItems_BindingsWidget_(iBindingsWidget *d) {
    clear_ListWidget(d->list);
    iConstForEach(PtrArray, i, list_Keys()) {
        const iBinding *bind = i.ptr;
        if (isEmpty_String(&bind->label)) {
            /* Only the ones with label are user-changeable. */
            continue;
        }
        iBindingItem *item = new_BindingItem();
        item->id = bind->id;
        set_String(&item->label, &bind->label);
        translate_Lang(&item->label);
        toString_Sym(bind->key, bind->mods, &item->key);
        addItem_ListWidget(d->list, item);
    }
    sort_ListWidget(d->list, cmpId_BindingItem_);
    updateVisible_ListWidget(d->list);
    invalidate_ListWidget(d->list);
}

void init_BindingsWidget(iBindingsWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, "bindings");
    setFlags_Widget(w, resizeChildren_WidgetFlag, iTrue);
    d->activePos = iInvalidPos;
    d->contextPos = iInvalidPos;
    d->list = new_ListWidget();
    setItemHeight_ListWidget(d->list, lineHeight_Text(uiLabel_FontId) * 1.5f);
    setPadding_Widget(as_Widget(d->list), 0, gap_UI, 0, gap_UI);
    addChild_Widget(w, iClob(d->list));
    updateItems_BindingsWidget_(d);
    d->menu = makeMenu_Widget(
        w,
        (iMenuItem[]){ { "${menu.binding.reset}", 0, 0, "binding.reset" },
                       { uiTextCaution_ColorEscape "${menu.binding.clear}", 0, 0, "binding.clear" } },
        2);
}

void deinit_BindingsWidget(iBindingsWidget *d) {
    /* nothing to do */
    iUnused(d);
}

static void setActiveItem_BindingsWidget_(iBindingsWidget *d, size_t pos) {
    if (d->activePos != iInvalidPos) {
        iBindingItem *item = item_ListWidget(d->list, d->activePos);
        item->isWaitingForEvent = iFalse;
        invalidateItem_ListWidget(d->list, d->activePos);
    }
    d->activePos = pos;
    if (d->activePos != iInvalidPos) {
        iBindingItem *item = item_ListWidget(d->list, d->activePos);
        item->isWaitingForEvent = iTrue;
        invalidateItem_ListWidget(d->list, d->activePos);
    }
    setScrollMode_ListWidget(d->list, d->activePos != iInvalidPos);
#if defined (iPlatformAppleDesktop) && defined (LAGRANGE_MAC_CONTEXTMENU)
    /* Native menus must be disabled while grabbing keys so the shortcuts don't trigger. */
    const iBool enableNativeMenus = (d->activePos == iInvalidPos);
    enableMenu_MacOS("${menu.title.file}", enableNativeMenus);
    enableMenu_MacOS("${menu.title.edit}", enableNativeMenus);
    enableMenu_MacOS("${menu.title.view}", enableNativeMenus);
    enableMenu_MacOS("${menu.title.bookmarks}", enableNativeMenus);
    enableMenu_MacOS("${menu.title.identity}", enableNativeMenus);
    enableMenuIndex_MacOS(6, enableNativeMenus);
    enableMenuIndex_MacOS(7, enableNativeMenus);
#endif
}

static iBool processEvent_BindingsWidget_(iBindingsWidget *d, const SDL_Event *ev) {
    iWidget *   w   = as_Widget(d);
    const char *cmd = command_UserEvent(ev);
    if (isCommand_Widget(w, ev, "list.clicked")) {
        const size_t index = (size_t) arg_Command(cmd);
        setActiveItem_BindingsWidget_(d, d->activePos != index ? index : iInvalidPos);
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "menu.closed")) {
        invalidateItem_ListWidget(d->list, d->contextPos);
    }
    else if (isCommand_Widget(w, ev, "binding.reset")) {
        iBindingItem *item = item_ListWidget(d->list, d->contextPos);
        if (item) {
            reset_Binding(item->id);
            updateItems_BindingsWidget_(d);
            postCommand_App("bindings.changed");
        }
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "binding.clear")) {
        setKey_BindingItem_(item_ListWidget(d->list, d->contextPos), 0, 0);
        invalidateItem_ListWidget(d->list, d->contextPos);
        d->contextPos = iInvalidPos;
        postCommand_App("bindings.changed");
        return iTrue;
    }
    else if (equalArg_Command(cmd, "tabs.changed", "id", "bindings")) {
        /* Force the scrollbar to unfade. The list is created hidden so the scrollbar is not
           shown by default.*/
        updateVisible_ListWidget(d->list);
        if (isTerminal_Platform()) {
            setFocus_Widget(as_Widget(d->list));
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "lang.changed")) {
        updateItems_BindingsWidget_(d);
        return iFalse;
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_RIGHT) {
        if (!isVisible_Widget(d->menu)) {
            d->contextPos = hoverItemIndex_ListWidget(d->list);
        }
    }
    if (d->contextPos != iInvalidPos) {
        processContextMenuEvent_Widget(d->menu, ev, {
            setActiveItem_BindingsWidget_(d, iInvalidPos);
        });
    }
    /* Waiting for a keypress? */
    if (d->activePos != iInvalidPos) {
        if (ev->type == SDL_KEYDOWN && !isMod_Sym(ev->key.keysym.sym)) {
            setKey_BindingItem_(item_ListWidget(d->list, d->activePos),
                                ev->key.keysym.sym,
                                keyMods_Sym(ev->key.keysym.mod));
            setActiveItem_BindingsWidget_(d, iInvalidPos);
            postCommand_App("bindings.changed");
            return iTrue;
        }
        else if (ev->type == SDL_KEYUP && isMod_Sym(ev->key.keysym.sym)) {
            setKey_BindingItem_(item_ListWidget(d->list, d->activePos), ev->key.keysym.sym, 0);
            setActiveItem_BindingsWidget_(d, iInvalidPos);
            postCommand_App("bindings.changed");
            return iTrue;
        }
    }
    return processEvent_Widget(w, ev);
}

static void draw_BindingsWidget_(const iBindingsWidget *d) {
    const iWidget *w = constAs_Widget(d);
    drawChildren_Widget(w);
    drawBackground_Widget(w); /* kludge to allow drawing a top border over the list */
}

static void draw_BindingItem_(const iBindingItem *d, iPaint *p, iRect itemRect,
                              const iListWidget *list) {
    const int   font       = uiLabel_FontId;
    const int   itemHeight = height_Rect(itemRect);
    const int   line       = lineHeight_Text(font);
    int         fg         = uiText_ColorId;
    const iBool isPressing = isMouseDown_ListWidget(list) || d->isWaitingForEvent;
    const iBindingsWidget *parent = (const iBindingsWidget *) parent_Widget(list);
    const iBool isMenuOpen = isVisible_Widget(parent->menu);
    const iBool isHover = ((!isMenuOpen && isHover_Widget(constAs_Widget(list)) &&
                            constHoverItem_ListWidget(list) == d) ||
                           (isMenuOpen && constItem_ListWidget(list, parent->contextPos) == d));
    const iBool isCursor = isFocused_Widget(list) && constCursorItem_ListWidget(list) == d;
    if (isHover || isPressing || isCursor) {
        fg = isPressing ? uiTextPressed_ColorId : uiTextFramelessHover_ColorId;
        fillRect_Paint(p,
                       itemRect,
                       isPressing ? uiBackgroundPressed_ColorId
                                  : uiBackgroundFramelessHover_ColorId);
    }
    const int y = top_Rect(itemRect) + (itemHeight - line) / 2;
    drawRange_Text(font,
                   init_I2(left_Rect(itemRect) + 3 * gap_UI, y),
                   fg,
                   range_String(&d->label));
    drawAlign_Text(d->isWaitingForEvent ? uiContent_FontId : font,
                   init_I2(right_Rect(itemRect) - 6 * gap_UI,
                           y - (lineHeight_Text(uiContent_FontId) - line) / 2),
                   fg,
                   right_Alignment,
                   "%s",
                   d->isWaitingForEvent ? "\U0001F449 \u2328" : cstr_String(&d->key));
}

iBeginDefineSubclass(BindingsWidget, Widget)
    .processEvent = (iAny *) processEvent_BindingsWidget_,
    .draw         = (iAny *) draw_BindingsWidget_,
iEndDefineSubclass(BindingsWidget)
