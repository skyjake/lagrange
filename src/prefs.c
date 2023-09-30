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

#include "prefs.h"
#include "app.h"

#include <the_Foundation/fileinfo.h>
#include <assert.h>

_Static_assert(offsetof(iPrefs, geminiStyledGopher) ==
               offsetof(iPrefs, bools[geminiStyledGopher_PrefsBool]),
               "memory layout mismatch (needs struct packing?)");

void init_Prefs(iPrefs *d) {
    iForIndices(i, d->strings) {
        init_String(&d->strings[i]);
    }
    d->dialogTab         = 0;
    d->langFrom          = 0; /* auto-detect */
    d->langTo            = 8; /* en */
    d->translationIgnorePre = iTrue;
    d->useSystemTheme    = iTrue;
    d->systemPreferredColorTheme[0] = d->systemPreferredColorTheme[1] = -1;
    d->theme             = dark_ColorTheme;
    d->accent            = isAppleDesktop_Platform() ? system_ColorAccent : cyan_ColorAccent;
    d->customFrame       = iFalse; /* needs some more work to be default */
    d->retainWindowSize  = iTrue;
    d->uiAnimations      = iTrue;
    d->uiScale           = 1.0f; /* default set elsewhere */
    d->inputZoomLevel    = 0;
    d->editorZoomLevel   = 0;
    d->editorSyntaxHighlighting = iTrue;
    d->zoomPercent       = 100;
    d->navbarActions[0]  = back_ToolbarAction;
    d->navbarActions[1]  = forward_ToolbarAction;
    d->navbarActions[2]  = leftSidebar_ToolbarAction;
    d->navbarActions[3]  = home_ToolbarAction;
#if defined (iPlatformAndroidMobile)
    /* Android has a system-wide back button so no need to have a duplicate. */
    d->toolbarActions[0] = closeTab_ToolbarAction;
#else
    d->toolbarActions[0] = back_ToolbarAction;
#endif
    d->toolbarActions[1] = forward_ToolbarAction;
    d->sideIcon          = iTrue;
    d->hideToolbarOnScroll = iTrue;
    d->blinkingCursor    = iTrue;
    if (deviceType_App() == phone_AppDeviceType) {
        d->bottomNavBar  = iTrue;
        d->bottomTabBar  = iTrue;
    }
    else {
        d->bottomNavBar  = iFalse;
        d->bottomTabBar  = iFalse;
    }
    if (isTerminal_Platform()) {
        d->bottomNavBar  = iTrue;
    }
    d->menuBar           = (deviceType_App() == desktop_AppDeviceType);
    d->simpleChars       = iTrue; /* only in terminal */
    d->evenSplit         = iFalse; /* split mode tabs have even width */
    d->detachedPrefs     = iTrue;
    d->pinSplit          = 1;
    d->feedInterval      = fourHours_FeedInterval;
    d->time24h           = iTrue;
    d->returnKey         = default_ReturnKeyBehavior;
    d->retainTabs        = iTrue;
    d->hoverLink         = iTrue;
    d->smoothScrolling   = iTrue;
    d->smoothScrollSpeed[keyboard_ScrollType] = 13;
    d->smoothScrollSpeed[mouse_ScrollType]    = 13;
    d->loadImageInsteadOfScrolling = iFalse;
    d->openDataUrlImagesOnLoad = iFalse;
    d->collapsePre             = notByDefault_Collapse;
    d->openArchiveIndexPages   = iTrue;
    d->addBookmarksToBottom    = iTrue;
    d->warnAboutMissingGlyphs  = iTrue;
    d->markdownAsSource        = iTrue;
    d->skipIndexPageOnParentNavigation = iTrue;
    d->edgeSwipe = iTrue;
    d->pageSwipe = iTrue;
    d->capsLockKeyModifier = iFalse;
    d->allowSchemeChangingRedirect = iFalse; /* must be manually followed */
    d->decodeUserVisibleURLs = iTrue;
    d->maxCacheSize      = 10;
    d->maxMemorySize     = 200;
    d->maxUrlSize        = 8192;
    setCStr_String(&d->strings[uiFont_PrefsString], "default");
    setCStr_String(&d->strings[headingFont_PrefsString], "default");
    setCStr_String(&d->strings[bodyFont_PrefsString], "default");
    setCStr_String(&d->strings[monospaceFont_PrefsString], "iosevka");
    setCStr_String(&d->strings[monospaceDocumentFont_PrefsString], "iosevka-body");
    d->disabledFontPacks = new_StringSet();
    d->fontSmoothing     = iTrue;
    d->gemtextAnsiEscapes = allowFg_AnsiFlag;
    d->monospaceGemini   = iFalse;
    d->monospaceGopher   = iFalse;
    d->boldLinkVisited   = iFalse;
    d->boldLinkDark      = iTrue;
    d->boldLinkLight     = iTrue;
    d->lineWidth         = 38;
    d->lineSpacing       = 1.0f;
    d->tabWidth          = 8;
    d->bigFirstParagraph = iTrue;
    d->justifyParagraph  = iFalse;
    d->quoteIcon         = iTrue;
    d->centerShortDocs   = iTrue;
    d->plainTextWrap     = iTrue;
    d->geminiStyledGopher = iTrue;
    d->imageStyle        = original_ImageStyle;
    d->docThemeDark      = colorfulDark_GmDocumentTheme;
    d->docThemeLight     = white_GmDocumentTheme;
    d->saturation        = 1.0f;
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
