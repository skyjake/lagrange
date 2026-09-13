/* Copyright 2026 Jaakko Keränen <jaakko.keranen@iki.fi>

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
#include <lagrange/gmcerts.h>
#include <the_Foundation/block.h>
#include <the_Foundation/string.h>
#include <the_Foundation/time.h>

iDeclareType(DocumentWidget)
iDeclareType(Gempub)
iDeclareType(GmRequest)
iDeclareType(GmResponse)

enum iRequestState {
    blank_RequestState,
    fetching_RequestState,
    receivedPartialResponse_RequestState,
    ready_RequestState,
};

enum iDocumentFetchFlag {
    pendingRedirect_DocumentFetchFlag = iBit(1), /* a redirect has been issued */
    waitForIdle_DocumentFetchFlag     = iBit(2), /* sequential loading; wait for previous
                                                    tabs to finish their requests */
    goBackOnStop_DocumentFetchFlag    = iBit(3), /* cancelling navigates back */
    fromCache_DocumentFetchFlag       = iBit(4), /* don't write anything to cache */
    preventInlining_DocumentFetchFlag = iBit(5), /* response can't become inline media */
};

iDeclareType(DocumentFetch)
iDeclareTypeConstruction(DocumentFetch)

struct Impl_DocumentFetch {
    iDocumentWidget *  owner;
    int                flags; /* see enum iDocumentFetchFlag */
    enum iRequestState state;
    iGmRequest *       request;
    iGmLinkId          requestLinkId; /* ID of the link that initiated the current request */
    uint32_t           lastRequestUpdateAt;
    int                certFlags;
    iBlock *           certFingerprint;     /* public key SHA-256 */
    iBlock *           certFullFingerprint; /* full certificate SHA-256 */
    iDate              certExpiry;
    iString *          certSubject;
    int                redirectCount;
    enum iGmStatusCode sourceStatus;
    iString            sourceHeader;
    iString            sourceMime;
    iBlock             sourceContent; /* original content as received, before filtering */
    iTime              sourceTime;
    iGempub *          sourceGempub; /* NULL doesn't mean there is no gempub */
};

void    setOwner_DocumentFetch          (iDocumentFetch *, iDocumentWidget *owner);

const char *humanReadableStatusCode_DocumentFetch(enum iGmStatusCode);

iBool   fetch_DocumentFetch             (iDocumentFetch *);
iBool   cancel_DocumentFetch            (iDocumentFetch *, iBool postBack);
iBool   tryWaiting_DocumentFetch        (iDocumentFetch *);
void    take_DocumentFetch              (iDocumentFetch *, iGmRequest *finishedRequest);
void    checkResponse_DocumentFetch     (iDocumentFetch *);
void    postProcessContent_DocumentFetch(iDocumentFetch *, iBool isCached);
void    updateProgress_DocumentFetch    (const iDocumentFetch *);
void    updateTrust_DocumentFetch       (iDocumentFetch *, const iGmResponse *);
void    cleanupRedirected_DocumentFetch (iDocumentFetch *);
void    restoreAddressBarAndHistory_DocumentFetch(iDocumentFetch *, const iString *fetchedUrl);

void    requestUpdated_DocumentFetch (iAnyObject *); /* GmRequest audience */
void    requestFinished_DocumentFetch(iAnyObject *);

iBool           isRequestOngoing_DocumentFetch  (const iDocumentFetch *);
iBool           isFetchingOwnLink_DocumentFetch (const iDocumentFetch *);
const iBlock *  sourceContent_DocumentFetch     (const iDocumentFetch *);
iTime           sourceTime_DocumentFetch        (const iDocumentFetch *);
