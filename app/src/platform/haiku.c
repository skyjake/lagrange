/* Copyright 2026 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "haiku.h"

#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <SDL3/SDL_keyboard.h>
#include <FindDirectory.h>

enum iHaikuMod {
    leftShift_HaikuMod,
    rightShift_HaikuMod,
    leftControl_HaikuMod,
    rightControl_HaikuMod,
    leftAlt_HaikuMod,
    rightAlt_HaikuMod,
    leftGui_HaikuMod,
    rightGui_HaikuMod,
    max_HaikuMod
};

static const SDL_Keycode modKeys_[max_HaikuMod] = {
    SDLK_LSHIFT, SDLK_RSHIFT, SDLK_LCTRL, SDLK_RCTRL, SDLK_LALT, SDLK_RALT, SDLK_LGUI, SDLK_RGUI,
};

static const int modBits_[max_HaikuMod] = {
    SDL_KMOD_LSHIFT, SDL_KMOD_RSHIFT, SDL_KMOD_LCTRL, SDL_KMOD_RCTRL,
    SDL_KMOD_LALT,   SDL_KMOD_RALT,   SDL_KMOD_LGUI,  SDL_KMOD_RGUI,
};

/* Haiku key codes of the modifier keys. */
static const uint32_t haikuKeys_[max_HaikuMod] = {
    0x4b, 0x56, 0x5c, 0x60, 0x5d, 0x5f, 0x66, 0x67,
};

static int roles_[max_HaikuMod] = { 0, 1, 2, 3, 4, 5, 6, 7 }; /* -1 if not a modifier */

static int indexOfKey_(SDL_Keycode key) {
    for (int i = 0; i < max_HaikuMod; i++) {
        if (modKeys_[i] == key) {
            return i;
        }
    }
    return -1;
}

static int indexOfHaikuKey_(uint32_t key) {
    for (int i = 0; i < max_HaikuMod; i++) {
        if (haikuKeys_[i] == key) {
            return i;
        }
    }
    return -1;
}

static int keyWithRole_(int role) {
    for (int i = 0; i < max_HaikuMod; i++) {
        if (roles_[i] == role) {
            return i;
        }
    }
    return -1;
}

void loadKeymap_Haiku(void) {
    /* Order of the modifier fields in "key_map", following the Caps/Scroll/Num keys. */
    static const int roles[] = {
        leftShift_HaikuMod,   rightShift_HaikuMod,      /* Shift */
        leftAlt_HaikuMod,     rightGui_HaikuMod,        /* Command */
        leftControl_HaikuMod, rightControl_HaikuMod,    /* Control */
        leftGui_HaikuMod,     rightAlt_HaikuMod,        /* Option */
    };
    for (int i = 0; i < max_HaikuMod; i++) {
        roles_[i] = i;
    }
    char dir[B_PATH_NAME_LENGTH];
    if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, false, dir, sizeof(dir)) != B_OK) {
        return;
    }
    iFile *f = iClob(newCStr_File(concatPath_CStr(dir, "Key_map")));
    if (!open_File(f, readOnly_FileMode)) {
        return; /* default keymap */
    }
    if (size_File(f) >= 4 * (4 + iElemCount(roles))) {
        setByteOrder_Stream(stream_File(f), bigEndian_StreamByteOrder);
        seek_File(f, 4 * 4);
        for (int i = 0; i < max_HaikuMod; i++) {
            roles_[i] = -1;
        }
        iForIndices(i, roles) {
            const int phys = indexOfHaikuKey_(readU32_File(f));
            if (phys >= 0) {
                roles_[phys] = roles[i];
            }
        }
    }
    close_File(f);
}

int mapMods_Haiku(int kmods) {
    int mapped = kmods & ~(SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI);
    for (int i = 0; i < max_HaikuMod; i++) {
        if ((kmods & modBits_[i]) && roles_[i] >= 0) {
            mapped |= modBits_[roles_[i]];
        }
    }
    return mapped;
}

int physicalMods_Haiku(int kmods) {
    static const int roles[][2] = {
        { SDL_KMOD_SHIFT, leftShift_HaikuMod },
        { SDL_KMOD_CTRL,  leftControl_HaikuMod },
        { SDL_KMOD_ALT,   leftAlt_HaikuMod },
        { SDL_KMOD_GUI,   leftGui_HaikuMod },
    };
    int phys = kmods & ~(SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI);
    iForIndices(i, roles) {
        if (kmods & roles[i][0]) {
            const int key = keyWithRole_(roles[i][1]);
            phys |= key >= 0 ? modBits_[key] : roles[i][0];
        }
    }
    return phys;
}

SDL_Keycode mapModKey_Haiku(SDL_Keycode key) {
    const int phys = indexOfKey_(key);
    if (phys < 0) {
        return key;
    }
    return roles_[phys] >= 0 ? modKeys_[roles_[phys]] : SDLK_UNKNOWN;
}

iBool isModKeyDown_Haiku(SDL_Keycode key) {
    const int index = indexOfKey_(key);
    if (index < 0) {
        return iFalse;
    }
    const int bits = (key == SDLK_LALT || key == SDLK_RALT ? SDL_KMOD_ALT : modBits_[index]);
    return (mapMods_Haiku(SDL_GetModState()) & bits) != 0;
}
