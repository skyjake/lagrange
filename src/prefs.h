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

#include <the_Foundation/string.h>

#include "gmdocument.h"
#include "ui/color.h"
#include "ui/text.h"

/* User preferences */

iDeclareType(Prefs)

struct Impl_Prefs {
    /* UI state */
    int              dialogTab;
    /* Window */
    iBool            useSystemTheme;
    enum iColorTheme theme;
    iBool            customFrame; /* when LAGRANGE_CUSTOM_FRAME is defined */
    iBool            retainWindowSize;
    float            uiScale;
    int              zoomPercent;
    iBool            sideIcon;
    /* Behavior */
    iString          downloadDir;
    iBool            hoverLink;
    iBool            smoothScrolling;
    iBool            loadImageInsteadOfScrolling;
    /* Network */
    iBool            decodeUserVisibleURLs;
    int              maxCacheSize; /* MB */
    iString          geminiProxy;
    iString          gopherProxy;
    iString          httpProxy;
    /* Style */
    enum iTextFont   font;
    enum iTextFont   headingFont;
    iBool            monospaceGemini;
    iBool            monospaceGopher;
    int              lineWidth;
    iBool            bigFirstParagraph;
    iBool            quoteIcon;
    iBool            centerShortDocs;
    /* Colors */
    enum iGmDocumentTheme docThemeDark;
    enum iGmDocumentTheme docThemeLight;
    float                 saturation;
};

iDeclareTypeConstruction(Prefs)
