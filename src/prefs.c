#include "prefs.h"

void init_Prefs(iPrefs *d) {
    d->theme             = dark_ColorTheme;
    d->useSystemTheme    = iTrue;
    d->retainWindowSize  = iTrue;
    d->zoomPercent       = 100;
    d->forceLineWrap     = iFalse;
    d->font              = nunito_TextFont;
    d->headingFont       = nunito_TextFont;
    d->lineWidth         = 40;
    d->bigFirstParagraph = iTrue;
    d->sideIcon          = iTrue;
    d->hoverOutline      = iFalse;
    d->docThemeDark      = colorfulDark_GmDocumentTheme;
    d->docThemeLight     = white_GmDocumentTheme;
    d->saturation        = 1.0f;
    init_String(&d->gopherProxy);
    init_String(&d->httpProxy);
    init_String(&d->downloadDir);
}

void deinit_Prefs(iPrefs *d) {
    deinit_String(&d->gopherProxy);
    deinit_String(&d->httpProxy);
    deinit_String(&d->downloadDir);
}
