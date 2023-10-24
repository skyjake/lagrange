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

/* These are defaults for bindings that are different depending on the platform. */

#define newIdentity_KeyShortcut         SDLK_n,             KMOD_SECONDARY
#define identityMenu_KeyShortcut        SDLK_i,             KMOD_SECONDARY

#if defined (iPlatformTerminal)
#   define pageInfo_KeyShortcut         SDLK_i,             0
#   define preferences_KeyShortcut      SDLK_COMMA,         0
#   define reload_KeyShortcut           SDLK_r,             0
#   define newTab_KeyShortcut           SDLK_t,             0
#   define closeTab_KeyShortcut         SDLK_w,             KMOD_PRIMARY
#   define prevTab_KeyShortcut          SDLK_LEFTBRACKET,   0
#   define nextTab_KeyShortcut          SDLK_RIGHTBRACKET,  0
#   define moveTabLeft_KeyShortcut      SDLK_LEFTBRACKET,   KMOD_ALT
#   define moveTabRight_KeyShortcut     SDLK_RIGHTBRACKET,  KMOD_ALT
#   define navigateBack_KeyShortcut     SDLK_LEFT,          0
#   define navigateForward_KeyShortcut  SDLK_RIGHT,         0
#   define navigateParent_KeyShortcut   SDLK_r,             KMOD_SHIFT
#   define navigateRoot_KeyShortcut     SDLK_r,             KMOD_PRIMARY
#   define bookmarkPage_KeyShortcut     SDLK_d,             0
#   define subscribeToPage_KeyShortcut  SDLK_d,             KMOD_SHIFT
#   define refreshFeeds_KeyShortcut     SDLK_r,             KMOD_ALT
#   define leftSidebar_KeyShortcut      SDLK_l,             KMOD_SHIFT
#   define rightSidebar_KeyShortcut     SDLK_p,             KMOD_SHIFT
#   define menuBar_KeyShortcut          '?',                0
#   define leftSidebarTab_KeyModifier   0
#   define byWord_KeyModifier           KMOD_CTRL
#   define byLine_KeyModifier           KMOD_ALT
#   define rightSidebarTab_KeyModifier  KMOD_ALT
#elif defined (iPlatformApple)
#   define pageInfo_KeyShortcut         SDLK_i, KMOD_PRIMARY
#   define preferences_KeyShortcut      SDLK_COMMA,         KMOD_PRIMARY
#   define reload_KeyShortcut           SDLK_r,             KMOD_PRIMARY
#   define newTab_KeyShortcut           SDLK_t,             KMOD_PRIMARY
#   define closeTab_KeyShortcut         SDLK_w,             KMOD_PRIMARY
#   define prevTab_KeyShortcut          SDLK_LEFTBRACKET,   KMOD_SECONDARY
#   define nextTab_KeyShortcut          SDLK_RIGHTBRACKET,  KMOD_SECONDARY
#   define moveTabLeft_KeyShortcut      SDLK_LEFTBRACKET,   KMOD_TERTIARY
#   define moveTabRight_KeyShortcut     SDLK_RIGHTBRACKET,  KMOD_TERTIARY
#   define navigateBack_KeyShortcut     SDLK_LEFT,          KMOD_PRIMARY
#   define navigateForward_KeyShortcut  SDLK_RIGHT,         KMOD_PRIMARY
#   define navigateParent_KeyShortcut   SDLK_UP,            KMOD_PRIMARY
#   define navigateRoot_KeyShortcut     SDLK_UP,            KMOD_SECONDARY
#   define bookmarkPage_KeyShortcut     SDLK_d,             KMOD_PRIMARY
#   define subscribeToPage_KeyShortcut  SDLK_d,             KMOD_SECONDARY
#   define refreshFeeds_KeyShortcut     SDLK_r,             KMOD_SECONDARY
#   define leftSidebar_KeyShortcut      SDLK_l,             KMOD_SECONDARY
#   define rightSidebar_KeyShortcut     SDLK_p,             KMOD_SECONDARY
#   define menuBar_KeyShortcut          SDLK_F10,           0
#   define leftSidebarTab_KeyModifier   KMOD_PRIMARY
#   define byWord_KeyModifier           KMOD_ALT
#   define byLine_KeyModifier           KMOD_PRIMARY
#   define rightSidebarTab_KeyModifier  KMOD_CTRL
#else
#   define pageInfo_KeyShortcut         SDLK_i, KMOD_PRIMARY
#   define preferences_KeyShortcut      SDLK_COMMA,         KMOD_PRIMARY
#   define reload_KeyShortcut           SDLK_r,             KMOD_PRIMARY
#   define newTab_KeyShortcut           SDLK_t,             KMOD_PRIMARY
#   define closeTab_KeyShortcut         SDLK_w,             KMOD_PRIMARY
#   define prevTab_KeyShortcut          SDLK_PAGEUP,        KMOD_PRIMARY
#   define nextTab_KeyShortcut          SDLK_PAGEDOWN,      KMOD_PRIMARY
#   define moveTabLeft_KeyShortcut      SDLK_PAGEUP,        KMOD_SECONDARY
#   define moveTabRight_KeyShortcut     SDLK_PAGEDOWN,      KMOD_SECONDARY
#   define navigateBack_KeyShortcut     SDLK_LEFT,          KMOD_ALT
#   define navigateForward_KeyShortcut  SDLK_RIGHT,         KMOD_ALT
#   define navigateParent_KeyShortcut   SDLK_UP,            KMOD_ALT
#   define navigateRoot_KeyShortcut     SDLK_UP,            KMOD_SHIFT | KMOD_ALT
#   define bookmarkPage_KeyShortcut     SDLK_d,             KMOD_PRIMARY
#   define subscribeToPage_KeyShortcut  SDLK_d,             KMOD_SECONDARY
#   define refreshFeeds_KeyShortcut     SDLK_r,             KMOD_SECONDARY
#   define leftSidebar_KeyShortcut      SDLK_l,             KMOD_SECONDARY
#   define rightSidebar_KeyShortcut     SDLK_p,             KMOD_SECONDARY
#   define menuBar_KeyShortcut          SDLK_F10,           0
#   define leftSidebarTab_KeyModifier   KMOD_PRIMARY
#   define byWord_KeyModifier           KMOD_CTRL
#   define byLine_KeyModifier           0
#   define rightSidebarTab_KeyModifier  KMOD_SHIFT | KMOD_CTRL
#endif

#define builtIn_BindingId   1000    /* not user-configurable */

iDeclareType(Binding)

struct Impl_Binding {
    int id;
    int flags;
    int key;
    int mods;
    iString command;
    iString label;
};

void            setKey_Binding      (int id, int key, int mods);
void            reset_Binding       (int id);

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

int             mapMods_Keys        (int modFlags);
int             modState_Keys       (void); /* current modifier key state */
void            setCapsLockDown_Keys(iBool isDown);

iBool           isDown_Keys         (const iBinding *binding);
