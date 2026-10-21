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

#include "../gmdocument.h"
#include "widget.h"
#include <the_Foundation/stream.h>

iDeclareType(Gempub)
iDeclareType(GmDocument)
iDeclareType(GmIdentity)
iDeclareType(GmRequest)
iDeclareType(GmResponse)
iDeclareType(History)

iDeclareWidgetClass(DocumentWidget)
iDeclareObjectConstruction(DocumentWidget)

iDocumentWidget *   duplicate_DocumentWidget        (const iDocumentWidget *);
void                cancelAllRequests_DocumentWidget(iDocumentWidget *);

void    serializeState_DocumentWidget   (const iDocumentWidget *, iStream *outs, iBool withContent);
void    deserializeState_DocumentWidget (iDocumentWidget *, iStream *ins);

iHistory *          history_DocumentWidget          (iDocumentWidget *);
const iGmIdentity * overrideIdentity_DocumentWidget (const iDocumentWidget *); /* NULL if not set */
const iString *     url_DocumentWidget              (const iDocumentWidget *);
const iBlock *      sourceContent_DocumentWidget    (const iDocumentWidget *);
const iGmDocument * document_DocumentWidget         (const iDocumentWidget *);
void                updateDocPrefs_DocumentWidget   (iDocumentWidget *); /* resolves again */
const iString *     bookmarkTitle_DocumentWidget    (const iDocumentWidget *);
const iString *     feedTitle_DocumentWidget        (const iDocumentWidget *);
const iString *     feedEntryUrl_DocumentWidget     (const iDocumentWidget *);
const iGempub *     gempub_DocumentWidget           (const iDocumentWidget *); /* NULL if not a book */
uint32_t            findBookmarkId_DocumentWidget   (const iDocumentWidget *);

const iGmIdentity * identity_DocumentWidget             (const iDocumentWidget *);
int                 generation_DocumentWidget           (const iDocumentWidget *);
iBool               isRequestOngoing_DocumentWidget     (const iDocumentWidget *);
iBool               isFetchingOwnLink_DocumentWidget    (const iDocumentWidget *);
iBool               isSourceTextView_DocumentWidget     (const iDocumentWidget *);
iBool               isIdentityPinned_DocumentWidget     (const iDocumentWidget *);
iBool               isSetIdentityRetained_DocumentWidget(const iDocumentWidget *, const iString *dstUrl);
iBool               isAutoReloading_DocumentWidget      (const iDocumentWidget *);
iBool               isUnseen_DocumentWidget             (const iDocumentWidget *);
iBool               isReader_DocumentWidget             (const iDocumentWidget *);

enum iDocumentWidgetSetUrlFlags {
    useCachedContentIfAvailable_DocumentWidgetSetUrlFlag = iBit(1),
    preventInlining_DocumentWidgetSetUrlFlag             = iBit(2),
    waitForOtherDocumentsToIdle_DocumentWidgetSetUrlFag  = iBit(3),
    disallowCachedDocument_DocumentWidgetSetUrlFlag      = iBit(4),
    overwriteBook_DocumentWidgetSetUrlFlag               = iBit(5),
};

void    setOrigin_DocumentWidget        (iDocumentWidget *, const iDocumentWidget *other, iBool isNewTab);
void    setIdentity_DocumentWidget      (iDocumentWidget *, const iBlock *setIdent); /* overrides normal sign-in */
void    setUrl_DocumentWidget           (iDocumentWidget *, const iString *url);
void    setUrlFlags_DocumentWidget      (iDocumentWidget *, const iString *url, int setUrlFlags,
                                         const iBlock *setIdent);
void    setUrlAndSource_DocumentWidget  (iDocumentWidget *, const iString *url, const iString *mime,
                                         const iBlock *source, float normScrollY);
void    setInitialScroll_DocumentWidget (iDocumentWidget *, float normScrollY); /* set after content received */
void    setRedirectCount_DocumentWidget (iDocumentWidget *, int count);
void    setOpenedExternally_DocumentWidget (iDocumentWidget *, iBool openedExternally);
void    setSource_DocumentWidget        (iDocumentWidget *, const iString *sourceText);
void    setReaderEntry_DocumentWidget   (iDocumentWidget *, const iString *entryUrl);

void    takeRequest_DocumentWidget      (iDocumentWidget *, iGmRequest *finishedRequest); /* ownership given */

void    updateSize_DocumentWidget               (iDocumentWidget *);
iBool   showSidebar_DocumentWidget              (iDocumentWidget *); /* reader or book */

