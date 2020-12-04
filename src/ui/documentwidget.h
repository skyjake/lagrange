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
const iString *     bookmarkTitle_DocumentWidget    (const iDocumentWidget *);
const iString *     feedTitle_DocumentWidget        (const iDocumentWidget *);

void    setUrl_DocumentWidget           (iDocumentWidget *, const iString *url);
void    setUrlFromCache_DocumentWidget  (iDocumentWidget *, const iString *url, iBool isFromCache);
void    setInitialScroll_DocumentWidget (iDocumentWidget *, float normScrollY); /* set after content received */
void    setRedirectCount_DocumentWidget (iDocumentWidget *, int count);

void    updateSize_DocumentWidget       (iDocumentWidget *);
