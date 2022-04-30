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

#include <the_Foundation/objectlist.h>
#include <the_Foundation/string.h>
#include <the_Foundation/stringset.h>
#include <the_Foundation/time.h>

#include "prefs.h"
#include "ui/color.h"

iDeclareType(Bookmarks)
iDeclareType(DocumentWidget)
iDeclareType(CommandLine)
iDeclareType(GmCerts)
iDeclareType(MainWindow)
iDeclareType(MimeHooks)
iDeclareType(Periodic)
iDeclareType(Root)
iDeclareType(Visited)
iDeclareType(Window)

/* Command line options strings. */
#define listTabUrls_CommandLineOption       "list-tab-urls;L"
#define openUrlOrSearch_CommandLineOption   "url-or-search;u"
#define windowWidth_CommandLineOption       "width;w"
#define windowHeight_CommandLineOption      "height;h"

enum iAppDeviceType {
    desktop_AppDeviceType,
    tablet_AppDeviceType,
    phone_AppDeviceType,
};

enum iAppEventMode {
    waitForNewEvents_AppEventMode,
    postedEventsOnly_AppEventMode,
};

enum iUserEventCode {
    command_UserEventCode = 1,
    refresh_UserEventCode,
    asleep_UserEventCode,
    periodic_UserEventCode,
    /* The start of a potential touch tap event is notified via a custom event because
       sending SDL_MOUSEBUTTONDOWN would be premature: we don't know how long the tap will
       take, it could turn into a tap-and-hold for example. */
    widgetTapBegins_UserEventCode,
    widgetTouchEnds_UserEventCode, /* finger lifted, but momentum may continue */
    releaseObject_UserEventCode, /* object that needs releasing in the main thread */
};

const iString *execPath_App     (void);
const iString *dataDir_App      (void);
const iString *downloadDir_App  (void);
const iString *debugInfo_App    (void);

int         run_App                     (int argc, char **argv);
void        rootOrder_App               (iRoot *roots[2]); /* TODO: max roots? */
void        processEvents_App           (enum iAppEventMode mode);
iBool       handleCommand_App           (const char *cmd);
void        refresh_App                 (void);
iBool       isRefreshPending_App        (void);
iBool       isFinishedLaunching_App     (void);
uint32_t    elapsedSinceLastTicker_App  (void); /* milliseconds */

iBool               isLandscape_App     (void);
iLocalDef iBool     isPortrait_App      (void) { return !isLandscape_App(); }
enum iAppDeviceType deviceType_App      (void);
iLocalDef iBool     isPortraitPhone_App (void) { return isPortrait_App() && deviceType_App() == phone_AppDeviceType; }
iLocalDef iBool     isLandscapePhone_App(void) { return isLandscape_App() && deviceType_App() == phone_AppDeviceType; }
iBool               isRunningUnderWindowSystem_App  (void);

const iCommandLine *commandLine_App     (void);
iGmCerts *          certs_App           (void);
iVisited *          visited_App         (void);
iBookmarks *        bookmarks_App       (void);
iMimeHooks *        mimeHooks_App       (void);
iPeriodic *         periodic_App        (void);
iDocumentWidget *   document_App        (void);
iObjectList *       listDocuments_App   (const iRoot *rootOrNull); /* NULL for all roots of current window */
iStringSet *        listOpenURLs_App    (void); /* all tabs */
iPtrArray *         listWindows_App     (void);
iDocumentWidget *   newTab_App          (const iDocumentWidget *duplicateOf, iBool switchToNew);
void                trimCache_App       (void);
void                trimMemory_App      (void);
//iBool               findCachedContent_App(const iString *url, iString *mime_out, iBlock *data_out);

iDocumentWidget *   document_Root       (iRoot *);

const iPrefs *      prefs_App           (void);
iBool               forceSoftwareRender_App(void);
void                setForceSoftwareRender_App(iBool sw);
enum iColorTheme    colorTheme_App      (void);
const iString *     schemeProxy_App     (iRangecc scheme);
iBool               willUseProxy_App    (const iRangecc scheme);
const iString *     searchQueryUrl_App  (const iString *queryStringUnescaped);
const iString *     fileNameForUrl_App  (const iString *url, const iString *mime);
const iString *     temporaryPathForUrl_App(const iString *url, const iString *mime); /* deleted before quitting */
const iString *     downloadPathForUrl_App(const iString *url, const iString *mime);

typedef void (*iTickerFunc)(iAny *);

iAny *      findWidget_App      (const char *id);
void        addTicker_App       (iTickerFunc ticker, iAny *context);
void        addTickerRoot_App   (iTickerFunc ticker, iRoot *root, iAny *context);
void        removeTicker_App    (iTickerFunc ticker, iAny *context);

void        addWindow_App       (iMainWindow *win);
void        removeWindow_App    (iMainWindow *win);
void        setActiveWindow_App (iMainWindow *win);
void        closeWindow_App     (iMainWindow *win);
size_t      numWindows_App      (void);
size_t      windowIndex_App     (const iMainWindow *win);
iMainWindow *newMainWindow_App  (void);
const iPtrArray *mainWindows_App(void);
iMainWindow *    mainWindow_App (void); /* currently active main window */
void        addPopup_App        (iWindow *popup);
void        removePopup_App     (iWindow *popup);
void        closePopups_App     (iBool doForce);

void        postRefresh_App     (void);
void        postCommand_Root    (iRoot *, const char *command);
void        postCommandf_Root   (iRoot *, const char *command, ...);
void        postCommandf_App    (const char *command, ...);

iLocalDef void postCommandString_Root(iRoot *d, const iString *command) {
    if (command) {
        postCommand_Root(d, cstr_String(command));
    }
}
iLocalDef void postCommand_App(const char *command) {
    postCommand_Root(NULL, command);
}

iDocumentWidget *   document_Command    (const char *cmd);

void            openInDefaultBrowser_App(const iString *url);
void            revealPath_App          (const iString *path);
void            updateCACertificates_App(void);
void            resetFonts_App          (void);
void            availableFontsChanged_App(void);
