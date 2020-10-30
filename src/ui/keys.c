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

#include "keys.h"
#include "util.h"
#include "app.h"

#include <the_Foundation/sortedarray.h>

iDeclareType(Keys)
iDeclareType(Binding)

struct Impl_Binding {
    int key;
    int mods;
    iString command;
    iString label;
};

static int cmp_Binding_(const void *a, const void *b) {
    const iBinding *d = a, *other = b;
    const int cmp = iCmp(d->key, other->key);
    if (cmp == 0) {
        return iCmp(d->mods, other->mods);
    }
    return cmp;
}

struct Impl_Keys {
    iSortedArray bindings;
};

static iKeys keys_;

static void clear_Keys_(iKeys *d) {
    iForEach(Array, i, &d->bindings.values) {
        iBinding *bind = i.value;
        deinit_String(&bind->command);
        deinit_String(&bind->label);
    }
}

static void bindDefaults_(void) {
    bind_Keys("scroll.top", SDLK_HOME, 0);
    bind_Keys("scroll.bottom", SDLK_END, 0);
    bind_Keys("scroll.step arg:-1", SDLK_UP, 0);
    bind_Keys("scroll.step arg:1", SDLK_DOWN, 0);
    bind_Keys("scroll.page arg:-1", SDLK_PAGEUP, 0);
    bind_Keys("scroll.page arg:-1", SDLK_SPACE, KMOD_SHIFT);
    bind_Keys("scroll.page arg:1", SDLK_PAGEDOWN, 0);
    bind_Keys("scroll.page arg:1", SDLK_SPACE, 0);
}

static iBinding *find_Keys_(iKeys *d, int key, int mods) {
    const iBinding bind = { .key = key, .mods = mods };
    size_t pos;
    if (locate_SortedArray(&d->bindings, &bind, &pos)) {
        return at_SortedArray(&d->bindings, pos);
    }
    return NULL;
}

static iBinding *findCommand_Keys_(iKeys *d, const char *command) {
    /* Note: O(n) */
    iForEach(Array, i, &d->bindings.values) {
        iBinding *bind = i.value;
        if (!cmp_String(&bind->command, command)) {
            return bind;
        }
    }
    return NULL;
}

/*----------------------------------------------------------------------------------------------*/

void init_Keys(void) {
    iKeys *d = &keys_;
    init_SortedArray(&d->bindings, sizeof(iBinding), cmp_Binding_);
    bindDefaults_();
}

void deinit_Keys(void) {
    iKeys *d = &keys_;
    clear_Keys_(d);
    deinit_SortedArray(&d->bindings);
}

void load_Keys(const char *saveDir) {

}

void save_Keys(const char *saveDir) {

}

void bind_Keys(const char *command, int key, int mods) {
    iKeys *d = &keys_;
    iBinding *bind = find_Keys_(d, key, mods);
    if (bind) {
        setCStr_String(&bind->command, command);
    }
    else {
        iBinding bind;
        bind.key = key;
        bind.mods = mods;
        initCStr_String(&bind.command, command);
        init_String(&bind.label);
        insert_SortedArray(&d->bindings, &bind);
    }
}

void setLabel_Keys(const char *command, const char *label) {
    iBinding *bind = findCommand_Keys_(&keys_, command);
    if (bind) {
        setCStr_String(&bind->label, label);
    }
}

//const iString *label_Keys(const char *command) {

//}

//const char *shortcutLabel_Keys(const char *command) {}

iBool processEvent_Keys(const SDL_Event *ev) {
    iKeys *d = &keys_;
    if (ev->type == SDL_KEYDOWN) {
        const iBinding *bind = find_Keys_(d, ev->key.keysym.sym, keyMods_Sym(ev->key.keysym.mod));
        if (bind) {
            postCommandString_App(&bind->command);
            return iTrue;
        }
    }
    return iFalse;
}
