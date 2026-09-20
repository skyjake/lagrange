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

#include <lagrange/core.h>
#include "history.h"
#include "ui/command.h"
#include "ui/documentwidget.h"
#include "ui/root.h"
#include "ui/util.h"
#include "ui/window.h"

#if defined (iPlatformAppleDesktop)
#   include "platform/macos.h"
#endif

iPtrArray *listWindows_App_(const iApp *d, iPtrArray *windows) {
    clear_PtrArray(windows);
    /* Popups. */ {
        iReverseConstForEach(PtrArray, i, &d->popupWindows) {
            pushBack_PtrArray(windows, i.ptr);
        }
    }
    /* Current active main window. */
    if (d->window) {
        pushBack_PtrArray(windows, d->window);
    }
    /* Other extra windows. */ {
        iConstForEach(PtrArray, i, &d->extraWindows) {
            if (i.ptr != d->window) {
                pushBack_PtrArray(windows, i.ptr);
            }
        }
    }
    /* Other main windows. */ {
        iConstForEach(PtrArray, i, &d->mainWindows) {
            if (i.ptr != d->window) {
                pushBack_PtrArray(windows, i.ptr);
            }
        }
    }
    return windows;
}

iPtrArray *listWindows_App(void) {
    iPtrArray *wins = new_PtrArray();
    listWindows_App_(&app_, wins);
    return wins;
}

void addWindow_App(iMainWindow *win) {
    iApp *d = &app_;
    pushBack_PtrArray(&d->mainWindows, win);
}

void removeWindow_App(iMainWindow *win) {
    iApp *d = &app_;
    removeOne_PtrArray(&d->mainWindows, win);
    if (isEmpty_PtrArray(&d->mainWindows)) {
        d->window = NULL;
    }
}

size_t numWindows_App(void) {
    return size_PtrArray(&app_.mainWindows);
}

size_t windowIndex_App(const iMainWindow *win) {
    return indexOf_PtrArray(&app_.mainWindows, win);
}

iMainWindow *newMainWindow_App(void) {
    iApp *d = &app_;
    const size_t placeIndex = nextWindowPlacementIndex_App_(d);
    iMainWindow *newWin = new_MainWindow(initialWindowRect_App_(d, placeIndex));
    newWin->place.placementIndex = placeIndex;
    addWindow_App(newWin); /* App takes ownership */
    SDL_ShowWindow(newWin->base.win);
    setCurrent_Window(newWin);
    if (isAppleDesktop_Platform() && size_PtrArray(mainWindows_App()) == 1) {
        /* Restore the window state as it was before (sidebars, navigation history) when
           opening a window again after all windows have been closed. */
        setActiveWindow_App(newWin);
        loadState_App_(d);
    }
    return newWin;
}

const iPtrArray *mainWindows_App(void) {
    return &app_.mainWindows;
}

const iPtrArray *regularWindows_App(void) {
    iApp *d = &app_;
    iPtrArray *wins = copy_Array(mainWindows_App());
    iConstForEach(PtrArray, i, &d->extraWindows) {
        pushBack_PtrArray(wins, i.ptr);
    }
    return collect_PtrArray(wins);
}

const iPtrArray *popupWindows_App(void) {
    return &app_.popupWindows;
}

void setActiveWindow_App(iAnyWindow *mainOrExtraWin) {
    iApp *d = &app_;
    d->window = mainOrExtraWin;
    /* Move the corresponding window to the front of the list so it gets to process events first. */
    if (d->window) {
        iAssert(d->window->type == main_WindowType || d->window->type == extra_WindowType);
        iPtrArray *list = (d->window->type == main_WindowType ? &d->mainWindows : &d->extraWindows);
        removeOne_PtrArray(list, d->window);
        pushFront_PtrArray(list, d->window);
    }
}

iWindow *activeWindow_App(void) {
    return app_.window;
}

void addPopup_App(iWindow *popup) {
    iApp *d = &app_;
    pushBack_PtrArray(&d->popupWindows, popup);
}

void removePopup_App(iWindow *popup) {
    iApp *d = &app_;
    removeOne_PtrArray(&d->popupWindows, popup);
}

void addExtraWindow_App(iWindow *extra) {
    iApp *d = &app_;
    pushBack_PtrArray(&d->extraWindows, extra);
}

void removeExtraWindow_App(iWindow *extra) {
    iApp *d = &app_;
    removeOne_PtrArray(&d->extraWindows, extra);
    if (d->window == extra) {
        d->window = NULL;
    }
}

iWindow *findWindow_App(int type, const char *widgetId) {
    iApp *d = &app_;
    iConstForEach(PtrArray,
                  i,
                  type == popup_WindowType   ? &d->popupWindows
                  : type == extra_WindowType ? &d->extraWindows
                                             : &d->mainWindows) {
        iWindow *win = i.ptr;
        iForIndices(r, win->roots) {
            iRoot *root = win->roots[r];
            if (root && findChild_Widget(root->widget, widgetId)) {
                return win;
            }
        }
    }
    return NULL;
}

iBool isLandscape_App(void) {
    const iInt2 size = size_Window(get_Window());
    if (isHandheld_Platform()) {
        /* Prefer the portrait layout due to the larger default font size.
           Can't fit as much stuff in the landscape navbar. */
        return size.x > size.y * 1.4f;
    }
    return size.x > size.y;
}

iDocumentWidget *document_Root(iRoot *d) {
    return iConstCast(iDocumentWidget *, currentTabPage_Widget(findChild_Widget(d->widget, "doctabs")));
}

iDocumentWidget *document_App(void) {
    iDocumentWidget *doc = document_Root(get_Root());
    if (doc) {
        return doc;
    }
    /* Try another window. */
    iConstForEach(PtrArray, i, mainWindows_App()) {
        iWindow *win = *i.value;
        iForIndices(j, win->roots) {
            if (win->roots[j]) {
                doc = document_Root(win->roots[j]);
                if (doc) {
                    return doc;
                }
            }
        }
    }
    return NULL;
}

iDocumentWidget *document_Command(const char *cmd) {
    /* Explicitly referenced. */
    iAnyObject *obj = pointerLabel_Command(cmd, "doc");
    if (obj) {
        return obj;
    }
    /* Implicit via source widget. */
    obj = pointer_Command(cmd);
    if (obj && isInstance_Object(obj, &Class_DocumentWidget)) {
        return obj;
    }
    /* Currently visible document. */
    return document_App();
}

iDocumentWidget *newTab_App(const iDocumentWidget *duplicateOf, int newTabFlags) {
    iWidget *tabs = findWidget_Root("doctabs");
    iDocumentWidget *doc = NULL;
    if (newTabFlags & reuseBlank_NewTabFlag && !duplicateOf && tabCount_Widget(tabs) == 1) {
        doc = (iDocumentWidget *) tabPage_Widget(tabs, 0);
        const iString *url = url_DocumentWidget(doc);
        if (cmpCase_String(url, "about:lagrange") && cmpCase_String(url, "about:blank")) {
            doc = NULL; /* don't reuse */
        }
    }
    if (!doc) {
        setFlags_Widget(tabs, hidden_WidgetFlag, iFalse);
        iWidget *newTabButton = findChild_Widget(tabs, "newtab");
        removeChild_Widget(newTabButton->parent, newTabButton);
        if (duplicateOf) {
            doc = duplicate_DocumentWidget(duplicateOf);
        }
        else {
            doc = new_DocumentWidget();
        }
        appendTabPage_Widget(tabs, as_Widget(doc), "", 0, 0);
        iRelease(doc); /* now owned by the tabs */
        /* Find and move to the insertion point. */
        if (~newTabFlags & append_NewTabFlag) {
            const size_t insertAt = tabPageIndex_Widget(
                tabs, findChild_Widget(tabs, cstr_String(&get_Root()->tabInsertId)));
            if (insertAt != iInvalidPos) {
                moveTabPage_Widget(tabs, tabCount_Widget(tabs) - 1, insertAt + 1);
            }
        }
        /* The next tab comes here. */
        set_String(&as_Widget(doc)->root->tabInsertId, id_Widget(as_Widget(doc)));
        addTabCloseButton_Widget(tabs, as_Widget(doc), "tabs.close");
        addChild_Widget(findChild_Widget(tabs, "tabs.buttons"), iClob(newTabButton));
        showOrHideNewTabButton_Root(tabs->root);
        if (newTabFlags & switchTo_NewTabFlag) {
            postCommandf_App("tabs.switch page:%p", doc);
        }
    }
    arrange_Widget(get_Root()->widget);
    refresh_Widget(tabs);
    postCommandf_Root(get_Root(), "tab.created id:%s", cstr_String(id_Widget(as_Widget(doc))));
    return doc;
}

void closeWindow_App(iWindow *win) {
    iApp *d = &app_;
    iAssert(win->type == main_WindowType || win->type == extra_WindowType);
    const iBool isMain = (win->type == main_WindowType);
    iWindow *activeWindow = d->window;
    /* Unlike a full app quit, this window won't be restored, so its backups can go too. */
    eraseBackupsForWindowSerial_App_(serial_Window(win));
    /* Preferences needs to be dismissed properly. */
    /* TODO: This needs a more generic dialog dismissal command system. Also needed for mobile! */
    if (win->type == extra_WindowType) {
        iWidget *prefs = findChild_Widget(win->roots[0]->widget, "prefs");
        /* The "prefs.dismiss" command normally will destroy the dialog, but since we are
           about to do it here, inform the handler of our intentions. */
        if (prefs && ~prefs->flags & destroyPending_WidgetFlag) {
            setFlags_Widget(prefs, destroyPending_WidgetFlag, iTrue);
            prefs->commandHandler(prefs, "prefs.dismiss");
        }
    }
    iForIndices(r, win->roots) {
        if (win->roots[r]) {
            setTreeFlags_Widget(win->roots[r]->widget, destroyPending_WidgetFlag, iTrue);
        }
    }
    collect_Garbage(win, isMain ? (iDeleteFunc) delete_MainWindow
                                : (iDeleteFunc) delete_Window);
    postRefresh_Window(NULL);
    if (isMain) {
        /* Remember this window's last placement. */
        iArray *rects = &d->initialWindowRects;
        const size_t idx = as_MainWindow(win)->place.placementIndex;
        if (idx >= size_Array(rects)) {
            resize_Array(rects, idx + 1);
        }
        set_Array(rects, idx, &as_MainWindow(win)->place.normalRect);
        if (isAppleDesktop_Platform() && size_PtrArray(&d->mainWindows) == 1) {
            /* App keeps running; Quit may not happen at all. */
            saveState_App_(d, iTrue);
            savePrefs_App_(d);
        }
    }
    if (activeWindow == win) {
        d->window = NULL;
        /* Activate another window. */
        iForEach(PtrArray, i, &d->mainWindows) {
            if (i.ptr != activeWindow) {
                iWindow *win = i.ptr;
                SDL_RaiseWindow(win->win);
                setActiveWindow_App(win); /* sets d->window */
                setCurrent_Window(win);
                break;
            }
        }
    }
    if (!d->window) {
        iForEach(PtrArray, j, &d->extraWindows) {
            iWindow *win = j.ptr;
            SDL_RaiseWindow(win->win);
            setActiveWindow_App(win);
            setCurrent_Window(win);
            break;
        }
    }
}

iObjectList *listDocuments_App(const iRoot *rootOrNull) {
    return listDocuments_MainWindow(get_MainWindow(), rootOrNull);
}

iStringSet *listOpenURLs_App(void) {
    iStringSet *set = new_StringSet();
    iObjectList *docs = listDocuments_App(NULL);
    iConstForEach(ObjectList, i, docs) {
        const iDocumentWidget *doc = i.object;
        if (isFetchingOwnLink_DocumentWidget(doc)) {
            /* The URL may still be reverted (e.g., an inline prompt), so avoid visual flicker. */
            continue;
        }
        insert_StringSet(set, canonicalUrl_String(url_DocumentWidget(doc)));
    }
    iRelease(docs);
    return set;
}

iMainWindow *mainWindow_App(void) {
    iApp *d = &app_;
    if (d->window && d->window->type == main_WindowType) {
        return as_MainWindow(d->window);
    }
    return NULL;
}

void closePopups_App(iBool doForce) {
    iUnused(doForce); /* not needed any more? */
    iApp *d = &app_;
    const uint32_t now = SDL_GetTicks();
    iForEach(PtrArray, i, &d->popupWindows) {
        iWindow *win = i.ptr;
        if (now - win->focusGainedAt > 200) {
            postCommand_Root(win->roots[0], "cancel");
        }
    }
}

void clearCache_App_(void) {
    iForEach(ObjectList, i, iClob(listDocuments_App(NULL))) {
        clearCache_History(history_DocumentWidget(i.object));
    }
}

iObjectList *listAllDocuments_App(void) {
    iWindow     *oldWindow = get_Window();
    iRoot       *oldRoot   = current_Root();
    iObjectList *allDocs   = new_ObjectList();
    iConstForEach(PtrArray, window, mainWindows_App()) {
        setCurrent_Window(window.ptr);
        iObjectList *docs = listDocuments_App(NULL);
        iForEach(ObjectList, i, docs) {
            pushBack_ObjectList(allDocs, i.object);
        }
        iRelease(docs);
    }
    setCurrent_Window(oldWindow);
    setCurrent_Root(oldRoot);
    return allDocs;
}

void trimCache_App(void) {
    iApp *d = &app_;
    size_t cacheSize = 0;
    const size_t limit = d->prefs.maxCacheSize * 1000000;
    iObjectList *docs = listAllDocuments_App();
    iForEach(ObjectList, i, docs) {
        cacheSize += cacheSize_History(history_DocumentWidget(i.object));
    }
    init_ObjectListIterator(&i, docs);
    iBool wasPruned = iFalse;
    while (cacheSize > limit) {
        iDocumentWidget *doc = i.object;
        const size_t pruned = pruneLeastImportant_History(history_DocumentWidget(doc));
        if (pruned) {
            cacheSize -= pruned;
            wasPruned = iTrue;
        }
        next_ObjectListIterator(&i);
        if (!i.value) {
            if (!wasPruned) break;
            wasPruned = iFalse;
            init_ObjectListIterator(&i, docs);
        }
    }
    iRelease(docs);
}

void trimMemory_App(void) {
    iApp *d = &app_;
    size_t memorySize = 0;
    const size_t limit = d->prefs.maxMemorySize * 1000000;
    iObjectList *docs = listAllDocuments_App();
    iForEach(ObjectList, i, docs) {
        memorySize += memorySize_History(history_DocumentWidget(i.object));
    }
    init_ObjectListIterator(&i, docs);
    iBool wasPruned = iFalse;
    while (memorySize > limit) {
        iDocumentWidget *doc = i.object;
        const size_t pruned = pruneLeastImportantMemory_History(history_DocumentWidget(doc));
        if (pruned) {
            memorySize -= pruned;
            wasPruned = iTrue;
        }
        next_ObjectListIterator(&i);
        if (!i.value) {
            if (!wasPruned) break;
            wasPruned = iFalse;
            init_ObjectListIterator(&i, docs);
        }
    }
    iRelease(docs);
}
