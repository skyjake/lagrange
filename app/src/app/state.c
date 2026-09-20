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

#include "impl.h"

#include "bookmarks.h"
#include <lagrange/core.h>
#include <lagrange/gmrequest.h>
#include "gmutil.h"
#include "ui/documentwidget.h"
#include "ui/inputwidget.h"
#include "ui/root.h"
#include "ui/sidebarwidget.h"
#include "ui/window.h"

#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/path.h>
#include <lagrange/visited.h>

#include <errno.h>

#if defined (iPlatformAppleDesktop)
#   include "platform/macos.h"
#endif

iBool isPlacementUnused_App_(const iApp *d, size_t index) {
    iConstForEach(PtrArray, i, &d->mainWindows) {
        if (((const iMainWindow *) i.ptr)->place.placementIndex == index) {
            return iTrue;
        }
    }
    return iFalse;
}

static const char *magicState_App_       = "lgL1";
static const char *magicWindow_App_      = "wind";
static const char *magicTabDocument_App_ = "tabd";
static const char *magicSidebar_App_     = "side";
static const char *magicInput_App_       = "inpt";

enum iDocumentStateFlag {
    current_DocumentStateFlag    = iBit(1),
    rootIndex1_DocumentStateFlag = iBit(2),
};

enum iWindowStateFlag {
    current_WindowStateFlag = iBit(9),
};

iRect initialWindowRect_App_(const iApp *d, size_t windowIndex) {
    if (windowIndex < size_Array(&d->initialWindowRects)) {
        const iRect rect = constValue_Array(&d->initialWindowRects, windowIndex, iRect);
        if (!isEmpty_Rect(rect)) {
            return rect;
        }
    }
    /* The default window rectangle. */
    iRect rect = init_Rect(-1, -1, 900, 560);
#if !defined (iPlatformTerminal)
#   if defined (iPlatformMsys) || defined (iPlatformWindows)
    /* Must scale by UI scaling factor. */
    mulfv_I2(&rect.size, desktopDPI_Win32(NULL));
#   endif
#   if defined (iPlatformLinux) && !defined (iPlatformAndroid)
    /* Scale by the primary (?) monitor DPI. */
    if (isRunningUnderWindowSystem_App()) {
        const float factor = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
        mulfv_I2(&rect.size, iMax(factor, 1.0f));
    }
#   endif
#endif
    return rect;
}

size_t nextWindowPlacementIndex_App_(const iApp *d) {
    /* Determine the lowest "Nth window" slot not currently occupied by an open main window. */
    size_t index = 0;
    while (isPlacementUnused_App_(d, index)) {
        index++;
    }
    return index;
}

static iBool loadStateFile_App_(iApp *d, const char *usedPath, iBool validateOnly);

iBool loadState_App_(iApp *d) {
    const char *oldPath    = concatPath_CStr(dataDir_App_(), oldStateFileName_App_);
    const char *path       = concatPath_CStr(dataDir_App_(), stateFileName_App_);
    const char *backupPath = concatPath_CStr(dataDir_App_(), backupStateFileName_App_);
    const char *quickPath  = concatPath_CStr(dataDir_App_(), quickStateFileName_App_);
    if (fileExistsCStr_FileInfo(quickPath)) {
        /* A quick save is left behind only when the app did not get to save the full state,
           so this is the most recent set of open tabs. There is no cached content in it; the
           documents are refetched after the launch has finished. */
        if (loadStateFile_App_(d, quickPath, iTrue)) {
            return loadStateFile_App_(d, quickPath, iFalse);
        }
        fprintf(stderr, "[App] %s is damaged, loading the full state instead\n", quickPath);
    }
    if (fileExistsCStr_FileInfo(path)) {
        /* If loading fails partway, the windows and tabs restored until then are left behind,
           so check the file first when there is a backup to use instead. */
        if (fileExistsCStr_FileInfo(backupPath) && !loadStateFile_App_(d, path, iTrue) &&
            loadStateFile_App_(d, backupPath, iTrue)) {
            fprintf(stderr, "[App] %s is damaged, loading the backup instead\n", path);
            return loadStateFile_App_(d, backupPath, iFalse);
        }
        return loadStateFile_App_(d, path, iFalse);
    }
    return loadStateFile_App_(d, fileExistsCStr_FileInfo(backupPath) ? backupPath : oldPath,
                              iFalse);
}

static iBool loadStateFile_App_(iApp *d, const char *usedPath, iBool validateOnly) {
    /* When validating, the entire file is parsed but nothing is actually applied. */
    iFile *f = iClob(newCStr_File(usedPath));
    if (open_File(f, readOnly_FileMode)) {
        char magic[4];
        if (readData_File(f, 4, magic) != 4 || memcmp(magic, magicState_App_, 4)) {
            printf("%s: format not recognized\n", cstr_String(path_File(f)));
            return iFalse;
        }
        const uint32_t version = readU32_File(f);
        /* Check supported versions. */
        if (version > latest_FileVersion) {
            printf("%s: unsupported version\n", cstr_String(path_File(f)));
            return iFalse;
        }
        setVersion_Stream(stream_File(f), version);
        /* Window state. */
        iDeclareType(CurrentTabs);
        struct Impl_CurrentTabs {
            iDocumentWidget *currentTab[2]; /* for each root */
        };
        int              numWins       = 0;
        iMainWindow *    win           = NULL;
        iMainWindow *    currentWin    = as_MainWindow(d->window);
        iArray *         currentTabs; /* two per window (per root per window) */
        iBool            isFirstTab[2];
        uint32_t         maxWindowSerial = 0;
        currentTabs = collectNew_Array(sizeof(iCurrentTabs));
        while (!atEnd_File(f)) {
            if (readData_File(f, 4, magic) != 4) {
                printf("%s: truncated\n", cstr_String(path_File(f)));
                setCurrent_Root(NULL);
                return iFalse;
            }
            if (!memcmp(magic, magicInput_App_, 4)) {
                deserialize_StringArray(validateOnly ? iClob(new_StringArray())
                                                     : d->recentlySubmittedInput,
                                        stream_File(f));
            }
            else if (!memcmp(magic, magicWindow_App_, 4)) {
                numWins++;
                const int      splitMode  = read32_File(f);
                const int      winState   = read32_File(f);
                const int      keyRoot    = (winState & 1);
                const iBool    isCurrent  = (winState & current_WindowStateFlag) != 0;
                const uint32_t serial     = (version >= persistentWindowSerial_FileVersion
                                                 ? readU32_File(f) : 0);
                const size_t   placeIndex = (version >= windowPlacementIndex_FileVersion
                                                 ? readU32_File(f) : 0);
//                printf("[State] '%.4s' split:%d state:%x\n", magic, splitMode, winState);
                if (validateOnly) {
                    continue;
                }
#if defined (iPlatformTerminal)
                /* Terminal only supports one window. */
                win = as_MainWindow(d->window);
#else
                if (numWins == 1) {
                    win = as_MainWindow(d->window);
                }
                else {
                    win = new_MainWindow(initialWindowRect_App_(d, numWins - 1));
                    win->place.placementIndex = numWins - 1;
                    addWindow_App(win);
                }
#endif
                if (version >= persistentWindowSerial_FileVersion) {
                    /* Restore the window's persistent identity. */
                    setSerial_Window(as_Window(win), serial);
                    maxWindowSerial = iMax(maxWindowSerial, serial);
                }
                if (version >= windowPlacementIndex_FileVersion) {
                    /* Window placement uses its own logical indexing that allows multi-window
                       arrangements where individual windows can be closed and opened. */
#if defined (iPlatformTerminal)
                    iUnused(placeIndex);
#else
                    if (placeIndex != win->place.placementIndex) {
                        win->place.placementIndex = placeIndex;
                        const iRect rect = initialWindowRect_App_(d, placeIndex);
                        win->place.normalRect = rect;
                        const iBool setPos = left_Rect(rect) >= 0 || top_Rect(rect) >= 0;
                        SDL_SetWindowPosition(win->base.win,
                                              setPos ? left_Rect(rect) : SDL_WINDOWPOS_CENTERED,
                                              setPos ? top_Rect(rect)  : SDL_WINDOWPOS_CENTERED);
                        SDL_SetWindowSize(win->base.win, width_Rect(rect), height_Rect(rect));
                    }
#endif
                }
                pushBack_Array(currentTabs, &(iCurrentTabs){ { NULL, NULL } });
                isFirstTab[0] = isFirstTab[1] = iTrue;
                if (isCurrent) {
                    currentWin = win;
                }
                setCurrent_Window(win);
                setCurrent_Root(NULL);
                win->pendingSplitMode = splitMode;
                setSplitMode_MainWindow(win, splitMode | noEvents_WindowSplit);
                win->base.keyRoot = win->base.roots[keyRoot];
            }
            else if (!memcmp(magic, magicSidebar_App_, 4)) {
                if (numWins == 0) {
                    printf("%s: missing window\n", cstr_String(path_File(f)));
                    setCurrent_Root(NULL);
                    return iFalse;
                }
                const uint16_t bits = readU16_File(f);
                const uint8_t modes = readU8_File(f);
                const float widths[2] = {
                    readf_Stream(stream_File(f)),
                    readf_Stream(stream_File(f))
                };
                iIntSet *closedFolders[2] = {
                    collectNew_IntSet(),
                    collectNew_IntSet()
                };
                if (version >= bookmarkFolderState_FileVersion) {
                    deserialize_IntSet(closedFolders[0], stream_File(f));
                    deserialize_IntSet(closedFolders[1], stream_File(f));
                }
                if (validateOnly) {
                    continue;
                }
                const uint8_t rootIndex = bits & 0xff;
                const uint8_t flags     = bits >> 8;
                iRoot *root = win->base.roots[rootIndex];
                if (root) {
                    iSidebarWidget *sidebar  = findChild_Widget(root->widget, "sidebar");
                    iSidebarWidget *sidebar2 = findChild_Widget(root->widget, "sidebar2");
                    setClosedFolders_SidebarWidget(sidebar, closedFolders[0]);
                    setClosedFolders_SidebarWidget(sidebar2, closedFolders[1]);
                    postCommandf_Root(root, "sidebar.mode arg:%u", modes & 0xf);
                    postCommandf_Root(root, "sidebar2.mode arg:%u", (modes >> 4) & 0xf);
                    if (flags & 4) {
                        postCommand_Widget(sidebar, "feeds.mode arg:%d", unread_FeedsMode);
                    }
                    if (flags & 8) {
                        postCommand_Widget(sidebar2, "feeds.mode arg:%d", unread_FeedsMode);
                    }
                    if (deviceType_App() == desktop_AppDeviceType) {
                        setWidth_SidebarWidget(sidebar,  widths[0]);
                        setWidth_SidebarWidget(sidebar2, widths[1]);
                        if (flags & 1) postCommand_Root(root, "sidebar.toggle noanim:1");
                        if (flags & 2) postCommand_Root(root, "sidebar2.toggle noanim:1");
                    }
                }
            }
            else if (!memcmp(magic, magicTabDocument_App_, 4)) {
                if (numWins == 0) {
                    printf("%s: missing window\n", cstr_String(path_File(f)));
                    setCurrent_Root(NULL);
                    return iFalse;
                }
                const int8_t flags = read8_File(f);
                if (validateOnly) {
                    deserializeState_DocumentWidget(NULL, stream_File(f)); /* just skip it */
                    continue;
                }
                int rootIndex = flags & rootIndex1_DocumentStateFlag ? 1 : 0;
                if (rootIndex > numRoots_Window(as_Window(win)) - 1) {
                    rootIndex = 0;
                }
                setCurrent_Root(win->base.roots[rootIndex]);
                iDocumentWidget *doc = NULL;
                if (d->prefs.retainTabs) {
                    if (isFirstTab[rootIndex]) {
                        isFirstTab[rootIndex] = iFalse;
                        /* There is one pre-created tab in each root. */
                        doc = document_Root(get_Root());
                    }
                    else {
                        doc = newTab_App(NULL, 0 /* no switching or inserting */);
                    }
                    if (flags & current_DocumentStateFlag) {
                        value_Array(currentTabs, numWins - 1, iCurrentTabs).currentTab[rootIndex] = doc;
                    }
                }
                deserializeState_DocumentWidget(doc, stream_File(f));
                doc = NULL;
            }
            else {
                printf("%s: unrecognized data\n", cstr_String(path_File(f)));
                setCurrent_Root(NULL);
                return iFalse;
            }
        }
        if (validateOnly) {
            return iTrue;
        }
        /* Avoid collisions with restored windows' serials. */
        advanceSerialCounter_Window(maxWindowSerial);
        iForEach(Array, i, currentTabs) {
            const iCurrentTabs *cur = i.value;
            win = at_PtrArray(&d->mainWindows, index_ArrayIterator(&i));
            for (size_t j = 0; j < 2; ++j) {
                /* A root that isn't in use has no current tab. Posting the switch anyway would
                   send a rootless command with a null page to every root, overriding the tab
                   that was just restored. */
                if (win->base.roots[j] && cur->currentTab[j]) {
                    postCommandf_Root(win->base.roots[j], "tabs.switch page:%p",
                                      cur->currentTab[j]);
                }
            }
            if (win->splitMode) {
                /* Update root placement. */
                resize_MainWindow(win, -1, -1);
            }
//            postCommand_Root(win->base.roots[0], "window.unfreeze");
            win->isDrawFrozen = iFalse;
            win->base.isExposed = iTrue;

            SDL_ShowWindow(win->base.win);
        }
#if defined (LAGRANGE_ENABLE_X11_XLIB)
        /* Set desktop properties after everything is loaded. */
        iForEach(Array, j, currentTabs) {
            iMainWindow *win = at_PtrArray(&d->mainWindows, index_ArrayIterator(&j));
            const size_t idx = index_ArrayIterator(&j);
            if (idx < size_Array(&d->initialWindowDesktops)) {
                const int *desk = (const int *) at_Array(&d->initialWindowDesktops, idx);
                if (desk && *desk >= 0) {
                    win->place.desktop = *desk;
                     postCommandf_App("~window.setdesktop window:%u arg:%d",
                                    id_Window(as_Window(win)), *desk);
                }
            }
            win->isDrawFrozen = iFalse;
            win->base.isExposed = iTrue;
        }
#else
        /* On non-X11 platforms, just unfreeze normally. */
        iForEach(Array, j, currentTabs) {
            iMainWindow *win = at_PtrArray(&d->mainWindows, index_ArrayIterator(&j));
            win->isDrawFrozen = iFalse;
            win->base.isExposed = iTrue;
        }
#endif

    if (currentWin) {
        SDL_RaiseWindow(currentWin->base.win);
        setActiveWindow_App(currentWin);
    }

        setCurrent_Root(NULL);
        return iTrue;
    }
    return iFalse;
}

#if defined (iPlatformAndroidMobile)
static void syncFile_App_(const char *path) {
    /* Ensure new file contents are flushed to disk. */
    const int fd = open(path, O_WRONLY);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
}
#endif

void saveState_App_(const iApp *d, iBool withContent) {
    if (isAppleDesktop_Platform() && isEmpty_PtrArray(&d->mainWindows)) {
        return; /* nothing to save; keep what was saved earlier */
    }
    lock_Mutex(saveMutex_App_);
    /* A quick save omits the cached content, so it is written to a file of its own. The full
       state file must not be overwritten with a copy that has no content in it. */
    const char *fullPath  = concatPath_CStr(dataDir_App_(), stateFileName_App_);
    const char *quickPath = concatPath_CStr(dataDir_App_(), quickStateFileName_App_);
    if (withContent) {
        trimCache_App();
    }
    /* UI state is saved in binary because it is quite complex (e.g.,
       navigation history, cached content) and depends closely on the widget
       tree. The data is largely not reorderable and should not be modified
       by the user manually. */
    const char *path     = withContent ? fullPath : quickPath;
    const char *tempPath = concatPath_CStr(dataDir_App_(), tempStateFileName_App_);
    iFile      *f        = newCStr_File(tempPath);
    if (open_File(f, writeOnly_FileMode)) {
        writeData_File(f, magicState_App_, 4);
        writeU32_File(f, latest_FileVersion); /* version */
        /* Recently submitted input strings. */
        writeData_File(f, magicInput_App_, 4);
        serialize_StringArray(d->recentlySubmittedInput, stream_File(f));
        iConstForEach(PtrArray, winIter, &d->mainWindows) {
            const iMainWindow *win = winIter.ptr;
            setCurrent_Window(winIter.ptr);
            /* Window state. */ {
                writeData_File(f, magicWindow_App_, 4);
                writeU32_File(f, win->splitMode);
                writeU32_File(f, (win->base.keyRoot == win->base.roots[0] ? 0 : 1) |
                                 (constAs_Window(win) == d->window ? current_WindowStateFlag : 0));
                writeU32_File(f, serial_Window(constAs_Window(win)));
                writeU32_File(f, (uint32_t) win->place.placementIndex);
            }
            /* State of UI elements. */ {
                iForIndices(i, win->base.roots) {
                    const iRoot *root = win->base.roots[i];
                    if (root) {
                        writeData_File(f, magicSidebar_App_, 4);
                        const iSidebarWidget *sidebar  = findChild_Widget(root->widget, "sidebar");
                        const iSidebarWidget *sidebar2 = findChild_Widget(root->widget, "sidebar2");
                        writeU16_File(f, i |
                                      (isVisible_Widget(sidebar)  ? 0x100 : 0) |
                                      (isVisible_Widget(sidebar2) ? 0x200 : 0) |
                                      (feedsMode_SidebarWidget(sidebar)  == unread_FeedsMode ? 0x400 : 0) |
                                      (feedsMode_SidebarWidget(sidebar2) == unread_FeedsMode ? 0x800 : 0));
                        writeU8_File(f,
                                     mode_SidebarWidget(sidebar) |
                                     (mode_SidebarWidget(sidebar2) << 4));
                        writef_Stream(stream_File(f), width_SidebarWidget(sidebar));
                        writef_Stream(stream_File(f), width_SidebarWidget(sidebar2));
                        serialize_IntSet(closedFolders_SidebarWidget(sidebar), stream_File(f));
                        serialize_IntSet(closedFolders_SidebarWidget(sidebar2), stream_File(f));
                    }
                }
            }
            iConstForEach(ObjectList, i, iClob(listDocuments_App(NULL))) {
                iAssert(isInstance_Object(i.object, &Class_DocumentWidget));
                const iWidget *widget = constAs_Widget(i.object);
                writeData_File(f, magicTabDocument_App_, 4);
                int8_t flags = (document_Root(widget->root) == i.object ? current_DocumentStateFlag : 0);
                if (widget->root == win->base.roots[1]) {
                    flags |= rootIndex1_DocumentStateFlag;
                }
                write8_File(f, flags);
                serializeState_DocumentWidget(i.object, stream_File(f), withContent);
            }
        }
        iRelease(f);
    }
    else {
        iRelease(f);
        fprintf(stderr, "[App] failed to save state: %s\n", strerror(errno));
        unlock_Mutex(saveMutex_App_);
        return;
    }
#if defined (iPlatformAndroidMobile)
    syncFile_App_(tempPath);
    if (withContent) {
        /* The old state is used as a backup copy. A quick save has no backup of its own; the
           full state file is the fallback. */
        renamePath_CStr(path, concatPath_CStr(dataDir_App_(), backupStateFileName_App_));
    }
#endif
    /* Copy it over to the real file. This avoids truncation if the app for any reason crashes
       before the state file is fully written. */
    commitFile_Core(path, tempPath);
    if (withContent) {
        /* State saved fully so discard the partial quick save. */
        removePath_CStr(quickPath);
    }
    unlock_Mutex(saveMutex_App_);
}

 void deferVisitedSave_App(void) {
     iApp *d = &app_;
    /* This gets called after the visited URLs have changed, but we want to avoid
       writing them constantly to the file. */
    const uint32_t now     = SDL_GetTicks();
    const uint32_t seconds = (now - d->lastVisitedSaveTime) / 1000;
    iRoot        **roots   = d->window->roots;
    if (seconds >= 60) {
        d->lastVisitedSaveTime = now;
        if (d->pendingVisitedSave) {
            d->pendingVisitedSave = iFalse;
            save_Visited(d->visited, dataDir_App_());
        }
    }
    else if (d->pendingVisitedSave) {
        /* Do it later. */
        addDelay_Periodic(&d->periodic,
                          (60 - seconds) * 1000, roots[0]->widget,
                          "*visited.save");
        return;
    }
    iForIndices(i, roots) {
        if (roots[i]) {
            remove_Periodic(&d->periodic, roots[i]->widget);
        }
    }
}

void saveStateQuickly_App(void) {
    saveState_App_(&app_, iFalse /* cached content is not saved */);
}

void saveState_App(void) {
    saveState_App_(&app_, iTrue /* including cache */);
    savePrefs_App_(&app_);
}

void eraseBackupsForWindowSerial_App_(uint32_t serial) {
    /* Erases this window's input backup files (see setBackupFileName_InputWidget()); the
       "win" marker avoids matching unrelated version-numbered files like trusted.2.txt. */
    const iString *suffix = collectNewFormat_String(".win%u.txt", serial);
    iForEach(DirFileInfo, entry, iClob(newCStr_DirFileInfo(dataDir_App_()))) {
        if (isDirectory_FileInfo(entry.value)) {
            continue;
        }
        if (endsWith_String(path_FileInfo(entry.value), cstr_String(suffix))) {
            removePath_CStr(cstr_String(path_FileInfo(entry.value)));
        }
    }
}
