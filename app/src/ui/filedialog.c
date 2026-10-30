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

#include "filedialog.h"
#include "defs.h"

#if defined (LAGRANGE_FILE_DIALOGS)

#include "app.h"
#include "window.h"

#if defined (iPlatformAppleDesktop)
#   include "platform/macos.h"
#endif

#include <SDL3/SDL_dialog.h>

iDeclareType(FileDialogRequest)
iDeclareTypeConstructionArgs(FileDialogRequest, const char *command)

struct Impl_FileDialogRequest {
    iString              command;
    iString              filterName;
    iString              filterPattern;
    SDL_DialogFileFilter filter; /* must remain valid until the callback */
};

void init_FileDialogRequest(iFileDialogRequest *d, const char *command) {
    initCStr_String(&d->command, command);
    init_String(&d->filterName);
    init_String(&d->filterPattern);
    iZap(d->filter);
}

void deinit_FileDialogRequest(iFileDialogRequest *d) {
    deinit_String(&d->filterPattern);
    deinit_String(&d->filterName);
    deinit_String(&d->command);
}

iDefineTypeConstructionArgs(FileDialogRequest, (const char *command), command)

/* May be called in a background thread. */
static void finished_FileDialogRequest_(void *userData, const char * const *fileList, int filter) {
    iFileDialogRequest *d = userData;
    iUnused(filter);
    if (fileList) {
        for (const char * const *path = fileList; *path; path++) {
            postCommandf_App("%s path:%s", cstr_String(&d->command), *path);
        }
    }
    else {
        fprintf(stderr, "[FileDialog] error: %s\n", SDL_GetError());
    }
    delete_FileDialogRequest(d);
}

static void show_FileDialogRequest_(iFileDialogRequest *d, SDL_FileDialogType type,
                                    SDL_PropertiesID props) {
#if defined (iPlatformAppleDesktop)
    if (get_MainWindow()) {
        restoreKeyAfterSheet_MacOS(as_Window(get_MainWindow()));
    }
#endif
    SDL_ShowFileDialogWithProperties(type, finished_FileDialogRequest_, d, props);
    SDL_DestroyProperties(props);
}

static SDL_PropertiesID newProperties_FileDialog_(const char *title) {
    SDL_PropertiesID props = SDL_CreateProperties();
    iMainWindow *mainWin = get_MainWindow();
    if (mainWin) {
        SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, as_Window(mainWin)->win);
    }
    if (title) {
        SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, translateCStr_Lang(title));
    }
    return props;
}

void open_FileDialog(const char *command, const char *title, const char *filterName,
                     const char *filterPattern, int flags) {
    iFileDialogRequest *d = new_FileDialogRequest(command);
    SDL_PropertiesID props = newProperties_FileDialog_(title);
    SDL_SetBooleanProperty(props, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN,
                           (flags & multiple_FileDialogFlag) != 0);
    if (filterPattern) {
        setCStr_String(&d->filterName, translateCStr_Lang(filterName ? filterName : filterPattern));
        setCStr_String(&d->filterPattern, filterPattern);
        d->filter = (SDL_DialogFileFilter){ cstr_String(&d->filterName),
                                            cstr_String(&d->filterPattern) };
        SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, &d->filter);
        SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, 1);
    }
    show_FileDialogRequest_(d,
                            flags & folders_FileDialogFlag ? SDL_FILEDIALOG_OPENFOLDER
                                                           : SDL_FILEDIALOG_OPENFILE,
                            props);
}

void save_FileDialog(const char *command, const char *title, const iString *defaultPath) {
    iFileDialogRequest *d = new_FileDialogRequest(command);
    SDL_PropertiesID props = newProperties_FileDialog_(title);
    if (defaultPath) {
        SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, cstr_String(defaultPath));
    }
    show_FileDialogRequest_(d, SDL_FILEDIALOG_SAVEFILE, props);
}

#else /* LAGRANGE_FILE_DIALOGS */

void open_FileDialog(const char *command, const char *title, const char *filterName,
                     const char *filterPattern, int flags) {
    iUnused(command, title, filterName, filterPattern, flags);
}

void save_FileDialog(const char *command, const char *title, const iString *defaultPath) {
    iUnused(command, title, defaultPath);
}

#endif
