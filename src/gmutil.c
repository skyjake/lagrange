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

#include "gmutil.h"

#include <the_Foundation/regexp.h>
#include <the_Foundation/object.h>
#include <the_Foundation/path.h>

void init_Url(iUrl *d, const iString *text) {
    static iRegExp *absoluteUrlPattern_;
    static iRegExp *relativeUrlPattern_;
    if (!absoluteUrlPattern_) {
        absoluteUrlPattern_ = new_RegExp("([a-z]+:)?(//[^/:?]*)(:[0-9]+)?([^?]*)(\\?.*)?",
                                         caseInsensitive_RegExpOption);
    }
    iRegExpMatch m;
    init_RegExpMatch(&m);
    if (matchString_RegExp(absoluteUrlPattern_, text, &m)) {
        d->protocol = capturedRange_RegExpMatch(&m, 1);
        d->host     = capturedRange_RegExpMatch(&m, 2);
        if (!isEmpty_Range(&d->host)) {
            d->host.start += 2; /* skip the double slash */
        }
        d->port = capturedRange_RegExpMatch(&m, 3);
        if (!isEmpty_Range(&d->port)) {
            d->port.start++; /* omit the colon */
        }
        d->path  = capturedRange_RegExpMatch(&m, 4);
        d->query = capturedRange_RegExpMatch(&m, 5);
    }
    else {
        /* Must be a relative path. */
        iZap(*d);
        if (!relativeUrlPattern_) {
            relativeUrlPattern_ = new_RegExp("([a-z]+:)?([^?]*)(\\?.*)?", 0);
        }
        if (matchString_RegExp(relativeUrlPattern_, text, &m)) {
            d->protocol = capturedRange_RegExpMatch(&m, 1);
            d->path     = capturedRange_RegExpMatch(&m, 2);
            d->query    = capturedRange_RegExpMatch(&m, 3);
        }
    }
    if (!isEmpty_Range(&d->protocol)) {
        d->protocol.end--; /* omit the colon */
    }
}

static iRangecc dirPath_(iRangecc path) {
    const size_t pos = lastIndexOfCStr_Rangecc(&path, "/");
    if (pos == iInvalidPos) return path;
    return (iRangecc){ path.start, path.start + pos };
}

iLocalDef iBool isDef_(iRangecc cc) {
    return !isEmpty_Range(&cc);
}

static iRangecc prevPathSeg_(const char *end, const char *start) {
    iRangecc seg = { end, end };
    do {
        seg.start--;
    } while (*seg.start != '/' && seg.start != start);
    return seg;
}

void cleanUrlPath_String(iString *d) {
    iString clean;
    init_String(&clean);
    iUrl parts;
    init_Url(&parts, d);
    iRangecc seg = iNullRange;
    while (nextSplit_Rangecc(&parts.path, "/", &seg)) {
        if (equal_Rangecc(&seg, "..")) {
            /* Back up one segment. */
            iRangecc last = prevPathSeg_(constEnd_String(&clean), constBegin_String(&clean));
            truncate_Block(&clean.chars, last.start - constBegin_String(&clean));
        }
        else if (equal_Rangecc(&seg, ".")) {
            /* Skip it. */
        }
        else {
            appendCStr_String(&clean, "/");
            appendRange_String(&clean, seg);
        }
    }
    if (endsWith_Rangecc(&parts.path, "/")) {
        appendCStr_String(&clean, "/");
    }
    /* Replace with the new path. */
    if (cmpCStrNSc_Rangecc(&parts.path, cstr_String(&clean), size_String(&clean), &iCaseSensitive)) {
        const size_t pos = parts.path.start - constBegin_String(d);
        remove_Block(&d->chars, pos, size_Range(&parts.path));
        insertData_Block(&d->chars, pos, cstr_String(&clean), size_String(&clean));
    }
    deinit_String(&clean);
}

iRangecc urlProtocol_String(const iString *d) {
    iUrl url;
    init_Url(&url, d);
    return url.protocol;
}

iRangecc urlHost_String(const iString *d) {
    iUrl url;
    init_Url(&url, d);
    return url.host;
}

const iString *absoluteUrl_String(const iString *d, const iString *urlMaybeRelative) {
    iUrl orig;
    iUrl rel;
    init_Url(&orig, d);
    init_Url(&rel, urlMaybeRelative);
    if (equalCase_Rangecc(&rel.protocol, "data") || equalCase_Rangecc(&rel.protocol, "about")) {
        /* Special case, the contents should be left unparsed. */
        return urlMaybeRelative;
    }
    const iBool isRelative = !isDef_(rel.host);
    iRangecc protocol = range_CStr("gemini");
    if (isDef_(rel.protocol)) {
        protocol = rel.protocol;
    }
    else if (isRelative && isDef_(orig.protocol)) {
        protocol = orig.protocol;
    }
    iString *absolute = collectNew_String();
    appendRange_String(absolute, protocol);
    appendCStr_String(absolute, "://"); {
        const iUrl *selHost = isDef_(rel.host) ? &rel : &orig;
        appendRange_String(absolute, selHost->host);
        if (!isEmpty_Range(&selHost->port)) {
            appendCStr_String(absolute, ":");
            appendRange_String(absolute, selHost->port);
        }
    }
    if (isDef_(rel.protocol) || isDef_(rel.host) || startsWith_Rangecc(&rel.path, "/")) {
        appendRange_String(absolute, rel.path); /* absolute path */
    }
    else {
        if (!endsWith_Rangecc(&orig.path, "/")) {
            /* Referencing a file. */
            appendRange_String(absolute, dirPath_(orig.path));
        }
        else {
            /* Referencing a directory. */
            appendRange_String(absolute, orig.path);
        }
        if (!endsWith_String(absolute, "/")) {
            appendCStr_String(absolute, "/");
        }
        appendRange_String(absolute, rel.path);
    }
    appendRange_String(absolute, rel.query);
    cleanUrlPath_String(absolute);
    return absolute;
}

iString *makeFileUrl_String(const iString *localFilePath) {
    iString *url = cleaned_Path(localFilePath);
    replace_Block(&url->chars, '\\', '/'); /* in case it's a Windows path */
    prependCStr_String(url, "file://");
    return url;
}

void urlEncodeSpaces_String(iString *d) {
    for (;;) {
        const size_t pos = indexOfCStr_String(d, " ");
        if (pos == iInvalidPos) break;
        remove_Block(&d->chars, pos, 1);
        insertData_Block(&d->chars, pos, "%20", 3);
    }
}

static const struct {
    enum iGmStatusCode code;
    iGmError           err;
} errors_[] = {
    { unknownStatusCode_GmStatusCode, /* keep this as the first one (fallback return value) */
      { 0x1f4ab, /* dizzy */
        "Unknown Status Code",
        "The server responded with a status code that is not specified in the Gemini "
        "protocol as known to this client. Maybe the server is from the future? Or "
        "just malfunctioning."} },
    { failedToOpenFile_GmStatusCode,
      { 0x1f4c1, /* file folder */
        "Failed to Open File",
        "The requested file does not exist or is inaccessible. "
        "Please check the file path." } },
    { invalidLocalResource_GmStatusCode,
      { 0,
        "Invalid Resource",
        "The requested resource does not exist." } },
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
      { 0x27a0, /* dashed arrow */
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
        "request being malformed." } },
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

iBool isDefined_GmError(enum iGmStatusCode code) {
    iForIndices(i, errors_) {
        if (errors_[i].code == code) {
            return iTrue;
        }
    }
    return iFalse;
}

const iGmError *get_GmError(enum iGmStatusCode code) {
    static const iGmError none = { 0, "", "" };
    if (code == 0) {
        return &none;
    }
    iForIndices(i, errors_) {
        if (errors_[i].code == code) {
            return &errors_[i].err;
        }
    }
    iAssert(errors_[0].code == unknownStatusCode_GmStatusCode);
    return &errors_[0].err; /* unknown */
}
