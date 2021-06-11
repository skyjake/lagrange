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
#include "embedded.h"
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

#if defined (iPlatformAppleDesktop)
#  define iHaveNativeMenus
#endif

#if defined (iPlatformPcDesktop)
/* TODO: Submenus wouldn't hurt here. */
static const iMenuItem navMenuItems_[] = {
    { add_Icon " ${menu.newtab}", 't', KMOD_PRIMARY, "tabs.new" },
    { "${menu.openlocation}", SDLK_l, KMOD_PRIMARY, "navigate.focus" },
    { "---", 0, 0, NULL },
    { download_Icon " " saveToDownloads_Label, SDLK_s, KMOD_PRIMARY, "document.save" },
    { "${menu.page.copysource}", SDLK_c, KMOD_PRIMARY, "copy" },
    { "---", 0, 0, NULL },
    { leftHalf_Icon " ${menu.sidebar.left}", SDLK_l, KMOD_PRIMARY | KMOD_SHIFT, "sidebar.toggle" },
    { rightHalf_Icon " ${menu.sidebar.right}", SDLK_p, KMOD_PRIMARY | KMOD_SHIFT, "sidebar2.toggle" },
    { "${menu.view.split}", SDLK_j, KMOD_PRIMARY, "splitmenu.open" },
    { "${menu.zoom.in}", SDLK_EQUALS, KMOD_PRIMARY, "zoom.delta arg:10" },
    { "${menu.zoom.out}", SDLK_MINUS, KMOD_PRIMARY, "zoom.delta arg:-10" },
    { "${menu.zoom.reset}", SDLK_0, KMOD_PRIMARY, "zoom.set arg:100" },
    { "---", 0, 0, NULL },
    { book_Icon " ${menu.bookmarks.list}", 0, 0, "!open url:about:bookmarks" },
    { "${menu.bookmarks.bytag}", 0, 0, "!open url:about:bookmarks?tags" },
    { "${menu.bookmarks.bytime}", 0, 0, "!open url:about:bookmarks?created" },
    { "---", 0, 0, NULL },
    { "${menu.downloads}", 0, 0, "downloads.open" },
    { "${menu.feeds.entrylist}", 0, 0, "!open url:about:feeds" },
    { "---", 0, 0, NULL },
    { gear_Icon " ${menu.preferences}", SDLK_COMMA, KMOD_PRIMARY, "preferences" },
    { "${menu.help}", SDLK_F1, 0, "!open url:about:help" },
    { "${menu.releasenotes}", 0, 0, "!open url:about:version" },
    { "---", 0, 0, NULL },
    { "${menu.quit}", 'q', KMOD_PRIMARY, "quit" }
};
#endif

#if defined (iPlatformAppleMobile)
/* Tablet menu. */
static const iMenuItem tabletNavMenuItems_[] = {
    { folder_Icon " ${menu.openfile}", SDLK_o, KMOD_PRIMARY, "file.open" },
    { add_Icon " ${menu.newtab}", 't', KMOD_PRIMARY, "tabs.new" },
    { close_Icon " ${menu.closetab}", 'w', KMOD_PRIMARY, "tabs.close" },
    { "---", 0, 0, NULL },
    { magnifyingGlass_Icon " ${menu.find}", 0, 0, "focus.set id:find.input" },
    { leftHalf_Icon " ${menu.sidebar.left}", SDLK_l, KMOD_PRIMARY | KMOD_SHIFT, "sidebar.toggle" },
    { rightHalf_Icon " ${menu.sidebar.right}", SDLK_p, KMOD_PRIMARY | KMOD_SHIFT, "sidebar2.toggle" },
    { "${menu.view.split}", SDLK_j, KMOD_PRIMARY, "splitmenu.open" },
    { "---", 0, 0, NULL },
    { book_Icon " ${menu.bookmarks.list}", 0, 0, "!open url:about:bookmarks" },
    { "${menu.bookmarks.bytag}", 0, 0, "!open url:about:bookmarks?tags" },
    { "${menu.feeds.entrylist}", 0, 0, "!open url:about:feeds" },
    { "${menu.downloads}", 0, 0, "downloads.open" },
    { "---", 0, 0, NULL },
    { gear_Icon " ${menu.preferences}", SDLK_COMMA, KMOD_PRIMARY, "preferences" },
    { "${menu.help}", SDLK_F1, 0, "!open url:about:help" },
    { "${menu.releasenotes}", 0, 0, "!open url:about:version" },
};

/* Phone menu. */
static const iMenuItem phoneNavMenuItems_[] = {
    { folder_Icon " ${menu.openfile}", SDLK_o, KMOD_PRIMARY, "file.open" },
    { add_Icon " ${menu.newtab}", 't', KMOD_PRIMARY, "tabs.new" },
    { close_Icon " ${menu.closetab}", 'w', KMOD_PRIMARY, "tabs.close" },
    { "---", 0, 0, NULL },
    { magnifyingGlass_Icon " ${menu.find}", 0, 0, "focus.set id:find.input" },
    { leftHalf_Icon " ${menu.sidebar}", SDLK_l, KMOD_PRIMARY | KMOD_SHIFT, "sidebar.toggle" },
    { "---", 0, 0, NULL },
    { book_Icon " ${menu.bookmarks.list}", 0, 0, "!open url:about:bookmarks" },
    { "${menu.downloads}", 0, 0, "downloads.open" },
    { "${menu.feeds.entrylist}", 0, 0, "!open url:about:feeds" },
    { "---", 0, 0, NULL },
    { gear_Icon " Settings...", SDLK_COMMA, KMOD_PRIMARY, "preferences" },
};
#endif /* AppleMobile */

#if defined (iPlatformAppleMobile)
static const iMenuItem identityButtonMenuItems_[] = {
    { "${menu.identity.notactive}", 0, 0, "ident.showactive" },
    { "---", 0, 0, NULL },
    { add_Icon " ${menu.identity.new}", newIdentity_KeyShortcut, "ident.new" },
    { "${menu.identity.import}", SDLK_i, KMOD_PRIMARY | KMOD_SHIFT, "ident.import" },
    { "---", 0, 0, NULL },
    { person_Icon " ${menu.show.identities}", 0, 0, "toolbar.showident" },
};
#else /* desktop */
static const iMenuItem identityButtonMenuItems_[] = {
    { "${menu.identity.notactive}", 0, 0, "ident.showactive" },
    { "---", 0, 0, NULL },
# if !defined (iPlatformAppleDesktop)
    { add_Icon " ${menu.identity.new}", newIdentity_KeyShortcut, "ident.new" },
    { "${menu.identity.import}", SDLK_i, KMOD_PRIMARY | KMOD_SHIFT, "ident.import" },
    { "---", 0, 0, NULL },
    { person_Icon " ${menu.show.identities}", '4', KMOD_PRIMARY, "sidebar.mode arg:3 show:1" },
# else
    { add_Icon " ${menu.identity.new}", 0, 0, "ident.new" },
    { "---", 0, 0, NULL },
    { person_Icon " ${menu.show.identities}", 0, 0, "sidebar.mode arg:3 show:1" },
# endif
};
#endif

static const char *reloadCStr_   = reload_Icon;
static const char *pageMenuCStr_ = midEllipsis_Icon;

/* TODO: A preference for these, maybe? */
static const char *stopSeqCStr_[] = {
    /* Corners */
    uiTextCaution_ColorEscape "\U0000231c",
    uiTextCaution_ColorEscape "\U0000231d",
    uiTextCaution_ColorEscape "\U0000231f",
    uiTextCaution_ColorEscape "\U0000231e",
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

static iRoot *   activeRoot_     = NULL;

iDefineTypeConstruction(Root)

void init_Root(iRoot *d) {
    iZap(*d);
}

void deinit_Root(iRoot *d) {
    iReleasePtr(&d->widget);
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

void destroyPending_Root(iRoot *d) {
    iRoot *oldRoot = current_Root();
    setCurrent_Root(d);
    iForEach(PtrSet, i, d->pendingDestruction) {
        iWidget *widget = *i.value;
        if (!isFinished_Anim(&widget->visualOffset)) {
            continue;
        }
        if (widget->flags & keepOnTop_WidgetFlag) {
            removeOne_PtrArray(onTop_Root(widget->root), widget);
        }
        if (widget->parent) {
            removeChild_Widget(widget->parent, widget);
        }
        iAssert(widget->parent == NULL);
        iRelease(widget);
        remove_PtrSetIterator(&i);
    }
    setCurrent_Root(oldRoot);
}

void postArrange_Root(iRoot *d) {
    if (!d->pendingArrange) {
        d->pendingArrange = iTrue;
        SDL_Event ev = { .type = SDL_USEREVENT };
        ev.user.code = arrange_UserEventCode;
        ev.user.data2 = d;
        SDL_PushEvent(&ev);
    }
}

iPtrArray *onTop_Root(iRoot *d) {
    if (!d->onTop) {
        d->onTop = new_PtrArray();
    }
    return d->onTop;
}

static iBool handleRootCommands_(iWidget *root, const char *cmd) {
    iUnused(root);
    if (equal_Command(cmd, "menu.open")) {
        iWidget *button = pointer_Command(cmd);
        iWidget *menu = findChild_Widget(button, "menu");
        iAssert(menu);
        if (!isVisible_Widget(menu)) {
            openMenu_Widget(menu, bottomLeft_Rect(bounds_Widget(button)));
        }
        else {
            closeMenu_Widget(menu);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "splitmenu.open")) {
        setFocus_Widget(NULL);
        iWidget *menu = findWidget_Root("splitmenu");
        openMenu_Widget(menu, zero_I2());
        setPos_Widget(menu, sub_I2(divi_I2(size_Root(get_Root()), 2), divi_I2(menu->rect.size, 2)));
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
    else if (equal_Command(cmd, "input.resized")) {
        /* No parent handled this, so do a full rearrangement. */
        /* TODO: Defer this and do a single rearrangement later. */
        arrange_Widget(root);
        postRefresh_App();
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
        iSidebarWidget *sidebar2 = findChild_Widget(root, "sidebar2");
        removeChild_Widget(parent_Widget(sidebar), sidebar);
        setButtonFont_SidebarWidget(sidebar, isLandscape_App() ? uiLabel_FontId : defaultBig_FontId);
        setButtonFont_SidebarWidget(sidebar2, isLandscape_App() ? uiLabel_FontId : defaultBig_FontId);
        //        setBackgroundColor_Widget(findChild_Widget(as_Widget(sidebar), "buttons"),
        //                                  isPortrait_App() ? uiBackgroundUnfocusedSelection_ColorId
        //                                                   : uiBackgroundSidebar_ColorId);
        setFlags_Widget(findChild_Widget(as_Widget(sidebar), "buttons"),
                        borderTop_WidgetFlag,
                        isPortrait_App());
        if (isLandscape_App()) {
            addChildPos_Widget(findChild_Widget(root, "tabs.content"), iClob(sidebar), front_WidgetAddPos);
            setWidth_SidebarWidget(sidebar, 73.0f);
            if (isVisible_Widget(findWidget_App("sidebar2"))) {
                postCommand_App("sidebar2.toggle");
            }
        }
        else {
            addChildPos_Widget(findChild_Widget(root, "stack"), iClob(sidebar), back_WidgetAddPos);
            setWidth_SidebarWidget(sidebar, (float) width_Widget(root) / (float) gap_UI);
            setWidth_SidebarWidget(sidebar2, (float) width_Widget(root) / (float) gap_UI);
        }
        return iFalse;
    }
    else if (handleCommand_App(cmd)) {
        return iTrue;
    }
    return iFalse;
}

static void updateNavBarIdentity_(iWidget *navBar) {
    const iGmIdentity *ident =
        identityForUrl_GmCerts(certs_App(), url_DocumentWidget(document_App()));
    iWidget *button = findChild_Widget(navBar, "navbar.ident");
    iWidget *tool = findWidget_App("toolbar.ident");
    setFlags_Widget(button, selected_WidgetFlag, ident != NULL);
    setFlags_Widget(tool, selected_WidgetFlag, ident != NULL);
    /* Update menu. */
    iLabelWidget *idItem = child_Widget(findChild_Widget(button, "menu"), 0);
    const iString *subjectName = ident ? name_GmIdentity(ident) : NULL;
    setTextCStr_LabelWidget(
        idItem,
        subjectName ? format_CStr(uiTextAction_ColorEscape "%s", cstr_String(subjectName))
                    : "${menu.identity.notactive}");
    setFlags_Widget(as_Widget(idItem), disabled_WidgetFlag, !ident);
}

static void updateNavDirButtons_(iWidget *navBar) {
    const iHistory *history = history_DocumentWidget(document_App());
    setFlags_Widget(findChild_Widget(navBar, "navbar.back"), disabled_WidgetFlag,
                    atOldest_History(history));
    setFlags_Widget(findChild_Widget(navBar, "navbar.forward"), disabled_WidgetFlag,
                    atLatest_History(history));
    setFlags_Widget(findWidget_App("toolbar.back"), disabled_WidgetFlag,
                    atOldest_History(history));
    setFlags_Widget(findWidget_App("toolbar.forward"), disabled_WidgetFlag,
                    atLatest_History(history));
}

static const char *loadAnimationCStr_(void) {
    return stopSeqCStr_[loadAnimIndex_ % iElemCount(stopSeqCStr_)];
}

static uint32_t updateReloadAnimation_Root_(uint32_t interval, void *root) {
    loadAnimIndex_++;
    postCommandf_App("window.reload.update root:%p", root);
    return interval;
}

static void setReloadLabel_Root_(iRoot *d, iBool animating) {
    const iBool isMobile = deviceType_App() != desktop_AppDeviceType;
    iLabelWidget *label = findChild_Widget(d->widget, "reload");
    updateTextCStr_LabelWidget(
        label, animating ? loadAnimationCStr_() : (/*isMobile ? pageMenuCStr_ :*/ reloadCStr_));
//    if (isMobile) {
//        setCommand_LabelWidget(label,
//                               collectNewCStr_String(animating ? "navigate.reload" : "menu.open"));
//    }
}

static void checkLoadAnimation_Root_(iRoot *d) {
    const iBool isOngoing = isRequestOngoing_DocumentWidget(document_Root(d));
    if (isOngoing && !d->loadAnimTimer) {
        d->loadAnimTimer = SDL_AddTimer(loadAnimIntervalMs_, updateReloadAnimation_Root_, d);
    }
    else if (!isOngoing && d->loadAnimTimer) {
        SDL_RemoveTimer(d->loadAnimTimer);
        d->loadAnimTimer = 0;
    }
    setReloadLabel_Root_(d, isOngoing);
}

void updatePadding_Root(iRoot *d) {
    if (d == NULL) return;
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
    if (toolBar) {
        /* TODO: get this from toolBar height, but it's buggy for some reason */
        const int sidebarBottomPad = isPortrait_App() ? 11 * gap_UI + bottom : 0;
        setPadding_Widget(findChild_Widget(d->widget, "sidebar"), 0, 0, 0, sidebarBottomPad);
        setPadding_Widget(findChild_Widget(d->widget, "sidebar2"), 0, 0, 0, sidebarBottomPad);
        /* TODO: There seems to be unrelated layout glitch in the sidebar where its children
           are not arranged correctly until it's hidden and reshown. */
    }
    /* Note that `handleNavBarCommands_` also adjusts padding and spacing. */
#endif
}

void dismissPortraitPhoneSidebars_Root(iRoot *d) {
    if (deviceType_App() == phone_AppDeviceType && isPortrait_App()) {
        iWidget *sidebar = findChild_Widget(d->widget, "sidebar");
        iWidget *sidebar2 = findChild_Widget(d->widget, "sidebar2");
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
                               flags_Widget(navBar) & tight_WidgetFlag
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
    return width_Rect(safeRect_Root(d)) / gap_UI < 140;
}

static void updateNavBarSize_(iWidget *navBar) {
    const iBool isPhone = deviceType_App() == phone_AppDeviceType;
    const iBool isNarrow = !isPhone && isNarrow_Root(navBar->root);
    /* Adjust navbar padding. */ {
        int hPad = isPhone && isPortrait_App() ? 0 : (isPhone || isNarrow) ? gap_UI / 2
                                                                             : gap_UI * 3 / 2;
        int vPad = gap_UI * 3 / 2;
        int topPad = !findWidget_Root("winbar") ? gap_UI / 2 : 0;
        setPadding_Widget(navBar, hPad, vPad / 3 + topPad, hPad, vPad / 2);
    }
    /* Button sizing. */
    if (isNarrow ^ ((flags_Widget(navBar) & tight_WidgetFlag) != 0)) {
        setFlags_Widget(navBar, tight_WidgetFlag, isNarrow);
        iObjectList *lists[] = {
            children_Widget(navBar),
            children_Widget(findChild_Widget(navBar, "url")),
            children_Widget(findChild_Widget(navBar, "url.buttons")),
        };
        iForIndices(k, lists) {
            iForEach(ObjectList, i, lists[k]) {
                iWidget *child = as_Widget(i.object);
                setFlags_Widget(child, tight_WidgetFlag, isNarrow);
                if (isInstance_Object(i.object, &Class_LabelWidget)) {
                    iLabelWidget *label = i.object;
                    updateSize_LabelWidget(label);
                }
            }
        }
        updateUrlInputContentPadding_(navBar);
    }
    if (isPhone) {
        static const char *buttons[] = { "navbar.back",  "navbar.forward", "navbar.sidebar",
                                         "navbar.ident", "navbar.home",    "navbar.menu" };
        iWidget *toolBar = findWidget_Root("toolbar");
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

static iBool handleNavBarCommands_(iWidget *navBar, const char *cmd) {
    if (equal_Command(cmd, "window.resized") || equal_Command(cmd, "metrics.changed")) {
        updateNavBarSize_(navBar);
        return iFalse;
    }
    else if (equal_Command(cmd, "window.reload.update")) {
        if (pointerLabel_Command(cmd, "root") == get_Root()) {
            checkLoadAnimation_Root_(get_Root());
            return iTrue;
        }
        return iFalse;
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
                postCommandf_Root(navBar->root, "open url:%s", cstr_String(searchQueryUrl_App(newUrl)));
            }
            else {
                postCommandf_Root(navBar->root,
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
                iInputWidget *url = findWidget_Root("url");
                const iString *urlStr = collect_String(suffix_Command(cmd, "url"));
                trimCache_App();
                trimMemory_App();
                visitUrl_Visited(visited_App(), withSpacesEncoded_String(urlStr), 0); /* TODO: internal URI normalization */
                postCommand_App("visited.changed"); /* sidebar will update */
                setText_InputWidget(url, urlStr);
                checkLoadAnimation_Root_(get_Root());
                dismissPortraitPhoneSidebars_Root(get_Root());
                updateNavBarIdentity_(navBar);
                updateNavDirButtons_(navBar);
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
        if (doc) {
            setText_InputWidget(findChild_Widget(navBar, "url"), url_DocumentWidget(doc));
            checkLoadAnimation_Root_(get_Root());
            updateNavBarIdentity_(navBar);
        }
        setFocus_Widget(NULL);
    }
    else if (equal_Command(cmd, "mouse.clicked") && arg_Command(cmd)) {
        iWidget *widget = pointer_Command(cmd);
        iWidget *menu = findWidget_App("doctabs.menu");
        iAssert(menu->root == navBar->root);
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
                showCollapsed_Widget(searchBar, iTrue);
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
        openMenu_Widget(menu, innerToWindow_Widget(menu, init_I2(0, -height_Widget(menu))));
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
        const int viewHeight = size_Root(get_Root()).y;
        if (arg_Command(cmd) >= 0) {
            postCommandf_App("sidebar.mode arg:%d show:1", arg_Command(cmd));
//            if (!isVisible) {
//                setVisualOffset_Widget(sidebar, viewHeight, 0, 0);
//                setVisualOffset_Widget(sidebar, 0, 400, easeOut_AnimFlag | softer_AnimFlag);
//            }
        }
        else {
            postCommandf_App("sidebar.toggle");
//            if (isVisible) {
//                setVisualOffset_Widget(sidebar, height_Widget(sidebar), 250, easeIn_AnimFlag);
//            }
//            else {
//                setVisualOffset_Widget(sidebar, viewHeight, 0, 0);
//                setVisualOffset_Widget(sidebar, 0, 400, easeOut_AnimFlag | softer_AnimFlag);
//            }
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "toolbar.showident")) {
        /* TODO: Clean this up. */
        iWidget *sidebar  = findWidget_App("sidebar");
        iWidget *sidebar2 = findWidget_App("sidebar2");
        //dismissSidebar_(sidebar, "toolbar.view");
        if (isVisible_Widget(sidebar)) {
            postCommandf_App("sidebar.toggle");
        }
        const iBool isVisible = isVisible_Widget(sidebar2);
        //        setFlags_Widget(findChild_Widget(toolBar, "toolbar.ident"), noBackground_WidgetFlag,
        //                        isVisible);
        /* If a sidebar hasn't been shown yet, it's height is zero. */
        const int viewHeight = size_Root(get_Root()).y;
        if (isVisible) {
            dismissSidebar_(sidebar2, NULL);
        }
        else {
            postCommand_App("sidebar2.mode arg:3 show:1");
            int offset = height_Widget(sidebar2);
            if (offset == 0) offset = size_Root(get_Root()).y;
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
    iWidget *navBar     = findChild_Widget(d->widget, "navbar");
    iWidget *lock       = findChild_Widget(navBar, "navbar.lock");
    iWidget *url        = findChild_Widget(d->widget, "url");
    iWidget *rightEmbed = findChild_Widget(navBar, "url.rightembed");
    iWidget *embedPad   = findChild_Widget(navBar, "url.embedpad");
    iWidget *urlButtons = findChild_Widget(navBar, "url.buttons");
    setPadding_Widget(as_Widget(url), 0, gap_UI, 0, gap_UI);
    navBar->rect.size.y = 0; /* recalculate height based on children (FIXME: shouldn't be needed) */
//    updateSize_LabelWidget((iLabelWidget *) lock);
//    updateSize_LabelWidget((iLabelWidget *) findChild_Widget(navBar, "reload"));
//    arrange_Widget(urlButtons);
    setFixedSize_Widget(embedPad, init_I2(width_Widget(urlButtons) + gap_UI / 2, 1));
//    setContentPadding_InputWidget((iInputWidget *) url, width_Widget(lock) * 0.75,
//                                  width_Widget(lock) * 0.75);
    rightEmbed->rect.pos.y = gap_UI;
    updatePadding_Root(d);
    arrange_Widget(d->widget);
    updateUrlInputContentPadding_(navBar);
    postRefresh_App();
}

void createUserInterface_Root(iRoot *d) {
    iWidget *root = d->widget = new_Widget();
    root->rect.size = get_Window()->size;
    iAssert(root->root == d);
    setId_Widget(root, "root");
    /* Children of root cover the entire window. */
    setFlags_Widget(
        root, resizeChildren_WidgetFlag | fixedSize_WidgetFlag | focusRoot_WidgetFlag, iTrue);
    setCommandHandler_Widget(root, handleRootCommands_);

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
    iWidget *navBar;
    /* Navigation bar. */ {
        navBar = new_Widget();
        setId_Widget(navBar, "navbar");
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
        iWidget *navBack;
        setId_Widget(navBack = addChildFlags_Widget(navBar, iClob(newIcon_LabelWidget(backArrow_Icon, 0, 0, "navigate.back")), collapse_WidgetFlag), "navbar.back");
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
            setMaxLayoutLines_InputWidget(url, 1);
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
                                 arrangeHorizontal_WidgetFlag | arrangeWidth_WidgetFlag |
                                     resizeHeightOfChildren_WidgetFlag |
                                     moveToParentRightEdge_WidgetFlag);
            /* Feeds refresh indicator is inside the input field. */ {
                iLabelWidget *queryInd = new_LabelWidget("${status.query} " return_Icon, NULL);
                setId_Widget(as_Widget(queryInd), "input.indicator.search");
                setTextColor_LabelWidget(queryInd, uiTextAction_ColorId);
                setBackgroundColor_Widget(as_Widget(queryInd), uiBackground_ColorId);
                setFrameColor_Widget(as_Widget(queryInd), uiTextAction_ColorId);
                setAlignVisually_LabelWidget(queryInd, iTrue);
                setNoAutoMinHeight_LabelWidget(queryInd, iTrue);
                addChildFlags_Widget(rightEmbed,
                                     iClob(queryInd),
                                     collapse_WidgetFlag | hidden_WidgetFlag);
            }
            /* Feeds refresh indicator is inside the input field. */ {
                iLabelWidget *fprog = new_LabelWidget("", NULL);
                setId_Widget(as_Widget(fprog), "feeds.progress");
                setTextColor_LabelWidget(fprog, uiTextCaution_ColorId);
                setBackgroundColor_Widget(as_Widget(fprog), uiBackground_ColorId);
                setAlignVisually_LabelWidget(fprog, iTrue);
                setNoAutoMinHeight_LabelWidget(fprog, iTrue);
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
                iLabelWidget *pin = new_LabelWidget(uiTextAction_ColorEscape leftHalf_Icon, NULL);
                setId_Widget(as_Widget(pin), "document.pinned");
                setBackgroundColor_Widget(as_Widget(pin), uiBackground_ColorId);
                setAlignVisually_LabelWidget(pin, iTrue);
                setNoAutoMinHeight_LabelWidget(pin, iTrue);
                addChildFlags_Widget(rightEmbed,
                                     iClob(pin),
                                     collapse_WidgetFlag | hidden_WidgetFlag | tight_WidgetFlag |
                                     frameless_WidgetFlag);
                updateSize_LabelWidget(pin);
            }
            iWidget *urlButtons = new_Widget();
            setId_Widget(urlButtons, "url.buttons");
            setFlags_Widget(urlButtons, embedFlags | arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
            /* Mobile page menu. */
            if (deviceType_App() != desktop_AppDeviceType) {
                iLabelWidget *pageMenuButton;
                /* In a mobile layout, the reload button is replaced with the Page/Ellipsis menu. */
                pageMenuButton = makeMenuButton_LabelWidget(pageMenuCStr_,
                    (iMenuItem[]){
                        { upArrow_Icon " ${menu.parent}", navigateParent_KeyShortcut, "navigate.parent" },
                        { upArrowBar_Icon " ${menu.root}", navigateRoot_KeyShortcut, "navigate.root" },
                        { timer_Icon " ${menu.autoreload}", 0, 0, "document.autoreload.menu" },
                        { "---", 0, 0, NULL },
                        { bookmark_Icon " ${menu.page.bookmark}", SDLK_d, KMOD_PRIMARY, "bookmark.add" },
                        { star_Icon " ${menu.page.subscribe}", subscribeToPage_KeyModifier, "feeds.subscribe" },
                        { book_Icon " ${menu.page.import}", 0, 0, "bookmark.links confirm:1" },
                        { globe_Icon " ${menu.page.translate}", 0, 0, "document.translate" },
                        { "---", 0, 0, NULL },
                        { "${menu.page.copyurl}", 0, 0, "document.copylink" },
                        { "${menu.page.copysource}", 'c', KMOD_PRIMARY, "copy" },
                        { download_Icon " " saveToDownloads_Label, SDLK_s, KMOD_PRIMARY, "document.save" } },
                    12);
                setId_Widget(as_Widget(pageMenuButton), "pagemenubutton");
                setFont_LabelWidget(pageMenuButton, uiContentBold_FontId);
                setAlignVisually_LabelWidget(pageMenuButton, iTrue);
                addChildFlags_Widget(urlButtons, iClob(pageMenuButton), embedFlags | tight_WidgetFlag);
                updateSize_LabelWidget(pageMenuButton);
            }
            /* Reload button. */ {
                iLabelWidget *reload = newIcon_LabelWidget(reloadCStr_, 0, 0, "navigate.reload");
                setId_Widget(as_Widget(reload), "reload");
                addChildFlags_Widget(urlButtons, iClob(reload), embedFlags);
                updateSize_LabelWidget(reload);
            }
            addChildFlags_Widget(as_Widget(url), iClob(urlButtons), moveToParentRightEdge_WidgetFlag);
            arrange_Widget(urlButtons);
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
#if defined (iPlatformMobile)
        const iBool isPhone = (deviceType_App() == phone_AppDeviceType);
#endif
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
        iWidget *content = findChild_Widget(root, "tabs.content");
        iSidebarWidget *sidebar1 = new_SidebarWidget(left_SideBarSide);
        addChildPos_Widget(content, iClob(sidebar1), front_WidgetAddPos);
        iSidebarWidget *sidebar2 = new_SidebarWidget(right_SideBarSide);
        if (deviceType_App() != phone_AppDeviceType) {
            addChildPos_Widget(content, iClob(sidebar2), back_WidgetAddPos);
        }
        else {
            /* The identities sidebar is always in the main area. */
            addChild_Widget(findChild_Widget(root, "stack"), iClob(sidebar2));
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
        setEnterInsertsLF_InputWidget(input, iFalse);
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
        addChild_Widget(root, iClob(toolBar));
        setId_Widget(toolBar, "toolbar");
        setCommandHandler_Widget(toolBar, handleToolBarCommands_);
        setFlags_Widget(toolBar, moveToParentBottomEdge_WidgetFlag |
                                     parentCannotResizeHeight_WidgetFlag |
                                     resizeWidthOfChildren_WidgetFlag |
                                     arrangeHeight_WidgetFlag | arrangeHorizontal_WidgetFlag |
                                     commandOnClick_WidgetFlag |
                                     drawBackgroundToBottom_WidgetFlag, iTrue);
        setBackgroundColor_Widget(toolBar, tmBannerBackground_ColorId);
        setId_Widget(addChildFlags_Widget(toolBar,
                                          iClob(newLargeIcon_LabelWidget("\U0001f870", "navigate.back")),
                                          frameless_WidgetFlag),
                     "toolbar.back");
        setId_Widget(addChildFlags_Widget(toolBar,
                                          iClob(newLargeIcon_LabelWidget("\U0001f872", "navigate.forward")),
                                          frameless_WidgetFlag),
                     "toolbar.forward");
        setId_Widget(addChildFlags_Widget(toolBar,
                                          iClob(newLargeIcon_LabelWidget("\U0001f464", "toolbar.showident")),
                                          frameless_WidgetFlag),
                     "toolbar.ident");
        setId_Widget(addChildFlags_Widget(toolBar,
                                          iClob(newLargeIcon_LabelWidget(book_Icon, "toolbar.showview arg:-1")),
                                          frameless_WidgetFlag | commandOnClick_WidgetFlag),
                     "toolbar.view");
        iLabelWidget *menuButton = makeMenuButton_LabelWidget("\U0001d362", phoneNavMenuItems_,
                                                              iElemCount(phoneNavMenuItems_));
        setFont_LabelWidget(menuButton, uiLabelLarge_FontId);
        setId_Widget(as_Widget(menuButton), "toolbar.navmenu");
        addChildFlags_Widget(toolBar, iClob(menuButton), frameless_WidgetFlag);
        iForEach(ObjectList, i, children_Widget(toolBar)) {
            iLabelWidget *btn = i.object;
            setFlags_Widget(i.object, noBackground_WidgetFlag, iTrue);
            setTextColor_LabelWidget(i.object, tmBannerIcon_ColorId);
            //            setBackgroundColor_Widget(i.object, tmBannerSideTitle_ColorId);
        }
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
    updatePadding_Root(d);
    /* Global context menus. */ {
        iWidget *tabsMenu = makeMenu_Widget(
            root,
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
            makeMenu_Widget(root,
                            (iMenuItem[]){
                                { leftHalf_Icon " ${menu.sidebar.left}", 0, 0, "sidebar.toggle" },
                                { rightHalf_Icon " ${menu.sidebar.right}", 0, 0, "sidebar2.toggle" },
                                },
                            deviceType_App() == phone_AppDeviceType ? 1 : 2);
        iWidget *clipMenu = makeMenu_Widget(root,
                                            (iMenuItem[]){
                                                { scissor_Icon " ${menu.cut}", 0, 0, "input.copy cut:1" },
                                                { clipCopy_Icon " ${menu.copy}", 0, 0, "input.copy" },
                                                { "---", 0, 0, NULL },
                                                { clipboard_Icon " ${menu.paste}", 0, 0, "input.paste" },
                                                },
                                            4);
        iWidget *splitMenu = makeMenu_Widget(root, (iMenuItem[]){
            { "${menu.split.merge}", '1', 0, "ui.split arg:0" },
            { "${menu.split.swap}", SDLK_x, 0, "ui.split swap:1" },
            { "---", 0, 0, NULL },
            { "${menu.split.horizontal}", '3', 0, "ui.split arg:3 axis:0" },
            { "${menu.split.horizontal} 1:2", SDLK_d, 0, "ui.split arg:1 axis:0" },
            { "${menu.split.horizontal} 2:1", SDLK_e, 0, "ui.split arg:2 axis:0" },
            { "---", 0, 0, NULL },
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
        addAction_Widget(root, 'l', KMOD_PRIMARY, "navigate.focus");
        addAction_Widget(root, 'f', KMOD_PRIMARY, "focus.set id:find.input");
        addAction_Widget(root, '1', KMOD_PRIMARY, "sidebar.mode arg:0 toggle:1");
        addAction_Widget(root, '2', KMOD_PRIMARY, "sidebar.mode arg:1 toggle:1");
        addAction_Widget(root, '3', KMOD_PRIMARY, "sidebar.mode arg:2 toggle:1");
        addAction_Widget(root, '4', KMOD_PRIMARY, "sidebar.mode arg:3 toggle:1");
        addAction_Widget(root, '5', KMOD_PRIMARY, "sidebar.mode arg:4 toggle:1");
        addAction_Widget(root, '1', rightSidebar_KeyModifier, "sidebar2.mode arg:0 toggle:1");
        addAction_Widget(root, '2', rightSidebar_KeyModifier, "sidebar2.mode arg:1 toggle:1");
        addAction_Widget(root, '3', rightSidebar_KeyModifier, "sidebar2.mode arg:2 toggle:1");
        addAction_Widget(root, '4', rightSidebar_KeyModifier, "sidebar2.mode arg:3 toggle:1");
        addAction_Widget(root, '5', rightSidebar_KeyModifier, "sidebar2.mode arg:4 toggle:1");
        addAction_Widget(root, SDLK_j, KMOD_PRIMARY, "splitmenu.open");
    }
    updateMetrics_Root(d);
    updateNavBarSize_(navBar);
    if (deviceType_App() == phone_AppDeviceType) {
        const float sidebarWidth = width_Widget(root) / (float) gap_UI;
        setWidth_SidebarWidget(findChild_Widget(root, "sidebar"), sidebarWidth);
        setWidth_SidebarWidget(findChild_Widget(root, "sidebar2"), sidebarWidth);
    }
}

void showToolbars_Root(iRoot *d, iBool show) {
    /* The toolbar is only used on phone portrait layout. */
    if (isLandscape_App()) return;
    iWidget *toolBar = findChild_Widget(d->widget, "toolbar");
    if (!toolBar) return;
    const int height = size_Root(d).y - top_Rect(boundsWithoutVisualOffset_Widget(toolBar));
    if (show && !isVisible_Widget(toolBar)) {
        setFlags_Widget(toolBar, hidden_WidgetFlag, iFalse);
        setVisualOffset_Widget(toolBar, 0, 200, easeOut_AnimFlag);
    }
    else if (!show && isVisible_Widget(toolBar)) {
        /* Close any menus that open via the toolbar. */
        closeMenu_Widget(findChild_Widget(findWidget_App("toolbar.navmenu"), "menu"));
        closeMenu_Widget(findChild_Widget(toolBar, "toolbar.menu"));
        setFlags_Widget(toolBar, hidden_WidgetFlag, iTrue);
        setVisualOffset_Widget(toolBar, height, 200, easeOut_AnimFlag);
    }
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
    iRect rect = { zero_I2(), size_Root(d) };
#if defined (iPlatformAppleMobile)
    float left, top, right, bottom;
    safeAreaInsets_iOS(&left, &top, &right, &bottom);
    adjustEdges_Rect(&rect, top, -right, -bottom, left);
#endif
    return rect;
}

iInt2 visibleSize_Root(const iRoot *d) {
    return addY_I2(size_Root(d), -get_Window()->keyboardHeight);
}
