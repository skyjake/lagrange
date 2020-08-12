#pragma once

#include "widget.h"
#include <the_Foundation/stream.h>

iDeclareType(GmDocument)
iDeclareType(History)

iDeclareWidgetClass(DocumentWidget)
iDeclareObjectConstruction(DocumentWidget)

void    serializeState_DocumentWidget   (const iDocumentWidget *, iStream *outs);
void    deserializeState_DocumentWidget (iDocumentWidget *, iStream *ins);

iDocumentWidget *   duplicate_DocumentWidget        (const iDocumentWidget *);
iHistory *          history_DocumentWidget          (iDocumentWidget *);

const iString *     url_DocumentWidget              (const iDocumentWidget *);
iBool               isRequestOngoing_DocumentWidget (const iDocumentWidget *);
const iGmDocument * document_DocumentWidget         (const iDocumentWidget *);

void    setUrl_DocumentWidget           (iDocumentWidget *, const iString *url);
void    setUrlFromCache_DocumentWidget  (iDocumentWidget *, const iString *url, iBool isFromCache);
void    setInitialScroll_DocumentWidget (iDocumentWidget *, int scrollY); /* set after content received */

void    updateSize_DocumentWidget       (iDocumentWidget *);
