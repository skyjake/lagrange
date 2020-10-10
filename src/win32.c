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
#include <SDL_syswm.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d2d1.h>

void setDPIAware_Win32(void) {
    SetProcessDPIAware();
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
