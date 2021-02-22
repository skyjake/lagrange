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
#include <SDL.h>
#include <stdio.h>
#include <signal.h>

int main(int argc, char **argv) {
    int enable_audio;
    enable_audio = 1;

    printf("Lagrange: A Beautiful Gemini Client\n");

    if (argc == 2) {
	if (memcmp(argv[1], "help", 4) == 0) {
	    printf("\n==== Help ====\n");
	    printf("%s [url] [option]\n", argv[0]);
	    printf("help - Help command\n");
	    printf("noaudio - Disables audio support\n");
	} else if (memcmp(argv[1], "noaudio", 7) == 0)
	    enable_audio = 0;
        else if (memcmp(argv[1], "gemini://", 9) != 0) {
	    printf("Unknown command. Please use %s help\n", argv[0]);
	    return 0;
	}
    } else if (argc == 3) {
        if (memcmp(argv[1], "gemini://", 9) != 0) {
            printf("Unknown command. Please use %s help\n", argv[0]);
	    return 0;
	}

        if (memcmp(argv[2], "noaudio", 7) == 0)
	    enable_audio = 0;

    } else if (argc != 1) {
	printf("Too many arguments. Please use %s help\n", argv[0]);
	return 0;
    }

    signal(SIGPIPE, SIG_IGN);
#if defined (iPlatformAppleDesktop)
    enableMomentumScroll_MacOS();
    registerURLHandler_MacOS();
#endif
#if defined (iPlatformMsys)
    /* MSYS runtime takes care of WinMain. */
    setDPIAware_Win32();
    SDL_SetMainReady();
#endif
    /* Initialize libraries. */
#if defined (LAGRANGE_ENABLE_MPG123)
    if (enable_audio == 1)
    	mpg123_init();
#endif
    init_Foundation();
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    SDL_SetHint(SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return -1;
    }
    run_App(argc, argv);
    SDL_Quit();
#if defined (LAGRANGE_ENABLE_MPG123)
    if (enable_audio == 1)
        mpg123_exit();
#endif
    deinit_Foundation();
    return 0;
}
