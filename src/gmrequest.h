#pragma once

#include <the_Foundation/audience.h>
#include <the_Foundation/tlsrequest.h>

#include "gmutil.h"

iDeclareClass(GmRequest)
iDeclareObjectConstruction(GmRequest)

iDeclareNotifyFunc(GmRequest, Updated)
iDeclareNotifyFunc(GmRequest, Finished)
iDeclareAudienceGetter(GmRequest, updated)
iDeclareAudienceGetter(GmRequest, finished)

void    setUrl_GmRequest    (iGmRequest *, const iString *url);
void    submit_GmRequest    (iGmRequest *);

enum iGmRequestCertFlags {
    available_GmRequestCertFlag      = iBit(1), /* certificate provided by server */
    trusted_GmRequestCertFlag        = iBit(2), /* TOFU status */
    timeVerified_GmRequestCertFlag   = iBit(3), /* has not expired */
    domainVerified_GmRequestCertFlag = iBit(4), /* cert matches server domain */
};

iBool               isFinished_GmRequest    (const iGmRequest *);
enum iGmStatusCode  status_GmRequest        (const iGmRequest *);
const iString *     meta_GmRequest          (const iGmRequest *);
const iBlock  *     body_GmRequest          (const iGmRequest *);
const iString *     url_GmRequest           (const iGmRequest *);

int                 certFlags_GmRequest         (const iGmRequest *);
iDate               certExpirationDate_GmRequest(const iGmRequest *);
