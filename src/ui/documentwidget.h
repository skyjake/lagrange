#pragma once

#include "widget.h"

iDeclareType(History)

iDeclareWidgetClass(DocumentWidget)
iDeclareObjectConstruction(DocumentWidget)

iDocumentWidget *duplicate_DocumentWidget       (iDocumentWidget *);
iHistory *      history_DocumentWidget          (iDocumentWidget *);

const iString * url_DocumentWidget              (const iDocumentWidget *);
iBool           isRequestOngoing_DocumentWidget (const iDocumentWidget *);

void    setUrl_DocumentWidget           (iDocumentWidget *, const iString *url);
void    setUrlFromCache_DocumentWidget  (iDocumentWidget *d, const iString *url, iBool isFromCache);
void    setInitialScroll_DocumentWidget (iDocumentWidget *, int scrollY); /* set after content received */
