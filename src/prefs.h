#pragma once
#include <the_Foundation/string.h>

#include "gmdocument.h"
#include "ui/color.h"
#include "ui/text.h"

/* User preferences */

iDeclareType(Prefs)

struct Impl_Prefs {
    iBool retainWindowSize;
    float uiScale;    
    int zoomPercent;
    iBool useSystemTheme;
    enum iColorTheme theme;
    iString gopherProxy;
    iString httpProxy;
    iString downloadDir;
    /* Content */
    enum iTextFont font;
    int lineWidth;
    iBool bigFirstParagraph;
    iBool forceLineWrap;
    enum iGmDocumentTheme docThemeDark;
    enum iGmDocumentTheme docThemeLight;
    float saturation;
};

iDeclareTypeConstruction(Prefs)
