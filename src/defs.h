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

#include "lang.h"

enum iSourceFormat {
    undefined_SourceFormat = -1,
    gemini_SourceFormat    = 0,
    plainText_SourceFormat,
};

enum iFileVersion {
    initial_FileVersion                 = 0,
    addedResponseTimestamps_FileVersion = 1,
    multipleRoots_FileVersion           = 2,
    serializedSidebarState_FileVersion  = 3,
    addedRecentUrlFlags_FileVersion     = 4,
    /* meta */
    idents_FileVersion = 1, /* version used by GmCerts/idents.lgr */
    latest_FileVersion = 4,
};

enum iScrollType {
    keyboard_ScrollType,
    mouse_ScrollType,
    max_ScrollType
};

enum iReturnKeyFlag {
    return_ReturnKeyFlag        = 0,
    shiftReturn_ReturnKeyFlag   = 1,
    controlReturn_ReturnKeyFlag = 2,
    guiReturn_ReturnKeyFlag     = 3,
    mask_ReturnKeyFlag          = 0xf,
    accept_ReturnKeyFlag        = 4, /* shift */
};

/* Return key behavior is not handled via normal bindings because only certain combinations
   are valid. */
enum iReturnKeyBehavior {
    default_ReturnKeyBehavior =
        shiftReturn_ReturnKeyFlag | (return_ReturnKeyFlag << accept_ReturnKeyFlag),
    acceptWithShift_ReturnKeyBehavior =
        return_ReturnKeyFlag | (shiftReturn_ReturnKeyFlag << accept_ReturnKeyFlag),
    acceptWithPrimaryMod_ReturnKeyBehavior =
#if defined (iPlatformApple)
        return_ReturnKeyFlag | (guiReturn_ReturnKeyFlag << accept_ReturnKeyFlag),
#else
        return_ReturnKeyFlag | (controlReturn_ReturnKeyFlag << accept_ReturnKeyFlag),
#endif
};

int     keyMod_ReturnKeyFlag    (int flag);

iLocalDef int lineBreakKeyMod_ReturnKeyBehavior(int behavior) {
    return keyMod_ReturnKeyFlag(behavior & mask_ReturnKeyFlag);
}
iLocalDef int acceptKeyMod_ReturnKeyBehavior(int behavior) {
    return keyMod_ReturnKeyFlag((behavior >> accept_ReturnKeyFlag) & mask_ReturnKeyFlag);
}

/* Icons */

#define menu_Icon           "\U0001d362"
#define rightArrowhead_Icon "\u27a4"
#define leftArrowhead_Icon  "\u27a4"
#define warning_Icon        "\u26a0"
#define openLock_Icon       "\U0001f513"
#define closedLock_Icon     "\U0001f512"
#define close_Icon          "\u2a2f"
#define reload_Icon         "\U0001f503"
#define backArrow_Icon      "\U0001f870"
#define forwardArrow_Icon   "\U0001f872"
#define upArrow_Icon        "\u2191"
#define upArrowBar_Icon     "\u2912"
#define downArrowBar_Icon   "\u2913"
#define rightArrowWhite_Icon "\u21e8"
#define rightArrow_Icon     "\u279e"
#define barLeftArrow_Icon   "\u21a4"
#define barRightArrow_Icon  "\u21a6"
#define clock_Icon          "\U0001f553"
#define pin_Icon            "\U0001f588"
#define star_Icon           "\u2605"
#define whiteStar_Icon      "\u2606"
#define person_Icon         "\U0001f464"
#define download_Icon       "\u2ba7"
#define upload_Icon         "\u2ba5"
#define export_Icon         "\U0001f4e4"
#define hourglass_Icon      "\u231b"
#define timer_Icon          "\u23f2"
#define home_Icon           "\U0001f3e0"
#define edit_Icon           "\u270e"
#define delete_Icon         "\u232b"
#define copy_Icon           "\u2398" //"\u2bba"
#define check_Icon          "\u2714"
#define ballotCheck_Icon    "\U0001f5f9"
#define inbox_Icon          "\U0001f4e5"
#define book_Icon           "\U0001f56e"
#define bookmark_Icon       "\U0001f516"
#define folder_Icon         "\U0001f4c1"
#define openTab_Icon        "\u2750"
#define openTabBg_Icon      "\u2b1a"
#define openExt_Icon        "\u27a0"
#define add_Icon            "\u2795"
#define page_Icon           "\U00010117"
#define circle_Icon         "\u25cf"
#define circleWhite_Icon    "\u25cb"
#define gear_Icon           "\u2699"
#define explosion_Icon      "\U0001f4a5"
#define leftAngle_Icon      "\U0001fba4"
#define rightAngle_Icon     "\U0001fba5"
#define planet_Icon         "\U0001fa90"
#define info_Icon           "\u2139"
#define bug_Icon            "\U0001f41e"
#define leftHalf_Icon       "\u25e7"
#define rightHalf_Icon      "\u25e8"
#define scissor_Icon        "\u2700"
#define clipCopy_Icon       "\u2398"
#define clipboard_Icon      "\U0001f4cb"
#define unhappy_Icon        "\U0001f641"
#define globe_Icon          "\U0001f310"
#define magnifyingGlass_Icon "\U0001f50d"
#define midEllipsis_Icon    "\u00b7\u00b7\u00b7"
#define return_Icon         "\u23ce"

#if defined (iPlatformApple)
#   define shift_Icon       "\u21e7"
#   define shiftReturn_Icon shift_Icon return_Icon
#else
#   define shift_Icon       "Shift"
#   define shiftReturn_Icon shift_Icon " " return_Icon
#endif

#if defined (iPlatformAppleDesktop)
#   define iHaveNativeMenus
#endif

/* UI labels that depend on the platform */

#if defined (iPlatformMobile)
#   define saveToDownloads_Label    "${menu.save.files}"
#else
#   define saveToDownloads_Label    "${menu.save.downloads}"
#endif
