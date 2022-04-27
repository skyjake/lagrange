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
#include "window.h"
#include "app.h"

#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <the_Foundation/ptrset.h>
#include <SDL_keyboard.h>

enum iModMap {
    none_ModMap,
    leftShift_ModMap,
    leftControl_ModMap,
    leftAlt_ModMap,
    leftGui_ModMap,
    rightShift_ModMap,
    rightControl_ModMap,
    rightAlt_ModMap,
    rightGui_ModMap,
    capsLock_ModMap,
    max_ModMap
};

static const char *modToStr_[max_ModMap] = {
    "none",
    "Lshift",
    "Lctrl",
    "Lalt",
    "Lgui",
    "Rshift",
    "Rctrl",
    "Ralt",
    "Rgui",
    "caps",
};

static int strToMod_(iRangecc str) {
    trim_Rangecc(&str);
    for (int i = 0; i < max_ModMap; i++) {
        if (equalCase_Rangecc(str, modToStr_[i])) {
            return i;
        }
    }
    return none_ModMap;
}

static int modMap_[max_ModMap];
static iBool capsLockDown_;

static void init_ModMap_(void) {
    for (int i = 0; i < max_ModMap; i++) {
        modMap_[i] = i;
    }
}

int mapMods_Keys(int modFlags) {
    static const int bits[max_ModMap] = {
        0,
        KMOD_LSHIFT,
        KMOD_LCTRL,
        KMOD_LALT,
        KMOD_LGUI,
        KMOD_RSHIFT,
        KMOD_RCTRL,
        KMOD_RALT,
        KMOD_RGUI,
        KMOD_CAPS,
    };
    int mapped = 0;
    /* Treat capslock as a modifier key. */
    modFlags |= (capsLockDown_ ? KMOD_CAPS : 0);
    for (int i = 0; i < max_ModMap; ++i) {
        if (modFlags & bits[i]) {
            mapped |= bits[modMap_[i]];
        }
    }
    return mapped;
}

int modState_Keys(void) {
    int state = SDL_GetModState() & ~(KMOD_NUM | KMOD_MODE | KMOD_CAPS);
    /* Treat capslock as a modifier key. */
    if (capsLockDown_) state |= KMOD_CAPS;
    return mapMods_Keys(state);
}

void setCapsLockDown_Keys(iBool isDown) {
    capsLockDown_ = isDown;
}

static void loadModMap_Keys_(const char *saveDir) {
    iFile *f = iClob(newCStr_File(concatPath_CStr(saveDir, "modmap.txt")));
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        const iString *text = collect_String(readString_File(f));
        iRangecc textLine = iNullRange;
        while (nextSplit_Rangecc(range_String(text), "\n", &textLine)) {
            iRangecc line = textLine;
            trim_Rangecc(&line);
            if (isEmpty_Range(&line) || startsWith_Rangecc(line, "#")) {
                continue; /* comment */
            }
            iRangecc seg = iNullRange;
            if (nextSplit_Rangecc(line, "->", &seg)) {
                const int fromMod = strToMod_(seg);
                if (fromMod && nextSplit_Rangecc(line, "->", &seg)) {
                    const int toMod = strToMod_(seg);
                    modMap_[fromMod] = toMod;
                }
            }
        }
        close_File(f);
    }
    else {
        open_File(f, writeOnly_FileMode | text_FileMode);
        printf_Stream(stream_File(f),
                      "# This is a translation table for keyboard modifiers. The syntax is:\n"
                      "#\n"
                      "# (hardware key) -> (effective modifier)\n"
                      "#\n"
                      "# A modifier can be mapped to \"none\" to disable it. For example:\n"
                      "#\n"
                      "# Lalt -> none\n"
                      "#\n"
                      "# When using CapsLock as a modifier key, its toggled state will still affect\n"
                      "# text entry. You may need to remap or disable CapsLock in your window system.\n"
                      "#\n"
                      "# You may delete this file and it will be recreated with the default mapping.\n\n");
        for (int i = 1; i < max_ModMap; i++) {
            printf_Stream(stream_File(f), "%s -> %s\n", modToStr_[i], modToStr_[i]);
        }
        close_File(f);
    }
}

/*----------------------------------------------------------------------------------------------*/

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

enum iBindFlag {
    argRepeat_BindFlag  = iBit(1),
    argRelease_BindFlag = iBit(2),
    noDirectTrigger_BindFlag = iBit(3), /* can only be triggered via LabelWidget */
};

/* TODO: This indirection could be used for localization, although all UI strings
   would need to be similarly handled. */
static const struct { int id; iMenuItem bind; int flags; } defaultBindings_[] = {
    { 1,  { "${keys.top}",                  SDLK_HOME, 0,                   "scroll.top"                        }, 0 },
    { 2,  { "${keys.bottom}",               SDLK_END, 0,                    "scroll.bottom"                     }, 0 },
    { 10, { "${keys.scroll.up}",            SDLK_UP, 0,                     "scroll.step arg:-1"                }, argRepeat_BindFlag },
    { 11, { "${keys.scroll.down}",          SDLK_DOWN, 0,                   "scroll.step arg:1"                 }, argRepeat_BindFlag },
    { 22, { "${keys.scroll.halfpage.up}",   SDLK_SPACE, KMOD_SHIFT,         "scroll.page arg:-1"                }, argRepeat_BindFlag },
    { 23, { "${keys.scroll.halfpage.down}", SDLK_SPACE, 0,                  "scroll.page arg:1"                 }, argRepeat_BindFlag },
    { 24, { "${keys.scroll.page.up}",       SDLK_PAGEUP, 0,                 "scroll.page arg:-1 full:1"         }, argRepeat_BindFlag },
    { 25, { "${keys.scroll.page.down}",     SDLK_PAGEDOWN, 0,               "scroll.page arg:1 full:1"          }, argRepeat_BindFlag },
    { 30, { "${keys.back}",                 navigateBack_KeyShortcut,       "navigate.back"                     }, 0 },
    { 31, { "${keys.forward}",              navigateForward_KeyShortcut,    "navigate.forward"                  }, 0 },
    { 32, { "${keys.parent}",               navigateParent_KeyShortcut,     "navigate.parent"                   }, 0 },
    { 33, { "${keys.root}",                 navigateRoot_KeyShortcut,       "navigate.root"                     }, 0 },
    { 35, { "${keys.reload}",               reload_KeyShortcut,             "document.reload"                   }, 0 },
    { 36, { "${LC:menu.openlocation}",      SDLK_l, KMOD_PRIMARY,           "navigate.focus"                    }, 0 },
    { 41, { "${keys.link.modkey}",          SDLK_LALT, 0,                   "document.linkkeys arg:0"           }, argRelease_BindFlag },
    { 42, { "${keys.link.homerow}",         'f', 0,                         "document.linkkeys arg:1"           }, 0 },
    { 45, { "${keys.link.homerow.newtab}",  'f', KMOD_SHIFT,                "document.linkkeys arg:1 newtab:1"  }, 0 },
    { 46, { "${keys.link.homerow.hover}",   'h', 0,                         "document.linkkeys arg:1 hover:1"   }, 0 },
    { 47, { "${keys.link.homerow.next}",    '.', 0,                         "document.linkkeys more:1"          }, 0 },
    { 50, { "${keys.bookmark.add}",         bookmarkPage_KeyShortcut,       "bookmark.add"                      }, 0 },
    { 51, { "${keys.bookmark.addfolder}",   'n', KMOD_SHIFT,                "bookmarks.addfolder"               }, 0 },
    { 55, { "${keys.subscribe}",            subscribeToPage_KeyShortcut,    "feeds.subscribe"                   }, 0 },
    { 56, { "${keys.feeds.showall}",        SDLK_u, KMOD_SHIFT,             "feeds.mode arg:0"                  }, 0 },
    { 57, { "${keys.feeds.showunread}",     SDLK_u, 0,                      "feeds.mode arg:1"                  }, 0 },
    { 60, { "${keys.findtext}",             'f', KMOD_PRIMARY,              "focus.set id:find.input"           }, 0 },
    { 65, { "${LC:menu.viewformat.plain}",  SDLK_y, KMOD_PRIMARY,           "document.viewformat"               }, 0 },
    { 70, { "${keys.zoom.in}",              SDLK_EQUALS, KMOD_ZOOM,         "zoom.delta arg:10"                 }, 0 },
    { 71, { "${keys.zoom.out}",             SDLK_MINUS, KMOD_ZOOM,          "zoom.delta arg:-10"                }, 0 },
    { 72, { "${keys.zoom.reset}",           SDLK_0, KMOD_ZOOM,              "zoom.set arg:100"                  }, 0 },
#if !defined (iPlatformApple) /* Ctrl-Cmd-F on macOS */
    { 73, { "${keys.fullscreen}",           SDLK_F11, 0,                    "window.fullscreen"                 }, 0 },
#endif
    { 76, { "${keys.tab.new}",              newTab_KeyShortcut,             "tabs.new"                          }, 0 },
    { 77, { "${keys.tab.close}",            closeTab_KeyShortcut,           "tabs.close"                        }, 0 },
    { 78, { "${keys.tab.close.other}",      SDLK_w, KMOD_SECONDARY,         "tabs.close toleft:1 toright:1"     }, 0 },
    { 79, { "${LC:menu.reopentab}",         SDLK_t, KMOD_SECONDARY,         "tabs.new reopen:1"                 }, 0 },        
    { 80, { "${keys.tab.prev}",             prevTab_KeyShortcut,            "tabs.prev"                         }, 0 },
    { 81, { "${keys.tab.next}",             nextTab_KeyShortcut,            "tabs.next"                         }, 0 },
    { 90, { "${keys.split.menu}",           SDLK_j, KMOD_PRIMARY,           "splitmenu.open"                    }, 0 },
    { 91, { "${keys.split.next}",           SDLK_TAB, KMOD_CTRL,            "keyroot.next",                     }, 0 },
    { 92, { "${keys.split.item} ${menu.split.merge}",           '1', 0,     "ui.split arg:0",                   }, noDirectTrigger_BindFlag },
    { 93, { "${keys.split.item} ${menu.split.swap}",            SDLK_x, 0,  "ui.split swap:1",                  }, noDirectTrigger_BindFlag },
    { 94, { "${keys.split.item} ${menu.split.horizontal}",      '3', 0,     "ui.split arg:3 axis:0",            }, noDirectTrigger_BindFlag },
    { 95, { "${keys.split.item} ${menu.split.horizontal} 1:2",  SDLK_d, 0,  "ui.split arg:1 axis:0",            }, noDirectTrigger_BindFlag },
    { 96, { "${keys.split.item} ${menu.split.horizontal} 2:1",  SDLK_e, 0,  "ui.split arg:2 axis:0",            }, noDirectTrigger_BindFlag },
    { 97, { "${keys.split.item} ${menu.split.vertical}",        '2', 0,     "ui.split arg:3 axis:1",            }, noDirectTrigger_BindFlag },
    { 98, { "${keys.split.item} ${menu.split.vertical} 1:2",    SDLK_f, 0,  "ui.split arg:1 axis:1",            }, noDirectTrigger_BindFlag },
    { 99, { "${keys.split.item} ${menu.split.vertical} 2:1",    SDLK_r, 0,  "ui.split arg:2 axis:1",            }, noDirectTrigger_BindFlag },
    { 100,{ "${keys.hoverurl}",             '/', KMOD_PRIMARY,              "prefs.hoverlink.toggle"            }, 0 },
    { 110,{ "${menu.save.downloads}",       SDLK_s, KMOD_PRIMARY,           "document.save"                     }, 0 },
    { 120,{ "${keys.upload}",               SDLK_u, KMOD_PRIMARY,           "document.upload"                   }, 0 },
    { 121,{ "${keys.upload.edit}",          SDLK_e, KMOD_PRIMARY,           "document.upload copy:1"            }, 0 },
    { 125,{ "${keys.pageinfo}",             pageInfo_KeyShortcut,           "document.info"                     }, 0 },
    { 126,{ "${keys.sitespec}",             ',', KMOD_SECONDARY,            "document.sitespec"                 }, 0 },
    { 130,{ "${keys.input.precedingline}",  SDLK_v, KMOD_SECONDARY,         "input.precedingline"               }, 0 },
    { 140,{ "${keys.identmenu}",            identityMenu_KeyShortcut,       "identmenu.open focus:1"            }, 0 },          
    { 200,{ "${keys.menubar.focus}",        menuBar_KeyShortcut,            "menubar.focus"                     }, 0 },
    { 205,{ "${keys.contextmenu}",          '/', 0,                         "contextkey"                        }, 0 },
    /* The following cannot currently be changed (built-in duplicates). */
#if defined (iPlatformApple)
    { 1002, { NULL, SDLK_LEFTBRACKET,  KMOD_PRIMARY,             "navigate.back"        }, 0 },
    { 1003, { NULL, SDLK_RIGHTBRACKET, KMOD_PRIMARY,             "navigate.forward"     }, 0 },
    { 1100, { NULL, SDLK_SPACE,        KMOD_PRIMARY | KMOD_CTRL, "emojipicker"          }, 0 },
#endif
    { 1004, { NULL, SDLK_F5, 0,                         "document.reload"               }, 0 },
    /* Media keys. */
    { 1005, { NULL, SDLK_AC_SEARCH, 0,                  "focus.set id:find.input"       }, 0 },
    { 1006, { NULL, SDLK_AC_HOME, 0,                    "navigate.home"                 }, 0 },
    { 1007, { NULL, SDLK_AC_BACK, 0,                    "navigate.back"                 }, 0 },
    { 1008, { NULL, SDLK_AC_FORWARD, 0,                 "navigate.forward"              }, 0 },
    { 1009, { NULL, SDLK_AC_STOP, 0,                    "document.stop"                 }, 0 },
    { 1010, { NULL, SDLK_AC_REFRESH, 0,                 "document.reload"               }, 0 },
    { 1011, { NULL, SDLK_AC_BOOKMARKS, 0,               "sidebar.mode arg:0 toggle:1"   }, 0 },
};

static iBinding *findId_Keys_(iKeys *d, int id) {
    iForEach(Array, i, &d->bindings) {
        iBinding *bind = i.value;
        if (bind->id == id) {
            return bind;
        }
    }
    return NULL;
}

static void setFlags_Keys_(int id, int bindFlags) {
    iBinding *bind = findId_Keys_(&keys_, id);
    if (bind) {
        bind->flags = bindFlags;
    }
}

static void bindDefaults_(void) {
    iForIndices(i, defaultBindings_) {
        const int       id   = defaultBindings_[i].id;
        const iMenuItem bind = defaultBindings_[i].bind;
        bind_Keys(id, bind.command, bind.key, bind.kmods);
        if (bind.label) {
            setLabel_Keys(id, bind.label);
        }
        setFlags_Keys_(id, defaultBindings_[i].flags);
    }
}

static iBinding *find_Keys_(iKeys *d, int key, int mods) {
    size_t pos;
    /* Do not differentiate between left and right modifier keys. */
    key = normalizedMod_Sym(key);
    if (isMod_Sym(key)) {
        mods = 0;
    }
    const iBinding elem = { .key = key, .mods = mods };
    if (locate_PtrSet(&d->lookup, &elem, &pos)) {
        return at_PtrSet(&d->lookup, pos);
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
        const iBinding *bind = i.value;
        if (~bind->flags & noDirectTrigger_BindFlag) {
            insert_PtrSet(&d->lookup, i.value);
        }
    }
}

void setKey_Binding(int id, int key, int mods) {
    iBinding *bind = findId_Keys_(&keys_, id);
    if (bind) {
        bind->key  = normalizedMod_Sym(key);
        bind->mods = isMod_Sym(key) ? 0 : mods;
        updateLookup_Keys_(&keys_);
    }
}

void reset_Binding(int id) {
    iBinding *bind = findId_Keys_(&keys_, id);
    if (bind) {
        iForIndices(i, defaultBindings_) {
            if (defaultBindings_[i].id == id) {
                bind->key  = defaultBindings_[i].bind.key;
                bind->mods = defaultBindings_[i].bind.kmods;
                updateLookup_Keys_(&keys_);
                break;
            }
        }
    }
}

/*----------------------------------------------------------------------------------------------*/

#if defined (iPlatformTerminal)
static const char *filename_Keys_ = "cbindings.txt";
#else
static const char *filename_Keys_ = "bindings.txt";
#endif

void init_Keys(void) {
    iKeys *d = &keys_;
    init_ModMap_();
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
    loadModMap_Keys_(saveDir);
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
                    if (*m == 'k') bind->mods |= KMOD_CAPS;
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
                if (bind->mods & KMOD_CAPS) appendChar_String(line, 'k');
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

iBool processEvent_Keys(const SDL_Event *ev) {
    iKeys *d = &keys_;
    iRoot *root = get_Window() ? get_Window()->keyRoot : NULL;
    if (ev->type == SDL_KEYDOWN || ev->type == SDL_KEYUP) {
        const iBinding *bind = find_Keys_(d, ev->key.keysym.sym, keyMods_Sym(ev->key.keysym.mod));
        if (bind) {
            if (ev->type == SDL_KEYUP) {
                if (bind->flags & argRelease_BindFlag) {
                    postCommandf_Root(root, "%s release:1", cstr_String(&bind->command));
                    return iTrue;
                }
                return iFalse;
            }
            if (ev->key.repeat && (bind->flags & argRepeat_BindFlag)) {
                postCommandf_Root(root, "%s repeat:1", cstr_String(&bind->command));
            }
            else {
                postCommandf_Root(root, "%s keydown:1", cstr_String(&bind->command));
            }
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
