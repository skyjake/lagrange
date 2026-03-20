/* Copyright 2025 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include <the_Foundation/vec2.h>

iDeclareType(Widget);
iDeclareType(Window);

#if !defined (iPlatformTerminal) && defined (LAGRANGE_ENABLE_GAMEPAD)
#  define LAGRANGE_USE_GAMEPAD
#endif

/* Device ID used for emulated mouse events. */
#define mouseId_Gamepad ((uint32_t) -2)

enum iGamepadAction {
    primary_GamepadAction,
    secondary_GamepadAction,
    cancel_GamepadAction,
    openNavMenu_GamepadAction,
    openPageMenu_GamepadAction,
    openSidebar_GamepadAction,
    reloadPage_GamepadAction,
    focusUrl_GamepadAction,
    max_GamepadAction,
};

#define unassigned_Gamepad  -1
#define triggerMod_Gamepad  0x1000

extern int actions_Gamepad[max_GamepadAction];

int     findAction_Gamepad      (int button, iBool trigger);

/*----------------------------------------------------------------------------------------------*/

iDeclareType(Gamepad);
iDeclareTypeConstruction(Gamepad);

iBool   isAvailable_Gamepad     (void);

iBool   isConnected_Gamepad     (const iGamepad *);
iBool   isPointing_Gamepad      (const iGamepad *);
iInt2   pointerCoord_Gamepad    (const iGamepad *);
int     modState_Gamepad        (const iGamepad *);

const char *buttonName_Gamepad  (const iGamepad *, int sdlGameControllerButton);

iLocalDef iBool isPointerHidden_Gamepad(const iGamepad *d) {
    return isConnected_Gamepad(d) && !isPointing_Gamepad(d);
}

void    movePointer_Gamepad             (iGamepad *, iInt2 coord, int span);
void    movePointerOntoWidget_Gamepad   (iGamepad *, iWidget *widget, int span);

iBool   processEvent_Gamepad    (iGamepad *, const void *sdlEvent);
void    draw_Gamepad            (const iGamepad *);
