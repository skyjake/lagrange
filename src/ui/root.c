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

#include "root.h"

#include "app.h"
#include "bookmarks.h"
#include "command.h"
#include "defs.h"
#include "documentwidget.h"
#include "resources.h"
#include "inputwidget.h"
#include "keys.h"
#include "labelwidget.h"
#include "lookupwidget.h"
#include "sidebarwidget.h"
#include "window.h"
#include "../visited.h"
#include "../history.h"
#include "../gmcerts.h"
#include "../gmutil.h"
#include "../sitespec.h"
#include "../visited.h"

#if defined (iPlatformMsys)
#   include "../win32.h"
#endif
#if defined (iPlatformAppleDesktop)
#   include "macos.h"
#endif
#if defined (iPlatformAppleMobile)
#   include "ios.h"
#endif

#include <SDL_timer.h>

static const iMenuItem desktopNavMenuItems_[] = {
    { openWindow_Icon " ${menu.newwindow}", SDLK_n, KMOD_PRIMARY, "window.new" },
    { add_Icon " ${menu.newtab}", SDLK_t, KMOD_PRIMARY, "tabs.new append:1" },
    { close_Icon " ${menu.closetab}", SDLK_w, KMOD_PRIMARY, "tabs.close" },
    { "${menu.openlocation}", SDLK_l, KMOD_PRIMARY, "navigate.focus" },
    { "---" },
    { download_Icon " " saveToDownloads_Label, SDLK_s, KMOD_PRIMARY, "document.save" },
    { "${menu.page.copysource}", SDLK_c, KMOD_PRIMARY, "copy" },
    { "---" },
    { leftHalf_Icon " ${menu.sidebar.left}", leftSidebar_KeyShortcut, "sidebar.toggle" },
    { rightHalf_Icon " ${menu.sidebar.right}", rightSidebar_KeyShortcut, "sidebar2.toggle" },
    { "${menu.view.split}", SDLK_j, KMOD_PRIMARY, "splitmenu.open" },
    { "${menu.zoom.in}", SDLK_EQUALS, KMOD_PRIMARY, "zoom.delta arg:10" },
    { "${menu.zoom.out}", SDLK_MINUS, KMOD_PRIMARY, "zoom.delta arg:-10" },
    { "${menu.zoom.reset}", SDLK_0, KMOD_PRIMARY, "zoom.set arg:100" },
    { "---" },
    { "${menu.feeds.entrylist}", 0, 0, "!open url:about:feeds" },
    { "${menu.downloads}", 0, 0, "downloads.open" },
    { export_Icon " ${menu.export}", 0, 0, "export" },
    { "---" },
    { gear_Icon " ${menu.preferences}", preferences_KeyShortcut, "preferences" },
 #if defined (LAGRANGE_ENABLE_WINSPARKLE)
    { "${menu.update}", 0, 0, "updater.check" },
 #endif
    { "${menu.help}", SDLK_F1, 0, "!open url:about:help" },
    { "${menu.releasenotes}", 0, 0, "!open url:about:version" },
    { "---" },
    { "${menu.quit}", 'q', KMOD_PRIMARY, "quit" },
    { NULL }
};

static const iMenuItem tabletNavMenuItems_[] = {
    { add_Icon " ${menu.newtab}", SDLK_t, KMOD_PRIMARY, "tabs.new append:1" },
    { folder_Icon " ${menu.openfile}", SDLK_o, KMOD_PRIMARY, "file.open" },
    { "---" },
    { close_Icon " ${menu.closetab}", 'w', KMOD_PRIMARY, "tabs.close" },
    { "${menu.closetab.other}", 0, 0, "tabs.close toleft:1 toright:1" },
    { "${menu.reopentab}", SDLK_t, KMOD_SECONDARY, "tabs.new reopen:1" },
    { "---" },
    { magnifyingGlass_Icon " ${menu.find}", 0, 0, "focus.set id:find.input" },
    { leftHalf_Icon " ${menu.sidebar.left}", leftSidebar_KeyShortcut, "sidebar.toggle" },
    { rightHalf_Icon " ${menu.sidebar.right}", rightSidebar_KeyShortcut, "sidebar2.toggle" },
    { "${menu.view.split}", SDLK_j, KMOD_PRIMARY, "splitmenu.open" },
    { "---" },
    { gear_Icon " ${menu.settings}", preferences_KeyShortcut, "preferences" },
    { NULL }
};

static const iMenuItem phoneNavMenuItems_[] = {
    { add_Icon " ${menu.newtab}", SDLK_t, KMOD_PRIMARY, "tabs.new append:1" },
    { folder_Icon " ${menu.openfile}", SDLK_o, KMOD_PRIMARY, "file.open" },
    { "---" },
    { close_Icon " ${menu.closetab}", 'w', KMOD_PRIMARY, "tabs.close" },
    { "${menu.closetab.other}", 0, 0, "tabs.close toleft:1 toright:1" },
    { "${menu.reopentab}", SDLK_t, KMOD_SECONDARY, "tabs.new reopen:1" },
    { "---" },
    { magnifyingGlass_Icon " ${menu.find}", 0, 0, "focus.set id:find.input" },
    { "---" },
    { gear_Icon " ${menu.settings}", preferences_KeyShortcut, "preferences" },
    { NULL }
};

#if 0
#if defined (iPlatformMobile)
static const iMenuItem identityButtonMenuItems_[] = {
    { "${menu.identity.notactive}", 0, 0, "ident.showactive" },
    { "---" },
    { add_Icon " ${menu.identity.new}", newIdentity_KeyShortcut, "ident.new" },
    { "${menu.identity.import}", SDLK_m, KMOD_SECONDARY, "ident.import" },
    { "---" },
    { person_Icon " ${menu.show.identities}", 0, 0, "toolbar.showident" },
};
#else /* desktop */
static const iMenuItem identityButtonMenuItems_[] = {
    { "${menu.identity.notactive}", 0, 0, "ident.showactive" },
    { "---" },
# if !defined (iPlatformAppleDesktop)
    { add_Icon " ${menu.identity.newdomain}", newIdentity_KeyShortcut, "ident.new" },
    { "${menu.identity.import}", SDLK_m, KMOD_SECONDARY, "ident.import" },
    { "---" },
    { person_Icon " ${menu.show.identities}", '4', KMOD_PRIMARY, "sidebar.mode arg:3 toggle:1" },
# else
    { add_Icon " ${menu.identity.newdomain}", 0, 0, "ident.new" },
    { "---" },
    { person_Icon " ${menu.show.identities}", 0, 0, "sidebar.mode arg:3 toggle:1" },
# endif
};
#endif
#endif

static const char *reloadCStr_   = reload_Icon;
static const char *pageMenuCStr_ = midEllipsis_Icon;

/* TODO: A preference for these, maybe? */
static const char *stopSeqCStr_[] = {
    /* Corners */
    uiTextAction_ColorEscape "\U0000231c",
    uiTextAction_ColorEscape "\U0000231d",
    uiTextAction_ColorEscape "\U0000231f",
    uiTextAction_ColorEscape "\U0000231e",
#if 0
    /* Rotating arrow */
    uiTextCaution_ColorEscape "\U00002b62",
    uiTextCaution_ColorEscape "\U00002b68",
    uiTextCaution_ColorEscape "\U00002b63",
    uiTextCaution_ColorEscape "\U00002b69",
    uiTextCaution_ColorEscape "\U00002b60",
    uiTextCaution_ColorEscape "\U00002b66",
    uiTextCaution_ColorEscape "\U00002b61",
    uiTextCaution_ColorEscape "\U00002b67",
#endif
#if 0
    /* Star */
    uiTextCaution_ColorEscape "\u2bcc",
    uiTextCaution_ColorEscape "\u2bcd",
    uiTextCaution_ColorEscape "\u2bcc",
    uiTextCaution_ColorEscape "\u2bcd",
    uiTextCaution_ColorEscape "\u2bcc",
    uiTextCaution_ColorEscape "\u2bcd",
    uiTextCaution_ColorEscape "\u2bce",
    uiTextCaution_ColorEscape "\u2bcf",
    uiTextCaution_ColorEscape "\u2bce",
    uiTextCaution_ColorEscape "\u2bcf",
    uiTextCaution_ColorEscape "\u2bce",
    uiTextCaution_ColorEscape "\u2bcf",
#endif
#if 0
    /* Pulsing circle */
    uiTextCaution_ColorEscape "\U0001f785",
    uiTextCaution_ColorEscape "\U0001f786",
    uiTextCaution_ColorEscape "\U0001f787",
    uiTextCaution_ColorEscape "\U0001f788",
    uiTextCaution_ColorEscape "\U0001f789",
    uiTextCaution_ColorEscape "\U0001f789",
    uiTextCaution_ColorEscape "\U0001f788",
    uiTextCaution_ColorEscape "\U0001f787",
    uiTextCaution_ColorEscape "\U0001f786",
#endif
#if 0
    /* Dancing dots */
    uiTextCaution_ColorEscape "\U0001fb00",
    uiTextCaution_ColorEscape "\U0001fb01",
    uiTextCaution_ColorEscape "\U0001fb07",
    uiTextCaution_ColorEscape "\U0001fb1e",
    uiTextCaution_ColorEscape "\U0001fb0f",
    uiTextCaution_ColorEscape "\U0001fb03",
    uiTextCaution_ColorEscape "\U0001fb00",
    uiTextCaution_ColorEscape "\U0001fb01",
    uiTextCaution_ColorEscape "\U0001fb07",
    uiTextCaution_ColorEscape "\U0001fb1e",
    uiTextCaution_ColorEscape "\U0001fb0f",
    uiTextCaution_ColorEscape "\U0001fb03",

    uiTextCaution_ColorEscape "\U0001fb7d",
    uiTextCaution_ColorEscape "\U0001fb7e",
    uiTextCaution_ColorEscape "\U0001fb7f",
    uiTextCaution_ColorEscape "\U0001fb7c",
    uiTextCaution_ColorEscape "\U0001fb7d",
    uiTextCaution_ColorEscape "\U0001fb7e",
    uiTextCaution_ColorEscape "\U0001fb7f",
    uiTextCaution_ColorEscape "\U0001fb7c",

    uiTextCaution_ColorEscape "\U0001fb00",
    uiTextCaution_ColorEscape "\U0001fb01",
    uiTextCaution_ColorEscape "\U0001fb07",
    uiTextCaution_ColorEscape "\U0001fb03",
    uiTextCaution_ColorEscape "\U0001fb0f",
    uiTextCaution_ColorEscape "\U0001fb1e",
    uiTextCaution_ColorEscape "\U0001fb07",
    uiTextCaution_ColorEscape "\U0001fb03",
#endif
};

static const int loadAnimIntervalMs_ = 133;
static int       loadAnimIndex_      = 0;
static iRoot *   activeRoot_         = NULL;

static void     setupMovableElements_Root_  (iRoot *);
static void     updateNavBarSize_           (iWidget *navBar);
static void     updateBottomBarPosition_    (iWidget *bottomBar, iBool animate);

iDefineTypeConstruction(Root)
iDefineAudienceGetter(Root, arrangementChanged)
iDefineAudienceGetter(Root, visualOffsetsChanged)

void init_Root(iRoot *d) {
    iZap(*d);
    init_String(&d->tabInsertId);
}

void deinit_Root(iRoot *d) {
    iReleasePtr(&d->widget);
    delete_PtrArray(d->onTop);
    delete_PtrSet(d->pendingDestruction);
    delete_Audience(d->visualOffsetsChanged);
    delete_Audience(d->arrangementChanged);
    deinit_String(&d->tabInsertId);
    if (d->loadAnimTimer) {
        SDL_RemoveTimer(d->loadAnimTimer);
        d->loadAnimTimer = 0;
    }
}

void setCurrent_Root(iRoot *root) {
    activeRoot_ = root;
}

iRoot *current_Root(void) {
    return activeRoot_;
}

iRoot *get_Root(void) {
    iAssert(activeRoot_);
    return activeRoot_;
}

iAnyObject *findWidget_Root(const char *id) {
    if (activeRoot_) {
        return findChild_Widget(activeRoot_->widget, id);
    }
    return NULL;
}

iDocumentWidget *findDocument_Root(const iRoot *d, const iString *url) {
    iForEach(ObjectList, i, iClob(listDocuments_App(d))) {
        if (equalCase_String(url, url_DocumentWidget(i.object))) {
            return i.object;
        }
    }
    return NULL;
}

void destroyPending_Root(iRoot *d) {
    iRoot *oldRoot = current_Root();
    setCurrent_Root(d);
    iForEach(PtrSet, i, d->pendingDestruction) {
        iWidget *widget = *i.value;
        iAssert(widget->root == d);
        if (!isFinished_Anim(&widget->visualOffset) ||
            isBeingVisuallyOffsetByReference_Widget(widget)) {
            continue;
        }
        if (widget->flags & keepOnTop_WidgetFlag) {
            removeOne_PtrArray(d->onTop, widget);
            widget->flags &= ~keepOnTop_WidgetFlag;
        }
        iAssert(indexOf_PtrArray(d->onTop, widget) == iInvalidPos);
        if (widget->parent) {
            removeChild_Widget(widget->parent, widget);
        }
        iAssert(widget->parent == NULL);
        iRelease(widget);
        remove_PtrSetIterator(&i);
    }
#if 0
    printf("Root %p onTop (%zu):\n", d, size_PtrArray(d->onTop));
    iConstForEach(PtrArray, t, d->onTop) {
        const iWidget *p = *t.value;
        printf(" - %p {%s}\n", p, cstr_String(id_Widget(p)));
    }
#endif
    setCurrent_Root(oldRoot);
}

iPtrArray *onTop_Root(iRoot *d) {
    if (!d->onTop) {
        d->onTop = new_PtrArray();
    }
    return d->onTop;
}

static iWidget *makeIdentityMenu_(iWidget *parent) {
    iArray items;
    init_Array(&items, sizeof(iMenuItem));
    /* Current identity. */
    const iDocumentWidget *doc        = document_App();
    const iString         *docUrl     = url_DocumentWidget(doc);
    const iGmIdentity     *ident      = identity_DocumentWidget(doc);
    const iBool            isSetIdent = isIdentityPinned_DocumentWidget(doc);
    const iString *fp  = ident ? collect_String(hexEncode_Block(&ident->fingerprint)) : NULL;
    iString       *str = NULL;
    if (ident) {
        str = copy_String(name_GmIdentity(ident));
        if (!isEmpty_String(&ident->notes)) {
            appendFormat_String(str, "\n\x1b[0m" uiHeading_ColorEscape "%s", cstr_String(&ident->notes));
        }
    }
    pushBack_Array(
        &items,
        &(iMenuItem){ format_CStr("```" uiHeading_ColorEscape "\x1b[1m%s",
                                  str ? cstr_String(str) : "${menu.identity.notactive}") });
    if (isSetIdent) {
        pushBack_Array(&items,
                       &(iMenuItem){ close_Icon " ${ident.unset}",
                                     0, 0, "document.unsetident" });
    }
    else if (ident && isUsedOn_GmIdentity(ident, docUrl)) {
        pushBack_Array(&items,
                       &(iMenuItem){ close_Icon " ${ident.stopuse}",
                                     0,
                                     0,
                                     format_CStr("ident.signout ident:%s url:%s",
                                                 cstr_String(fp),
                                                 cstr_String(docUrl)) });
    }
    pushBack_Array(&items, &(iMenuItem){ "---" });
    delete_String(str);
    /* Alternate identities. */
    const iString *site = collectNewRange_String(urlRoot_String(docUrl));
    iBool haveAlts = iFalse;
    iConstForEach(StringArray, i, strings_SiteSpec(site, usedIdentities_SiteSpecKey)) {
        if (!fp || !equal_String(i.value, fp)) {
            const iBlock *otherFp = collect_Block(hexDecode_Rangecc(range_String(i.value)));
            const iGmIdentity *other = findIdentity_GmCerts(certs_App(), otherFp);
            if (other && other != ident) {
                pushBack_Array(
                    &items,
                    &(iMenuItem){
                        format_CStr(translateCStr_Lang("\U0001f816 ${ident.switch}"),
                                    format_CStr("\x1b[1m%s",
                                                cstr_String(name_GmIdentity(other)))),
                        0,
                        0,
                        format_CStr("ident.switch fp:%s", cstr_String(i.value)) });
                haveAlts = iTrue;
            }
        }
    }
    if (haveAlts) {
        pushBack_Array(&items, &(iMenuItem){ "---" });
    }
    iSidebarWidget *sidebar = findWidget_App("sidebar");
    const iBool isGemini = equalCase_Rangecc(urlScheme_String(docUrl), "gemini");
    pushBackN_Array(
        &items,
        (iMenuItem[]){
            { isGemini ? add_Icon " ${menu.identity.newdomain}"
                       : add_Icon " ${menu.identity.new}",
              0, 0,
              isGemini ? "ident.new scope:1"
                       : "ident.new" },
            { "${menu.identity.import}", SDLK_m, KMOD_SECONDARY, "ident.import" },
            { "---" } }, 3);
    if (deviceType_App() == desktop_AppDeviceType) {
        pushBack_Array(&items,
                       &(iMenuItem){ isVisible_Widget(sidebar) && mode_SidebarWidget(sidebar) ==
                                                                      identities_SidebarMode
                                         ? leftHalf_Icon " ${menu.hide.identities}"
                                         : leftHalf_Icon " ${menu.show.identities}",
                                     0,
                                     0,
                                     "sidebar.mode arg:3 toggle:1" });
    }
    else {
        pushBack_Array(&items, &(iMenuItem){ gear_Icon " ${menu.identities}", 0, 0,
                                             "toolbar.showident"});
    }
    iWidget *menu = makeMenu_Widget(parent, constData_Array(&items), size_Array(&items));
    deinit_Array(&items);
    return menu;
}

iBool handleRootCommands_Widget(iWidget *root, const char *cmd) {
    iUnused(root);
    if (equal_Command(cmd, "menu.open")) {
        iWidget *button = pointer_Command(cmd);
        iWidget *menu = findChild_Widget(button, "menu");
        if (!menu) {
            /* Independent popup window. */
            postCommand_App("cancel");
            return iTrue;
        }
        const iBool isPlacedUnder = argLabel_Command(cmd, "under");
        const iBool isMenuBar = argLabel_Command(cmd, "bar");
        iAssert(menu);
        if (!isVisible_Widget(menu)) {
            if (isMenuBar) {
                setFlags_Widget(button, selected_WidgetFlag, iTrue);
            }
            openMenu_Widget(menu,
                            isPlacedUnder ? bottomLeft_Rect(bounds_Widget(button))
                                          : topLeft_Rect(bounds_Widget(button)));
        }
        else {
            /* Already open, do nothing. */
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "splitmenu.open")) {
        setFocus_Widget(NULL);
        iWidget *menu = findWidget_Root("splitmenu");
        openMenuFlags_Widget(menu, zero_I2(), postCommands_MenuOpenFlags | center_MenuOpenFlags);
        return iTrue;
    }
    else if (deviceType_App() == tablet_AppDeviceType && equal_Command(cmd, "toolbar.showident")) {
        /* No toolbar on tablet, so we handle this command here. */
        postCommand_App("preferences idents:1");
        return iTrue;
    }
    else if (equal_Command(cmd, "identmenu.open")) {
        const iBool setFocus = argLabel_Command(cmd, "focus");
        iWidget *toolBar = findWidget_Root("toolbar");
        iWidget *button = findWidget_Root(toolBar && isPortraitPhone_App() ? "toolbar.ident" : "navbar.ident");
        if (button) {
            iWidget *menu = makeIdentityMenu_(button);
            openMenuFlags_Widget(menu, bottomLeft_Rect(bounds_Widget(button)),
                                 postCommands_MenuOpenFlags | (setFocus ? setFocus_MenuOpenFlags : 0));
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "contextclick")) {
        iBool showBarMenu = iFalse;
        if (equal_Rangecc(range_Command(cmd, "id"), "buttons")) {
            const iWidget *sidebar = findWidget_App("sidebar");
            const iWidget *sidebar2 = findWidget_App("sidebar2");
            const iWidget *buttons = pointer_Command(cmd);
            if (hasParent_Widget(buttons, sidebar) ||
                hasParent_Widget(buttons, sidebar2)) {
                showBarMenu = iTrue;
            }
        }
        if (equal_Rangecc(range_Command(cmd, "id"), "navbar")) {
            showBarMenu = iTrue;
        }
        if (showBarMenu) {
            openMenu_Widget(findWidget_App("barmenu"), coord_Command(cmd));
            return iTrue;
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "focus.set")) {
        setFocus_Widget(findWidget_App(cstr_Command(cmd, "id")));
        return iTrue;
    }
    else if (equal_Command(cmd, "menubar.focus")) {
        iWidget *menubar = findWidget_App("menubar");
        if (menubar) {
            setFocus_Widget(child_Widget(menubar, 0));
            postCommand_Widget(focus_Widget(), "trigger");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "input.resized")) {
        /* No parent handled this, so do a full rearrangement. */
        /* TODO: Defer this and do a single rearrangement later. */
        arrange_Widget(root);
        postRefresh_App();
        return iTrue;
    }
    else if (equal_Command(cmd, "window.activate")) {
        iWindow *window = pointer_Command(cmd);
        SDL_RestoreWindow(window->win);
        SDL_RaiseWindow(window->win);
        SDL_SetWindowInputFocus(window->win);
        return iTrue;
    }
    else if (equal_Command(cmd, "window.focus.lost")) {
        setTextColor_LabelWidget(findWidget_App("winbar.app"), uiAnnotation_ColorId);
        setTextColor_LabelWidget(findWidget_App("winbar.title"), uiAnnotation_ColorId);
        return iFalse;
    }
    else if (equal_Command(cmd, "window.focus.gained")) {
        setTextColor_LabelWidget(findWidget_App("winbar.app"), uiTextAppTitle_ColorId);
        setTextColor_LabelWidget(findWidget_App("winbar.title"), uiTextStrong_ColorId);
        return iFalse;
    }
    else if (equal_Command(cmd, "window.setrect")) {
        if (hasLabel_Command(cmd, "index") &&
            argU32Label_Command(cmd, "index") != windowIndex_Root(root->root)) {
            return iFalse;
        }
        const int snap = argLabel_Command(cmd, "snap");
        if (snap) {
            iMainWindow *window = get_MainWindow();
            iInt2 coord = coord_Command(cmd);
            iInt2 size = init_I2(argLabel_Command(cmd, "width"),
                                 argLabel_Command(cmd, "height"));
            if (snap_MainWindow(window) != maximized_WindowSnap) {
                SDL_SetWindowPosition(window->base.win, coord.x, coord.y);
                SDL_SetWindowSize(window->base.win, size.x, size.y);
            }
            setSnap_MainWindow(get_MainWindow(), snap);
            return iTrue;
        }
    }
    else if (equal_Command(cmd, "window.restore")) {
        setSnap_MainWindow(get_MainWindow(), none_WindowSnap);
        return iTrue;
    }
    else if (equal_Command(cmd, "window.minimize")) {
        SDL_MinimizeWindow(get_Window()->win);
        return iTrue;
    }
    else if (equal_Command(cmd, "window.close")) {
        if (!isAppleDesktop_Platform() && size_PtrArray(mainWindows_App()) == 1) {
            SDL_PushEvent(&(SDL_Event){ .type = SDL_QUIT });
        }
        else {
            closeWindow_App(get_Window());
        }
        return iTrue;
    }
    else if (deviceType_App() == tablet_AppDeviceType && equal_Command(cmd, "window.resized")) {
        iSidebarWidget *sidebar = findChild_Widget(root, "sidebar");
        iSidebarWidget *sidebar2 = findChild_Widget(root, "sidebar2");
        setWidth_SidebarWidget(sidebar, 73.0f);
        setWidth_SidebarWidget(sidebar2, 73.0f);
        return iFalse;
    }
    else if (deviceType_App() == phone_AppDeviceType && equal_Command(cmd, "window.resized")) {
        /* Place the sidebar next to or under doctabs depending on orientation. */
        iSidebarWidget *sidebar = findChild_Widget(root, "sidebar");
        removeChild_Widget(parent_Widget(sidebar), sidebar);
        iChangeFlags(as_Widget(sidebar)->flags2, fadeBackground_WidgetFlag2, isPortrait_App());
        if (isLandscape_App()) {
            setVisualOffset_Widget(as_Widget(sidebar), 0, 0, 0);
            addChildPos_Widget(findChild_Widget(root, "tabs.content"), iClob(sidebar), front_WidgetAddPos);            
            setWidth_SidebarWidget(sidebar, 73.0f);
            setFlags_Widget(as_Widget(sidebar), fixedHeight_WidgetFlag | fixedPosition_WidgetFlag, iFalse);
        }
        else {
            addChild_Widget(root, iClob(sidebar));
            setWidth_SidebarWidget(sidebar, (float) width_Widget(root) / (float) gap_UI);
            int midHeight = height_Widget(root) / 2;// + lineHeight_Text(uiLabelLarge_FontId);
#if defined (iPlatformAndroidMobile)
            midHeight += 2 * lineHeight_Text(uiLabelLarge_FontId);
#endif
            setMidHeight_SidebarWidget(sidebar, midHeight);
            setFixedSize_Widget(as_Widget(sidebar), init_I2(-1, midHeight));
            setPos_Widget(as_Widget(sidebar), init_I2(0, height_Widget(root) - midHeight));
        }
        postCommandf_Root(root->root, "toolbar.show arg:%d", isPortrait_App() || prefs_App()->bottomNavBar);
        return iFalse;
    }
    else if (equal_Command(cmd, "root.arrange")) {
        iWidget *prefs = findWidget_Root("prefs");
        if (prefs) {
            updatePreferencesLayout_Widget(prefs);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "root.refresh")) {
        refresh_Widget(get_Root()->widget);
        return iTrue;
    }
    else if (equal_Command(cmd, "root.movable")) {
        setupMovableElements_Root_(root->root);
        arrange_Widget(root);
        iWidget *bottomBar = findChild_Widget(root, "bottombar");
        if (bottomBar) {
            /* Update bottom bar height and position. */
            updateBottomBarPosition_(bottomBar, iFalse);
            updateToolbarColors_Root(root->root);
        }
        return iFalse; /* all roots must handle this */
    }
    else if (equal_Command(cmd, "theme.changed")) {
        /* The phone toolbar is draw-buffered so it needs refreshing. */
        refresh_Widget(findWidget_App("toolbar"));
        return iFalse;
    }
    else if (handleCommand_App(cmd)) {
        return iTrue;
    }
    return iFalse;
}

static void updateNavBarIdentity_(iWidget *navBar) {
    iDocumentWidget *doc = document_App();
    const iGmIdentity *ident = identity_DocumentWidget(doc);
    /* Update menu. */
    const iString *subjectName = ident ? name_GmIdentity(ident) : NULL;
    if (navBar) {
        iWidget *button = findChild_Widget(navBar, "navbar.ident");
        iWidget *menu   = findChild_Widget(button, "menu");
        setFlags_Widget(button, selected_WidgetFlag, ident != NULL);
        const char *idLabel = subjectName ? cstr_String(subjectName) : "${menu.identity.notactive}";
        setMenuItemLabelByIndex_Widget(menu, 0, idLabel);
        setMenuItemDisabledByIndex_Widget(menu, 0, !ident);
        /* Visualize an identity override. */
        setOutline_LabelWidget((iLabelWidget *) button, isIdentityPinned_DocumentWidget(doc));
        setBackgroundColor_Widget(
            button, isIdentityPinned_DocumentWidget(doc) ? uiBackground_ColorId : none_ColorId);
    }
    iLabelWidget *toolButton = findWidget_App("toolbar.ident");
    iLabelWidget *toolName   = findWidget_App("toolbar.name");
    if (toolName) {
        setOutline_LabelWidget(toolButton, ident == NULL || isIdentityPinned_DocumentWidget(doc));
        if (ident) {
            setTextColor_LabelWidget(toolButton, uiTextAction_ColorId);
            setTextColor_LabelWidget(toolName, uiTextAction_ColorId);
        }
        else {
            setTextColor_LabelWidget(
                toolButton, textColor_LabelWidget(child_Widget(parent_Widget(toolButton), 0)));
        }
        /* Fit the name in the widget. */
        if (subjectName) {
            const char *endPos;
            tryAdvanceNoWrap_Text(
                uiLabelTiny_FontId, range_String(subjectName), width_Widget(toolName), &endPos);
            updateText_LabelWidget(
                toolName,
                collectNewRange_String((iRangecc){ constBegin_String(subjectName), endPos }));
        }
        else {
            updateTextCStr_LabelWidget(toolName, "");
        }
        setFont_LabelWidget(toolButton, subjectName ? uiLabelMedium_FontId : uiLabelLarge_FontId);
        setTextOffset_LabelWidget(toolButton, init_I2(0, subjectName ? -1.5f * gap_UI : 0));
        arrange_Widget(parent_Widget(toolButton));
#if defined (iPlatformAppleMobile)
        iRelease(findChild_Widget(as_Widget(toolButton), "menu"));
        makeIdentityMenu_(as_Widget(toolButton));
#endif
    }
}

static void updateNavDirButtons_(iWidget *navBar) {
    iBeginCollect();
    const iHistory *history  = history_DocumentWidget(document_App());
    const iBool     atOldest = atOldest_History(history);
    const iBool     atNewest = atNewest_History(history);
    /* Reset button state. */
    for (size_t i = 0; i < maxNavbarActions_Prefs; i++) {
        const char *id = format_CStr("navbar.action%d", i + 1);
        setFlags_Widget(findChild_Widget(navBar, id), disabled_WidgetFlag, iFalse);
    }
    setFlags_Widget(as_Widget(findMenuItem_Widget(navBar, "navigate.back")), disabled_WidgetFlag, atOldest);
    setFlags_Widget(as_Widget(findMenuItem_Widget(navBar, "navigate.forward")), disabled_WidgetFlag, atNewest);
    iWidget *toolBar = findWidget_App("toolbar");
    if (toolBar) {
        /* Reset the state. */
        for (int i = 0; i < 2; i++) {
            const char *id = (i == 0 ? "toolbar.action1" : "toolbar.action2");            
            setFlags_Widget(findChild_Widget(toolBar, id), disabled_WidgetFlag, iFalse);
            setOutline_LabelWidget(findChild_Widget(toolBar, id), iFalse);
        }
        /* Disable certain actions. */
        iLabelWidget *back = findMenuItem_Widget(toolBar, "navigate.back");
        iLabelWidget *fwd  = findMenuItem_Widget(toolBar, "navigate.forward");
        setFlags_Widget(as_Widget(back), disabled_WidgetFlag, atOldest);
        setOutline_LabelWidget(back, atOldest);
        setFlags_Widget(as_Widget(fwd), disabled_WidgetFlag, atNewest);
        setOutline_LabelWidget(fwd, atNewest);
        refresh_Widget(toolBar);
    }
    iEndCollect();
}

static const char *loadAnimationCStr_(void) {
    return stopSeqCStr_[loadAnimIndex_ % iElemCount(stopSeqCStr_)];
}

static uint32_t updateReloadAnimation_Root_(uint32_t interval, void *root) {
    loadAnimIndex_++;
    postCommandf_App("window.reload.update root:%p", root);
    return interval;
}

static void setReloadLabel_Root_(iRoot *d, const iDocumentWidget *doc) {
    const iBool   isOngoing = isRequestOngoing_DocumentWidget(doc);
    const iBool   isAuto    = isAutoReloading_DocumentWidget(doc) && !isOngoing;
    iLabelWidget *label     = findChild_Widget(d->widget, "reload");
    updateTextCStr_LabelWidget(label, isOngoing ? loadAnimationCStr_() : reloadCStr_);
    setBackgroundColor_Widget(as_Widget(label), isAuto ? uiBackground_ColorId : none_ColorId);
    setTextColor_LabelWidget(label, isAuto ? uiTextAction_ColorId : uiText_ColorId);    
    setOutline_LabelWidget(label, isAuto);
    if (isTerminal_Platform()) {
        showCollapsed_Widget(as_Widget(label), isOngoing);
    }
}

static void checkLoadAnimation_Root_(iRoot *d) {
    const iDocumentWidget *doc       = document_Root(d);
    const iBool            isOngoing = isRequestOngoing_DocumentWidget(doc);
    if (isOngoing && !d->loadAnimTimer) {
        d->loadAnimTimer = SDL_AddTimer(loadAnimIntervalMs_, updateReloadAnimation_Root_, d);
    }
    else if (!isOngoing && d->loadAnimTimer) {
        SDL_RemoveTimer(d->loadAnimTimer);
        d->loadAnimTimer = 0;
    }
    setReloadLabel_Root_(d, doc);
}

void updatePadding_Root(iRoot *d) {
    if (!d) return;
#if defined (iPlatformAppleMobile)
    iWidget *toolBar = findChild_Widget(d->widget, "toolbar");
    float left, top, right, bottom;
    safeAreaInsets_iOS(&left, &top, &right, &bottom);
    /* Respect the safe area insets. */ {
        setPadding_Widget(findChild_Widget(d->widget, "navdiv"), left, top, right, 0);
        if (toolBar) {
            setPadding_Widget(toolBar, left, 0, right, bottom);
        }
    }
#endif
}

void updateToolbarColors_Root(iRoot *d) {
    if (!isMobile_Platform()) {
        return;
    }
    iWidget *bottomBar = findChild_Widget(d->widget, "bottombar");
    if (bottomBar) {
        iWidget *toolBar = findChild_Widget(bottomBar, "toolbar");
        const iWidget *tabs = findChild_Widget(d->widget, "doctabs");
        const size_t numPages = childCount_Widget(findChild_Widget(tabs, "tabs.pages"));
        const iBool useThemeColors = !prefs_App()->bottomNavBar &&
                                     !(prefs_App()->bottomTabBar && numPages > 1);
        const int bg = useThemeColors ? tmBannerBackground_ColorId : uiBackground_ColorId;
        setBackgroundColor_Widget(bottomBar, bg);
        iForEach(ObjectList, i, children_Widget(toolBar)) {
            setTextColor_LabelWidget(i.object, useThemeColors ? tmBannerIcon_ColorId : uiTextDim_ColorId);
            setBackgroundColor_Widget(i.object, bg); /* using noBackground, but ident has outline */
        }
        if (!useThemeColors) {
            /* Menu uses accent color. */
            setTextColor_LabelWidget(findChild_Widget(toolBar, "toolbar.navmenu"), uiTextAction_ColorId);
        }
        updateNavBarIdentity_(NULL); /* updates the identity button */
    }
}

void showOrHideNewTabButton_Root(iRoot *d) {
    iWidget *tabs = findChild_Widget(d->widget, "doctabs");
    iWidget *newTabButton = findChild_Widget(tabs, "newtab");
    iBool hide = iFalse;
    if (isPortraitPhone_App()) {
        hide = iTrue; /* no room for it */
    }
    iForIndices(i, prefs_App()->navbarActions) {
        if (prefs_App()->navbarActions[i] == newTab_ToolbarAction) {
            hide = iTrue;
            break;
        }
    }
    setFlags_Widget(newTabButton, hidden_WidgetFlag, hide);
    arrange_Widget(findChild_Widget(tabs, "tabs.buttons"));
}

void notifyVisualOffsetChange_Root(iRoot *d) {
    if (d && (d->didAnimateVisualOffsets || d->didChangeArrangement)) {
        iNotifyAudience(d, visualOffsetsChanged, RootVisualOffsetsChanged);
    }
}

void dismissPortraitPhoneSidebars_Root(iRoot *d) {
    if (deviceType_App() == phone_AppDeviceType && isPortrait_App()) {
        iWidget *sidebar = findChild_Widget(d->widget, "sidebar");
//        iWidget *sidebar2 = findChild_Widget(d->widget, "sidebar2");
        if (isVisible_Widget(sidebar)) {
            postCommand_App("sidebar.toggle");
            setVisualOffset_Widget(sidebar, height_Widget(sidebar), 250, easeIn_AnimFlag);
        }
#if 0
        if (isVisible_Widget(sidebar2)) {
            postCommand_App("sidebar2.toggle");
            setVisualOffset_Widget(sidebar2, height_Widget(sidebar2), 250, easeIn_AnimFlag);
        }
        //        setFlags_Widget(findWidget_App("toolbar.ident"), noBackground_WidgetFlag, iTrue);
        //        setFlags_Widget(findWidget_App("toolbar.view"), noBackground_WidgetFlag, iTrue);
#endif
    }
}

static iBool willPerformSearchQuery_(const iString *userInput) {
    const iString *clean = collect_String(trimmed_String(userInput));
    if (isEmpty_String(clean)) {
        return iFalse;
    }
    return !isEmpty_String(&prefs_App()->strings[searchUrl_PrefsString]) &&
           !isLikelyUrl_String(userInput);
}

static void updateUrlInputContentPadding_(iWidget *navBar) {
    iInputWidget *url = findChild_Widget(navBar, "url");
    const int lockWidth = width_Widget(findChild_Widget(navBar, "navbar.lock"));
    const int indicatorsWidth = width_Widget(findChild_Widget(navBar, "url.rightembed"));
    /* The indicators widget has a padding that covers the urlButtons area. */
    setContentPadding_InputWidget(url,
                                  lockWidth - 2 * gap_UI, // * 0.75f,
                                  indicatorsWidth);
}

static void showSearchQueryIndicator_(iBool show) {
    iWidget *navBar = findWidget_Root("navbar");
    iWidget *indicator = findWidget_App("input.indicator.search");
    updateTextCStr_LabelWidget((iLabelWidget *) indicator,
                               (deviceType_App() == phone_AppDeviceType ||
                                flags_Widget(navBar) & tight_WidgetFlag)
                                   ? "${status.query.tight} " return_Icon
                                   : "${status.query} " return_Icon);
    indicator->rect.size.x = defaultSize_LabelWidget((iLabelWidget *) indicator).x; /* don't touch height */
    showCollapsed_Widget(indicator, show);
    updateUrlInputContentPadding_(navBar);
}

static int navBarAvailableSpace_(iWidget *navBar) {
    int avail = width_Rect(innerBounds_Widget(navBar));
    iConstForEach(ObjectList, i, children_Widget(navBar)) {
        const iWidget *child = i.object;
        if (~flags_Widget(child) & expand_WidgetFlag &&
            isVisible_Widget(child) &&
            cmp_String(id_Widget(child), "url")) {
            avail -= width_Widget(child);
        }
    }
    return avail;
}

iBool isNarrow_Root(const iRoot *d) {
    return width_Rect(safeRect_Root(d)) / gap_UI <
        (isTerminal_Platform() ? 81 : deviceType_App() == tablet_AppDeviceType ? 160 : 140);
}

static void updateNavBarSize_(iWidget *navBar) {
    const iBool isPhone  = deviceType_App() == phone_AppDeviceType;
    const iBool isNarrow = !isPhone && isNarrow_Root(navBar->root);
    /* Adjust navbar padding. */ {
        int hPad   = isPortraitPhone_App() ? 0 : isPhone || isNarrow ? gap_UI / 2 : (gap_UI * 3 / 2);
        int vPad   = gap_UI * 3 / 2;
        int botPad = vPad / 2;
        int topPad = !findWidget_Root("winbar") ? gap_UI / 2 : 0;
        if (prefs_App()->bottomNavBar &&
            ((isPhone && isLandscape_App()) || deviceType_App() == tablet_AppDeviceType)) {
            botPad += bottomSafeInset_Mobile();
            hPad += leftSafeInset_Mobile();
        }
        if (!isPhone && prefs_App()->bottomNavBar) {
            topPad = vPad / 2 - vPad / 3;
        }
        setPadding_Widget(navBar, hPad, vPad / 3 + topPad, hPad, botPad);
    }
    /* Button sizing. */
    if (isNarrow ^ ((flags_Widget(navBar) & tight_WidgetFlag) != 0)) {
        setFlags_Widget(navBar, tight_WidgetFlag, isNarrow);
        showCollapsed_Widget(findChild_Widget(navBar, "navbar.action3"), !isNarrow);
        showCollapsed_Widget(findChild_Widget(navBar, "document.bookmarked"), !isNarrow);
        iObjectList *lists[] = {
            children_Widget(navBar),
            children_Widget(findChild_Widget(navBar, "url")),
            children_Widget(findChild_Widget(navBar, "url.buttons")),
        };
        iForIndices(k, lists) {
            iForEach(ObjectList, i, lists[k]) {
                iWidget *child = as_Widget(i.object);
                if (!cmp_String(id_Widget(i.object), "navbar.lock")) {
                    continue;
                }
                if (cmp_String(id_Widget(i.object), "navbar.unsplit")) {
                    setFlags_Widget(child, tight_WidgetFlag, isNarrow);
                    if (isInstance_Object(i.object, &Class_LabelWidget)) {
                        iLabelWidget *label = i.object;
                        updateSize_LabelWidget(label);
                    }
                }
            }
        }
        updateUrlInputContentPadding_(navBar);
    }
    if (isPhone) {
        static const char *buttons[] = { "navbar.action1", "navbar.action2", "navbar.action3",
                                         "navbar.action4", "navbar.ident",   "navbar.menu",
                                         "document.bookmarked" };
        iWidget *toolBar = findWidget_Root("toolbar");
//        setVisualOffset_Widget(toolBar, 0, 0, 0);
//        setFlags_Widget(toolBar, hidden_WidgetFlag, isLandscape_App());
        iForIndices(i, buttons) {
            iLabelWidget *btn = findChild_Widget(navBar, buttons[i]);
            setFlags_Widget(as_Widget(btn), hidden_WidgetFlag, isPortrait_App());
            if (isLandscape_App()) {
                /* Collapsing sets size to zero and the label doesn't know when to update
                   its own size automatically. */
                updateSize_LabelWidget(btn);
            }
        }
        showOrHideNewTabButton_Root(navBar->root);
        arrange_Widget(navBar->root->widget);
    }
    /* Resize the URL input field. */ {
        iWidget *urlBar = findChild_Widget(navBar, "url");
        urlBar->rect.size.x = iMini(navBarAvailableSpace_(navBar), 167 * gap_UI);
        arrange_Widget(navBar);
    }
    updateMetrics_Root(navBar->root); /* tight flags changed; need to resize URL bar contents */
//    refresh_Widget(navBar);
    postCommand_Widget(navBar, "layout.changed id:navbar");
}

static void updateNavBarActions_(iWidget *navBar) {
    const iPrefs *prefs = prefs_App();
    for (size_t i = 0; i < iElemCount(prefs->navbarActions); i++) {
        iBeginCollect();
        const int action = prefs->navbarActions[i];
        iLabelWidget *button =
            findChild_Widget(navBar, format_CStr("navbar.action%d", i + 1));
        if (button) {
            setFlags_Widget(as_Widget(button), disabled_WidgetFlag, iFalse);
            updateTextCStr_LabelWidget(button, toolbarActions_Mobile[action].icon);
            setCommand_LabelWidget(button, collectNewCStr_String(toolbarActions_Mobile[action].command));
        }
        iEndCollect();
    }
    showOrHideNewTabButton_Root(navBar->root);
}

static iBool handleNavBarCommands_(iWidget *navBar, const char *cmd) {
    if (equal_Command(cmd, "window.resized") || equal_Command(cmd, "metrics.changed")) {
        updateNavBarSize_(navBar);
        //arrange_Widget(root_Widget(navBar));
        //updateBottomBarPosition_(findWidget_Root("bottombar"), iFalse);
        return iFalse;
    }
    else if (equal_Command(cmd, "window.reload.update")) {
        if (pointerLabel_Command(cmd, "root") == get_Root()) {
            checkLoadAnimation_Root_(get_Root());
            return iTrue;
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "navbar.actions.changed")) {
        updateNavBarActions_(navBar);
        return iTrue;
    }
    else if (equal_Command(cmd, "contextclick")) {
        const iRangecc id = range_Command(cmd, "id");
        if (id.start && startsWith_CStr(id.start, "navbar.action")) {
            const int buttonIndex = id.end[-1] - '1';
            iArray items;
            init_Array(&items, sizeof(iMenuItem));
            pushBack_Array(&items, &(iMenuItem){ "```${menu.toolbar.setaction}" });
            for (size_t i = 0; i < max_ToolbarAction; i++) {
                pushBack_Array(
                    &items,
                    &(iMenuItem){
                        format_CStr(
                            "%s %s", toolbarActions_Mobile[i].icon, toolbarActions_Mobile[i].label),
                        0,
                        0,
                        format_CStr("navbar.action.set arg:%d button:%d", i, buttonIndex) });
            }
            openMenu_Widget(
                makeMenu_Widget(get_Root()->widget, constData_Array(&items), size_Array(&items)),
                coord_Command(cmd));
            deinit_Array(&items);
            return iTrue;
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "navigate.focus")) {
        /* The upload dialog has its own path field. */
        if (findChild_Widget(root_Widget(navBar), "upload")) {
            postCommand_Root(navBar->root, "focus.set id:upload.path");
            return iTrue;
        }
        iWidget *url = findChild_Widget(navBar, "url");
        if (focus_Widget() != url) {
            setFocus_Widget(findChild_Widget(navBar, "url"));
        }
        selectAll_InputWidget((iInputWidget *) url);
        return iTrue;
    }
    else if (deviceType_App() != desktop_AppDeviceType &&
             (equal_Command(cmd, "focus.gained") || equal_Command(cmd, "focus.lost"))) {
        iInputWidget *url = findChild_Widget(navBar, "url");
        if (pointer_Command(cmd) == url) {
            const iBool isFocused = equal_Command(cmd, "focus.gained");
            if (deviceType_App() == tablet_AppDeviceType && isPortrait_App()) {
                setFlags_Widget(findChild_Widget(navBar, "navbar.action1"), hidden_WidgetFlag, isFocused);
                setFlags_Widget(findChild_Widget(navBar, "navbar.action2"), hidden_WidgetFlag, isFocused);
                setFlags_Widget(findChild_Widget(navBar, "navbar.action4"), hidden_WidgetFlag, isFocused);
                setFlags_Widget(findChild_Widget(navBar, "navbar.ident"), hidden_WidgetFlag, isFocused);
            }
            setFlags_Widget(findChild_Widget(navBar, "navbar.lock"), hidden_WidgetFlag, isFocused);
            setFlags_Widget(findChild_Widget(navBar, "navbar.clear"), hidden_WidgetFlag, !isFocused);
            showCollapsed_Widget(findChild_Widget(navBar, "navbar.cancel"), isFocused);
            showCollapsed_Widget(findChild_Widget(navBar, "pagemenubutton"), !isFocused);
            showCollapsed_Widget(findChild_Widget(navBar, "reload"), !isFocused);
            updateNavBarSize_(navBar);
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "navbar.clear")) {
        iInputWidget *url = findChild_Widget(navBar, "url");
        setText_InputWidget(url, collectNew_String());
#if 0
        selectAll_InputWidget(url);
        /* Emulate a Backspace keypress. */
        class_InputWidget(url)->processEvent(
            as_Widget(url),
            (SDL_Event *) &(SDL_KeyboardEvent){ .type      = SDL_KEYDOWN,
                                                .timestamp = SDL_GetTicks(),
                                                .state     = SDL_PRESSED,
                                                .keysym    = { .sym = SDLK_BACKSPACE } });
#endif
        return iTrue;
    }
    else if (equal_Command(cmd, "navbar.cancel")) {
        setFocus_Widget(NULL);
        return iTrue;
    }
    else if (equal_Command(cmd, "input.edited")) {
        iAnyObject *   url  = findChild_Widget(navBar, "url");
        const iString *text = rawText_InputWidget(url);
        const iBool    show = willPerformSearchQuery_(text);
        showSearchQueryIndicator_(show);
        if (pointer_Command(cmd) == url) {
            submit_LookupWidget(findWidget_App("lookup"), text);
            return iTrue;
        }
    }
    else if (equalArg_Command(cmd, "input.ended", "id", "url")) {
        iInputWidget *url = findChild_Widget(navBar, "url");
        showSearchQueryIndicator_(iFalse);
        if (isEmpty_String(text_InputWidget(url))) {
            /* User entered nothing; restore the current URL. */
            setText_InputWidget(url, url_DocumentWidget(document_App()));
            return iFalse;
        }
        if (arg_Command(cmd) && argLabel_Command(cmd, "enter") &&
            !isFocused_Widget(findWidget_App("lookup"))) {
            iString *newUrl = copy_String(rawText_InputWidget(url));
            trim_String(newUrl);
            if (willPerformSearchQuery_(newUrl)) {
                postCommandf_Root(
                    navBar->root, "open url:%s", cstr_String(searchQueryUrl_App(newUrl)));
            }
            else {
                postCommandf_Root(
                    navBar->root,
                    "open notinline:1 url:%s",
                    cstr_String(absoluteUrl_String(&iStringLiteral(""), text_InputWidget(url))));
            }
            return iFalse;
        }
    }
    else if (startsWith_CStr(cmd, "document.")) {
        /* React to the current document only. */
        if (document_Command(cmd) == document_App()) {
            if (equal_Command(cmd, "document.changed")) {
                iInputWidget *url = findWidget_Root("url");
                const iString *urlStr = collect_String(suffix_Command(cmd, "url"));
                const enum iGmStatusCode statusCode = argLabel_Command(cmd, "status");
                trimCache_App();
                trimMemory_App();
                visitUrl_Visited(visited_App(),
                                 urlStr,
                                 /* The transient flag modifies history navigation behavior on
                                    special responses like input queries. */
                                 category_GmStatusCode(statusCode) == categoryInput_GmStatusCode ||
                                 category_GmStatusCode(statusCode) == categoryRedirect_GmStatusCode
                                     ? transient_VisitedUrlFlag
                                     : 0);
                postCommand_App("visited.changed"); /* sidebar will update */
                setText_InputWidget(url, urlStr);
                checkLoadAnimation_Root_(get_Root());
                dismissPortraitPhoneSidebars_Root(get_Root());
                updateNavBarIdentity_(navBar);
                updateNavDirButtons_(navBar);
                /* Update site-specific used identities. */ {
                    const iGmIdentity *ident =
                        identityForUrl_GmCerts(certs_App(), url_DocumentWidget(document_App()));
                    if (ident) {
                        const iString *site =
                            collectNewRange_String(urlRoot_String(canonicalUrl_String(urlStr)));
                        const iStringArray *usedIdents =
                            strings_SiteSpec(site, usedIdentities_SiteSpecKey);
                        const iString *fingerprint = collect_String(hexEncode_Block(&ident->fingerprint));
                        /* Keep this identity at the end of the list. */
                        removeString_SiteSpec(site, usedIdentities_SiteSpecKey, fingerprint);
                        insertString_SiteSpec(site, usedIdentities_SiteSpecKey, fingerprint);
                        /* Keep the list short. */
                        while (size_StringArray(usedIdents) > 5) {
                            removeString_SiteSpec(site,
                                                  usedIdentities_SiteSpecKey,
                                                  constAt_StringArray(usedIdents, 0));
                        }
                    }
                }
                /* Icon updates should be limited to automatically chosen icons if the user
                   is allowed to pick their own in the future. */
                if (updateBookmarkIcon_Bookmarks(bookmarks_App(), urlStr,
                                                 siteIcon_GmDocument(document_DocumentWidget(document_App())))) {
                    postCommand_App("bookmarks.changed");
                }
                return iFalse;
            }
            else if (equal_Command(cmd, "document.request.cancelled")) {
                checkLoadAnimation_Root_(get_Root());
                return iFalse;
            }
            else if (equal_Command(cmd, "document.request.started")) {
                iInputWidget *url = findChild_Widget(navBar, "url");
                setTextCStr_InputWidget(url, suffixPtr_Command(cmd, "url"));
                checkLoadAnimation_Root_(get_Root());
                dismissPortraitPhoneSidebars_Root(get_Root());
                return iFalse;
            }
        }
    }
    else if (equal_Command(cmd, "tabs.changed")) {
        /* Update navbar according to the current tab. */
        iDocumentWidget *doc = document_App();
        iAssert(doc);
        if (doc) {
            iInputWidget *url = findChild_Widget(navBar, "url");
            setText_InputWidget(url, url_DocumentWidget(doc));
            if (isFocused_Widget(url)) {
                selectAll_InputWidget(url);
            }
            checkLoadAnimation_Root_(get_Root());
            updateToolbarColors_Root(as_Widget(doc)->root);
            updateNavBarIdentity_(navBar);
        }
        makePaletteGlobal_GmDocument(document_DocumentWidget(doc));
        refresh_Widget(findWidget_Root("doctabs"));
    }
    else if (equal_Command(cmd, "mouse.clicked") && arg_Command(cmd)) {
        iWidget *widget = pointer_Command(cmd);
        iWidget *menu = findWidget_App("doctabs.menu");
        iAssert(menu->root == navBar->root);
        if (isTabButton_Widget(widget)) {
            if (!isVisible_Widget(menu)) {
                iWidget *tabs = findWidget_App("doctabs");
                iWidget *page = tabPage_Widget(tabs, indexOfChild_Widget(widget->parent, widget));
                if (argLabel_Command(cmd, "button") == SDL_BUTTON_MIDDLE) {
                    postCommandf_App("tabs.close id:%s", cstr_String(id_Widget(page)));
                    return iTrue;
                }
                showTabPage_Widget(tabs, page);
                openMenu_Widget(menu, coord_Command(cmd));
            }
        }
    }
    else if (equal_Command(cmd, "navigate.reload")) {
        iDocumentWidget *doc = document_Command(cmd);
        if (isRequestOngoing_DocumentWidget(doc)) {
            postCommand_App("document.stop");
        }
        else {
            postCommand_App("document.reload");
        }
        return iTrue;
    }
    else if (deviceType_App() == tablet_AppDeviceType && equal_Command(cmd, "keyboard.changed")) {
        const int keyboardHeight = arg_Command(cmd);
        if (focus_Widget() == findChild_Widget(navBar, "url") && prefs_App()->bottomNavBar) {
            setVisualOffset_Widget(navBar, -keyboardHeight + bottomSafeInset_Mobile(),
                                   400, easeOut_AnimFlag | softer_AnimFlag);
        }
        else {
            setVisualOffset_Widget(navBar, 0, 400, easeOut_AnimFlag | softer_AnimFlag);
        }
        return iFalse;
    }
    return iFalse;
}

static iBool handleSearchBarCommands_(iWidget *searchBar, const char *cmd) {
    if (equalArg_Command(cmd, "input.ended", "id", "find.input")) {
        iInputWidget *input = findChild_Widget(searchBar, "find.input");
        if (arg_Command(cmd) && argLabel_Command(cmd, "enter") && isVisible_Widget(input)) {
            postCommand_Root(searchBar->root, "find.next");
            /* Keep focus when pressing Enter. */
            if (!isEmpty_String(text_InputWidget(input))) {
                postCommand_Root(searchBar->root, "focus.set id:find.input");
            }
        }
        else {
            postCommand_Root(searchBar->root, "find.clearmark");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "focus.gained")) {
        if (pointer_Command(cmd) == findChild_Widget(searchBar, "find.input")) {
            if (!isVisible_Widget(searchBar)) {
                /* InputWidget will unfocus itself if there isn't enough space for editing
                   text. A collapsed widget will not have been arranged yet, so on the first
                   time the widget will just be unfocused immediately. */
                const iBool wasArranged = area_Rect(bounds_Widget(searchBar)) > 0;
                showCollapsed_Widget(searchBar, iTrue);
                if (!wasArranged) {
                    postCommand_App("focus.set id:find.input");
                }
            }
        }
    }
    else if (equal_Command(cmd, "find.close")) {
        if (isVisible_Widget(searchBar)) {
            showCollapsed_Widget(searchBar, iFalse);
            if (isFocused_Widget(findChild_Widget(searchBar, "find.input"))) {
                setFocus_Widget(NULL);
            }
            refresh_Widget(searchBar->parent);
        }
        return iTrue;
    }
    return iFalse;
}

#if defined (iPlatformMobile)

static void updateToolBarActions_(iWidget *toolBar) {
    const iPrefs *prefs = prefs_App();
    for (int i = 0; i < 2; i++) {
        const int action = prefs->toolbarActions[i];
        iLabelWidget *button =
            findChild_Widget(toolBar, i == 0 ? "toolbar.action1" : "toolbar.action2");
        if (button) {
            setFlags_Widget(as_Widget(button), disabled_WidgetFlag, iFalse);
            setOutline_LabelWidget(button, iFalse);
            updateTextCStr_LabelWidget(button, toolbarActions_Mobile[action].icon);
            setCommand_LabelWidget(button, collectNewCStr_String(toolbarActions_Mobile[action].command));
        }
    }
    refresh_Widget(toolBar);
}

static iBool handleToolBarCommands_(iWidget *toolBar, const char *cmd) {
    if (equalWidget_Command(cmd, toolBar, "mouse.clicked") && arg_Command(cmd) &&
        argLabel_Command(cmd, "button") == SDL_BUTTON_RIGHT) {
        iWidget *menu = findChild_Widget(toolBar, "toolbar.menu");
        arrange_Widget(menu);
        openMenu_Widget(menu, innerToWindow_Widget(menu, init_I2(0, -height_Widget(menu))));
        return iTrue;
    }
    else if (equal_Command(cmd, "toolbar.show")) {
        showToolbar_Root(toolBar->root, arg_Command(cmd));
        return iTrue;
    }
    else if (equal_Command(cmd, "toolbar.showview")) {
        if (arg_Command(cmd) >= 0) {
            postCommandf_App("sidebar.mode arg:%d show:1", arg_Command(cmd));
        }
        else {
            postCommandf_App("sidebar.toggle");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "toolbar.showident")) {
        iWidget *sidebar = findWidget_App("sidebar");
        if (isVisible_Widget(sidebar)) {
            postCommandf_App("sidebar.toggle");
        }
        postCommand_App("preferences idents:1");
        return iTrue;
    }
    else if (equal_Command(cmd, "sidebar.mode.changed")) {
        iLabelWidget *viewTool = findChild_Widget(toolBar, "toolbar.view");
        updateTextCStr_LabelWidget(viewTool, icon_SidebarMode(arg_Command(cmd)));
        return iFalse;
    }
    else if (equal_Command(cmd, "toolbar.actions.changed")) {
        updateToolBarActions_(toolBar);
        return iFalse;        
    }
    else if (equal_Command(cmd, "keyboard.changed") && prefs_App()->bottomNavBar) {
        int height = arg_Command(cmd);
        iWidget *bottomBar = findChild_Widget(root_Widget(toolBar), "bottombar");
        if (!bottomBar) {
            return iFalse;
        }
        iWidget *navBar = findChild_Widget(root_Widget(toolBar), "navbar");
#if defined (iPlatformAppleMobile)
        const int showSpan = 400;
        const int hideSpan = 350;
        const int animFlag = easeOut_AnimFlag | softer_AnimFlag;
        int landscapeOffset = 5 * gap_UI; /* TODO: Why this amount? Something's funny here. */
#else
        const int showSpan = 80;
        const int hideSpan = 250;
        const int animFlag = easeOut_AnimFlag;
        int landscapeOffset = 0;
#endif
        if (focus_Widget() == findChild_Widget(navBar, "url") && height > 0) {
            int keyboardPad = height - (isPortrait_App() ? height_Widget(toolBar) : landscapeOffset);
            bottomBar->padding[3] = keyboardPad;
            arrange_Widget(bottomBar);
            arrange_Widget(bottomBar);
            setVisualOffset_Widget(bottomBar, keyboardPad, 0, 0);
            setVisualOffset_Widget(bottomBar, 0, showSpan, animFlag);
        }
        if (height == 0) {
            setVisualOffset_Widget(bottomBar, -bottomBar->padding[3], 0, 0);
            setVisualOffset_Widget(bottomBar, 0, hideSpan, animFlag);
            bottomBar->padding[3] = 0;
            arrange_Widget(bottomBar);
            arrange_Widget(bottomBar);
            updateBottomBarPosition_(bottomBar, iTrue);
        }
        return iFalse;
    }
    return iFalse;
}

#endif /* defined (iPlatformMobile) */

static iLabelWidget *newLargeIcon_LabelWidget(const char *text, const char *cmd) {
    iLabelWidget *lab = newIcon_LabelWidget(text, 0, 0, cmd);
    setFont_LabelWidget(lab, uiLabelLarge_FontId);
    return lab;
}

int appIconSize_Root(void) {
    return lineHeight_Text(uiContent_FontId);
}

void updateMetrics_Root(iRoot *d) {
    if (d == NULL) {
        return;
    }
    /* Custom frame. */
    iWidget *winBar = findChild_Widget(d->widget, "winbar");
    if (winBar) {
        iWidget *appIcon  = findChild_Widget(winBar, "winbar.icon");
        iWidget *appTitle = findChild_Widget(winBar, "winbar.title");
        iWidget *appMin   = findChild_Widget(winBar, "winbar.min");
        iWidget *appMax   = findChild_Widget(winBar, "winbar.max");
        iWidget *appClose = findChild_Widget(winBar, "winbar.close");
        setPadding_Widget(winBar, 0, gap_UI / 3, 0, 0);
        setFixedSize_Widget(appMin, init_I2(gap_UI * 11.5f, height_Widget(appTitle)));
        setFixedSize_Widget(appMax, appMin->rect.size);
        setFixedSize_Widget(appClose, appMin->rect.size);
        setFixedSize_Widget(appIcon, init_I2(appIconSize_Root(), appMin->rect.size.y));
    }
    iWidget      *navBar     = findChild_Widget(d->widget, "navbar");
    iWidget      *url        = findChild_Widget(d->widget, "url");
    iWidget      *rightEmbed = findChild_Widget(navBar, "url.rightembed");
    iWidget      *embedPad   = findChild_Widget(navBar, "url.embedpad");
    iWidget      *urlButtons = findChild_Widget(navBar, "url.buttons");
    iLabelWidget *idName     = findChild_Widget(d->widget, "toolbar.name");
    if (navBar) {
        setPadding_Widget(as_Widget(url), 0, gap_UI, 0, gap_UI);
        setFixedSize_Widget(embedPad, init_I2(width_Widget(urlButtons) + gap_UI / 2, 1));
        rightEmbed->rect.pos.y = gap_UI;
    }
    updatePadding_Root(d);
    arrange_Widget(d->widget);
    if (navBar) {
        updateUrlInputContentPadding_(navBar);
    }
    if (idName) {
        setFixedSize_Widget(as_Widget(idName),
                            init_I2(-1, 2 * gap_UI + lineHeight_Text(uiLabelTiny_FontId)));
    }
    postRefresh_App();
}

static void addUnsplitButton_(iWidget *navBar) {
    iLabelWidget *unsplit = addChildFlags_Widget(
        navBar,
        iClob(newIcon_LabelWidget(close_Icon, 0, 0, "ui.split arg:0 focusother:1")),
        collapse_WidgetFlag | frameless_WidgetFlag | tight_WidgetFlag | hidden_WidgetFlag);
    setId_Widget(as_Widget(unsplit), "navbar.unsplit");
    setTextColor_LabelWidget(unsplit, uiTextAction_ColorId);
    updateSize_LabelWidget(unsplit);
}

static int sortByWindowPtrSerial_(const void *e1, const void *e2) {
    const iWindow * const *w[2] = { e1, e2 };
    return iCmp((*w[0])->serial, (*w[1])->serial);
}

static iBool updateWindowMenu_(iWidget *menuBarItem, const char *cmd) {
    /* Note: This only works with non-native menus. */
    if (equalWidget_Command(cmd, menuBarItem, "menu.opened")) {
        /* Get rid of the old window list. See `windowMenuItems_` in window.c for the fixed list. */
        iWidget *menu = findChild_Widget(menuBarItem, "menu");
        while (childCount_Widget(menu) > 9) {
            destroy_Widget(removeChild_Widget(menu, child_Widget(menu, 9)));
        }
        iArray winItems;
        init_Array(&winItems, sizeof(iMenuItem));
        iPtrArray *sortedWindows = collect_PtrArray(copy_PtrArray(mainWindows_App()));
        sort_Array(sortedWindows, sortByWindowPtrSerial_);
        iForEach(PtrArray, i, sortedWindows) {
            const iWindow *win = i.ptr;
            iDocumentWidget *doc = document_Root(win->roots[0]);
            pushBack_Array(&winItems,
                           &(iMenuItem){ .label = cstr_String(bookmarkTitle_DocumentWidget(doc)),
                                         0,
                                         0,
                                         format_CStr("!window.activate ptr:%p", win) });
        }
        makeMenuItems_Widget(menu, constData_Array(&winItems), size_Array(&winItems));
        iLabelWidget *curWinItem =
            findMenuItem_Widget(menu, format_CStr("!window.activate ptr:%p", get_MainWindow()));
        if (curWinItem) {
            setFlags_Widget(as_Widget(curWinItem), noBackground_WidgetFlag, iFalse);
            setBackgroundColor_Widget(as_Widget(curWinItem), uiBackgroundUnfocusedSelection_ColorId);
            setTextColor_LabelWidget(curWinItem, uiTextStrong_ColorId);
        }
        deinit_Array(&winItems);
        arrange_Widget(menu);
    }
    return handleTopLevelMenuBarCommand_Widget(menuBarItem, cmd);
}

static iBool updateMobilePageMenuItems_(iWidget *menu, const char *cmd) {
    if (equalWidget_Command(cmd, menu, "menu.opened")) {
        /* Update the items. */
        setMenuItemLabel_Widget(menu,
                                "document.viewformat",
                                isSourceTextView_DocumentWidget(document_App())
                                    ? "${menu.viewformat.gemini}"
                                    : "${menu.viewformat.plain}",
                                ' ');
    }
    return handleMenuCommand_Widget(menu, cmd);
}

void createUserInterface_Root(iRoot *d) {
    iWidget *root = d->widget = new_Widget();
    root->rect.size = get_Window()->size;
    iAssert(root->root == d);
    setId_Widget(root, "root");
    /* Children of root cover the entire window. */
    setFlags_Widget(
        root, resizeChildren_WidgetFlag | fixedSize_WidgetFlag | focusRoot_WidgetFlag, iTrue);
    setCommandHandler_Widget(root, handleRootCommands_Widget);
    iWidget *div = makeVDiv_Widget();
    setId_Widget(div, "navdiv");
    addChild_Widget(root, iClob(div));
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
    /* Window title bar. */
    if (prefs_App()->customFrame) {
        setPadding1_Widget(div, 1);
        iWidget *winBar = new_Widget();
        setId_Widget(winBar, "winbar");
        setFlags_Widget(winBar,
                        arrangeHeight_WidgetFlag | resizeWidthOfChildren_WidgetFlag |
                            arrangeHorizontal_WidgetFlag | collapse_WidgetFlag,
                        iTrue);
        iWidget *appIcon;
        setId_Widget(addChildFlags_Widget(
                         winBar, iClob(appIcon = makePadding_Widget(0)), collapse_WidgetFlag),
                     "winbar.icon");
        iLabelWidget *appButton = addChildFlags_Widget(
            winBar,
            iClob(new_LabelWidget("Lagrange", NULL)),
            fixedHeight_WidgetFlag | frameless_WidgetFlag | collapse_WidgetFlag);
        setTextColor_LabelWidget(appButton, uiTextAppTitle_ColorId);
        setId_Widget(as_Widget(appButton), "winbar.app");
        iLabelWidget *appTitle;
        setFont_LabelWidget(appButton, uiContentBold_FontId);
        setId_Widget(addChildFlags_Widget(winBar,
                                          iClob(appTitle = new_LabelWidget("", NULL)),
                                          expand_WidgetFlag | fixedHeight_WidgetFlag |
                                              frameless_WidgetFlag | commandOnClick_WidgetFlag),
                     "winbar.title");
        setTextColor_LabelWidget(appTitle, uiTextStrong_ColorId);
        iLabelWidget *appMin, *appMax, *appClose;
        setId_Widget(addChildFlags_Widget(
                         winBar,
                         iClob(appMin = newLargeIcon_LabelWidget("\u2013", "window.minimize")),
                         frameless_WidgetFlag | collapse_WidgetFlag),
                     "winbar.min");
        addChildFlags_Widget(
            winBar,
            iClob(appMax = newLargeIcon_LabelWidget("\u25a1", "window.maximize toggle:1")),
            frameless_WidgetFlag | collapse_WidgetFlag);
        setId_Widget(as_Widget(appMax), "winbar.max");
        addChildFlags_Widget(winBar,
                             iClob(appClose = newLargeIcon_LabelWidget(close_Icon, "window.close")),
                             frameless_WidgetFlag | collapse_WidgetFlag);
        setId_Widget(as_Widget(appClose), "winbar.close");
        setFont_LabelWidget(appClose, uiContent_FontId);
        addChild_Widget(div, iClob(winBar));
        setBackgroundColor_Widget(winBar, uiBackground_ColorId);
    }
#endif
#if defined (LAGRANGE_MENUBAR) && !defined (iPlatformMobile)
    /* Application menus. */ {
        iWidget *menuBar = addChildFlags_Widget(
            div,
            iClob(makeMenuBar_Widget(topLevelMenus_Window, iElemCount(topLevelMenus_Window))),
            collapse_WidgetFlag);
        /* The window menu needs to be dynamically updated with the list of open windows. */
        setCommandHandler_Widget(child_Widget(menuBar, 5), updateWindowMenu_);
        setId_Widget(menuBar, "menubar");
#  if 0
        addChildFlags_Widget(menuBar, iClob(new_Widget()), expand_WidgetFlag);
        /* It's nice to use this space for something, but it should be more valuable than 
           just the app version... */
        iLabelWidget *ver = addChildFlags_Widget(menuBar, iClob(new_LabelWidget(LAGRANGE_APP_VERSION, NULL)),
                                                 frameless_WidgetFlag);
        setTextColor_LabelWidget(ver, uiAnnotation_ColorId);
#  endif
    }
#endif        
    iWidget *navBar;
    /* Navigation bar. */ {
        navBar = new_Widget();
        setId_Widget(navBar, "navbar");
        setDrawBufferEnabled_Widget(navBar, iTrue);
        setFlags_Widget(navBar,
                        hittable_WidgetFlag | /* context menu */
                            arrangeHeight_WidgetFlag |
                            resizeWidthOfChildren_WidgetFlag |
                            arrangeHorizontal_WidgetFlag |
                            drawBackgroundToHorizontalSafeArea_WidgetFlag |
                            drawBackgroundToVerticalSafeArea_WidgetFlag,
                        iTrue);
        addChild_Widget(div, iClob(navBar));
        setBackgroundColor_Widget(navBar, uiBackground_ColorId);
        setCommandHandler_Widget(navBar, handleNavBarCommands_);
#if defined (iPlatformApple)
        addUnsplitButton_(navBar);
#endif
        setId_Widget(addChildFlags_Widget(navBar, iClob(newIcon_LabelWidget(backArrow_Icon, 0, 0, "navigate.back")), collapse_WidgetFlag), "navbar.action1");
        setId_Widget(addChildFlags_Widget(navBar, iClob(newIcon_LabelWidget(forwardArrow_Icon, 0, 0, "navigate.forward")), collapse_WidgetFlag), "navbar.action2");
        /* Button for toggling the left sidebar. */
        setId_Widget(addChildFlags_Widget(
                         navBar,
                         iClob(newIcon_LabelWidget(leftHalf_Icon, 0, 0, "sidebar.toggle")),
                         collapse_WidgetFlag),
                     "navbar.action3");
        addChildFlags_Widget(navBar, iClob(new_Widget()), expand_WidgetFlag | fixedHeight_WidgetFlag);
        iInputWidget *url;
        /* URL input field. */ {
            url = new_InputWidget(0);
            setFlags_Widget(as_Widget(url), resizeHeightOfChildren_WidgetFlag, iTrue);
            setSelectAllOnFocus_InputWidget(url, iTrue);
            setId_Widget(as_Widget(url), "url");
            setLineLimits_InputWidget(url, 1, 1); /* just one line while not focused */
            setLineBreaksEnabled_InputWidget(url, iFalse);
            setUrlContent_InputWidget(url, iTrue);
            setNotifyEdits_InputWidget(url, iTrue);
            setOmitDefaultSchemeIfNarrow_InputWidget(url, iTrue);
            setTextCStr_InputWidget(url, "gemini://");
            addChildFlags_Widget(navBar, iClob(url), 0);
            const int64_t embedFlags =
                noBackground_WidgetFlag | frameless_WidgetFlag | unpadded_WidgetFlag |
                (deviceType_App() == desktop_AppDeviceType ? tight_WidgetFlag : 0);
            /* Page information/certificate warning. */ {
                iLabelWidget *lock = addChildFlags_Widget(
                    as_Widget(url),
                    iClob(newIcon_LabelWidget("\U0001f513", SDLK_i, KMOD_PRIMARY, "document.info")),
                    embedFlags | moveToParentLeftEdge_WidgetFlag);
                setId_Widget(as_Widget(lock), "navbar.lock");
//                setFont_LabelWidget(lock, symbols_FontId + uiNormal_FontSize);
                updateTextCStr_LabelWidget(lock, "\U0001f512");
            }
            /* Button for clearing the URL bar contents. */ {
                iLabelWidget *clear = addChildFlags_Widget(
                    as_Widget(url),
                    iClob(newIcon_LabelWidget(delete_Icon, 0, 0, "navbar.clear")),
                    hidden_WidgetFlag | embedFlags | moveToParentLeftEdge_WidgetFlag | tight_WidgetFlag);
                setId_Widget(as_Widget(clear), "navbar.clear");
//                setFont_LabelWidget(clear, symbols2_FontId + uiNormal_FontSize);
                setFont_LabelWidget(clear, uiLabelSymbols_FontId);
//                setFlags_Widget(as_Widget(clear), noBackground_WidgetFlag, iFalse);
//                setBackgroundColor_Widget(as_Widget(clear), uiBackground_ColorId);
            }
            iWidget *rightEmbed = new_Widget();
            setId_Widget(rightEmbed, "url.rightembed");
            addChildFlags_Widget(as_Widget(url),
                                 iClob(rightEmbed),
                                 arrangeHorizontal_WidgetFlag | arrangeWidth_WidgetFlag |
                                     resizeHeightOfChildren_WidgetFlag |
                                     moveToParentRightEdge_WidgetFlag);
            /* Feeds refresh indicator is inside the input field. */ {
                iLabelWidget *queryInd = new_LabelWidget("${status.query} " return_Icon, NULL);
                setId_Widget(as_Widget(queryInd), "input.indicator.search");
                setTextColor_LabelWidget(queryInd, uiTextAction_ColorId);
                setFont_LabelWidget(queryInd, uiLabelSmall_FontId);
                setBackgroundColor_Widget(as_Widget(queryInd), uiBackground_ColorId);
                setFrameColor_Widget(as_Widget(queryInd), uiTextAction_ColorId);
//                setAlignVisually_LabelWidget(queryInd, iTrue);
                setNoAutoMinHeight_LabelWidget(queryInd, iTrue);
                addChildFlags_Widget(rightEmbed,
                                     iClob(queryInd),
                                     collapse_WidgetFlag | hidden_WidgetFlag);
            }
            /* Feeds refresh indicator is inside the input field. */ {
                iLabelWidget *fprog = new_LabelWidget("", NULL);
                setId_Widget(as_Widget(fprog), "feeds.progress");
                setTextColor_LabelWidget(fprog, uiTextAction_ColorId);
                setFont_LabelWidget(fprog, uiLabelSmall_FontId);
                setBackgroundColor_Widget(as_Widget(fprog), uiBackground_ColorId);
//                setAlignVisually_LabelWidget(fprog, iTrue);
                setNoAutoMinHeight_LabelWidget(fprog, iTrue);
                iWidget *progBar = new_Widget();
                setBackgroundColor_Widget(progBar, uiTextAction_ColorId);
                setFixedSize_Widget(progBar, init_I2(0, gap_UI / 4));
                setId_Widget(addChildFlags_Widget(as_Widget(fprog), iClob(progBar),
                                                  moveToParentBottomEdge_WidgetFlag),
                             "feeds.progressbar");
                addChildFlags_Widget(rightEmbed,
                                     iClob(fprog),
                                     collapse_WidgetFlag | hidden_WidgetFlag | frameless_WidgetFlag);
            }
            /* Download progress indicator is also inside the input field, but hidden normally. */ {
                iLabelWidget *progress = new_LabelWidget(uiTextCaution_ColorEscape "00.000 ${mb}", NULL);
                setId_Widget(as_Widget(progress), "document.progress");
                setBackgroundColor_Widget(as_Widget(progress), uiBackground_ColorId);
                setAlignVisually_LabelWidget(progress, iTrue);
                setNoAutoMinHeight_LabelWidget(progress, iTrue);
                addChildFlags_Widget(
                    rightEmbed, iClob(progress), collapse_WidgetFlag | hidden_WidgetFlag);
            }
            /* Pinning indicator. */ {
                iLabelWidget *indicator = new_LabelWidget(uiTextAction_ColorEscape leftHalf_Icon, NULL);
                setId_Widget(as_Widget(indicator), "document.pinned");
                setBackgroundColor_Widget(as_Widget(indicator), uiBackground_ColorId);
                setAlignVisually_LabelWidget(indicator, iTrue);
                setNoAutoMinHeight_LabelWidget(indicator, iTrue);
                addChildFlags_Widget(rightEmbed,
                                     iClob(indicator),
                                     collapse_WidgetFlag | hidden_WidgetFlag | tight_WidgetFlag |
                                     frameless_WidgetFlag);
                updateSize_LabelWidget(indicator);
            }
            iWidget *urlButtons = new_Widget();
            setId_Widget(urlButtons, "url.buttons");
            setFlags_Widget(urlButtons, embedFlags | arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
            /* Mobile page menu. */
            if (deviceType_App() != desktop_AppDeviceType) {
                iLabelWidget *navCancel = new_LabelWidget("${cancel}", "navbar.cancel");
                addChildFlags_Widget(urlButtons, iClob(navCancel),
                                     (embedFlags | tight_WidgetFlag | hidden_WidgetFlag |
                                      collapse_WidgetFlag) /*& ~noBackground_WidgetFlag*/);
                as_Widget(navCancel)->sizeRef = as_Widget(url);
                setFont_LabelWidget(navCancel, uiContentBold_FontId);
                setId_Widget(as_Widget(navCancel), "navbar.cancel");
                iLabelWidget *pageMenuButton;
                /* In a mobile layout, the reload button is replaced with the Page/Ellipsis menu. */
                pageMenuButton = makeMenuButton_LabelWidget(pageMenuCStr_,
                    (iMenuItem[]){
                        { upArrow_Icon " ${menu.parent}", navigateParent_KeyShortcut, "navigate.parent" },
                        { upArrowBar_Icon " ${menu.root}", navigateRoot_KeyShortcut, "navigate.root" },
                        { timer_Icon " ${menu.autoreload}", 0, 0, "document.autoreload.menu" },
                        { "---" },
                        { bookmark_Icon " ${menu.page.bookmark}", bookmarkPage_KeyShortcut, "bookmark.add" },
                        { star_Icon " ${menu.page.subscribe}", subscribeToPage_KeyShortcut, "feeds.subscribe" },
                        { globe_Icon " ${menu.page.translate}", 0, 0, "document.translate" },
                        { upload_Icon " ${menu.page.upload}", 0, 0, "document.upload" },
                        { edit_Icon " ${menu.page.upload.edit}", 0, 0, "document.upload copy:1" },
                        { book_Icon " ${menu.page.import}", 0, 0, "bookmark.links confirm:1" },
                        { "---" },
                        { download_Icon " " saveToDownloads_Label, SDLK_s, KMOD_PRIMARY, "document.save" },
                        { "${menu.page.copysource}", 'c', KMOD_PRIMARY, "copy" },
                        { "${menu.viewformat.plain}", 0, 0, "document.viewformat" } },
                    14);
                setCommandHandler_Widget(findChild_Widget(as_Widget(pageMenuButton), "menu"),
                                         updateMobilePageMenuItems_);
                setId_Widget(as_Widget(pageMenuButton), "pagemenubutton");
                setFont_LabelWidget(pageMenuButton, uiContentBold_FontId);
                setAlignVisually_LabelWidget(pageMenuButton, iTrue);
                addChildFlags_Widget(urlButtons, iClob(pageMenuButton),
                                     embedFlags | tight_WidgetFlag | collapse_WidgetFlag |
                                     resizeToParentHeight_WidgetFlag);
                updateSize_LabelWidget(pageMenuButton);
            }
            /* Bookmark indicator. */ {
                iLabelWidget *pin = new_LabelWidget(bookmark_Icon, "bookmark.add");
                setId_Widget(as_Widget(pin), "document.bookmarked");
                setTextColor_LabelWidget(pin, uiTextAction_ColorId);
                setBackgroundColor_Widget(as_Widget(pin), uiInputBackground_ColorId);
                setAlignVisually_LabelWidget(pin, iTrue);
                addChildFlags_Widget(urlButtons,
                                     iClob(pin),
                                     embedFlags | collapse_WidgetFlag | tight_WidgetFlag |
                                         resizeToParentHeight_WidgetFlag);
                updateSize_LabelWidget(pin);        
            }
            /* Reload button. */ {
                iLabelWidget *reload = newIcon_LabelWidget(reloadCStr_, 0, 0, "navigate.reload");
                setId_Widget(as_Widget(reload), "reload");
                addChildFlags_Widget(urlButtons, iClob(reload), embedFlags | collapse_WidgetFlag |
                                     resizeToParentHeight_WidgetFlag);
                updateSize_LabelWidget(reload);
            }
            addChildFlags_Widget(as_Widget(url), iClob(urlButtons), moveToParentRightEdge_WidgetFlag);
            arrange_Widget(urlButtons);
            setId_Widget(addChild_Widget(rightEmbed, iClob(makePadding_Widget(0))), "url.embedpad");
        }
        /* The active identity menu. */ {
            iLabelWidget *idButton = new_LabelWidget(person_Icon, "identmenu.open");
            setAlignVisually_LabelWidget(idButton, iTrue);
            setId_Widget(addChildFlags_Widget(navBar, iClob(idButton), collapse_WidgetFlag), "navbar.ident");
        }
        addChildFlags_Widget(navBar, iClob(new_Widget()), expand_WidgetFlag | fixedHeight_WidgetFlag);
        setId_Widget(addChildFlags_Widget(navBar,
                                          iClob(newIcon_LabelWidget(
                                              home_Icon, 0, 0, "navigate.home")),
                                          collapse_WidgetFlag),
                     "navbar.action4");
#if !defined (LAGRANGE_MAC_MENUBAR)
        /* Hamburger menu. */ {
            iLabelWidget *navMenu = makeMenuButton_LabelWidget(
                menu_Icon,
                deviceType_App() == desktop_AppDeviceType  ? desktopNavMenuItems_
                : deviceType_App() == tablet_AppDeviceType ? tabletNavMenuItems_
                                                           : phoneNavMenuItems_,
                iInvalidSize);
            setFrameColor_Widget(findChild_Widget(as_Widget(navMenu), "menu"), uiSeparator_ColorId);
            setCommand_LabelWidget(navMenu, collectNewCStr_String("menu.open under:1"));
            setAlignVisually_LabelWidget(navMenu, iTrue);
            setId_Widget(addChildFlags_Widget(navBar, iClob(navMenu), collapse_WidgetFlag), "navbar.menu");
        }
#endif
#if !defined (iPlatformApple)
        /* On PC platforms, the close buttons are generally on the top right. */
        addUnsplitButton_(navBar);
#endif
        if (deviceType_App() == tablet_AppDeviceType) {
            /* Ensure that all navbar buttons match the height of the input field.
               This is required because touch input fields are given extra padding,
               making them taller than buttons by default. */
            iForEach(ObjectList, i, children_Widget(navBar)) {
                if (isInstance_Object(i.object, &Class_LabelWidget)) {
                    as_Widget(i.object)->sizeRef = as_Widget(url);
                }
            }
        }
    }
    /* Tab bar. */ {
        iWidget *mainStack = new_Widget();
        setId_Widget(mainStack, "stack");
        addChildFlags_Widget(div, iClob(mainStack), resizeChildren_WidgetFlag | expand_WidgetFlag |
                                                        unhittable_WidgetFlag);
        iWidget *docTabs = makeTabs_Widget(mainStack);
        setId_Widget(docTabs, "doctabs");
        setBackgroundColor_Widget(docTabs, uiBackground_ColorId);
//        setTabBarPosition_Widget(docTabs, prefs_App()->bottomTabBar);
        iDocumentWidget *doc;
        appendTabPage_Widget(docTabs, iClob(doc = new_DocumentWidget()), "Document", 0, 0);
        addTabCloseButton_Widget(docTabs, as_Widget(doc), "tabs.close");
        iWidget *buttons = findChild_Widget(docTabs, "tabs.buttons");
        setFlags_Widget(buttons, collapse_WidgetFlag | hidden_WidgetFlag |
                                     drawBackgroundToHorizontalSafeArea_WidgetFlag, iTrue);
        if (deviceType_App() == phone_AppDeviceType) {
            setBackgroundColor_Widget(buttons, uiBackground_ColorId);
        }
        setId_Widget(
            addChildFlags_Widget(buttons,
                                 iClob(newIcon_LabelWidget(add_Icon, 0, 0, "tabs.new append:1")),
                                 moveToParentRightEdge_WidgetFlag | collapse_WidgetFlag),
            "newtab");
    }
    /* Sidebars. */ {
        iSidebarWidget *sidebar1 = new_SidebarWidget(left_SidebarSide);
        if (deviceType_App() != phone_AppDeviceType) {
            /* Sidebars are next to the tab content. */
            iWidget *content = findChild_Widget(root, "tabs.content");
            addChildPos_Widget(content, iClob(sidebar1), front_WidgetAddPos);
            iSidebarWidget *sidebar2 = new_SidebarWidget(right_SidebarSide);
            addChildPos_Widget(content, iClob(sidebar2), back_WidgetAddPos);
            setFlags_Widget(as_Widget(sidebar2), disabledWhenHidden_WidgetFlag, iTrue);
        }
        else {
            /* Sidebar is a slide-over sheet. */
            addChild_Widget(root, iClob(sidebar1));
            setFlags_Widget(as_Widget(sidebar1), hidden_WidgetFlag, iTrue);            
        }
    }
    /* Lookup results. */ {
        iLookupWidget *lookup = new_LookupWidget();
        addChildFlags_Widget(div, iClob(lookup), fixedPosition_WidgetFlag | hidden_WidgetFlag);
    }
    /* Search bar. */ {
        iWidget *searchBar = new_Widget();
        setId_Widget(searchBar, "search");
        setFlags_Widget(searchBar,
                        hidden_WidgetFlag | disabledWhenHidden_WidgetFlag | collapse_WidgetFlag |
                            arrangeHeight_WidgetFlag | resizeWidthOfChildren_WidgetFlag |
                            arrangeHorizontal_WidgetFlag,
                        iTrue);
        if (deviceType_App() == desktop_AppDeviceType) {
            addChild_Widget(div, iClob(searchBar));
        }
        else {
            /* The search bar appears at the top on mobile, because there is a virtual keyboard
               covering the bottom. */
            insertChildAfter_Widget(div, iClob(searchBar),
                                    indexOfChild_Widget(div, findChild_Widget(div, "navbar")));
        }
        setBackgroundColor_Widget(searchBar, uiBackground_ColorId);
        setCommandHandler_Widget(searchBar, handleSearchBarCommands_);
        addChildFlags_Widget(
            searchBar, iClob(new_LabelWidget(magnifyingGlass_Icon, NULL)), frameless_WidgetFlag);
        iInputWidget *input = new_InputWidget(0);
        setHint_InputWidget(input, "${hint.findtext}");
        setSelectAllOnFocus_InputWidget(input, iTrue);
        setEatEscape_InputWidget(input, iFalse); /* unfocus and close with one keypress */
        setLineBreaksEnabled_InputWidget(input, iFalse);
        setId_Widget(addChildFlags_Widget(searchBar, iClob(input), expand_WidgetFlag),
                     "find.input");
        addChild_Widget(searchBar, iClob(newIcon_LabelWidget("  \u2b9f  ", 'g', KMOD_PRIMARY, "find.next")));
        addChild_Widget(searchBar, iClob(newIcon_LabelWidget("  \u2b9d  ", 'g', KMOD_PRIMARY | KMOD_SHIFT, "find.prev")));
        addChild_Widget(searchBar, iClob(newIcon_LabelWidget(close_Icon, SDLK_ESCAPE, 0, "find.close")));
    }
#if defined (iPlatformMobile)
    /* Bottom toolbar. */
    if (deviceType_App() == phone_AppDeviceType) {
        iWidget *bottomBar = new_Widget();
        setId_Widget(bottomBar, "bottombar");
        addChildFlags_Widget(root,
                             iClob(bottomBar),
                             moveToParentBottomEdge_WidgetFlag |
                                 parentCannotResizeHeight_WidgetFlag | arrangeVertical_WidgetFlag |
                                 arrangeHeight_WidgetFlag | resizeWidthOfChildren_WidgetFlag |
                                 drawBackgroundToHorizontalSafeArea_WidgetFlag |
                                 drawBackgroundToBottom_WidgetFlag);
        iWidget *toolBar = new_Widget();
        addChild_Widget(bottomBar, iClob(toolBar));
        setId_Widget(toolBar, "toolbar");
        setDrawBufferEnabled_Widget(toolBar, iTrue);
        setCommandHandler_Widget(toolBar, handleToolBarCommands_);
        setFlags_Widget(toolBar,
                        //moveToParentBottomEdge_WidgetFlag | parentCannotResizeHeight_WidgetFlag |
                            resizeWidthOfChildren_WidgetFlag | arrangeHeight_WidgetFlag |
                            arrangeHorizontal_WidgetFlag | commandOnClick_WidgetFlag | collapse_WidgetFlag,
                        iTrue);
        setId_Widget(addChildFlags_Widget(toolBar,
                                          iClob(newLargeIcon_LabelWidget("", "...")),
                                          frameless_WidgetFlag),
                     "toolbar.action1");
        setId_Widget(addChildFlags_Widget(toolBar,
                                          iClob(newLargeIcon_LabelWidget("", "...")),
                                          frameless_WidgetFlag),
                     "toolbar.action2");
        iWidget *identButton;
        setId_Widget(identButton = addChildFlags_Widget(
                         toolBar,
                         iClob(newLargeIcon_LabelWidget("\U0001f464", "identmenu.open")),
                         frameless_WidgetFlag | fixedHeight_WidgetFlag),
                     "toolbar.ident");
        setId_Widget(addChildFlags_Widget(
                         toolBar,
                         iClob(newLargeIcon_LabelWidget(book_Icon, "toolbar.showview arg:-1")),
                         frameless_WidgetFlag | commandOnClick_WidgetFlag),
                     "toolbar.view");
        iLabelWidget *idName;
        setId_Widget(addChildFlags_Widget(identButton,
                                          iClob(idName = new_LabelWidget("", NULL)),
                                          frameless_WidgetFlag |
                                              noBackground_WidgetFlag |
                                              moveToParentBottomEdge_WidgetFlag |
                                              resizeToParentWidth_WidgetFlag),
                     "toolbar.name");
        setFont_LabelWidget(idName, uiLabelTiny_FontId);
        iLabelWidget *menuButton = makeMenuButton_LabelWidget(menu_Icon, phoneNavMenuItems_,
                                                              iElemCount(phoneNavMenuItems_));
        setFont_LabelWidget(menuButton, uiLabelLarge_FontId);
        setId_Widget(as_Widget(menuButton), "toolbar.navmenu");
        addChildFlags_Widget(toolBar, iClob(menuButton), frameless_WidgetFlag);
        iForEach(ObjectList, i, children_Widget(toolBar)) {
            setFlags_Widget(i.object, noBackground_WidgetFlag, iTrue);
        }
        updateToolbarColors_Root(d);
        updateToolBarActions_(toolBar);
        const iMenuItem items[] = {
            { book_Icon " ${sidebar.bookmarks}", 0, 0, "toolbar.showview arg:0" },
            { star_Icon " ${sidebar.feeds}", 0, 0, "toolbar.showview arg:1" },
            { clock_Icon " ${sidebar.history}", 0, 0, "toolbar.showview arg:2" },
            { page_Icon " ${toolbar.outline}", 0, 0, "toolbar.showview arg:4" },
        };
        iWidget *menu = makeMenu_Widget(findChild_Widget(toolBar, "toolbar.view"),
                                        items, iElemCount(items));
        setId_Widget(menu, "toolbar.menu"); /* view menu */
    }
#endif
    setupMovableElements_Root_(d);
    updateNavBarActions_(navBar);
    updatePadding_Root(d);
    /* Global context menus. */ {
        iArray *tabsItems = collectNew_Array(sizeof(iMenuItem));
        pushBackN_Array(tabsItems,
            (iMenuItem[]){
                { close_Icon " ${menu.closetab}", 0, 0, "tabs.close" },
                { copy_Icon " ${menu.duptab}", 0, 0, "tabs.new duplicate:1" },
                { "---" },
                { "${menu.closetab.other}", 0, 0, "tabs.close toleft:1 toright:1" },
                { barLeftArrow_Icon " ${menu.closetab.left}", 0, 0, "tabs.close toleft:1" },
                { barRightArrow_Icon " ${menu.closetab.right}", 0, 0, "tabs.close toright:1" },
                { "---" },
                { leftAngle_Icon " ${menu.movetab.left}", 0, 0, "tabs.move arg:-1" },
                { rightAngle_Icon " ${menu.movetab.right}", 0, 0, "tabs.move arg:1" } },
        9);
        if (deviceType_App() != phone_AppDeviceType) {
            pushBack_Array(tabsItems, &(iMenuItem){ "${menu.movetab.split}", 0, 0, "tabs.swap" });
        }
        if (deviceType_App() == desktop_AppDeviceType) {
            pushBack_Array(tabsItems, &(iMenuItem){ "${menu.movetab.newwindow}", 0, 0, "tabs.swap newwindow:1" });
        }
        iWidget *tabsMenu = makeMenu_Widget(root, data_Array(tabsItems), size_Array(tabsItems));
        /* TODO: .newwindow is only for desktop; .split is not for phone */
        iWidget *barMenu =
            makeMenu_Widget(root,
                            (iMenuItem[]){
                                { leftHalf_Icon " ${menu.sidebar.left}", 0, 0, "sidebar.toggle" },
                                { rightHalf_Icon " ${menu.sidebar.right}", 0, 0, "sidebar2.toggle" },
                            },
                            deviceType_App() == phone_AppDeviceType ? 1 : 2);
        iWidget *clipMenu = makeMenu_Widget(root,
#if defined (iPlatformMobile)
            (iMenuItem[]){
                { ">>>" scissor_Icon " ${menu.cut}", 0, 0, "input.copy cut:1" },
                { ">>>" clipCopy_Icon " ${menu.copy}", 0, 0, "input.copy" },
                { ">>>" clipboard_Icon " ${menu.paste}", 0, 0, "input.paste" },
                { "---" },
                { ">>>" delete_Icon " " uiTextCaution_ColorEscape "${menu.delete}", 0, 0, "input.delete" },
                { ">>>" select_Icon " ${menu.selectall}", 0, 0, "input.selectall" },
                { ">>>" undo_Icon " ${menu.undo}", 0, 0, "input.undo" },
            }, 7);
#else
            (iMenuItem[]){
                { scissor_Icon " ${menu.cut}", 0, 0, "input.copy cut:1" },
                { clipCopy_Icon " ${menu.copy}", 0, 0, "input.copy" },
                { clipboard_Icon " ${menu.paste}", 0, 0, "input.paste" },
                { return_Icon " ${menu.paste.go}", 0, 0, "input.paste enter:1" },
                { "---" },
                { delete_Icon " " uiTextCaution_ColorEscape "${menu.delete}", 0, 0, "input.delete" },
                { undo_Icon " ${menu.undo}", 0, 0, "input.undo" },
                { "---" },
                { select_Icon " ${menu.selectall}", 0, 0, "input.selectall" },
            }, 9);
#endif
        if (deviceType_App() == phone_AppDeviceType) {
            /* Small screen; conserve space by removing the Cancel item. */
            iRelease(removeChild_Widget(clipMenu, lastChild_Widget(clipMenu)));
            iRelease(removeChild_Widget(clipMenu, lastChild_Widget(clipMenu)));
            iRelease(removeChild_Widget(clipMenu, lastChild_Widget(clipMenu)));
        }
        iWidget *splitMenu = makeMenu_Widget(root, (iMenuItem[]){
            { "${menu.split.merge}", '1', 0, "ui.split arg:0" },
            { "${menu.split.swap}", SDLK_x, 0, "ui.split swap:1" },
            { "---" },
            { "${menu.split.horizontal}", '3', 0, "ui.split arg:3 axis:0" },
            { "${menu.split.horizontal} 1:2", SDLK_d, 0, "ui.split arg:1 axis:0" },
            { "${menu.split.horizontal} 2:1", SDLK_e, 0, "ui.split arg:2 axis:0" },
            { "---" },
            { "${menu.split.vertical}", '2', 0, "ui.split arg:3 axis:1" },
            { "${menu.split.vertical} 1:2", SDLK_f, 0, "ui.split arg:1 axis:1" },
            { "${menu.split.vertical} 2:1", SDLK_r, 0, "ui.split arg:2 axis:1" },
        }, 10);
        setFlags_Widget(splitMenu, disabledWhenHidden_WidgetFlag, iTrue); /* enabled when open */
        setId_Widget(tabsMenu, "doctabs.menu");
        setId_Widget(barMenu, "barmenu");
        setId_Widget(clipMenu, "clipmenu");
        setId_Widget(splitMenu, "splitmenu");
    }
    /* Global keyboard shortcuts. */ {
        addAction_Widget(root, SDLK_h, KMOD_PRIMARY | KMOD_SHIFT, "navigate.home");
        addAction_Widget(root, 'l', KMOD_PRIMARY, "navigate.focus");
        addAction_Widget(root, 'f', KMOD_PRIMARY, "focus.set id:find.input");
        addAction_Widget(root, '1', leftSidebarTab_KeyModifier, "sidebar.mode arg:0 toggle:1");
        addAction_Widget(root, '2', leftSidebarTab_KeyModifier, "sidebar.mode arg:1 toggle:1");
        addAction_Widget(root, '3', leftSidebarTab_KeyModifier, "sidebar.mode arg:2 toggle:1");
        addAction_Widget(root, '4', leftSidebarTab_KeyModifier, "sidebar.mode arg:3 toggle:1");
        addAction_Widget(root, '5', leftSidebarTab_KeyModifier, "sidebar.mode arg:4 toggle:1");
        addAction_Widget(root, '1', rightSidebarTab_KeyModifier, "sidebar2.mode arg:0 toggle:1");
        addAction_Widget(root, '2', rightSidebarTab_KeyModifier, "sidebar2.mode arg:1 toggle:1");
        addAction_Widget(root, '3', rightSidebarTab_KeyModifier, "sidebar2.mode arg:2 toggle:1");
        addAction_Widget(root, '4', rightSidebarTab_KeyModifier, "sidebar2.mode arg:3 toggle:1");
        addAction_Widget(root, '5', rightSidebarTab_KeyModifier, "sidebar2.mode arg:4 toggle:1");
        addAction_Widget(root, SDLK_j, KMOD_PRIMARY, "splitmenu.open");
        addAction_Widget(root, SDLK_F10, 0, "menubar.focus");
    }
    updateMetrics_Root(d);
    updateNavBarSize_(navBar);
    if (isLandscapePhone_App()) {
        const float sidebarWidth = width_Widget(root) / (float) gap_UI;
        setWidth_SidebarWidget(findChild_Widget(root, "sidebar"), sidebarWidth);
        //setWidth_SidebarWidget(findChild_Widget(root, "sidebar2"), sidebarWidth);
    }
}

static void setupMovableElements_Root_(iRoot *d) {
    /* The navbar and the tab bar may be move depending on preferences. */
    const iPrefs *prefs = prefs_App();
    iWidget *bottomBar = findChild_Widget(d->widget, "bottombar");
//    iWidget *toolBar   = findChild_Widget(bottomBar, "toolbar");
    iWidget *navBar    = findChild_Widget(d->widget, "navbar");
    iWidget *winBar    = findChild_Widget(d->widget, "winbar"); /* optional: custom window frame */
    iWidget *div       = findChild_Widget(d->widget, "navdiv");
    iWidget *docTabs   = findChild_Widget(d->widget, "doctabs");
    iWidget *tabBar    = findChild_Widget(docTabs, "tabs.buttons");
    iWidget *menuBar   = findChild_Widget(d->widget, "menubar");
    iWidget *navMenu   = findChild_Widget(d->widget, "navbar.menu");
    setFlags_Widget(menuBar, hidden_WidgetFlag, !prefs->menuBar);
    setFlags_Widget(navMenu, hidden_WidgetFlag, prefs->menuBar);
    if (navBar) {
        iChangeFlags(navBar->flags2, permanentVisualOffset_WidgetFlag2, iFalse);
    }
    if (prefs->bottomNavBar) {
        if (deviceType_App() == phone_AppDeviceType) {
            /* When at the bottom, the navbar is at the top of the bottombar, and gets fully hidden
               when the toolbar is hidden. */
            if (parent_Widget(navBar) != bottomBar) {
                removeChild_Widget(navBar->parent, navBar);
                addChildPos_Widget(bottomBar, navBar, front_WidgetAddPos);
                iRelease(navBar);
            }
        }
        else if (navBar) {
            /* On desktop/tablet, a bottom navbar is at the bottom of the main layout. */
            removeChild_Widget(navBar->parent, navBar);
            addChildPos_Widget(div, navBar, back_WidgetAddPos);
            iRelease(navBar);
            /* We'll need to be able to move the input field from under the keyboard. */
            iChangeFlags(navBar->flags2, permanentVisualOffset_WidgetFlag2,
                         deviceType_App() == tablet_AppDeviceType);
        }
    }
    else if (navBar) {
        /* In the top navbar layout, the navbar is always the first (or second) child. */
        removeChild_Widget(navBar->parent, navBar);
        if (winBar) {
            iAssert(indexOfChild_Widget(div, winBar) == 0);
            insertChildAfter_Widget(div, navBar, 1);
        }
        else {
#if defined (LAGRANGE_MENUBAR)
            insertChildAfter_Widget(div, navBar, 0);
#else
            addChildPos_Widget(div, navBar, front_WidgetAddPos);
#endif
        }
        iRelease(navBar);
    }
    if (tabBar) {
        iChangeFlags(tabBar->flags2, permanentVisualOffset_WidgetFlag2, prefs->bottomTabBar);
        /* Tab button frames. */
        iForEach(ObjectList, i, children_Widget(tabBar)) {
            if (isInstance_Object(i.object, &Class_LabelWidget)) {
                setNoTopFrame_LabelWidget(i.object, !prefs->bottomTabBar);
                setNoBottomFrame_LabelWidget(i.object, prefs->bottomTabBar);
            }
        }
        /* Adjust safe area paddings. */
        if (deviceType_App() == tablet_AppDeviceType && prefs->bottomTabBar && !prefs->bottomNavBar) {
            tabBar->padding[3] = bottomSafeInset_Mobile();
        }
        else {
            tabBar->padding[3] = 0;
        }
    }
    setTabBarPosition_Widget(docTabs, prefs->bottomTabBar);
    arrange_Widget(d->widget);
    postRefresh_App();
    postCommand_App("window.resized"); /* not really, but some widgets will update their layout */
}

static void updateBottomBarPosition_(iWidget *bottomBar, iBool animate) {
    if (deviceType_App() != phone_AppDeviceType) {
        return;
    }
    if (focus_Widget() && isInstance_Object(focus_Widget(), &Class_InputWidget)) {
        return;
    }
    const iPrefs *prefs        = prefs_App();
    float         bottomSafe   = 0.0f;
    iWidget      *tabBar       = NULL;
    iRoot        *root         = bottomBar->root;
    iWidget      *docTabs      = findChild_Widget(root->widget, "doctabs");
    iWidget      *toolBar      = findChild_Widget(bottomBar, "toolbar");
    iWidget      *navBar       = findChild_Widget(root->widget, "navbar");
    size_t        numPages     = 0;
    iBool         bottomTabBar = prefs->bottomTabBar;
    if (prefs->bottomTabBar || prefs->bottomNavBar) {
        tabBar = findChild_Widget(docTabs, "tabs.buttons");
        numPages = tabCount_Widget(docTabs);
        if (numPages == 1) {
            bottomTabBar = iFalse; /* it's not visible */
        }
    }
#if defined (iPlatformAppleMobile)
    if (bottomTabBar) {
        safeAreaInsets_iOS(NULL, NULL, NULL, &bottomSafe);
        if (bottomSafe >= gap_UI) {
            bottomSafe -= gap_UI; /* kludge: something's leaving a gap between the tabs and the bottombar */
        }
    }
#endif
    const int   height = height_Widget(bottomBar);
    const iBool shown  = ~flags_Widget(bottomBar) & hidden_WidgetFlag;
    if (shown) {
        setVisualOffset_Widget(bottomBar, 0, 200 * animate, easeOut_AnimFlag);
        if (isPortraitPhone_App()) {
            setVisualOffset_Widget(toolBar, 0, 200 * animate, 0);
        }
        setVisualOffset_Widget(navBar, 0, 200 * animate, 0);
        if (bottomTabBar) {
            /* Tab bar needs to stay visible, too. */
            if (prefs->bottomNavBar || isPortrait_App()) {
                setVisualOffset_Widget(tabBar, -height, 200 * animate, easeOut_AnimFlag);
            }
            else {
                setVisualOffset_Widget(tabBar, -bottomSafe, 200 * animate, easeOut_AnimFlag);
            }
        }
    }
    else {
        /* Close any menus that open via the toolbar. */
        setVisualOffset_Widget(bottomBar, height - bottomSafe, 200 * animate, easeOut_AnimFlag);
        if (bottomTabBar) {
            if (isPortraitPhone_App()) {
                setVisualOffset_Widget(toolBar, bottomSafe, 200 * animate, 0);
            }
            if (prefs->bottomNavBar) {
                setVisualOffset_Widget(navBar, bottomSafe, 200 * animate, 0);
            }
            setVisualOffset_Widget(tabBar, -bottomSafe, 200 * animate, easeOut_AnimFlag);
        }
    }
}

void enableToolbar_Root(iRoot *d, iBool enable) {
    iWidget *bottomBar = findChild_Widget(d->widget, "bottombar");
    iWidget *navBar = findChild_Widget(d->widget, "navbar");
    setFlags_Widget(bottomBar, disabled_WidgetFlag, !enable);
    setFlags_Widget(navBar, disabled_WidgetFlag, !enable);
}

void showToolbar_Root(iRoot *d, iBool show) {
    iWidget *bottomBar = findChild_Widget(d->widget, "bottombar");
    if (!bottomBar) return;
    if (focus_Widget() && isInstance_Object(focus_Widget(), &Class_InputWidget)) {
        /* Don't move anything while text input is active. */
        return;
    }
    const iPrefs *prefs = prefs_App();
    /* The toolbar is only used in the portrait phone layout, but the bottom bar may have other
       elements regardless. The toolbar is needed for clearing the bottom safe area when there
       is a bottom tab bar, even if the URL is at the top. Note that the entire bottom bar may
       be hidden, but the tab bar remains always visible if there are tabs open. */
    if (isLandscape_App() && !prefs->bottomTabBar && !prefs->bottomNavBar) {
        show = iFalse;
    }
    iWidget *toolBar = findChild_Widget(bottomBar, "toolbar");
    if (show) {
        setFlags_Widget(bottomBar, hidden_WidgetFlag, iFalse);
    }
    else {
        if (~flags_Widget(bottomBar) & hidden_WidgetFlag) {
            closeMenu_Widget(findChild_Widget(findWidget_App("toolbar.navmenu"), "menu"));
            closeMenu_Widget(findChild_Widget(bottomBar, "toolbar.menu"));
        }
        setFlags_Widget(bottomBar, hidden_WidgetFlag, iTrue);
    }
    /* The toolbar is only shown when in portrait mode, otherwise buttons are in the navbar. */
    showCollapsed_Widget(toolBar, isPortrait_App());
    updateBottomBarPosition_(bottomBar, iTrue);
}

size_t windowIndex_Root(const iRoot *d) {
    if (type_Window(d->window) == main_WindowType) {
        return windowIndex_App(as_MainWindow(d->window));
    }
    return iInvalidPos;
}

iInt2 size_Root(const iRoot *d) {
    return d && d->widget ? d->widget->rect.size : zero_I2();
}

iRect rect_Root(const iRoot *d) {
    if (d && d->widget) {
        return d->widget->rect;
    }
    return zero_Rect();
}

iRect safeRect_Root(const iRoot *d) {
    iRect rect = rect_Root(d);
#if defined (iPlatformAppleMobile)
    float left, top, right, bottom;
    safeAreaInsets_iOS(&left, &top, &right, &bottom);
    adjustEdges_Rect(&rect, top, -right, -bottom, left);
#endif
    return rect;
}

iRect visibleRect_Root(const iRoot *d) {
    iRect visRect = rect_Root(d);
    float bottom = 0.0f;
#if defined (iPlatformAppleMobile)
    /* TODO: Check this on device... Maybe DisplayUsableBounds would be good here, too? */
    float left, top, right;
    safeAreaInsets_iOS(&left, &top, &right, &bottom);
    visRect.pos.x = (int) left;
    visRect.size.x -= (int) (left + right);
    visRect.pos.y = (int) top;
    visRect.size.y -= (int) (top + bottom);
#endif
#if defined (iPlatformDesktop)
    /* Clamp to the actual window size. */
    visRect = intersect_Rect(visRect, (iRect){ zero_I2(), d->window->size });
    /* Apply the usable bounds of the display. */
    SDL_Rect usable;
    /* TODO: Needs some investigation. With multiple monitors, at least on macOS, the bounds
       returned here seem incorrect sometimes (infrequently). */    
    if (iFalse) {
        const float ratio = d->window->pixelRatio;
        SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(d->window->win), &usable);
        iInt2 winPos;
        SDL_GetWindowPosition(d->window->win, &winPos.x, &winPos.y);
        mulfv_I2(&winPos, ratio);
        usable.x *= ratio;
        usable.y *= ratio;
        usable.w *= ratio;
        usable.h *= ratio;
        /* Make it relative to the window. */
        usable.x -= winPos.x;
        usable.y -= winPos.y;
        visRect = intersect_Rect(visRect, init_Rect(usable.x, usable.y, usable.w, usable.h));        
    }
#endif
    if (get_MainWindow()) {
        const int keyboardHeight = get_MainWindow()->keyboardHeight;
        if (keyboardHeight > bottom) {
            adjustEdges_Rect(&visRect, 0, 0, -keyboardHeight + bottom, 0);
        }
    }
    return visRect;
}
