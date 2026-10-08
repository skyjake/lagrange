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
#include <lagrange/gmutil.h>
#include <lagrange/snippets.h>
#include "snippetwidget.h"
#include "sync.h"
#include "touch.h"
#include "scopedprefs.h"
#include "sliderwidget.h"
#include "util.h"
#include "widget.h"
#include "window.h"

#if defined (iPlatformAppleDesktop)
#   include "platform/macos.h"
#endif

#include <the_Foundation/math.h>
#include <the_Foundation/path.h>

static const char *urlSizeSliderSpec_ =
    "slider id:prefs.urlsize min:1 max:32 step:0.5 bigstep:4 prec:1 unit:kb";

static const char *uiScaleSliderSpec_(void) {
    const iRangef range = uiScaleRange_Window();
    return format_CStr("slider id:prefs.uiscale min:%g max:%g step:0.01 bigstep:0.1 prec:2 unit:x",
                       range.start, range.end);
}

/* Global settings may be shown at the end of a scoped settings panel. */
static void appendToPanel_(iArray *mainItems, const char *panelId, const iMenuItem *items) {
    iForEach(Array, i, mainItems) {
        iMenuItem *panel = i.value;
        if (equal_Command(panel->label, "panel") &&
            !iCmpStr(cstr_Command(panel->label, "id"), panelId)) {
            iArray *joined = collectNew_Array(sizeof(iMenuItem));
            pushBackN_Array(joined, panel->data, count_MenuItem(panel->data));
            pushBackN_Array(joined, items, count_MenuItem(items) + 1 /* NULL */);
            panel->data = constData_Array(joined);
            return;
        }
    }
}

static void expandToPageEdge_(iAnyObject *widget) {
    if (!widget) return;
    iWidget *w = as_Widget(widget);
    for (const iWidget *pages = w->parent; pages; pages = pages->parent) {
        if (!cmp_String(id_Widget(pages), "tabs.pages")) {
            w->rect.size.x = right_Rect(bounds_Widget(pages)) - left_Rect(bounds_Widget(w));
            break;
        }
    }
}

static void findWrappedLabels_(iWidget *d, iPtrArray *labels) {
    if (!d->children) return;
    iForEach(ObjectList, i, d->children) {
        iWidget *child = i.object;
        if (flags_Widget(child) & keepOnTop_WidgetFlag) {
            continue; /* menus are not part of the page */
        }
        if (isInstance_Object(child, &Class_LabelWidget) &&
            isWrapped_LabelWidget((iLabelWidget *) child)) {
            pushBack_PtrArray(labels, child);
        }
        findWrappedLabels_(child, labels);
    }
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

static float prefsWindowHeight_; /* window coordinates; remembered for the session only */

static void fitPreferencesWindow_(iWidget *prefs, iWidget *tabs) {
    /* The tabs are the only child of a promoted dialog. */
    const iWindow *win = window_Widget(prefs);
    const iInt2 natural = init_I2(width_Widget(tabs) + prefs->padding[0] + prefs->padding[2],
                                  height_Widget(tabs) + prefs->padding[1] + prefs->padding[3]);
    SDL_SetWindowMinimumSize(
        win->win, natural.x / win->pixelRatio, natural.y / win->pixelRatio);
#if !defined (iPlatformTerminal)
    SDL_SetWindowMaximumSize(win->win, natural.x / win->pixelRatio, 0);
#endif
    const int extra = (int) (prefsWindowHeight_ * win->pixelRatio) - natural.y;
    if (extra > 0) {
        tabs->rect.size.y += extra;
    }
}

void setPreferencesWindowHeight_Widget(iWidget *prefs, int height) {
    if (!prefs || !isPromoted_Widget(prefs)) {
        return;
    }
    prefsWindowHeight_ = height / window_Widget(prefs)->pixelRatio;
    updatePreferencesLayout_Widget(prefs);
}

static void arrangeTabs_(iWidget *prefs, iWidget *tabs) {
    resizeToLargestPage_Widget(tabs);
    if (isPromoted_Widget(prefs)) {
        fitPreferencesWindow_(prefs, tabs);
        arrange_Widget(root_Widget(prefs)); /* frame root must match the window size */
    }
    else {
        arrange_Widget(prefs);
    }
}

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
        "prefs.sync.server",
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
    /* Wrapped labels extend to the page edge, so they must not affect the page width. */
    iPtrArray *wrapped = collectNew_PtrArray();
    findWrappedLabels_(tabs, wrapped);
    iForEach(PtrArray, w, wrapped) {
        setFlags_Widget(w.ptr, fixedHeight_WidgetFlag, iFalse);
        as_Widget(w.ptr)->rect.size.x = 0;
        updateSize_LabelWidget(w.ptr);
    }
    iWidget *bindings = findChild_Widget(prefs, "bindings");
    if (bindings) {
        bindings->rect.size.x = 0;
    }
    arrangeTabs_(prefs, tabs);
    if (!isEmpty_PtrArray(wrapped)) {
        iForEach(PtrArray, label, wrapped) {
            expandToPageEdge_(label.ptr);
            setFlags_Widget(label.ptr, fixedHeight_WidgetFlag, iTrue);
            updateWrappedHeight_LabelWidget(label.ptr);
        }
        arrangeTabs_(prefs, tabs); /* the largest page may have changed */
    }
    iForIndices(i, inputIds) {
        expandToPageEdge_(findChild_Widget(tabs, inputIds[i]));
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

static iBool isSyncIdentity_(void *context, const iGmIdentity *ident) {
    iUnused(context);
    return ~ident->flags & temporary_GmIdentityFlag && hasPrivateKey_TlsCertificate(ident->cert) &&
           keyType_TlsCertificate(ident->cert) == ed25519_TlsCertificateKeyType;
}

static const iArray *syncIdentityItems_(iWidget *menu) {
    iUnused(menu);
    iArray *items = collectNew_Array(sizeof(iMenuItem));
    pushBack_Array(items, &(iMenuItem){ "${prefs.sync.identity.none}", 0, 0, "sync.identity fp:" });
    appendIdentities_MenuItem(items, "sync.identity", isSyncIdentity_);
    return items;
}

static void updateSyncIdentityButton_(iWidget *d, const char *fingerprint) {
    iLabelWidget *button = findChild_Widget(d, "prefs.sync.identity");
    if (!button) {
        return;
    }
    const iGmIdentity *ident =
        *fingerprint ? findIdentity_GmCerts(certs_App(),
                                            collect_Block(hexDecode_Rangecc(range_CStr(fingerprint))))
                     : NULL;
    updateTextAndResizeWidthCStr_LabelWidget(
        button, ident ? cstr_String(name_GmIdentity(ident)) : "${prefs.sync.identity.none}");
}

static void updateSyncStatus_(iWidget *d) {
    iLabelWidget *status = findChild_Widget(d, "prefs.sync.status");
    if (status) {
        updateText_LabelWidget(status, collect_String(statusText_Sync()));
        if (deviceType_App() == desktop_AppDeviceType) {
            updatePreferencesLayout_Widget(d);
        }
        else {
            arrange_Widget(parent_Widget(status));
        }
    }
}

static iBool applySyncServer_(iWidget *d) {
    /* Text fields are normally applied when the dialog is dismissed. */
    const iInputWidget *input = findChild_Widget(d, "prefs.sync.server");
    if (!input || equal_String(text_InputWidget(input),
                               &prefs_App()->strings[syncServer_PrefsString])) {
        return iFalse;
    }
    const iPrefsStringSpec *spec = stringSpec_Prefs(syncServer_PrefsString);
    postCommandf_App("%s %s:%s", spec->cmd, spec->label, cstrText_InputWidget(input));
    return iTrue;
}

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
    const iMenuItem syncIntervalItems[] = {
        { "${prefs.sync.interval.manual}", 0, 0, "syncinterval.set arg:0" },
        { formatCStrs_Lang("num.hours.n", 1), 0, 0, "syncinterval.set arg:1" },
        { formatCStrs_Lang("num.hours.n", 6), 0, 0, "syncinterval.set arg:6" },
        { "${reload.onceperday}", 0, 0, "syncinterval.set arg:24" },
        { NULL }
    };
    const iMenuItem syncIdentityItems[] = {
        { "${prefs.sync.identity.none}", 0, 0, "sync.identity fp:" },
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
        const iMenuItem gempubSidebarItems[] = {
            { "button id:prefs.gempubsidebar.0 label:prefs.sidebar.left",  0, 0, "gempubsidebar.set arg:0" },
            { "button id:prefs.gempubsidebar.1 label:prefs.sidebar.right", 0, 0, "gempubsidebar.set arg:1" },
            { NULL }
        };
        const iMenuItem pageTurnItems[] = {
            { "button id:prefs.pageturn.0 label:prefs.pageturn.none",   0, 0, "pageturn.set arg:0" },
            { "button id:prefs.pageturn.1 label:prefs.pageturn.scroll", 0, 0, "pageturn.set arg:1" },
            { "button id:prefs.pageturn.2 label:prefs.pageturn.curl",   0, 0, "pageturn.set arg:2" },
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
            { "radio device:1 id:prefs.pinsplit", 0, 0, (const void *) pinSplitItems },
            { "padding" },
            { "dropdown id:prefs.feedinterval", 0, 0, (const void *) feedIntervalItems },
            { "padding" },
            { "dropdown id:prefs.uilang", 0, 0, (const void *) langItems },
            { "toggle id:prefs.time.24h" },
            { "radio id:prefs.pageturn", 0, 0, (const void *) pageTurnItems },
#if defined (LAGRANGE_RELEASE_NOTES_POPUP)
            { "padding" },
            { "toggle id:prefs.releasenotes" },
#endif
            { "padding" },
            { NULL }
        };
        const iMenuItem uiPanelItems[] = {
            { "title id:heading.prefs.ui" },
            { "padding arg:0.667" },
            { "toggle id:prefs.ostheme" },
            { "radio id:prefs.theme", 0, 0, (const void *) themeItems },
            { "radio horizontal:1 rowlen:3 id:prefs.accent", 0, 0, (const void *) accentItems },
            { "padding" },
            { "dropdown device:0 id:prefs.returnkey", 0, 0, (const void *) returnKeyBehaviorItems },
            { "toggle id:prefs.bottomnavbar text:${LC:prefs.bottomnavbar}" },
            { "toggle id:prefs.bottomtabbar text:${LC:prefs.bottomtabbar}" },
            { "toggle id:prefs.hidetabs" },
            { "padding" },
            { "toggle id:prefs.swipe.edge" },
            { "toggle id:prefs.swipe.page" },
            { "heading id:heading.prefs.sizing" },
            { uiScaleSliderSpec_() },
            { "padding" },
            { NULL }
        };
        const iMenuItem toolbarPanelItems[] = {
            { "title id:heading.prefs.toolbars" },
            { "padding arg:0.667" },
            { "toggle device:2 id:prefs.hidetoolbarscroll" },
            { "toggle id:prefs.bookmarks.addbottom" },
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
            { "radio device:1 id:prefs.gempubsidebar", 0, 0, (const void *) gempubSidebarItems },
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
            { urlSizeSliderSpec_ },
            { "padding" },
            { "toggle id:prefs.ipv6" },
            { "toggle id:prefs.redirect.allowscheme" },
            { "toggle id:prefs.warn.security" },
            { "padding" },
            { NULL }
        };
        const iMenuItem syncPanelItems[] = {
            { "title id:heading.prefs.sync" },
            { "padding arg:0.667" },
            { "toggle id:prefs.sync" },
            { "label text:${prefs.sync.info}" },
            { "padding" },
            { "input id:prefs.sync.server url:1 hint:hint.sync.server" },
            { "dropdown id:prefs.sync.identity", 0, 0, (const void *) syncIdentityItems },
            { "button text:" add_Icon " ${prefs.sync.newidentity}", 0, 0, "sync.newidentity" },
            { "dropdown id:prefs.sync.interval", 0, 0, (const void *) syncIntervalItems },
            { "padding" },
            { "label id:prefs.sync.status text:" },
            { "button text:" reload_Icon " " uiTextAction_ColorEscape "${prefs.sync.now}", 0, 0, "sync.now" },
            { NULL }
        };
        const iMenuItem identityPanelItems[] = {
            { "title id:sidebar.identities" },
            { "padding arg:0.667" },
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
            { "padding arg:0.667" },
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
            { "button text:" book_Icon " ${menu.bookmarks.list}", 0, 0, "docdlg.open url:about:bookmarks" },
            { "button text:" tag_Icon " ${menu.bookmarks.bytag}", 0, 0, "docdlg.open url:about:bookmarks?tags" },
            { "button text:" clock_Icon " ${macos.menu.bookmarks.bytime}", 0, 0, "docdlg.open url:about:bookmarks?created" },
            { "padding" },
            { "button text:" star_Icon " ${menu.feeds.entrylist}", 0, 0, "docdlg.open url:about:feeds" },
            { "padding" },
            { "button text:" download_Icon " ${menu.downloads}", 0, 0,
              format_CStr("docdlg.open url:%s",
                          cstrCollect_String(makeFileUrl_String(downloadDir_App()))) },
            { "padding" },
            { "button text:" fontpack_Icon " " uiTextAction_ColorEscape "${menu.fonts}", 0, 0, "!open url:about:fonts" },
            { NULL }
        };
        const iMenuItem supportPanelItems[] = {
            { "title id:heading.prefs.support" },
            { format_CStr("heading text:%s", cstr_String(aboutText)) },
            { "button text:" star_Icon " ${menu.releasenotes}", 0, 0, "docdlg.open url:about:version" },
            { "button text:" info_Icon " ${menu.help}", 0, 0, "docdlg.open url:about:help" },
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
            { "button text:" info_Icon " ${menu.aboutpages}", 0, 0, "docdlg.open url:about:about" },
            { "button text:" bug_Icon " ${menu.debug}", 0, 0, "docdlg.open url:about:debug" },
            { NULL }
        };
        iArray *contentItems = collectNew_Array(sizeof(iMenuItem)); {
            const iMenuItem *globalItems = globalContentItems_ScopedPrefs();
            pushBack_Array(contentItems, &(iMenuItem){ "title id:heading.prefs.content" });
            pushBackN_Array(contentItems, globalItems, count_MenuItem(globalItems));
            pushBackN_Array(contentItems, (iMenuItem[]){
                { "padding" },
                { "toggle id:prefs.dataurl.openimages" },
                { "toggle id:prefs.archive.openindex" },
                { "padding" },
                { NULL }
            }, 5);
        }
        iArray *mainItems = collectNew_Array(sizeof(iMenuItem));
        pushBackN_Array(mainItems, (iMenuItem[]){
            { "padding arg:0.333" },
            { "title id:heading.settings" },
            { "padding arg:0.167" },
            /* Icon colors run through the rainbow from top to bottom. */
            { format_CStr("panel iconcolor:%d text:" gear_Icon " ${heading.prefs.general}",
                          red_ColorId), 0, 0, (const void *) generalPanelItems },
            { format_CStr("panel icon:0x1f4f1 iconcolor:%d id:heading.prefs.ui", red_ColorId),
              0, 0, (const void *) uiPanelItems },
            { format_CStr("panel icon:0x2b13 iconcolor:%d id:heading.prefs.toolbars", red_ColorId),
              0, 0, (const void *) toolbarPanelItems },
            { format_CStr("panel icon:0x1f5a7 iconcolor:%d id:heading.prefs.network", orange_ColorId),
              0, 0, (const void *) networkPanelItems },
            { format_CStr("panel noscroll:1 iconcolor:%d id:sidebar.identities text:" person_Icon
                          " ${sidebar.identities}",
                          orange_ColorId), 0, 0, (const void *) identityPanelItems },
            { format_CStr("panel icon:0x1f503 iconcolor:%d id:heading.prefs.sync", orange_ColorId),
              0, 0, (const void *) syncPanelItems },
            { "padding" },
        }, 10);
        appendPanels_ScopedPrefs(mainItems, iTrue, green_ColorId);
        appendToPanel_(mainItems, "heading.prefs.typography", (iMenuItem[]){
            { "toggle id:prefs.font.warnmissing" },
            { "padding" },
            { NULL }
        });
        pushBackN_Array(mainItems, (iMenuItem[]){
            { format_CStr("panel icon:0x1f5bc iconcolor:%d id:heading.prefs.content", green_ColorId),
              0, 0, constData_Array(contentItems) },
            { "padding" },
            { format_CStr("panel icon:0x1f4e6 iconcolor:%d id:heading.prefs.user", cyan_ColorId),
              0, 0, (const void *) userPanelItems },
            { format_CStr("panel icon:0x1f4cb iconcolor:%d id:heading.prefs.snip", cyan_ColorId),
              0, 0, (const void *) snippetPanelItems },
            { "padding" },
            { format_CStr("panel iconcolor:%d text:" info_Icon " ${heading.prefs.support}",
                          blue_ColorId), 0, 0, (const void *) supportPanelItems },
            { NULL }
        }, 7);
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
                             6 /* after Toolbars */,
                             &(iMenuItem) {format_CStr("panel icon:0x1f3ae iconcolor:%d "
                                                       "id:heading.prefs.gamepad",
                                                       orange_ColorId),
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
        addDialogToggle_Widget(headings, values, "${prefs.hidetabs}", "prefs.hidetabs");
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
        addDialogPadding_Widget(headings, values);
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
        addDialogToggle_Widget(headings, values, "${prefs.evensplit}", "prefs.evensplit");
        addDialogPadding_Widget(headings, values);
        addDialogToggle_Widget(headings, values, "${prefs.retaintabs}", "prefs.retaintabs");
        if (!isTerminal_Platform()) {
            addDialogToggle_Widget(headings, values, "${prefs.retainwindow}", "prefs.retainwindow");
        }
#if defined (LAGRANGE_RELEASE_NOTES_POPUP)
        addDialogToggle_Widget(headings, values, "${prefs.releasenotes}", "prefs.releasenotes");
#endif
        addDialogPadding_Widget(headings, values);
        /* UI languages. */ {
            iArray *uiLangs = collectNew_Array(sizeof(iMenuItem));
            pushBackN_Array(uiLangs, langItems, iElemCount(langItems) - 1);
            addDialogDropMenu_Widget(headings, values, "${prefs.uilang}",
                               constData_Array(uiLangs), size_Array(uiLangs),
                               "prefs.uilang");
        }
    }
    /* Appearance. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs, eye_Icon " ${heading.prefs.appearance}", red_ColorId, '2', &headings, &values),
                     "prefs.page.appearance");
        if (isTerminal_Platform()) {
            /* Display character set. */
            addDialogToggle_Widget(headings, values, "${prefs.tui.simple}", "prefs.tui.simple");
        }
#if !defined (iPlatformTerminal)
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
        if (!isTerminal_Platform()) {
            addDialogPadding_Widget(headings, values);
            makeTwoColumnItems_Widget(headings, values,
                                      (iMenuItem[]){ { uiScaleSliderSpec_() }, { NULL } });
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
            addDialogToggle_Widget(headings, values, "${prefs.customframe}", "prefs.customframe");
#endif
            addDialogPadding_Widget(headings, values);
            addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.font.ui}")));
            addFontButtons_(values, "ui");
        }
    #if defined (LAGRANGE_ENABLE_CORETEXT) || defined (LAGRANGE_ENABLE_FREETYPE)
        addDialogToggle_Widget(headings, values, "${prefs.font.coloremoji}", "prefs.font.coloremoji");
    #endif
        if (!isTerminal_Platform()) {
            addDialogToggle_Widget(headings, values, "${prefs.editor.highlight}", "prefs.editor.highlight");
    #if !defined (LAGRANGE_ENABLE_CORETEXT)
            addDialogToggle_Widget(headings, values, "${prefs.font.smooth}", "prefs.font.smooth");
    #endif
        }
        addDialogPadding_Widget(headings, values);
        addDialogToggle_Widget(headings, values, "${prefs.thickscroll}", "prefs.thickscroll");
    }
    /* Document preferences. */
    appendTabPages_ScopedPrefs(tabs, iTrue, orange_ColorId, '3');
    /* Content. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs, photo_Icon " ${heading.prefs.content}",
                                                   green_ColorId, '7', &headings, &values),
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
        makeTwoColumnItems_Widget(headings, values, globalContentItems_ScopedPrefs());
        addDialogPadding_Widget(headings, values);
        addDialogToggle_Widget(headings, values, "${prefs.dataurl.openimages}", "prefs.dataurl.openimages");
        addDialogToggle_Widget(headings, values, "${prefs.archive.openindex}", "prefs.archive.openindex");
        addDialogPadding_Widget(headings, values);
        addDialogToggle_Widget(headings, values, "${prefs.font.warnmissing}", "prefs.font.warnmissing");
    }
    /* UI behavior. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs, computer_Icon " ${heading.prefs.interface}", green_ColorId, '8', &headings, &values),
                     "prefs.page.ui");
        /* Feed refresh interval. */
        addDialogDropMenu_Widget(headings,
                           values,
                           "${prefs.feedinterval}",
                           feedIntervalItems,
                           iInvalidSize,
                           "prefs.feedinterval");
        addDialogPadding_Widget(headings, values);
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
            addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.pageturn}")));
            iWidget *turns = new_Widget();
            addRadioButton_Widget(turns, "prefs.pageturn.0", "${prefs.pageturn.none}", "pageturn.set arg:0");
            addRadioButton_Widget(turns, "prefs.pageturn.1", "${prefs.pageturn.scroll}", "pageturn.set arg:1");
            addRadioButton_Widget(turns, "prefs.pageturn.2", "${prefs.pageturn.curl}", "pageturn.set arg:2");
            addChildFlags_Widget(values, iClob(turns), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        }
        addDialogToggle_Widget(headings, values, "${prefs.smoothscroll}", "prefs.smoothscroll");
        makeTwoColumnItems_Widget(
            headings,
            values,
            (iMenuItem[]){
                { "slider id:prefs.scrollspeed.keyboard cmd:scrollspeed.keyboard min:0 max:6" },
                { "slider id:prefs.scrollspeed.mouse cmd:scrollspeed.mouse min:0 max:6" },
                { NULL } });
        if (!isTerminal_Platform()) {
            addDialogToggle_Widget(headings, values, "${prefs.blink}", "prefs.blink");
        }
    }
    /* Network. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs,
                                                   network_Icon " ${heading.prefs.network}",
                                                   cyan_ColorId,
                                                   '9',
                                                   &headings,
                                                   &values),
                     "prefs.page.network");
        addDialogToggle_Widget(headings, values, "${prefs.ipv6}", "prefs.ipv6");
        addDialogToggle_Widget(headings, values, "${prefs.redirect.allowscheme}", "prefs.redirect.allowscheme");
        addDialogToggle_Widget(headings, values, "${prefs.warn.security}", "prefs.warn.security");
        addDialogPadding_Widget(headings, values);
        addDialogToggle_Widget(headings, values, "${prefs.decodeurls}", "prefs.decodeurls");
        makeTwoColumnItems_Widget(headings, values, (iMenuItem[]){ { urlSizeSliderSpec_ }, { NULL } });
        makeTwoColumnHeading_Widget("${heading.prefs.certs}", headings, values);
        addPrefsInputWithHeading_Widget(headings, values, "prefs.ca.file", iClob(new_InputWidget(0)));
        addPrefsInputWithHeading_Widget(headings, values, "prefs.ca.path", iClob(new_InputWidget(0)));
    }
    /* Proxy configuration. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs,
                                                   networkProxy_Icon " ${heading.prefs.proxy}",
                                                   cyan_ColorId,
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
    /* Sync. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs,
                                                   reload_Icon " ${heading.prefs.sync}",
                                                   cyan_ColorId,
                                                   0,
                                                   &headings,
                                                   &values),
                     "prefs.page.sync");
        addDialogToggle_Widget(headings, values, "${prefs.sync}", "prefs.sync");
        /* Sync info. */ {
            iWidget      *head = addChild_Widget(headings, iClob(makeHeading_Widget("")));
            iLabelWidget *info = new_LabelWidget(uiTextDim_ColorEscape "${prefs.sync.info}", NULL);
            setId_Widget(as_Widget(info), "prefs.sync.info");
            setWrap_LabelWidget(info, iTrue);
            addChildFlags_Widget(values, iClob(info), frameless_WidgetFlag);
            head->sizeRef = as_Widget(info);
        }
        addDialogPadding_Widget(headings, values);
        iInputWidget *server = new_InputWidget(0);
        setHint_InputWidget(server, "${hint.sync.server}");
        setUrlContent_InputWidget(server, iTrue);
        addPrefsInputWithHeading_Widget(headings, values, "prefs.sync.server", iClob(server));
        addDialogDropMenu_Widget(headings,
                                 values,
                                 "${prefs.sync.identity}",
                                 syncIdentityItems,
                                 iElemCount(syncIdentityItems) - 1,
                                 "prefs.sync.identity");
        addChild_Widget(headings, iClob(makeHeading_Widget("")));
        addChildFlags_Widget(
            values,
            iClob(new_LabelWidget(add_Icon " ${prefs.sync.newidentity}", "sync.newidentity")),
            0);
        addDialogDropMenu_Widget(headings,
                                 values,
                                 "${prefs.sync.interval}",
                                 syncIntervalItems,
                                 iElemCount(syncIntervalItems) - 1,
                                 "prefs.sync.interval");
        addDialogPadding_Widget(headings, values);
        iWidget      *statusHead =
            addChild_Widget(headings, iClob(makeHeading_Widget("${prefs.sync.status}")));
        iLabelWidget *status = new_LabelWidget("", NULL);
        setId_Widget(as_Widget(status), "prefs.sync.status");
        setWrap_LabelWidget(status, iTrue);
        addChildFlags_Widget(values, iClob(status), frameless_WidgetFlag);
        statusHead->sizeRef = as_Widget(status);
        addChild_Widget(headings, iClob(makeHeading_Widget("")));
        addChildFlags_Widget(
            values,
            iClob(new_LabelWidget(reload_Icon " " uiTextAction_ColorEscape "${prefs.sync.now}",
                                  "sync.now")),
            0);
    }
    /* Snippets. */ {
        iSnippetWidget *sniped = new_SnippetWidget();
        appendFramelessTabPage_Widget(tabs,
                                      iClob(sniped),
                                      clipboard_Icon " ${heading.prefs.snip}",
                                      blue_ColorId,
                                      0,
                                      0);
    }
    /* Sidebars. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs,
                                                   leftHalf_Icon " ${heading.prefs.sidebars}",
                                                   blue_ColorId,
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
        /* The Book mode is on one side only. */ {
            addChild_Widget(headings, iClob(makeHeading_Widget("${sidebar.gempub}:")));
            iWidget *sides = new_Widget();
            addRadioButton_Widget(sides, "prefs.gempubsidebar.0", "${prefs.sidebar.left}", "gempubsidebar.set arg:0");
            addRadioButton_Widget(sides, "prefs.gempubsidebar.1", "${prefs.sidebar.right}", "gempubsidebar.set arg:1");
            addChildFlags_Widget(values, iClob(sides), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        }
    }
    /* Keybindings. */ {
        iBindingsWidget *bind = new_BindingsWidget();
        appendFramelessTabPage_Widget(tabs, iClob(bind), keyboard_Icon " ${heading.prefs.keys}",
                                      blue_ColorId, 0, 0);
    }
#if defined (LAGRANGE_USE_GAMEPAD)
    /* Gamepad. */ {
        setId_Widget(appendTwoColumnTabPage_Widget(tabs,
                                                   gamepad_Icon " ${heading.prefs.gamepad}",
                                                   blue_ColorId,
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

static void updatePrefsGempubSidebarButtons_(iWidget *d, int value) {
    for (int i = 0; i < 2; i++) {
        setFlags_Widget(findChild_Widget(d, format_CStr("prefs.gempubsidebar.%d", i)),
                        selected_WidgetFlag,
                        i == value);
    }
}

static void updatePrefsPinSplitButtons_(iWidget *d, int value) {
    for (int i = 0; i < 3; i++) {
        setFlags_Widget(findChild_Widget(d, format_CStr("prefs.pinsplit.%d", i)),
                        selected_WidgetFlag,
                        i == value);
    }
}

static void updatePrefsPageTurnButtons_(iWidget *d, int value) {
    for (int i = 0; i < 3; i++) {
        setFlags_Widget(findChild_Widget(d, format_CStr("prefs.pageturn.%d", i)),
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

/* Slider levels correspond to these speeds. */
static const int scrollSpeeds_[] = { 7, 10, 13, 17, 23, 30, 40 };

static int scrollSpeed_(int level) {
    return scrollSpeeds_[iClamp(level, 0, (int) iElemCount(scrollSpeeds_) - 1)];
}

static int scrollSpeedLevel_(int speed) {
    int level = 0;
    iForIndices(i, scrollSpeeds_) {
        if (iAbs(scrollSpeeds_[i] - speed) < iAbs(scrollSpeeds_[level] - speed)) {
            level = (int) i;
        }
    }
    return level;
}

static void updateScrollSpeedSlider_(iWidget *d, enum iScrollType type, const int value) {
    const char *id = type == mouse_ScrollType ? "prefs.scrollspeed.mouse"
                                              : "prefs.scrollspeed.keyboard";
    setValue_SliderWidget(findChild_Widget(d, id), scrollSpeedLevel_(value));
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
            const float uiScale = value_SliderWidget(findChild_Widget(d, "prefs.uiscale"));
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
                         (int) lroundf(value_SliderWidget(findChild_Widget(d, "prefs.urlsize")) *
                                       1024));
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
    else if (equal_Command(cmd, "gempubsidebar.set")) {
        updatePrefsGempubSidebarButtons_(d, arg_Command(cmd));
        return iFalse;
    }
    else if (equal_Command(cmd, "promptposition.set")) {
        updatePrefsPromptPositionButtons_(d, arg_Command(cmd));
        return iFalse;
    }
    else if (equal_Command(cmd, "pageturn.set")) {
        updatePrefsPageTurnButtons_(d, arg_Command(cmd));
        return iFalse;
    }
    else if (equal_Command(cmd, "feedinterval.set")) {
        updateFeedIntervalButton_(findChild_Widget(d, "prefs.feedinterval"), arg_Command(cmd));
        return iFalse;
    }
    else if (equal_Command(cmd, "syncinterval.set")) {
        updateDropdownSelection_LabelWidget(findChild_Widget(d, "prefs.sync.interval"),
                                            format_CStr(".set arg:%d", arg_Command(cmd)));
        return iFalse;
    }
    else if (equal_Command(cmd, "sync.identity")) {
        updateSyncIdentityButton_(d, suffixPtr_Command(cmd, "fp"));
        return iFalse;
    }
    else if (equal_Command(cmd, "sync.status.changed")) {
        updateSyncStatus_(d);
        return iFalse;
    }
    else if (equalArg_Command(cmd, "input.ended", "id", "prefs.sync.server")) {
        applySyncServer_(d);
        return iFalse;
    }
    else if (equal_Command(cmd, "sync.now") && !argLabel_Command(cmd, "applied")) {
        if (applySyncServer_(d)) {
            postCommand_App("sync.now applied:1");
            return iTrue;
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "scrollspeed")) {
        updateScrollSpeedSlider_(d, argLabel_Command(cmd, "type"), arg_Command(cmd));
        return iFalse;
    }
    else if (equal_Command(cmd, "scrollspeed.keyboard") ||
             equal_Command(cmd, "scrollspeed.mouse")) {
        postCommandf_App("scrollspeed arg:%d type:%d",
                         scrollSpeed_(arg_Command(cmd)),
                         equal_Command(cmd, "scrollspeed.mouse") ? mouse_ScrollType
                                                                 : keyboard_ScrollType);
        return iTrue;
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
    else if (equal_Command(cmd, "lang.changed")) {
        postCommand_Root(d->root, "root.arrange"); /* after the labels are translated */
        return iFalse;
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

static void openPanel_PrefsDialog_(iWidget *dlg, const char *panelId) {
    iWidget *button = findChild_Widget(dlg, format_CStr("%s.button", panelId));
    if (button) {
        postCommand_Widget(button, "panel.open");
    }
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
    updatePrefsGempubSidebarButtons_(dlg, prefs->gempubSidebar);
    updatePrefsPromptPositionButtons_(dlg, prefs->promptPosition);
    updatePrefsPageTurnButtons_(dlg, prefs->pageTurn);
    iForIndices(type, prefs->smoothScrollSpeed) {
        updateScrollSpeedSlider_(dlg, type, prefs->smoothScrollSpeed[type]);
    }
    updateFeedIntervalButton_(findChild_Widget(dlg, "prefs.feedinterval"), prefs->feedInterval);
    updateDropdownSelection_LabelWidget(findChild_Widget(dlg, "prefs.sync.interval"),
                                        format_CStr(".set arg:%d", prefs->syncInterval));
    updateSyncIdentityButton_(dlg, cstr_String(&prefs->strings[syncIdentity_PrefsString]));
    if (findChild_Widget(dlg, "prefs.sync.identity")) {
        setMenuUpdateItemsFunc_Widget(
            dropdownMenu_Widget(findChild_Widget(dlg, "prefs.sync.identity")), syncIdentityItems_);
    }
    updateSyncStatus_(dlg);
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
    setValue_SliderWidget(findChild_Widget(dlg, "prefs.uiscale"),
                          uiScale_Window(activeWindow_App()));
    updateFontButton_(findChild_Widget(dlg, "prefs.font.ui"), &prefs->strings[uiFont_PrefsString]);
    init_ScopedPrefs(dlg, "base"); /* the selection is not saved */
    setText_InputWidget(findChild_Widget(dlg, "prefs.cachesize"),
                        collectNewFormat_String("%d", prefs->maxCacheSize));
    setText_InputWidget(findChild_Widget(dlg, "prefs.memorysize"),
                        collectNewFormat_String("%d", prefs->maxMemorySize));
    setValue_SliderWidget(findChild_Widget(dlg, "prefs.urlsize"), prefs->maxUrlSize / 1024.0f);
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
        updatePreferencesLayout_Widget(dlg); /* apply window size limits and remembered height */
        showPromotedDialog_Widget(dlg);
    }
    if (argLabel_Command(cmd, "idents") && deviceType_App() != desktop_AppDeviceType) {
        openPanel_PrefsDialog_(dlg, "sidebar.identities");
    }
    if (argLabel_Command(cmd, "sniped")) {
        if (deviceType_App() == desktop_AppDeviceType) {
            postCommand_Widget(dlg, "tabs.switch id:sniped");
        }
        else {
            openPanel_PrefsDialog_(dlg, "heading.prefs.snip");
        }
    }
    if (argLabel_Command(cmd, "sidecfg")) {
        if (deviceType_App() == desktop_AppDeviceType) {
            postCommand_Widget(dlg, "tabs.switch id:sidecfg");
        }
        else {
            openPanel_PrefsDialog_(dlg, "heading.prefs.toolbars"); /* has the sidebar tabs */
        }
    }
    if (isPointerHidden_Gamepad(gamepad_App())) {
        setFocus_Widget(findFocusable_Widget(dlg, forward_WidgetFocusDir));
    }
}
