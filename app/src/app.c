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

#include "app/impl.h"

#include <lagrange/core.h>
#include <lagrange/gmcerts.h>
#include "gmutil.h"
#include "render/text.h"
#include "ui/command.h"
#include "ui/documentwidget.h"
#include "ui/gamepad.h"
#include "ui/keys.h"
#include "ui/labelwidget.h"
#include "ui/root.h"
#include "ui/touch.h"
#include "ui/util.h"
#include "ui/window.h"

#include <the_Foundation/networkproxy.h>
#include <the_Foundation/path.h>

#include <stdarg.h>

//#define LAGRANGE_ENABLE_MOUSE_TOUCH_EMULATION 1

#if defined (iPlatformAppleDesktop)
#   include "platform/macos.h"
#endif
#if defined (iPlatformAppleMobile)
#   include "platform/ios.h"
#   include <CoreFoundation/CoreFoundation.h>
#endif
#if defined (iPlatformAndroidMobile)
#   include "platform/android.h"
#   include <SDL3/SDL_log.h>
#   include <fcntl.h>
#   include <unistd.h>
#endif
#if defined (iPlatformMsys) || defined (iPlatformWindows)
#   include "platform/win32.h"
#endif
#if defined (LAGRANGE_ENABLE_X11_XLIB)
#   include "platform/x11.h"
#endif
#if SDL_VERSION_ATLEAST(2, 0, 14)
#   include <SDL3/SDL_misc.h>
#endif

iApp     app_;
iMutex  *saveMutex_App_;

static const size_t maxRecentlySubmittedInput_App_ = 10;

/*----------------------------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------------------------*/

void init_SavedWidth(iSavedWidth *d, float gaps) {
    d->gaps = gaps;
}

void deinit_SavedWidth(iSavedWidth *d) {
    iUnused(d);
}

iDefineObjectConstructionArgs(SavedWidth, (float gaps), gaps);
iDefineClass(SavedWidth)

/*----------------------------------------------------------------------------------------------*/

const iStringArray *recentlySubmittedInput_App(void) {
    return app_.recentlySubmittedInput;
}

void saveSubmittedInput_App(const iString *queryInput) {
    iApp *d = &app_;
    iStringArray *array = d->recentlySubmittedInput;
    /* Avoid duplicates in the history since there is limited space. */
    iForEach(StringArray, i, array) {
        if (equalCase_String(i.value, queryInput)) {
            remove_StringArray(array, index_StringArrayIterator(&i));
            break;
        }
    }
    pushBack_StringArray(array, queryInput);
    if (size_StringArray(array) > maxRecentlySubmittedInput_App_) {
        remove_StringArray(array, 0);
    }
    saveStateQuickly_App();
}

void clearSubmittedInput_App(void) {
    iApp *d = &app_;
    clear_StringArray(d->recentlySubmittedInput);
    /* Don't save right away, in case this was an accident. */
}

iBool checkSavedWidth_App(const iString *resizeId, float *gaps_out) {
    iApp *d = &app_;
    const iSavedWidth *saved = constValue_StringHash(d->savedWidths, resizeId);
    if (saved) {
        if (gaps_out) {
            *gaps_out = saved->gaps;
        }
        return iTrue;
    }
    return iFalse;
}

const iPrefs *prefs_App(void) {
    return &app_.prefs;
}

iBool forceSoftwareRender_App(void) {
    if (app_.forceSoftwareRender) {
        return iTrue;
    }
#if defined (LAGRANGE_ENABLE_X11_SWRENDER)
    if (getenv("DISPLAY")) {
        return iTrue;
    }
#endif
    return iFalse;
}

void setForceSoftwareRender_App(iBool sw) {
    app_.forceSoftwareRender = sw;
}

void setInputZoomLevel_App(int level) {
    app_.prefs.inputZoomLevel = level;
}

void setEditorZoomLevel_App(int level) {
    app_.prefs.editorZoomLevel = level;
}

void setRecentMenuBarIndex_App(int index) {
    app_.prefs.recentMenuBarIndex = index;
}

void setRecentMisfinId_App(const iGmIdentity *ident) {
    iString *str = &app_.prefs.strings[recentMisfinId_PrefsString];
    if (ident) {
        set_String(str, collect_String(hexEncode_Block(&ident->fingerprint)));
    }
    else {
        clear_String(str);
    }
}

enum iColorTheme colorTheme_App(void) {
    return app_.prefs.theme;
}

void postRefresh_Window(iAnyWindow *windowPtr) {
    iApp *d = &app_;
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
    d->isIdling = iFalse;
#endif
    iWindow *window = windowPtr;
    iAtomicInt *pendingWindow = (window ? &window->isRefreshPending : NULL);
    iBool wasPending = exchange_Atomic(&d->pendingRefresh, iTrue);
    if (pendingWindow) {
        wasPending |= exchange_Atomic(pendingWindow, iTrue);
    }
    if (!wasPending) {
        SDL_Event ev = { .type = SDL_EVENT_USER };
        ev.user.code = refresh_UserEventCode;
        SDL_PushEvent(&ev);
    }
}

void postRefreshAllWindows_App(void) {
    iApp *d = &app_;
    iConstForEach(PtrArray, m, &d->mainWindows) {
        postRefresh_Window(m.ptr);
    }
    iConstForEach(PtrArray, x, &d->extraWindows) {
        postRefresh_Window(x.ptr);
    }
    iConstForEach(PtrArray, p, &d->popupWindows) {
        postRefresh_Window(p.ptr);
    }
}

void postCommand_Root(iRoot *d, const char *command) {
    iAssert(command);
    if (strlen(command) == 0) {
        return;
    }
    if (*command == global_CommandPrefix) {
        /* Global command; this is global context so just ignore. */
        command++;
    }
    if (*command == deferred_CommandPrefix) {
        /* Requires launch to be finished; defer it if needed. */
        command++;
        if (!app_.isFinishedLaunching) {
            pushBackCStr_StringList(app_.launchCommands, command);
            return;
        }
    }
    SDL_Event ev = { .type = SDL_EVENT_USER };
    ev.user.code = command_UserEventCode;
    ev.user.data1 = iDupStr(command);
    ev.user.data2 = d; /* all events are root-specific */
    ev.user.windowID = d ? id_Window(d->window) : 0; /* root-specific means window-specific */
    SDL_PushEvent(&ev);
    iWindow *win = d ? d->window : NULL;
    if (app_.commandEcho) {
        const int windowIndex =
            win && type_Window(win) == main_WindowType ? windowIndex_App(as_MainWindow(win)) + 1 : 0;
        printf("%s%s[command] {%d:%d} %s\n",
               !app_.isFinishedLaunching ? "<Ln> " : "",
               app_.isLoadingPrefs ? "<Pr> " : "",
               windowIndex,
               (d == NULL || win == NULL ? 0
                : d == win->roots[0]     ? 1
                                         : 2),
               command);
        fflush(stdout);
    }
}

void postCommandf_Root(iRoot *d, const char *command, ...) {
    iBlock chars;
    init_Block(&chars, 0);
    va_list args;
    va_start(args, command);
    vprintf_Block(&chars, command, args);
    va_end(args);
    postCommand_Root(d, cstr_Block(&chars));
    deinit_Block(&chars);
}

void postCommandf_App(const char *command, ...) {
    iBlock chars;
    init_Block(&chars, 0);
    va_list args;
    va_start(args, command);
    vprintf_Block(&chars, command, args);
    va_end(args);
    postCommand_Root(NULL, cstr_Block(&chars));
    deinit_Block(&chars);
}

static void notifyString_(iRoot *d, iString *cmd) {
    makeNotification_Command(cmd);
    postCommandString_Root(d, cmd);
}

void notify_Root(iRoot *d, const char *command) {
    iString cmd;
    initCStr_String(&cmd, command);
    notifyString_(d, &cmd);
    deinit_String(&cmd);
}

void notifyf_Root(iRoot *d, const char *command, ...) {
    iString cmd;
    init_String(&cmd);
    va_list args;
    va_start(args, command);
    vprintf_Block(&cmd.chars, command, args);
    va_end(args);
    notifyString_(d, &cmd);
    deinit_String(&cmd);
}

void notifyf_App(const char *command, ...) {
    iString cmd;
    init_String(&cmd);
    va_list args;
    va_start(args, command);
    vprintf_Block(&cmd.chars, command, args);
    va_end(args);
    notifyString_(NULL, &cmd);
    deinit_String(&cmd);
}

iAny *findWidget_App(const char *id) {
    if (!*id) return NULL;
    iConstForEach(PtrArray, w, regularWindows_App()) {
        iRoot *order[2];
        rootOrder_Window(w.ptr, order);
        iForIndices(i, order) {
            if (order[i]) {
                iAny *found = findChild_Widget(order[i]->widget, id);
                if (found) {
                    return found;
                }
            }
        }
    }
    return NULL;
}

iBool moveFocusInsideMenu_App(const void *sdlEvent) {
    if (!focus_Widget()) {
        return iFalse;
    }
    const SDL_Event *event = sdlEvent;
    if (event->type != SDL_EVENT_KEY_DOWN) {
        return iFalse;
    }
    const int key = event->key.key;
    /* The menubar has special behavior for focus changing to navigate between sibling menus. */
    iWidget *menu = parentMenu_Widget(focus_Widget());
    if (menu) {
        const size_t focusIndex = indexOfChild_Widget(menu, focus_Widget());
        if (key >= 'a' && key <= 'z') {
            /* See if any menu item starts with a matching letter. */
            iWidget *firstMatch = NULL;
            size_t index = 0;
            iForEach(ObjectList, i, children_Widget(menu)) {
                if (isInstance_Object(i.object, &Class_LabelWidget)) {
                    iLabelWidget *item = i.object;
                    char prefix[2] = { key, 0 };
                    if (startsWithCase_String(text_LabelWidget(item), prefix)) {
                        if (!firstMatch) {
                            firstMatch = i.object;
                        }
                        if (focus_Widget() != i.object && index > focusIndex) {
                            setCurrent_Window(window_Widget(focus_Widget()));
                            setFocus_Widget(i.object);
                            return iTrue;
                        }
                    }
                }
                index++;
            }
            if (firstMatch) {
                /* Loop back around. */
                setCurrent_Window(window_Widget(focus_Widget()));
                setFocus_Widget(firstMatch);
                return iTrue;
            }
        }
        else if (key == SDLK_PAGEUP || key == SDLK_PAGEDOWN || key == SDLK_HOME || key == SDLK_END) {
            /* Move to top/bottom of menu. */
            enum iDirection dir =
                (key == SDLK_PAGEUP || key == SDLK_HOME ? up_Direction : down_Direction);
            iWidget *next = focus_Widget();
            for (;;) {
                iWidget *adjacent = findAdjacentFocusable_Widget(next, dir);
                if (!adjacent || adjacent == next) break;
                next = adjacent;
            }
            setCurrent_Window(window_Widget(focus_Widget()));
            setFocus_Widget(next);
            return iTrue;
        }
    }
    if (key == SDLK_LEFT || key == SDLK_RIGHT) {
        /* Arrow keys in the menubar will switch between top-level menus. */
        iWidget *menubar = findParent_Widget(focus_Widget(), "menubar");
        if (menubar) {
            iWidget *button = parent_Widget(parent_Widget(focus_Widget()));
            size_t index = indexOfChild_Widget(menubar, button);
            if (index == iInvalidPos) {
                return iFalse;
            }
            const size_t curIndex = index;
            if (key == SDLK_LEFT && index > 0) {
                index--;
            }
            else if (key == SDLK_RIGHT && index < childCount_Widget(menubar) - 1) {
                index++;
            }
            if (curIndex != index) {
                setCurrent_Window(window_Widget(focus_Widget()));
                setFocus_Widget(child_Widget(menubar, index));
                postCommand_Widget(child_Widget(menubar, index), "trigger");
            }
            return iTrue;
        }
        else if (menu) {
            setCurrent_Window(window_Widget(focus_Widget()));
            postCommand_Widget(focus_Widget(), "cancel");
        }
    }
    return iFalse;
}

iBool moveFocusWithArrows_App(const void *sdlEvent) {
    if (!focus_Widget()) {
        return iFalse;
    }
    const SDL_Event *event = sdlEvent;
    if (event->type != SDL_EVENT_KEY_DOWN) {
        return iFalse;
    }
    const int key = event->key.key;
    iWidget *nextFocus = findAdjacentFocusable_Widget(focus_Widget(),
                                                        key == SDLK_UP    ? up_Direction
                                                      : key == SDLK_DOWN  ? down_Direction
                                                      : key == SDLK_LEFT  ? left_Direction
                                                      : key == SDLK_RIGHT ? right_Direction
                                                                          : none_Direction);
    if (nextFocus) {
        setCurrent_Window(window_Widget(focus_Widget()));
        setFocusWithMethod_Widget(nextFocus, arrowKeys_FocusMethod);
        return iTrue;
    }
    return focusRoot_Widget(focus_Widget()) != root_Widget(focus_Widget());
}

iMimeHooks *mimeHooks_App(void) {
    return app_.mimehooks;
}

iPeriodic *periodic_App(void) {
    return &app_.periodic;
}

iGamepad *gamepad_App(void) {
    return app_.gamepad;
}

iRoot *submenuRoot_MacOS(void) {
#if defined (iPlatformAppleDesktop) && defined (LAGRANGE_NATIVE_MENU)
    return app_.submenuRoot;
#else
    return NULL;
#endif
}

iBool isRunningUnderWindowSystem_App(void) {
    return app_.isRunningUnderWindowSystem;
}

iBool isRunningUnderWayland_App(void) {
    return app_.isRunningUnderWayland;
}

void setTextInputActive_App(iBool active) {
    app_.isTextInputActive = active;
    if (!app_.window) {
        /* May be called while tearing down widgets during shutdown. */
        return;
    }
    SDL_Window *win = as_Window(app_.window)->win;
    if (active) {
        SDL_StartTextInput(win);
    }
    else {
        SDL_StopTextInput(win);
    }
}

iBool isTextInputActive_App(void) {
    return app_.isTextInputActive;
}

const iCommandLine *commandLine_App(void) {
    return &app_.args;
}

iGmCerts *certs_App(void) {
    return app_.certs;
}

iVisited *visited_App(void) {
    return app_.visited;
}

iBookmarks *bookmarks_App(void) {
    return app_.bookmarks;
}

iBool willUseProxy_App(const iRangecc scheme) {
    return schemeProxy_Prefs(get_Prefs(), scheme) != NULL;
}

const iString *searchQueryUrl_App(const iString *queryStringUnescaped) {
    iApp *d = &app_;
    if (isEmpty_String(&d->prefs.strings[searchUrl_PrefsString])) {
        return collectNew_String();
    }
    const iString *escaped = urlEncode_String(queryStringUnescaped);
    return collectNewFormat_String(
        "%s?%s", cstr_String(&d->prefs.strings[searchUrl_PrefsString]), cstr_String(escaped));
}

void resetFonts_App(void) {
    iApp *d = &app_;
    iPtrArray *windows = listWindows_App_(d, collectNew_PtrArray());
    /* Mark all Text instances for refresh first. Windows may be sharing them. */
    iConstForEach(PtrArray, pre, windows) {
        text_Window(pre.ptr)->needRefresh = iTrue;
    }
    iConstForEach(PtrArray, win, windows) {
        resetFontsIfNeeded_Text(text_Window(win.ptr));
    }
}

void availableFontsChanged_App(void) {
    iApp *d = &app_;
    iConstForEach(PtrArray, win, listWindows_App_(d, collectNew_PtrArray())) {
        resetMissing_Text(text_Window(win.ptr));
    }
}
