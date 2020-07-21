#include <the_Foundation/commandline.h>
#include <stdio.h>
#if defined (iPlatformMsys)
#  define SDL_MAIN_HANDLED
#endif
#include <SDL.h>

#include "app.h"

int main(int argc, char **argv) {
#if defined (iPlatformMsys)
    /* MSYS runtime takes care of WinMain. */
    SDL_SetMainReady();
#endif
    init_Foundation();
    printf("Lagrange: A Beautiful Gemini Client\n");
    /* Initialize SDL. */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return -1;
    }
    run_App(argc, argv);
    SDL_Quit();
    return 0;
}
