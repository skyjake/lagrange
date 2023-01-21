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

#include "app.h"
#include "updater.h"

#if defined (iPlatformAppleDesktop)
#  include "macos.h"
#endif
#if defined (iPlatformAppleMobile)
#  include "ios.h"
#endif
#if defined (iPlatformMsys)
#  include "win32.h"
#  define SDL_MAIN_HANDLED
#endif
#if defined (LAGRANGE_ENABLE_MPG123)
#  include <mpg123.h>
#endif

#include <the_Foundation/commandline.h>
#include <the_Foundation/tlsrequest.h>
#include <SDL.h>
#include <stdio.h>
#include <signal.h>

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
#if defined (iPlatformAppleDesktop)
    enableMomentumScroll_MacOS();
    registerURLHandler_MacOS();
#endif
#if defined (iPlatformMsys)
    init_Win32(); /* DPI awareness, dark mode */
    SDL_SetMainReady(); /* MSYS runtime takes care of WinMain. */
#endif
    /* Initialize libraries. */
#if defined (LAGRANGE_ENABLE_MPG123)
    mpg123_init();
#endif
    init_Foundation();
    /* IssueID #122: Recommended set of TLS ciphers for Gemini */
    setCiphers_TlsRequest("ECDHE-ECDSA-AES256-GCM-SHA384:"
                          "ECDHE-ECDSA-CHACHA20-POLY1305:"
                          "ECDHE-ECDSA-AES128-GCM-SHA256:"
                          "ECDHE-RSA-AES256-GCM-SHA384:"
                          "ECDHE-RSA-CHACHA20-POLY1305:"
                          "ECDHE-RSA-AES128-GCM-SHA256:"
                          "DHE-RSA-AES256-GCM-SHA384");
#if SDL_VERSION_ATLEAST(2, 24, 0)
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitor");
#endif
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    SDL_EnableScreenSaver();
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    SDL_SetHint(SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK, "1");
#if SDL_VERSION_ATLEAST(2, 0, 8)
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
#endif
#if 0
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1"); /* debugging! */
#endif
#if defined (iPlatformAppleMobile)
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
        fprintf(stderr, "[SDL] init failed: %s\n", SDL_GetError());
        return -1;
    }
    init_Updater();
    run_App(argc, argv);
    SDL_Quit();
#if defined (LAGRANGE_ENABLE_MPG123)
    mpg123_exit();
#endif
    deinit_Updater();
    deinit_Foundation();
    return 0;
}
