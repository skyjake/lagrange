#pragma once

#include <the_Foundation/range.h>
#include <the_Foundation/string.h>

iDeclareType(GmError)
iDeclareType(Url)

/* Response status codes. */
enum iGmStatusCode {
    /* clientside status codes */
    clientSide_GmStatusCode = -100,
    invalidRedirect_GmStatusCode,
    invalidHeader_GmStatusCode,
    unsupportedMimeType_GmStatusCode,
    failedToOpenFile_GmStatusCode,
    unknownStatusCode_GmStatusCode,
    invalidLocalResource_GmStatusCode,
    none_GmStatusCode                      = 0,
    /* general status code categories */
    categoryInput_GmStatusCode             = 1,
    categorySuccess_GmStatusCode           = 2,
    categoryRedirect_GmStatusCode          = 3,
    categoryTemporaryFailure_GmStatusCode  = 4,
    categoryPermanentFailure_GmStatusCode  = 5,
    categoryClientCertificate_GmStatus     = 6,
    /* detailed status codes */
    input_GmStatusCode                     = 10,
    sensitiveInput_GmStatusCode            = 11,
    success_GmStatusCode                   = 20,
    redirectTemporary_GmStatusCode         = 30,
    redirectPermanent_GmStatusCode         = 31,
    temporaryFailure_GmStatusCode          = 40,
    serverUnavailable_GmStatusCode         = 41,
    cgiError_GmStatusCode                  = 42,
    proxyError_GmStatusCode                = 43,
    slowDown_GmStatusCode                  = 44,
    permanentFailure_GmStatusCode          = 50,
    notFound_GmStatusCode                  = 51,
    gone_GmStatusCode                      = 52,
    proxyRequestRefused_GmStatusCode       = 53,
    badRequest_GmStatusCode                = 59,
    clientCertificateRequired_GmStatusCode = 60,
    certificateNotAuthorized_GmStatusCode  = 61,
    certificateNotValid_GmStatusCode       = 62,
};

iLocalDef enum iGmStatusCode category_GmStatusCode(enum iGmStatusCode code) {
    if (code < 0) return 0;
    if (code < 10) return code;
    return code / 10;
}

struct Impl_GmError {
    iChar       icon;
    const char *title;
    const char *info;
};

iBool               isDefined_GmError   (enum iGmStatusCode code);
const iGmError *    get_GmError         (enum iGmStatusCode code);

struct Impl_Url {
    iRangecc protocol;
    iRangecc host;
    iRangecc port;
    iRangecc path;
    iRangecc query;
};

void            init_Url                (iUrl *, const iString *text);

iRangecc        urlHost_String          (const iString *);
const iString * absoluteUrl_String      (const iString *, const iString *urlMaybeRelative);
iString *       makeFileUrl_String      (const iString *localFilePath);
void            urlEncodeSpaces_String  (iString *);
