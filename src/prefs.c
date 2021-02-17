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

void init_Prefs(iPrefs *d) {
    d->dialogTab         = 0;
    d->useSystemTheme    = iTrue;
    d->theme             = dark_ColorTheme;
    d->customFrame       = iFalse; /* needs some more work to be default */
    d->retainWindowSize  = iTrue;
    d->uiScale           = 1.0f; /* default set elsewhere */
    d->zoomPercent       = 100;
    d->sideIcon          = iTrue;
    d->hoverLink         = iTrue;
    d->smoothScrolling   = iTrue;
    d->loadImageInsteadOfScrolling = iFalse;
    d->decodeUserVisibleURLs = iTrue;
    d->maxCacheSize      = 10;
    d->font              = nunito_TextFont;
    d->headingFont       = nunito_TextFont;
    d->monospaceGemini   = iFalse;
    d->monospaceGopher   = iFalse;
    d->lineWidth         = 38;
    d->bigFirstParagraph = iTrue;
    d->quoteIcon         = iTrue;
    d->centerShortDocs   = iTrue;
    d->docThemeDark      = colorfulDark_GmDocumentTheme;
    d->docThemeLight     = white_GmDocumentTheme;
    d->saturation        = 1.0f;
    init_String(&d->geminiProxy);
    init_String(&d->gopherProxy);
    init_String(&d->httpProxy);
    init_String(&d->downloadDir);
    init_String(&d->searchUrl);
}

void deinit_Prefs(iPrefs *d) {
    deinit_String(&d->searchUrl);
    deinit_String(&d->geminiProxy);
    deinit_String(&d->gopherProxy);
    deinit_String(&d->httpProxy);
    deinit_String(&d->downloadDir);
}
