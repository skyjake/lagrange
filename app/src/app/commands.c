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
#include "export.h"
#include "feeds.h"
#include "fontpack.h"
#include "gempub.h"
#include <lagrange/core.h>
#include <lagrange/gmcerts.h>
#include <lagrange/gmrequest.h>
#include <lagrange/mimehooks.h>
#include <lagrange/resources.h>
#include <lagrange/sitespec.h>
#include <lagrange/snippets.h>
#include <lagrange/visited.h>
#include "gmdocument.h"
#include "gmutil.h"
#include "history.h"
#include "ipc.h"
#include "misfin.h"
#include "periodic.h"
#include "render/text.h"
#include "ui/certimportwidget.h"
#include "ui/command.h"
#include "ui/documentwidget.h"
#include "ui/gamepad.h"
#include "ui/inputwidget.h"
#include "ui/keys.h"
#include "ui/labelwidget.h"
#include "ui/prefsdialog.h"
#include "ui/root.h"
#include "ui/sidebarwidget.h"
#include "ui/touch.h"
#include "ui/uploadwidget.h"
#include "ui/util.h"
#include "ui/window.h"
#include "updater.h"

#include <the_Foundation/buffer.h>
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/path.h>
#include <the_Foundation/process.h>
#include <the_Foundation/stringset.h>

#if defined (iPlatformAppleDesktop)
#   include "platform/macos.h"
#endif
#if defined (iPlatformAppleMobile)
#   include "platform/ios.h"
#endif
#if defined (iPlatformAndroidMobile)
#   include "platform/android.h"
#endif
#if defined (iPlatformMsys) || defined (iPlatformWindows)
#   include "platform/win32.h"
#endif
#if defined (LAGRANGE_ENABLE_X11_XLIB)
#   include "platform/x11.h"
#endif

static iBool handleIdentityCreationCommands_(iWidget *dlg, const char *cmd) {
    iApp *d = &app_;
    if (equal_Command(cmd, "ident.showmore")) {
        iForEach(ObjectList,
                 i,
                 children_Widget(findChild_Widget(
                     dlg, isUsingPanelLayout_Mobile() ? "panel.top" : "headings"))) {
            if (flags_Widget(i.object) & collapse_WidgetFlag) {
                setFlags_Widget(i.object, hidden_WidgetFlag, iFalse);
            }
        }
        iForEach(ObjectList, j, children_Widget(findChild_Widget(dlg, "values"))) {
            if (flags_Widget(j.object) & collapse_WidgetFlag) {
                setFlags_Widget(j.object, hidden_WidgetFlag, iFalse);
            }
        }
        setFlags_Widget(pointer_Command(cmd), isUsingPanelLayout_Mobile() ? hidden_WidgetFlag : disabled_WidgetFlag, iTrue);
        const int oldY = dlg->rect.pos.y;
        arrange_Widget(dlg);
        refresh_Widget(dlg);
        dlg->rect.pos.y = oldY;
        return iTrue;
    }
    if (equal_Command(cmd, "ident.scope")) {
        iLabelWidget *scope = findChild_Widget(dlg, "ident.scope");
        updateDropdownSelection_LabelWidget(scope, format_CStr(" arg:%d", arg_Command(cmd)));
        updateSize_LabelWidget(scope);
        arrange_Widget(findWidget_App("ident"));
        return iTrue;
    }
    if (equal_Command(cmd, "ident.temp.changed")) {
        setFlags_Widget(
            findChild_Widget(dlg, "ident.temp.note"), hidden_WidgetFlag, !arg_Command(cmd));
        return iFalse;
    }
    if (equal_Command(cmd, "ident.accept") || equal_Command(cmd, "ident.cancel")) {
        if (equal_Command(cmd, "ident.accept")) {
            const iString *commonName   = text_InputWidget (findChild_Widget(dlg, "ident.common"));
            const iString *email        = text_InputWidget (findChild_Widget(dlg, "ident.email"));
            const iString *userId       = text_InputWidget (findChild_Widget(dlg, "ident.userid"));
            const iString *domain       = text_InputWidget (findChild_Widget(dlg, "ident.domain"));
            const iString *organization = text_InputWidget (findChild_Widget(dlg, "ident.org"));
            const iString *country      = text_InputWidget (findChild_Widget(dlg, "ident.country"));
            const iBool    isTemp       = isSelected_Widget(findChild_Widget(dlg, "ident.temp"));
            if (isEmpty_String(commonName)) {
                makeSimpleMessage_Widget(uiHeading_ColorEscape "${heading.newident.missing}",
                                         "${dlg.newindent.missing.commonname}");
                return iTrue;
            }
            iDate until;
            /* Validate the date. */ {
                iZap(until);
                unsigned int val[6];
                iDate today;
                initCurrent_Date(&today);
                const int n =
                    sscanf(cstr_String(text_InputWidget(findChild_Widget(dlg, "ident.until"))),
                           "%u-%u-%u %u:%u:%u",
                           &val[0], &val[1], &val[2], &val[3], &val[4], &val[5]);
                if (n <= 0) {
                    makeSimpleMessage_Widget(uiHeading_ColorEscape "${heading.newident.date.bad}",
                                             "${dlg.newident.date.example}");
                    return iTrue;
                }
                until.year   = val[0];
                until.month  = n >= 2 ? val[1] : 1;
                until.day    = n >= 3 ? val[2] : 1;
                until.hour   = n >= 4 ? val[3] : 0;
                until.minute = n >= 5 ? val[4] : 0;
                until.second = n == 6 ? val[5] : 0;
                until.gmtOffsetSeconds = today.gmtOffsetSeconds;
                if (!cmp_String(
                        text_InputWidget(findChild_Widget(dlg, "ident.until")),
                        "9999-12-31 23:59:59")) {
                    /* This is a special "indefinite" date, intended to be set in GMT. */
                    until.gmtOffsetSeconds = 0;
                }
                /* In the past? */ {
                    iTime now, t;
                    initCurrent_Time(&now);
                    if (!isUndefinedX509_Date(&until) &&
                            (init_Time(&t, &until), cmp_Time(&t, &now)) <= 0) {
                        makeSimpleMessage_Widget(uiHeading_ColorEscape
                                                 "${heading.newident.date.bad}",
                                                 "${dlg.newident.date.past}");
                        return iTrue;
                    }
                }
            }
            /* The input seems fine. */
            iGmIdentity *ident = newIdentity_GmCerts(d->certs,
                                                     isTemp ? temporary_GmIdentityFlag : 0,
                                                     until,
                                                     commonName,
                                                     email,
                                                     userId,
                                                     domain,
                                                     organization,
                                                     country);
            /* Use in the chosen scope. */ {
                int         selScope = 0;
                const char *scopeCmd =
                    selectedDropdownCommand_LabelWidget(findChild_Widget(dlg, "ident.scope"));
                if (startsWith_CStr(scopeCmd, "ident.scope arg:")) {
                    selScope = arg_Command(scopeCmd);
                }
                const iString *docUrl = url_DocumentWidget(document_Root(dlg->root));
                iString *useUrl = NULL;
                switch (selScope) {
                    default: /* not used */
                        break;
                    case 1: /* current domain */
                        useUrl = collectNewFormat_String("gemini://%s",
                                                         cstr_Rangecc(urlHost_String(docUrl)));
                        break;
                    case 2: { /* current directory */
                        useUrl = collectNewFormat_String("gemini://%s%s",
                                                         cstr_Rangecc(urlHost_String(docUrl)),
                                                         cstr_Rangecc(urlDirectory_String(docUrl)));
                        break;
                    }
                    case 3: /* current page */
                        useUrl = collect_String(copy_String(docUrl));
                        break;
                }
                if (useUrl) {
                    signIn_GmCerts(d->certs, ident, useUrl);
                    postCommand_App("navigate.reload");
                }
            }
            postCommandf_App("sidebar.mode arg:%d show:1", identities_SidebarMode);
            postCommand_App("idents.changed");
        }
        setupSheetTransition_Mobile(dlg, dialogTransitionDir_Widget(dlg));
        destroy_Widget(dlg);
        return iTrue;
    }
    return iFalse;
}

static void invalidateCachedDocuments_App_(void) {
    iForEach(ObjectList, i, iClob(listDocuments_App(NULL))) {
        invalidateCachedLayout_History(history_DocumentWidget(i.object));
    }
}

static void pushClosedTabUrl_App_(iApp *d, const iString *url) {
    pushBack_StringList(d->recentlyClosedTabUrls, url);
    if (size_StringList(d->recentlyClosedTabUrls) > 50) { /* not an infinite number */
        popFront_StringList(d->recentlyClosedTabUrls);
    }
}

static const iString *popClosedTabUrl_App_(iApp *d) {
    if (isEmpty_StringList(d->recentlyClosedTabUrls)) {
        return NULL;
    }
    return collect_String(takeLast_StringList(d->recentlyClosedTabUrls));
}

iBool handleNonWindowRelatedCommand_App_(iApp *d, const char *cmd) {
    if (handlePrefsCommand_App_(d, cmd)) {
        return iTrue;
    }
    if (equal_Command(cmd, "downloads.open")) {
        postCommandf_App("open newtab:%d url:%s",
                         argLabel_Command(cmd, "newtab"),
                         cstrCollect_String(makeFileUrl_String(downloadDir_App())));
        return iTrue;
    }
    else if (equal_Command(cmd, "search")) {
        const int newTab = argLabel_Command(cmd, "newtab");
        const iString *query = collect_String(suffix_Command(cmd, "query"));
        if (!isLikelyUrl_String(query)) {
            const iString *url = searchQueryUrl_App(query);
            if (!isEmpty_String(url)) {
                postCommandf_App("open newtab:%d url:%s", newTab, cstr_String(url));
            }
        }
        else {
            postCommandf_App("open newtab:%d url:%s", newTab, cstr_String(query));
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "reveal")) {
        const iString *path = NULL;
        if (hasLabel_Command(cmd, "path")) {
            path = suffix_Command(cmd, "path");
        }
        else if (hasLabel_Command(cmd, "url")) {
            path = collect_String(localFilePathFromUrl_String(suffix_Command(cmd, "url")));
        }
        if (path) {
            revealPath_App(path);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "window.new")) {
#if !defined (iPlatformTerminal)
        iMainWindow *newWin = newMainWindow_App();
        if (hasLabel_Command(cmd, "url")) {
            const char *urlAndArgs = cmd + 11; /* all arguments to "window.new" passed on */
            if (strlen(suffixPtr_Command(cmd, "url")) /* not empty URL */) {
                /* We pass a pointer to the correct DocumentWidget because if the
                   event queue is busy, the active window may still switch away from
                   `newWin` before the "open" is handled. ("open" is an app-level
                   command so it isn't handled by any widget directly.) */
                postCommandf_App("~open doc:%p %s",
                                  document_Root(newWin->base.roots[0]),
                                  urlAndArgs);
            }
        }
        else {
            postCommand_Root(newWin->base.roots[0], "~navigate.home focus:1");
        }
        postCommand_Root(newWin->base.roots[0], "~window.unfreeze");
#endif
        return iTrue;
    }
    else if (equal_Command(cmd, "bookmarks.changed")) {
        save_Bookmarks(d->bookmarks, dataDir_App_());
#if defined (iPlatformAppleDesktop) && defined (LAGRANGE_NATIVE_MENU)
        /* Update the macOS Bookmarks menu items. These application menu submenus need to
           exist without any windows existing, so we use an offscreen Root to store them. */
        iRoot *oldRoot = current_Root();
        setCurrent_Root(d->submenuRoot);
        const iArray *items = updateBookmarksMenu_Widget(NULL);
        updateMenuItems_MacOS(4, constData_Array(items), size_Array(items));
        setCurrent_Root(oldRoot);
#endif
        return iFalse;
    }
    else if (equal_Command(cmd, "bookmarks.sort")) {
        sort_Bookmarks(d->bookmarks, arg_Command(cmd), cmpTitleAscending_Bookmark);
        notify_App("bookmarks.changed");
        return iTrue;
    }
    else if (equal_Command(cmd, "bookmarks.reload.remote")) {
        fetchRemote_Bookmarks(bookmarks_App());
        return iTrue;
    }
    else if (equal_Command(cmd, "bookmarks.request.finished")) {
        requestFinished_Bookmarks(bookmarks_App(), pointerLabel_Command(cmd, "req"));
        return iTrue;
    }
    else if (equal_Command(cmd, "feeds.refresh")) {
        refresh_Feeds();
        return iTrue;
    }
    else if (equal_Command(cmd, "feeds.reset")) {
        resetKnownEntries_Feeds();
        notify_App("feeds.update.finished"); /* not really, but we have zero entries now */
        return iTrue;
    }
    else if (equal_Command(cmd, "visited.changed")) {
        /* The visited file can grow large, so don't keep rewriting it after every navigation. */
        d->pendingVisitedSave = iTrue;
        deferVisitedSave_App();
        return iFalse;
    }
    else if (equal_Command(cmd, "idents.changed")) {
        saveIdentities_GmCerts(d->certs);
        return iFalse;
    }
    else if (equal_Command(cmd, "ident.signin")) {
        const iString *url = collect_String(suffix_Command(cmd, "url"));
        signIn_GmCerts(
            d->certs,
            findIdentity_GmCerts(d->certs, collect_Block(hexDecode_Rangecc(range_Command(cmd, "ident")))),
            url);
        postCommand_App("navigate.reload");
        postCommand_App("idents.changed");
        return iTrue;
    }
    else if (equal_Command(cmd, "ident.signout")) {
        iGmIdentity *ident = findIdentity_GmCerts(
            d->certs, collect_Block(hexDecode_Rangecc(range_Command(cmd, "ident"))));
        if (arg_Command(cmd)) {
            clearUse_GmIdentity(ident);
        }
        else {
            setUse_GmIdentity(ident, collect_String(suffix_Command(cmd, "url")), iFalse);
        }
        postCommand_App("navigate.reload");
        postCommand_App("idents.changed");
        return iTrue;
    }
    else if (equal_Command(cmd, "os.theme.changed")) {
        const int dark = argLabel_Command(cmd, "dark");
        d->isDarkSystemTheme = dark;
        if (d->prefs.useSystemTheme) {
            const int contrast  = argLabel_Command(cmd, "contrast");
            const int preferred = d->prefs.systemPreferredColorTheme[dark ^ 1];
            postCommandf_App("theme.set arg:%d auto:1",
                             preferred >= 0 ? preferred
                             : dark ? (contrast ? pureBlack_ColorTheme : dark_ColorTheme)
                                            : (contrast ? pureWhite_ColorTheme : light_ColorTheme));
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "updater.check")) {
        checkNow_Updater();
        return iTrue;
    }
    else if (equal_Command(cmd, "fontpack.enable")) {
        const iString *packId = collect_String(suffix_Command(cmd, "id"));
        enablePack_Fonts(packId, arg_Command(cmd));
        postCommand_App("navigate.reload");
        return iTrue;
    }
#if defined (LAGRANGE_ENABLE_IPC)
    else if (equal_Command(cmd, "ipc.list.urls")) {
        iProcessId pid = argLabel_Command(cmd, "pid");
        if (pid) {
            iString *urls = collectNew_String();
            iConstForEach(ObjectList, i, iClob(listDocuments_App(NULL))) {
                append_String(urls, url_DocumentWidget(i.object));
                appendCStr_String(urls, "\n");
            }
            write_Ipc(pid, urls, response_IpcWrite);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "ipc.active.url")) {
        write_Ipc(argLabel_Command(cmd, "pid"),
                  collectNewFormat_String(
                      "%s\n", d->window ? cstr_String(url_DocumentWidget(document_App())) : ""),
                  response_IpcWrite);
        return iTrue;
    }
    else if (equal_Command(cmd, "ipc.signal")) {
        if (argLabel_Command(cmd, "raise")) {
            if (d->window && d->window->win) {
                SDL_RaiseWindow(d->window->win);
            }
        }
        signal_Ipc(arg_Command(cmd));
        return iTrue;
    }
#endif /* defined (LAGRANGE_ENABLE_IPC) */
    else if (equal_Command(cmd, "quit")) {
        SDL_Event ev;
        ev.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&ev);
    }
    return iFalse;
}

static iBool handleOpenCommand_App_(iApp *d, const char *cmd) {
    iAssert(equal_Command(cmd, "open"));
    const char *urlArg = suffixPtr_Command(cmd, "url");
    if (!urlArg) {
        return iTrue; /* invalid command */
    }
    /* TODO: "idle" should be passed to 'open' reinvocations */
    iString *setIdentArg = collectNew_String();
    iBool hasSetIdent = iFalse;
    if (hasLabel_Command(cmd, "setident")) {
        hasSetIdent = iTrue;
        setCStr_String(setIdentArg, " setident:");
        appendRange_String(setIdentArg, range_Command(cmd, "setident"));
    }
    if (argLabel_Command(cmd, "query")) {
        const iString *url = collectNewCStr_String(urlArg);
        iString *spartanCmd = collectNewFormat_String(
            "spartan.input newtab:%d newwindow:%d urlesc:%s",
            argLabel_Command(cmd, "newtab"),
            argLabel_Command(cmd, "newwindow"),
            cstr_String(withSpacesEncoded_String(url)));
        iUrl parts;
        init_Url(&parts, url);
        iWidget *dlg = makeValueInputWithAdditionalActions_Widget(
            as_Widget(document_Command(cmd)),
            NULL,
            cstr_Rangecc((iRangecc){ parts.host.start, parts.path.end }),
            "${spartan.input}",
            "${dlg.input.send}",
            cstr_String(spartanCmd),
            (iMenuItem[]){ { "${dlg.spartan.upload}",
                             SDLK_U,
                             KMOD_PRIMARY,
                             format_CStr("valueinput.upload url:%s",
                                         cstr_String(urlQueryStripped_String(url))) } },
            1);
        setBackupFileName_InputWidget(findChild_Widget(dlg, "input"), "spartanbackup");
        if (!isEmpty_Range(&parts.query)) {
            postCommand_Widget(dlg,
                               "valueinput.set select:1 text:%s",
                               cstrCollect_String(urlDecode_String(collectNewRange_String(
                                   (iRangecc){ parts.query.start + 1, parts.query.end }))));
        }
        return iTrue;
    }
    if (argLabel_Command(cmd, "switch")) {
        iDocumentWidget *doc = findDocument_Root(get_Root(), collectNewCStr_String(urlArg));
        if (doc) {
            showTabPage_Widget(findWidget_Root("doctabs"), doc);
            return iTrue;
        }
    }
    if (findWidget_App("prefs")) {
        postCommand_App("prefs.dismiss");
    }
    if (argLabel_Command(cmd, "newwindow")) {
        const iRangecc gotoheading = range_Command(cmd, "gotoheading");
        const iRangecc gotourlheading = range_Command(cmd, "gotourlheading");
        postCommandf_Root(get_Root(), "window.new%s%s%s%s%s url:%s",
                          cstr_String(setIdentArg),
                          isEmpty_Range(&gotoheading) ? "" : " gotoheading:",
                          isEmpty_Range(&gotoheading) ? "" : cstr_Rangecc(gotoheading),
                          isEmpty_Range(&gotourlheading) ? "" : " gotourlheading:",
                          isEmpty_Range(&gotourlheading) ? "" : cstr_Rangecc(gotourlheading),
                          urlArg);
        return iTrue;
    }
    iString    *url       = newCStr_String(urlArg);
    const iBool noProxy   = argLabel_Command(cmd, "noproxy") != 0;
    const iBool isHistory = argLabel_Command(cmd, "history") != 0;
    iUrl parts;
    init_Url(&parts, url);
    if (equal_Rangecc(parts.scheme, "about") && equal_Rangecc(parts.path, "command") &&
        !isEmpty_Range(&parts.query)) {
        /* NOTE: Careful here! `about:command` allows issuing UI events via links on the page.
           There is a special set of pages where these are allowed (e.g., "about:fonts").
           On every other page, `about:command` links will not be clickable. */
        iString *query = collectNewRange_String((iRangecc){
            parts.query.start + 1, parts.query.end
        });
        replace_String(query, "%20", " ");
        postCommandString_Root(NULL, query);
        delete_String(url);
        return iTrue;
    }
    iDocumentWidget *doc = document_Command(cmd);
    if (doc && as_Widget(doc)->root != get_Root()) {
        /* Command may originate from a window without documents (e.g., Preferences). */
        setCurrent_Window(as_Widget(doc)->root->window);
        setCurrent_Root(as_Widget(doc)->root);
    }
    if (equalCase_Rangecc(parts.scheme, "titan")) {
        if (!isHistory) {
            setRedirectCount_DocumentWidget(doc, 0);
            iUploadWidget *upload = new_UploadWidget(titan_UploadProtocol);
            setUrl_UploadWidget(upload, url);
            setResponseViewer_UploadWidget(upload, document_App());
            addChild_Widget(get_Root()->widget, iClob(upload));
            setupSheetTransition_Mobile(as_Widget(upload),
                                        incoming_TransitionFlag |
                                            dialogTransitionDir_Widget(as_Widget(upload)));
            /* User can resize the upload dialog. */
            setResizeId_Widget(as_Widget(upload), "upload");
            restoreWidth_Widget(as_Widget(upload));
            postRefresh_Window(get_Window());
            delete_String(url);
            return iTrue;
        }
    }
    if (equalCase_Rangecc(parts.scheme, "misfin")) {
        if (!isHistory) {
            openMessageComposer_Misfin(url, NULL);
            delete_String(url);
            return iTrue;
        }
    }
    if (argLabel_Command(cmd, "default") || equalCase_Rangecc(parts.scheme, "mailto") ||
        ((noProxy || isEmpty_String(&d->prefs.strings[httpProxy_PrefsString])) &&
         (equalCase_Rangecc(parts.scheme, "http") ||
          equalCase_Rangecc(parts.scheme, "https")))) {
        openInDefaultBrowser_App(url, string_Command(cmd, "mime"));
        delete_String(url);
        return iTrue;
    }
    iAssert(doc);
    iDocumentWidget *origin = doc;
    if (hasLabel_Command(cmd, "origin")) {
        iDocumentWidget *cmdOrig = findWidget_App(cstr_Command(cmd, "origin"));
        if (cmdOrig) {
            origin = cmdOrig;
        }
    }
    int newTab = argLabel_Command(cmd, "newtab");
    if (newTab & otherRoot_OpenTabFlag && numRoots_Window(get_Window()) == 1) {
        /* Need to split first. */
        const int splitMode = argLabel_Command(cmd, "splitmode");
        postCommandf_App("ui.split arg:%d axis:%d origin:%s newtab:%d%s url:%s",
                         splitMode ? splitMode : 3 /* equal weights */,
                         defaultSplitAxis_MainWindow(get_MainWindow()),
                         cstr_String(id_Widget(as_Widget(origin))),
                         newTab & ~otherRoot_OpenTabFlag,
                         cstr_String(setIdentArg),
                         cstr_String(url));
        delete_String(url);
        return iTrue;
    }
    iRoot *root = get_Root();
    iRoot *oldRoot = root;
    if (newTab & otherRoot_OpenTabFlag) {
        root = otherRoot_Window(get_Window(), root);
        setKeyRoot_Window(get_Window(), root);
        setCurrent_Root(root); /* need to change for widget creation */
        doc = document_Command(cmd); /* may be different */
    }
    /* If not opening in a new tab, we must not domains/roots accidentally. */
    if (!isHistory &&
        isIdentityPinned_DocumentWidget(doc) &&
        (newTab & newTabMask_OpenTabFlag) == 0 &&
        !isSetIdentityRetained_DocumentWidget(doc, url)) {
        /* Ensure a new tab is opened where there is no set identity. */
        newTab = new_OpenTabFlag;
    }
    const iBool isNewTabDoc = (newTab & newTabMask_OpenTabFlag) != 0;
    if (isNewTabDoc) {
        /* `newtab:2` to open in background */
        doc = newTab_App(NULL, (newTab & new_OpenTabFlag) != 0 ? switchTo_NewTabFlag : 0);
    }
    setOpenedExternally_DocumentWidget(doc, argLabel_Command(cmd, "external") != 0);
    iHistory   *history       = history_DocumentWidget(doc);
    const iBool waitForIdle   = argLabel_Command(cmd, "idle") != 0;
    int         redirectCount = argLabel_Command(cmd, "redirect");
    if (!isHistory) {
        /* TODO: Shouldn't DocumentWidget manage history on its own? */
        if (redirectCount) {
            replace_History(history, url);
        }
        else {
            add_History(history, url);
        }
    }
    setInitialScroll_DocumentWidget(doc, argfLabel_Command(cmd, "scroll"));
    setRedirectCount_DocumentWidget(doc, redirectCount);
    setOrigin_DocumentWidget(doc, origin, isNewTabDoc);
    showCollapsed_Widget(findWidget_App("document.progress"), iFalse);
    /* Do the fetch or load page from cache. */
    setUrlFlags_DocumentWidget(
        doc,
        url,
        (isHistory ? useCachedContentIfAvailable_DocumentWidgetSetUrlFlag : 0) |
            (argLabel_Command(cmd, "notinline") ? preventInlining_DocumentWidgetSetUrlFlag : 0) |
            (waitForIdle ? waitForOtherDocumentsToIdle_DocumentWidgetSetUrlFag : 0),
        hasSetIdent ? collect_Block(hexDecode_Rangecc(range_Command(cmd, "setident"))) : NULL);
    /* Optionally, jump to a text in the document. This will only work if the document
       is already available, e.g., it's from "about:" or restored from cache. */
    const iRangecc gotoHeading = range_Command(cmd, "gotoheading");
    if (gotoHeading.start) {
        postCommandf_Root(root, "document.goto heading:%s", cstr_Rangecc(gotoHeading));
    }
    const iRangecc gotoUrlHeading = range_Command(cmd, "gotourlheading");
    if (gotoUrlHeading.start) {
        postCommandf_Root(root, "document.goto heading:%s",
                          cstrCollect_String(urlDecode_String(
                              collect_String(newRange_String(gotoUrlHeading)))));
    }
    setCurrent_Root(oldRoot);
    delete_String(url);
    return iTrue;
}

static iBool shouldCreateWindowForCommand_App_(const char *cmd) {
    /* Determines if a window should be opened before handling a command, if there are
       currently no windows open. */
    if (isTerminal_Platform()) {
        return iFalse;
    }
    static const char *cmds_[] = {
        "open", "export", "navigate.focus", "sidebar.mode", "sidebar.toggle", "tabs.new",
        "ident.new", "ident.import"
    };
    iForIndices(i, cmds_) {
        if (equal_Command(cmd, cmds_[i])) {
            return iTrue;
        }
    }
    return iFalse;
}

iBool handleCommand_App(const char *cmd) {
    iApp *d = &app_;
    const iBool isFrozen   = isDrawFrozen_Window(d->window);
    const iBool isHeadless = numWindows_App() == 0;
    const iBool isMainWin  = d->window && d->window->type == main_WindowType;
    if (handleNonWindowRelatedCommand_App_(d, cmd)) {
        return iTrue;
    }
    if (isHeadless) {
        if (shouldCreateWindowForCommand_App_(cmd)) {
            iMainWindow *newWin = newMainWindow_App();
            setCurrent_Window(newWin);
            setActiveWindow_App(newWin);
            postCommand_Root(newWin->base.roots[0], "window.unfreeze");
            /* Window creation may have side effects, so repost the original command after
               those have been handled. */
            postCommand_App(cmd);
            return iTrue;
        }
        else {
            /* All the subsequent commands assume that a window exists. */
            return iFalse;
        }
    }
    /* TODO: Maybe break this up a little bit? There's a very long list of ifs here. */
    if (equal_Command(cmd, "config.error")) {
        makeSimpleMessage_Widget(uiTextCaution_ColorEscape "CONFIG ERROR",
                                 format_CStr("Error in config file: %s\n"
                                             "See \"about:debug\" for details.",
                                             suffixPtr_Command(cmd, "where")));
        return iTrue;
    }
    else if (equal_Command(cmd, "ui.split") && isMainWin) {
        if (argLabel_Command(cmd, "swap")) {
            swapRoots_MainWindow(as_MainWindow(d->window));
            return iTrue;
        }
        if (argLabel_Command(cmd, "focusother")) {
            iWindow *baseWin = d->window;
            if (baseWin->roots[1]) {
                baseWin->keyRoot =
                    (baseWin->keyRoot == baseWin->roots[1] ? baseWin->roots[0] : baseWin->roots[1]);
            }
        }
        iMainWindow *mw = as_MainWindow(d->window);
        mw->pendingSplitMode =
            (argLabel_Command(cmd, "axis") ? vertical_WindowSplit : 0) | (arg_Command(cmd) << 1);
        const char *url = suffixPtr_Command(cmd, "url");
        setCStr_String(mw->pendingSplitUrl, url ? url : "");
        setRange_String(mw->pendingSplitSetIdent, range_Command(cmd, "setident"));
        if (hasLabel_Command(cmd, "origin")) {
            set_String(mw->pendingSplitOrigin, string_Command(cmd, "origin"));
        }
        postRefresh_Window(mw);
        return iTrue;
    }
    else if (equal_Command(cmd, "window.maximize")) {
        const size_t winIndex = argU32Label_Command(cmd, "index");
        if (winIndex < size_PtrArray(&d->mainWindows)) {
            iMainWindow *win = at_PtrArray(&d->mainWindows, winIndex);
            if (!argLabel_Command(cmd, "toggle")) {
                setSnap_MainWindow(win, maximized_WindowSnap);
            }
            else {
                setSnap_MainWindow(
                    win, snap_MainWindow(win) == maximized_WindowSnap ? 0 : maximized_WindowSnap);
            }
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "window.fullscreen") && isMainWin) {
        const iBool wasFull = snap_MainWindow(as_MainWindow(d->window)) == fullscreen_WindowSnap;
        setSnap_MainWindow(as_MainWindow(d->window), wasFull ? 0 : fullscreen_WindowSnap);
        postCommandf_App("window.fullscreen.changed arg:%d", !wasFull);
        return iTrue;
    }
    else if (equal_Command(cmd, "font.reset")) {
        resetFonts_App();
        return iTrue;
    }
    else if (equal_Command(cmd, "font.reload")) {
        reload_Fonts(); /* also does font cache reset, window invalidation */
        return iTrue;
    }
    else if (equal_Command(cmd, "font.find")) {
        searchOnlineLibraryForCharacters_Fonts(string_Command(cmd, "chars"));
        return iTrue;
    }
    else if (equal_Command(cmd, "font.found")) {
        if (hasLabel_Command(cmd, "error")) {
            makeSimpleMessage_Widget("${heading.glyphfinder}",
                                     format_CStr("%d %s",
                                                 argLabel_Command(cmd, "error"),
                                                 suffixPtr_Command(cmd, "msg")));
            return iTrue;
        }
        iString *src = collectNew_String();
        setCStr_String(src, "# ${heading.glyphfinder.results}\n\n");
        iRangecc path = iNullRange;
        iBool isFirst = iTrue;
        while (nextSplit_Rangecc(range_Command(cmd, "packs"), ",", &path)) {
            if (isFirst) {
                appendCStr_String(src, "${glyphfinder.results}\n\n");
            }
            iRangecc fpath = path;
            iRangecc fsize = path;
            fpath.end = strchr(fpath.start, ';');
            fsize.start = fpath.end + 1;
            const uint32_t size = strtoul(fsize.start, NULL, 10);
            appendFormat_String(src, "=> gemini://skyjake.fi/fonts/%s %s (%.1f MB)\n",
                                cstr_Rangecc(fpath),
                                cstr_Rangecc(fpath),
                                (double) size / 1.0e6);
            isFirst = iFalse;
        }
        if (isFirst) {
            appendFormat_String(src, "${glyphfinder.results.empty}\n");
        }
        appendCStr_String(src, "\n=> about:fonts ${menu.fonts}");
        iDocumentWidget *page = newTab_App(NULL, switchTo_NewTabFlag);
        translate_Lang(src);
        setUrlAndSource_DocumentWidget(page,
                                       collectNewCStr_String(""),
                                       collectNewCStr_String("text/gemini"),
                                       utf8_String(src),
                                       0);
        return iTrue;
    }
    else if (equal_Command(cmd, "inputzoom.set")) {
        d->prefs.inputZoomLevel = arg_Command(cmd);
        d->prefs.inputZoomLevel = iClamp(d->prefs.inputZoomLevel, 0, 2);
        return iTrue;
    }
    else if (equal_Command(cmd, "uploadzoom.set")) {
        d->prefs.editorZoomLevel = arg_Command(cmd);
        d->prefs.editorZoomLevel = iClamp(d->prefs.editorZoomLevel, 0, 3);
        return iTrue;
    }
    else if (equal_Command(cmd, "zoom.set")) {
        if (arg_Command(cmd) != d->prefs.zoomPercent) {
            d->prefs.zoomPercent = arg_Command(cmd);
            invalidateCachedDocuments_App_();
            iConstForEach(PtrArray, wind, mainWindows_App()) {
                if (!isFrozen) {
                    setFreezeDraw_MainWindow(wind.ptr, iTrue); /* no intermediate draws before docs updated */
                }
                setDocumentFontSize_Text(text_Window(wind.ptr), (float) d->prefs.zoomPercent / 100.0f);
            }
            if (!isFrozen) {
                postCommand_App("font.changed");
                postCommand_App("window.unfreeze");
            }
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "zoom.delta")) {
        int delta = arg_Command(cmd);
        if (d->prefs.zoomPercent < 100 || (delta < 0 && d->prefs.zoomPercent == 100)) {
            delta /= 2;
        }
        const int oldZoom = d->prefs.zoomPercent;
        d->prefs.zoomPercent = iClamp(d->prefs.zoomPercent + delta, 50, 200);
        if (oldZoom != d->prefs.zoomPercent) {
            invalidateCachedDocuments_App_();
            iConstForEach(PtrArray, wind, mainWindows_App()) {
                if (!isFrozen) {
                    setFreezeDraw_MainWindow(wind.ptr, iTrue); /* no intermediate draws before docs updated */
                }
                setDocumentFontSize_Text(text_Window(wind.ptr), (float) d->prefs.zoomPercent / 100.0f);
            }
            if (!isFrozen) {
                postCommand_App("font.changed");
                postCommand_App("window.unfreeze");
            }
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "spartan.input")) {
        const char *value = suffixPtr_Command(cmd, "value");
        iRangecc url = range_Command(cmd, "urlesc");
        postCommand_Widget(
            document_Command(cmd),
            "open newtab:%d newwindow:%d url:%s?%s",
            argLabel_Command(cmd, "newtab"),
            argLabel_Command(cmd, "newwindow"),
            cstr_String(urlQueryStripped_String(collectNewRange_String(url))),
            cstr_String(collect_String(urlEncode_String(collectNewCStr_String(value)))));
        return iTrue;
    }
    else if (equal_Command(cmd, "open")) {
        return handleOpenCommand_App_(d, cmd);
    }
    else if (equal_Command(cmd, "file.open")) {
        const char *path = suffixPtr_Command(cmd, "path");
        if (path) {
            postCommandf_App("open temp:%d url:%s",
                             argLabel_Command(cmd, "temp"),
                             makeFileUrl_CStr(path));
            return iTrue;
        }
#if defined (iPlatformAppleMobile)
        pickFile_iOS("file.open");
#endif
#if defined (iPlatformAndroidMobile)
        pickFile_Android("file.open");
#endif
        return iTrue;
    }
    else if (equal_Command(cmd, "file.delete")) {
        const char *path = suffixPtr_Command(cmd, "path");
        if (argLabel_Command(cmd, "confirm")) {
            makeQuestion_Widget(
                uiHeading_ColorEscape "${heading.file.delete}",
                format_CStr("${dlg.file.delete.confirm}\n%s", path),
                (iMenuItem[]){
                    { "${cancel}", 0, 0, NULL },
                    { uiTextCaution_ColorEscape "${dlg.file.delete}", 0, 0,
                      format_CStr("!file.delete path:%s", path) } },
                2);
        }
        else {
            removePath_CStr(path);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "document.request.cancelled")) {
        /* TODO: How should cancelled requests be treated in the history? */
#if 0
        if (d->historyPos == 0) {
            iHistoryItem *item = historyItem_App_(d, 0);
            if (item) {
                /* Pop this cancelled URL off history. */
                deinit_HistoryItem(item);
                popBack_Array(&d->history);
                printHistory_App_(d);
            }
        }
#endif
        return iFalse;
    }
    else if (equal_Command(cmd, "tabs.new") && isMainWin) {
        if (argLabel_Command(cmd, "reopen")) {
            const iString *reopenUrl = popClosedTabUrl_App_(d);
            if (reopenUrl) {
                newTab_App(NULL, iTrue);
                postCommandf_App("open url:%s", cstr_String(reopenUrl));
            }
            return iTrue;
        }
        const iBool isAppend    = argLabel_Command(cmd, "append") != 0;
        const iBool isDuplicate = argLabel_Command(cmd, "duplicate") != 0;
        newTab_App(isDuplicate ? document_App() : NULL,
                   switchTo_NewTabFlag | (isAppend ? append_NewTabFlag : 0));
        if (!isDuplicate) {
            postCommandf_App("navigate.home focus:%d", deviceType_App() == desktop_AppDeviceType);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "tabs.close") && isMainWin) {
        iWidget *tabs = hasLabel_Command(cmd, "tabs") ? pointerLabel_Command(cmd, "tabs")
                                                      : findWidget_App("doctabs");
        /* Can't close the last tab on mobile. Same on Windows/Linux when it's the
           last window, since the app cannot keep running without any windows open. */
        if (tabCount_Widget(tabs) == 1 && numRoots_Window(get_Window()) == 1 &&
            (isMobile_Platform() ||
             ((isWindows_Platform() || isLinux_Platform()) && numWindows_App() == 1))) {
            postCommand_App("document.unsetident"); /* implicit unpinning since a tab is closing */
            /* On mobile, we go home because the Home action isn't readily available
               elsewhere, unless the user has it in the toolbar. */
            postCommand_App(isMobile_Platform() ? "navigate.home" : "open url:about:blank");
            return iTrue;
        }
        const iRangecc tabId = range_Command(cmd, "id");
        iWidget *      doc   = !isEmpty_Range(&tabId) ? findChild_Widget(tabs, cstr_Rangecc(tabId))
                                                      : document_App();
        iBool          wasCurrent       = (doc == (iWidget *) document_App());
        size_t         index            = tabPageIndex_Widget(tabs, doc);
        const iBool    isRightmost      = (index == tabCount_Widget(tabs) - 1);
        iBool          wasClosed        = iFalse;
        const int      closedGeneration = generation_DocumentWidget((iDocumentWidget *) doc);
        notify_App("document.openurls.changed");
        if (argLabel_Command(cmd, "toright")) {
            while (tabCount_Widget(tabs) > index + 1) {
                iDocumentWidget *closed = (iDocumentWidget *) removeTabPage_Widget(tabs, index + 1);
                pushClosedTabUrl_App_(d, url_DocumentWidget(closed));
                cancelAllRequests_DocumentWidget(closed);
                destroy_Widget(as_Widget(closed));
            }
            wasClosed = iTrue;
        }
        if (argLabel_Command(cmd, "toleft")) {
            while (index-- > 0) {
                iDocumentWidget *closed = (iDocumentWidget *) removeTabPage_Widget(tabs, 0);
                pushClosedTabUrl_App_(d, url_DocumentWidget(closed));
                cancelAllRequests_DocumentWidget(closed);
                destroy_Widget(as_Widget(closed));
            }
            postCommandf_App("tabs.switch page:%p", tabPage_Widget(tabs, 0));
            wasClosed = iTrue;
        }
        if (wasClosed) {
            arrange_Widget(tabs);
            return iTrue;
        }
        const iBool isSplit = numRoots_Window(get_Window()) > 1;
        if (tabCount_Widget(tabs) > 1 || isSplit) {
            if (index != iInvalidPos) {
                iAssert(doc);
                iDocumentWidget *closed = (iDocumentWidget *) removeTabPage_Widget(tabs, index);
                iAssert(closed);
                pushClosedTabUrl_App_(d, url_DocumentWidget(closed));
                cancelAllRequests_DocumentWidget(closed);
                destroy_Widget(as_Widget(closed)); /* released later */
            }
            if (tabCount_Widget(tabs) == 0) {
                iAssert(isSplit);
                postCommand_App("ui.split arg:0");
            }
            else {
                arrange_Widget(tabs);
                if (wasCurrent) {
                    size_t newIndex = index;
                    if (isRightmost) {
                        newIndex--;
                    }
                    else if (newIndex > 0) {
                        if (generation_DocumentWidget((iDocumentWidget *) tabPage_Widget(
                                tabs, newIndex)) != closedGeneration) {
                            newIndex--;
                        }
                    }
                    postCommandf_App("tabs.switch page:%p", tabPage_Widget(tabs, newIndex));
                }
            }
        }
#if defined (iPlatformAppleDesktop)
        else {
            closeWindow_App(d->window);
        }
#else
        else if (numWindows_App() > 1) {
            closeWindow_App(d->window);
        }
        else {
            postCommand_App("quit");
        }
#endif
        return iTrue;
    }
    else if (equal_Command(cmd, "keyroot.next") && isMainWin) {
        if (setKeyRoot_Window(as_Window(d->window),
                              otherRoot_Window(as_Window(d->window), d->window->keyRoot))) {
            setFocus_Widget(NULL);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "preferences")) {
        showPreferences_Widget(cmd);
    }
    else if (equal_Command(cmd, "navigate.home") && isMainWin) {
        /* Look for bookmarks tagged "homepage". */
        const iPtrArray *homepages =
            list_Bookmarks(d->bookmarks, NULL, filterHomepage_Bookmark, NULL);
        if (isEmpty_PtrArray(homepages)) {
            postCommand_Root(get_Root(), "open url:about:lagrange");
        }
        else {
            iStringSet *urls = iClob(new_StringSet());
            iConstForEach(PtrArray, i, homepages) {
                const iBookmark *bm = i.ptr;
                /* Try to switch to a different bookmark. */
                if (cmpStringCase_String(url_DocumentWidget(document_App()), &bm->url)) {
                    insert_StringSet(urls, &bm->url);
                }
            }
            if (!isEmpty_StringSet(urls)) {
                postCommandf_Root(get_Root(),
                    "open url:%s",
                    cstr_String(constAt_StringSet(urls, iRandoms(0, size_StringSet(urls)))));
            }
        }
        if (argLabel_Command(cmd, "focus")) {
            postCommand_Root(get_Root(), "navigate.focus");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "bookmark.add") && isMainWin) {
        if (findWidget_Root("bmed.create")) {
            return iTrue;
        }
        iDocumentWidget *doc = document_App();
        const iString *url;
        const iString *title;
        const iBlock *ident = isIdentityPinned_DocumentWidget(doc) ?
            &identity_DocumentWidget(doc)->fingerprint : NULL;
        iChar icon = siteIcon_GmDocument(document_DocumentWidget(doc));
        if (suffixPtr_Command(cmd, "url")) {
            url          = collect_String(suffix_Command(cmd, "url"));
            iString *str = newRange_String(range_Command(cmd, "title"));
            replace_String(str, "%20", " ");
            title = collect_String(str);
        }
        else {
            url   = url_DocumentWidget(doc);
            title = bookmarkTitle_DocumentWidget(doc);
        }
        if (hasLabel_Command(cmd, "arg")) {
            /* This is triggered via the bookmark button context menu. Just add the bookmark
               with the default values. */
            const uint32_t bmId = add_Bookmarks(bookmarks_App(), url, title, NULL, icon);
            get_Bookmarks(bookmarks_App(), bmId)->parentId = arg_Command(cmd);
            notify_App("bookmarks.changed");
            return iTrue;
        }
        const uint32_t existing = findUrlIdent_Bookmarks(
            bookmarks_App(), url, ident ? collect_String(hexEncode_Block(ident)) : NULL);
        if (existing) {
            /* Editing bookmarks is a sidebar command. */
            postCommand_Widget(findWidget_App("sidebar"), "bookmark.edit id:%u", existing);
            return iTrue;
        }
        makeBookmarkCreation_Widget(url, title, icon);
        if (deviceType_App() == desktop_AppDeviceType) {
            postCommand_App("focus.set id:bmed.title");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "bookmark.setfolder")) {
        const uint32_t bmId = argLabel_Command(cmd, "bmid");
        const uint32_t destFolder = arg_Command(cmd);
        get_Bookmarks(bookmarks_App(), bmId)->parentId = destFolder;
        notify_App("bookmarks.changed");
        return iTrue;
    }
    else if (equal_Command(cmd, "feeds.subscribe") && isMainWin) {
        const iString *url = url_DocumentWidget(document_App());
        if (isEmpty_String(url)) {
            return iTrue;
        }
        makeFeedSettings_Widget(findUrl_Bookmarks(d->bookmarks, url));
        return iTrue;
    }
    else if (equal_Command(cmd, "bookmarks.addfolder")) {
        const int parentId = argLabel_Command(cmd, "parent");
        if (suffixPtr_Command(cmd, "value")) {
            uint32_t id = add_Bookmarks(d->bookmarks, NULL,
                                        collect_String(suffix_Command(cmd, "value")), NULL, 0);
            if (parentId) {
                get_Bookmarks(d->bookmarks, id)->parentId = parentId;
            }
            notifyf_App("bookmarks.changed added:%zu", id);
            setRecentFolder_Bookmarks(d->bookmarks, id);
        }
        else {
            iWidget *dlg = makeValueInput_Widget(
                get_Root()->widget, collectNewCStr_String(cstr_Lang("dlg.addfolder.defaulttitle")),
                uiHeading_ColorEscape "${heading.addfolder}", "${dlg.addfolder.prompt}",
                uiTextAction_ColorEscape "${dlg.addfolder}",
                format_CStr("bookmarks.addfolder parent:%d", parentId));
            setSelectAllOnFocus_InputWidget(findChild_Widget(dlg, "input"), iTrue);
        }
        return iTrue;
    }
    else if (startsWith_Command(cmd, "feeds.update.")) {
        const iWidget *navBar = findChild_Widget(get_Window()->roots[0]->widget, "navbar");
        iAnyObject *prog = findChild_Widget(navBar, "feeds.progress");
        if (!navBar || !prog) {
            return iFalse;
        }
        if (equal_Command(cmd, "feeds.update.started") ||
            equal_Command(cmd, "feeds.update.progress")) {
            const int num   = arg_Command(cmd);
            const int total = argLabel_Command(cmd, "total");
            updateTextAndResizeWidthCStr_LabelWidget(prog,
                                                     flags_Widget(navBar) & tight_WidgetFlag ||
                                                             deviceType_App() == phone_AppDeviceType
                                                         ? star_Icon
                                                         : star_Icon " ${status.feeds}");
            showCollapsed_Widget(prog, iTrue);
            setFixedSize_Widget(findChild_Widget(prog, "feeds.progressbar"),
                                init_I2(total ? width_Widget(prog) * num / total : 0, -1));
        }
        else if (equal_Command(cmd, "feeds.update.finished")) {
            showCollapsed_Widget(prog, iFalse);
            refreshFinished_Feeds();
            refresh_Widget(findWidget_App("url"));
            return iFalse;
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "document.changed")) {
        /* Set of open tabs has changed. */
        notify_App("document.openurls.changed");
        if (deviceType_App() == phone_AppDeviceType) {
            showToolbar_Root(d->window->roots[0], iTrue);
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "ident.new") && isMainWin) {
        iWidget *dlg = makeIdentityCreation_Widget();
        setTextCStr_InputWidget(findChild_Widget(dlg, "ident.until"), "9999-12-31 23:59:59");
        setFocus_Widget(findChild_Widget(dlg, "ident.common"));
        setCommandHandler_Widget(dlg, handleIdentityCreationCommands_);
        iLabelWidget *scope = findChild_Widget(dlg, "ident.scope");
        if (argLabel_Command(cmd, "scope")) {
            updateDropdownSelection_LabelWidget(
                scope, format_CStr("arg:%d", argLabel_Command(cmd, "scope")));
        }
        updateSize_LabelWidget(scope);
        arrange_Widget(dlg);
        return iTrue;
    }
    else if (equal_Command(cmd, "ident.import") && isMainWin) {
        iCertImportWidget *imp = new_CertImportWidget();
        setPageContent_CertImportWidget(imp, sourceContent_DocumentWidget(document_App()));
        addChild_Widget(get_Root()->widget, iClob(imp));
        arrange_Widget(as_Widget(imp));
        setupSheetTransition_Mobile(as_Widget(imp), incoming_TransitionFlag |
                                    dialogTransitionDir_Widget(as_Widget(imp)));
        postRefresh_Window(get_Window());
        return iTrue;
    }
    else if (equal_Command(cmd, "ident.switch")) {
        /* This is different than "ident.signin" in that the currently used identity's activation
           URL is used instead of the current one. */
        const iString     *docUrl = url_DocumentWidget(document_App());
        const iGmIdentity *cur    = identity_DocumentWidget(document_App());
        iGmIdentity       *dst    = findIdentity_GmCerts(
            d->certs, collect_Block(hexDecode_Rangecc(range_Command(cmd, "fp"))));
        if (dst && cur != dst) {
            iString *useUrl = copy_String(findUse_GmIdentity(cur, docUrl));
            if (isEmpty_String(useUrl)) {
                useUrl = copy_String(docUrl);
            }
            setIdentity_DocumentWidget(document_App(), NULL); /* no longer overridden */
            signIn_GmCerts(d->certs, dst, useUrl);
            postCommand_App("idents.changed");
            postCommand_App("navigate.reload");
            delete_String(useUrl);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "fontpack.delete")) {
        const iString *packId = collect_String(suffix_Command(cmd, "id"));
        if (isEmpty_String(packId)) {
            return iTrue;
        }
        const iFontPack *pack = pack_Fonts(cstr_String(packId));
        if (pack && loadPath_FontPack(pack)) {
            if (argLabel_Command(cmd, "confirmed")) {
                remove_StringSet(d->prefs.disabledFontPacks, packId);
                removePath_CStr(cstr_String(loadPath_FontPack(pack)));
                reload_Fonts();
                postCommand_App("navigate.reload");
            }
            else {
                makeQuestion_Widget(
                    uiTextCaution_ColorEscape "${heading.fontpack.delete}",
                    format_Lang("${dlg.fontpack.delete.confirm}",
                                cstr_String(packId)),
                    (iMenuItem[]){ { "${cancel}" },
                                   { uiTextAction_ColorEscape " ${dlg.fontpack.delete}",
                                     0,
                                     0,
                                     format_CStr("!fontpack.delete confirmed:1 id:%s",
                                                 cstr_String(packId)) } },
                    2);
            }
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "export")) {
        iExport *export = new_Export();
        iBuffer *zip    = new_Buffer();
        generate_Export(export);
        openEmpty_Buffer(zip);
        serialize_Archive(archive_Export(export), stream_Buffer(zip));
        iDocumentWidget *expTab = newTab_App(NULL, switchTo_NewTabFlag | reuseBlank_NewTabFlag);
        iDate now;
        initCurrent_Date(&now);
        setUrlAndSource_DocumentWidget(
            expTab,
            collect_String(format_Date(&now, "file:Lagrange User Data %Y-%m-%d %H%M%S.zip")),
            collectNewCStr_String("application/zip"),
            data_Buffer(zip),
            0);
        iRelease(zip);
        delete_Export(export);
#if defined (iPlatformAppleMobile) || defined (iPlatformAndroidMobile)
        /* Straight to the save sheet. */
        postCommand_App("document.save");
#endif
        return iTrue;
    }
    else if (equal_Command(cmd, "import")) {
        const iString *path = collect_String(suffix_Command(cmd, "path"));
        iArchive *zip = iClob(new_Archive());
        if (openFile_Archive(zip, path)) {
            if (!arg_Command(cmd)) {
                makeUserDataImporter_Widget(path);
                return iTrue;
            }
            const int bookmarks = argLabel_Command(cmd, "bookmarks");
            const int trusted   = argLabel_Command(cmd, "trusted");
            const int idents    = argLabel_Command(cmd, "idents");
            const int visited   = argLabel_Command(cmd, "visited");
            const int siteSpec  = argLabel_Command(cmd, "sitespec");
            const int snippets  = argLabel_Command(cmd, "snippets");
            iExport *export = new_Export();
            if (load_Export(export, zip)) {
                import_Export(export, bookmarks, idents, trusted, visited, siteSpec, snippets);
            }
            else {
                makeSimpleMessage_Widget(uiHeading_ColorEscape "${heading.import.userdata.error}",
                                         format_Lang("${import.userdata.error}", cstr_String(path)));
            }
            delete_Export(export);
        }
        else {
            makeSimpleMessage_Widget(uiHeading_ColorEscape "${heading.import.userdata.error}",
                                     format_Lang("${import.userdata.error}", cstr_String(path)));
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "snippet.add")) {
        iWidget *dlg = makeSnippetCreation_Widget();
        if (hasLabel_Command(cmd, "content")) {
            setTextCStr_InputWidget(findChild_Widget(dlg, "snip.content"),
                                    suffixPtr_Command(cmd, "content"));
        }
        return iTrue;
    }
    else {
        return iFalse;
    }
    return iTrue;
}
