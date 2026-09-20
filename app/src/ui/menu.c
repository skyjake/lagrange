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

#include "menu.h"

#include "app.h"
#include "command.h"
#include "defs.h"
#include "gamepad.h"
#include "labelwidget.h"
#include "periodic.h"
#include "render/text.h"
#include "root.h"
#include "util.h"
#include "widget.h"
#include "window.h"

#if defined (iPlatformAppleDesktop)
#   include "platform/macos.h"
#endif
#if defined (iPlatformAppleMobile)
#   include "platform/ios.h"
#endif
#if defined (LAGRANGE_ENABLE_X11_XLIB)
#   include "platform/x11.h"
#endif

#include <SDL3/SDL_timer.h>

static iBool isCommandIgnoredByMenus_(const char *cmd) {
    return isNotification_Command(cmd) ||
           equal_Command(cmd, "document.reload") || /* may also be a user action */
           /* TODO: mark the Android commands as notifications, too */
           equal_Command(cmd, "android.keyboard.changed") ||
           equal_Command(cmd, "android.input.selrange") ||
           equal_Command(cmd, "android.audio.time") ||
           startsWith_Command(cmd, "open idle:1") || /* opening a URL sometime later */
           (equal_Command(cmd, "open") &&
            argLabel_Command(cmd, "redirect")) || /* not a user action */
           (deviceType_App() == desktop_AppDeviceType && equal_Command(cmd, "window.resized")) ||
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

static void closeSubmenus_(iWidget *menu, iRoot *root) {
    iConstForEach(ObjectList, i, children_Widget(menu)) {
        if (isInstance_Object(i.object, &Class_LabelWidget)) {
            iLabelWidget *label = (iLabelWidget *) i.object;
            const iString *subCmd = command_LabelWidget(label);
            if (startsWith_String(subCmd, "submenu id:")) {
                const char *subId = cstr_Command(cstr_String(subCmd), "id");
                iWidget *submenu = NULL;
                /* When menus are opened in popups, the menu widget is temporarily migrated
                   to the popup window's root. */
                iConstForEach(PtrArray, p, popupWindows_App()) {
                    const iWindow *pop = p.ptr;
//                    printf("[%zu] root %p '%s'\n", index_PtrArrayConstIterator(&p),
//                           pop->roots[0]->widget, cstr_String(id_Widget(pop->roots[0]->widget)));
                    if ((submenu = findChild_Widget(pop->roots[0]->widget, subId)) != NULL) {
                        break;
                    }
                }
                if (!submenu) {
                    submenu = findChild_Widget(root->widget, subId);
                }
                if (submenu && isVisible_Widget(submenu)) {
                    remove_Periodic(periodic_App(), submenu);
                    closeSubmenus_(submenu, root);
                    closeMenu_Widget(submenu);
                }
            }
        }
    }
}

static void openSubmenu_(iWidget *d) {
    if (isRecentlyDeleted_Widget(d)) {
        return;
    }
    iAssert(isInstance_Object(d, &Class_LabelWidget));
    iWidget    *menu    = parent_Widget(d);
    const iBool isPopup = type_Window(window_Widget(menu)) == popup_WindowType;
    iRoot      *root    = isPopup ? constAs_Widget(userData_Object(menu))->root : d->root;
    closeSubmenus_(menu, root);
    const char *subId = cstr_Command(cstr_String(command_LabelWidget((iLabelWidget *) d)), "id");
    iWidget *submenu = findChild_Widget(root->widget, subId);
    if (!submenu && isPopup) {
        /* Inline submenus live inside the menu widget; when the menu was promoted
           to a popup window they moved with it, so also search there. */
        submenu = findChild_Widget(window_Widget(menu)->roots[0]->widget, subId);
    }
    if (submenu && !isVisible_Widget(submenu)) {
        remove_Periodic(periodic_App(), menu);
//        printf("openSubmenu_ %s isPopup:%d\n d's window type: %d",
//               cstr_String(id_Widget(submenu)), isPopup, window_Widget(d)->type); fflush(stdout);
        if (isPopup) {
            setCurrent_Window(window_Widget(d));
        }
        openMenuAnchorFlags_Widget(submenu,
                                   bounds_Widget(d),
                                   submenu_MenuOpenFlags |
                                       (isTerminal_Platform() ? setFocus_MenuOpenFlags : 0) |
                                       (isPopup ? forcePopup_MenuOpenFlags : 0));
    }
}

iBool handleMenuCommand_Widget(iWidget *menu, const char *cmd) {
    if (isVisible_Widget(menu)) {
        if (equalWidget_Command(cmd, menu, "menu.opened")) {
            return iFalse;
        }
        if (equalWidget_Command(cmd, menu, "menu.keepatbottom")) {
            iAssert(deviceType_App() != desktop_AppDeviceType);
            menu->rect.pos.y = windowToLocal_Widget(
                    menu,
                    init_I2(0, bottom_Rect(safeRect_Root(menu->root)) - menu->rect.size.y)).y;
            return iTrue;
        }
        if (equal_Command(cmd, "submenu.open")) {
            /* This is sent directly via Periodic after mouse hover on an item. */
            iWidget *menuItem = pointer_Command(cmd);
            if (isHover_Widget(menuItem) || isTerminal_Platform()) {
                openSubmenu_(menuItem);
            }
            return iTrue;
        }
        if (equal_Command(cmd, "submenu") && parent_Widget(pointer_Command(cmd)) == menu) {
            return iFalse;
        }
        if (equal_Command(cmd, "menu.open") &&
            (pointer_Command(cmd) == menu->parent || argLabel_Command(cmd, "self"))) {
            /* Don't reopen self; instead, root will close the menu. */
            return iFalse;
        }
        if ((equal_Command(cmd, "mouse.clicked") || equal_Command(cmd, "mouse.missed")) &&
            arg_Command(cmd)) {
            const iInt2 coord = coord_Command(cmd);
            /* Dismiss open menus when clicking outside them. A possible parent menu button
               is considered part of the menu. */
            iLabelWidget *menuButton = parentMenuButton_(menu);
            if (menuButton && contains_Widget(as_Widget(menuButton), coord)) {
                return iFalse;
            }
            if (!contains_Widget(menu, coord)) {
                closeMenu_Widget(menu);
                return iFalse;
            }
            return iFalse;
        }
        if (equal_Command(cmd, "cancel") && pointerLabel_Command(cmd, "menu") == menu) {
            return iFalse;
        }
        if (equal_Command(cmd, "submenu.close") &&
            (pointerLabel_Command(cmd, "menu") == menu || ~menu->flags & radio_WidgetFlag)) {
            return iFalse;
        }
        if (equal_Command(cmd, "contextclick")) {
            return iFalse;
        }
        if (deviceType_App() == phone_AppDeviceType && equal_Command(cmd, "keyboard.changed") &&
            arg_Command(cmd) == 0) {
            /* May need to reposition the menu. */
            notify_Widget(menu, "menu.keepatbottom");
            return iFalse;
        }
        if (!isCommandIgnoredByMenus_(cmd)) {
#if !defined (NDEBUG) && !defined (iPlatformTerminal)
            printf("closemenu being called on %p (id:%s) due to cmd: %s\n", menu,
                   cstr_String(id_Widget(menu)), cmd);
            fflush(stdout);
#endif
            closeMenu_Widget(menu);
        }
    }
    return iFalse;
}

static iWidget *makeMenuSeparator_(void) {
    iWidget *sep = new_Widget();
    iWidget *sbar = new_Widget();
    setFlags_Widget(sep, resizeChildren_WidgetFlag, iTrue);
    sep->flags2 |= centerChildrenVertical_WidgetFlag2;
    addChildFlags_Widget(sep, iClob(sbar), 0);
    setBackgroundColor_Widget(sbar, isMobile_Platform() ? uiSeparator_ColorId : uiTextDisabled_ColorId);
    sep->rect.size.y = 2 * gap_UI * aspect_UI;
    setFixedSize_Widget(sbar, init_I2(-1, gap_UI / 3));
    setPadding_Widget(sep, 2 * gap_UI, 0, 2 * gap_UI, gap_UI / 3);
    setFlags_Widget(sep, hover_WidgetFlag | fixedHeight_WidgetFlag, iTrue);
    return sep;
}

static iBool submenuItemHandler_(iWidget *d, const char *cmd) {
    if (!isHandheld_Platform() && equal_Command(cmd, "mouse.hovered") && isVisible_Widget(d)) {
        iAssert(isInstance_Object(d, &Class_LabelWidget));
        iLabelWidget *label = (iLabelWidget *) d;
        const iString *subCmd = command_LabelWidget(label);
        if (startsWith_String(subCmd, "submenu id:")) {
            iWidget *menu = parent_Widget(d);
            remove_Periodic(periodic_App(), menu);
            addDelay_Periodic(periodic_App(), 150, menu, format_CStr("submenu.open ptr:%p", label));
        }
        return iTrue;
    }
    return iFalse;
}

static iBool isValidLabelIcon_(iChar c) {
    return c >= 0x100 && c != 0x2014;
}

void makeMenuItems_Widget(iWidget *menu, const iMenuItem *items, size_t n) {
    iBool       haveIcons       = iFalse;
    iBool       haveSubmenu     = iFalse;
    iWidget    *horizGroup      = NULL;
    const iBool isPortraitPhone = (deviceType_App() == phone_AppDeviceType && isPortrait_App());
    int64_t     itemFlags       = (deviceType_App() != desktop_AppDeviceType ? 0 : 0) |
                                  (isPortraitPhone ? extraPadding_WidgetFlag : 0);
    for (size_t i = 0; i < n && items[i].label; i++) {
        const iMenuItem *item = &items[i];
        if (!item->label) {
            break;
        }
        if (!checkDevice_MenuItem(item)) {
            continue;
        }
        const char *labelText = item->label;
        iArray *submenuItems = NULL;
        iString *submenuId = NULL;
        if (!startsWith_CStr(labelText, ">>>")) {
            horizGroup = NULL;
        }
        if (equal_CStr(labelText, "---")) {
            addChild_Widget(menu, iClob(makeMenuSeparator_()));
        }
        else {
            iBool isInfo = iFalse;
            iBool isDisabled = iFalse;
            if (startsWith_CStr(labelText, "---")) {
                if (equal_CStr(labelText, "---:")) {
                    /* This is just a submenu terminator. */
                    continue;
                }
                /* A submenu with items embedded in this one. */
                labelText += 3;
                submenuId = newFormat_String("sub.%p.%zu", menu, i);
                /* Collect the contents of the submenu. */
                submenuItems = new_Array(sizeof(iMenuItem));
                for (i++; i < n && items[i].label && !startsWith_CStr(items[i].label, "---"); i++) {
                    pushBack_Array(submenuItems, &items[i]);
                }
                i--;
            }
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
            const iBool isIcon = length_String(&labelStr) == 1 &&
                                 isValidLabelIcon_(first_String(&labelStr));
            const char *command = item->command;
            if (submenuId) {
                command = format_CStr("submenu id:%s", cstr_String(submenuId));
            }
            iLabelWidget *label = addChildFlags_Widget(
                horizGroup ? horizGroup : menu,
                iClob(isIcon
                    ? newIcon_LabelWidget(cstr_String(&labelStr), item->key, item->kmods, command)
                    : newKeyMods_LabelWidget(cstr_String(&labelStr), item->key, item->kmods, command)),
                noBackground_WidgetFlag | frameless_WidgetFlag |
                    (!isIcon ? alignLeft_WidgetFlag | drawKey_WidgetFlag : 0) |
                    itemFlags);
            deinit_String(&labelStr);
            setWrap_LabelWidget(label, isInfo);
            if (isInfo) {
                setMinSize_Widget(as_Widget(label), init_I2(50 * gap_UI, 0));
            }
            else {
                haveIcons |= checkIcon_LabelWidget(label);
            }
            if (isIcon) {
                setTextColor_LabelWidget(label, uiIcon_ColorId);
                setFont_LabelWidget(label, uiLabelMedium_FontId);
            }
            if (isInfo && deviceType_App() != desktop_AppDeviceType) {
                setFont_LabelWidget(label, uiContent_FontId);
            }
            if (command && startsWith_CStr(command, "submenu id:")) {
                setChevron_LabelWidget(label, iTrue);
                setFlags_Widget(as_Widget(label), drawKey_WidgetFlag, iFalse);
                haveSubmenu = iTrue;
            }
            if (command && !submenuItems) {
                setMenuCanceling_LabelWidget(label, iTrue);
            }
            as_Widget(label)->flags2 |= commandOnHover_WidgetFlag2;
            setFlags_Widget(as_Widget(label), disabled_WidgetFlag, isDisabled);
            if (isInfo) {
                setFlags_Widget(as_Widget(label), resizeToParentWidth_WidgetFlag |
                                fixedHeight_WidgetFlag, iTrue); /* wrap changes height */
                setTextColor_LabelWidget(label, uiTextAction_ColorId);
            }
            updateSize_LabelWidget(label); /* drawKey was set */
            /* Create a hidden submenu. */
            if (submenuItems) {
                iWidget *sub = makeMenu_Widget(menu,
                                               data_Array(submenuItems), size_Array(submenuItems));
                setId_Widget(sub, cstr_String(submenuId));
                delete_Array(submenuItems);
                delete_String(submenuId);
            }
        }
    }
    if (deviceType_App() == phone_AppDeviceType) {
        addChild_Widget(menu, iClob(makeMenuSeparator_()));
        addChildFlags_Widget(menu,
                             iClob(new_LabelWidget("${cancel}", "cancel")),
                             itemFlags | noBackground_WidgetFlag | frameless_WidgetFlag |
                             alignLeft_WidgetFlag);
    }
    if (haveIcons || haveSubmenu) {
        iForEach(ObjectList, i, children_Widget(menu)) {
            if (isInstance_Object(i.object, &Class_LabelWidget)) {
                iLabelWidget *label = i.object;
                if (haveIcons) {
                    /* All items must have icons if at least one of them has. */
                    if (!isWrapped_LabelWidget(label) && icon_LabelWidget(label) == 0) {
                        setIcon_LabelWidget(label, ' ');
                    }
                }
                if (haveSubmenu) {
                    /* Open and close submenus on hover. */
                    setCommandHandler_Widget(i.object, submenuItemHandler_);
                }
            }
        }
    }
}

static iArray *deepCopyMenuItems_(const iMenuItem *items, size_t n) {
    iArray *array = new_Array(sizeof(iMenuItem));
    iString cmd;
    init_String(&cmd);
    for (size_t i = 0; i < n && items[i].label; i++) {
        const iMenuItem *item = &items[i];
        const char *itemCommand = item->command;
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
    setUserData_Object(menu, deepCopyMenuItems_(items, n));
    /* Keyboard shortcuts still need to triggerable via the menu, although
       the items don't exist. */ {
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

iWidget *parentMenu_Widget(const iWidget *menuItem) {
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

void setMenuUpdateItemsFunc_Widget(iWidget *menu, const iArray *(*func)(iWidget *)) {
    menu->updateMenuItems = func;
#if defined (LAGRANGE_NATIVE_MENU)
    if (isAppleMobile_Platform()) {
        updateItems_SystemMenu(menu, NULL, 0); /* creates a deferred element */
    }
#endif
}

void addMenuCancelAction_Widget(iWidget *menu) {
    /* A keyboard shortcut for closing the menu. */
    iWidget *cancel = addAction_Widget(menu, SDLK_ESCAPE, 0, "cancel");
    setId_Widget(cancel, "menu.cancel");
    setFlags_Widget(cancel, disabled_WidgetFlag, iTrue);
}

iWidget *makeMenuFlags_Widget(iWidget *parent, const iMenuItem *items, size_t n,
                              iBool allowNative) {
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
                        fixedPosition_WidgetFlag |
                        arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag |
                        resizeChildrenToWidestChild_WidgetFlag | overflowScrollable_WidgetFlag,
                    iTrue);
    makeMenuItems_Widget(menu, items, n);
    addChild_Widget(parent, menu);
    iRelease(menu); /* owned by parent now */
    setCommandHandler_Widget(menu, handleMenuCommand_Widget);
    addMenuCancelAction_Widget(menu);
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
    openMenuAnchorFlags_Widget(d, initCorners_Rect(windowCoord, windowCoord), menuOpenFlags);
}

void openMenuAnchorFlags_Widget(iWidget *d, iRect windowAnchorRect, int menuOpenFlags) {
    iInt2 windowCoord         = topRight_Rect(windowAnchorRect);
    const iBool postCommands  = (menuOpenFlags & postCommands_MenuOpenFlags) != 0;
    const iBool isMenuFocused = ((menuOpenFlags & setFocus_MenuOpenFlags) ||
                                 focus_Widget() == parent_Widget(d));
    const iBool isPopupForced = (menuOpenFlags & forcePopup_MenuOpenFlags) != 0;
    const iBool isSubmenu     = (menuOpenFlags & submenu_MenuOpenFlags) != 0;
    const iBool isFromMenuBar = (menuOpenFlags & fromMenuBar_MenuOpenFlags) != 0;
    /* Some menus may require updating the items dynamically. */
    if (d->updateMenuItems) {
        const iArray *newItems = d->updateMenuItems(d);
        if (flags_Widget(d) & nativeMenu_WidgetFlag) {
            deleteMenuItems_(userData_Object(d));
            setUserData_Object(
                d, deepCopyMenuItems_(constData_Array(newItems), size_Array(newItems)));
        }
        else {
            releaseChildren_Widget(d);
            makeMenuItems_Widget(d, constData_Array(newItems), size_Array(newItems));
            addMenuCancelAction_Widget(d); /* must be present as well */
        }
    }
    if (postCommands && !isSubmenu) {
        postCommandf_App("cancel menu:%p", d); /* dismiss any other menus */
    }
    iWindow *currentWindow = get_Window();
    /* Menu closes when commands are emitted, so handle any pending ones beforehand. */
    processEvents_App(postedEventsOnly_AppEventMode);
    setCurrent_Window(currentWindow);
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
    iBool allowOverflow = (get_Window()->type == extra_WindowType);
    /* A vertical offset determined by a possible selected label in the menu. */
    iWidget *focusedItem = NULL;
    if (deviceType_App() == desktop_AppDeviceType &&
        windowCoord.y < rootSize.y - lineHeight_Text(uiNormal_FontSize) * 3) {
        iForEach(ObjectList, child, children_Widget(d)) {
            iWidget *item = as_Widget(child.object);
            if (flags_Widget(item) & selected_WidgetFlag) {
                windowCoord.y -= item->rect.pos.y;
                allowOverflow = iTrue;
                focusedItem = item;
            }
        }
    }
    if (isUsingMenuPopupWindows_()) {
        /* Determine total display bounds where the popup may appear. */
        iRect displayRect = zero_Rect();
        int numDisplays = 0;
        SDL_DisplayID *displays = SDL_GetDisplays(&numDisplays);
        if (displays) {
            for (int i = 0; i < numDisplays; i++) {
                SDL_Rect dispBounds;
                SDL_GetDisplayUsableBounds(displays[i], &dispBounds);
                displayRect = union_Rect(
                    displayRect, init_Rect(dispBounds.x, dispBounds.y, dispBounds.w, dispBounds.h));
            }
            SDL_free(displays);
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
        /* Only use a popup window if the menu can't fit inside the window. */
        if (isPopupForced || height_Widget(d) / pixelRatio > visibleWinRect.size.y ||
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
            parent_Widget(d)->flags2 |= childMenuOpenedAsPopup_WidgetFlag2;
            removeChild_Widget(parent_Widget(d), d); /* we'll borrow the widget for a while */
            iInt2 winPos;
            SDL_GetWindowPosition(sdlWin, &winPos.x, &winPos.y);
            iInt2 menuPos = add_I2(winPos,
                                   divf_I2(sub_I2(windowCoord, divi_I2(gap2_UI, 2)), pixelRatio));
            iInt2 menuSize = divf_I2(d->rect.size, pixelRatio);
            /* Check display bounds. */ {
                if (menuOpenFlags & center_MenuOpenFlags) {
                    iInt2 winSize;
                    SDL_GetWindowSize(sdlWin, &winSize.x, &winSize.y);
                    menuPos = sub_I2(add_I2(winPos, divi_I2(winSize, 2)), divi_I2(menuSize, 2));
                }
                if (isSubmenu && menuPos.x + width_Widget(d) > right_Rect(displayRect)) {
                    /* Flip it to the right side. */
                    menuPos.x -= menuSize.x + width_Rect(windowAnchorRect) / pixelRatio;
                }
                menuPos.x = iMin(menuPos.x, right_Rect(displayRect) - menuSize.x);
                if (!isFromMenuBar) {
                    menuPos.y = iMax(0, iMin(menuPos.y, bottom_Rect(displayRect) - menuSize.y));
                }
            }
            iWindow *win = newPopup_Window(menuPos, d); /* window takes the widget */
#if !defined (iPlatformTerminal)
            if (isFromMenuBar && menuPos.y + menuSize.y > bottom_Rect(displayRect)) {
                const int maxMenuHeight = bottom_Rect(displayRect) - menuPos.y;
                SDL_SetWindowMaximumSize(win->win, displayRect.size.x, maxMenuHeight);
            }
#endif
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
    if (isSubmenu && windowCoord.x + width_Widget(d) > right_Rect(rootRect)) {
        /* Flip it to the right side. */
        windowCoord = addX_I2(topLeft_Rect(windowAnchorRect), -width_Widget(d));
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
    if (isFromMenuBar && windowCoord.y + height_Widget(d) > bottom_Rect(rootRect)) {
        /* Don't overlap the menu bar. */
        d->overflowTopMargin = windowCoord.y - top_Rect(rootRect);
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
    int         topExcess    = top_Rect(rootRect) + d->overflowTopMargin - top_Rect(bounds);
    int         bottomExcess = bottom_Rect(bounds) - bottom_Rect(rootRect);
#if defined (iPlatformMobile)
    /* Reserve space for the system status bar. */ {
        float l, t, r, b;
        safeAreaInsets_Mobile(&l, &t, &r, &b);
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
        if (deviceType_App() == desktop_AppDeviceType) {
            if (!isFromMenuBar && bottomExcess > 0 && !isSlidePanel) {
                d->rect.pos.y -= bottomExcess;
            }
            if (topExcess > 0) {
                d->rect.pos.y += topExcess;
            }
        }
        else {
            d->rect.pos.y = windowToLocal_Widget(d, init_I2(0, bottom_Rect(rootRect) -
                                                            height_Rect(bounds) -
                                                            bottomSafeInset_Mobile())).y;
        }
    }
    if (rightExcess > 0) {
        d->rect.pos.x -= rightExcess;
    }
    if (leftExcess > 0) {
        d->rect.pos.x += leftExcess;
    }
    refresh_Widget(d);
    if (postCommands) {
        postCommand_Widget(d, "menu.opened");
    }
    setupMenuTransition_Mobile(d, iTrue);
    if (isMenuFocused) {
        if (focusedItem) {
            setFocus_Widget(focusedItem);
        }
        else {
            iForEach(ObjectList, i, children_Widget(d)) {
                if (flags_Widget(i.object) & focusable_WidgetFlag) {
                    setFocus_Widget(i.object);
                    break;
                }
            }
        }
    }
}

void closeMenu_Widget(iWidget *d) {
    if (!d || flags_Widget(d) & nativeMenu_WidgetFlag) {
        return; /* Handled natively. */
    }
    remove_Periodic(periodic_App(), d);
    iWindow *win = window_Widget(d);
    if (type_Window(win) == popup_WindowType) {
        iWidget *originalParent = userData_Object(d);
        setUserData_Object(d, NULL);
        /* This may have been a popup window opened from a root being deleted.
           originalParent may also be NULL if the widget ended up in a popup
           window as a passenger (e.g., an inline submenu carried by its parent
           menu) without being individually promoted via openMenuAnchorFlags_Widget. */
        if (originalParent && !isRecentlyDeleted_Widget(originalParent)) {
            win->roots[0]->widget = NULL;
            setRoot_Widget(d, originalParent->root);
            addChild_Widget(originalParent, d);
            originalParent->flags2 &= ~childMenuOpenedAsPopup_WidgetFlag2;
            setFlags_Widget(d, keepOnTop_WidgetFlag, iTrue);
        }
        SDL_HideWindow(win->win);
        collect_Garbage(win, (iDeleteFunc) delete_Window); /* get rid of it after event processing */
    }
    if (~flags_Widget(d) & hidden_WidgetFlag) {
        setFlags_Widget(d, hidden_WidgetFlag, iTrue);
        setFlags_Widget(findChild_Widget(d, "menu.cancel"), disabled_WidgetFlag, iTrue);
        iLabelWidget *button = parentMenuButton_(d);
        if (button) {
            setFlags_Widget(as_Widget(button), selected_WidgetFlag, iFalse);
        }
        iWidget *menubar = findParent_Widget(d, "menubar");
        if (menubar) {
            setRecentMenuBarIndex_App(indexOfChild_Widget(menubar, parent_Widget(d)));
        }
        refresh_Widget(d);
        if (d->menuClosed) {
            d->menuClosed(d);
        }
        notify_Widget(d, "menu.closed");
        setupMenuTransition_Mobile(d, iFalse);
        if (focus_Widget() && hasParent_Widget(focus_Widget(), d)) {
            setFocus_Widget(menubar ? NULL : as_Widget(button));
        }
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
    if (menu && ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev->button.button == SDL_BUTTON_RIGHT) {
        if (isVisible_Widget(menu)) {
            closeMenu_Widget(menu);
            return 0x1;
        }
        const iInt2 mousePos = init_I2(ev->button.x, ev->button.y);
        if (contains_Widget(menu->parent, mousePos)) {
            openMenu_Widget(menu, mousePos);
            if (isEmulatedMouseDevice_UserEvent(ev) &&
                (!isMobile_Platform() || isPointing_Gamepad(gamepad_App()))) {
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
    if (isTerminal_Platform()) {
        setFlags_Widget(as_Widget(button), tight_WidgetFlag, iTrue);
    }
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

iWidget *dropdownMenu_Widget(const iWidget *dropButton) {
    if (!dropButton) {
        return NULL;
    }
    iWidget *menu = findChild_Widget(dropButton, "menu");
    if (!menu) {
        if (dropButton->flags2 & childMenuOpenedAsPopup_WidgetFlag2) {
            /* The menu has been migrated temporarily into a popup window. We need to locate
               the right popup. */
            iConstForEach(PtrArray, p, popupWindows_App()) {
                const iWindow *win = p.ptr;
                iWidget *winRoot = win->roots[0]->widget;
                if (userData_Object(winRoot) == dropButton) {
                    iAssert(!cmp_String(id_Widget(winRoot), "menu"));
                    return winRoot;
                }
            }
        }
    }
    //iAssert(menu);
    return menu;
}

static iBool updateMenuSelection_Widget_(iWidget *menu, iLabelWidget *dropButton,
                                         const char *selectedCommand) {
    iBool wasFound = iFalse;
    iForEach(ObjectList, i, children_Widget(menu)) {
        if (isInstance_Object(i.object, &Class_LabelWidget)) {
            iLabelWidget *item       = i.object;
            const iBool   isSelected = endsWith_String(command_LabelWidget(item), selectedCommand);
            setFlags_Widget(as_Widget(item), selected_WidgetFlag, isSelected);
            if (isSelected) {
                updateText_LabelWidget(dropButton,
                                       replaceNewlinesWithDash_(text_LabelWidget(item)));
                checkIcon_LabelWidget(dropButton);
                if (!icon_LabelWidget(dropButton)) {
                    setIcon_LabelWidget(dropButton, icon_LabelWidget(item));
                }
                wasFound = iTrue;
            }
        }
        else if (isInstance_Object(i.object, &Class_Widget)) {
            /* Possibly a submenu. They get populated as child menus of the parent menu. */
            wasFound |= updateMenuSelection_Widget_(i.object, dropButton, selectedCommand);
        }
    }
    return wasFound;
}

void updateDropdownSelection_LabelWidget(iLabelWidget *dropButton, const char *selectedCommand) {
    if (!dropButton) {
        return;
    }
    iWidget *menu = dropdownMenu_Widget(as_Widget(dropButton));
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
    updateMenuSelection_Widget_(menu, dropButton, selectedCommand);
}

const char *selectedDropdownCommand_LabelWidget(const iLabelWidget *dropButton) {
    if (!dropButton) {
        return "";
    }
    iWidget *menu = dropdownMenu_Widget(constAs_Widget(dropButton));
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
        iWidget *child = i.object;
        iWidget *menu = findChild_Widget(child, "menu");
        if (isVisible_Widget(menu) || child->flags2 & childMenuOpenedAsPopup_WidgetFlag2) {
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

size_t findWidestLabel_MenuItem(const iMenuItem *items, size_t num) {
    int widest = 0;
    size_t widestPos = iInvalidPos;
    for (size_t i = 0; i < num && items[i].label; i++) {
        if (startsWith_CStr(items[i].label, "---")) {
            continue; /* skip separators and submenu headers */
        }
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
