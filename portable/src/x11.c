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

static iBool getDesktop_X11(Display *dpy, Window w, unsigned long *out) {
    Atom           actual_type;
    int            actual_fmt;
    unsigned long  n, after;
    unsigned char *data = NULL;
    Atom           net  = XInternAtom(dpy, "_NET_WM_DESKTOP", False);
    if (XGetWindowProperty(dpy,
                           w,
                           net,
                           0,
                           1,
                           False,
                           AnyPropertyType,
                           &actual_type,
                           &actual_fmt,
                           &n,
                           &after,
                           &data) == Success &&
        actual_type != None && actual_fmt == 32 && n == 1) {
        *out = *(unsigned long *) data;
        XFree(data);
        return iTrue;
    }
    if (data) XFree(data);
    Atom win = XInternAtom(dpy, "_WIN_WORKSPACE", False); /* old GNOME hint */
    if (XGetWindowProperty(dpy,
                           w,
                           win,
                           0,
                           1,
                           False,
                           AnyPropertyType,
                           &actual_type,
                           &actual_fmt,
                           &n,
                           &after,
                           &data) == Success &&
        actual_type != None && actual_fmt == 32 && n == 1) {
        *out = *(unsigned long *) data;
        XFree(data);
        return iTrue;
    }
    if (data) XFree(data);
    return iFalse;
}

iBool getWindowDesktop_X11(SDL_Window *win, unsigned long *out) {
    if (!isXSession_X11()) {
        return iFalse;
    }
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo(win, &wm) || wm.subsystem != SDL_SYSWM_X11) {
        return iFalse;
    }
    return getDesktop_X11(wm.info.x11.display, wm.info.x11.window, out);
}

void setWindowDesktopPropOnly_X11(SDL_Window *win, unsigned long desk) {
    if (!isXSession_X11()) return;
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo(win, &wm) || wm.subsystem != SDL_SYSWM_X11) {
        return;
    }
    Display *dpy            = wm.info.x11.display;
    Window   w              = wm.info.x11.window;
    Atom     NET_WM_DESKTOP = XInternAtom(dpy, "_NET_WM_DESKTOP", False);
    XChangeProperty(
        dpy, w, NET_WM_DESKTOP, XA_CARDINAL, 32, PropModeReplace, (unsigned char *) &desk, 1);
    Atom WIN_WORKSPACE = XInternAtom(dpy, "_WIN_WORKSPACE", False);
    XChangeProperty(
        dpy, w, WIN_WORKSPACE, XA_CARDINAL, 32, PropModeReplace, (unsigned char *) &desk, 1);
    XFlush(dpy);
}

iBool getCurrentDesktop_X11(SDL_Window *anyWin, unsigned long *out) {
    if (!isXSession_X11()) return iFalse;
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo(anyWin, &wm) || wm.subsystem != SDL_SYSWM_X11) {
        return iFalse;
    }
    Display       *dpy  = wm.info.x11.display;
    Window         root = DefaultRootWindow(dpy);
    Atom           CUR  = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
    Atom           type;
    int            fmt;
    unsigned long  n, after;
    unsigned char *data = NULL;
    if (XGetWindowProperty(
            dpy, root, CUR, 0, 1, False, XA_CARDINAL, &type, &fmt, &n, &after, &data) == Success &&
        type == XA_CARDINAL && fmt == 32 && n == 1) {
        *out = *(unsigned long *) data;
        XFree(data);
        return iTrue;
    }
    if (data) XFree(data);
    return iFalse;
}

void setWindowDesktop_X11(SDL_Window *win, unsigned long desk) {
    if (!isXSession_X11()) {
        return;
    }
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo(win, &wm) || wm.subsystem != SDL_SYSWM_X11) {
        return;
    }
    if (!wm.info.x11.display || !wm.info.x11.window) {
        return;
    }
    Display          *dpy  = wm.info.x11.display;
    Window            w    = wm.info.x11.window;
    Window            root = DefaultRootWindow(dpy);
    XWindowAttributes attrs;
    if (XGetWindowAttributes(dpy, w, &attrs) && attrs.map_state == IsUnmapped) {
        setWindowDesktopPropOnly_X11(win, desk);
        return;
    }
    Atom   NET_WM_DESKTOP = XInternAtom(dpy, "_NET_WM_DESKTOP", False);
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xclient.type         = ClientMessage;
    ev.xclient.window       = w;
    ev.xclient.message_type = NET_WM_DESKTOP;
    ev.xclient.format       = 32;
    ev.xclient.data.l[0]    = desk;        // target desktop
    ev.xclient.data.l[1]    = 1;           // source indication (1 = application)
    ev.xclient.data.l[2]    = CurrentTime; // timestamp
    ev.xclient.data.l[3]    = 0;
    ev.xclient.data.l[4]    = 0;
    XSendEvent(dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XChangeProperty(
        dpy, w, NET_WM_DESKTOP, XA_CARDINAL, 32, PropModeReplace, (unsigned char *) &desk, 1);
    XFlush(dpy);
}

iBool isXSession_X11(void) {
    const char *driver = SDL_GetCurrentVideoDriver();
    if (driver && !iCmpStr(driver, "wayland")) {
        return iFalse;
    }
    const char *dpy = getenv("DISPLAY");
    if (!dpy || strlen(dpy) == 0) {
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
        Display    *dpy   = wmInfo.info.x11.display;
        Window      wnd   = wmInfo.info.x11.window;
        Atom        prop  = XInternAtom(dpy, "_GTK_THEME_VARIANT", False);
        Atom        u8    = XInternAtom(dpy, "UTF8_STRING", False);
        const char *value = setDark ? "dark" : "light";
        XChangeProperty(
            dpy, wnd, prop, u8, 8, PropModeReplace, (unsigned char *) value, strlen(value));
    }
}

void handleCommand_X11(const char *cmd) {
    if (!isXSession_X11()) {
        return;
    }
    if (equal_Command(cmd, "theme.changed")) {
        iConstForEach(PtrArray, iter, mainWindows_App()) {
            iMainWindow *mw = iter.ptr;
            setDarkWindowTheme_SDLWindow(mw->base.win, isDark_ColorTheme(prefs_App()->theme));
        }
    }
}
