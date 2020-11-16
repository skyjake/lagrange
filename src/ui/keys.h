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

#include <the_Foundation/string.h>
#include <the_Foundation/ptrarray.h>
#include <SDL_events.h>

#if defined (iPlatformApple)
#   define reload_KeyShortcut           SDLK_r,             KMOD_PRIMARY
#   define prevTab_KeyShortcut          SDLK_LEFTBRACKET,   KMOD_SHIFT | KMOD_PRIMARY
#   define nextTab_KeyShortcut          SDLK_RIGHTBRACKET,  KMOD_SHIFT | KMOD_PRIMARY
#   define navigateBack_KeyShortcut     SDLK_LEFT,          KMOD_PRIMARY
#   define navigateForward_KeyShortcut  SDLK_RIGHT,         KMOD_PRIMARY
#   define navigateParent_KeyShortcut   SDLK_UP,            KMOD_PRIMARY
#   define navigateRoot_KeyShortcut     SDLK_UP,            KMOD_SHIFT | KMOD_PRIMARY
#   define byWord_KeyModifier           KMOD_ALT
#   define byLine_KeyModifier           KMOD_PRIMARY
#else
#   define reload_KeyShortcut           SDLK_r,             KMOD_PRIMARY
#   define prevTab_KeyShortcut          SDLK_PAGEUP,        KMOD_PRIMARY
#   define nextTab_KeyShortcut          SDLK_PAGEDOWN,      KMOD_PRIMARY
#   define navigateBack_KeyShortcut     SDLK_LEFT,          KMOD_ALT
#   define navigateForward_KeyShortcut  SDLK_RIGHT,         KMOD_ALT
#   define navigateParent_KeyShortcut   SDLK_UP,            KMOD_ALT
#   define navigateRoot_KeyShortcut     SDLK_UP,            KMOD_SHIFT | KMOD_ALT
#   define byWord_KeyModifier           KMOD_CTRL
#   define byLine_KeyModifier           0
#endif

iDeclareType(Binding)

struct Impl_Binding {
    int id;
    int key;
    int mods;
    iString command;
    iString label;
};

void            setKey_Binding      (int id, int key, int mods);

/*----------------------------------------------------------------------------------------------*/

void            init_Keys           (void);
void            deinit_Keys         (void);

void            load_Keys           (const char *saveDir);
void            save_Keys           (const char *saveDir);

void            bind_Keys           (int id, const char *command, int key, int mods);
void            setLabel_Keys       (int id, const char *label);

iLocalDef void bindLabel_Keys(int id, const char *command, int key, int mods, const char *label) {
    bind_Keys(id, command, key, mods);
    setLabel_Keys(id, label);
}

const iBinding *findCommand_Keys    (const char *command);

iBool           processEvent_Keys   (const SDL_Event *);
const iPtrArray *list_Keys          (void);
