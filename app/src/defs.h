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

/* The core library is shared by the GUI and TUI builds, so terminal-specific
   definitions belong here. */

iLocalDef iBool isTerminal_Platform(void) {
#if defined (iPlatformTerminal)
    return iTrue;
#else
    return iFalse;
#endif
}

#if defined (iPlatformTerminal)
#   define appDefault_ReturnKeyBehavior RETURN_KEY_BEHAVIOR(gui_ReturnKeyFlag, 0)
#else
#   define appDefault_ReturnKeyBehavior default_ReturnKeyBehavior
#endif

#if defined (iPlatformTerminal)
#   undef page_Icon
#   undef upload_Icon
#   undef midEllipsis_Icon
#   define page_Icon        "∷"
#   define upload_Icon      upArrow_Icon
#   define midEllipsis_Icon "..."
#   define shift_Icon       "Sh"
#   define shiftReturn_Icon "Sh-" return_Icon
#elif defined (iPlatformApple)
#   define shift_Icon       "⇧"
#   define shiftReturn_Icon shift_Icon return_Icon
#else
#   define shift_Icon       "Shift"
#   define shiftReturn_Icon shift_Icon " " return_Icon
#endif

int     keyMod_ReturnKeyFlag    (int flag);

/* Special keyboard modifier flag to indicate where menu items are valid. */
#define KMOD_DESKTOP    0x10000
#define KMOD_TABLET     0x20000
#define KMOD_PHONE      0x40000
#define KMOD_MOBILE     (KMOD_TABLET | KMOD_PHONE)
#define KMOD_NODEVICE   0x80000 /* hidden everywhere */

/* Menu items that open a new window. */
#if defined (iPlatformEmscripten)
#   define KMOD_NEWWINDOW   KMOD_NODEVICE
#else
#   define KMOD_NEWWINDOW   KMOD_DESKTOP
#endif

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

#if defined (iPlatformDesktop) && !defined (iPlatformTerminal) && !defined (iPlatformEmscripten)
#   define LAGRANGE_MULTIPLE_WINDOWS
#   define LAGRANGE_FILE_DIALOGS
#endif

/* Showing a file in the system file manager (see revealPath_App). */
#if defined (iPlatformAppleDesktop) || \
    (defined (iPlatformLinux) && !defined (iPlatformAndroid)) || \
    ((defined (iPlatformMsys) || defined (iPlatformWindows)) && !defined (iPlatformTerminal))
#   define LAGRANGE_HAVE_FILE_REVEAL
#endif

/* Sparkle and WinSparkle show the release notes themselves. */
#if !defined (LAGRANGE_ENABLE_SPARKLE) && !defined (LAGRANGE_ENABLE_WINSPARKLE)
#   define LAGRANGE_RELEASE_NOTES_POPUP
#endif

/* UI labels that depend on the platform */

#if defined (iPlatformMobile)
#   define saveToDownloads_Label    "${menu.save.files}"
#else
#   define saveToDownloads_Label    "${menu.save.downloads}"
#endif

#if defined (iPlatformAppleDesktop)
#   define revealFile_Label         "${menu.reveal.macos}"
#else
#   define revealFile_Label         "${menu.reveal.filemgr}"
#endif
