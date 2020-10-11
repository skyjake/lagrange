#pragma once
#include <the_Foundation/string.h>

#include "gmdocument.h"
#include "ui/color.h"
#include "ui/text.h"

/* User preferences */

iDeclareType(Prefs)

struct Impl_Prefs {
    int dialogTab;
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
    enum iTextFont headingFont;
    int lineWidth;
    iBool bigFirstParagraph;
    iBool forceLineWrap;
    iBool sideIcon;
    iBool hoverOutline;
    enum iGmDocumentTheme docThemeDark;
    enum iGmDocumentTheme docThemeLight;
    float saturation;
};

iDeclareTypeConstruction(Prefs)
