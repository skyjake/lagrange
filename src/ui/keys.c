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

#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <the_Foundation/ptrset.h>

iDeclareType(Keys)

static int cmpPtr_Binding_(const void *a, const void *b) {
    const iBinding *d = *(const void **) a, *other = *(const void **) b;
    const int cmp = iCmp(d->key, other->key);
    if (cmp == 0) {
        return iCmp(d->mods, other->mods);
    }
    return cmp;
}

/*----------------------------------------------------------------------------------------------*/

struct Impl_Keys {
    iArray  bindings;
    iPtrSet lookup; /* quick key/mods lookup */
};

static iKeys keys_;

static void clear_Keys_(iKeys *d) {
    iForEach(Array, i, &d->bindings) {
        iBinding *bind = i.value;
        deinit_String(&bind->command);
        deinit_String(&bind->label);
    }
}

static void bindDefaults_(void) {
    /* TODO: This indirection could be used for localization, although all UI strings
       would need to be similarly handled. */
    bindLabel_Keys(1, "scroll.top", SDLK_HOME, 0, "Jump to top");
    bindLabel_Keys(2, "scroll.bottom", SDLK_END, 0, "Jump to bottom");
    bindLabel_Keys(10, "scroll.step arg:-1", SDLK_UP, 0, "Scroll up");
    bindLabel_Keys(11, "scroll.step arg:1", SDLK_DOWN, 0, "Scroll down");
    bindLabel_Keys(20, "scroll.page arg:-1", SDLK_PAGEUP, 0, "Scroll up half a page");
    bindLabel_Keys(21, "scroll.page arg:1", SDLK_PAGEDOWN, 0, "Scroll down half a page");
    /* The following cannot currently be changed (built-in duplicates). */
    bind_Keys(1000, "scroll.page arg:-1", SDLK_SPACE, KMOD_SHIFT);
    bind_Keys(1001, "scroll.page arg:1", SDLK_SPACE, 0);
}

static iBinding *find_Keys_(iKeys *d, int key, int mods) {
    size_t pos;
    const iBinding elem = { .key = key, .mods = mods };
    if (locate_PtrSet(&d->lookup, &elem, &pos)) {
        return at_PtrSet(&d->lookup, pos);
    }
    return NULL;
}

static iBinding *findId_Keys_(iKeys *d, int id) {
    iForEach(Array, i, &d->bindings) {
        iBinding *bind = i.value;
        if (bind->id == id) {
            return bind;
        }
    }
    return NULL;
}

static iBinding *findCommand_Keys_(iKeys *d, const char *command) {
    /* Note: O(n) */
    iForEach(Array, i, &d->bindings) {
        iBinding *bind = i.value;
        if (!cmp_String(&bind->command, command)) {
            return bind;
        }
    }
    return NULL;
}

static void updateLookup_Keys_(iKeys *d) {
    clear_PtrSet(&d->lookup);
    iConstForEach(Array, i, &d->bindings) {
        insert_PtrSet(&d->lookup, i.value);
    }
}

void setKey_Binding(int id, int key, int mods) {
    iBinding *bind = findId_Keys_(&keys_, id);
    if (bind) {
        bind->key = key;
        bind->mods = mods;
        updateLookup_Keys_(&keys_);
    }
}

/*----------------------------------------------------------------------------------------------*/

static const char *filename_Keys_ = "bindings.txt";

void init_Keys(void) {
    iKeys *d = &keys_;
    init_Array(&d->bindings, sizeof(iBinding));
    initCmp_PtrSet(&d->lookup, cmpPtr_Binding_);
    bindDefaults_();
    updateLookup_Keys_(d);
}

void deinit_Keys(void) {
    iKeys *d = &keys_;
    clear_Keys_(d);
    deinit_PtrSet(&d->lookup);
    deinit_Array(&d->bindings);
}

void load_Keys(const char *saveDir) {
    iKeys *d = &keys_;
    iFile *f = iClob(newCStr_File(concatPath_CStr(saveDir, filename_Keys_)));
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        iBlock * src  = collect_Block(readAll_File(f));
        iRangecc line = iNullRange;
        while (nextSplit_Rangecc(range_Block(src), "\n", &line)) {
            int id, key;
            char modBits[5];
            iZap(modBits);
            sscanf(line.start, "%d %x %4s", &id, &key, modBits);
            iBinding *bind = findId_Keys_(d, id);
            if (bind) {
                bind->key = key;
                bind->mods = 0;
                const char *m = modBits;
                for (int i = 0; i < 4 && *m; i++, m++) {
                    if (*m == 's') bind->mods |= KMOD_SHIFT;
                    if (*m == 'a') bind->mods |= KMOD_ALT;
                    if (*m == 'c') bind->mods |= KMOD_CTRL;
                    if (*m == 'g') bind->mods |= KMOD_GUI;
                }
            }
        }
    }
    updateLookup_Keys_(d);
}

void save_Keys(const char *saveDir) {
    iKeys *d = &keys_;
    iFile *f = newCStr_File(concatPath_CStr(saveDir, filename_Keys_));
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        iString *line = collectNew_String();
        iConstForEach(Array, i, &d->bindings) {
            const iBinding *bind = i.value;
            format_String(line, "%d %x ", bind->id, bind->key);
            if (bind->mods == 0) {
                appendCStr_String(line, "0");
            }
            else {
                if (bind->mods & KMOD_SHIFT) appendChar_String(line, 's');
                if (bind->mods & KMOD_ALT) appendChar_String(line, 'a');
                if (bind->mods & KMOD_CTRL) appendChar_String(line, 'c');
                if (bind->mods & KMOD_GUI) appendChar_String(line, 'g');
            }
            appendChar_String(line, '\n');
            write_File(f, &line->chars);
        }
        iRelease(f);
    }
}

void bind_Keys(int id, const char *command, int key, int mods) {
    iKeys *d = &keys_;
    iBinding *bind = findId_Keys_(d, id);
    if (!bind) {
        iBinding elem = { .id = id, .key = key, .mods = mods };
        initCStr_String(&elem.command, command);
        init_String(&elem.label);
        pushBack_Array(&d->bindings, &elem);
    }
    else {
        setCStr_String(&bind->command, command);
        bind->key  = key;
        bind->mods = mods;
    }
}

void setLabel_Keys(int id, const char *label) {
    iBinding *bind = findId_Keys_(&keys_, id);
    if (bind) {
        setCStr_String(&bind->label, label);
    }
}

#if 0
const iString *label_Keys(const char *command) {
    iKeys *d = &keys_;
    /* TODO: A hash wouldn't hurt here. */
    iConstForEach(PtrSet, i, &d->bindings) {
        const iBinding *bind = *i.value;
        if (!cmp_String(&bind->command, command) && !isEmpty_String(&bind->label)) {
            return &bind->label;
        }
    }
    return collectNew_String();
}
#endif

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

const iBinding *findCommand_Keys(const char *command) {
    return findCommand_Keys_(&keys_, command);
}

const iPtrArray *list_Keys(void) {
    iKeys *d = &keys_;
    iPtrArray *list = collectNew_PtrArray();
    iConstForEach(Array, i, &d->bindings) {
        pushBack_PtrArray(list, i.value);
    }
    return list;
}
