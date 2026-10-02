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

#include "prefsdialog.h"

#include "app.h"
#include "bindingswidget.h"
#include "command.h"
#include "defs.h"
#include "fontpack.h"
#include "gamepad.h"
#include "inputwidget.h"
#include "keys.h"
#include "labelwidget.h"
#include "mobile.h"
#include "render/text.h"
#include "root.h"
#include <lagrange/snippets.h>
#include "snippetwidget.h"
#include "touch.h"
#include "scopedprefs.h"
#include "util.h"
#include "widget.h"
#include "window.h"

#if defined (iPlatformAppleDesktop)
#   include "platform/macos.h"
#endif

#include <the_Foundation/math.h>
#include <the_Foundation/path.h>

static void expandInputFieldWidth_(iInputWidget *input) {
    if (!input) return;
    iWidget *page = as_Widget(input)->parent->parent->parent->parent; /* tabs > page > values > input */
    as_Widget(input)->rect.size.x =
        right_Rect(bounds_Widget(page)) - left_Rect(bounds_Widget(constAs_Widget(input)));
}

static void addFontButtons_(iWidget *parent, const char *id) {
    const iArray *items = fontItems_ScopedPrefs(id);
    size_t widestIndex = findWidestLabel_MenuItem(constData_Array(items), size_Array(items));
    iLabelWidget *button = makeMenuButton_LabelWidget(constValue_Array(items, widestIndex, iMenuItem).label,
                                                      constData_Array(items), size_Array(items));
    setBackgroundColor_Widget(findChild_Widget(as_Widget(button), "menu"),
                              uiBackgroundMenu_ColorId);
    setId_Widget(as_Widget(button), format_CStr("prefs.font.%s", id));
    addChildFlags_Widget(parent, iClob(button), alignLeft_WidgetFlag);
}

/*----------------------------------------------------------------------------------------------*/

void updatePreferencesLayout_Widget(iWidget *prefs) {
    if (!prefs || deviceType_App() != desktop_AppDeviceType) {
        return;
    }
    /* Doing manual layout here because the widget arranging logic isn't sophisticated enough. */
    /* TODO: Make the arranging more sophisticated to automate this. */
    static const char *inputIds[] = {
        "prefs.searchurl",
        "prefs.downloads",
        "prefs.userfont",
        "prefs.ca.file",
        "prefs.ca.path",
        "prefs.proxy.gemini",
        "prefs.proxy.gopher",
        "prefs.proxy.http",
        "prefs.socks.server",
        "prefs.socks.user",
        "prefs.socks.password",
    };
    iWidget *tabs = findChild_Widget(prefs, "prefs.tabs");
    tabs->rect.size = zero_I2();
    /* Input fields expand to the right edge. */
    /* TODO: Add an arrangement flag for this. */
    iForIndices(i, inputIds) {
        iInputWidget *input = findChild_Widget(tabs, inputIds[i]);
        if (input) {
            as_Widget(input)->rect.size.x = 0;
        }
    }
    iWidget *bindings = findChild_Widget(prefs, "bindings");
    if (bindings) {
        bindings->rect.size.x = 0;
    }
    resizeToLargestPage_Widget(tabs);
    arrange_Widget(prefs);
    iForIndices(i, inputIds) {
        expandInputFieldWidth_(findChild_Widget(tabs, inputIds[i]));
    }
}

static void addDialogToggleGroupWithLabels_(iWidget *headings, iWidget *values, const char *title,
                                  const char *toggleIds[], const char *toggleLabels[], size_t n) {
    addChild_Widget(headings, iClob(makeHeading_Widget(title)));
    iWidget *group = new_Widget();
    for (size_t i = 0; i < n && toggleIds[i]; i++) {
        iWidget *tog;
        setTextCStr_LabelWidget(
            addChild_Widget(group, tog = iClob(makeToggle_Widget(toggleIds[i]))),
            toggleLabels ? toggleLabels[i] : format_CStr("${%s}", toggleIds[i]));
        setFlags_Widget(tog, fixedWidth_WidgetFlag, iFalse);
        updateSize_LabelWidget((iLabelWidget *) tog);
    }
    addChildFlags_Widget(
        values, iClob(group), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
}

static void addDialogToggleGroup_(iWidget *headings, iWidget *values, const char *title,
                                  const char *toggleIds[], size_t n) {
    addDialogToggleGroupWithLabels_(headings, values, title, toggleIds, NULL, n);
}

static const char *returnKeyBehaviorStr_(int behavior) {
    iString *nl = collectNew_String();
    iString *ac = collectNew_String();
    toString_Sym(SDLK_RETURN, lineBreakKeyMod_ReturnKeyBehavior(behavior), nl);
    toString_Sym(SDLK_RETURN, acceptKeyMod_ReturnKeyBehavior(behavior), ac);
    return format_CStr("${prefs.returnkey.linebreak} " uiTextAction_ColorEscape
                       "%s" restore_ColorEscape
                       "    ${prefs.returnkey.accept} " uiTextAction_ColorEscape "%s",
                       cstr_String(nl),
                       cstr_String(ac));
}

static const iArray *gamepadButtonItems_(const char *cmd) {
    iArray *items = collectNew_Array(sizeof(iMenuItem));
    pushBackN_Array(
        items,
        (iMenuItem[]) { { "${prefs.gamepad.primary}", 0, 0, format_CStr("%s arg:0", cmd) },
                        { "${prefs.gamepad.secondary}", 0, 0, format_CStr("%s arg:1", cmd) },
                        { "${prefs.gamepad.cancel}", 0, 0, format_CStr("%s arg:2", cmd) },
                        { "${prefs.gamepad.mainmenu}", 0, 0, format_CStr("%s arg:3", cmd) },
                        { "${prefs.gamepad.pagemenu}", 0, 0, format_CStr("%s arg:4", cmd) },
                        { "${prefs.gamepad.sidebar}", 0, 0, format_CStr("%s arg:5", cmd) },
                        { "${prefs.gamepad.reload}", 0, 0, format_CStr("%s arg:6", cmd) },
                        { "${prefs.gamepad.editurl}", 0, 0, format_CStr("%s arg:7", cmd) },
                        { "---" },
                        { "\u2014", 0, 0, format_CStr("%s arg:-1", cmd) },
                        { NULL } },
        11);
    return items;
}

#if defined (LAGRANGE_USE_GAMEPAD)

iDeclareType(GamepadButtonInfo);

struct Impl_GamepadButtonInfo {
    int         button;
    iBool       trigger;
    const char *cmd;
};

static iArray *gamepadButtonInfo_(void) {
    iArray *info = collectNew_Array(sizeof(iGamepadButtonInfo));
    for (int t = 0; t <= 1; t++) {
        for (int b = SDL_GAMEPAD_BUTTON_SOUTH; b <= SDL_GAMEPAD_BUTTON_START; b++) {
            if (b > SDL_GAMEPAD_BUTTON_NORTH && t) continue; /* trigger only for A, B, X, Y */
            pushBack_Array(info,
                           (iGamepadButtonInfo[]) {
                               b, t, format_CStr("gamepad.set trig:%d button:%d", t, b) });
        }
    }
    return info;
};

#endif /* LAGRANGE_USE_GAMEPAD */

iWidget *makePreferences_Widget(void) {
    /* Common items. */
    /* clang-format off */
    const iMenuItem langItems[] = {
        /* Latin */
        { u8"Čeština - cs", 0, 0, "uilang id:cs" },
        { u8"Deutsch - de", 0, 0, "uilang id:de" },
        { u8"English - en", 0, 0, "uilang id:en" },
        { u8"Español - es", 0, 0, "uilang id:es" },
        { u8"Español (México) - es", 0, 0, "uilang id:es_MX" },
        { u8"Esperanto - eo", 0, 0, "uilang id:eo" },
        { u8"Euskara - eu", 0, 0, "uilang id:eu" },
        { u8"Français - fr", 0, 0, "uilang id:fr" },
        { u8"Galego - gl", 0, 0, "uilang id:gl" },
        { u8"Interlingua - ia", 0, 0, "uilang id:ia" },
        { u8"Interlingue - ie", 0, 0, "uilang id:ie" },
        { u8"Interslavic - isv", 0, 0, "uilang id:isv" },
        { u8"Italiano - it", 0, 0, "uilang id:it" },
        { u8"Magyar - hu", 0, 0, "uilang id:hu" },
        { u8"Nederlands - nl", 0, 0, "uilang id:nl" },
        { u8"Polski - pl", 0, 0, "uilang id:pl" },
        { u8"Samogitian - sgs", 0, 0, "uilang id:sgs" },
        { u8"Slovak - sk", 0, 0, "uilang id:sk" },
        { u8"Suomi - fi", 0, 0, "uilang id:fi" },
        { u8"Toki pona - tok", 0, 0, "uilang id:tok" },
        { u8"Türkçe - tr", 0, 0, "uilang id:tr" },
        { "---" },
        /* Cyrillic */
        { u8"Русский - ru", 0, 0, "uilang id:ru" },
        { u8"Српски - sr", 0, 0, "uilang id:sr" },
        { u8"Українська - uk", 0, 0, "uilang id:uk" },
        { "---" },
        /* CJK */
        { u8"简体中文 - zh", 0, 0, "uilang id:zh_Hans" },
        { u8"繁體/正體中文 - zh", 0, 0, "uilang id:zh_Hant" },
        { u8"日本語 - ja", 0, 0, "uilang id:ja" },
        { NULL }
    };
    const iMenuItem feedIntervalItems[] = {
        { "${prefs.feedinterval.manual}", 0, 0, format_CStr("feedinterval.set arg:%d", manual_FeedInterval) },
        { formatCStrs_Lang("num.minutes.n", 30), 0, 0, format_CStr("feedinterval.set arg:%d", thirtyMinutes_FeedInterval) },
        { formatCStrs_Lang("num.hours.n", 1), 0, 0, format_CStr("feedinterval.set arg:%d", oneHour_FeedInterval) },
        { formatCStrs_Lang("num.hours.n", 2), 0, 0, format_CStr("feedinterval.set arg:%d", twoHours_FeedInterval) },
        { formatCStrs_Lang("num.hours.n", 4), 0, 0, format_CStr("feedinterval.set arg:%d", fourHours_FeedInterval) },
        { formatCStrs_Lang("num.hours.n", 8), 0, 0, format_CStr("feedinterval.set arg:%d", eightHours_FeedInterval) },
        { "${reload.onceperday}", 0, 0, format_CStr("feedinterval.set arg:%d", oneDay_FeedInterval) },
        { NULL }
    };
    /* clang-format on */
    const iMenuItem returnKeyBehaviorItems[] = {
        { returnKeyBehaviorStr_(default_ReturnKeyBehavior),
          0,
          0,
          format_CStr("returnkey.set arg:%d", default_ReturnKeyBehavior) },
#if !defined (iPlatformTerminal)
        { returnKeyBehaviorStr_(RETURN_KEY_BEHAVIOR(0, shift_ReturnKeyFlag)),
          0,
          0,
          format_CStr("returnkey.set arg:%d", RETURN_KEY_BEHAVIOR(0, shift_ReturnKeyFlag)) },
        { returnKeyBehaviorStr_(acceptWithPrimaryMod_ReturnKeyBehavior),
          0,
          0,
          format_CStr("returnkey.set arg:%d", acceptWithPrimaryMod_ReturnKeyBehavior) },
#else
        { returnKeyBehaviorStr_(RETURN_KEY_BEHAVIOR(gui_ReturnKeyFlag, 0)),
          0,
          0,
          format_CStr("returnkey.set arg:%d", RETURN_KEY_BEHAVIOR(gui_ReturnKeyFlag, 0)) },
#endif
        { returnKeyBehaviorStr_(onlyWithMods_ReturnKeyBehavior),
          0,
          0,
          format_CStr("returnkey.set arg:%d", onlyWithMods_ReturnKeyBehavior) },
        { NULL }
    };
    iMenuItem toolbarActionItems[2][max_ToolbarAction + 1];
    iZap(toolbarActionItems);
    for (int j = 0; j < 2; j++) {
        int index = 0;
        for (int i = 0; i < max_ToolbarAction; i++) {
            if (deviceType_App() == phone_AppDeviceType &&
                (i == rightSidebar_ToolbarAction || i == leftSidebar_ToolbarAction)) {
                continue;
            }
            toolbarActionItems[j][index].label = toolbarActions_Mobile[i].label;
            toolbarActionItems[j][index].command =
                format_CStr("toolbar.action.set arg:%d button:%d", i, j);
            index++;
        }
    }
    const iMenuItem accentItems[max_ColorAccent] = {
#if defined (iPlatformAppleDesktop)
        { circle_Icon " ${prefs.accent.system}", 0, 0, "accent.set arg:6" },
#endif
        { circle_Icon " ${prefs.accent.teal}", 0, 0, "accent.set arg:0" },
        { circle_Icon " ${prefs.accent.orange}", 0, 0, "accent.set arg:1" },
        { circle_Icon " ${prefs.accent.red}", 0, 0, "accent.set arg:2" },
        { circle_Icon " ${prefs.accent.green}", 0, 0, "accent.set arg:3" },
        { circle_Icon " ${prefs.accent.blue}", 0, 0, "accent.set arg:4" },
        { circle_Icon " ${prefs.accent.gray}", 0, 0, "accent.set arg:5" }
    };
    /* Create the Preferences UI. */
    if (isUsingPanelLayout_Mobile()) {
        const iMenuItem pinSplitItems[] = {
            { "button id:prefs.pinsplit.0 label:prefs.pinsplit.none",  0, 0, "pinsplit.set arg:0" },
            { "button id:prefs.pinsplit.1 label:prefs.pinsplit.left",  0, 0, "pinsplit.set arg:1" },
            { "button id:prefs.pinsplit.2 label:prefs.pinsplit.right", 0, 0, "pinsplit.set arg:2" },
            { NULL }
        };
        const iMenuItem themeItems[] = {
            { "button id:prefs.theme.0 label:prefs.theme.black", 0, 0, "theme.set arg:0" },
            { "button id:prefs.theme.1 label:prefs.theme.dark",  0, 0, "theme.set arg:1" },
            { "button id:prefs.theme.2 label:prefs.theme.light", 0, 0, "theme.set arg:2" },
            { "button id:prefs.theme.3 label:prefs.theme.white", 0, 0, "theme.set arg:3" },
            { NULL }
        };
        const iMenuItem accentItems[] = {
            { "button id:prefs.accent.0 label:prefs.accent.teal", 0, 0, "accent.set arg:0" },
            { "button id:prefs.accent.1 label:prefs.accent.orange", 0, 0, "accent.set arg:1" },
            { "button id:prefs.accent.2 label:prefs.accent.red", 0, 0, "accent.set arg:2" },
            { "button id:prefs.accent.3 label:prefs.accent.green", 0, 0, "accent.set arg:3" },
            { "button id:prefs.accent.4 label:prefs.accent.blue", 0, 0, "accent.set arg:4" },
            { "button id:prefs.accent.5 label:prefs.accent.gray", 0, 0, "accent.set arg:5" },
            { NULL }
        };
        const iMenuItem generalPanelItems[] = {
            { "title id:heading.prefs.general" },
            { "heading text:${prefs.searchurl}" },
            { "input id:prefs.searchurl url:1 noheading:1" },
            { "padding" },
            { "toggle id:prefs.bookmarks.addbottom" },
            { "toggle id:prefs.dataurl.openimages" },
            { "toggle id:prefs.archive.openindex" },
            { "toggle id:prefs.font.warnmissing" },
            { "radio device:1 id:prefs.pinsplit", 0, 0, (const void *) pinSplitItems },
            { "padding" },
            { "dropdown id:prefs.feedinterval", 0, 0, (const void *) feedIntervalItems },
            { "padding" },
            { "dropdown id:prefs.uilang", 0, 0, (const void *) langItems },
            { "toggle id:prefs.time.24h" },
            { "padding" },
            { NULL }
        };
        const iMenuItem uiPanelItems[] = {
            { "title id:heading.prefs.ui" },
            { "padding arg:0.667" },
            { "dropdown device:0 id:prefs.returnkey", 0, 0, (const void *) returnKeyBehaviorItems },
            { "toggle device:2 id:prefs.hidetoolbarscroll" },
            { "toggle id:prefs.bottomnavbar text:${LC:prefs.bottomnavbar}" },
            { "toggle id:prefs.bottomtabbar text:${LC:prefs.bottomtabbar}" },
            { "toggle id:prefs.hidetabs" },
            { "padding" },
            { "toggle id:prefs.swipe.edge" },
            { "toggle id:prefs.swipe.page" },
            { "heading device:2 id:heading.prefs.toolbaractions" },
            { "dropdown device:2 id:prefs.toolbaraction1", 0, 0, (const void *) toolbarActionItems[0] },
            { "dropdown device:2 id:prefs.toolbaraction2", 0, 0, (const void *) toolbarActionItems[1] },
            { "heading device:1 id:heading.prefs.toolbartabs.left" },
            { "heading device:2 id:heading.prefs.toolbartabs" },
            { "toggle id:prefs.sidebar.enabled.0 text:${LC:sidebar.bookmarks}" },
            { "toggle id:prefs.sidebar.enabled.1 text:${LC:sidebar.feeds}" },
            { "toggle id:prefs.sidebar.enabled.2 text:${LC:sidebar.subscriptions}" },
            { "toggle id:prefs.sidebar.enabled.4 text:${LC:sidebar.outline}" },
            { "toggle id:prefs.sidebar.enabled.5 text:${LC:sidebar.structure}" },
            { "toggle id:prefs.sidebar.enabled.6 text:${LC:sidebar.documents}" },
            { "toggle id:prefs.sidebar.enabled.7 text:${LC:sidebar.history}" },
            { "heading device:1 id:heading.prefs.toolbartabs.right" },
            { "toggle device:1 id:prefs.sidebar2.enabled.0 text:${LC:sidebar.bookmarks}" },
            { "toggle device:1 id:prefs.sidebar2.enabled.1 text:${LC:sidebar.feeds}" },
            { "toggle device:1 id:prefs.sidebar2.enabled.2 text:${LC:sidebar.subscriptions}" },
            { "toggle device:1 id:prefs.sidebar2.enabled.4 text:${LC:sidebar.outline}" },
            { "toggle device:1 id:prefs.sidebar2.enabled.5 text:${LC:sidebar.structure}" },
            { "toggle device:1 id:prefs.sidebar2.enabled.6 text:${LC:sidebar.documents}" },
            { "toggle device:1 id:prefs.sidebar2.enabled.7 text:${LC:sidebar.history}" },
            { "heading id:heading.prefs.sizing" },
            { "input id:prefs.uiscale maxlen:5" },
            { "padding" },
            { NULL }
        };
        const iMenuItem colorPanelItems[] = {
            { "title id:heading.prefs.colors" },
            { "padding arg:0.667" },
#if !defined (iPlatformLinuxMobile)
            { "toggle id:prefs.ostheme" },
#endif
            { "radio id:prefs.theme", 0, 0, (const void *) themeItems },
            { "radio horizontal:1 rowlen:3 id:prefs.accent", 0, 0, (const void *) accentItems },
            { "padding" },
            { NULL }
        };
        const iMenuItem networkPanelItems[] = {
            { "title id:heading.prefs.network" },
            { "heading text:${prefs.proxy.gemini}" },
            { "input id:prefs.proxy.gemini noheading:1" },
            { "heading text:${prefs.proxy.gopher}" },
            { "input id:prefs.proxy.gopher noheading:1" },
            { "heading text:${prefs.proxy.http}" },
            { "input id:prefs.proxy.http noheading:1" },
            { "heading id:heading.prefs.socks" },
            { "toggle id:prefs.socks" },
            { "input id:prefs.socks.server hint:hint.socks.server" },
            { "input id:prefs.socks.user" },
            { "input id:prefs.socks.password sensitive:1" },
            { "padding" },
            { "input id:prefs.cachesize maxlen:4 selectall:1 unit:mb" },
            { "input id:prefs.memorysize maxlen:4 selectall:1 unit:mb" },
            { "padding" },
            { "toggle id:prefs.decodeurls" },
            { "input id:prefs.urlsize maxlen:7 selectall:1" },
            { "padding" },
            { "toggle id:prefs.ipv6" },
            { "toggle id:prefs.redirect.allowscheme" },
            { "toggle id:prefs.warn.security" },
            { "padding" },
            { NULL }
        };
        const iMenuItem identityPanelItems[] = {
            { "title id:sidebar.identities" },
            { "certlist" },
            { "navi.action id:prefs.ident.import text:" import_Icon, 0, 0, "ident.import" },
            { "navi.action id:prefs.ident.new text:" add_Icon, 0, 0, "ident.new" },
            { NULL }
        };
        iString *aboutText = collectNew_String(); {
            setCStr_String(aboutText, "Lagrange " LAGRANGE_APP_VERSION);
#if defined (iPlatformAppleMobile)
            appendFormat_String(aboutText, " (" LAGRANGE_IOS_VERSION ") %s" LAGRANGE_IOS_BUILD_DATE,
                                escape_Color(uiTextDim_ColorId));
#endif
#if defined (iPlatformAndroidMobile)
            appendFormat_String(aboutText, " (" LAGRANGE_ANDROID_VERSION ") %s" LAGRANGE_ANDROID_BUILD_DATE,
                                escape_Color(uiTextDim_ColorId));
#endif
        }
        const iMenuItem snippetPanelItems[] = {
            { "title id:heading.prefs.snip" },
            { "snippetlist" },
            { "navi.action id:sniped.new text:" add_Icon, 0, 0, "sniped.new" },
            { NULL }
        };
        const iMenuItem userPanelItems[] = {
            { "title id:heading.prefs.user" },
            { "padding arg:0.667" },
            { "button text:" export_Icon " " uiTextAction_ColorEscape "${menu.export}", 0, 0, "export" },
            { "button text:" import_Icon " " uiTextAction_ColorEscape "${menu.import}", 0, 0, "file.open" },
            { "padding" },
            { "button text:" book_Icon " ${menu.bookmarks.list}", 0, 0, "!open url:about:bookmarks" },
            { "button text:" bookmark_Icon " ${menu.bookmarks.bytag}", 0, 0, "!open url:about:bookmarks?tags" },
            { "button text:" clock_Icon " ${macos.menu.bookmarks.bytime}", 0, 0, "!open url:about:bookmarks?created" },
            { "padding" },
            { "button text:" star_Icon " ${menu.feeds.entrylist}", 0, 0, "!open url:about:feeds" },
            { "padding" },
            { "button text:" download_Icon " ${menu.downloads}", 0, 0, "downloads.open" },
            { "padding" },
            { "button text:" fontpack_Icon " " uiTextAction_ColorEscape "${menu.fonts}", 0, 0, "!open url:about:fonts" },
            { NULL }
        };
        const iMenuItem supportPanelItems[] = {
            { "title id:heading.prefs.support" },
            { format_CStr("heading text:%s", cstr_String(aboutText)) },
            { "button text:" star_Icon " ${menu.releasenotes}", 0, 0, "!open url:about:version" },
            { "button text:" info_Icon " ${menu.help}", 0, 0, "!open url:about:help" },
            { "padding" },
            { "button text:" globe_Icon " " uiTextAction_ColorEscape "${menu.website}",
              0,
              0,
              "!open url:https://gmi.skyjake.fi/lagrange" },
            { "button text:" person_Icon " " uiTextAction_ColorEscape "@jk@skyjake.fi",
              0,
              0,
              "!open default:1 url:https://skyjake.fi/@jk" },
            { "button text:" envelope_Icon " " uiTextAction_ColorEscape "${menu.email}",
              0,
              0,
              "!open default:1 url:mailto:jaakko.keranen@iki.fi" },
            { "padding" },
            { "button text:" info_Icon " ${menu.aboutpages}", 0, 0, "!open url:about:about" },
            { "button text:" bug_Icon " ${menu.debug}", 0, 0, "!open url:about:debug" },
            { NULL }
        };
        iArray *mainItems = collectNew_Array(sizeof(iMenuItem));
        pushBackN_Array(mainItems, (iMenuItem[]){
            { "padding arg:0.333" },
            { "title id:heading.settings" },
            { "padding arg:0.167" },
            { "panel text:" gear_Icon " ${heading.prefs.general}", 0, 0, (const void *) generalPanelItems },
            { "panel icon:0x1f4f1 id:heading.prefs.ui", 0, 0, (const void *) uiPanelItems },
            { "panel icon:0x1f5a7 id:heading.prefs.network", 0, 0, (const void *) networkPanelItems },
            { "panel noscroll:1 text:" person_Icon " ${sidebar.identities}", 0, 0, (const void *) identityPanelItems },
            { "padding" },
            { "panel icon:0x1f58c id:heading.prefs.colors", 0, 0, (const void *) colorPanelItems },
            { "padding" },
        }, 10);
        appendPanels_ScopedPrefs(mainItems, iTrue);
        pushBackN_Array(mainItems, (iMenuItem[]){
            { "padding" },
            { "panel icon:0x1f4e6 id:heading.prefs.user", 0, 0, (const void *) userPanelItems },
            { "panel icon:0x1f4cb id:heading.prefs.snip", 0, 0, (const void *) snippetPanelItems },
            { "padding" },
            { "panel text:" info_Icon " ${heading.prefs.support}", 0, 0, (const void *) supportPanelItems },
            { NULL }
        }, 6);
#if defined (LAGRANGE_USE_GAMEPAD)
        iBool haveGamepad = iFalse;
        if (isAvailable_Gamepad()) {
            /* The gamepad settings only appears when the controller is available (maybe not
               initialized, though). */
            iArray *gamepadItems = collectNew_Array(sizeof(iMenuItem));
            pushBackN_Array(gamepadItems,
                            (iMenuItem[]) { { "title id:heading.prefs.gamepad" },
                                            { "padding arg:0.667" },
                                            { "toggle id:prefs.gamepad" },
                                            { "padding" } },
                            4);
            if (isConnected_Gamepad(gamepad_App())) {
                haveGamepad = iTrue;
                iConstForEach(Array, btInfo, gamepadButtonInfo_()) {
                    const iGamepadButtonInfo *info = btInfo.value;
                    pushBack_Array(
                            gamepadItems,
                            /* This ID is translated to "trig:%d button:%d", we can't include spaces in
                               the ID in the panel item syntax. */
                            &(iMenuItem) {format_CStr("dropdown id:prefs.gamepad.%d.%d text:%s%s",
                                                      info->button,
                                                      info->trigger,
                                                      info->trigger ? "${prefs.gamepad.triggermod}"
                                                                    : "",
                                                      buttonName_Gamepad(gamepad_App(),
                                                                         info->button)),
                                          0,
                                          0,
                                          constData_Array(gamepadButtonItems_(info->cmd))});
                }
                pushBack_Array(gamepadItems, &(iMenuItem) {NULL});
                insert_Array(mainItems,
                             5 /* after UI */,
                             &(iMenuItem) {"panel icon:0x1f3ae id:heading.prefs.gamepad",
                                           0,
                                           0,
                                           constData_Array(gamepadItems)});
            }
        }
#endif /* LAGRANGE_USE_GAMEPAD */
        iWidget *dlg = makePanels_Mobile("prefs", constData_Array(mainItems), NULL, 0);
#if defined (LAGRANGE_USE_GAMEPAD)
        if (haveGamepad) {
            iConstForEach(Array, i, gamepadButtonInfo_()) {
                const iGamepadButtonInfo *info = i.value;
                updateDropdownSelection_LabelWidget(
                    findChild_Widget(dlg, info->cmd),
                    format_CStr(" arg:%d", findAction_Gamepad(info->button, info->trigger)));
            }
        }
#endif /* LAGRANGE_USE_GAMEPAD */
        setupSheetTransition_Mobile(dlg, incoming_TransitionFlag | dialogTransitionDir_Widget(dlg));
        return dlg;
    }
    iWidget *dlg  = makeSheet_Widget("prefs");
    iWidget *content;
    iWidget *tabs = makeDialogVerticalTabs_Widget(dlg, "prefs.tabs", "${heading.prefs}",
                                                  "prefs.title", &content);
    iWidget *headings, *values;
    /* General settings. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs,
                                                   gear_Icon " ${heading.prefs.general}",
                                                   red_ColorId,
                                                   '1',
                                                   &headings,
                                                   &values),
                     "prefs.page.general");
#if defined (LAGRANGE_ENABLE_DOWNLOAD_EDIT)
        addPrefsInputWithHeading_Widget(headings, values, "prefs.downloads", iClob(new_InputWidget(0)));
#endif
        iInputWidget *searchUrl;
        addPrefsInputWithHeading_Widget(headings, values, "prefs.searchurl", iClob(searchUrl = new_InputWidget(0)));
        setUrlContent_InputWidget(searchUrl, iTrue);
        addDialogPadding_Widget(headings, values);
        addDialogToggle_Widget(headings, values, "${prefs.retaintabs}", "prefs.retaintabs");
        if (deviceType_App() != phone_AppDeviceType) {
            addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.pinsplit}")));
            iWidget *pinSplit = new_Widget();
            /* Split mode document pinning. */ {
                addRadioButton_Widget(pinSplit, "prefs.pinsplit.0", "${prefs.pinsplit.none}", "pinsplit.set arg:0");
                addRadioButton_Widget(pinSplit, "prefs.pinsplit.1", "${prefs.pinsplit.left}", "pinsplit.set arg:1");
                addRadioButton_Widget(pinSplit, "prefs.pinsplit.2", "${prefs.pinsplit.right}", "pinsplit.set arg:2");
            }
            addChildFlags_Widget(values, iClob(pinSplit), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        }
        /* Feed refresh interval. */
        addDialogPadding_Widget(headings, values);
        addDialogDropMenu_Widget(headings,
                           values,
                           "${prefs.feedinterval}",
                           feedIntervalItems,
                           iInvalidSize,
                           "prefs.feedinterval");
        addDialogPadding_Widget(headings, values);
        /* UI languages. */ {
            iArray *uiLangs = collectNew_Array(sizeof(iMenuItem));
            pushBackN_Array(uiLangs, langItems, iElemCount(langItems) - 1);
            addDialogDropMenu_Widget(headings, values, "${prefs.uilang}",
                               constData_Array(uiLangs), size_Array(uiLangs),
                               "prefs.uilang");
        }
    }
    /* UI behavior. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs, computer_Icon " ${heading.prefs.interface}", red_ColorId, '2', &headings, &values),
                     "prefs.page.ui");
        addDialogToggle_Widget(headings, values, "${prefs.hoverlink}", "prefs.hoverlink");
        addDialogToggle_Widget(headings, values, "${prefs.bookmarks.addbottom}", "prefs.bookmarks.addbottom");
        /* Return key behaviors. */
        addDialogDropMenu_Widget(headings,
                           values,
                           "${prefs.returnkey}",
                           returnKeyBehaviorItems,
                           iInvalidSize,
                           "prefs.returnkey");
        if (!isTerminal_Platform()) {
            addDialogToggle_Widget(headings, values, "${prefs.imageloadscroll}", "prefs.imageloadscroll");
        }
        addDialogToggle_Widget(headings, values, "${prefs.time.24h}", "prefs.time.24h");
        addDialogPadding_Widget(headings, values);
        addDialogToggle_Widget(headings, values, "${prefs.animate}", "prefs.animate");
        if (!isTerminal_Platform()) {
            addDialogToggle_Widget(headings, values, "${prefs.blink}", "prefs.blink");
        }
        //        makeTwoColumnHeading_Widget("${heading.prefs.scrolling}", headings, values);
        addDialogToggle_Widget(headings, values, "${prefs.smoothscroll}", "prefs.smoothscroll");
        /* Scroll speeds. */ {
            for (int type = 0; type < max_ScrollType; type++) {
                const char *typeStr = (type == mouse_ScrollType ? "mouse" : "keyboard");
                addChild_Widget(headings,
                                iClob(makeHeading_Widget(type == mouse_ScrollType
                                                             ? "${prefs.scrollspeed.mouse}"
                                                             : "${prefs.scrollspeed.keyboard}")));
                /* TODO: Make a SliderWidget. */
                iWidget *scrollSpeed = new_Widget();
                addRadioButton_Widget(scrollSpeed, format_CStr("prefs.scrollspeed.%s.7",  typeStr), "0", format_CStr("scrollspeed arg:7  type:%d", type));
                addRadioButton_Widget(scrollSpeed, format_CStr("prefs.scrollspeed.%s.10", typeStr), "1", format_CStr("scrollspeed arg:10 type:%d", type));
                addRadioButton_Widget(scrollSpeed, format_CStr("prefs.scrollspeed.%s.13", typeStr), "2", format_CStr("scrollspeed arg:13 type:%d", type));
                addRadioButton_Widget(scrollSpeed, format_CStr("prefs.scrollspeed.%s.17", typeStr), "3", format_CStr("scrollspeed arg:17 type:%d", type));
                addRadioButton_Widget(scrollSpeed, format_CStr("prefs.scrollspeed.%s.23", typeStr), "4", format_CStr("scrollspeed arg:23 type:%d", type));
                addRadioButton_Widget(scrollSpeed, format_CStr("prefs.scrollspeed.%s.30", typeStr), "5", format_CStr("scrollspeed arg:30 type:%d", type));
                addRadioButton_Widget(scrollSpeed, format_CStr("prefs.scrollspeed.%s.40", typeStr), "6", format_CStr("scrollspeed arg:40 type:%d", type));
                addChildFlags_Widget(
                    values, iClob(scrollSpeed), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
            }
        }
    }
    /* Appearance. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs, eye_Icon " ${heading.prefs.appearance}", red_ColorId, '3', &headings, &values),
                     "prefs.page.appearance");
        if (isTerminal_Platform()) {
            /* Display character set. */
            addDialogToggle_Widget(headings, values, "${prefs.tui.simple}", "prefs.tui.simple");
        }
#if (defined (iPlatformApple) || defined (iPlatformMsys) || defined (iPlatformWindows)) && !defined (iPlatformTerminal)
        addDialogToggle_Widget(headings, values, "${prefs.ostheme}", "prefs.ostheme");
#endif
        addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.theme}")));
        iWidget *themes = new_Widget();
        /* Themes. */ {
            setId_Widget(addChild_Widget(themes, iClob(new_LabelWidget("${prefs.theme.black}", "theme.set arg:0"))), "prefs.theme.0");
            setId_Widget(addChild_Widget(themes, iClob(new_LabelWidget("${prefs.theme.dark}", "theme.set arg:1"))), "prefs.theme.1");
            setId_Widget(addChild_Widget(themes, iClob(new_LabelWidget("${prefs.theme.light}", "theme.set arg:2"))), "prefs.theme.2");
            setId_Widget(addChild_Widget(themes, iClob(new_LabelWidget("${prefs.theme.white}", "theme.set arg:3"))), "prefs.theme.3");
        }
        addChildFlags_Widget(values, iClob(themes), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        /* Accents. */ {
            iLabelWidget *accentMenu = addDialogDropMenu_Widget(headings,
                                                          values,
                                                          "${prefs.accent}",
                                                          accentItems,
                                                          iElemCount(accentItems),
                                                          "prefs.accent");
            int accentId = isAppleDesktop_Platform() ? -1 : 0;
            iForEach(ObjectList,
                     i,
                     children_Widget(findChild_Widget(constAs_Widget(accentMenu), "menu"))) {
                if (isInstance_Object(i.object, &Class_LabelWidget)) {
                    setIconColor_LabelWidget(i.object, color_ColorAccent(accentId < 0 ? system_ColorAccent : accentId, iTrue));
                    accentId++;
                }
            }
        }
        addDialogPadding_Widget(headings, values);
        addDialogToggleGroup_(
            headings,
            values,
            "${prefs.uilayout}",
            (const char *[]) {
#if defined (LAGRANGE_MAC_MENUBAR) || defined (iPlatformTerminal)
                "prefs.bottomnavbar", "prefs.bottomtabbar", NULL
#else
                "prefs.bottomnavbar", "prefs.bottomtabbar", "prefs.menubar", NULL
#endif
            },
            iInvalidSize);
        if (isDesktop_Platform()) {
            addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.promptposition}")));
            iWidget *promptPos = new_Widget();
            /* Input prompt position: inline under the link, or always modal top/bottom. */ {
                addRadioButton_Widget(promptPos, "prefs.promptposition.0", "${prefs.promptposition.inline}",
                                "promptposition.set arg:0");
                addRadioButton_Widget(promptPos, "prefs.promptposition.1", "${prefs.promptposition.top}",
                                "promptposition.set arg:1");
                addRadioButton_Widget(promptPos, "prefs.promptposition.2", "${prefs.promptposition.bottom}",
                                "promptposition.set arg:2");
            }
            addChildFlags_Widget(values, iClob(promptPos),
                                 arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        }
        addDialogToggle_Widget(headings, values, "${prefs.thickscroll}", "prefs.thickscroll");
        addDialogToggle_Widget(headings, values, "${prefs.hidetabs}", "prefs.hidetabs");
        addDialogToggle_Widget(headings, values, "${prefs.evensplit}", "prefs.evensplit");
        if (!isTerminal_Platform()) {
            addDialogPadding_Widget(headings, values);
            // makeTwoColumnHeading_Widget("${heading.prefs.sizing}", headings, values);
            addPrefsInputWithHeading_Widget(headings, values, "prefs.uiscale", iClob(new_InputWidget(5)));
            addDialogToggle_Widget(headings, values, "${prefs.retainwindow}", "prefs.retainwindow");
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
            addDialogToggle_Widget(headings, values, "${prefs.customframe}", "prefs.customframe");
#endif
            addDialogPadding_Widget(headings, values);
            addDialogToggle_Widget(headings, values, "${prefs.editor.highlight}", "prefs.editor.highlight");
            addDialogPadding_Widget(headings, values);
            addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.font.ui}")));
            addFontButtons_(values, "ui");
        }
    #if defined (LAGRANGE_ENABLE_CORETEXT) || defined (LAGRANGE_ENABLE_FREETYPE)
        addDialogToggle_Widget(headings, values, "${prefs.font.coloremoji}", "prefs.font.coloremoji");
    #endif
    #if !defined (LAGRANGE_ENABLE_CORETEXT)
        if (!isTerminal_Platform()) {
            addDialogToggle_Widget(headings, values, "${prefs.font.smooth}", "prefs.font.smooth");
        }
    #endif
    }
    /* Document preferences. */
    appendTabPages_ScopedPrefs(tabs, iTrue, orange_ColorId, '4');
    /* Content. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs, photo_Icon " ${heading.prefs.content}",
                                                   green_ColorId, '9', &headings, &values),
                     "prefs.page.content");
        /* Cache size. */ {
            iInputWidget *cache = new_InputWidget(4);
            setSelectAllOnFocus_InputWidget(cache, iTrue);
            addPrefsInputWithHeading_Widget(headings, values, "prefs.cachesize", iClob(cache));
            iWidget *unit =
                addChildFlags_Widget(as_Widget(cache),
                                     iClob(new_LabelWidget("${mb}", NULL)),
                                     frameless_WidgetFlag | moveToParentRightEdge_WidgetFlag |
                                         resizeToParentHeight_WidgetFlag);
            setContentPadding_InputWidget(cache, 0, width_Widget(unit) - 4 * gap_UI);
        }
        /* Memory size. */ {
            iInputWidget *mem = new_InputWidget(4);
            setSelectAllOnFocus_InputWidget(mem, iTrue);
            addPrefsInputWithHeading_Widget(headings, values, "prefs.memorysize", iClob(mem));
            iWidget *unit =
                addChildFlags_Widget(as_Widget(mem),
                                     iClob(new_LabelWidget("${mb}", NULL)),
                                     frameless_WidgetFlag | moveToParentRightEdge_WidgetFlag |
                                         resizeToParentHeight_WidgetFlag);
            setContentPadding_InputWidget(mem, 0, width_Widget(unit) - 4 * gap_UI);
        }
        addDialogPadding_Widget(headings, values);
        addDialogToggle_Widget(headings, values, "${prefs.dataurl.openimages}", "prefs.dataurl.openimages");
        addDialogToggle_Widget(headings, values, "${prefs.archive.openindex}", "prefs.archive.openindex");
        addDialogPadding_Widget(headings, values);
        addDialogToggle_Widget(headings, values, "${prefs.font.warnmissing}", "prefs.font.warnmissing");
    }
    /* Sidebars. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs,
                                                   leftHalf_Icon " ${heading.prefs.sidebars}",
                                                   cyan_ColorId,
                                                   0,
                                                   &headings,
                                                   &values),
                     "sidecfg");
        const char *modes[maxSidebarModes_Prefs] = {
            "bookmarks",
            "feeds",
            "subscriptions",
            "identities",
            "outline",
            "structure",
            "documents",
            "history",
        };
        makeTwoColumnHeading_Widget("${heading.prefs.sidebars.tabs}", headings, values);
        iForIndices(sm, modes) {
            addDialogToggleGroupWithLabels_(
                headings,
                values,
                format_CStr("${sidebar.%s}:", modes[sm]),
                (const char *[]) { format_CStr("prefs.sidebar.enabled.%u", sm),
                                   format_CStr("prefs.sidebar2.enabled.%u", sm) },
                (const char *[]) { "${prefs.sidebar.left}", "${prefs.sidebar.right}" },
                2);
        }
    }
    /* Snippets. */ {
        iSnippetWidget *sniped = new_SnippetWidget();
        appendFramelessTabPage_Widget(tabs,
                                      iClob(sniped),
                                      clipboard_Icon " ${heading.prefs.snip}",
                                      cyan_ColorId,
                                      '9',
                                      KMOD_PRIMARY);
    }
    /* Keybindings. */ {
        iBindingsWidget *bind = new_BindingsWidget();
        appendFramelessTabPage_Widget(tabs, iClob(bind), keyboard_Icon " ${heading.prefs.keys}",
                                      cyan_ColorId, '9', KMOD_PRIMARY);
    }
#if defined (LAGRANGE_USE_GAMEPAD)
    /* Gamepad. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs,
                                                   gamepad_Icon " ${heading.prefs.gamepad}",
                                                   cyan_ColorId,
                                                   0,
                                                   &headings,
                                                   &values),
                     "prefs.page.gamepad");
        addDialogToggle_Widget(headings, values, "${prefs.gamepad}", "prefs.gamepad");
        addDialogPadding_Widget(headings, values);
        const iArray *buttonInfo = gamepadButtonInfo_();
        iConstForEach(Array, i, buttonInfo) {
            iBeginCollect();
            const iGamepadButtonInfo *info = i.value;
            iLabelWidget             *drop =
                addDialogDropMenu_Widget(headings,
                                   values,
                                   format_CStr("%s%s",
                                               info->trigger ? "${prefs.gamepad.triggermod}" : "",
                                               buttonName_Gamepad(gamepad_App(), info->button)),
                                   constData_Array(gamepadButtonItems_(info->cmd)),
                                   iInvalidSize,
                                   info->cmd);
            updateDropdownSelection_LabelWidget(
                drop, format_CStr(" arg:%d", findAction_Gamepad(info->button, info->trigger)));
            iEndCollect();
        }
    }
#endif /* LAGRANGE_USE_GAMEPAD */
    /* Network. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs,
                                                   network_Icon " ${heading.prefs.network}",
                                                   blue_ColorId,
                                                   0,
                                                   &headings,
                                                   &values),
                     "prefs.page.network");
        addDialogToggle_Widget(headings, values, "${prefs.ipv6}", "prefs.ipv6");
        addDialogToggle_Widget(headings, values, "${prefs.redirect.allowscheme}", "prefs.redirect.allowscheme");
        addDialogToggle_Widget(headings, values, "${prefs.warn.security}", "prefs.warn.security");
        addDialogPadding_Widget(headings, values);
        addDialogToggle_Widget(headings, values, "${prefs.decodeurls}", "prefs.decodeurls");
        addPrefsInputWithHeading_Widget(headings, values, "prefs.urlsize", iClob(new_InputWidget(10)));
        makeTwoColumnHeading_Widget("${heading.prefs.certs}", headings, values);
        addPrefsInputWithHeading_Widget(headings, values, "prefs.ca.file", iClob(new_InputWidget(0)));
        addPrefsInputWithHeading_Widget(headings, values, "prefs.ca.path", iClob(new_InputWidget(0)));
    }
    /* Proxy configuration. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs,
                                                   networkProxy_Icon " ${heading.prefs.proxy}",
                                                   blue_ColorId,
                                                   0,
                                                   &headings,
                                                   &values),
                     "prefs.page.proxy");
        /* Gemini proxies. */
        makeTwoColumnHeading_Widget("${heading.prefs.proxies}", headings, values);
        addPrefsInputWithHeading_Widget(headings, values, "prefs.proxy.gemini", iClob(new_InputWidget(0)));
        addPrefsInputWithHeading_Widget(headings, values, "prefs.proxy.gopher", iClob(new_InputWidget(0)));
        addPrefsInputWithHeading_Widget(headings, values, "prefs.proxy.http", iClob(new_InputWidget(0)));
        /* SOCKS configuration. */
        makeTwoColumnHeading_Widget("${heading.prefs.socks}", headings, values);
        addDialogToggle_Widget(headings, values, "${prefs.socks}", "prefs.socks");
        addDialogPadding_Widget(headings, values);
        iInputWidget *field = new_InputWidget(0);
        setHint_InputWidget(field, "${hint.socks.server}");
        addPrefsInputWithHeading_Widget(headings, values, "prefs.socks.server", iClob(field));
        addDialogPadding_Widget(headings, values);
        addPrefsInputWithHeading_Widget(headings, values, "prefs.socks.user", iClob(field = new_InputWidget(0)));
        setHint_InputWidget(field, "${hint.optional}");
        addPrefsInputWithHeading_Widget(headings, values, "prefs.socks.password", iClob(field = new_InputWidget(0)));
        setSensitiveContent_InputWidget(field, iTrue);
        setHint_InputWidget(field, "${hint.optional}");
    }
    addChild_Widget(content, iClob(makePadding_Widget(gap_UI)));
    const iMenuItem actions[] = { { "${menu.fonts}", 0, 0, "!open url:about:fonts" },
                                { "---" },
                                { "${close}", SDLK_ESCAPE, 0, "prefs.dismiss" } };
    const size_t actOffset = (isTerminal_Platform() ? 2 : 0);
    iWidget *buttons = addChild_Widget(
        content, iClob(makeDialogButtons_Widget(actions + actOffset, iElemCount(actions) - actOffset)));
    setId_Widget(child_Widget(buttons, 0), "prefs.aboutfonts");
    updatePreferencesLayout_Widget(dlg);
    addChild_Widget(dlg->root->widget, iClob(dlg));
    setupSheetTransition_Mobile(dlg, incoming_TransitionFlag | top_TransitionDir);
    return dlg;
}

static void updatePrefsThemeButtons_(iWidget *d) {
    for (size_t i = 0; i < max_ColorTheme; i++) {
        setFlags_Widget(findChild_Widget(d, format_CStr("prefs.theme.%u", i)),
                        selected_WidgetFlag,
                        colorTheme_App() == i);
    }
    for (size_t i = 0; i < max_ColorAccent; i++) {
        setFlags_Widget(findChild_Widget(d, format_CStr("prefs.accent.%u", i)),
                        selected_WidgetFlag,
                        prefs_App()->accent == i);
    }
    updateDropdownSelection_LabelWidget(findChild_Widget(d, "prefs.accent"),
                                        format_CStr(" arg:%u", prefs_App()->accent));
}

static void updatePrefsPinSplitButtons_(iWidget *d, int value) {
    for (int i = 0; i < 3; i++) {
        setFlags_Widget(findChild_Widget(d, format_CStr("prefs.pinsplit.%d", i)),
                        selected_WidgetFlag,
                        i == value);
    }
}

static void updatePrefsPromptPositionButtons_(iWidget *d, int value) {
    for (int i = 0; i < 3; i++) {
        setFlags_Widget(findChild_Widget(d, format_CStr("prefs.promptposition.%d", i)),
                        selected_WidgetFlag,
                        i == value);
    }
}

static void updateFeedIntervalButton_(iLabelWidget *button, int feedInterval) {
    updateDropdownSelection_LabelWidget(button, format_CStr(".set arg:%d", feedInterval));
}

static void updatePrefsToolBarActionButton_(iWidget *prefs, int buttonIndex, int action) {
    updateDropdownSelection_LabelWidget(
        findChild_Widget(prefs, format_CStr("prefs.toolbaraction%d", buttonIndex + 1)),
        format_CStr(" arg:%d button:%d", action, buttonIndex));
}

static void updateScrollSpeedButtons_(iWidget *d, enum iScrollType type, const int value) {
    const char *typeStr = (type == mouse_ScrollType ? "mouse" : "keyboard");
    for (int i = 0; i <= 40; i++) {
        setFlags_Widget(findChild_Widget(d, format_CStr("prefs.scrollspeed.%s.%d", typeStr, i)),
                        selected_WidgetFlag,
                        i == value);
    }
}

static void updateFontButton_(iLabelWidget *button, const iString *fontId) {
    if (!button || isEmpty_String(fontId)) return;
    updateDropdownSelection_LabelWidget(button, format_CStr(":%s", cstr_String(fontId)));
}

static iBool handlePrefsCommands_(iWidget *d, const char *cmd) {
    /* Site Settings has precedence when open in the same root. */
    if (!findChild_Widget(d->root->widget, "sitespec.palette") &&
        handleCommand_ScopedPrefs(d, cmd)) {
        return iTrue;
    }
    if (equal_Command(cmd, "prefs.dismiss") || equal_Command(cmd, "preferences") ||
        equal_Command(cmd, "tabs.close")) {
        setupSheetTransition_Mobile(d, dialogTransitionDir_Widget(d));
        enableToolbar_Root(get_Root(), iTrue);
        /* Apply the new UI scaling factor to all non-popup windows. */ {
            const float uiScale =
                toFloat_String(text_InputWidget(findChild_Widget(d, "prefs.uiscale")));
            iConstForEach(PtrArray, i, regularWindows_App()) {
                setUiScale_Window(i.ptr, uiScale);
            }
        }
        /* Text fields are only read when the dialog is dismissed. */
        for (enum iPrefsString i = 0; i < max_PrefsString; i++) {
            const iPrefsStringSpec *spec  = stringSpec_Prefs(i);
            const iInputWidget     *input = spec->widgetId ?
                (const iInputWidget *) findChild_Widget(d, spec->widgetId) : NULL;
            if (input) {
                postCommandf_App("%s %s%s:%s",
                                 spec->cmd,
                                 spec->args ? format_CStr("%s ", spec->args) : "",
                                 spec->label,
                                 cstrText_InputWidget(input));
            }
        }
        postCommandf_App("font.user path:%s",
                         cstrText_InputWidget(findChild_Widget(d, "prefs.userfont")));
        postCommandf_App("cachesize.set arg:%d",
                         toInt_String(text_InputWidget(findChild_Widget(d, "prefs.cachesize"))));
        postCommandf_App("memorysize.set arg:%d",
                         toInt_String(text_InputWidget(findChild_Widget(d, "prefs.memorysize"))));
        postCommandf_App("urlsize.set arg:%d",
                         toInt_String(text_InputWidget(findChild_Widget(d, "prefs.urlsize"))));
        const iWidget *tabs = findChild_Widget(d, "prefs.tabs");
        if (tabs) {
            postCommandf_App("prefs.dialogtab arg:%u",
                             tabPageIndex_Widget(tabs, currentTabPage_Widget(tabs)));
        }
        deinit_ScopedPrefs(d);
        destroyDialog_Widget(d);
        postCommand_App("prefs.changed");
        return iTrue;
    }
    else if (equal_Command(cmd, "tabs.changed")) {
        if (isTerminal_Platform()) {
            iWidget *tabs = findChild_Widget(d, "prefs.tabs");
            setFocus_Widget((iWidget *) tabPageButton_Widget(tabs, currentTabPage_Widget(tabs)));
        }
        refresh_Widget(d);
        return iFalse;
    }
    else if (equal_Command(cmd, "uilang")) {
        updateDropdownSelection_LabelWidget(findChild_Widget(d, "prefs.uilang"),
                                            cstr_String(string_Command(cmd, "id")));
        return iFalse;
    }
    else if (equal_Command(cmd, "returnkey.set")) {
        updateDropdownSelection_LabelWidget(findChild_Widget(d, "prefs.returnkey"),
                                            format_CStr("returnkey.set arg:%d", arg_Command(cmd)));
        return iFalse;
    }
    else if (equal_Command(cmd, "gamepad.set")) {
#if defined (LAGRANGE_USE_GAMEPAD)
        const int trig   = argLabel_Command(cmd, "trig");
        const int button = argLabel_Command(cmd, "button");
        const int action = arg_Command(cmd);
        updateDropdownSelection_LabelWidget(
            findChild_Widget(d, format_CStr("gamepad.set trig:%d button:%d", trig, button)),
            format_CStr(" arg:%d", action));
        /* Each action can be assigned to a single button only, so another dropdown
           may need updating, too. */
        for (int b = SDL_GAMEPAD_BUTTON_SOUTH; b <= SDL_GAMEPAD_BUTTON_START; b++) {
            for (int t = 0; t <= 1; t++) {
                if (b != button || t != trig) {
                    if (findAction_Gamepad(b, t) == action) {
                        updateDropdownSelection_LabelWidget(
                            findChild_Widget(d, format_CStr("gamepad.set trig:%d button:%d", t, b)),
                            " arg:-1");
                    }
                }
            }
        }
        refresh_Widget(d);
#endif /* LAGRANGE_USE_GAMEPAD */
        return iFalse;
    }
    else if (equal_Command(cmd, "toolbar.action.set")) {
        updatePrefsToolBarActionButton_(d, argLabel_Command(cmd, "button"), arg_Command(cmd));
        return iFalse;
    }
    else if (equal_Command(cmd, "pinsplit.set")) {
        updatePrefsPinSplitButtons_(d, arg_Command(cmd));
        return iFalse;
    }
    else if (equal_Command(cmd, "promptposition.set")) {
        updatePrefsPromptPositionButtons_(d, arg_Command(cmd));
        return iFalse;
    }
    else if (equal_Command(cmd, "feedinterval.set")) {
        updateFeedIntervalButton_(findChild_Widget(d, "prefs.feedinterval"), arg_Command(cmd));
        return iFalse;
    }
    else if (equal_Command(cmd, "scrollspeed")) {
        updateScrollSpeedButtons_(d, argLabel_Command(cmd, "type"), arg_Command(cmd));
        return iFalse;
    }
    else if (equal_Command(cmd, "font.set")) {
        updateFontButton_(findChild_Widget(d, "prefs.font.ui"), string_Command(cmd, "ui"));
        return iFalse;
    }
    else if (equal_Command(cmd, "prefs.ostheme.changed")) {
        postCommandf_App("ostheme arg:%d", arg_Command(cmd));
    }
    else if (equal_Command(cmd, "theme.changed")) {
        updatePrefsThemeButtons_(d);
        if (!argLabel_Command(cmd, "auto")) {
            setToggle_Widget(findChild_Widget(d, "prefs.ostheme"), prefs_App()->useSystemTheme);
        }
    }
    else if (equalWidget_Command(cmd, d, "input.resized")) {
        if (d->root->pendingArrange < arg_Command(cmd)) {
            d->root->pendingArrange = arg_Command(cmd);
            postCommand_Root(d->root, "root.arrange");
        }
        return iTrue;
    }
    return iFalse;
}

void showPreferences_Widget(const char *cmd) {
    const iPrefs *prefs = prefs_App();
    /* Preferences may already be open. */ {
        iWindow *win = findWindow_App(extra_WindowType, "prefs");
        if (win) {
            SDL_ShowWindow(win->win);
            SDL_RaiseWindow(win->win);
            return;
        }
    }
    if (isMobile_Platform()) {
        enableToolbar_Root(get_Root(), iFalse); /* toolbars disabled while Settings is shown */
        if (findWidget_App("upload")) {
            postCommand_App("upload.cancel");
        }
        postCommand_App("valueinput.cancel"); /* in case an input dialog is currently open */
    }
    setFocus_Widget(NULL);
    iWidget *dlg = makePreferences_Widget();
    updatePrefsThemeButtons_(dlg);
    /* Update preferences values into the widgets. */ {
        for (int i = 0; i < max_PrefsBool; i++) {
            const char *id = boolSpec_Prefs(i)->id;
            if (id) {
                setToggle_Widget(findChild_Widget(dlg, id), prefs->bools[i]);
            }
        }
        for (int i = 0; i < max_PrefsString; i++) {
            const char *id = stringSpec_Prefs(i)->widgetId;
            if (id) {
                setText_InputWidget(findChild_Widget(dlg, id), &prefs->strings[i]);
            }
        }
    }
    updatePrefsPinSplitButtons_(dlg, prefs->pinSplit);
    updatePrefsPromptPositionButtons_(dlg, prefs->promptPosition);
    updateScrollSpeedButtons_(dlg, mouse_ScrollType, prefs->smoothScrollSpeed[mouse_ScrollType]);
    updateScrollSpeedButtons_(dlg, keyboard_ScrollType, prefs->smoothScrollSpeed[keyboard_ScrollType]);
    updateFeedIntervalButton_(findChild_Widget(dlg, "prefs.feedinterval"), prefs->feedInterval);
    updateDropdownSelection_LabelWidget(findChild_Widget(dlg, "prefs.uilang"), cstr_String(&prefs->strings[uiLanguage_PrefsString]));
    updateDropdownSelection_LabelWidget(
        findChild_Widget(dlg, "prefs.returnkey"),
        format_CStr("returnkey.set arg:%d", prefs->returnKey));
    updatePrefsToolBarActionButton_(dlg, 0, prefs->toolbarActions[0]);
    updatePrefsToolBarActionButton_(dlg, 1, prefs->toolbarActions[1]);
    for (int side = 0; side < 2; side++) {
        for (int barMode = 0; barMode < maxSidebarModes_Prefs; barMode++) {
            setToggle_Widget(findChild_Widget(dlg,
                                              format_CStr("prefs.%s.enabled.%d",
                                                          side == 0 ? "sidebar" : "sidebar2",
                                                          barMode)),
                             prefs->sidebarModeEnabled[side][barMode]);
        }
    }
    setText_InputWidget(findChild_Widget(dlg, "prefs.uiscale"),
                        collectNewFormat_String("%g", uiScale_Window(activeWindow_App())));
    updateFontButton_(findChild_Widget(dlg, "prefs.font.ui"), &prefs->strings[uiFont_PrefsString]);
    init_ScopedPrefs(dlg, "base"); /* the selection is not saved */
    setText_InputWidget(findChild_Widget(dlg, "prefs.cachesize"),
                        collectNewFormat_String("%d", prefs->maxCacheSize));
    setText_InputWidget(findChild_Widget(dlg, "prefs.memorysize"),
                        collectNewFormat_String("%d", prefs->maxMemorySize));
    setText_InputWidget(findChild_Widget(dlg, "prefs.urlsize"),
                        collectNewFormat_String("%d", prefs->maxUrlSize));
    iWidget *tabs = findChild_Widget(dlg, "prefs.tabs");
    if (tabs) {
        showTabPage_Widget(tabs, tabPage_Widget(tabs, prefs->dialogTab));
    }
    setCommandHandler_Widget(dlg, handlePrefsCommands_);
    if (prefs_App()->detachedPrefs && (!isWindows_Platform() || !prefs_App()->customFrame) &&
        deviceType_App() == desktop_AppDeviceType && !isTerminal_Platform()) {
        /* Detach into a window. The heading must be the first child to become the title. */
        iWidget *title = findChild_Widget(dlg, "prefs.title");
        addChildPos_Widget(dlg, iClob(removeChild_Widget(title->parent, title)), front_WidgetAddPos);
        setPadding1_Widget(dlg, 0); /* no sheet frame in a window */
        updatePreferencesLayout_Widget(dlg);
        promoteDialogToWindow_Widget(dlg);
    }
    if (argLabel_Command(cmd, "idents") && deviceType_App() != desktop_AppDeviceType) {
        iWidget *idPanel = panel_Mobile(dlg,
                                        isConnected_Gamepad(gamepad_App())
                                            ? 4
                                            : 3); /* TODO: Don't hardcode the panel index. */
        iWidget *button  = findUserData_Widget(findChild_Widget(dlg, "panel.top"), idPanel);
        postCommand_Widget(button, "panel.open");
    }
    if (argLabel_Command(cmd, "sniped")) {
        if (deviceType_App() == desktop_AppDeviceType) {
            postCommand_Widget(dlg, "tabs.switch id:sniped");
        }
        else {
            /* TODO: Don't hardcode the panel index. */
            iWidget *snippetPanel =
                panel_Mobile(dlg, isConnected_Gamepad(gamepad_App()) ? 12 : 11);
            iWidget *button  = findUserData_Widget(findChild_Widget(dlg, "panel.top"), snippetPanel);
            postCommand_Widget(button, "panel.open");
        }
    }
    if (argLabel_Command(cmd, "sidecfg")) {
        if (deviceType_App() == desktop_AppDeviceType) {
            postCommand_Widget(dlg, "tabs.switch id:sidecfg");
        }
        else {
            /* TODO: Don't hardcode the panel index. */
            iWidget *snippetPanel = panel_Mobile(dlg, 1);
            iWidget *button  = findUserData_Widget(findChild_Widget(dlg, "panel.top"), snippetPanel);
            postCommand_Widget(button, "panel.open");
        }
    }
    if (isPointerHidden_Gamepad(gamepad_App())) {
        setFocus_Widget(findFocusable_Widget(dlg, forward_WidgetFocusDir));
    }
}
