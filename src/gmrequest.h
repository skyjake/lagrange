#pragma once

#include <the_Foundation/audience.h>
#include <the_Foundation/tlsrequest.h>

#include "gmutil.h"

iDeclareType(GmResponse)

enum iGmCertFlags {
    available_GmCertFlag      = iBit(1), /* certificate provided by server */
    trusted_GmCertFlag        = iBit(2), /* TOFU status */
    timeVerified_GmCertFlag   = iBit(3), /* has not expired */
    domainVerified_GmCertFlag = iBit(4), /* cert matches server domain */
};

struct Impl_GmResponse {
    enum iGmStatusCode statusCode;
    iString            meta; /* MIME type or other metadata */
    iBlock             body;
    int                certFlags;
    iDate              certValidUntil;
};

iDeclareTypeConstruction(GmResponse)

iGmResponse *       copy_GmResponse             (const iGmResponse *);

/*----------------------------------------------------------------------------------------------*/

iDeclareClass(GmRequest)
iDeclareObjectConstruction(GmRequest)

iDeclareNotifyFunc(GmRequest, Updated)
iDeclareNotifyFunc(GmRequest, Finished)
iDeclareAudienceGetter(GmRequest, updated)
iDeclareAudienceGetter(GmRequest, finished)

void                setUrl_GmRequest            (iGmRequest *, const iString *url);
void                submit_GmRequest            (iGmRequest *);

iBool               isFinished_GmRequest        (const iGmRequest *);
enum iGmStatusCode  status_GmRequest            (const iGmRequest *);
const iString *     meta_GmRequest              (const iGmRequest *);
const iBlock  *     body_GmRequest              (const iGmRequest *);
const iString *     url_GmRequest               (const iGmRequest *);
const iGmResponse * response_GmRequest          (const iGmRequest *);

int                 certFlags_GmRequest         (const iGmRequest *);
iDate               certExpirationDate_GmRequest(const iGmRequest *);
