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

/* Private declarations shared by the app/*.c sources. */

#include "app.h"
#include "defs.h"
#include "periodic.h"

#include <the_Foundation/commandline.h>
#include <the_Foundation/object.h>
#include <the_Foundation/rect.h>
#include <the_Foundation/mutex.h>
#include <the_Foundation/sortedarray.h>
#include <the_Foundation/stringhash.h>
#include <the_Foundation/stringlist.h>
#include <SDL3/SDL.h>

#if defined (iPlatformAppleDesktop)
#define EMB_BIN "../../Resources/resources.lgr"
#define defaultDataDir_App_ "~/Library/Application Support/fi.skyjake.Lagrange"
#endif

#if defined (iPlatformAppleMobile)
#define EMB_BIN "../../Resources/resources.lgr"
#define defaultDataDir_App_ "~/Library/Application Support"
#endif

#if defined (iPlatformMsys) || defined (iPlatformWindows)
#define EMB_BIN "../resources.lgr"
#define EMB_BIN2 "../../resources.lgr" /* MSVC dev build places binary in a subfolder */
#define defaultDataDir_App_ "~/AppData/Roaming/fi.skyjake.Lagrange"

#elif defined (iPlatformAndroidMobile)
#define EMB_BIN "resources.lgr" /* loaded from assets with SDL_rwops */
#define defaultDataDir_App_ NULL /* will ask SDL */

#elif defined (iPlatformLinux) || defined (iPlatformTerminal) || defined (iPlatformOther)
#define EMB_BIN   "../../share/lagrange/resources.lgr"
#define EMB_BIN2  "../../../share/lagrange/resources.lgr"
#define EMB_BIN3  "../share/lagrange/resources.lgr"
#define defaultDataDir_App_ "~/.config/lagrange"
#endif

#if defined (iPlatformAppleDesktop) || defined (iPlatformLinux) || defined (iPlatformTerminal) || defined (iPlatformOther)
#   define LAGRANGE_HANDLE_SIGTERM
#   include <signal.h>
#endif

#if defined (iPlatformHaiku)
#define EMB_BIN "./resources.lgr"
#define defaultDataDir_App_ "~/config/settings/lagrange"
#endif

#define EMB_BIN_EXEC "../resources.lgr" /* fallback from build/executable dir */
#if defined (iPlatformTerminal)
#   define STATE_NAME "cstate" /* separate for console since it's a different environment */
#   define PREFS_NAME "cprefs"
#else
#   define STATE_NAME "state"
#   define PREFS_NAME "prefs"
#endif

#define prefsFileName_App_       PREFS_NAME ".cfg"
#define tempPrefsFileName_App_   PREFS_NAME ".cfg.tmp"
#define oldStateFileName_App_    STATE_NAME ".binary"
#define stateFileName_App_       STATE_NAME ".lgr"
#define tempStateFileName_App_   STATE_NAME ".lgr.tmp"
#define backupStateFileName_App_ STATE_NAME ".lgr.old"   /* Windows, Android */
#define quickStateFileName_App_  STATE_NAME "-quick.lgr" /* no cached content */

iDeclareType(App)

typedef iWindow iMainOrExtraWindow;

struct Impl_App {
    iCommandLine args;
    iString *    overrideDataPath;
    iBool        didCheckDataPathOption;
    iString *    execPath;
    iStringSet * tempFilesPendingDeletion;
    iStringList *recentlyClosedTabUrls; /* for reopening, like an undo stack */
    iStringArray *recentlySubmittedInput;
    iStringHash *savedWidths;
    iMimeHooks * mimehooks;
    iGmCerts *   certs;
    iVisited *   visited;
    iBookmarks * bookmarks;
    iMainOrExtraWindow *window; /* currently active MainWindow or extra Window */
    iPtrArray    mainWindows;
    iPtrArray    extraWindows;
    iPtrArray    popupWindows;
    iSortedArray tickers; /* per-frame callbacks, used for animations */
    uint32_t     lastTickerTime;
    uint32_t     elapsedSinceLastTicker;
    iBool        isRunning;
    iBool        isRunningUnderWindowSystem;
    iBool        isRunningUnderWayland;
    iBool        isTextInputActive;
    iBool        isDarkSystemTheme;
    iBool        isSuspended;
    iAtomicInt   pendingRefresh;
    iBool        isLoadingPrefs;
    iStringList *launchCommands;
    iBool        isFinishedLaunching;
    iTime        lastDropTime; /* for detecting drops of multiple items */
    uint32_t     lastVisitedSaveTime;
    iBool        pendingVisitedSave; /* need to save visited URLs soon */
    int          autoReloadTimer; /* TODO: only start this when tabs are autoreloading */
    iPeriodic    periodic;
    int          warmupFrames; /* forced refresh just after resuming from background; FIXME: shouldn't be needed */
    iGamepad *   gamepad;
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
    iBool        isIdling;
    uint32_t     lastEventTime;
    int          sleepTimer;
    unsigned int idleSleepDelayMs;
#endif
#if defined (iPlatformAndroidMobile)
    uint32_t     lastBackButtonTime; /* detect and discard rapid Back button presses */
#endif
#if defined (iPlatformAppleDesktop) && defined (LAGRANGE_NATIVE_MENU)
    iRoot *      submenuRoot; /* offscreen, since the application menu is not tied to a window */
#endif
    /* Preferences: */
    iBool        commandEcho;         /* --echo */
    iBool        forceSoftwareRender; /* --sw */
    iArray       initialWindowRects;  /* one per window, indexed by placementIndex */
    iArray       initialWindowDesktops;
    iPrefs       prefs;
};

iDeclareClass(SavedWidth);
iDeclareObjectConstructionArgs(SavedWidth, float gaps)

struct Impl_SavedWidth {
    iObject object;
    float gaps; /* density independent */
};

iDeclareType(Ticker)

struct Impl_Ticker {
    iAny *context;
    iRoot *root;
    void (*callback)(iAny *);
};

extern iApp    app_;

/* Only one thread can write the state/prefs files at a time. */
extern iMutex *saveMutex_App_;

/* paths.c */
const char *    dataDir_App_            (void);
const char *    downloadDir_App_        (void);
#if defined (iPlatformAndroid)
void            migrateInternalUserDirToExternalStorage_App_(iApp *);
#endif

/* state.c */
iBool           isPlacementUnused_App_  (const iApp *, size_t index);
iRect           initialWindowRect_App_  (const iApp *, size_t windowIndex);
size_t          nextWindowPlacementIndex_App_(const iApp *);
iBool           loadState_App_          (iApp *);
void            saveState_App_          (const iApp *, iBool withContent);
void            eraseBackupsForWindowSerial_App_(uint32_t serial);

/* prefs.c */
void            loadPrefs_App_          (iApp *);
void            savePrefs_App_          (const iApp *);
iBool           handlePrefsCommand_App_ (iApp *, const char *cmd);
void            prefsBoolValueChanged_App_(iApp *, enum iPrefsBool, iBool isFrozen);

/* windows.c */
iPtrArray *     listWindows_App_        (const iApp *, iPtrArray *windows);
void            clearCache_App_         (void);

/* events.c */
int             cmp_Ticker_             (const void *a, const void *b);
int             run_App_                (iApp *);
uint32_t        postAutoReloadCommand_App_(void *userdata, SDL_TimerID, uint32_t interval);
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
uint32_t        checkAsleep_App_        (void *param, SDL_TimerID, uint32_t interval);
#endif

/* commands.c */
iBool           handleNonWindowRelatedCommand_App_(iApp *, const char *cmd);
