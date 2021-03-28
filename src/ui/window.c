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

#include "window.h"

#include "defs.h"
#include "labelwidget.h"
#include "inputwidget.h"
#include "documentwidget.h"
#include "sidebarwidget.h"
#include "lookupwidget.h"
#include "bookmarks.h"
#include "embedded.h"
#include "command.h"
#include "paint.h"
#include "util.h"
#include "keys.h"
#include "touch.h"
#include "../app.h"
#include "../visited.h"
#include "../gmcerts.h"
#include "../gmutil.h"
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

#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <the_Foundation/regexp.h>
#include <SDL_hints.h>
#include <SDL_timer.h>
#include <SDL_syswm.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_resize.h"

static iWindow *theWindow_ = NULL;

#if defined (iPlatformApple) || defined (iPlatformLinux)
static float initialUiScale_ = 1.0f;
#else
static float initialUiScale_ = 1.1f;
#endif

iDefineTypeConstructionArgs(Window, (iRect rect), rect)

static iBool handleRootCommands_(iWidget *root, const char *cmd) {
    iUnused(root);
    if (equal_Command(cmd, "menu.open")) {
        iWidget *button = pointer_Command(cmd);
        iWidget *menu = findChild_Widget(button, "menu");
        iAssert(menu);
        if (!isVisible_Widget(menu)) {
            openMenu_Widget(menu, init_I2(0, button->rect.size.y));
        }
        else {
            closeMenu_Widget(menu);
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
        setFocus_Widget(findWidget_App(cstr_Rangecc(range_Command(cmd, "id"))));
        return iTrue;
    }
    else if (equal_Command(cmd, "window.focus.lost")) {
#if !defined (iPlatformMobile) /* apps don't share input focus on mobile */
        setFocus_Widget(NULL);
#endif
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
        const int snap = argLabel_Command(cmd, "snap");
        if (snap) {
            iWindow *window = get_Window();
            iInt2 coord = coord_Command(cmd);
            iInt2 size = init_I2(argLabel_Command(cmd, "width"),
                                 argLabel_Command(cmd, "height"));
            SDL_SetWindowPosition(window->win, coord.x, coord.y);
            SDL_SetWindowSize(window->win, size.x, size.y);
            window->place.snap = snap;
            return iTrue;
        }
    }
    else if (equal_Command(cmd, "window.restore")) {
        setSnap_Window(get_Window(), none_WindowSnap);
        return iTrue;
    }
    else if (equal_Command(cmd, "window.minimize")) {
        SDL_MinimizeWindow(get_Window()->win);
        return iTrue;
    }
    else if (equal_Command(cmd, "window.close")) {
        SDL_PushEvent(&(SDL_Event){ .type = SDL_QUIT });
        return iTrue;
    }
    else if (deviceType_App() == phone_AppDeviceType && equal_Command(cmd, "window.resized")) {
        /* Place the sidebar next to or under doctabs depending on orientation. */
        iSidebarWidget *sidebar = findChild_Widget(root, "sidebar");
        removeChild_Widget(parent_Widget(sidebar), sidebar);
        setButtonFont_SidebarWidget(sidebar, isLandscape_App() ? uiLabel_FontId : uiLabelLarge_FontId);
//        setBackgroundColor_Widget(findChild_Widget(as_Widget(sidebar), "buttons"),
//                                  isPortrait_App() ? uiBackgroundUnfocusedSelection_ColorId
//                                                   : uiBackgroundSidebar_ColorId);
        setFlags_Widget(findChild_Widget(as_Widget(sidebar), "buttons"),
                        borderTop_WidgetFlag,
                        isPortrait_App());
        if (isLandscape_App()) {
            addChildPos_Widget(findChild_Widget(root, "tabs.content"), iClob(sidebar), front_WidgetAddPos);
            setWidth_SidebarWidget(sidebar, 73 * gap_UI);
            if (isVisible_Widget(findWidget_App("sidebar2"))) {
                postCommand_App("sidebar2.toggle");
            }
        }
        else {
            addChildPos_Widget(findChild_Widget(root, "stack"), iClob(sidebar), back_WidgetAddPos);
            setWidth_SidebarWidget(sidebar, width_Widget(root));
        }
        return iFalse;
    }
    else if (handleCommand_App(cmd)) {
        return iTrue;
    }
    return iFalse;
}

/* TODO: Define menus per platform. */

#if defined (iPlatformAppleDesktop)
#  define iHaveNativeMenus
#endif

#if !defined (iHaveNativeMenus)
#if !defined (iPlatformAppleMobile)
/* TODO: Submenus wouldn't hurt here. */
static const iMenuItem navMenuItems_[] = {
    { add_Icon " ${menu.newtab}", 't', KMOD_PRIMARY, "tabs.new" },
    { "${menu.openlocation}", SDLK_l, KMOD_PRIMARY, "navigate.focus" },
    { "---", 0, 0, NULL },
    { download_Icon " " saveToDownloads_Label, SDLK_s, KMOD_PRIMARY, "document.save" },
    { "${menu.copy.source}", SDLK_c, KMOD_PRIMARY, "copy" },
    { "---", 0, 0, NULL },
    { leftHalf_Icon " ${menu.sidebar.left}", SDLK_l, KMOD_PRIMARY | KMOD_SHIFT, "sidebar.toggle" },
    { rightHalf_Icon " ${menu.sidebar.right}", SDLK_p, KMOD_PRIMARY | KMOD_SHIFT, "sidebar2.toggle" },
    { "${menu.zoom.in}", SDLK_EQUALS, KMOD_PRIMARY, "zoom.delta arg:10" },
    { "${menu.zoom.out}", SDLK_MINUS, KMOD_PRIMARY, "zoom.delta arg:-10" },
    { "${menu.zoom.reset}", SDLK_0, KMOD_PRIMARY, "zoom.set arg:100" },
    { "---", 0, 0, NULL },
    { book_Icon " ${menu.bookmarks.list}", 0, 0, "!open url:about:bookmarks" },
    { "${menu.bookmarks.bytag}", 0, 0, "!open url:about:bookmarks?tags" },
    { "${menu.bookmarks.bytime}", 0, 0, "!open url:about:bookmarks?created" },
    { "---", 0, 0, NULL },
    { "${menu.feeds.entrylist}", 0, 0, "!open url:about:feeds" },
    { "---", 0, 0, NULL },
    { gear_Icon " ${menu.preferences}", SDLK_COMMA, KMOD_PRIMARY, "preferences" },
    { "${menu.help}", SDLK_F1, 0, "!open url:about:help" },
    { "${menu.releasenotes}", 0, 0, "!open url:about:version" },
    { "---", 0, 0, NULL },
    { "${menu.quit}", 'q', KMOD_PRIMARY, "quit" }
};
#else
/* Tablet menu. */
static const iMenuItem tabletNavMenuItems_[] = {
    { add_Icon " ${menu.opentab}", 't', KMOD_PRIMARY, "tabs.new" },
    { close_Icon " ${menu.closetab}", 'w', KMOD_PRIMARY, "tabs.close" },
    { "---", 0, 0, NULL },
    { magnifyingGlass_Icon " ${menu.find}", 0, 0, "focus.set id:find.input" },
    { leftHalf_Icon " ${menu.sidebar.left}", SDLK_l, KMOD_PRIMARY | KMOD_SHIFT, "sidebar.toggle" },
    { rightHalf_Icon " ${menu.sidebar.right}", SDLK_p, KMOD_PRIMARY | KMOD_SHIFT, "sidebar2.toggle" },
    { "${menu.zoom.in}", SDLK_EQUALS, KMOD_PRIMARY, "zoom.delta arg:10" },
    { "${menu.zoom.out}", SDLK_MINUS, KMOD_PRIMARY, "zoom.delta arg:-10" },
    { "${menu.zoom.reset}", SDLK_0, KMOD_PRIMARY, "zoom.set arg:100" },
    { "---", 0, 0, NULL },
    { book_Icon " ${menu.bookmarks.list}", 0, 0, "!open url:about:bookmarks" },
    { "${menu.bookmarks.bytag}", 0, 0, "!open url:about:bookmarks?tags" },
    { "${menu.bookmarks.bydate}", 0, 0, "!open url:about:feeds" },
    { "---", 0, 0, NULL },
    { gear_Icon " ${menu.preferences}", SDLK_COMMA, KMOD_PRIMARY, "preferences" },
    { "${menu.help}", SDLK_F1, 0, "!open url:about:help" },
    { "${menu.releasenotes}", 0, 0, "!open url:about:version" },
};

/* Phone menu. */
static const iMenuItem phoneNavMenuItems_[] = {
    { add_Icon " ${menu.opentab}", 't', KMOD_PRIMARY, "tabs.new" },
    { close_Icon " ${menu.closetab}", 'w', KMOD_PRIMARY, "tabs.close" },
    { "---", 0, 0, NULL },
    { magnifyingGlass_Icon " ${menu.find}", 0, 0, "focus.set id:find.input" },
    { leftHalf_Icon " ${menu.sidebar}", SDLK_l, KMOD_PRIMARY | KMOD_SHIFT, "sidebar.toggle" },
    { "${menu.zoom.in}", SDLK_EQUALS, KMOD_PRIMARY, "zoom.delta arg:10" },
    { "${menu.zoom.out}", SDLK_MINUS, KMOD_PRIMARY, "zoom.delta arg:-10" },
    { "${menu.zoom.reset}", SDLK_0, KMOD_PRIMARY, "zoom.set arg:100" },
    { "---", 0, 0, NULL },
    { book_Icon " ${menu.bookmarks.list}", 0, 0, "!open url:about:bookmarks" },
    { "${menu.feeds.entrylist}", 0, 0, "!open url:about:feeds" },
    { "---", 0, 0, NULL },
    { gear_Icon " Settings...", SDLK_COMMA, KMOD_PRIMARY, "preferences" },
};
#endif /* AppleMobile */
#endif

#if defined (iHaveNativeMenus)
/* Using native menus. */
static const iMenuItem fileMenuItems_[] = {
    { "${menu.newtab}", SDLK_t, KMOD_PRIMARY, "tabs.new" },
    { "${menu.openlocation}", SDLK_l, KMOD_PRIMARY, "navigate.focus" },
    { "---", 0, 0, NULL },
    { saveToDownloads_Label, SDLK_s, KMOD_PRIMARY, "document.save" },
};

static const iMenuItem editMenuItems_[] = {
    { "${menu.cut}", SDLK_x, KMOD_PRIMARY, "input.copy cut:1" },
    { "${menu.copy}", SDLK_c, KMOD_PRIMARY, "copy" },
    { "${menu.paste}", SDLK_v, KMOD_PRIMARY, "input.paste" },
    { "---", 0, 0, NULL },
    { "${menu.copy.pagelink}", SDLK_c, KMOD_PRIMARY | KMOD_SHIFT, "document.copylink" },
    { "---", 0, 0, NULL },
    { "${macos.menu.find}", SDLK_f, KMOD_PRIMARY, "focus.set id:find.input" },
};

static const iMenuItem viewMenuItems_[] = {
    { "${menu.show.bookmarks}", '1', KMOD_PRIMARY, "sidebar.mode arg:0 toggle:1" },
    { "${menu.show.feeds}", '2', KMOD_PRIMARY, "sidebar.mode arg:1 toggle:1" },
    { "${menu.show.history}", '3', KMOD_PRIMARY, "sidebar.mode arg:2 toggle:1" },
    { "${menu.show.identities}", '4', KMOD_PRIMARY, "sidebar.mode arg:3 toggle:1" },
    { "${menu.show.outline}", '5', KMOD_PRIMARY, "sidebar.mode arg:4 toggle:1" },
    { "${menu.sidebar.left}", SDLK_l, KMOD_PRIMARY | KMOD_SHIFT, "sidebar.toggle" },
    { "${menu.sidebar.right}", SDLK_p, KMOD_PRIMARY | KMOD_SHIFT, "sidebar2.toggle" },
    { "---", 0, 0, NULL },
    { "${menu.back}", SDLK_LEFTBRACKET, KMOD_PRIMARY, "navigate.back" },
    { "${menu.forward}", SDLK_RIGHTBRACKET, KMOD_PRIMARY, "navigate.forward" },
    { "${menu.parent}", navigateParent_KeyShortcut, "navigate.parent" },
    { "${menu.root}", navigateRoot_KeyShortcut, "navigate.root" },
    { "${menu.reload}", reload_KeyShortcut, "navigate.reload" },
    { "---", 0, 0, NULL },
    { "${menu.zoom.in}", SDLK_EQUALS, KMOD_PRIMARY, "zoom.delta arg:10" },
    { "${menu.zoom.out}", SDLK_MINUS, KMOD_PRIMARY, "zoom.delta arg:-10" },
    { "${menu.zoom.reset}", SDLK_0, KMOD_PRIMARY, "zoom.set arg:100" },
};

static iMenuItem bookmarksMenuItems_[] = {
    { "${menu.page.bookmark}", SDLK_d, KMOD_PRIMARY, "bookmark.add" },
    { "${menu.page.subscribe}", subscribeToPage_KeyModifier, "feeds.subscribe" },
    { "---", 0, 0, NULL },
    { "${menu.import.links}", 0, 0, "bookmark.links confirm:1" },
    { "---", 0, 0, NULL },
    { "${macos.menu.bookmarks.list}", 0, 0, "open url:about:bookmarks" },
    { "${macos.menu.bookmarks.bytag}", 0, 0, "open url:about:bookmarks?tags" },
    { "${macos.menu.bookmarks.bytime}", 0, 0, "open url:about:bookmarks?created" },
    { "${menu.feeds.entrylist}", 0, 0, "open url:about:feeds" },
    { "---", 0, 0, NULL },
    { "${menu.bookmarks.refresh}", 0, 0, "bookmarks.reload.remote" },
    { "${menu.feeds.refresh}", SDLK_r, KMOD_PRIMARY | KMOD_SHIFT, "feeds.refresh" },
};

static const iMenuItem identityMenuItems_[] = {
    { "${menu.identity.new}", SDLK_n, KMOD_PRIMARY | KMOD_SHIFT, "ident.new" },
    { "---", 0, 0, NULL },
    { "${menu.identity.import}", SDLK_i, KMOD_PRIMARY | KMOD_SHIFT, "ident.import" },
};

static const iMenuItem helpMenuItems_[] = {
    { "${menu.help}", 0, 0, "!open url:about:help" },
    { "${menu.releasenotes}", 0, 0, "!open url:about:version" },
    { "---", 0, 0, NULL },
    { "${menu.aboutpages}", 0, 0, "!open url:about:about" },
    { "${menu.debug}", 0, 0, "!open url:about:debug" },
};

static void insertMacMenus_(void) {
    insertMenuItems_MacOS("${menu.title.file}", 1, fileMenuItems_, iElemCount(fileMenuItems_));
    insertMenuItems_MacOS("${menu.title.edit}", 2, editMenuItems_, iElemCount(editMenuItems_));
    insertMenuItems_MacOS("${menu.title.view}", 3, viewMenuItems_, iElemCount(viewMenuItems_));
    insertMenuItems_MacOS("${menu.title.bookmarks}", 4, bookmarksMenuItems_, iElemCount(bookmarksMenuItems_));
    insertMenuItems_MacOS("${menu.title.identity}", 5, identityMenuItems_, iElemCount(identityMenuItems_));
    insertMenuItems_MacOS("${menu.title.help}", 7, helpMenuItems_, iElemCount(helpMenuItems_));
}

static void removeMacMenus_(void) {
    removeMenu_MacOS(7);
    removeMenu_MacOS(5);
    removeMenu_MacOS(4);
    removeMenu_MacOS(3);
    removeMenu_MacOS(2);
    removeMenu_MacOS(1);
}
#endif

#if defined (iPlatformAppleMobile)
static const iMenuItem identityButtonMenuItems_[] = {
    { "No Active Identity", 0, 0, "ident.showactive" },
    { "---", 0, 0, NULL },
    { add_Icon " New Identity...", SDLK_n, KMOD_PRIMARY | KMOD_SHIFT, "ident.new" },
    { "Import...", SDLK_i, KMOD_PRIMARY | KMOD_SHIFT, "ident.import" },
    { "---", 0, 0, NULL },
    { person_Icon " Show Identities", 0, 0, "toolbar.showident" },
};
#else /* desktop */
static const iMenuItem identityButtonMenuItems_[] = {
    { "${menu.identity.notactive}", 0, 0, "ident.showactive" },
    { "---", 0, 0, NULL },
#   if !defined (iHaveNativeMenus)
    { add_Icon " ${menu.identity.new}", SDLK_n, KMOD_PRIMARY | KMOD_SHIFT, "ident.new" },
    { "${menu.identity.import}", SDLK_i, KMOD_PRIMARY | KMOD_SHIFT, "ident.import" },
    { "---", 0, 0, NULL },
    { person_Icon " ${menu.show.identities}", '4', KMOD_PRIMARY, "sidebar.mode arg:3 show:1" },
#   else
    { add_Icon " ${menu.identity.new}", 0, 0, "ident.new" },
    { "---", 0, 0, NULL },
    { person_Icon " ${menu.show.identities}", 0, 0, "sidebar.mode arg:3 show:1" },
#endif
};
#endif

static const char *reloadCStr_ = reload_Icon;

/* TODO: A preference for these, maybe? */
static const char *stopSeqCStr_[] = {
    /* Corners */
    uiTextCaution_ColorEscape "\U0000230c",
    uiTextCaution_ColorEscape "\U0000230d",
    uiTextCaution_ColorEscape "\U0000230f",
    uiTextCaution_ColorEscape "\U0000230e",
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

static void updateNavBarIdentity_(iWidget *navBar) {
    const iGmIdentity *ident =
        identityForUrl_GmCerts(certs_App(), url_DocumentWidget(document_App()));
    iWidget *button = findChild_Widget(navBar, "navbar.ident");
    iWidget *tool = findWidget_App("toolbar.ident");
    setFlags_Widget(button, selected_WidgetFlag, ident != NULL);
    setFlags_Widget(tool, selected_WidgetFlag, ident != NULL);
    /* Update menu. */
    iLabelWidget *idItem = child_Widget(findChild_Widget(button, "menu"), 0);
    setTextCStr_LabelWidget(
        idItem,
        ident ? format_CStr(uiTextAction_ColorEscape "%s",
                            cstrCollect_String(subject_TlsCertificate(ident->cert)))
              : "${menu.identity.notactive}");
    setFlags_Widget(as_Widget(idItem), disabled_WidgetFlag, !ident);
}

static const int loadAnimIntervalMs_ = 133;
static int       loadAnimIndex_      = 0;

static const char *loadAnimationCStr_(void) {
    return stopSeqCStr_[loadAnimIndex_ % iElemCount(stopSeqCStr_)];
}

static uint32_t updateReloadAnimation_Window_(uint32_t interval, void *window) {
    iUnused(window);
    loadAnimIndex_++;
    postCommand_App("window.reload.update");
    return interval;
}

static void setReloadLabel_Window_(iWindow *d, iBool animating) {
    iLabelWidget *label = findChild_Widget(d->root, "reload");
    updateTextCStr_LabelWidget(label, animating ? loadAnimationCStr_() : reloadCStr_);
}

static void checkLoadAnimation_Window_(iWindow *d) {
    const iBool isOngoing = isRequestOngoing_DocumentWidget(document_App());
    if (isOngoing && !d->loadAnimTimer) {
        d->loadAnimTimer = SDL_AddTimer(loadAnimIntervalMs_, updateReloadAnimation_Window_, d);
    }
    else if (!isOngoing && d->loadAnimTimer) {
        SDL_RemoveTimer(d->loadAnimTimer);
        d->loadAnimTimer = 0;
    }
    setReloadLabel_Window_(d, isOngoing);
}

static void updatePadding_Window_(iWindow *d) {
#if defined (iPlatformAppleMobile)
    /* Respect the safe area insets. */ {
        float left, top, right, bottom;
        safeAreaInsets_iOS(&left, &top, &right, &bottom);
        setPadding_Widget(findChild_Widget(d->root, "navdiv"), left, top, right, 0);
        iWidget *toolBar = findChild_Widget(d->root, "toolbar");
        if (toolBar) {
            setPadding_Widget(toolBar, left, 0, right, bottom);
        }
    }
    /* Note that `handleNavBarCommands_` also adjusts padding and spacing. */
#endif
}

void dismissPortraitPhoneSidebars_Window(iWindow *d) {
    if (deviceType_App() == phone_AppDeviceType && isPortrait_App()) {
        iWidget *sidebar = findWidget_App("sidebar");
        iWidget *sidebar2 = findWidget_App("sidebar2");
        if (isVisible_Widget(sidebar)) {
            postCommand_App("sidebar.toggle");
            setVisualOffset_Widget(sidebar, height_Widget(sidebar), 250, easeIn_AnimFlag);
        }
        if (isVisible_Widget(sidebar2)) {
            postCommand_App("sidebar2.toggle");
            setVisualOffset_Widget(sidebar2, height_Widget(sidebar2), 250, easeIn_AnimFlag);
        }
//        setFlags_Widget(findWidget_App("toolbar.ident"), noBackground_WidgetFlag, iTrue);
//        setFlags_Widget(findWidget_App("toolbar.view"), noBackground_WidgetFlag, iTrue);
    }
}

static iBool willPerformSearchQuery_(const iString *userInput) {
    const iString *clean = collect_String(trimmed_String(userInput));
    if (isEmpty_String(clean)) {
        return iFalse;
    }
    return !isEmpty_String(&prefs_App()->searchUrl) && !isLikelyUrl_String(userInput);
}

static void showSearchQueryIndicator_(iBool show) {
    iWidget *indicator = findWidget_App("input.indicator.search");
    showCollapsed_Widget(indicator, show);
    iAssert(isInstance_Object(parent_Widget(parent_Widget(indicator)), &Class_InputWidget));
    iInputWidget *url = (iInputWidget *) parent_Widget(parent_Widget(indicator));
    setContentPadding_InputWidget(url, -1, contentPadding_InputWidget(url).left +
                                  (show ? width_Widget(indicator) : 0));
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

iBool isNarrow_Window(const iWindow *d) {
    return width_Rect(safeRootRect_Window(d)) / gap_UI < 140;
}

static iBool handleNavBarCommands_(iWidget *navBar, const char *cmd) {
    if (equal_Command(cmd, "window.resized") || equal_Command(cmd, "metrics.changed")) {
        const iBool isPhone = deviceType_App() == phone_AppDeviceType;
        const iBool isNarrow = !isPhone && isNarrow_Window(get_Window());
        /* Adjust navbar padding. */ {
            int hPad = isPhone && isPortrait_App() ? 0 : (isPhone || isNarrow) ? gap_UI / 2
                                                                               : gap_UI * 3 / 2;
            int vPad = gap_UI * 3 / 2;
            int topPad = !findWidget_App("winbar") ? gap_UI / 2 : 0;
            setPadding_Widget(navBar, hPad, vPad / 3 + topPad, hPad, vPad / 2);
        }
        /* Button sizing. */
        if (isNarrow ^ ((flags_Widget(navBar) & tight_WidgetFlag) != 0)) {
            setFlags_Widget(navBar, tight_WidgetFlag, isNarrow);
            iForEach(ObjectList, i, navBar->children) {
                iWidget *child = as_Widget(i.object);
                setFlags_Widget(child, tight_WidgetFlag, isNarrow);
                if (isInstance_Object(i.object, &Class_LabelWidget)) {
                    iLabelWidget *label = i.object;
                    updateSize_LabelWidget(label);
                }
            }
            /* Note that InputWidget uses the `tight` flag to adjust its inner padding. */
            /* TODO: Is this redundant? See `updateMetrics_Window_()`. */
            const int embedButtonWidth = width_Widget(findChild_Widget(navBar, "navbar.lock"));
            setContentPadding_InputWidget(findChild_Widget(navBar, "url"),
                                          embedButtonWidth * 0.75f,
                                          embedButtonWidth * 0.75f);
        }
        if (isPhone) {
            static const char *buttons[] = { "navbar.back",  "navbar.forward", "navbar.sidebar",
                                             "navbar.ident", "navbar.home",    "navbar.menu" };
            iWidget *toolBar = findWidget_App("toolbar");
            setVisualOffset_Widget(toolBar, 0, 0, 0);
            setFlags_Widget(toolBar, hidden_WidgetFlag, isLandscape_App());
            iForIndices(i, buttons) {
                iLabelWidget *btn = findChild_Widget(navBar, buttons[i]);
                setFlags_Widget(as_Widget(btn), hidden_WidgetFlag, isPortrait_App());
                if (isLandscape_App()) {
                    /* Collapsing sets size to zero and the label doesn't know when to update
                       its own size automatically. */
                    updateSize_LabelWidget(btn);
                }
            }
            arrange_Widget(get_Window()->root);
        }
        /* Resize the URL input field. */ {
            iWidget *urlBar = findChild_Widget(navBar, "url");
            urlBar->rect.size.x = iMini(navBarAvailableSpace_(navBar), 167 * gap_UI);
            arrange_Widget(navBar);
        }
        refresh_Widget(navBar);
        postCommand_Widget(navBar, "layout.changed id:navbar");
        return iFalse;
    }
    else if (equal_Command(cmd, "window.reload.update")) {
        checkLoadAnimation_Window_(get_Window());
        return iTrue;
    }
    else if (equal_Command(cmd, "navigate.focus")) {
        iWidget *url = findChild_Widget(navBar, "url");
        if (focus_Widget() != url) {
            setFocus_Widget(findChild_Widget(navBar, "url"));
        }
        else {
            selectAll_InputWidget((iInputWidget *) url);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "input.edited")) {
        iAnyObject *   url  = findChild_Widget(navBar, "url");
        const iString *text = text_InputWidget(url);
        const iBool show = willPerformSearchQuery_(text);
        showSearchQueryIndicator_(show);
        if (pointer_Command(cmd) == url) {
            submit_LookupWidget(findWidget_App("lookup"), text);
            return iTrue;
        }
    }
    else if (startsWith_CStr(cmd, "input.ended id:url ")) {
        iInputWidget *url = findChild_Widget(navBar, "url");
        showSearchQueryIndicator_(iFalse);
        if (isEmpty_String(text_InputWidget(url))) {
            /* User entered nothing; restore the current URL. */
            setText_InputWidget(url, url_DocumentWidget(document_App()));
            return iTrue;
        }
        if (arg_Command(cmd) && argLabel_Command(cmd, "enter") &&
            !isFocused_Widget(findWidget_App("lookup"))) {
            iString *newUrl = copy_String(text_InputWidget(url));
            trim_String(newUrl);
            if (willPerformSearchQuery_(newUrl)) {
                postCommandf_App("open url:%s", cstr_String(searchQueryUrl_App(newUrl)));
            }
            else {
                postCommandf_App(
                    "open url:%s",
                    cstr_String(absoluteUrl_String(&iStringLiteral(""), collect_String(newUrl))));
            }
            return iTrue;
        }
    }
    else if (startsWith_CStr(cmd, "document.")) {
        /* React to the current document only. */
        if (document_Command(cmd) == document_App()) {
            if (equal_Command(cmd, "document.changed")) {
                iInputWidget *url = findWidget_App("url");
                const iString *urlStr = collect_String(suffix_Command(cmd, "url"));
                trimCache_App();
                visitUrl_Visited(visited_App(), withSpacesEncoded_String(urlStr), 0); /* TODO: internal URI normalization */
                postCommand_App("visited.changed"); /* sidebar will update */
                setText_InputWidget(url, urlStr);
                checkLoadAnimation_Window_(get_Window());
                dismissPortraitPhoneSidebars_Window(get_Window());
                updateNavBarIdentity_(navBar);
                /* Icon updates should be limited to automatically chosen icons if the user
                   is allowed to pick their own in the future. */
                if (updateBookmarkIcon_Bookmarks(bookmarks_App(), urlStr,
                        siteIcon_GmDocument(document_DocumentWidget(document_App())))) {
                    postCommand_App("bookmarks.changed");
                }
                return iFalse;
            }
            else if (equal_Command(cmd, "document.request.cancelled")) {
                checkLoadAnimation_Window_(get_Window());
                return iFalse;
            }
            else if (equal_Command(cmd, "document.request.started")) {
                iInputWidget *url = findChild_Widget(navBar, "url");
                setTextCStr_InputWidget(url, suffixPtr_Command(cmd, "url"));
                checkLoadAnimation_Window_(get_Window());
                dismissPortraitPhoneSidebars_Window(get_Window());
                return iFalse;
            }
        }
    }
    else if (equal_Command(cmd, "tabs.changed")) {
        /* Update navbar according to the current tab. */
        iDocumentWidget *doc = document_App();
        if (doc) {
            setText_InputWidget(findChild_Widget(navBar, "url"), url_DocumentWidget(doc));
            checkLoadAnimation_Window_(get_Window());
            updateNavBarIdentity_(navBar);
        }
        setFocus_Widget(NULL);
    }
    else if (equal_Command(cmd, "mouse.clicked") && arg_Command(cmd)) {
        iWidget *widget = pointer_Command(cmd);
        iWidget *menu = findWidget_App("doctabs.menu");
        if (isTabButton_Widget(widget)) {
            if (!isVisible_Widget(menu)) {
                iWidget *tabs = findWidget_App("doctabs");
                iWidget *page = tabPage_Widget(tabs, childIndex_Widget(widget->parent, widget));
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
    return iFalse;
}

static iBool handleSearchBarCommands_(iWidget *searchBar, const char *cmd) {
    if (equal_Command(cmd, "input.ended") &&
        equal_Rangecc(range_Command(cmd, "id"), "find.input")) {
        iInputWidget *input = findChild_Widget(searchBar, "find.input");
        if (arg_Command(cmd) && argLabel_Command(cmd, "enter") && isVisible_Widget(input)) {
            postCommand_App("find.next");
            /* Keep focus when pressing Enter. */
            if (!isEmpty_String(text_InputWidget(input))) {
                postCommand_App("focus.set id:find.input");
            }
        }
        else {
            postCommand_App("find.clearmark");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "focus.gained")) {
        if (pointer_Command(cmd) == findChild_Widget(searchBar, "find.input")) {
            if (!isVisible_Widget(searchBar)) {
                setFlags_Widget(searchBar, hidden_WidgetFlag | disabled_WidgetFlag, iFalse);
                arrange_Widget(get_Window()->root);
                postRefresh_App();
            }
        }
    }
    else if (equal_Command(cmd, "find.close")) {
        if (isVisible_Widget(searchBar)) {
            setFlags_Widget(searchBar, hidden_WidgetFlag | disabled_WidgetFlag, iTrue);
            arrange_Widget(searchBar->parent);
            if (isFocused_Widget(findChild_Widget(searchBar, "find.input"))) {
                setFocus_Widget(NULL);
            }
            refresh_Widget(searchBar->parent);
        }
        return iTrue;
    }
    return iFalse;
}

#if defined (iPlatformAppleMobile)
static void dismissSidebar_(iWidget *sidebar, const char *toolButtonId) {
    if (isVisible_Widget(sidebar)) {
        postCommandf_App("%s.toggle", cstr_String(id_Widget(sidebar)));
        if (toolButtonId) {
//            setFlags_Widget(findWidget_App(toolButtonId), noBackground_WidgetFlag, iTrue);
        }
        setVisualOffset_Widget(sidebar, height_Widget(sidebar), 250, easeIn_AnimFlag);
    }
}

static iBool handleToolBarCommands_(iWidget *toolBar, const char *cmd) {
    if (equalWidget_Command(cmd, toolBar, "mouse.clicked") && arg_Command(cmd) &&
        argLabel_Command(cmd, "button") == SDL_BUTTON_RIGHT) {
        iWidget *menu = findChild_Widget(toolBar, "toolbar.menu");
        arrange_Widget(menu);
        openMenu_Widget(menu, init_I2(0, -height_Widget(menu)));
        return iTrue;
    }
    else if (equal_Command(cmd, "toolbar.showview")) {
        /* TODO: Clean this up. */
        iWidget *sidebar  = findWidget_App("sidebar");
        iWidget *sidebar2 = findWidget_App("sidebar2");
        dismissSidebar_(sidebar2, "toolbar.ident");
        const iBool isVisible = isVisible_Widget(sidebar);
//        setFlags_Widget(findChild_Widget(toolBar, "toolbar.view"), noBackground_WidgetFlag,
//                        isVisible);
        /* If a sidebar hasn't been shown yet, it's height is zero. */
        const int viewHeight = rootSize_Window(get_Window()).y;
        if (arg_Command(cmd) >= 0) {
            postCommandf_App("sidebar.mode arg:%d show:1", arg_Command(cmd));
            if (!isVisible) {
                setVisualOffset_Widget(sidebar, viewHeight, 0, 0);
                setVisualOffset_Widget(sidebar, 0, 400, easeOut_AnimFlag | softer_AnimFlag);
            }
        }
        else {
            postCommandf_App("sidebar.toggle");
            if (isVisible) {
                setVisualOffset_Widget(sidebar, height_Widget(sidebar), 250, easeIn_AnimFlag);
            }
            else {
                setVisualOffset_Widget(sidebar, viewHeight, 0, 0);
                setVisualOffset_Widget(sidebar, 0, 400, easeOut_AnimFlag | softer_AnimFlag);
            }
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "toolbar.showident")) {
        /* TODO: Clean this up. */
        iWidget *sidebar  = findWidget_App("sidebar");
        iWidget *sidebar2 = findWidget_App("sidebar2");
        dismissSidebar_(sidebar, "toolbar.view");
        const iBool isVisible = isVisible_Widget(sidebar2);
//        setFlags_Widget(findChild_Widget(toolBar, "toolbar.ident"), noBackground_WidgetFlag,
//                        isVisible);
        /* If a sidebar hasn't been shown yet, it's height is zero. */
        const int viewHeight = rootSize_Window(get_Window()).y;
        if (isVisible) {
            dismissSidebar_(sidebar2, NULL);
        }
        else {
            postCommand_App("sidebar2.mode arg:3 show:1");
            int offset = height_Widget(sidebar2);
            if (offset == 0) offset = rootSize_Window(get_Window()).y;
            setVisualOffset_Widget(sidebar2, offset, 0, 0);
            setVisualOffset_Widget(sidebar2, 0, 400, easeOut_AnimFlag | softer_AnimFlag);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "sidebar.mode.changed")) {
        iLabelWidget *viewTool = findChild_Widget(toolBar, "toolbar.view");
        updateTextCStr_LabelWidget(viewTool, icon_SidebarMode(arg_Command(cmd)));
        return iFalse;
    }
    return iFalse;
}
#endif /* defined (iPlatformAppleMobile) */

static iLabelWidget *newLargeIcon_LabelWidget(const char *text, const char *cmd) {
    iLabelWidget *lab = newIcon_LabelWidget(text, 0, 0, cmd);
    setFont_LabelWidget(lab, uiLabelLarge_FontId);
    return lab;
}

static int appIconSize_(void) {
    return lineHeight_Text(uiContent_FontId);
}

static void updateMetrics_Window_(iWindow *d) {
    /* Custom frame. */
    iWidget *winBar = findChild_Widget(d->root, "winbar");
    if (winBar) {
        iWidget *appIcon  = findChild_Widget(winBar, "winbar.icon");
        iWidget *appTitle = findChild_Widget(winBar, "winbar.title");
        iWidget *appMin   = findChild_Widget(winBar, "winbar.min");
        iWidget *appMax   = findChild_Widget(winBar, "winbar.max");
        iWidget *appClose = findChild_Widget(winBar, "winbar.close");
        setPadding_Widget(winBar, 0, gap_UI / 3, 0, 0);
        setSize_Widget(appMin, init_I2(gap_UI * 11.5f, height_Widget(appTitle)));
        setSize_Widget(appMax, appMin->rect.size);
        setSize_Widget(appClose, appMin->rect.size);
        setSize_Widget(appIcon, init_I2(appIconSize_(), appMin->rect.size.y));
    }
    iWidget *navBar     = findChild_Widget(d->root, "navbar");
    iWidget *lock       = findChild_Widget(navBar, "navbar.lock");
    iWidget *url        = findChild_Widget(d->root, "url");
    iWidget *rightEmbed = findChild_Widget(navBar, "url.rightembed");
    iWidget *embedPad   = findChild_Widget(navBar, "url.embedpad");
    setPadding_Widget(as_Widget(url), 0, gap_UI, 0, gap_UI);
    navBar->rect.size.y = 0; /* recalculate height based on children (FIXME: shouldn't be needed) */
    updateSize_LabelWidget((iLabelWidget *) lock);
    setSize_Widget(embedPad, init_I2(width_Widget(lock) + gap_UI / 2, 1));
    setContentPadding_InputWidget((iInputWidget *) url, width_Widget(lock) * 0.75,
                                  width_Widget(lock) * 0.75);
    rightEmbed->rect.pos.y = gap_UI;
    updatePadding_Window_(d);
    arrange_Widget(d->root);
    postRefresh_App();
}

static void setupUserInterface_Window(iWindow *d) {
#if defined (iPlatformMobile)
    const iBool isPhone = (deviceType_App() == phone_AppDeviceType);
#endif
    /* Children of root cover the entire window. */
    setFlags_Widget(d->root, resizeChildren_WidgetFlag, iTrue);
    setCommandHandler_Widget(d->root, handleRootCommands_);

    iWidget *div = makeVDiv_Widget();
    setId_Widget(div, "navdiv");
    addChild_Widget(d->root, iClob(div));

#if defined (LAGRANGE_CUSTOM_FRAME)
    /* Window title bar. */
    if (prefs_App()->customFrame) {
        setPadding1_Widget(div, 1);
        iWidget *winBar = new_Widget();
        setId_Widget(winBar, "winbar");
        setFlags_Widget(winBar,
                        arrangeHeight_WidgetFlag | resizeChildren_WidgetFlag |
                            arrangeHorizontal_WidgetFlag | collapse_WidgetFlag,
                        iTrue);
        iWidget *appIcon;
        setId_Widget(
            addChild_Widget(winBar, iClob(appIcon = makePadding_Widget(0))), "winbar.icon");
        iLabelWidget *appButton =
            addChildFlags_Widget(winBar,
                                 iClob(new_LabelWidget("Lagrange", NULL)),
                                 fixedHeight_WidgetFlag | frameless_WidgetFlag);
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
                         frameless_WidgetFlag),
                     "winbar.min");
        addChildFlags_Widget(
            winBar,
            iClob(appMax = newLargeIcon_LabelWidget("\u25a1", "window.maximize toggle:1")),
            frameless_WidgetFlag);
        setId_Widget(as_Widget(appMax), "winbar.max");
        addChildFlags_Widget(winBar,
                             iClob(appClose = newLargeIcon_LabelWidget(close_Icon, "window.close")),
                             frameless_WidgetFlag);
        setId_Widget(as_Widget(appClose), "winbar.close");
        setFont_LabelWidget(appClose, uiContent_FontId);
        addChild_Widget(div, iClob(winBar));
        setBackgroundColor_Widget(winBar, uiBackground_ColorId);
    }
#endif
    /* Navigation bar. */ {
        iWidget *navBar = new_Widget();
        setId_Widget(navBar, "navbar");
        setFlags_Widget(navBar,
                        hittable_WidgetFlag | /* context menu */
                        arrangeHeight_WidgetFlag |
                        resizeChildren_WidgetFlag |
                        arrangeHorizontal_WidgetFlag |
                        drawBackgroundToHorizontalSafeArea_WidgetFlag |
                        drawBackgroundToVerticalSafeArea_WidgetFlag,
                        iTrue);
        addChild_Widget(div, iClob(navBar));
        setBackgroundColor_Widget(navBar, uiBackground_ColorId);
        setCommandHandler_Widget(navBar, handleNavBarCommands_);
        setId_Widget(addChildFlags_Widget(navBar, iClob(newIcon_LabelWidget(backArrow_Icon, 0, 0, "navigate.back")), collapse_WidgetFlag), "navbar.back");
        setId_Widget(addChildFlags_Widget(navBar, iClob(newIcon_LabelWidget(forwardArrow_Icon, 0, 0, "navigate.forward")), collapse_WidgetFlag), "navbar.forward");
        /* Mobile devices have a button for easier access to the left sidebar. */
        if (deviceType_App() != desktop_AppDeviceType) {
            setId_Widget(addChildFlags_Widget(
                             navBar,
                             iClob(newIcon_LabelWidget(leftHalf_Icon, 0, 0, "sidebar.toggle")),
                             collapse_WidgetFlag),
                         "navbar.sidebar");
        }
        addChildFlags_Widget(navBar, iClob(new_Widget()), expand_WidgetFlag);
        iLabelWidget *idMenu = makeMenuButton_LabelWidget(
            "\U0001f464", identityButtonMenuItems_, iElemCount(identityButtonMenuItems_));
        setAlignVisually_LabelWidget(idMenu, iTrue);
        setId_Widget(addChildFlags_Widget(navBar, iClob(idMenu), collapse_WidgetFlag), "navbar.ident");
        iInputWidget *url;
        /* URL input field. */ {
            url = new_InputWidget(0);
            setFlags_Widget(as_Widget(url), resizeHeightOfChildren_WidgetFlag, iTrue);
            setSelectAllOnFocus_InputWidget(url, iTrue);
            setId_Widget(as_Widget(url), "url");
            setUrlContent_InputWidget(url, iTrue);
            setNotifyEdits_InputWidget(url, iTrue);
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
                setFont_LabelWidget(lock, symbols_FontId + uiNormal_FontSize);
                updateTextCStr_LabelWidget(lock, "\U0001f512");
            }
            iWidget *rightEmbed = new_Widget();
            setId_Widget(rightEmbed, "url.rightembed");
            addChildFlags_Widget(as_Widget(url),
                                 iClob(rightEmbed),
                                 arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag |
                                     resizeHeightOfChildren_WidgetFlag |
                                     moveToParentRightEdge_WidgetFlag);
            /* Feeds refresh indicator is inside the input field. */ {
                iLabelWidget *queryInd =
                    new_LabelWidget(uiTextAction_ColorEscape "\u21d2 ${status.query}", NULL);
                setId_Widget(as_Widget(queryInd), "input.indicator.search");
                setBackgroundColor_Widget(as_Widget(queryInd), uiBackground_ColorId);
                setFrameColor_Widget(as_Widget(queryInd), uiTextAction_ColorId);
                setAlignVisually_LabelWidget(queryInd, iTrue);
                addChildFlags_Widget(rightEmbed,
                                     iClob(queryInd),
                                     collapse_WidgetFlag | hidden_WidgetFlag);
            }
            /* Feeds refresh indicator is inside the input field. */ {
                iLabelWidget *fprog = new_LabelWidget(uiTextCaution_ColorEscape
                                                      "\u2605 ${status.feeds}", NULL);
                setId_Widget(as_Widget(fprog), "feeds.progress");
                setBackgroundColor_Widget(as_Widget(fprog), uiBackground_ColorId);
                setAlignVisually_LabelWidget(fprog, iTrue);
                addChildFlags_Widget(rightEmbed,
                                     iClob(fprog),
                                     collapse_WidgetFlag | hidden_WidgetFlag);
            }
            /* Download progress indicator is also inside the input field, but hidden normally. */ {
                iLabelWidget *progress = new_LabelWidget(uiTextCaution_ColorEscape "00.000 ${mb}", NULL);
                setId_Widget(as_Widget(progress), "document.progress");
                setBackgroundColor_Widget(as_Widget(progress), uiBackground_ColorId);
                setAlignVisually_LabelWidget(progress, iTrue);
                addChildFlags_Widget(
                    rightEmbed, iClob(progress), collapse_WidgetFlag);
            }
            /* Reload button. */
            iLabelWidget *reload = newIcon_LabelWidget(reloadCStr_, 0, 0, "navigate.reload");
            setId_Widget(as_Widget(reload), "reload");
            addChildFlags_Widget(as_Widget(url), iClob(reload), embedFlags | moveToParentRightEdge_WidgetFlag);
            updateSize_LabelWidget(reload);
            setId_Widget(addChild_Widget(rightEmbed, iClob(makePadding_Widget(0))), "url.embedpad");
        }
        if (deviceType_App() != desktop_AppDeviceType) {
            /* On mobile, the Identities button is on the right side of the URL bar. */
            iWidget *ident = removeChild_Widget(navBar, findChild_Widget(navBar, "navbar.ident"));
            addChild_Widget(navBar, iClob(ident));
        }
        addChildFlags_Widget(navBar, iClob(new_Widget()), expand_WidgetFlag);
        setId_Widget(addChildFlags_Widget(navBar,
                        iClob(newIcon_LabelWidget(
                            home_Icon, SDLK_h, KMOD_PRIMARY | KMOD_SHIFT, "navigate.home")),
                        collapse_WidgetFlag),
                     "navbar.home");
#if !defined (iHaveNativeMenus)
#   if defined (iPlatformAppleMobile)
        iLabelWidget *navMenu =
            makeMenuButton_LabelWidget("\U0001d362", isPhone ? phoneNavMenuItems_ : tabletNavMenuItems_,
                                       isPhone ? iElemCount(phoneNavMenuItems_) : iElemCount(tabletNavMenuItems_));
#   else
        iLabelWidget *navMenu =
            makeMenuButton_LabelWidget("\U0001d362", navMenuItems_, iElemCount(navMenuItems_));
#   endif
        setAlignVisually_LabelWidget(navMenu, iTrue);
        setId_Widget(addChildFlags_Widget(navBar, iClob(navMenu), collapse_WidgetFlag), "navbar.menu");
#else
        insertMacMenus_();
#endif
    }
    /* Tab bar. */ {
        iWidget *mainStack = new_Widget();
        setId_Widget(mainStack, "stack");
        addChildFlags_Widget(div, iClob(mainStack), resizeChildren_WidgetFlag | expand_WidgetFlag |
                             unhittable_WidgetFlag);
        iWidget *tabBar = makeTabs_Widget(mainStack);
        setId_Widget(tabBar, "doctabs");
        setBackgroundColor_Widget(tabBar, uiBackground_ColorId);
        appendTabPage_Widget(tabBar, iClob(new_DocumentWidget()), "Document", 0, 0);
        iWidget *buttons = findChild_Widget(tabBar, "tabs.buttons");
        setFlags_Widget(buttons, collapse_WidgetFlag | hidden_WidgetFlag |
                        drawBackgroundToHorizontalSafeArea_WidgetFlag, iTrue);
        if (deviceType_App() == phone_AppDeviceType) {
            setBackgroundColor_Widget(buttons, uiBackground_ColorId);
        }
        setId_Widget(
            addChild_Widget(buttons, iClob(newIcon_LabelWidget(add_Icon, 0, 0, "tabs.new"))),
            "newtab");
    }
    /* Sidebars. */ {
        iWidget *content = findChild_Widget(d->root, "tabs.content");
        iSidebarWidget *sidebar1 = new_SidebarWidget(left_SideBarSide);
        addChildPos_Widget(content, iClob(sidebar1), front_WidgetAddPos);
        iSidebarWidget *sidebar2 = new_SidebarWidget(right_SideBarSide);
        if (deviceType_App() != phone_AppDeviceType) {
            addChildPos_Widget(content, iClob(sidebar2), back_WidgetAddPos);
        }
        else {
            /* The identities sidebar is always in the main area. */
            addChild_Widget(findChild_Widget(d->root, "stack"), iClob(sidebar2));
            setFlags_Widget(as_Widget(sidebar2), hidden_WidgetFlag, iTrue);
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
                        hidden_WidgetFlag | disabled_WidgetFlag | collapse_WidgetFlag |
                            arrangeHeight_WidgetFlag | resizeChildren_WidgetFlag |
                            arrangeHorizontal_WidgetFlag,
                        iTrue);
        if (deviceType_App() == desktop_AppDeviceType) {
            addChild_Widget(div, iClob(searchBar));
        }
        else {
            /* The search bar appears at the top on mobile, because there is a virtual keyboard
               covering the bottom. */
            insertChildAfter_Widget(div, iClob(searchBar),
                                    childIndex_Widget(div, findChild_Widget(div, "navbar")));
        }
        setBackgroundColor_Widget(searchBar, uiBackground_ColorId);
        setCommandHandler_Widget(searchBar, handleSearchBarCommands_);
        addChildFlags_Widget(
            searchBar, iClob(new_LabelWidget(magnifyingGlass_Icon, NULL)), frameless_WidgetFlag);
        iInputWidget *input = new_InputWidget(0);
        setHint_InputWidget(input, "${hint.findtext}");
        setSelectAllOnFocus_InputWidget(input, iTrue);
        setEatEscape_InputWidget(input, iFalse); /* unfocus and close with one keypress */
        setId_Widget(addChildFlags_Widget(searchBar, iClob(input), expand_WidgetFlag),
                     "find.input");
        addChild_Widget(searchBar, iClob(newIcon_LabelWidget("  \u2b9f  ", 'g', KMOD_PRIMARY, "find.next")));
        addChild_Widget(searchBar, iClob(newIcon_LabelWidget("  \u2b9d  ", 'g', KMOD_PRIMARY | KMOD_SHIFT, "find.prev")));
        addChild_Widget(searchBar, iClob(newIcon_LabelWidget(close_Icon, SDLK_ESCAPE, 0, "find.close")));
    }
#if defined (iPlatformAppleMobile)
    /* Bottom toolbar. */
    if (isPhone_iOS()) {
        iWidget *toolBar = new_Widget();
        addChild_Widget(d->root, iClob(toolBar));
        setId_Widget(toolBar, "toolbar");
        setCommandHandler_Widget(toolBar, handleToolBarCommands_);
        setFlags_Widget(toolBar, moveToParentBottomEdge_WidgetFlag |
                        parentCannotResizeHeight_WidgetFlag |
                        resizeWidthOfChildren_WidgetFlag |
                        arrangeHeight_WidgetFlag | arrangeHorizontal_WidgetFlag, iTrue);
        setBackgroundColor_Widget(toolBar, tmBannerBackground_ColorId);
        addChildFlags_Widget(toolBar, iClob(newLargeIcon_LabelWidget("\U0001f870", "navigate.back")), frameless_WidgetFlag);
        addChildFlags_Widget(toolBar, iClob(newLargeIcon_LabelWidget("\U0001f872", "navigate.forward")), frameless_WidgetFlag);
        setId_Widget(addChildFlags_Widget(toolBar, iClob(newLargeIcon_LabelWidget("\U0001f464", "toolbar.showident")), frameless_WidgetFlag), "toolbar.ident");
        setId_Widget(addChildFlags_Widget(toolBar, iClob(newLargeIcon_LabelWidget("\U0001f588", "toolbar.showview arg:-1")),
                             frameless_WidgetFlag | commandOnClick_WidgetFlag), "toolbar.view");
        iLabelWidget *menuButton = makeMenuButton_LabelWidget("\U0001d362", phoneNavMenuItems_,
                                                              iElemCount(phoneNavMenuItems_));
        setFont_LabelWidget(menuButton, uiLabelLarge_FontId);
        addChildFlags_Widget(toolBar, iClob(menuButton), frameless_WidgetFlag);
        iForEach(ObjectList, i, children_Widget(toolBar)) {
            iLabelWidget *btn = i.object;
            setFlags_Widget(i.object, noBackground_WidgetFlag, iTrue);
            setTextColor_LabelWidget(i.object, tmBannerIcon_ColorId);
//            setBackgroundColor_Widget(i.object, tmBannerSideTitle_ColorId);
        }
        const iMenuItem items[] = {
            { pin_Icon " ${sidebar.bookmarks}", 0, 0, "toolbar.showview arg:0" },
            { star_Icon " ${sidebar.feeds}", 0, 0, "toolbar.showview arg:1" },
            { clock_Icon " ${sidebar.history}", 0, 0, "toolbar.showview arg:2" },
            { page_Icon " ${toolbar.outline}", 0, 0, "toolbar.showview arg:4" },
        };
        iWidget *menu = makeMenu_Widget(findChild_Widget(toolBar, "toolbar.view"),
                                        items, iElemCount(items));
        setId_Widget(menu, "toolbar.menu");
    }
#endif
    updatePadding_Window_(d);
    /* Global context menus. */ {
        iWidget *tabsMenu = makeMenu_Widget(
            d->root,
            (iMenuItem[]){
                { close_Icon " ${menu.closetab}", 0, 0, "tabs.close" },
                { copy_Icon " ${menu.duptab}", 0, 0, "tabs.new duplicate:1" },
                { "---", 0, 0, NULL },
                { "${menu.closetab.other}", 0, 0, "tabs.close toleft:1 toright:1" },
                { barLeftArrow_Icon " ${menu.closetab.left}", 0, 0, "tabs.close toleft:1" },
                { barRightArrow_Icon " ${menu.closetab.right}", 0, 0, "tabs.close toright:1" },
            },
            6);
        iWidget *barMenu =
            makeMenu_Widget(d->root,
                            (iMenuItem[]){
                                { leftHalf_Icon " ${menu.sidebar.left}", 0, 0, "sidebar.toggle" },
                                { rightHalf_Icon " ${menu.sidebar.right}", 0, 0, "sidebar2.toggle" },
                            },
                            deviceType_App() == phone_AppDeviceType ? 1 : 2);
        iWidget *clipMenu = makeMenu_Widget(d->root,
                                            (iMenuItem[]){
                                                { scissor_Icon " ${menu.cut}", 0, 0, "input.copy cut:1" },
                                                { clipCopy_Icon " ${menu.copy}", 0, 0, "input.copy" },
                                                { "---", 0, 0, NULL },
                                                { clipboard_Icon " ${menu.paste}", 0, 0, "input.paste" },
                                            },
                                            4);
        setId_Widget(tabsMenu, "doctabs.menu");
        setId_Widget(barMenu, "barmenu");
        setId_Widget(clipMenu, "clipmenu");
    }
    /* Global keyboard shortcuts. */ {
        addAction_Widget(d->root, 'l', KMOD_PRIMARY, "navigate.focus");
        addAction_Widget(d->root, 'f', KMOD_PRIMARY, "focus.set id:find.input");
        addAction_Widget(d->root, '1', KMOD_PRIMARY, "sidebar.mode arg:0 toggle:1");
        addAction_Widget(d->root, '2', KMOD_PRIMARY, "sidebar.mode arg:1 toggle:1");
        addAction_Widget(d->root, '3', KMOD_PRIMARY, "sidebar.mode arg:2 toggle:1");
        addAction_Widget(d->root, '4', KMOD_PRIMARY, "sidebar.mode arg:3 toggle:1");
        addAction_Widget(d->root, '5', KMOD_PRIMARY, "sidebar.mode arg:4 toggle:1");
        addAction_Widget(d->root, '1', rightSidebar_KeyModifier, "sidebar2.mode arg:0 toggle:1");
        addAction_Widget(d->root, '2', rightSidebar_KeyModifier, "sidebar2.mode arg:1 toggle:1");
        addAction_Widget(d->root, '3', rightSidebar_KeyModifier, "sidebar2.mode arg:2 toggle:1");
        addAction_Widget(d->root, '4', rightSidebar_KeyModifier, "sidebar2.mode arg:3 toggle:1");
        addAction_Widget(d->root, '5', rightSidebar_KeyModifier, "sidebar2.mode arg:4 toggle:1");
    }
    updateMetrics_Window_(d);
}

void showToolbars_Window(iWindow *d, iBool show) {
    if (isLandscape_App()) return;
    iWidget *toolBar = findChild_Widget(d->root, "toolbar");
    if (!toolBar) return;
    const int height = rootSize_Window(d).y - top_Rect(boundsWithoutVisualOffset_Widget(toolBar));
    if (show && !isVisible_Widget(toolBar)) {
        setFlags_Widget(toolBar, hidden_WidgetFlag, iFalse);
        setVisualOffset_Widget(toolBar, 0, 200, easeOut_AnimFlag);
    }
    else if (!show && isVisible_Widget(toolBar)) {
        setFlags_Widget(toolBar, hidden_WidgetFlag, iTrue);
        setVisualOffset_Widget(toolBar, height, 200, easeOut_AnimFlag);
    }
}

static void updateRootSize_Window_(iWindow *d, iBool notifyAlways) {
    iInt2 *size = &d->root->rect.size;
    const iInt2 oldSize = *size;
    SDL_GetRendererOutputSize(d->render, &size->x, &size->y);
    size->y -= d->keyboardHeight;
    if (notifyAlways || !isEqual_I2(oldSize, *size)) {
        const iBool isHoriz = (d->place.lastNotifiedSize.x != size->x);
        const iBool isVert  = (d->place.lastNotifiedSize.y != size->y);
        arrange_Widget(d->root);
        postCommandf_App("window.resized width:%d height:%d horiz:%d vert:%d",
                         size->x,
                         size->y,
                         isHoriz,
                         isVert);
        postCommand_App("widget.overflow"); /* check bounds with updated sizes */
        postRefresh_App();
        d->place.lastNotifiedSize = *size;
    }
}

void drawWhileResizing_Window(iWindow *d, int w, int h) {
    /* This is called while a window resize is in progress, so we can be pretty confident
       the size has actually changed. */
    d->root->rect.size = coord_Window(d, w, h);
    arrange_Widget(d->root);
    draw_Window(d);
}

static float pixelRatio_Window_(const iWindow *d) {
#if defined (iPlatformMsys)
    iUnused(d);
    return desktopDPI_Win32();
#elif defined (iPlatformLinux)
    float vdpi = 0.0f;
    SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(d->win), NULL, NULL, &vdpi);
    const float factor = vdpi / 96.0f;
    return iMax(1.0f, factor);
#else
    int dx, x;
    SDL_GetRendererOutputSize(d->render, &dx, NULL);
    SDL_GetWindowSize(d->win, &x, NULL);
    return (float) dx / (float) x;
#endif
}

static void drawBlank_Window_(iWindow *d) {
    const iColor bg = get_Color(uiBackground_ColorId);
    SDL_SetRenderDrawColor(d->render, bg.r, bg.g, bg.b, 255);
    SDL_RenderClear(d->render);
    SDL_RenderPresent(d->render);
}

#if defined (LAGRANGE_CUSTOM_FRAME)
static SDL_HitTestResult hitTest_Window_(SDL_Window *win, const SDL_Point *pos, void *data) {
    iWindow *d = data;
    iAssert(d->win == win);
    if (SDL_GetWindowFlags(d->win) & (SDL_WINDOW_MOUSE_CAPTURE | SDL_WINDOW_FULLSCREEN_DESKTOP)) {
        return SDL_HITTEST_NORMAL;
    }
    const int snap = snap_Window(d);
    int w, h;
    SDL_GetWindowSize(win, &w, &h);
    /* TODO: Check if inside the caption label widget. */
    const iBool isLeft   = pos->x < gap_UI;
    const iBool isRight  = pos->x >= w - gap_UI;
    const iBool isTop    = pos->y < gap_UI && snap != yMaximized_WindowSnap;
    const iBool isBottom = pos->y >= h - gap_UI && snap != yMaximized_WindowSnap;
    const int captionHeight = lineHeight_Text(uiContent_FontId) + gap_UI * 2;
    const int rightEdge = left_Rect(bounds_Widget(findChild_Widget(d->root, "winbar.min")));
    d->place.lastHit = SDL_HITTEST_NORMAL;
    if (snap != maximized_WindowSnap) {
        if (isLeft) {
            return pos->y < captionHeight       ? SDL_HITTEST_RESIZE_TOPLEFT
                   : pos->y > h - captionHeight ? SDL_HITTEST_RESIZE_BOTTOMLEFT
                                                : (d->place.lastHit = SDL_HITTEST_RESIZE_LEFT);
        }
        if (isRight) {
            return pos->y < captionHeight       ? SDL_HITTEST_RESIZE_TOPRIGHT
                   : pos->y > h - captionHeight ? SDL_HITTEST_RESIZE_BOTTOMRIGHT
                                                : (d->place.lastHit = SDL_HITTEST_RESIZE_RIGHT);
        }
        if (isTop) {
            return pos->x < captionHeight       ? SDL_HITTEST_RESIZE_TOPLEFT
                   : pos->x > w - captionHeight ? SDL_HITTEST_RESIZE_TOPRIGHT
                                                : (d->place.lastHit = SDL_HITTEST_RESIZE_TOP);
        }
        if (isBottom) {
            return pos->x < captionHeight       ? SDL_HITTEST_RESIZE_BOTTOMLEFT
                   : pos->x > w - captionHeight ? SDL_HITTEST_RESIZE_BOTTOMRIGHT
                                                : (d->place.lastHit = SDL_HITTEST_RESIZE_BOTTOM);
        }
    }
    if (pos->x < rightEdge && pos->y < captionHeight) {
        return SDL_HITTEST_DRAGGABLE;
    }
    return SDL_HITTEST_NORMAL;
}

SDL_HitTestResult hitTest_Window(const iWindow *d, iInt2 pos) {
    return hitTest_Window_(d->win, &(SDL_Point){ pos.x, pos.y }, iConstCast(void *, d));
}
#endif

iBool create_Window_(iWindow *d, iRect rect, uint32_t flags) {
    flags |= SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN;
#if defined (LAGRANGE_CUSTOM_FRAME)
    if (prefs_App()->customFrame) {
        /* We are drawing a custom frame so hide the default one. */
        flags |= SDL_WINDOW_BORDERLESS;
    }
#endif
    if (SDL_CreateWindowAndRenderer(
            width_Rect(rect), height_Rect(rect), flags, &d->win, &d->render)) {
        return iFalse;
    }
#if defined (LAGRANGE_CUSTOM_FRAME)
    if (prefs_App()->customFrame) {
        /* Register a handler for window hit testing (drag, resize). */
        SDL_SetWindowHitTest(d->win, hitTest_Window_, d);
        SDL_SetWindowResizable(d->win, SDL_TRUE);
    }
#endif
    return iTrue;
}

static SDL_Surface *loadImage_(const iBlock *data, int resized) {
    int      w = 0, h = 0, num = 4;
    stbi_uc *pixels = stbi_load_from_memory(
        constData_Block(data), size_Block(data), &w, &h, &num, STBI_rgb_alpha);
    if (resized) {
        stbi_uc *rsPixels = malloc(num * resized * resized);
        stbir_resize_uint8(pixels, w, h, 0, rsPixels, resized, resized, 0, num);
        free(pixels);
        pixels = rsPixels;
        w = h = resized;
    }
    return SDL_CreateRGBSurfaceWithFormatFrom(
        pixels, w, h, 8 * num, w * num, SDL_PIXELFORMAT_RGBA32);
}

void init_Window(iWindow *d, iRect rect) {
    theWindow_ = d;
    d->win = NULL;
    iZap(d->cursors);
    d->place.initialPos = rect.pos;
    d->place.normalRect = rect;
    d->place.lastNotifiedSize = zero_I2();
    d->pendingCursor = NULL;
    d->isDrawFrozen = iTrue;
    d->isExposed = iFalse;
    d->isMinimized = iFalse;
    d->isMouseInside = iTrue;
    d->ignoreClick = iFalse;
    d->focusGainedAt = 0;
    d->keyboardHeight = 0;
    init_Anim(&d->rootOffset, 0.0f);
    uint32_t flags = 0;
#if defined (iPlatformAppleDesktop)
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, shouldDefaultToMetalRenderer_MacOS() ? "metal" : "opengl");
#elif defined (iPlatformAppleMobile)
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
#else
    flags |= SDL_WINDOW_OPENGL;
#endif
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    /* First try SDL's default renderer that should be the best option. */
    if (forceSoftwareRender_App() || !create_Window_(d, rect, flags)) {
        /* No luck, maybe software only? This should always work as long as there is a display. */
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        if (!create_Window_(d, rect, 0)) {
            fprintf(stderr, "Error when creating window: %s\n", SDL_GetError());
            exit(-2);
        }
    }
    if (left_Rect(rect) >= 0 || top_Rect(rect) >= 0) {
        SDL_SetWindowPosition(d->win, left_Rect(rect), top_Rect(rect));
    }
    const iInt2 minSize = init_I2(425, 325);
    SDL_SetWindowMinimumSize(d->win, minSize.x, minSize.y);
    SDL_SetWindowTitle(d->win, "Lagrange");
    /* Some info. */ {
        SDL_RendererInfo info;
        SDL_GetRendererInfo(d->render, &info);
        printf("[window] renderer: %s%s\n", info.name,
               info.flags & SDL_RENDERER_ACCELERATED ? " (accelerated)" : "");
#if !defined (NDEBUG)
        printf("[window] max texture size: %d x %d\n",
               info.max_texture_width,
               info.max_texture_height);
        for (size_t i = 0; i < info.num_texture_formats; ++i) {
            printf("[window] supported texture format: %s\n", SDL_GetPixelFormatName(
                       info.texture_formats[i]));
        }
#endif
    }
    drawBlank_Window_(d);
    d->uiScale = initialUiScale_;
    d->pixelRatio = pixelRatio_Window_(d);
    setPixelRatio_Metrics(d->pixelRatio * d->uiScale);
#if defined (iPlatformMsys)
    SDL_Rect usable;
    SDL_GetDisplayUsableBounds(0, &usable);
    SDL_SetWindowMaximumSize(d->win, usable.w, usable.h);
    SDL_SetWindowMinimumSize(d->win, minSize.x * d->pixelRatio, minSize.y * d->pixelRatio);
    useExecutableIconResource_SDLWindow(d->win);
#endif
#if defined (iPlatformLinux)
    SDL_SetWindowMinimumSize(d->win, minSize.x * d->pixelRatio, minSize.y * d->pixelRatio);
    /* Load the window icon. */ {
        SDL_Surface *surf = loadImage_(&imageLagrange64_Embedded, 0);
        SDL_SetWindowIcon(d->win, surf);
        free(surf->pixels);
        SDL_FreeSurface(surf);
    }
#endif
#if defined (iPlatformAppleMobile)
    setupWindow_iOS(d);
#endif
    d->root = new_Widget();
    setFlags_Widget(d->root, focusRoot_WidgetFlag, iTrue);
    d->presentTime = 0.0;
    d->frameTime = SDL_GetTicks();
    d->loadAnimTimer = 0;
    setId_Widget(d->root, "root");
    init_Text(d->render);
    setupUserInterface_Window(d);
    postCommand_App("bindings.changed"); /* update from bindings */
    updateRootSize_Window_(d, iFalse);
    /* Load the border shadow texture. */ {
        SDL_Surface *surf = loadImage_(&imageShadow_Embedded, 0);
        d->borderShadow = SDL_CreateTextureFromSurface(d->render, surf);
        SDL_SetTextureBlendMode(d->borderShadow, SDL_BLENDMODE_BLEND);
        free(surf->pixels);
        SDL_FreeSurface(surf);
    }
    d->appIcon = NULL;
#if defined (LAGRANGE_CUSTOM_FRAME)
    /* Load the app icon for drawing in the title bar. */
    if (prefs_App()->customFrame) {
        SDL_Surface *surf = loadImage_(&imageLagrange64_Embedded, appIconSize_());
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        d->appIcon = SDL_CreateTextureFromSurface(d->render, surf);
        free(surf->pixels);
        SDL_FreeSurface(surf);
        /* We need to observe non-client-area events. */
        SDL_EventState(SDL_SYSWMEVENT, SDL_TRUE);
    }
#endif
}

void deinit_Window(iWindow *d) {
    iRecycle();
    if (theWindow_ == d) {
        theWindow_ = NULL;
    }
    iForIndices(i, d->cursors) {
        if (d->cursors[i]) {
            SDL_FreeCursor(d->cursors[i]);
        }
    }
    iReleasePtr(&d->root);
    deinit_Text();
    SDL_DestroyRenderer(d->render);
    SDL_DestroyWindow(d->win);
}

SDL_Renderer *renderer_Window(const iWindow *d) {
    return d->render;
}

iInt2 maxTextureSize_Window(const iWindow *d) {
    SDL_RendererInfo info;
    SDL_GetRendererInfo(d->render, &info);
    return init_I2(info.max_texture_width, info.max_texture_height);
}

iBool isFullscreen_Window(const iWindow *d) {
    return snap_Window(d) == fullscreen_WindowSnap;
}

static void invalidate_Window_(iWindow *d) {
    iUnused(d);
    resetFonts_Text();
    postCommand_App("theme.changed"); /* forces UI invalidation */
}

static iBool isNormalPlacement_Window_(const iWindow *d) {
    if (d->isDrawFrozen) return iFalse;
#if defined (iPlatformApple)
    /* Maximized mode is not special on macOS. */
    if (snap_Window(d) == maximized_WindowSnap) {
        return iTrue;
    }
#endif
    if (snap_Window(d)) return iFalse;
    return !(SDL_GetWindowFlags(d->win) & SDL_WINDOW_MINIMIZED);
}

static iBool unsnap_Window_(iWindow *d, const iInt2 *newPos) {
#if defined (LAGRANGE_CUSTOM_FRAME)
    if (!prefs_App()->customFrame) {
        return iFalse;
    }
    const int snap = snap_Window(d);
    if (snap == yMaximized_WindowSnap || snap == left_WindowSnap || snap == right_WindowSnap) {
        if (!newPos || (d->place.lastHit == SDL_HITTEST_RESIZE_LEFT ||
                        d->place.lastHit == SDL_HITTEST_RESIZE_RIGHT)) {
            return iFalse;
        }
        if (newPos) {
            SDL_Rect usable;
            SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(d->win), &usable);
            /* Snap to top. */
            if (snap == yMaximized_WindowSnap &&
                iAbs(newPos->y - usable.y) < lineHeight_Text(uiContent_FontId) * 2) {
                setSnap_Window(d, redo_WindowSnap | yMaximized_WindowSnap);
                return iFalse;
            }
        }
    }
    if (snap && snap != fullscreen_WindowSnap) {
        if (snap_Window(d) == yMaximized_WindowSnap && newPos) {
            d->place.normalRect.pos = *newPos;
        }
        //printf("unsnap\n"); fflush(stdout);
        setSnap_Window(d, none_WindowSnap);
        return iTrue;
    }
#endif
    return iFalse;
}

static void notifyMetricsChange_Window_(const iWindow *d) {
    /* Dynamic UI metrics change. Widgets need to update themselves. */
    setPixelRatio_Metrics(d->pixelRatio * d->uiScale);
    resetFonts_Text();
    postCommand_App("metrics.changed");
}

static void checkPixelRatioChange_Window_(iWindow *d) {
    const float ratio = pixelRatio_Window_(d);
    if (iAbs(ratio - d->pixelRatio) > 0.001f) {
        d->pixelRatio = ratio;
        notifyMetricsChange_Window_(d);
    }
}

static iBool handleWindowEvent_Window_(iWindow *d, const SDL_WindowEvent *ev) {
    switch (ev->event) {
        case SDL_WINDOWEVENT_EXPOSED:
            if (!d->isExposed) {
                drawBlank_Window_(d); /* avoid showing system-provided contents */
                d->isExposed = iTrue;
            }
            /* Since we are manually controlling when to redraw the window, we are responsible
               for ensuring that window contents get redrawn after expose events. Under certain
               circumstances (e.g., under openbox), not doing this would mean that the window
               is missing contents until other events trigger a refresh. */
            postRefresh_App();
#if defined (LAGRANGE_ENABLE_WINDOWPOS_FIX)
            if (d->place.initialPos.x >= 0) {
                int bx, by;
                SDL_GetWindowBordersSize(d->win, &by, &bx, NULL, NULL);
                SDL_SetWindowPosition(d->win, d->place.initialPos.x + bx, d->place.initialPos.y + by);
                d->place.initialPos = init1_I2(-1);
            }
#endif
            return iFalse;
#if defined (iPlatformDesktop)
        case SDL_WINDOWEVENT_MOVED: {
            if (d->isMinimized) {
                return iFalse;
            }
            checkPixelRatioChange_Window_(d);
            const iInt2 newPos = init_I2(ev->data1, ev->data2);
            if (isEqual_I2(newPos, init1_I2(-32000))) { /* magic! */
                /* Maybe minimized? Seems like a Windows constant of some kind. */
                d->isMinimized = iTrue;
                return iFalse;
            }
#if defined (LAGRANGE_CUSTOM_FRAME)
            /* Set the snap position depending on where the mouse cursor is. */
            if (prefs_App()->customFrame) {
                SDL_Rect usable;
                iInt2 mouse = cursor_Win32(); /* SDL is unaware of the current cursor pos */
                SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(d->win), &usable);
                const iBool isTop = iAbs(mouse.y - usable.y) < gap_UI * 20;
                const iBool isBottom = iAbs(usable.y + usable.h - mouse.y) < gap_UI * 20;
                if (iAbs(mouse.x - usable.x) < gap_UI) {
                    setSnap_Window(d,
                                   redo_WindowSnap | left_WindowSnap |
                                       (isTop ? topBit_WindowSnap : 0) |
                                       (isBottom ? bottomBit_WindowSnap : 0));
                    return iTrue;
                }
                if (iAbs(mouse.x - usable.x - usable.w) < gap_UI) {
                    setSnap_Window(d,
                                   redo_WindowSnap | right_WindowSnap |
                                       (isTop ? topBit_WindowSnap : 0) |
                                       (isBottom ? bottomBit_WindowSnap : 0));
                    return iTrue;
                }
                if (iAbs(mouse.y - usable.y) < 2) {
                    setSnap_Window(d,
                                   redo_WindowSnap | (d->place.lastHit == SDL_HITTEST_RESIZE_TOP
                                                          ? yMaximized_WindowSnap
                                                          : maximized_WindowSnap));
                    return iTrue;
                }
            }
#endif /* defined LAGRANGE_CUSTOM_FRAME */
            //printf("MOVED: %d, %d\n", ev->data1, ev->data2); fflush(stdout);
            if (unsnap_Window_(d, &newPos)) {
                return iTrue;
            }
            if (isNormalPlacement_Window_(d)) {
                d->place.normalRect.pos = newPos;
                //printf("normal rect set (move)\n"); fflush(stdout);
                iInt2 border = zero_I2();
#if !defined (iPlatformApple)
                SDL_GetWindowBordersSize(d->win, &border.y, &border.x, NULL, NULL);
#endif
                d->place.normalRect.pos = max_I2(zero_I2(), sub_I2(d->place.normalRect.pos, border));
            }
            return iTrue;
        }
        case SDL_WINDOWEVENT_RESIZED:
            updatePadding_Window_(d);
            if (d->isMinimized) {
                updateRootSize_Window_(d, iTrue);
                return iTrue;
            }
            if (unsnap_Window_(d, NULL)) {
                return iTrue;
            }
            if (isNormalPlacement_Window_(d)) {
                d->place.normalRect.size = init_I2(ev->data1, ev->data2);
                //printf("normal rect set (resize)\n"); fflush(stdout);
            }
            checkPixelRatioChange_Window_(d);
            updateRootSize_Window_(d, iTrue /* we were already redrawing during the resize */);
            postRefresh_App();
            return iTrue;
        case SDL_WINDOWEVENT_RESTORED:
            updateRootSize_Window_(d, iTrue);
            invalidate_Window_(d);
            d->isMinimized = iFalse;
            postRefresh_App();
            return iTrue;
        case SDL_WINDOWEVENT_MINIMIZED:
            d->isMinimized = iTrue;
            return iTrue;
#endif
        case SDL_WINDOWEVENT_LEAVE:
            unhover_Widget();
            d->isMouseInside = iFalse;
            postCommand_App("window.mouse.exited");
            return iTrue;
        case SDL_WINDOWEVENT_ENTER:
            d->isMouseInside = iTrue;
            postCommand_App("window.mouse.entered");
            return iTrue;
#if defined (iPlatformMobile)
        case SDL_WINDOWEVENT_RESIZED:
            /* On mobile, this occurs when the display is rotated. */
            invalidate_Window_(d);
            postRefresh_App();
            return iTrue;
#endif
        case SDL_WINDOWEVENT_FOCUS_GAINED:
            d->focusGainedAt = SDL_GetTicks();
            setCapsLockDown_Keys(iFalse);
            postCommand_App("window.focus.gained");
#if defined (iPlatformMobile)
            /* Returned to foreground, may have lost buffered content. */
            invalidate_Window_(d);
#endif
            return iFalse;
        case SDL_WINDOWEVENT_FOCUS_LOST:
            postCommand_App("window.focus.lost");
            return iFalse;
        case SDL_WINDOWEVENT_TAKE_FOCUS:
            SDL_SetWindowInputFocus(d->win);
            postRefresh_App();
            return iTrue;
        default:
            break;
    }
    return iFalse;
}

static void applyCursor_Window_(iWindow *d) {
    if (d->pendingCursor) {
        SDL_SetCursor(d->pendingCursor);
        d->pendingCursor = NULL;
    }
}

iBool processEvent_Window(iWindow *d, const SDL_Event *ev) {
    switch (ev->type) {
#if defined (LAGRANGE_CUSTOM_FRAME)
        case SDL_SYSWMEVENT: {
            /* We observe native Win32 messages for better user interaction with the
               window frame. Mouse clicks especially will not generate normal SDL
               events if they happen on the custom hit-tested regions. These events
               are processed only there; the UI widgets do not get involved. */
            processNativeEvent_Win32(ev->syswm.msg, d);
            break;
        }
#endif
        case SDL_WINDOWEVENT: {
            return handleWindowEvent_Window_(d, &ev->window);
        }
        case SDL_RENDER_TARGETS_RESET:
        case SDL_RENDER_DEVICE_RESET: {
            invalidate_Window_(d);
            break;
        }
        default: {
            SDL_Event event = *ev;
            if (event.type == SDL_USEREVENT && isCommand_UserEvent(ev, "window.unfreeze")) {
                d->isDrawFrozen = iFalse;
                /* When the window is shown for the first time, ensure glyphs get
                   re-cached correctly. */
                if (SDL_GetWindowFlags(d->win) & SDL_WINDOW_HIDDEN) {
                    SDL_ShowWindow(d->win);
                    resetFonts_Text();
                    postCommand_App("theme.changed");
                }
                postRefresh_App();
                return iTrue;
            }
            if (processEvent_Touch(&event)) {
                return iTrue;
            }
            if (event.type == SDL_KEYDOWN && SDL_GetTicks() - d->focusGainedAt < 10) {
                /* Suspiciously close to when input focus was received. For example under openbox,
                   closing xterm with Ctrl+D will cause the keydown event to "spill" over to us.
                   As a workaround, ignore these events. */
                return iTrue; /* won't go to bindings, either */
            }
            if (event.type == SDL_MOUSEBUTTONDOWN && d->ignoreClick) {
                d->ignoreClick = iFalse;
                return iTrue;
            }
            /* Map mouse pointer coordinate to our coordinate system. */
            if (event.type == SDL_MOUSEMOTION) {
                setCursor_Window(d, SDL_SYSTEM_CURSOR_ARROW); /* default cursor */
                const iInt2 pos = coord_Window(d, event.motion.x, event.motion.y);
                event.motion.x = pos.x;
                event.motion.y = pos.y;
            }
            else if (event.type == SDL_MOUSEBUTTONUP || event.type == SDL_MOUSEBUTTONDOWN) {
                const iInt2 pos = coord_Window(d, event.button.x, event.button.y);
                event.button.x = pos.x;
                event.button.y = pos.y;
            }
            iWidget *widget = d->root;
            if (event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEWHEEL ||
                event.type == SDL_MOUSEBUTTONUP || event.type == SDL_MOUSEBUTTONDOWN) {
                if (mouseGrab_Widget()) {
                    widget = mouseGrab_Widget();
                }
            }
            iWidget *oldHover = hover_Widget();
            /* Dispatch the event to the tree of widgets. */
            iBool wasUsed = dispatchEvent_Widget(widget, &event);
            if (!wasUsed) {
                /* As a special case, clicking the middle mouse button can be used for pasting
                   from the clipboard. */
                if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_MIDDLE) {
                    SDL_Event paste;
                    iZap(paste);
                    paste.type           = SDL_KEYDOWN;
                    paste.key.keysym.sym = SDLK_v;
                    paste.key.keysym.mod = KMOD_PRIMARY;
                    paste.key.state      = SDL_PRESSED;
                    paste.key.timestamp  = SDL_GetTicks();
                    wasUsed = dispatchEvent_Widget(widget, &paste);
                }
                if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
                    postContextClick_Window(d, &event.button);
                }
            }
            if (isMetricsChange_UserEvent(&event)) {
                updateMetrics_Window_(d);
            }
            if (isCommand_UserEvent(&event, "lang.changed")) {
#if defined (iPlatformAppleDesktop)
                /* Retranslate the menus. */
                removeMacMenus_();
                insertMacMenus_();
#endif
                invalidate_Window_(d);
                arrange_Widget(d->root);
            }
            if (oldHover != hover_Widget()) {
                postRefresh_App();
            }
            if (event.type == SDL_MOUSEMOTION) {
                applyCursor_Window_(d);
            }
            return wasUsed;
        }
    }
    return iFalse;
}

iBool postContextClick_Window(iWindow *d, const SDL_MouseButtonEvent *ev) {
    /* A context menu may still get triggered here. */
    const iWidget *hit = hitChild_Widget(d->root, init_I2(ev->x, ev->y));
    while (hit && isEmpty_String(id_Widget(hit))) {
        hit = parent_Widget(hit);
    }
    if (hit) {
        postCommandf_App("contextclick id:%s ptr:%p coord:%d %d",
                         cstr_String(id_Widget(hit)), hit,
                         ev->x, ev->y);
        return iTrue;
    }
    return iFalse;
}

void draw_Window(iWindow *d) {
    if (d->isDrawFrozen) {
        return;
    }
#if defined (iPlatformMobile)
    /* Check if root needs resizing. */ {
        iInt2 renderSize;
        SDL_GetRendererOutputSize(d->render, &renderSize.x, &renderSize.y);
        if (!isEqual_I2(renderSize, d->root->rect.size)) {
            updatePadding_Window_(d);
            updateRootSize_Window_(d, iTrue);
            processEvents_App(postedEventsOnly_AppEventMode);
        }
    }
#endif
    const int   winFlags = SDL_GetWindowFlags(d->win);
    const iBool gotFocus = (winFlags & SDL_WINDOW_INPUT_FOCUS) != 0;
    /* Clear the window. The clear color is visible as a border around the window
       when the custom frame is being used. */ {
#if defined (iPlatformAppleMobile)
        const iColor back = get_Color(tmBackground_ColorId);
#else
        const iColor back = get_Color(gotFocus && d->place.snap != maximized_WindowSnap &&
                                              ~winFlags & SDL_WINDOW_FULLSCREEN_DESKTOP
                                          ? uiAnnotation_ColorId
                                          : uiSeparator_ColorId);
#endif
        SDL_SetRenderDrawColor(d->render, back.r, back.g, back.b, 255);
        SDL_RenderClear(d->render);
    }
    /* Draw widgets. */
    d->frameTime = SDL_GetTicks();
    draw_Widget(d->root);
#if defined (LAGRANGE_CUSTOM_FRAME)
    /* App icon. */
    const iWidget *appIcon = findChild_Widget(d->root, "winbar.icon");
    if (isVisible_Widget(appIcon)) {
        const int   size    = appIconSize_();
        const iRect rect    = bounds_Widget(appIcon);
        const iInt2 mid     = mid_Rect(rect);
        const iBool isLight = isLight_ColorTheme(colorTheme_App());
        iColor iconColor    = get_Color(gotFocus || isLight ? white_ColorId : uiAnnotation_ColorId);
        SDL_SetTextureColorMod(d->appIcon, iconColor.r, iconColor.g, iconColor.b);
        SDL_SetTextureAlphaMod(d->appIcon, gotFocus || !isLight ? 255 : 92);
        SDL_RenderCopy(
            d->render,
            d->appIcon,
            NULL,
            &(SDL_Rect){ left_Rect(rect) + gap_UI * 1.25f, mid.y - size / 2, size, size });
    }
#endif
#if 0
    /* Text cache debugging. */ {
        SDL_Texture *cache = glyphCache_Text();
        SDL_Rect rect = { d->root->rect.size.x - 640, 0, 640, 2.5 * 640 };
        SDL_SetRenderDrawColor(d->render, 0, 0, 0, 255);
        SDL_RenderFillRect(d->render, &rect);
        SDL_RenderCopy(d->render, glyphCache_Text(), NULL, &rect);
    }
#endif
    SDL_RenderPresent(d->render);
}

void resize_Window(iWindow *d, int w, int h) {
    SDL_SetWindowSize(d->win, w, h);
    updateRootSize_Window_(d, iFalse);
}

void setTitle_Window(iWindow *d, const iString *title) {
    SDL_SetWindowTitle(d->win, cstr_String(title));
    iLabelWidget *bar = findChild_Widget(d->root, "winbar.title");
    if (bar) {
        updateText_LabelWidget(bar, title);
    }
}

void setUiScale_Window(iWindow *d, float uiScale) {
    uiScale = iClamp(uiScale, 0.5f, 4.0f);
    if (d) {
        if (iAbs(d->uiScale - uiScale) > 0.0001f) {
            d->uiScale = uiScale;
            notifyMetricsChange_Window_(d);
        }
    }
    else {
        initialUiScale_ = uiScale;
    }
}

void setFreezeDraw_Window(iWindow *d, iBool freezeDraw) {
    d->isDrawFrozen = freezeDraw;
}

void setCursor_Window(iWindow *d, int cursor) {
    if (!d->cursors[cursor]) {
        d->cursors[cursor] = SDL_CreateSystemCursor(cursor);
    }
    d->pendingCursor = d->cursors[cursor];
}

uint32_t id_Window(const iWindow *d) {
    return d && d->win ? SDL_GetWindowID(d->win) : 0;
}

iInt2 rootSize_Window(const iWindow *d) {
    return d ? d->root->rect.size : zero_I2();
}

iRect safeRootRect_Window(const iWindow *d) {
    iRect rect = { zero_I2(), d->root->rect.size };
#if defined (iPlatformAppleMobile)
    float left, top, right, bottom;
    safeAreaInsets_iOS(&left, &top, &right, &bottom);
    adjustEdges_Rect(&rect, top, -right, -bottom, left);
#endif
    return rect;
}

iInt2 visibleRootSize_Window(const iWindow *d) {
    return addY_I2(rootSize_Window(d), -d->keyboardHeight);
}

iInt2 coord_Window(const iWindow *d, int x, int y) {
#if defined (iPlatformMsys) || defined (iPlatformLinux)
    /* On Windows, surface coordinates are in pixels. */
    return init_I2(x, y);
#else
    /* Coordinates are in points. */
    return mulf_I2(init_I2(x, y), d->pixelRatio);
#endif
}

iInt2 mouseCoord_Window(const iWindow *d) {
    if (!d->isMouseInside) {
        return init_I2(-1000000, -1000000);
    }
    int x, y;
    SDL_GetMouseState(&x, &y);
    return coord_Window(d, x, y);
}

float uiScale_Window(const iWindow *d) {
    return d->uiScale;
}

uint32_t frameTime_Window(const iWindow *d) {
    return d->frameTime;
}

iWindow *get_Window(void) {
    /* TODO: This should be thread-specific. */
    return theWindow_;
}

void setKeyboardHeight_Window(iWindow *d, int height) {
    if (d->keyboardHeight != height) {
        d->keyboardHeight = height;
        if (height == 0) {
            setFlags_Anim(&d->rootOffset, easeBoth_AnimFlag, iTrue);
            setValue_Anim(&d->rootOffset, 0, 250);
        }
        postCommandf_App("keyboard.changed arg:%d", height);
        postRefresh_App();
    }
}

void setSnap_Window(iWindow *d, int snapMode) {
    if (!prefs_App()->customFrame) {
        if (snapMode == maximized_WindowSnap) {
            SDL_MaximizeWindow(d->win);
        }
        else if (snapMode == fullscreen_WindowSnap) {
            SDL_SetWindowFullscreen(d->win, SDL_WINDOW_FULLSCREEN_DESKTOP);
        }
        else {
            if (snap_Window(d) == fullscreen_WindowSnap) {
                SDL_SetWindowFullscreen(d->win, 0);
            }
            else {
                SDL_RestoreWindow(d->win);
            }
        }
        return;
    }
#if defined (LAGRANGE_CUSTOM_FRAME)
    if (d->place.snap == snapMode) {
        return;
    }
    const int snapDist = gap_UI * 4;
    iRect newRect = zero_Rect();
    SDL_Rect usable;
    SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(d->win), &usable);
    if (d->place.snap == fullscreen_WindowSnap) {
        SDL_SetWindowFullscreen(d->win, 0);
    }
    d->place.snap = snapMode & ~redo_WindowSnap;
    switch (snapMode & mask_WindowSnap) {
        case none_WindowSnap:
            newRect = intersect_Rect(d->place.normalRect,
                                     init_Rect(usable.x, usable.y, usable.w, usable.h));
            break;
        case left_WindowSnap:
            newRect = init_Rect(usable.x, usable.y, usable.w / 2, usable.h);
            break;
        case right_WindowSnap:
            newRect =
                init_Rect(usable.x + usable.w / 2, usable.y, usable.w - usable.w / 2, usable.h);
            break;
        case maximized_WindowSnap:
            newRect = init_Rect(usable.x, usable.y, usable.w, usable.h);
            break;
        case yMaximized_WindowSnap:
            newRect.pos.y = 0;
            newRect.size.y = usable.h;
            SDL_GetWindowSize(d->win, &newRect.size.x, NULL);
            SDL_GetWindowPosition(d->win, &newRect.pos.x, NULL);
            /* Snap the window to left/right edges, if close by. */
            if (iAbs(right_Rect(newRect) - (usable.x + usable.w)) < snapDist) {
                newRect.pos.x = usable.x + usable.w - width_Rect(newRect);
            }
            if (iAbs(newRect.pos.x - usable.x) < snapDist) {
                newRect.pos.x = usable.x;
            }
            break;
        case fullscreen_WindowSnap:
            SDL_SetWindowFullscreen(d->win, SDL_WINDOW_FULLSCREEN_DESKTOP);
            break;
    }
    if (snapMode & (topBit_WindowSnap | bottomBit_WindowSnap)) {
        newRect.size.y /= 2;
    }
    if (snapMode & bottomBit_WindowSnap) {
        newRect.pos.y += newRect.size.y;
    }
    /* Update window controls. */
    iWidget *winBar = findWidget_App("winbar");
    updateTextCStr_LabelWidget(findChild_Widget(winBar, "winbar.max"),
                               d->place.snap == maximized_WindowSnap ? "\u25a2" : "\u25a1");
    /* Show and hide the title bar. */
    const iBool wasVisible = isVisible_Widget(winBar);
    setFlags_Widget(winBar, hidden_WidgetFlag, d->place.snap == fullscreen_WindowSnap);
    if (newRect.size.x) {
        SDL_SetWindowPosition(d->win, newRect.pos.x, newRect.pos.y);
        SDL_SetWindowSize(d->win, newRect.size.x, newRect.size.y);
        postCommand_App("window.resized");
    }
    if (wasVisible != isVisible_Widget(winBar)) {
        arrange_Widget(d->root);
        postRefresh_App();
    }
#endif /* defined (LAGRANGE_CUSTOM_FRAME) */
}

int snap_Window(const iWindow *d) {
    if (!prefs_App()->customFrame) {
        const int flags = SDL_GetWindowFlags(d->win);
        if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
            return fullscreen_WindowSnap;
        }
        else if (flags & SDL_WINDOW_MAXIMIZED) {
            return maximized_WindowSnap;
        }
        return none_WindowSnap;
    }
    return d->place.snap;
}
