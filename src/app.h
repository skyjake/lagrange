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
    refresh_UserEventCode  = 2,
};

const iString *execPath_App     (void);
const iString *dataDir_App      (void);

int         run_App             (int argc, char **argv);
void        processEvents_App   (enum iAppEventMode mode);
iBool       handleCommand_App   (const char *cmd);
void        refresh_App         (void);
iBool       isRefreshPending_App(void);

int                 zoom_App            (void);
enum iColorTheme    colorTheme_App      (void);

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
