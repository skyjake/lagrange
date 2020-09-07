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

#pragma once

/* Application core: event loop, base event processing, audio synth. */

#include <the_Foundation/string.h>
#include <the_Foundation/time.h>

#include "ui/color.h"

iDeclareType(Bookmarks)
iDeclareType(DocumentWidget)
iDeclareType(GmCerts)
iDeclareType(Visited)
iDeclareType(Window)

enum iAppEventMode {
    waitForNewEvents_AppEventMode,
    postedEventsOnly_AppEventMode,
};

enum iUserEventCode {
    command_UserEventCode = 1,
    refresh_UserEventCode = 2,
};

const iString *execPath_App     (void);
const iString *dataDir_App      (void);

int         run_App                     (int argc, char **argv);
void        processEvents_App           (enum iAppEventMode mode);
iBool       handleCommand_App           (const char *cmd);
void        refresh_App                 (void);
iBool       isRefreshPending_App        (void);
uint32_t    elapsedSinceLastTicker_App  (void); /* milliseconds */

int                 zoom_App            (void);
iBool               isLineWrapForced_App(void);
enum iColorTheme    colorTheme_App      (void);
const iString *     schemeProxy_App     (iRangecc scheme);

iGmCerts *          certs_App           (void);
iVisited *          visited_App         (void);
iBookmarks *        bookmarks_App       (void);
iDocumentWidget *   document_App        (void);
iDocumentWidget *   document_Command    (const char *cmd);
iDocumentWidget *   newTab_App          (const iDocumentWidget *duplicateOf);

iAny *      findWidget_App      (const char *id);
void        addTicker_App       (void (*ticker)(iAny *), iAny *context);
void        postRefresh_App     (void);
void        postCommand_App     (const char *command);
void        postCommandf_App    (const char *command, ...);

iLocalDef void postCommandString_App(const iString *command) {
    postCommand_App(cstr_String(command));
}

void        openInDefaultBrowser_App    (const iString *url);
void        revealPath_App              (const iString *path);
