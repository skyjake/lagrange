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

enum iFileVersion {
    initial_FileVersion                 = 0,
    addedResponseTimestamps_FileVersion = 1,
    /* meta */
    latest_FileVersion = 1
};

/* Icons */

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
#define hourglass_Icon      "\u231b"
#define timer_Icon          "\u23f2"
#define home_Icon           "\U0001f3e0"
#define edit_Icon           "\u270e"
#define delete_Icon         "\u232b" //"\u2bbf"
#define copy_Icon           "\u2bba"
#define check_Icon          "\u2714"
#define ballotCheck_Icon    "\U0001f5f9"
#define inbox_Icon          "\U0001f4e5"
#define book_Icon           "\U0001f56e"
#define openTab_Icon        "\u2750"
#define openTabBg_Icon      "\u2b1a"
#define openExt_Icon        "\u27a0"
#define add_Icon            "\u2795"
#define page_Icon           "\U00010117" // "\U0001d363" // "\U0001f5b9"
#define circle_Icon         "\u25cf"
#define circleWhite_Icon    "\u25cb"
#define gear_Icon           "\u2699"
#define explosion_Icon      "\U0001f4a5"
#define leftAngle_Icon      "\U0001fba4"
#define rightAngle_Icon     "\U0001fba5"
#define planet_Icon         "\U0001fa90"
#define info_Icon           "\u2139"
