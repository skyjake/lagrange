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

#include <the_Foundation/audience.h>
#include <the_Foundation/tlsrequest.h>

#include "gmutil.h"

iDeclareType(GmCerts)
iDeclareType(GmIdentity)
iDeclareType(GmResponse)

enum iGmCertFlag {
    available_GmCertFlag         = iBit(1), /* certificate provided by server */
    trusted_GmCertFlag           = iBit(2), /* TOFU status */
    timeVerified_GmCertFlag      = iBit(3), /* has not expired */
    domainVerified_GmCertFlag    = iBit(4), /* cert matches server domain */
    haveFingerprint_GmCertFlag   = iBit(5),
    authorityVerified_GmCertFlag = iBit(6),
};

struct Impl_GmResponse {
    enum iGmStatusCode statusCode;
    iString            meta; /* MIME type or other metadata */
    iBlock             body;
    int                certFlags;
    iBlock             certFingerprint;
    iDate              certValidUntil;
    iString            certSubject;
    iTime              when;
};

iDeclareTypeConstruction(GmResponse)
iDeclareTypeSerialization(GmResponse)

iGmResponse *       copy_GmResponse             (const iGmResponse *);

/*----------------------------------------------------------------------------------------------*/

iDeclareClass(GmRequest)
iDeclareObjectConstructionArgs(GmRequest, iGmCerts *)

iDeclareNotifyFunc(GmRequest, Updated)
iDeclareNotifyFunc(GmRequest, Finished)
iDeclareAudienceGetter(GmRequest, updated)
iDeclareAudienceGetter(GmRequest, finished)
    
typedef void (*iGmRequestProgressFunc)(iGmRequest *, size_t current, size_t total);

void                enableFilters_GmRequest     (iGmRequest *, iBool enable);
void                setUrl_GmRequest            (iGmRequest *, const iString *url);
void                setIdentity_GmRequest       (iGmRequest *, const iGmIdentity *id);
void                setUploadData_GmRequest     (iGmRequest *, const iString *mime,
                                                 const iBlock *payload, const iString *token);
void                setSendProgressFunc_GmRequest(iGmRequest *, iGmRequestProgressFunc func);
void                submit_GmRequest            (iGmRequest *);
void                cancel_GmRequest            (iGmRequest *);

iGmResponse *       lockResponse_GmRequest      (iGmRequest *);
void                unlockResponse_GmRequest    (iGmRequest *);

uint32_t            id_GmRequest                (const iGmRequest *); /* unique ID */
iBool               isFinished_GmRequest        (const iGmRequest *);
enum iGmStatusCode  status_GmRequest            (const iGmRequest *);
const iString *     meta_GmRequest              (const iGmRequest *);
const iBlock  *     body_GmRequest              (const iGmRequest *);
size_t              bodySize_GmRequest          (const iGmRequest *);
const iString *     url_GmRequest               (const iGmRequest *);

int                 certFlags_GmRequest         (const iGmRequest *);
iDate               certExpirationDate_GmRequest(const iGmRequest *);
