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

#include <the_Foundation/stringset.h>

#include "gmdocument.h"
#include "ui/color.h"
#include "ui/text.h"

/* User preferences */

iDeclareType(Prefs)

enum iPrefsString {
    /* General */
    uiLanguage_PrefsString,
    downloadDir_PrefsString,
    searchUrl_PrefsString,

    /* Network */
    caFile_PrefsString,
    caPath_PrefsString,
    geminiProxy_PrefsString,
    gopherProxy_PrefsString,
    httpProxy_PrefsString,

    /* Style */
    uiFont_PrefsString,
    headingFont_PrefsString,
    bodyFont_PrefsString,
    monospaceFont_PrefsString,
    monospaceDocumentFont_PrefsString,

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

    blinkingCursor_PrefsBool,
    bottomNavBar_PrefsBool,
    bottomTabBar_PrefsBool,
    menuBar_PrefsBool,
    simpleChars_PrefsBool,

    evenSplit_PrefsBool,
    detachedPrefs_PrefsBool,
    editorSyntaxHighlighting_PrefsBool,

    /* Document presentation */
    sideIcon_PrefsBool,
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
    markdownAsSource_PrefsBool,
    skipIndexPageOnParentNavigation_PrefsBool,

    edgeSwipe_PrefsBool,
    pageSwipe_PrefsBool,
    capsLockKeyModifier_PrefsBool,

    /* Network */
    decodeUserVisibleURLs_PrefsBool,
    allowSchemeChangingRedirect_PrefsBool,

    /* Style */
    monospaceGemini_PrefsBool,
    monospaceGopher_PrefsBool,
    boldLinkVisited_PrefsBool,
    boldLinkDark_PrefsBool,
    boldLinkLight_PrefsBool,

    fontSmoothing_PrefsBool,
    bigFirstParagraph_PrefsBool,
    justifyParagraph_PrefsBool,
    quoteIcon_PrefsBool,
    centerShortDocs_PrefsBool,

    plainTextWrap_PrefsBool,
    geminiStyledGopher_PrefsBool,

    /* Meta */
    max_PrefsBool
};

enum iCollapse {
    never_Collapse,
    notByDefault_Collapse,
    byDefault_Collapse,
    always_Collapse,
};

#define maxNavbarActions_Prefs  4

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

            iBool blinkingCursor;
            iBool bottomNavBar;
            iBool bottomTabBar;
            iBool menuBar;
            iBool simpleChars;

            iBool evenSplit;
            iBool detachedPrefs;
            iBool editorSyntaxHighlighting;

            /* Document presentation */
            iBool sideIcon;
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
            iBool markdownAsSource;
            iBool skipIndexPageOnParentNavigation;

            iBool edgeSwipe; /* mobile: one can swipe from edges to navigate */
            iBool pageSwipe; /* mobile: one can swipe over the page to navigate */
            iBool capsLockKeyModifier;

            /* Network */
            iBool decodeUserVisibleURLs;
            iBool allowSchemeChangingRedirect;

            /* Style */
            iBool monospaceGemini;
            iBool monospaceGopher;
            iBool boldLinkVisited;
            iBool boldLinkDark;
            iBool boldLinkLight;

            iBool fontSmoothing;
            iBool bigFirstParagraph;
            iBool justifyParagraph;
            iBool quoteIcon;
            iBool centerShortDocs;

            iBool plainTextWrap;
            iBool geminiStyledGopher;
        };
    };
    /* UI state (belongs to state.lgr...) */
    int              dialogTab;
    int              langFrom;
    int              langTo;
    iBool            translationIgnorePre;
    /* Colors */
    enum iColorTheme systemPreferredColorTheme[2]; /* dark, light */
    enum iColorTheme theme;
    enum iColorAccent accent;
    /* Window and User Interface */
    float            uiScale;
    enum iToolbarAction navbarActions[maxNavbarActions_Prefs];
    enum iToolbarAction toolbarActions[2];
    int              inputZoomLevel;
    int              editorZoomLevel;
    /* Document presentation */
    int              zoomPercent;
    /* Behavior */
    int              pinSplit; /* 0: no pinning, 1: left doc, 2: right doc */
    enum iFeedInterval feedInterval;
    int              returnKey;
    int              smoothScrollSpeed[max_ScrollType];
    enum iCollapse   collapsePre;
    /* Network */
    int              maxCacheSize; /* MB */
    int              maxMemorySize; /* MB */
    int              maxUrlSize; /* bytes; longer ones will be disregarded */
    /* Style */
    iStringSet *     disabledFontPacks;
    int              gemtextAnsiEscapes;
    int              lineWidth;
    float            lineSpacing;
    int              tabWidth;
    enum iImageStyle imageStyle;
    /* Colors */
    enum iGmDocumentTheme docThemeDark;
    enum iGmDocumentTheme docThemeLight;
    float                 saturation;
};

iDeclareTypeConstruction(Prefs)

iLocalDef float scrollSpeedFactor_Prefs(const iPrefs *d, enum iScrollType type) {
    iAssert(type >= 0 && type < max_ScrollType);
    return 10.0f / iMax(1, d->smoothScrollSpeed[type]) * (type == mouse_ScrollType ? 0.5f : 1.0f);
}

iLocalDef enum iGmDocumentTheme docTheme_Prefs(const iPrefs *d) {
    return isDark_ColorTheme(d->theme) ? d->docThemeDark : d->docThemeLight;
}
