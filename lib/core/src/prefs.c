/* Copyright 2020-2022 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "lagrange/prefs.h"
#include "lagrange/core.h"
#include "lagrange/gmutil.h"

#include <assert.h>
#include <the_Foundation/fileinfo.h>

static const iPrefs *prefs_;

const iPrefs *get_Prefs(void) {
    return prefs_;
}

#if defined (_MSC_VER)
/* MSVC doesn't accept bools[x] here. */
#   define assertBoolLayout_(member) \
        _Static_assert(offsetof(iPrefs, member) == offsetof(iPrefs, bools) + member##_PrefsBool, \
                       "memory layout mismatch (needs struct packing?)")
#else
#   define assertBoolLayout_(member) \
        _Static_assert(offsetof(iPrefs, member) == offsetof(iPrefs, bools[member##_PrefsBool]), \
                       "memory layout mismatch (needs struct packing?)")
#endif

assertBoolLayout_(useSystemTheme);
assertBoolLayout_(retainTabs);
assertBoolLayout_(useProxy);
assertBoolLayout_(warnTlsSecurity);
assertBoolLayout_(geminiStyledGopher);
assertBoolLayout_(colorEmoji);

/* Note: No ID and no config command means the value is not serialized automatically. */
static const iPrefsSpec boolSpecs_[max_PrefsBool] = {
    /* Window and User Interface */
    [useSystemTheme_PrefsBool] =
        { "prefs.ostheme", "ostheme", NULL,
          noSerialize_PrefsSpecFlag | noDispatch_PrefsSpecFlag }, /* has extra arguments */
    [customFrame_PrefsBool] =
        { "prefs.customframe", "customframe", NULL,
          noSerialize_PrefsSpecFlag }, /* build-time conditional */
    [retainWindowSize_PrefsBool]        = { "prefs.retainwindow", "window.retain" },
    [uiAnimations_PrefsBool]            = { "prefs.animate" },
    [hideToolbarOnScroll_PrefsBool]     = { "prefs.hidetoolbarscroll", "hidetoolbarscroll", NULL,
                                            mobileOnly_PrefsSpecFlag },
    [hideTabBar_PrefsBool]              = { "prefs.hidetabs", NULL, "~root.movable" },
    [blinkingCursor_PrefsBool]          = { "prefs.blink" },
    [bottomNavBar_PrefsBool]            = { "prefs.bottomnavbar", NULL, "~root.movable" },
    [bottomTabBar_PrefsBool]            = { "prefs.bottomtabbar", NULL, "~root.movable" },
    [menuBar_PrefsBool]                 = { "prefs.menubar", NULL, NULL,
                                            noDispatch_PrefsSpecFlag }, /* forced in TUI */
    [simpleChars_PrefsBool]             = { "prefs.tui.simple" },
    [evenSplit_PrefsBool]               = { "prefs.evensplit" },
    [detachedPrefs_PrefsBool]           = { NULL, NULL, NULL,
                                            noSerialize_PrefsSpecFlag | noDispatch_PrefsSpecFlag },
    [editorSyntaxHighlighting_PrefsBool] = { "prefs.editor.highlight" },
    [useGamepad_PrefsBool]              = { "prefs.gamepad" },
    [thickScrollBar_PrefsBool]          = { "prefs.thickscroll" },
    /* Document presentation */
    [italicQuote_PrefsBool]             = { "prefs.quote.italic" },
    [sideIcon_PrefsBool]                = { "prefs.sideicon", NULL, NULL, refresh_PrefsSpecFlag },
    [time24h_PrefsBool]                 = { "prefs.time.24h" },
    /* Behavior */
    [retainTabs_PrefsBool]              = { "prefs.retaintabs" },
    [hoverLink_PrefsBool]               = { "prefs.hoverlink", NULL, NULL, refresh_PrefsSpecFlag },
    [smoothScrolling_PrefsBool]         = { "prefs.smoothscroll", "smoothscroll" },
    [loadImageInsteadOfScrolling_PrefsBool] = { "prefs.imageloadscroll", "imageloadscroll" },
    [openDataUrlImagesOnLoad_PrefsBool] = { "prefs.dataurl.openimages" },
    [openArchiveIndexPages_PrefsBool]   = { "prefs.archive.openindex" },
    [addBookmarksToBottom_PrefsBool]    = { "prefs.bookmarks.addbottom" },
    [warnAboutMissingGlyphs_PrefsBool]  = { "prefs.font.warnmissing" },
    [markdownAsSource_PrefsBool]        = { "prefs.markdown.viewsource" },
    [skipIndexPageOnParentNavigation_PrefsBool] = { NULL, "parentnavskipindex" },
    [edgeSwipe_PrefsBool]               = { "prefs.swipe.edge" },
    [pageSwipe_PrefsBool]               = { "prefs.swipe.page" },
    [capsLockKeyModifier_PrefsBool]     = { NULL, NULL, NULL,
                                            noSerialize_PrefsSpecFlag | noDispatch_PrefsSpecFlag },
    [misfinSelfCopy_PrefsBool]          = { "misfin.self.copy" },
    /* Network */
    [warnTlsSecurity_PrefsBool]        = { "prefs.warn.security" },
    [decodeUserVisibleURLs_PrefsBool]   = { "prefs.decodeurls", "decodeurls" },
    [allowSchemeChangingRedirect_PrefsBool] = { "prefs.redirect.allowscheme" },
    [preferIPv6_PrefsBool]              = { "prefs.ipv6" },
    [useProxy_PrefsBool]                = { "prefs.socks" },
    /* Style */
    [monospaceGemini_PrefsBool]         = { "prefs.mono.gemini" },
    [monospaceGopher_PrefsBool]         = { "prefs.mono.gopher" },
    [boldLinkVisited_PrefsBool]         = { "prefs.boldlink.visited", NULL, "font.changed" },
    [boldLinkDark_PrefsBool]            = { "prefs.boldlink.dark",    NULL, "font.changed" },
    [boldLinkLight_PrefsBool]           = { "prefs.boldlink.light",   NULL, "font.changed" },
    [fontSmoothing_PrefsBool]           = { "prefs.font.smooth" },
    [bigFirstParagraph_PrefsBool]       = { "prefs.biglede", NULL, "document.layout.changed" },
    [justifyParagraph_PrefsBool]        = { "prefs.justify", NULL, "document.layout.changed" },
    [quoteIcon_PrefsBool]               = { NULL, "quoteicon.set",
                                            "document.layout.changed redo:1" },
    [centerShortDocs_PrefsBool]         = { "prefs.centershort", NULL, NULL,
                                            invalidate_PrefsSpecFlag },
    [plainTextWrap_PrefsBool]           = { "prefs.plaintext.wrap", NULL, "document.layout.changed" },
    [expandToLongLines_PrefsBool]       = { "prefs.expandline",    NULL, "document.layout.changed" },
    [geminiStyledGopher_PrefsBool]      = { "prefs.gopher.gemstyle" },
    [colorEmoji_PrefsBool]              = { "prefs.font.coloremoji" },
};

const iPrefsSpec *boolSpec_Prefs(enum iPrefsBool id) {
    iAssert(id >= 0 && id < max_PrefsBool);
    return &boolSpecs_[id];
}

static iBool isChangeCommand_(const iPrefsSpec *d, iRangecc cmdName) {
    static const char *suffix = ".changed";
    const size_t       idLen  = d->id ? strlen(d->id) : 0;
    return idLen && size_Range(&cmdName) == idLen + strlen(suffix) &&
           !iCmpStrN(cmdName.start, d->id, idLen) &&
           !iCmpStrN(cmdName.start + idLen, suffix, strlen(suffix));
}

enum iPrefsBool findBool_Prefs(iRangecc cmdName) {
    for (int i = 0; i < max_PrefsBool; i++) {
        const iPrefsSpec *spec = &boolSpecs_[i];
        if (spec->flags & noDispatch_PrefsSpecFlag) {
            continue;
        }
        if ((spec->cfgCmd && equal_Rangecc(cmdName, spec->cfgCmd)) ||
            isChangeCommand_(spec, cmdName)) {
            return i;
        }
    }
    return max_PrefsBool;
}

iBool setBool_Prefs(iPrefs *d, enum iPrefsBool id, iBool value) {
    iAssert(id >= 0 && id < max_PrefsBool);
    if (d->bools[id] == value) {
        return iFalse;
    }
    d->bools[id] = value;
    return iTrue;
}

void serializeBools_Prefs(const iPrefs *d, iString *out) {
    for (int i = 0; i < max_PrefsBool; i++) {
        const iPrefsSpec *spec = &boolSpecs_[i];
        if (spec->flags & noSerialize_PrefsSpecFlag ||
            ((spec->flags & mobileOnly_PrefsSpecFlag) && !isMobile_Platform())) {
            continue;
        }
        if (spec->cfgCmd) {
            appendFormat_String(out, "%s arg:%d\n", spec->cfgCmd, d->bools[i]);
        }
        else {
            appendFormat_String(out, "%s.changed arg:%d\n", spec->id, d->bools[i]);
        }
    }
}

/* Note: Fonts and the download directory are serialized by hand. */
static const iPrefsStringSpec stringSpecs_[max_PrefsString] = {
    /* General */
    [uiLanguage_PrefsString]     = { "uilang", "id" },
    [keyboardLayout_PrefsString] = { "keyboard", "id" },
    [downloadDir_PrefsString]    = { "downloads", "path", NULL, "prefs.downloads",
                                     noSerialize_PrefsSpecFlag | noDispatch_PrefsSpecFlag },
    [searchUrl_PrefsString]      = { "searchurl", "address", NULL, "prefs.searchurl" },
    [recentMisfinId_PrefsString] = { "misfin.recent", "fp" },
    /* Network */
    [caFile_PrefsString]         = { "ca.file", "path", "noset:1", "prefs.ca.file" },
    [caPath_PrefsString]         = { "ca.path", "path", NULL, "prefs.ca.path" },
    [geminiProxy_PrefsString]    = { "proxy.gemini", "address", NULL, "prefs.proxy.gemini" },
    [gopherProxy_PrefsString]    = { "proxy.gopher", "address", NULL, "prefs.proxy.gopher" },
    [httpProxy_PrefsString]      = { "proxy.http", "address", NULL, "prefs.proxy.http" },
    [socksServer_PrefsString]    = { "proxy.socks", "address", "noupdate:1", "prefs.socks.server" },
    [socksUser_PrefsString]      = { "proxy.socks", "user", "noupdate:1", "prefs.socks.user" },
    [socksPassword_PrefsString]  = { "proxy.socks", "password", NULL, "prefs.socks.password" },
    /* Style */
    [uiFont_PrefsString]         = { "font.set", "ui", NULL, NULL, noSerialize_PrefsSpecFlag | noDispatch_PrefsSpecFlag },
    [headingFont_PrefsString]    = { "font.set", "heading", NULL, NULL, noSerialize_PrefsSpecFlag | noDispatch_PrefsSpecFlag },
    [bodyFont_PrefsString]       = { "font.set", "body", NULL, NULL, noSerialize_PrefsSpecFlag | noDispatch_PrefsSpecFlag },
    [monospaceFont_PrefsString]  = { "font.set", "mono", NULL, NULL, noSerialize_PrefsSpecFlag | noDispatch_PrefsSpecFlag },
    [monospaceDocumentFont_PrefsString] = { "font.set", "monodoc", NULL, NULL, noSerialize_PrefsSpecFlag | noDispatch_PrefsSpecFlag },
};

const iPrefsStringSpec *stringSpec_Prefs(enum iPrefsString id) {
    iAssert(id >= 0 && id < max_PrefsString);
    return &stringSpecs_[id];
}

void serializeStrings_Prefs(const iPrefs *d, iString *out) {
    for (int i = 0; i < max_PrefsString; i++) {
        const iPrefsStringSpec *spec = &stringSpecs_[i];
        if (spec->flags & noSerialize_PrefsSpecFlag) {
            continue;
        }
        appendFormat_String(out, "%s ", spec->cmd);
        if (spec->args) {
            appendFormat_String(out, "%s ", spec->args);
        }
        appendFormat_String(out, "%s:%s\n", spec->label, cstr_String(&d->strings[i]));
    }
}

iDefineTypeConstruction(Prefs)

void init_Prefs(iPrefs *d) {
    prefs_ = d; /* global access; there is only one Prefs */
#if !defined (NDEBUG)
    iForIndices(i, boolSpecs_) { /* validate the specs */
        iAssert(boolSpecs_[i].id || boolSpecs_[i].cfgCmd ||
                boolSpecs_[i].flags & noSerialize_PrefsSpecFlag);
    }
#endif
    iForIndices(i, d->strings) {
        init_String(&d->strings[i]);
    }
    setCStr_String(&d->strings[keyboardLayout_PrefsString], "us-english"); /* KeyboardWidget */
    d->dialogTab                    = 0;
    d->langFrom                     = 0; /* auto-detect */
    d->langTo                       = 8; /* en */
    d->translationIgnorePre         = iTrue;
    d->recentMenuBarIndex           = 0;
    d->useSystemTheme               = iTrue;
    d->systemPreferredColorTheme[0] = pureBlack_ColorTheme;
    d->systemPreferredColorTheme[1] = pureWhite_ColorTheme;
    d->theme                        = dark_ColorTheme;
    d->accent                   = isAppleDesktop_Platform() ? system_ColorAccent : cyan_ColorAccent;
    d->customFrame              = iFalse; /* needs some more work to be default */
    d->retainWindowSize         = iTrue;
    d->uiAnimations             = iTrue;
    d->inputZoomLevel           = 0;
    d->editorZoomLevel          = 0;
    d->editorSyntaxHighlighting = iTrue;
    d->useGamepad               = isDesktop_Platform(); /* enabled by default on desktop */
    d->thickScrollBar           = iFalse;
    d->zoomPercent              = 100;
    d->navbarActions[0]         = back_ToolbarAction;
    d->navbarActions[1]         = forward_ToolbarAction;
    d->navbarActions[2]         = leftSidebar_ToolbarAction;
    d->navbarActions[3]         = home_ToolbarAction;
#if defined (iPlatformAndroidMobile)
    /* Android has a system-wide back button so no need to have a duplicate. */
    d->toolbarActions[0] = closeTab_ToolbarAction;
#else
    d->toolbarActions[0] = back_ToolbarAction;
#endif
    d->toolbarActions[1] = forward_ToolbarAction;
    iZap(d->sidebarModeEnabled);
    if (deviceType_App() == phone_AppDeviceType) {
        /* Phone layout has only the one sidebar. */
        iForIndices(i, d->sidebarModeEnabled[0]) {
            d->sidebarModeEnabled[0][i] = (i != subscriptions_SidebarMode &&
                                           i != identities_SidebarMode &&
                                           i != openDocuments_SidebarMode);
        }
    }
    else {
        d->sidebarModeEnabled[0][bookmarks_SidebarMode] = iTrue;
        d->sidebarModeEnabled[0][feedEntries_SidebarMode] = iTrue;
        d->sidebarModeEnabled[0][subscriptions_SidebarMode] = iTrue;
        d->sidebarModeEnabled[0][identities_SidebarMode] = iTrue;
        d->sidebarModeEnabled[1][documentOutline_SidebarMode] = iTrue;
        d->sidebarModeEnabled[1][siteStructure_SidebarMode] = iTrue;
        d->sidebarModeEnabled[1][openDocuments_SidebarMode] = iTrue;
        d->sidebarModeEnabled[1][history_SidebarMode] = iTrue;
    }
    d->sideIcon            = iTrue;
    d->hideToolbarOnScroll = iTrue;
    d->hideTabBar          = iFalse;
    d->blinkingCursor      = iTrue;
    if (deviceType_App() == phone_AppDeviceType) {
        d->bottomNavBar = iTrue;
        d->bottomTabBar = iTrue;
    }
    else {
        d->bottomNavBar = iFalse;
        d->bottomTabBar = iFalse;
    }
    if (isTerminal_Platform()) {
        d->bottomNavBar = iTrue;
        d->bottomTabBar = iTrue;
    }
    d->menuBar                                = (deviceType_App() == desktop_AppDeviceType);
    d->simpleChars                            = iTrue;  /* only in terminal */
    d->evenSplit                              = iFalse; /* split mode tabs have even width */
    d->detachedPrefs                          = iTrue;
    d->pinSplit                               = 1;
    d->promptPosition                         = inline_InputPromptPosition;
    d->feedInterval                           = fourHours_FeedInterval;
    d->italicQuote                            = iTrue;
    d->time24h                                = iTrue;
    d->returnKey                              = default_ReturnKeyBehavior;
    d->retainTabs                             = iTrue;
    d->hoverLink                              = iTrue;
    d->smoothScrolling                        = !isTerminal_Platform();
    d->smoothScrollSpeed[keyboard_ScrollType] = 13;
    d->smoothScrollSpeed[mouse_ScrollType]    = 13;
    d->loadImageInsteadOfScrolling            = iFalse;
    d->openDataUrlImagesOnLoad                = iFalse;
    d->collapsePre                            = notByDefault_Collapse;
    d->openArchiveIndexPages                  = iTrue;
    d->addBookmarksToBottom                   = iTrue;
    d->warnAboutMissingGlyphs                 = iTrue;
    d->markdownAsSource                       = iTrue;
    d->skipIndexPageOnParentNavigation        = iTrue;
    d->edgeSwipe                              = !isAndroid_Platform(); /* conflict with system */
    d->pageSwipe                              = iTrue;
    d->capsLockKeyModifier                    = iFalse;
    d->misfinSelfCopy                         = iTrue;
    d->allowSchemeChangingRedirect            = iFalse; /* must be manually followed */
    d->preferIPv6                             = iFalse;
    d->useProxy                               = iTrue;
    d->decodeUserVisibleURLs                  = iTrue;
    d->warnTlsSecurity                        = iTrue;
    d->maxCacheSize                           = 10;
    d->maxMemorySize                          = 200;
    d->maxUrlSize                             = 8192;
    setCStr_String(&d->strings[uiFont_PrefsString], "default");
    setCStr_String(&d->strings[headingFont_PrefsString], "default");
    setCStr_String(&d->strings[bodyFont_PrefsString], "default");
    setCStr_String(&d->strings[monospaceFont_PrefsString], "iosevka");
    setCStr_String(&d->strings[monospaceDocumentFont_PrefsString], "iosevka-body");
    d->disabledFontPacks  = new_StringSet();
    d->fontSmoothing      = iTrue;
    d->gemtextAnsiEscapes = allowFg_AnsiFlag;
    d->monospaceGemini    = iFalse;
    d->monospaceGopher    = iTrue;
    d->boldLinkVisited    = iFalse;
    d->boldLinkDark       = iTrue;
    d->boldLinkLight      = iTrue;
    d->lineWidth          = deviceType_App() == phone_AppDeviceType ? 1000 /* fill */ : 38;
    d->lineSpacing        = 1.0f;
    d->tabWidth           = 8;
    d->bigFirstParagraph  = iTrue;
    d->justifyParagraph   = iFalse;
    d->quoteIcon          = iTrue;
    d->centerShortDocs    = iTrue;
    d->plainTextWrap      = iTrue;
    d->expandToLongLines  = iTrue;
    d->geminiStyledGopher = iFalse;
    d->colorEmoji         = iFalse;
    d->imageStyle         = original_ImageStyle;
    d->docThemeDark       = colorfulDark_GmDocumentTheme;
    d->docThemeLight      = white_GmDocumentTheme;
    d->saturation         = 1.0f;
    setCStr_String(&d->strings[uiLanguage_PrefsString], "en");
    /* TODO: Add some platform-specific common locations? */
    if (fileExistsCStr_FileInfo("/etc/ssl/cert.pem")) { /* macOS */
        setCStr_String(&d->strings[caFile_PrefsString], "/etc/ssl/cert.pem");
    }
    if (fileExistsCStr_FileInfo("/etc/ssl/certs")) {
        setCStr_String(&d->strings[caPath_PrefsString], "/etc/ssl/certs");
    }
}

void deinit_Prefs(iPrefs *d) {
    iRelease(d->disabledFontPacks);
    iForIndices(i, d->strings) {
        deinit_String(&d->strings[i]);
    }
}

const iString *schemeProxy_Prefs(const iPrefs *d, iRangecc scheme) {
    const iString *proxy = NULL;
    if (equalCase_Rangecc(scheme, "gemini")) {
        proxy = &d->strings[geminiProxy_PrefsString];
    }
    else if (isGopherScheme_Rangecc(scheme)) {
        proxy = &d->strings[gopherProxy_PrefsString];
    }
    else if (equalCase_Rangecc(scheme, "http") || equalCase_Rangecc(scheme, "https")) {
        proxy = &d->strings[httpProxy_PrefsString];
    }
    return isEmpty_String(proxy) ? NULL : proxy;
}

iBool schemeProxyHostAndPort_Prefs(const iPrefs *d, iRangecc scheme, const iString **host,
                                   uint16_t *port) {
    const iString *proxy = schemeProxy_Prefs(d, scheme);
    if (!proxy) {
        return iFalse;
    }
    if (contains_String(proxy, ':')) {
        const size_t cpos = indexOf_String(proxy, ':');
        *port = atoi(cstr_String(proxy) + cpos + 1);
        *host = collect_String(newCStrN_String(cstr_String(proxy), cpos));
    }
    else {
        *host = proxy;
        *port = 0;
    }
    return iTrue;
}
