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

#include <the_Foundation/range.h>
#include <the_Foundation/string.h>

iDeclareType(GmError)
iDeclareType(RegExp)
iDeclareType(RegExpMatch)
iDeclareType(Url)

/* Response status codes. */
enum iGmStatusCode {
    /* clientside status codes */
    clientSide_GmStatusCode = -100,
    invalidRedirect_GmStatusCode,
    schemeChangeRedirect_GmStatusCode,
    tooManyRedirects_GmStatusCode,
    incompleteHeader_GmStatusCode,
    invalidHeader_GmStatusCode,
    unsupportedMimeType_GmStatusCode,
    unsupportedProtocol_GmStatusCode,
    failedToOpenFile_GmStatusCode,
    unknownStatusCode_GmStatusCode,
    invalidLocalResource_GmStatusCode,
    tlsFailure_GmStatusCode,
    tlsServerCertificateExpired_GmStatusCode,
    tlsServerCertificateNotVerified_GmStatusCode,
    proxyCertificateExpired_GmStatusCode,
    proxyCertificateNotVerified_GmStatusCode,
    ansiEscapes_GmStatusCode,
    missingGlyphs_GmStatusCode,

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
iLocalDef iBool isSuccess_GmStatusCode(enum iGmStatusCode code) {
    return category_GmStatusCode(code) == categorySuccess_GmStatusCode;
}

struct Impl_GmError {
    iChar       icon;
    const char *title;
    const char *info;
};

iBool               isDefined_GmError   (enum iGmStatusCode code);
const iGmError *    get_GmError         (enum iGmStatusCode code);

iRegExp *       newGemtextLink_RegExp   (void);

#define GEMINI_DEFAULT_PORT         ((uint16_t) 1965)
#define GEMINI_DEFAULT_PORT_CSTR    "1965"
#define URL_RESERVED_CHARS          ":/?#[]@!$&'()*+,;=" /* RFC 3986 */

struct Impl_Url {
    iRangecc scheme;
    iRangecc host;
    iRangecc port;
    iRangecc path;
    iRangecc query;
    iRangecc fragment;
};

void            init_Url                (iUrl *, const iString *text);
uint16_t        port_Url                (const iUrl *);

iRangecc        urlScheme_String        (const iString *);
iRangecc        urlHost_String          (const iString *);
iRangecc        urlDirectory_String     (const iString *); /* without a file name; ends with slash */
uint16_t        urlPort_String          (const iString *);
iRangecc        urlUser_String          (const iString *);
iRangecc        urlRoot_String          (const iString *);
const iBlock *  urlThemeSeed_String     (const iString *);
const iBlock *  urlPaletteSeed_String   (const iString *);

const iString * absoluteUrl_String      (const iString *, const iString *urlMaybeRelative);
iBool           isLikelyUrl_String      (const iString *);
iBool           isKnownScheme_Rangecc   (iRangecc scheme); /* any URI scheme */
iBool           isKnownUrlScheme_Rangecc(iRangecc scheme); /* URL schemes only */
void            punyEncodeDomain_Rangecc(iRangecc domain, iString *encoded_out);
void            punyEncodeUrlHost_String(iString *absoluteUrl);
void            stripUrlPort_String     (iString *);
void            stripDefaultUrlPort_String(iString *);
const iString * urlFragmentStripped_String(const iString *);
const iString * urlQueryStripped_String (const iString *);
void            urlDecodePath_String    (iString *);
void            urlEncodePath_String    (iString *);
void            urlEncodeQuery_String   (iString *);
iString *       makeFileUrl_String      (const iString *localFilePath);
const char *    makeFileUrl_CStr        (const char *localFilePath);
iString *       localFilePathFromUrl_String(const iString *);
void            urlEncodeSpaces_String  (iString *);
const iString * withSpacesEncoded_String(const iString *);
const iString * withScheme_String       (const iString *, const char *scheme); /* replace URI scheme */
const iString * canonicalUrl_String     (const iString *);
const iString * prettyDataUrl_String    (const iString *, int contentColor);

const char *    mediaType_Path                      (const iString *path);
const char *    mediaTypeFromFileExtension_String   (const iString *);
iRangecc        mediaTypeWithoutParameters_Rangecc  (iRangecc mime);

const iString * findContainerArchive_Path           (const iString *path);


const iString * feedEntryOpenCommand_String (const iString *url, int newTab, int newWindow); /* checks fragment */
