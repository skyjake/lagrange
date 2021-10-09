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

#include "prefs.h"

#include <the_Foundation/fileinfo.h>

void init_Prefs(iPrefs *d) {
    iForIndices(i, d->strings) {
        init_String(&d->strings[i]);
    }
    d->dialogTab         = 0;
    d->langFrom          = 3; /* fr */
    d->langTo            = 2; /* en */
    d->useSystemTheme    = iTrue;
    d->systemPreferredColorTheme[0] = d->systemPreferredColorTheme[1] = -1;
    d->theme             = dark_ColorTheme;
    d->accent            = cyan_ColorAccent;
    d->customFrame       = iFalse; /* needs some more work to be default */
    d->retainWindowSize  = iTrue;
    d->uiAnimations      = iTrue;
    d->uiScale           = 1.0f; /* default set elsewhere */
    d->zoomPercent       = 100;
    d->sideIcon          = iTrue;
    d->hideToolbarOnScroll = iTrue;
    d->pinSplit          = 1;
    d->returnKey         = default_ReturnKeyBehavior;
    d->hoverLink         = iFalse;
    d->smoothScrolling   = iTrue;
    d->smoothScrollSpeed[keyboard_ScrollType] = 13;
    d->smoothScrollSpeed[mouse_ScrollType]    = 13;
    d->loadImageInsteadOfScrolling = iFalse;
    d->collapsePreOnLoad = iFalse;
    d->openArchiveIndexPages = iTrue;
    d->decodeUserVisibleURLs = iTrue;
    d->maxCacheSize      = 10;
    d->maxMemorySize     = 200;
    setCStr_String(&d->strings[uiFont_PrefsString], "default");
    setCStr_String(&d->strings[headingFont_PrefsString], "default");
    setCStr_String(&d->strings[bodyFont_PrefsString], "default");
    setCStr_String(&d->strings[monospaceFont_PrefsString], "iosevka");
    setCStr_String(&d->strings[monospaceDocumentFont_PrefsString], "iosevka-body");
    d->monospaceGemini   = iFalse;
    d->monospaceGopher   = iFalse;
    d->boldLinkDark      = iTrue;
    d->boldLinkLight     = iTrue;
    d->lineWidth         = 38;
    d->lineSpacing       = 1.0f;
    d->bigFirstParagraph = iTrue;
    d->quoteIcon         = iTrue;
    d->centerShortDocs   = iTrue;
    d->plainTextWrap     = iTrue;
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
    iForIndices(i, d->strings) {
        deinit_String(&d->strings[i]);
    }
}
