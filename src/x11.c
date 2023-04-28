/* Copyright 2023 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "x11.h"
#include "ui/command.h"
#include "ui/window.h"
#include "prefs.h"
#include "app.h"

#include <SDL_syswm.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

iBool isXSession_X11(void) {
    const char *driver = SDL_GetCurrentVideoDriver();
    if (driver && !iCmpStr(driver, "wayland")) {
        return iFalse;
    }
    return iTrue; /* assume yes if this source file is being used */
}

void setDarkWindowTheme_SDLWindow(SDL_Window *d, iBool setDark) {
    if (!isXSession_X11()) {
        return;
    }
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (SDL_GetWindowWMInfo(d, &wmInfo)) {
        Display *   dpy   = wmInfo.info.x11.display;
        Window      wnd   = wmInfo.info.x11.window;
        Atom        prop  = XInternAtom(dpy, "_GTK_THEME_VARIANT", False);
        Atom        u8    = XInternAtom(dpy, "UTF8_STRING", False);
        const char *value = setDark ? "dark" : "light";
        XChangeProperty(dpy, wnd, prop, u8, 8, PropModeReplace,
                        (unsigned char *) value, strlen(value));        
    }
}

void handleCommand_X11(const char *cmd) {
    if (!isXSession_X11()) {
        return;
    }
    if (equal_Command(cmd, "theme.changed")) {        
        iConstForEach(PtrArray, iter, mainWindows_App()) {
            iMainWindow *mw = iter.ptr;
            setDarkWindowTheme_SDLWindow(
                mw->base.win, isDark_ColorTheme(prefs_App()->theme));
        }
    }
}

