#pragma once

#include "widget.h"

iDeclareWidgetClass(DocumentWidget)
iDeclareObjectConstruction(DocumentWidget)

void    setUrl_DocumentWidget           (iDocumentWidget *, const iString *url);
void    setInitialScroll_DocumentWidget (iDocumentWidget *, int scrollY); /* set after content received */
iBool   isRequestOngoing_DocumentWidget (const iDocumentWidget *);
