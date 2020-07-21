#include "win32.h"
#include <SDL_syswm.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

void useExecutableIconResource_SDLWindow(SDL_Window *win) {
    HINSTANCE handle = GetModuleHandle(NULL);
    HICON icon = LoadIcon(handle, "IDI_ICON1");
    if (icon) {
        SDL_SysWMinfo wmInfo;
        SDL_VERSION(&wmInfo.version);
        if (SDL_GetWindowWMInfo(win, &wmInfo)) {
            HWND hwnd = wmInfo.info.win.window;
            SetClassLongPtr(hwnd, -14 /*GCL_HICON*/, (LONG_PTR) icon);
        }
    }
}
