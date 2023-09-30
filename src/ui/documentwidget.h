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

iDeclareType(GmDocument)
iDeclareType(GmIdentity)
iDeclareType(GmRequest)
iDeclareType(History)
iDeclareType(Banner)
iDeclareType(ScrollWidget)

iDeclareWidgetClass(DocumentWidget)
iDeclareObjectConstruction(DocumentWidget)

iDocumentWidget *   duplicate_DocumentWidget        (const iDocumentWidget *);
void                cancelAllRequests_DocumentWidget(iDocumentWidget *);

void    serializeState_DocumentWidget   (const iDocumentWidget *, iStream *outs, iBool withContent);
void    deserializeState_DocumentWidget (iDocumentWidget *, iStream *ins);

iHistory *          history_DocumentWidget          (iDocumentWidget *);
iWidget *           footerButtons_DocumentWidget    (const iDocumentWidget *);
iScrollWidget *     scrollBar_DocumentWidget        (const iDocumentWidget *);
const iString *     url_DocumentWidget              (const iDocumentWidget *);
const iBlock *      sourceContent_DocumentWidget    (const iDocumentWidget *);
iTime               sourceTime_DocumentWidget       (const iDocumentWidget *);
const iGmDocument * document_DocumentWidget         (const iDocumentWidget *);
const iString *     bookmarkTitle_DocumentWidget    (const iDocumentWidget *);
const iString *     feedTitle_DocumentWidget        (const iDocumentWidget *);
uint32_t            findBookmarkId_DocumentWidget   (const iDocumentWidget *);

int                 documentWidth_DocumentWidget        (const iDocumentWidget *);
int                 footerHeight_DocumentWidget         (const iDocumentWidget *);
int                 phoneToolbarHeight_DocumentWidget   (const iDocumentWidget *);
int                 phoneBottomNavbarHeight_DocumentWidget(const iDocumentWidget *);
iRangecc            selectionMark_DocumentWidget        (const iDocumentWidget *);
const iGmIdentity * identity_DocumentWidget             (const iDocumentWidget *);
int                 generation_DocumentWidget           (const iDocumentWidget *);
iBool               isRequestOngoing_DocumentWidget     (const iDocumentWidget *);
iBool               isPrerenderingAllowed_DocumentWidget(const iDocumentWidget *);
iBool               isSourceTextView_DocumentWidget     (const iDocumentWidget *);
iBool               isIdentityPinned_DocumentWidget     (const iDocumentWidget *);
iBool               isSetIdentityRetained_DocumentWidget(const iDocumentWidget *, const iString *dstUrl);
iBool               isAutoReloading_DocumentWidget      (const iDocumentWidget *);
iBool               isHoverAllowed_DocumentWidget       (const iDocumentWidget *);
iBool               noHoverWhileScrolling_DocumentWidget(const iDocumentWidget *);
iBool               isShowingLinkNumbers_DocumentWidget (const iDocumentWidget *);
iBool               isBlank_DocumentWidget              (const iDocumentWidget *);
iMediaRequest *     findMediaRequest_DocumentWidget     (const iDocumentWidget *, iGmLinkId linkId);

size_t              ordinalBase_DocumentWidget          (const iDocumentWidget *);
iChar               linkOrdinalChar_DocumentWidget      (const iDocumentWidget *, size_t ord);

enum iWheelSwipeState {
    none_WheelSwipeState,
    direct_WheelSwipeState,
};

enum iWheelSwipeState   wheelSwipeState_DocumentWidget  (const iDocumentWidget *);

enum iDocumentWidgetSetUrlFlags {
    useCachedContentIfAvailable_DocumentWidgetSetUrlFlag = iBit(1),
    preventInlining_DocumentWidgetSetUrlFlag             = iBit(2),
    waitForOtherDocumentsToIdle_DocumentWidgetSetUrlFag  = iBit(3),
    disallowCachedDocument_DocumentWidgetSetUrlFlag      = iBit(4),
};

void    setOrigin_DocumentWidget        (iDocumentWidget *, const iDocumentWidget *other);
void    setIdentity_DocumentWidget      (iDocumentWidget *, const iBlock *setIdent); /* overrides normal sign-in */
void    setUrl_DocumentWidget           (iDocumentWidget *, const iString *url);
void    setUrlFlags_DocumentWidget      (iDocumentWidget *, const iString *url, int setUrlFlags,
                                         const iBlock *setIdent);
void    setUrlAndSource_DocumentWidget  (iDocumentWidget *, const iString *url, const iString *mime,
                                         const iBlock *source, float normScrollY);
void    setInitialScroll_DocumentWidget (iDocumentWidget *, float normScrollY); /* set after content received */
void    setRedirectCount_DocumentWidget (iDocumentWidget *, int count);
void    setSource_DocumentWidget        (iDocumentWidget *, const iString *sourceText);

void    takeRequest_DocumentWidget      (iDocumentWidget *, iGmRequest *finishedRequest); /* ownership given */

void    documentRunsInvalidated_DocumentWidget  (iDocumentWidget *);
void    updateSize_DocumentWidget               (iDocumentWidget *);
void    updateHoverLinkInfo_DocumentWidget      (iDocumentWidget *, uint16_t linkId);
void    scrollBegan_DocumentWidget              (iAnyObject *, int, uint32_t); /* SmoothScroll callback */
void    aboutToScrollView_DocumentWidget        (iDocumentWidget *, int scrollMax);
void    didScrollView_DocumentWidget            (iDocumentWidget *);

void    animate_DocumentWidget                  (iAny *); /* ticker */
void    refreshWhileScrolling_DocumentWidget    (iAny *); /* ticker */
