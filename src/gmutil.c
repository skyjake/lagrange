#include "gmutil.h"
#include "gemini.h"

#include <the_Foundation/regexp.h>
#include <the_Foundation/object.h>

void init_Url(iUrl *d, const iString *text) {
    iRegExp *absPat =
        new_RegExp("(.+)://([^/:?]*)(:[0-9]+)?([^?]*)(\\?.*)?", caseInsensitive_RegExpOption);
    iRegExpMatch m;
    if (matchString_RegExp(absPat, text, &m)) {
        d->protocol = capturedRange_RegExpMatch(&m, 1);
        d->host     = capturedRange_RegExpMatch(&m, 2);
        d->port     = capturedRange_RegExpMatch(&m, 3);
        if (!isEmpty_Range(&d->port)) {
            /* Don't include the colon. */
            d->port.start++;
        }
        d->path  = capturedRange_RegExpMatch(&m, 4);
        d->query = capturedRange_RegExpMatch(&m, 5);
    }
    else {
        /* Must be a relative path. */
        iZap(*d);
        iRegExp *relPat = new_RegExp("([^?]*)(\\?.*)?", 0);
        if (matchString_RegExp(relPat, text, &m)) {
            d->path  = capturedRange_RegExpMatch(&m, 1);
            d->query = capturedRange_RegExpMatch(&m, 2);
        }
        iRelease(relPat);
    }
    iRelease(absPat);
}

void urlEncodeSpaces_String(iString *d) {
    for (;;) {
        const size_t pos = indexOfCStr_String(d, " ");
        if (pos == iInvalidPos) break;
        remove_Block(&d->chars, pos, 1);
        insertData_Block(&d->chars, pos, "%20", 3);
    }
}

const iGmError *get_GmError(enum iGmStatusCode code) {
    static const iGmError none = { 0, "", "" };
    static const struct {
        enum iGmStatusCode code;
        iGmError           err;
    } errors[] = {
        { failedToOpenFile_GmStatusCode,
          { 0x1f4c1, /* file folder */
            "Failed to Open File",
            "The requested file does not exist or is inaccessible. "
            "Please check the file path." } },
        { unsupportedMimeType_GmStatusCode,
          { 0x1f47d, /* alien */
            "Unsupported MIME Type",
            "The received content is in an unsupported format and cannot be viewed with "
            "this application." } },
        { invalidHeader_GmStatusCode,
          { 0x1f4a9, /* pile of poo */
            "Invalid Header",
            "The received header did not conform to the Gemini specification. "
            "Perhaps the server is malfunctioning or you tried to contact a "
            "non-Gemini server." } },
        { invalidRedirect_GmStatusCode,
          { 0, /*  */
            "Invalid Redirect",
            "The server responded with a redirect but did not provide a valid destination URL. "
            "Perhaps the server is malfunctioning." } },
        { temporaryFailure_GmStatusCode,
          { 0x1f50c, /* electric plug */
            "Temporary Failure",
            "The request has failed, but may succeed if you try again in the future." } },
        { serverUnavailable_GmStatusCode,
          { 0x1f525, /* fire */
            "Server Unavailable",
            "The server is unavailable due to overload or maintenance. Check back later." } },
        { cgiError_GmStatusCode,
          { 0x1f4a5, /* collision */
            "CGI Error",
            "Failure during dynamic content generation on the server. This may be due "
            "to buggy serverside software." } },
        { proxyError_GmStatusCode,
          { 0x1f310, /* globe */
            "Proxy Error",
            "A proxy request failed because the server was unable to successfully "
            "complete a transaction with the remote host. Perhaps there are difficulties "
            "with network connectivity." } },
        { slowDown_GmStatusCode,
          { 0x1f40c, /* snail */
            "Slow Down",
            "The server is rate limiting requests. Please wait..." } },
        { permanentFailure_GmStatusCode,
          { 0x1f6ab, /* no entry */
            "Permanent Failure",
            "Your request has failed and will fail in the future as well if repeated." } },
        { notFound_GmStatusCode,
          { 0x1f50d, /* magnifying glass */
            "Not Found",
            "The requested resource could not be found at this time." } },
        { gone_GmStatusCode,
          { 0x1f47b, /* ghost */
            "Gone",
            "The resource requested is no longer available and will not be available again." } },
        { proxyRequestRefused_GmStatusCode,
          { 0x1f6c2, /* passport control */
            "Proxy Request Refused",
            "The request was for a resource at a domain not served by the server and the "
            "server does not accept proxy requests." } },
        { badRequest_GmStatusCode,
          { 0x1f44e, /* thumbs down */
            "Bad Request",
            "The server was unable to parse your request, presumably due to the "
            "request being malformed. Likely a bug in Lagrange." } },
        { clientCertificateRequired_GmStatusCode,
          { 0x1f511, /* key */
            "Certificate Required",
            "Access to the requested resource requires identification via "
            "a client certificate." } },
        { certificateNotAuthorized_GmStatusCode,
          { 0x1f512, /* lock */
            "Certificate Not Authorized",
            "The provided client certificate is valid but is not authorized for accessing "
            "the requested resource. " } },
        { certificateNotValid_GmStatusCode,
          { 0x1f6a8, /* revolving light */
            "Invalid Certificate",
            "The provided client certificate is expired or invalid." } },
    };
    iForIndices(i, errors) {
        if (errors[i].code == code) {
            return &errors[i].err;
        }
    }
    return &none;
}
