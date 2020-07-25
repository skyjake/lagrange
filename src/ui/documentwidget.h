#pragma once

#include "widget.h"

iDeclareWidgetClass(DocumentWidget)
iDeclareObjectConstruction(DocumentWidget)

void    setUrl_DocumentWidget           (iDocumentWidget *, const iString *url);
iBool   isRequestOngoing_DocumentWidget (const iDocumentWidget *);
