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

#include "web.h"
#include "app.h"
#include "color.h"
#include "ui/command.h"
#include "ui/dialog.h"
#include "ui/inputwidget.h"
#include "ui/labelwidget.h"
#include "ui/root.h"
#include "ui/util.h"
#include "ui/window.h"

#include <lagrange/gmutil.h>
#include <lagrange/prefs.h>
#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <the_Foundation/stringlist.h>
#include <the_Foundation/wisp.h>

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>
#include <emscripten.h>
#include <emscripten/eventloop.h>
#include <math.h>
#include <stdlib.h>

static const char *uploadPath_Web_   = "/tmp/upload";
static const char *relayKeyFileName_ = "relay.key";
static const int   autosaveInterval_ = 60 * 1000; /* ms */

static iBool   isStopped_Web_;
static iString *relayPublicKey_Web_;

EM_JS(int, isStoragePersistent_Web_, (void), {
    return Module.isStoragePersistent && Module.isStoragePersistent() ? 1 : 0;
})

EM_JS(void, requestStorageSync_Web_, (void), {
    if (Module.syncStorage) {
        Module.syncStorage();
    }
})

void syncStorage_Web(void) {
    requestStorageSync_Web_();
}

static void autosave_Web_(void *ptr) {
    iUnused(ptr);
    saveUserData_App(iFalse);
}

EM_JS(int, isAppleDevice_Web_, (void), {
    const platform = navigator.userAgentData ? navigator.userAgentData.platform : navigator.platform;
    return /^(mac|iphone|ipad|ipod)/i.test(platform || "") ? 1 : 0;
})

iBool isApple_Web(void) {
    static int isApple_ = -1;
    if (isApple_ < 0) {
        isApple_ = isAppleDevice_Web_();
    }
    return isApple_;
}

/* The whole page is shown in fullscreen, not just the canvas, so that the page's other
   elements (e.g., the paste target) remain usable. */
EM_JS(void, setFullscreen_Web_, (bool fullscreen), {
    const root = document.documentElement;
    const current = document.fullscreenElement || document.webkitFullscreenElement;
    if (fullscreen && !current) {
        const request = root.requestFullscreen || root.webkitRequestFullscreen;
        if (request) {
            Promise.resolve(request.call(root)).catch((error) => {
                console.warn('Failed to enter fullscreen:', error);
            });
        }
    }
    else if (!fullscreen && current) {
        (document.exitFullscreen || document.webkitExitFullscreen).call(document);
    }
})

EM_JS(int, isFullscreen_Web_, (void), {
    return document.fullscreenElement || document.webkitFullscreenElement ? 1 : 0;
})

void setFullscreen_Web(iBool fullscreen) {
    setFullscreen_Web_(fullscreen);
}

iBool isFullscreen_Web(void) {
    return isFullscreen_Web_();
}

/* Browsers allow some actions, like showing the file picker, only while an input event is
   being handled. SDL has already queued the event, so the app handles it immediately.
   Clipboard text is only available in a paste event, so the paste shortcut is given to the
   browser, which pastes into a hidden text area. */
EM_JS(void, addInputListeners_Web_, (bool isApple), {
    const pasteArea = document.createElement('textarea');
    pasteArea.className = 'paste-target';
    pasteArea.tabIndex = -1;
    pasteArea.setAttribute('aria-hidden', 'true');
    document.body.appendChild(pasteArea);
    pasteArea.addEventListener('paste', (event) => {
        event.preventDefault();
        pasteArea.blur();
        const text = event.clipboardData.getData('text/plain');
        if (text) {
            _pasteText_Web(stringToNewUTF8(text));
        }
    });
    window.addEventListener('keyup', () => pasteArea.blur(), true);
    window.addEventListener('keydown', (event) => {
        const isPrimary = isApple ? event.metaKey && !event.ctrlKey : event.ctrlKey && !event.metaKey;
        const key = event.key.toLowerCase();
        /* Non-Latin layouts use the key's position. */
        const isV = key == 'v' || (key.length == 1 && !/[a-z]/.test(key) && event.code == 'KeyV');
        if ((isPrimary && !event.altKey && !event.shiftKey && isV) ||
            (event.shiftKey && event.key == 'Insert' && !event.ctrlKey && !event.metaKey &&
             !event.altKey)) {
            event.stopImmediatePropagation(); /* not seen by SDL */
            pasteArea.value = "";
            pasteArea.focus();
            return;
        }
        /* SDL lets Command shortcuts through while text input is active. */
        if (event.metaKey) {
            event.preventDefault();
        }
    }, true);
    for (const type of ['keydown', 'pointerdown', 'pointerup']) {
        window.addEventListener(type, () => {
            Module.isHandlingInput = true;
            try {
                _processInput_Web();
            }
            finally {
                Module.isHandlingInput = false;
            }
        });
    }
})

EMSCRIPTEN_KEEPALIVE void processInput_Web(void) {
    /* Commands posted during event handling are queued after SDL's poll sentinel. */
    for (int i = 0; i < 4 && !isStopped_Web_ && SDL_HasEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
         i++) {
        processEvents_App(waitForNewEvents_AppEventMode);
    }
}

EM_JS(void, writeClipboard_Web_, (const char *text), {
    navigator.clipboard.writeText(UTF8ToString(text)).catch((error) => {
        console.warn('Failed to copy to the clipboard:', error);
    });
})

static iBool isPasting_Web_;

/* SDL has no browser clipboard, so copied text is passed on as it is set. */
static bool clipboardWatcher_Web_(void *user, SDL_Event *ev) {
    iUnused(user);
    if (ev->type == SDL_EVENT_CLIPBOARD_UPDATE && ev->clipboard.owner && !isPasting_Web_ &&
        SDL_HasClipboardText()) {
        char *text = SDL_GetClipboardText();
        writeClipboard_Web_(text);
        SDL_free(text);
    }
    return true;
}

static void setClipboardText_Web_(const char *text) {
    isPasting_Web_ = iTrue;
    SDL_SetClipboardText(text);
    isPasting_Web_ = iFalse;
}

static void pushKey_Web_(SDL_Keycode key, SDL_Scancode scancode, SDL_Keymod mods) {
    SDL_Event ev = { .key = { .type      = SDL_EVENT_KEY_DOWN,
                              .timestamp = SDL_GetTicksNS(),
                              .windowID  = SDL_GetWindowID(get_MainWindow()->base.win),
                              .scancode  = scancode,
                              .key       = key,
                              .mod       = mods,
                              .down      = true } };
    SDL_PushEvent(&ev);
    ev.key.type = SDL_EVENT_KEY_UP;
    ev.key.down = false;
    SDL_PushEvent(&ev);
}

/* Replays the paste shortcut now that the clipboard has the pasted text. */
EMSCRIPTEN_KEEPALIVE void pasteText_Web(char *text) {
    setClipboardText_Web_(text);
    free(text);
    if (isStopped_Web_) {
        return;
    }
    pushKey_Web_(SDLK_V, SDL_SCANCODE_V, isApple_Web() ? SDL_KMOD_LGUI : SDL_KMOD_LCTRL);
    processInput_Web();
}

/* Text entered with an on-screen keyboard. The strings are kept until the next input,
   by which time the events have been handled. */
static iStringList *softKeyboardTexts_Web_;

EMSCRIPTEN_KEEPALIVE void softKeyboardText_Web(char *text) {
    if (!isStopped_Web_) {
        if (!softKeyboardTexts_Web_) {
            softKeyboardTexts_Web_ = new_StringList();
        }
        clear_StringList(softKeyboardTexts_Web_);
        pushBackCStr_StringList(softKeyboardTexts_Web_, text);
        SDL_Event ev = { .text = { .type      = SDL_EVENT_TEXT_INPUT,
                                   .timestamp = SDL_GetTicksNS(),
                                   .windowID  = SDL_GetWindowID(get_MainWindow()->base.win),
                                   .text      = cstr_String(constAt_StringList(
                                       softKeyboardTexts_Web_, 0)) } };
        SDL_PushEvent(&ev);
        processInput_Web();
    }
    free(text);
}

EMSCRIPTEN_KEEPALIVE void softKeyboardBackspace_Web(int count) {
    if (!isStopped_Web_) {
        for (int i = 0; i < count; i++) {
            pushKey_Web_(SDLK_BACKSPACE, SDL_SCANCODE_BACKSPACE, 0);
        }
        processInput_Web();
    }
}

EMSCRIPTEN_KEEPALIVE void softKeyboardEnter_Web(void) {
    if (!isStopped_Web_) {
        pushKey_Web_(SDLK_RETURN, SDL_SCANCODE_RETURN, 0);
        processInput_Web();
    }
}

EMSCRIPTEN_KEEPALIVE void softKeyboardHeight_Web(int height) {
    iMainWindow *win = get_MainWindow();
    if (win && !isStopped_Web_) {
        setKeyboardHeight_MainWindow(win, height);
    }
}

/* Mobile browsers show a keyboard only for a focused text field, so a hidden one is
   focused while text is being entered. Typed text arrives as input events. */
EM_JS(void, addSoftKeyboard_Web_, (bool isMobile), {
    const field = document.createElement('textarea');
    field.className = 'text-target';
    field.tabIndex = -1;
    field.autocomplete = 'off';
    field.spellcheck = false;
    field.setAttribute('autocapitalize', 'off');
    field.setAttribute('autocorrect', 'off');
    field.setAttribute('aria-hidden', 'true');
    document.body.appendChild(field);
    /* A placeholder character that Backspace can delete. */
    const placeholder = ' ';
    const newline = String.fromCharCode(10);
    let isComposing = false;
    let isEnterKeyDown = false;
    /* Field contents already passed to the app. */
    let sent = placeholder;
    const reset = () => {
        sent = placeholder;
        field.value = placeholder;
        field.setSelectionRange(placeholder.length, placeholder.length);
    };
    /* Android keyboards compose every word, so changes are passed on during composition
       as deletions and insertions. */
    const sync = () => {
        const value = field.value;
        let common = 0;
        while (common < sent.length && common < value.length &&
               sent.charCodeAt(common) == value.charCodeAt(common)) {
            common++;
        }
        const high = common > 0 ? sent.charCodeAt(common - 1) : 0;
        if (high >= 0xd800 && high <= 0xdbff) {
            common--;
        }
        const deleted = Array.from(sent.substring(common)).length;
        const lines = value.substring(common).split(newline);
        const text = lines.join("");
        /* An Enter key press was already handled. */
        const enters = isEnterKeyDown ? 0 : lines.length - 1;
        /* The app may end the composition while handling these. */
        if (isComposing && value.startsWith(placeholder) && !value.includes(newline)) {
            sent = value;
        }
        else {
            reset();
        }
        if (deleted) {
            _softKeyboardBackspace_Web(deleted);
        }
        if (text) {
            _softKeyboardText_Web(stringToNewUTF8(text));
        }
        for (let i = 0; i < enters; i++) {
            _softKeyboardEnter_Web();
        }
    };
    /* On-screen keyboards may send Enter without a key code, which SDL cannot map, or
       only as a line break. */
    field.addEventListener('keydown', (event) => {
        if (event.key == 'Enter') {
            isEnterKeyDown = true;
            if (event.code != 'Enter' && event.code != 'NumpadEnter') {
                event.preventDefault();
                _softKeyboardEnter_Web();
            }
        }
    });
    field.addEventListener('keyup', (event) => {
        if (event.key == 'Enter') {
            isEnterKeyDown = false;
        }
    });
    field.addEventListener('compositionstart', () => { isComposing = true; });
    field.addEventListener('compositionend', () => {
        isComposing = false;
        sync();
    });
    field.addEventListener('input', sync);
    const useField = isMobile || matchMedia('(pointer: coarse)').matches;
    let isActive = false;
    let isRefocusNeeded = false;
    const focus = () => {
        isEnterKeyDown = false;
        reset();
        field.focus({ preventScroll: true });
    };
    Module.setTextInputActive = (active) => {
        if (!useField) {
            return;
        }
        isActive = active;
        /* iOS shows the keyboard only if focused while handling a tap. */
        isRefocusNeeded = active && !Module.isHandlingInput;
        if (active) {
            focus();
        }
        else {
            field.blur();
        }
    };
    /* Runs after the app has handled the tap. */
    window.addEventListener('pointerup', () => {
        if (isActive && isRefocusNeeded) {
            isRefocusNeeded = false;
            field.blur();
            focus();
        }
    });
    /* The keyboard covers the bottom of the page. */
    if (window.visualViewport) {
        const viewport = window.visualViewport;
        const update = () => {
            const covered = Math.max(0, window.innerHeight - viewport.height - viewport.offsetTop);
            _softKeyboardHeight_Web(Math.round(covered * window.devicePixelRatio));
        };
        viewport.addEventListener('resize', update);
        viewport.addEventListener('scroll', update);
    }
})

EM_JS(void, setTextInputActive_Web_, (bool active), {
    if (Module.setTextInputActive) {
        Module.setTextInputActive(active);
    }
})

void setTextInputActive_Web(iBool active) {
    setTextInputActive_Web_(active);
}

EM_JS(void, readClipboard_Web_, (const char *cmd), {
    const command = UTF8ToString(cmd);
    const done = (text) => _clipboardRead_Web(stringToNewUTF8(command),
                                              text ? stringToNewUTF8(text) : 0);
    if (navigator.clipboard && navigator.clipboard.readText) {
        navigator.clipboard.readText().then(done, () => done(null));
    }
    else {
        done(null);
    }
})

EMSCRIPTEN_KEEPALIVE void clipboardRead_Web(char *cmd, char *text) {
    if (text) {
        setClipboardText_Web_(text);
    }
    if (!isStopped_Web_) {
        postCommandf_App("%s clip:1", cmd);
    }
    free(text);
    free(cmd);
}

/* Command acts as the primary modifier on Apple devices, so it is swapped with Control. */
int swapAppleMods_Web(int mods) {
    if (isApple_Web()) {
        const int ctrl = mods & SDL_KMOD_CTRL;
        const int gui  = mods & SDL_KMOD_GUI;
        mods &= ~(SDL_KMOD_CTRL | SDL_KMOD_GUI);
        if (ctrl & SDL_KMOD_LCTRL) mods |= SDL_KMOD_LGUI;
        if (ctrl & SDL_KMOD_RCTRL) mods |= SDL_KMOD_RGUI;
        if (gui  & SDL_KMOD_LGUI)  mods |= SDL_KMOD_LCTRL;
        if (gui  & SDL_KMOD_RGUI)  mods |= SDL_KMOD_RCTRL;
    }
    return mods;
}

/* The macOS defaults for Apple devices. */
static const struct { int id; int key; int mods; } appleBindings_Web_[] = {
    { 30, SDLK_LEFT,         KMOD_PRIMARY },
    { 31, SDLK_RIGHT,        KMOD_PRIMARY },
    { 32, SDLK_UP,           KMOD_PRIMARY },
    { 33, SDLK_UP,           KMOD_SECONDARY },
    { 80, SDLK_LEFTBRACKET,  KMOD_SECONDARY },
    { 81, SDLK_RIGHTBRACKET, KMOD_SECONDARY },
    { 84, SDLK_LEFTBRACKET,  KMOD_TERTIARY },
    { 85, SDLK_RIGHTBRACKET, KMOD_TERTIARY },
};

void appleDefaultKey_Web(int bindingId, int *key, int *mods) {
    if (isApple_Web()) {
        iForIndices(i, appleBindings_Web_) {
            if (appleBindings_Web_[i].id == bindingId) {
                *key  = appleBindings_Web_[i].key;
                *mods = appleBindings_Web_[i].mods;
                break;
            }
        }
    }
}

static void swapAppleKey_Web_(SDL_KeyboardEvent *ev) {
    static const struct { SDL_Keycode key; SDL_Scancode scan; } swaps_[][2] = {
        { { SDLK_LCTRL, SDL_SCANCODE_LCTRL }, { SDLK_LGUI, SDL_SCANCODE_LGUI } },
        { { SDLK_RCTRL, SDL_SCANCODE_RCTRL }, { SDLK_RGUI, SDL_SCANCODE_RGUI } },
    };
    ev->mod = swapAppleMods_Web(ev->mod);
    if (isApple_Web()) {
        iForIndices(i, swaps_) {
            for (int j = 0; j < 2; j++) {
                if (ev->key == swaps_[i][j].key) {
                    ev->key      = swaps_[i][j ^ 1].key;
                    ev->scancode = swaps_[i][j ^ 1].scan;
                    break;
                }
            }
        }
    }
}

static iBool isWheelStepValue_Web_(float value) {
    /* Mouse wheel steps are 100 or 120 pixels, or 3 lines. */
    const float a = fabsf(value);
    if (a < 0.99f) {
        return a == 0.0f;
    }
    return fabsf(a - roundf(a)) < 0.01f || fabsf(a / 1.2f - roundf(a / 1.2f)) < 0.01f;
}

static iBool isWheelStep_Web_(const SDL_MouseWheelEvent *ev) {
    /* SDL reports browser scrolling in units of 100 pixels. */
    return isWheelStepValue_Web_(ev->x) && isWheelStepValue_Web_(ev->y);
}

enum { wheelGesturePause_Ms = 200 };

/* Browsers do not report when a trackpad gesture ends, which is needed for finishing
   swipe navigation. A pause ends it instead. */
static struct {
    int                 axis;    /* 0: undecided, 1: horizontal, 2: vertical */
    iBool               isEnded; /* the rest of the gesture only scrolls vertically */
    uint64_t            lastTime;
    float               pending[2]; /* movement while the axis is undecided */
    SDL_MouseWheelEvent last;    /* last horizontal event */
    int                 seq;     /* identifies the pending end timer */
} wheelGesture_Web_;

static int lockScrollAxis_Web_(SDL_MouseWheelEvent *ev) {
    /* Only scroll on one axis at a time. */
    if (ev->timestamp - wheelGesture_Web_.lastTime > SDL_MS_TO_NS(wheelGesturePause_Ms)) {
        wheelGesture_Web_.axis = 0;
        wheelGesture_Web_.isEnded = iFalse;
        iZap(wheelGesture_Web_.pending);
    }
    wheelGesture_Web_.lastTime = ev->timestamp;
    if (wheelGesture_Web_.isEnded) {
        ev->x = 0.0f; /* remaining momentum */
    }
    if (wheelGesture_Web_.axis == 0) {
        /* Decide after some movement (in units of 100 px). Horizontal must clearly dominate.
           Vertical movement is applied immediately, horizontal is held back until then. */
        float *pending = wheelGesture_Web_.pending;
        pending[0] += ev->x;
        pending[1] += ev->y;
        if (fabsf(pending[0]) + fabsf(pending[1]) < 0.08f) {
            ev->x = 0.0f;
            return 0;
        }
        wheelGesture_Web_.axis = fabsf(pending[0]) > 2 * fabsf(pending[1]) ? 1 : 2;
        ev->x = pending[0];
        iZap(wheelGesture_Web_.pending);
    }
    if (wheelGesture_Web_.axis == 1) {
        ev->y = 0.0f;
    }
    else if (wheelGesture_Web_.axis == 2) {
        ev->x = 0.0f;
    }
    return wheelGesture_Web_.axis;
}

static Uint32 postWheelGestureEnd_Web_(void *seq, SDL_TimerID id, Uint32 interval) {
    iUnused(id, interval);
    SDL_Event ev = { .user = { .type  = SDL_EVENT_USER,
                               .code  = wheelGestureEnds_UserEventCode,
                               .data1 = seq } };
    SDL_PushEvent(&ev);
    return 0;
}

static void continueWheelGesture_Web_(const SDL_MouseWheelEvent *ev) {
    wheelGesture_Web_.last = *ev;
    SDL_AddTimer(wheelGesturePause_Ms, postWheelGestureEnd_Web_,
                 (void *) (intptr_t) ++wheelGesture_Web_.seq);
}

static iBool endWheelGesture_Web_(SDL_Event *ev) {
    if ((intptr_t) ev->user.data1 != wheelGesture_Web_.seq) {
        return iFalse; /* the gesture has continued, or was ended already */
    }
    ev->wheel           = wheelGesture_Web_.last;
    ev->wheel.timestamp = SDL_GetTicksNS();
    ev->wheel.x         = 0.0f;
    ev->wheel.y         = 0.0f;
    setInertia_MouseWheelEvent(&ev->wheel, iFalse);
    setScrollFinished_MouseWheelEvent(&ev->wheel, iTrue);
    return iTrue;
}

void endWheelGesture_Web(void) {
    wheelGesture_Web_.axis    = 0;
    wheelGesture_Web_.isEnded = iTrue;
    iZap(wheelGesture_Web_.pending);
    wheelGesture_Web_.seq++;
}

static void convertToPerPixel_Web_(SDL_MouseWheelEvent *ev, float pixelRatio) {
    static float accum[2]; /* fractional pixels carried over */
    accum[0] += ev->x * 100.0f * pixelRatio;
    accum[1] += ev->y * 100.0f * pixelRatio;
    ev->x = (int) accum[0];
    ev->y = (int) accum[1];
    accum[0] -= ev->x;
    accum[1] -= ev->y;
    setPerPixel_MouseWheelEvent(ev, iTrue);
}

/* Returns true if the event was consumed. */
static iBool processWheelEvent_Web_(SDL_Event *ev) {
    if (ev->type == SDL_EVENT_USER && ev->user.code == wheelGestureEnds_UserEventCode) {
        return !endWheelGesture_Web_(ev);
    }
    if (ev->type != SDL_EVENT_MOUSE_WHEEL) {
        return iFalse;
    }
    ev->wheel.x = -ev->wheel.x;
    if (!isWheelStep_Web_(&ev->wheel)) {
        const int axis = lockScrollAxis_Web_(&ev->wheel);
        convertToPerPixel_Web_(&ev->wheel, get_MainWindow()->base.pixelRatio);
        if (axis == 1) {
            continueWheelGesture_Web_(&ev->wheel);
        }
        return ev->wheel.x == 0 && ev->wheel.y == 0;
    }
    return iFalse;
}

iBool processEvent_Web(SDL_Event *ev) {
    if (processWheelEvent_Web_(ev)) {
        return iTrue;
    }
    if (ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_KEY_UP) {
        swapAppleKey_Web_(&ev->key);
    }
    else if (isCommand_SDLEvent(ev)) {
        /* Pasting from a menu reads the browser clipboard first. */
        const char *cmd = command_UserEvent(ev);
        if ((equal_Command(cmd, "input.paste") || equal_Command(cmd, "certimport.paste")) &&
            !hasLabel_Command(cmd, "clip") && !hasLabel_Command(cmd, "snippet")) {
            readClipboard_Web_(cmd);
            free(ev->user.data1);
            return iTrue;
        }
    }
    return iFalse;
}

static iBlock *relayKey_Web_(void) {
    const char *path = concatPath_CStr(cstr_String(dataDir_App()), relayKeyFileName_);
    iFile *f = newCStr_File(path);
    iBlock *key = NULL;
    if (open_File(f, readOnly_FileMode)) {
        key = readAll_File(f);
        close_File(f);
    }
    if (!key || size_Block(key) != 32) {
        delete_Block(key);
        key = newPrivateKey_Wisp();
        if (key && open_File(f, writeOnly_FileMode)) {
            write_File(f, key);
            close_File(f);
            syncStorage_Web();
        }
    }
    iRelease(f);
    return key;
}

const iString *relayPublicKey_Web(void) {
    if (!relayPublicKey_Web_) {
        iBlock *key = relayKey_Web_();
        iBlock *pub = key ? newPublicKey_Wisp(key) : NULL;
        relayPublicKey_Web_ = pub ? base64Encode_Block(pub) : new_String();
        delete_Block(pub);
        delete_Block(key);
    }
    return relayPublicKey_Web_;
}

static iString *relayUrl_Web_(const iString *pref) {
    iString *url = copy_String(pref);
    trim_String(url);
    if (isEmpty_String(url)) {
        return url;
    }
    /* Plain HTTP means an unencrypted WebSocket; any other scheme is replaced. */
    const size_t schemeEnd = indexOfCStr_String(url, "://");
    const iBool  isPlain   = startsWithCase_String(url, "ws://") ||
                             startsWithCase_String(url, "http://");
    if (schemeEnd != iInvalidPos) {
        remove_Block(&url->chars, 0, schemeEnd);
    }
    else {
        prependCStr_String(url, "://");
    }
    prependCStr_String(url, isPlain ? "ws" : "wss");
    iUrl parts;
    init_Url(&parts, url);
    if (isEmpty_Range(&parts.path)) {
        appendCStr_String(url, "/wisp/");
    }
    return url;
}

EM_JS(char *, pageHostName_Web_, (void), {
    return stringToNewUTF8(location.hostname);
})

void updateRelay_Web(iPrefs *prefs) {
    iString *url = relayUrl_Web_(&prefs->strings[relayUrl_PrefsString]);
    iBlock *key = NULL;
    /* Plaintext protocols use the relay's built-in proxy. */
    iString *proxy = &prefs->strings[relayProxy_PrefsString];
    if (!isEmpty_String(url)) {
        key = relayKey_Web_();
        iUrl parts;
        init_Url(&parts, url);
        setRange_String(proxy, parts.host);
    }
    else {
        char *host = pageHostName_Web_();
        setCStr_String(proxy, host);
        free(host);
    }
    if (!isEmpty_String(proxy)) {
        appendCStr_String(proxy, ":1965");
    }
    setUrl_Wisp(cstr_String(url));
    setCredentials_Wisp(cstr_String(&prefs->strings[relayUser_PrefsString]),
                        cstr_String(&prefs->strings[relayPassword_PrefsString]),
                        key);
    delete_Block(key);
    delete_String(url);
}

void addRelayPrefs_Web(iWidget *headings, iWidget *values) {
    makeTwoColumnHeading_Widget("${heading.prefs.relay}", headings, values);
    iInputWidget *field = new_InputWidget(0);
    setHint_InputWidget(field, "${hint.relay.url}");
    addPrefsInputWithHeading_Widget(headings, values, "prefs.relay.url", iClob(field));
    addDialogPadding_Widget(headings, values);
    addPrefsInputWithHeading_Widget(headings, values, "prefs.relay.user", iClob(field = new_InputWidget(0)));
    setHint_InputWidget(field, "${hint.optional}");
    addPrefsInputWithHeading_Widget(headings, values, "prefs.relay.password", iClob(field = new_InputWidget(0)));
    setSensitiveContent_InputWidget(field, iTrue);
    setHint_InputWidget(field, "${hint.optional}");
    addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.relay.key}")));
    iWidget *key = new_Widget();
    addChildFlags_Widget(key,
                         iClob(new_LabelWidget(cstr_String(relayPublicKey_Web()), NULL)),
                         frameless_WidgetFlag);
    addChild_Widget(key, iClob(new_LabelWidget("${menu.copy}", "prefs.relay.key.copy")));
    addChildFlags_Widget(values, iClob(key), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
}

/* The page around the canvas has the UI background color. Safari also uses it for the
   toolbar. */
EM_JS(void, setPageColors_Web_, (int red, int green, int blue, bool isDark), {
    const color = 'rgb(' + red + ', ' + green + ', ' + blue + ')';
    document.documentElement.style.colorScheme = isDark ? 'dark' : 'light';
    document.documentElement.style.setProperty('--background', color);
    /* Replaces the page's defaults for light and dark mode. */
    document.querySelectorAll('meta[name="theme-color"]').forEach((m) => m.remove());
    const meta = document.createElement('meta');
    meta.name = 'theme-color';
    meta.content = color;
    document.head.appendChild(meta);
})

static void updatePageColors_Web_(void) {
    const iColor bg = get_Color(uiBackground_ColorId);
    setPageColors_Web_(bg.r, bg.g, bg.b, isDark_ColorTheme(prefs_App()->theme));
}

/* Returns the current theme as bits (1: dark, 2: high contrast). */
EM_JS(int, systemTheme_Web_, (void), {
    return (window.matchMedia('(prefers-color-scheme: dark)').matches ? 1 : 0) |
           (window.matchMedia('(prefers-contrast: more)').matches ? 2 : 0);
})

iBool isDarkSystemTheme_Web(iBool *isHighContrast) {
    const int theme = systemTheme_Web_();
    *isHighContrast = (theme & 2) != 0;
    return theme & 1;
}

EM_JS(void, switchVariant_Web_, (const char *name), {
    Module.switchVariant(UTF8ToString(name));
})

void handleCommand_Web(const char *cmd) {
    if (equal_Command(cmd, "theme.changed")) {
        updatePageColors_Web_();
    }
    else if (equal_Command(cmd, "web.variant")) {
        static const char *names_[] = { "desktop", "tablet", "phone" };
        const int variant = arg_Command(cmd);
        if (variant >= 0 && variant < iElemCount(names_) && variant != deviceType_App()) {
            switchVariant_Web_(names_[variant]);
        }
    }
    else if (equal_Command(cmd, "prefs.relay.key.copy")) {
        SDL_SetClipboardText(cstr_String(relayPublicKey_Web()));
    }
}

EMSCRIPTEN_KEEPALIVE void systemThemeChanged_Web(int isDark, int isHighContrast) {
    if (!isStopped_Web_) {
        postCommandf_App("os.theme.changed dark:%d contrast:%d", isDark, isHighContrast);
    }
}

/* Calls systemThemeChanged_Web() when the theme changes. */
EM_JS(void, watchSystemTheme_Web_, (void), {
    const dark = window.matchMedia('(prefers-color-scheme: dark)');
    const contrast = window.matchMedia('(prefers-contrast: more)');
    const notify = () => _systemThemeChanged_Web(dark.matches ? 1 : 0, contrast.matches ? 1 : 0);
    dark.addEventListener('change', notify);
    contrast.addEventListener('change', notify);
})

void init_Web(void) {
    addInputListeners_Web_(isApple_Web());
    addSoftKeyboard_Web_(deviceType_App() != desktop_AppDeviceType);
    SDL_AddEventWatch(clipboardWatcher_Web_, NULL);
    watchSystemTheme_Web_();
    updatePageColors_Web_();
    emscripten_set_interval(autosave_Web_, autosaveInterval_, NULL);
    if (!isStoragePersistent_Web_()) {
        iRoot *oldRoot = current_Root();
        setCurrent_Root(get_Window()->roots[0]);
        makeSimpleMessage_Widget(uiTextCaution_ColorEscape "${heading.storage.unavailable}",
                                 "${dlg.storage.unavailable}");
        setCurrent_Root(oldRoot);
    }
}

iBool isStopped_Web(void) {
    return isStopped_Web_;
}

/* iOS Safari does not send the change event to an input that is not in the document. */
EM_JS(void, openFilePicker_Web_, (const char *cmd), {
    const command = UTF8ToString(cmd);
    document.querySelectorAll('.file-target').forEach((old) => old.remove());
    const input = document.createElement('input');
    input.type = 'file';
    input.className = 'file-target';
    input.tabIndex = -1;
    input.setAttribute('aria-hidden', 'true');
    document.body.appendChild(input);
    input.addEventListener('cancel', () => input.remove());
    input.onchange = async () => {
        const file = input.files[0];
        input.remove();
        if (!file) {
            return;
        }
        const data = new Uint8Array(await file.arrayBuffer());
        const dataPtr = _malloc(Math.max(1, data.length));
        HEAPU8.set(data, dataPtr);
        const cmdPtr = stringToNewUTF8(command);
        const namePtr = stringToNewUTF8(file.name);
        _receiveFile_Web(cmdPtr, namePtr, dataPtr, data.length);
        _free(namePtr);
        _free(cmdPtr);
    };
    input.click();
})

void pickFile_Web(const char *cmd) {
    openFilePicker_Web_(cmd);
}

EMSCRIPTEN_KEEPALIVE void receiveFile_Web(const char *cmd, const char *name, void *data,
                                          size_t size) {
    iString *fileName = newCStr_String(name);
    replace_String(fileName, "/", "_");
    makeDirs_Path(collectNewCStr_String(uploadPath_Web_));
    const char *path = concatPath_CStr(uploadPath_Web_, cstr_String(fileName));
    iFile *f = newCStr_File(path);
    if (open_File(f, writeOnly_FileMode)) {
        writeData_File(f, data, size);
        close_File(f);
        postCommandf_App("%s path:%s", cmd, path);
    }
    iRelease(f);
    delete_String(fileName);
    free(data);
}

EM_JS(void, download_Web_, (const char *name, const char *mime, const void *data, size_t size), {
    const blob = new Blob([HEAPU8.slice(data, data + size)], { type: UTF8ToString(mime) });
    const link = document.createElement('a');
    link.href = URL.createObjectURL(blob);
    link.download = UTF8ToString(name);
    document.body.appendChild(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(link.href), 60000);
})

void exportDownloadedFile_Web(const iString *localPath, const iString *mime) {
    iFile *f = new_File(localPath);
    if (open_File(f, readOnly_FileMode)) {
        iBlock *data = readAll_File(f);
        close_File(f);
        download_Web_(cstr_Rangecc(baseName_Path(localPath)),
                      isEmpty_String(mime) ? "application/octet-stream" : cstr_String(mime),
                      constData_Block(data),
                      size_Block(data));
        delete_Block(data);
        /* The browser has its own copy now. */
        remove(cstr_String(localPath));
    }
    iRelease(f);
}

EMSCRIPTEN_KEEPALIVE void openUrl_Web(const char *url) {
    if (!isStopped_Web_) {
        postCommandf_App("open newtab:1 url:%s", url);
    }
}

/* The browser's Back (-1) or Forward (1) button was used on the page. */
EMSCRIPTEN_KEEPALIVE void navigate_Web(int dir) {
    if (isStopped_Web_ || !get_MainWindow()) {
        return;
    }
    if (dir < 0) {
        pushKey_Web_(SDLK_AC_BACK, SDL_SCANCODE_AC_BACK, 0);
    }
    else {
        pushKey_Web_(SDLK_AC_FORWARD, SDL_SCANCODE_AC_FORWARD, 0);
    }
    processInput_Web();
}

void runMainLoop_Web(void (*iterate)(void *), void *context) {
    emscripten_set_main_loop_arg(iterate, context, 0, true);
}

EMSCRIPTEN_KEEPALIVE void stop_Web(int save) {
    if (isStopped_Web_) {
        return;
    }
    if (save) {
        saveUserData_App(iTrue);
    }
    isStopped_Web_ = iTrue;
    emscripten_cancel_main_loop();
}
