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

/* Menus, dropdown buttons, and the menu bar. */

#include <lagrange/gmcerts.h>

#include <the_Foundation/array.h>
#include <the_Foundation/rect.h>
#include <the_Foundation/string.h>
#include <SDL3/SDL_events.h>

iDeclareType(LabelWidget)
iDeclareType(MenuItem)
iDeclareType(Widget)

struct Impl_MenuItem {
    const char *label;
    int key;
    int kmods;
    union {
        const char *command;
        const void *data;
    };
};

enum iMenuOpenFlags {
    postCommands_MenuOpenFlags = iBit(1),
    center_MenuOpenFlags       = iBit(2),
    setFocus_MenuOpenFlags     = iBit(3),
    submenu_MenuOpenFlags      = iBit(4),
    forcePopup_MenuOpenFlags   = iBit(5),
    fromMenuBar_MenuOpenFlags  = iBit(6), /* must not overlap the menubar */
};

iWidget *       makeMenu_Widget                 (iWidget *parent, const iMenuItem *items, size_t n); /* returns no ref */
iWidget *       makeMenuFlags_Widget            (iWidget *parent, const iMenuItem *items, size_t n, iBool allowNative);
void            makeMenuItems_Widget            (iWidget *menu, const iMenuItem *items, size_t n);
void            addMenuCancelAction_Widget      (iWidget *menu); /* automatically called when menu is created */
void            openMenu_Widget                 (iWidget *, iInt2 windowCoord);
void            openMenuFlags_Widget            (iWidget *, iInt2 windowCoord, int flags);
void            openMenuAnchorFlags_Widget      (iWidget *, iRect windowAnchorRect, int menuOpenFlags);
void            closeMenu_Widget                (iWidget *);
iBool           handleMenuCommand_Widget        (iWidget *menu, const char *cmd); /* used as the command handler */
void            releaseNativeMenu_Widget        (iWidget *);
void            setMenuUpdateItemsFunc_Widget   (iWidget *menu, const iArray *(*func)(iWidget *));

size_t          count_MenuItem                  (const iMenuItem *itemsNullTerminated);
iBool           checkDevice_MenuItem            (const iMenuItem *);
size_t          findWidestLabel_MenuItem        (const iMenuItem *items, size_t num);
size_t          findCommand_MenuItem            (const iMenuItem *items, size_t num, const char *command);
void            setSelected_NativeMenuItem      (iMenuItem *item, iBool isSelected);
void            appendIdentities_MenuItem       (iArray *menuItems, const char *command, iGmCertsIdentityFilterFunc);
const char *    widestLabel_MenuItemArray       (const iArray *items);

iLabelWidget *  findMenuItem_Widget             (iWidget *menu, const char *command);
iMenuItem *     findNativeMenuItem_Widget       (iWidget *menu, const char *commandSuffix);
void            setMenuItemDisabled_Widget      (iWidget *menu, const char *command, iBool disable);
void            setMenuItemDisabledByIndex_Widget(iWidget *menu, size_t index, iBool disable);
void            setMenuItemLabel_Widget         (iWidget *menu, const char *command, const char *newLabel, iChar icon);
void            setMenuItemLabelByIndex_Widget  (iWidget *menu, size_t index, const char *newLabel);
void            setNativeMenuItems_Widget       (iWidget *menu, const iMenuItem *items, size_t n);
iWidget *       findUserData_Widget             (iWidget *, void *userData);
iWidget *       parentMenu_Widget               (const iWidget *menuItem);

int             checkContextMenu_Widget         (iWidget *, const SDL_Event *ev); /* see macro below */

#define processContextMenuEvent_Widget(menu, sdlEvent, stmtEaten) \
    for (const int result = checkContextMenu_Widget((menu), (sdlEvent));;) { \
        if (result) { {stmtEaten;} return result >> 1; } \
        break; \
    }

iLabelWidget *  makeMenuButton_LabelWidget          (const char *label, const iMenuItem *items, size_t n);
void            updateDropdownSelection_LabelWidget (iLabelWidget *dropButton, const char *selectedCommand);
const char *    selectedDropdownCommand_LabelWidget (const iLabelWidget *dropButton);
iLabelWidget *  makeIdentityDropdown_LabelWidget    (iWidget *headings, iWidget *values,
                                                     const iArray *identItems, const char *label,
                                                     const char *id);

/*-----------------------------------------------------------------------------------------------*/

iWidget *       makeMenuBar_Widget                  (const iMenuItem *topLevelMenus, size_t num);
iBool           handleTopLevelMenuBarCommand_Widget (iWidget *menuButton, const char *cmd);
