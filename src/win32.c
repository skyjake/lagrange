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

#include "win32.h"
#include "ui/window.h"
#include "ui/command.h"
#include "prefs.h"
#include "app.h"

#include <the_Foundation/path.h>
#include <the_Foundation/sortedarray.h>
#include <SDL_syswm.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <d2d1.h>

static HWND windowHandle_(SDL_Window *win) {
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (SDL_GetWindowWMInfo(win, &wmInfo)) {
        return wmInfo.info.win.window;
    }
    return NULL;
}

/* Windows 10 Dark Mode Support

Apparently Microsoft never documented the Win32 functions that control dark mode for
apps and windows. Here we manually query certain entrypoints from uxtheme.dll and
user32.dll and use them to enable dark mode for the application, and switch the title
bar colors to dark or light depending on the Prefs UI color theme.

Perhaps these Win32 APIs will be documented properly in some future version of Windows,
but for now this is what we have to do to avoid having a white title bar in dark mode.

Calling random functions from system DLLs is a great way to introduce crashes in the
future! Be on the lookout for launch problems down the road.
   
Adapted from https://github.com/ysc3839/win32-darkmode. */

enum WINDOWCOMPOSITIONATTRIB {
	WCA_UNDEFINED = 0,
	WCA_NCRENDERING_ENABLED = 1,
	WCA_NCRENDERING_POLICY = 2,
	WCA_TRANSITIONS_FORCEDISABLED = 3,
	WCA_ALLOW_NCPAINT = 4,
	WCA_CAPTION_BUTTON_BOUNDS = 5,
	WCA_NONCLIENT_RTL_LAYOUT = 6,
	WCA_FORCE_ICONIC_REPRESENTATION = 7,
	WCA_EXTENDED_FRAME_BOUNDS = 8,
	WCA_HAS_ICONIC_BITMAP = 9,
	WCA_THEME_ATTRIBUTES = 10,
	WCA_NCRENDERING_EXILED = 11,
	WCA_NCADORNMENTINFO = 12,
	WCA_EXCLUDED_FROM_LIVEPREVIEW = 13,
	WCA_VIDEO_OVERLAY_ACTIVE = 14,
	WCA_FORCE_ACTIVEWINDOW_APPEARANCE = 15,
	WCA_DISALLOW_PEEK = 16,
	WCA_CLOAK = 17,
	WCA_CLOAKED = 18,
	WCA_ACCENT_POLICY = 19,
	WCA_FREEZE_REPRESENTATION = 20,
	WCA_EVER_UNCLOAKED = 21,
	WCA_VISUAL_OWNER = 22,
	WCA_HOLOGRAPHIC = 23,
	WCA_EXCLUDED_FROM_DDA = 24,
	WCA_PASSIVEUPDATEMODE = 25,
	WCA_USEDARKMODECOLORS = 26,
	WCA_LAST = 27
};

struct WINDOWCOMPOSITIONATTRIBDATA {
	enum WINDOWCOMPOSITIONATTRIB Attrib;
	PVOID pvData;
	SIZE_T cbData;
};

enum PreferredAppMode { Default, AllowDark, ForceDark, ForceLight };

typedef void (WINAPI *RtlGetNtVersionNumbersFunc)(LPDWORD major, LPDWORD minor, LPDWORD build);
typedef bool (WINAPI *AllowDarkModeForAppFunc)(BOOL allow);
typedef enum PreferredAppMode (WINAPI *SetPreferredAppModeFunc)(enum PreferredAppMode appMode);
typedef BOOL (WINAPI *SetWindowCompositionAttributeFunc)(HWND hWnd, struct WINDOWCOMPOSITIONATTRIBDATA *);
typedef BOOL (WINAPI *AllowDarkModeForWindowFunc)(HWND hWnd, BOOL allow);

static AllowDarkModeForWindowFunc        AllowDarkModeForWindow_;
static SetWindowCompositionAttributeFunc SetWindowCompositionAttribute_;

iDeclareType(HandleDarkness)

struct Impl_HandleDarkness {
    HWND hwnd;
    BOOL isDark;
};

static int cmp_HandleDarkness_(const void *a, const void *b) {
    return iCmp(((const iHandleDarkness *) a)->hwnd,
                ((const iHandleDarkness *) b)->hwnd);
}

static DWORD ntBuildNumber_;
static iSortedArray darkness_; /* state tracking; TODO: replace with a flag in MainWindow? */

/* Ugly bit of bookkeeping here, but the idea is that the Windows-specific behavior
   is invisible outside this source module. */

static void maybeInitDarkness_(void) {
    if (!darkness_.cmp) {
        init_SortedArray(&darkness_, sizeof(iHandleDarkness), cmp_HandleDarkness_);
    }
}

static BOOL isDark_(HWND hwnd) {
    maybeInitDarkness_();
    size_t pos;
    if (locate_SortedArray(&darkness_, &(iHandleDarkness){ hwnd }, &pos)) {
        return ((const iHandleDarkness *) at_SortedArray(&darkness_, pos))->isDark;
    }
    return FALSE;
}

static void setIsDark_(HWND hwnd, BOOL isDark) {
    maybeInitDarkness_();
    insert_SortedArray(&darkness_, &(iHandleDarkness){ hwnd, isDark });
}

static void cleanDark_(void) {
    /* TODO: Just add a flag in MainWindow. */
    iForEach(Array, i, &darkness_.values) {
        iHandleDarkness *dark = i.value;
        iBool exists = iFalse;
        iConstForEach(PtrArray, iter, mainWindows_App()) {
            if (windowHandle_(((const iMainWindow *) iter.ptr)->base.win) == dark->hwnd) {
                exists = iTrue;
                break;
            }
        }
        if (!exists) {
            remove_ArrayIterator(&i);
        }
    }
}

static iBool refreshTitleBarThemeColor_(HWND hwnd) {
	BOOL dark = isDark_ColorTheme(prefs_App()->theme);
    if (dark == isDark_(hwnd)) {
        return FALSE;
    }
	if (ntBuildNumber_ < 18362) {
        INT_PTR pDark = dark;
		SetPropW(hwnd, L"UseImmersiveDarkModeColors", (HANDLE) pDark);
    }
	else if (SetWindowCompositionAttribute_) {
		struct WINDOWCOMPOSITIONATTRIBDATA data = {
            WCA_USEDARKMODECOLORS, &dark, sizeof(dark)
        };
		SetWindowCompositionAttribute_(hwnd, &data);
	}
    setIsDark_(hwnd, dark);
    return TRUE;
}

static void enableDarkMode_Win32(void) {
    RtlGetNtVersionNumbersFunc RtlGetNtVersionNumbers_ = 
        (RtlGetNtVersionNumbersFunc)
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetNtVersionNumbers");
    if (!RtlGetNtVersionNumbers_) {
        return;
    }
    DWORD major, minor;
   	RtlGetNtVersionNumbers_(&major, &minor, &ntBuildNumber_);
	ntBuildNumber_ &= ~0xf0000000;
    //printf("%u.%u %u\n", major, minor, ntBuildNumber_);
    /* Windows 11 is apparently still NT version 10. */
	if (!(major == 10 && minor == 0 && ntBuildNumber_ >= 17763)) {
        return;
    }
    HMODULE hUxtheme = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hUxtheme) {
		AllowDarkModeForWindow_ = (AllowDarkModeForWindowFunc) 
            GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133));
        AllowDarkModeForAppFunc AllowDarkModeForApp_ = NULL;
        SetPreferredAppModeFunc SetPreferredAppMode_ = NULL;
        FARPROC ord135 = GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
        if (ord135) {
            if (ntBuildNumber_ < 18362) {
                AllowDarkModeForApp_ = (AllowDarkModeForAppFunc) ord135;
                AllowDarkModeForApp_(TRUE);
            }
            else {
                SetPreferredAppMode_ = (SetPreferredAppModeFunc) ord135;
                SetPreferredAppMode_(AllowDark);
            }
        }
        SetWindowCompositionAttribute_ = (SetWindowCompositionAttributeFunc)
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
    }
}

void init_Win32(void) {
    SetProcessDPIAware();
    enableDarkMode_Win32();
}

float desktopDPI_Win32(void) {
    /* Query Direct2D for the desktop DPI (not aware of which monitor, though). */
    float ratio = 1.0f;
    ID2D1Factory *d2dFactory = NULL;
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void **) &d2dFactory);
    if (SUCCEEDED(hr)) {
        FLOAT dpiX = 96;
        FLOAT dpiY = 96;
        ID2D1Factory_GetDesktopDpi(d2dFactory, &dpiX, &dpiY);
        ratio = (float) (dpiX / 96.0);
        ID2D1Factory_Release(d2dFactory);
    }
    return ratio;
}

iString *windowsDirectory_Win32(void) {
    WCHAR winDir[MAX_PATH];
    GetWindowsDirectoryW(winDir, MAX_PATH);
    return newUtf16_String(winDir);
}

iString *tempDirectory_Win32(void) {
    /* Calling GetTempPathW would just return C:\WINDOWS? A local config issue? */
    WCHAR buf[32768];
    if (GetEnvironmentVariableW(L"TMP", buf, sizeof(buf))) {
        return newUtf16_String(buf);
    }
    if (GetEnvironmentVariableW(L"TEMP", buf, sizeof(buf))) {
        return newUtf16_String(buf);
    }
    if (GetEnvironmentVariableW(L"USERPROFILE", buf, sizeof(buf))) {
        return concatCStr_Path(collect_String(newUtf16_String(buf)), "AppData\\Local\\Temp");
    }
    return concatCStr_Path(collect_String(windowsDirectory_Win32()), "Temp");
}

void useExecutableIconResource_SDLWindow(SDL_Window *win) {
    HINSTANCE handle = GetModuleHandle(NULL);
    HICON icon = LoadIcon(handle, "IDI_ICON1");
    if (icon) {
        HWND hwnd = windowHandle_(win);
        SetClassLongPtr(hwnd, -14 /*GCL_HICON*/, (LONG_PTR) icon);
    }
}

void enableDarkMode_SDLWindow(SDL_Window *win) {
    if (AllowDarkModeForWindow_) {
        HWND hwnd = windowHandle_(win);
        AllowDarkModeForWindow_(hwnd, TRUE);
        refreshTitleBarThemeColor_(hwnd);
    }    
}

void handleCommand_Win32(const char *cmd) {
    if (equal_Command(cmd, "theme.changed")) {        
        iConstForEach(PtrArray, iter, mainWindows_App()) {
            iMainWindow *mw = iter.ptr;
            SDL_Window *win = mw->base.win;
            if (refreshTitleBarThemeColor_(windowHandle_(win)) &&
                !isFullscreen_MainWindow(mw) &&
                !argLabel_Command(cmd, "auto")) {
                /* Silly hack, but this will ensure that the non-client area is repainted. */
                SDL_MinimizeWindow(win);
                SDL_RestoreWindow(win);
            }
        }
    }
    else if (equal_Command(cmd, "window.focus.gained")) {
        /* Purge old windows from the darkness. */ 
        cleanDark_();
    }
}

#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
iInt2 cursor_Win32(void) {
    POINT p;
    GetPhysicalCursorPos(&p);
    return init_I2(p.x, p.y);
}

void processNativeEvent_Win32(const struct SDL_SysWMmsg *msg, iWindow *window) {
    static int winDown_[2] = { 0, 0 };
    HWND hwnd = msg->msg.win.hwnd;
    //printf("[syswm] %x\n", msg->msg.win.msg); fflush(stdout);
    const WPARAM wp = msg->msg.win.wParam;
    switch (msg->msg.win.msg) {
        case WM_ACTIVATE: {
            //LONG style = GetWindowLong(hwnd, GWL_STYLE);
            //SetWindowLog(hwnd, GWL_STYLE, style);
            iZap(winDown_); /* may have hidden the up event */
            break;
        }
        case WM_KEYDOWN: {
            if (wp == VK_LWIN) {
                //printf("lwin down\n"); fflush(stdout);
                winDown_[0] = iTrue;
            }
            else if (wp == VK_RWIN) {
                //printf("rwin down\n"); fflush(stdout);
                winDown_[1] = iTrue;
            }
            break;
        }
        case WM_KEYUP: {
            if (winDown_[0] || winDown_[1]) {
                iMainWindow *mw = as_MainWindow(window);
                /* Emulate the default window snapping behavior. */
                int snap = snap_MainWindow(mw);
                if (wp == VK_LEFT) {
                    snap &= ~(topBit_WindowSnap | bottomBit_WindowSnap);
                    setSnap_MainWindow(mw,
                                       snap == right_WindowSnap ? 0 : left_WindowSnap);
                }
                else if (wp == VK_RIGHT) {
                    snap &= ~(topBit_WindowSnap | bottomBit_WindowSnap);
                    setSnap_MainWindow(mw,
                                       snap == left_WindowSnap ? 0 : right_WindowSnap);
                }
                else if (wp == VK_UP) {
                    if (~snap & topBit_WindowSnap) {
                        setSnap_MainWindow(mw,
                                       snap & bottomBit_WindowSnap ? snap & ~bottomBit_WindowSnap
                                       : snap == left_WindowSnap || snap == right_WindowSnap
                                           ? snap | topBit_WindowSnap
                                           : maximized_WindowSnap);
                    }
                    else {
                        postCommand_App("window.maximize");
                    }
                }
                else if (wp == VK_DOWN) {
                    if (snap == 0 || snap & bottomBit_WindowSnap) {
                        postCommand_App("window.minimize");
                    }
                    else {
                        setSnap_MainWindow(mw,
                                       snap == maximized_WindowSnap ? 0
                                       : snap & topBit_WindowSnap   ? snap & ~topBit_WindowSnap
                                       : snap == left_WindowSnap || snap == right_WindowSnap
                                           ? snap | bottomBit_WindowSnap
                                           : 0);
                    }
                }
            }
            if (wp == VK_LWIN) {
                winDown_[0] = iFalse;
            }
            if (wp == VK_RWIN) {
                winDown_[1] = iFalse;
            }
            break;            
        }
        case WM_NCLBUTTONDBLCLK: {
            iMainWindow *mw = as_MainWindow(window);
            POINT point = { GET_X_LPARAM(msg->msg.win.lParam), 
                            GET_Y_LPARAM(msg->msg.win.lParam) };
            ScreenToClient(hwnd, &point);
            iInt2 pos = init_I2(point.x, point.y);
            switch (hitTest_MainWindow(mw, pos)) {
                case SDL_HITTEST_DRAGGABLE:
                    window->ignoreClick = iTrue; /* avoid hitting something inside the window */
                    postCommandf_App("window.%s",
                                     snap_MainWindow(mw) ? "restore" : "maximize toggle:1");
                    break;
                case SDL_HITTEST_RESIZE_TOP:
                case SDL_HITTEST_RESIZE_BOTTOM: {
                    window->ignoreClick = iTrue; /* avoid hitting something inside the window */
                    setSnap_MainWindow(mw, yMaximized_WindowSnap);
                    break;
                }
            }
            //fflush(stdout);
            break;
        }
#if 0
        case WM_NCLBUTTONUP: {
            POINT point = { GET_X_LPARAM(msg->msg.win.lParam), 
                            GET_Y_LPARAM(msg->msg.win.lParam) };
            printf("%d,%d\n", point.x, point.y); fflush(stdout);
            ScreenToClient(hwnd, &point);
            iInt2 pos = init_I2(point.x, point.y);
            if (hitTest_MainWindow(as_MainWindow(window), pos) == SDL_HITTEST_DRAGGABLE) {
                printf("released draggable\n"); fflush(stdout);
            }
            break;
        }
#endif
#if 0
        /* SDL does not use WS_SYSMENU on the window, so we can't display the system menu.
           However, the only useful function in the menu would be moving-via-keyboard,
           but that doesn't work with a custom frame. We could show a custom system menu? */
        case WM_NCRBUTTONUP: {
            POINT point = { GET_X_LPARAM(msg->msg.win.lParam), 
                            GET_Y_LPARAM(msg->msg.win.lParam) };
            HMENU menu = GetSystemMenu(hwnd, FALSE);
            printf("menu at %d,%d menu:%p\n", point.x, point.y, menu); fflush(stdout);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd, NULL);
            break;
        }
#endif
    }
}
#endif /* defined (LAGRANGE_ENABLE_CUSTOM_FRAME) */
