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

#include <lagrange/defs.h>
#include <lagrange/lang.h>

enum iScrollType {
    keyboard_ScrollType,
    mouse_ScrollType,
    max_ScrollType
};

enum iToolbarAction {
    back_ToolbarAction        = 0,
    forward_ToolbarAction     = 1,
    home_ToolbarAction        = 2,
    parent_ToolbarAction      = 3,
    reload_ToolbarAction      = 4,
    newTab_ToolbarAction      = 5,
    closeTab_ToolbarAction    = 6,
    addBookmark_ToolbarAction = 7,
    translate_ToolbarAction   = 8,
    upload_ToolbarAction      = 9,
    editPage_ToolbarAction    = 10,
    findText_ToolbarAction    = 11,
    settings_ToolbarAction    = 12,
    leftSidebar_ToolbarAction    = 13, /* desktop/tablet only */
    rightSidebar_ToolbarAction   = 14, /* desktop/tablet only */
    scrollToTop_ToolbarAction    = 15,
    scrollToBottom_ToolbarAction = 16,
    max_ToolbarAction
};

enum iReturnKeyFlag {
    noMod_ReturnKeyFlag   = 0,
    shift_ReturnKeyFlag   = 1,
    control_ReturnKeyFlag = 2,
    gui_ReturnKeyFlag     = 3,
    mask_ReturnKeyFlag    = 0xf,
    accept_ReturnKeyFlag  = 4, /* shift */
};

#define RETURN_KEY_BEHAVIOR(newlineFlag, acceptFlag) \
    ((newlineFlag) & 3 | ((acceptFlag) << accept_ReturnKeyFlag))

/* Return key behavior is not handled via normal bindings because only certain combinations
   are valid. */
enum iReturnKeyBehavior {
    acceptWithPrimaryMod_ReturnKeyBehavior =
#if defined (iPlatformApple)
        RETURN_KEY_BEHAVIOR(0, gui_ReturnKeyFlag),
#else
        RETURN_KEY_BEHAVIOR(control_ReturnKeyFlag, 0),
#endif
    onlyWithMods_ReturnKeyBehavior =
#if defined (iPlatformApple)
        RETURN_KEY_BEHAVIOR(shift_ReturnKeyFlag, gui_ReturnKeyFlag),
#else
        RETURN_KEY_BEHAVIOR(shift_ReturnKeyFlag, control_ReturnKeyFlag),
#endif
#if defined (iPlatformTerminal)
    default_ReturnKeyBehavior = RETURN_KEY_BEHAVIOR(gui_ReturnKeyFlag, 0),
#elif defined (iPlatformAndroidMobile)
    default_ReturnKeyBehavior = RETURN_KEY_BEHAVIOR(0, shift_ReturnKeyFlag),
#else
    default_ReturnKeyBehavior = RETURN_KEY_BEHAVIOR(shift_ReturnKeyFlag, 0),
#endif
};

int     keyMod_ReturnKeyFlag    (int flag);

/* Special keyboard modifier flag to indicate where menu items are valid. */
#define KMOD_DESKTOP    0x10000
#define KMOD_TABLET     0x20000
#define KMOD_PHONE      0x40000
#define KMOD_MOBILE     (KMOD_TABLET | KMOD_PHONE)

iLocalDef int lineBreakKeyMod_ReturnKeyBehavior(int behavior) {
    return keyMod_ReturnKeyFlag(behavior & mask_ReturnKeyFlag);
}
iLocalDef int acceptKeyMod_ReturnKeyBehavior(int behavior) {
    return keyMod_ReturnKeyFlag((behavior >> accept_ReturnKeyFlag) & mask_ReturnKeyFlag);
}

#if defined (iPlatformAppleMobile)
#   define LAGRANGE_NATIVE_MENU
#elif defined (iPlatformAppleDesktop) && defined (LAGRANGE_ENABLE_MAC_MENUS)
#   define LAGRANGE_NATIVE_MENU
#   define LAGRANGE_MAC_MENUBAR
#   define LAGRANGE_MAC_CONTEXTMENU
#elif defined (iPlatformDesktop)
#   define LAGRANGE_MENUBAR
#endif

#if defined (iPlatformDesktop) && !defined (iPlatformTerminal)
#   define LAGRANGE_MULTIPLE_WINDOWS
#endif

/* UI labels that depend on the platform */

#if defined (iPlatformMobile)
#   define saveToDownloads_Label    "${menu.save.files}"
#else
#   define saveToDownloads_Label    "${menu.save.downloads}"
#endif
