#pragma once

#include <the_Foundation/audience.h>
#include <the_Foundation/object.h>

#include "gemini.h"

iDeclareClass(GmRequest)
iDeclareObjectConstruction(GmRequest)

iDeclareNotifyFunc(GmRequest, Updated)
iDeclareNotifyFunc(GmRequest, Finished)
iDeclareAudienceGetter(GmRequest, updated)
iDeclareAudienceGetter(GmRequest, finished)

void    setUrl_GmRequest    (iGmRequest *, const iString *url);
void    submit_GmRequest    (iGmRequest *);

enum iGmRequestCertification {
    notApplicable_GmRequestCertification,
    invalid_GmRequestCertification,
    valid_GmRequestCertification,
    expired_GmRequestCertification,
};

iBool               isFinished_GmRequest    (const iGmRequest *);
enum iGmStatusCode  status_GmRequest        (const iGmRequest *);
const iString *     meta_GmRequest          (const iGmRequest *);
const iBlock  *     body_GmRequest          (const iGmRequest *);
const iString *     url_GmRequest           (const iGmRequest *);
