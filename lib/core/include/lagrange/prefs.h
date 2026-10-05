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

#include "defs.h"
#include "docprefs.h"
#include <the_Foundation/stringset.h>

/* User preferences */

iDeclareType(Prefs);

const iPrefs *  get_Prefs   (void); /* returns the global instance (set in init_Prefs) */

enum iPrefsString {
    /* General */
    uiLanguage_PrefsString,
    keyboardLayout_PrefsString,
    downloadDir_PrefsString,
    searchUrl_PrefsString,
    recentMisfinId_PrefsString,

    /* Network */
    caFile_PrefsString,
    caPath_PrefsString,
    geminiProxy_PrefsString,
    gopherProxy_PrefsString,
    httpProxy_PrefsString,

    socksServer_PrefsString,
    socksUser_PrefsString,
    socksPassword_PrefsString,

    /* Style */
    uiFont_PrefsString,

    /* Meta */
    max_PrefsString
};

/* Note: These match match the array/struct in Prefs. */
enum iPrefsBool {
    /* Window and User Interface */
    useSystemTheme_PrefsBool,
    customFrame_PrefsBool,
    retainWindowSize_PrefsBool,
    uiAnimations_PrefsBool,
    hideToolbarOnScroll_PrefsBool,

    hideTabBar_PrefsBool,
    blinkingCursor_PrefsBool,
    bottomNavBar_PrefsBool,
    bottomTabBar_PrefsBool,

    menuBar_PrefsBool,
    simpleChars_PrefsBool,
    evenSplit_PrefsBool,
    detachedPrefs_PrefsBool,
    editorSyntaxHighlighting_PrefsBool,

    useGamepad_PrefsBool,
    thickScrollBar_PrefsBool,
    time24h_PrefsBool,


    /* Behavior */
    retainTabs_PrefsBool,
    hoverLink_PrefsBool,
    smoothScrolling_PrefsBool,
    loadImageInsteadOfScrolling_PrefsBool,
    openDataUrlImagesOnLoad_PrefsBool,

    openArchiveIndexPages_PrefsBool,
    addBookmarksToBottom_PrefsBool,
    warnAboutMissingGlyphs_PrefsBool,
    skipIndexPageOnParentNavigation_PrefsBool,

    edgeSwipe_PrefsBool,
    pageSwipe_PrefsBool,
    capsLockKeyModifier_PrefsBool,
    misfinSelfCopy_PrefsBool,

    /* Network */
    warnTlsSecurity_PrefsBool,
    decodeUserVisibleURLs_PrefsBool,
    allowSchemeChangingRedirect_PrefsBool,
    preferIPv6_PrefsBool,
    useProxy_PrefsBool,

    /* Style */
    fontSmoothing_PrefsBool,
    colorEmoji_PrefsBool,

    /* Meta */
    max_PrefsBool
};

enum iPrefsSpecFlags {
    noSerialize_PrefsSpecFlag = iBit(1), /* written to the config file by hand */
    noDispatch_PrefsSpecFlag  = iBit(2), /* stored by a dedicated command handler */
    mobileOnly_PrefsSpecFlag  = iBit(3), /* only serialized on mobile */
    refresh_PrefsSpecFlag     = iBit(4), /* refresh all windows after a change */
    invalidate_PrefsSpecFlag  = iBit(5), /* invalidate the current window after a change */
};

iDeclareType(PrefsSpec)

struct Impl_PrefsSpec {
    const char *id;     /* widget ID; the change command is "<id>.changed" */
    const char *cfgCmd; /* command in the configuration file; NULL means "<id>.changed" */
    const char *notify; /* command posted after the value has changed */
    uint32_t    flags;
};

const iPrefsSpec *  boolSpec_Prefs      (enum iPrefsBool);
enum iPrefsBool     findBool_Prefs      (iRangecc cmdName); /* max_PrefsBool if not found */
iBool               setBool_Prefs       (iPrefs *, enum iPrefsBool, iBool value); /* true if changed */
void                serializeBools_Prefs(const iPrefs *, iString *out);

iDeclareType(PrefsStringSpec)

struct Impl_PrefsStringSpec {
    const char *cmd;      /* command that sets the value */
    const char *label;    /* argument carrying the value; always the last one in the command */
    const char *args;     /* extra arguments that defer any side effects to a later pref */
    const char *widgetId; /* InputWidget in Preferences */
    uint32_t    flags;
};

const iPrefsStringSpec *stringSpec_Prefs      (enum iPrefsString);
void                    serializeStrings_Prefs(const iPrefs *, iString *out);

enum iInputPromptPosition {
    inline_InputPromptPosition = 0, /* embedded under the link when possible, otherwise modal */
    top_InputPromptPosition    = 1, /* always modal, docked at the top */
    bottom_InputPromptPosition = 2, /* always modal, docked at the bottom */
};

enum iPageTurnAnimation {
    none_PageTurnAnimation   = 0,
    scroll_PageTurnAnimation = 1,
    curl_PageTurnAnimation   = 2,
};

#define maxNavbarActions_Prefs  4
#define maxSidebarModes_Prefs   8

/* TODO: Use a systematic command naming convention for notifications. */

struct Impl_Prefs {
    iString strings[max_PrefsString];
    union {
        iBool bools[max_PrefsBool];
        /* For convenience, contents of the array are accessible also via these members. */
        struct {
            /* Window and User Interface */
            iBool useSystemTheme;
            iBool customFrame; /* when LAGRANGE_ENABLE_CUSTOM_FRAME is defined */
            iBool retainWindowSize;
            iBool uiAnimations;
            iBool hideToolbarOnScroll;

            iBool hideTabBar;
            iBool blinkingCursor;
            iBool bottomNavBar;
            iBool bottomTabBar;

            iBool menuBar;
            iBool simpleChars;
            iBool evenSplit;
            iBool detachedPrefs;
            iBool editorSyntaxHighlighting;

            iBool useGamepad;
            iBool thickScrollBar;
            iBool time24h;


            /* Behavior */
            iBool retainTabs;
            iBool hoverLink;
            iBool smoothScrolling;
            iBool loadImageInsteadOfScrolling;
            iBool openDataUrlImagesOnLoad;

            iBool openArchiveIndexPages;
            iBool addBookmarksToBottom;
            iBool warnAboutMissingGlyphs;
            iBool skipIndexPageOnParentNavigation;

            iBool edgeSwipe; /* mobile: one can swipe from edges to navigate */
            iBool pageSwipe; /* mobile: one can swipe over the page to navigate */
            iBool capsLockKeyModifier;
            iBool misfinSelfCopy;

            /* Network */
            iBool warnTlsSecurity;
            iBool decodeUserVisibleURLs;
            iBool allowSchemeChangingRedirect;
            iBool preferIPv6;
            iBool useProxy;

            /* Style */
            iBool fontSmoothing;
            iBool colorEmoji; /* macOS only: prefer Apple Color Emoji over bundled B&W fonts */
        };
    };

    /* UI state (belongs to state.lgr...) */
    int   dialogTab;
    int   siteDialogTab;
    int   langFrom;
    int   langTo;
    iBool translationIgnorePre;
    int   recentMenuBarIndex; /* most recently opened menubar child */

    /* Colors */
    enum iColorTheme  systemPreferredColorTheme[2]; /* dark, light */
    enum iColorTheme  theme;
    enum iColorAccent accent;

    /* Window and User Interface */
    enum iToolbarAction navbarActions[maxNavbarActions_Prefs];
    enum iToolbarAction toolbarActions[2];
    iBool               sidebarModeEnabled[2][maxSidebarModes_Prefs];
    int                 inputZoomLevel;
    int                 editorZoomLevel;

    /* Behavior */
    int                        pinSplit; /* 0: no pinning, 1: left doc, 2: right doc */
    int                        gempubSidebar; /* side of the Book sidebar: 0 left, 1 right */
    enum iInputPromptPosition  promptPosition; /* desktop only */
    enum iPageTurnAnimation    pageTurn;
    enum iFeedInterval         feedInterval;
    int                returnKey;
    int                smoothScrollSpeed[max_ScrollType];

    /* Network */
    int maxCacheSize;  /* MB */
    int maxMemorySize; /* MB */
    int maxUrlSize;    /* bytes; longer ones will be disregarded */

    /* Style */
    iStringSet      *disabledFontPacks;
};

iDeclareTypeConstruction(Prefs);

const iString * schemeProxy_Prefs            (const iPrefs *, iRangecc scheme);
iBool           schemeProxyHostAndPort_Prefs (const iPrefs *, iRangecc scheme, const iString **host, uint16_t *port);

iLocalDef float scrollSpeedFactor_Prefs(const iPrefs *d, enum iScrollType type) {
    iAssert(type >= 0 && type < max_ScrollType);
    return 10.0f / iMax(1, d->smoothScrollSpeed[type]) * (type == mouse_ScrollType ? 0.5f : 1.0f);
}
