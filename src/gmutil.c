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
#include "fontpack.h"
#include "lang.h"
#include "sitespec.h"
#include "ui/color.h"

#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/object.h>
#include <the_Foundation/path.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/regexp.h>

iRegExp *newGemtextLink_RegExp(void) {
    return new_RegExp("=>\\s*([^\\s]+)(\\s.*)?", 0);
}

void init_Url(iUrl *d, const iString *text) {
    if (!text) {
        iZap(*d);
        return;
    }
    /* Handle "file:" as a special case since it only has the path part. */
    if (startsWithCase_String(text, "file://")) {
        iZap(*d);
        const char *cstr = constBegin_String(text);
        d->scheme = (iRangecc){ cstr, cstr + 4 };
        d->path   = (iRangecc){ cstr + 7, constEnd_String(text) };
        return;
    }
    static iRegExp *urlPattern_;
    static iRegExp *authPattern_;
    if (!urlPattern_) {
        urlPattern_  = new_RegExp("^(([-.+a-z0-9]+):)?(//([^/?#]*))?"
                                 "([^?#]*)(\\?([^#]*))?(#(.*))?",
                                 caseInsensitive_RegExpOption);
        authPattern_ = new_RegExp("(([^@]+)@)?(([^:\\[\\]]+)"
                                  "|(\\[[0-9a-f:]+\\]))(:([0-9]+))?",
                                  caseInsensitive_RegExpOption);
    }
    iZap(*d);
    iRegExpMatch m;
    init_RegExpMatch(&m);
    if (matchString_RegExp(urlPattern_, text, &m)) {
        d->scheme   = capturedRange_RegExpMatch(&m, 2);
        d->host     = capturedRange_RegExpMatch(&m, 4);
        d->port     = (iRangecc){ d->host.end, d->host.end };
        d->path     = capturedRange_RegExpMatch(&m, 5);
        d->query    = capturedRange_RegExpMatch(&m, 6);
        d->fragment = capturedRange_RegExpMatch(&m, 8); /* starts with a hash */
        /* Check if the authority contains a port. */
        init_RegExpMatch(&m);
        if (matchRange_RegExp(authPattern_, d->host, &m)) {
            d->host = capturedRange_RegExpMatch(&m, 3);
            d->port = capturedRange_RegExpMatch(&m, 7);
        }
        /* Remove brackets from an IPv6 literal. */
        if (size_Range(&d->host) > 2 && d->host.start[0] == '[' && d->host.end[-1] == ']') {
            d->host.start++;
            d->host.end--;
        }
    }
}

uint16_t port_Url(const iUrl *d) {
    uint16_t port = 0;
    if (!isEmpty_Range(&d->port)) {
        iString portStr;
        initRange_String(&portStr, d->port);
        port = toInt_String(&portStr);
        deinit_String(&portStr);
    }
    if (port != 0) {
        return port;
    }
    if (isEmpty_Range(&d->scheme) || equalCase_Rangecc(d->scheme, "gemini")) {
        port = GEMINI_DEFAULT_PORT;
    }
    else if (equalCase_Rangecc(d->scheme, "gopher")) {
        port = 70;
    }
    else if (equalCase_Rangecc(d->scheme, "finger")) {
        port = 79;
    }
    else if (equalCase_Rangecc(d->scheme, "http")) {
        port = 80;
    }
    else if (equalCase_Rangecc(d->scheme, "spartan")) {
        port = 300;
    }
    else if (equalCase_Rangecc(d->scheme, "https")) {
        port = 443;
    }
    return port;
}

static iRangecc dirPath_(iRangecc path) {
    const size_t pos = lastIndexOfCStr_Rangecc(path, "/");
    if (pos == iInvalidPos) return path;
    return (iRangecc){ path.start, path.start + pos };
}

iLocalDef iBool isDef_(iRangecc cc) {
    return !isEmpty_Range(&cc);
}

static iRangecc prevPathSeg_(const char *end, const char *start) {
    iRangecc seg = { end, end };
    if (start == end) {
        return seg;
    }
    do {
        seg.start--;
    } while (*seg.start != '/' && seg.start != start);
    return seg;
}

void stripUrlPort_String(iString *d) {
    iUrl parts;
    init_Url(&parts, d);
    if (!isEmpty_Range(&parts.port)) {
        /* Always preceded by a colon. */
        remove_Block(&d->chars, parts.port.start - 1 - constBegin_String(d),
                     size_Range(&parts.port) + 1);
    }
}

void stripDefaultUrlPort_String(iString *d) {
    iUrl parts;
    init_Url(&parts, d);
    if (equalCase_Rangecc(parts.scheme, "gemini") &&
        equal_Rangecc(parts.port, GEMINI_DEFAULT_PORT_CSTR)) {
        /* Always preceded by a colon. */
        remove_Block(&d->chars, parts.port.start - 1 - constBegin_String(d),
                     size_Range(&parts.port) + 1);
    }
}

const iString *urlQueryStripped_String(const iString *url) {
    size_t pos = indexOfCStr_String(url, "?");
    if (pos != iInvalidPos) {
        iString *stripped = collect_String(copy_String(url));
        truncate_Block(&stripped->chars, pos);
        return stripped;
    }
    return url;
}

iBool isDataUrl_String(const iString *d) {
    return startsWithCase_String(d, "data:");
}

const iString *urlFragmentStripped_String(const iString *d) {
    if (isDataUrl_String(d)) {
        return d;
    }
    /* Note: Could use `iUrl` here and leave out the fragment. */
    const size_t fragPos = indexOf_String(d, '#');
    if (fragPos != iInvalidPos) {
        return collect_String(newRange_String((iRangecc){ constBegin_String(d),
                                                          constBegin_String(d) + fragPos }));
    }
    return d;
}

void cleanUrlPath_String(iString *d) {
    iString clean;
    init_String(&clean);
    iUrl parts;
    init_Url(&parts, d);
    iRangecc seg = iNullRange;
    while (nextSplit_Rangecc(parts.path, "/", &seg)) {
        if (equal_Rangecc(seg, "..")) {
            /* Back up one segment. */
            iRangecc last = prevPathSeg_(constEnd_String(&clean), constBegin_String(&clean));
            truncate_Block(&clean.chars, last.start - constBegin_String(&clean));
        }
        else if (equal_Rangecc(seg, ".")) {
            /* Skip it. */
        }
        else if (!isEmpty_Range(&seg)) {
            /* Ensure the cleaned path starts with a slash if the original does. */
            if (!isEmpty_String(&clean) || startsWith_Rangecc(parts.path, "/")) {
                appendCStr_String(&clean, "/");
            }
            appendRange_String(&clean, seg);
        }
    }
    if (endsWith_Rangecc(parts.path, "/")) {
        appendCStr_String(&clean, "/");
    }
    /* Replace with the new path. */
    if (cmpCStrNSc_Rangecc(parts.path, cstr_String(&clean), size_String(&clean), &iCaseSensitive)) {
        const size_t pos = parts.path.start - constBegin_String(d);
        remove_Block(&d->chars, pos, size_Range(&parts.path));
        insertData_Block(&d->chars, pos, cstr_String(&clean), size_String(&clean));
    }
    deinit_String(&clean);
}

iRangecc urlScheme_String(const iString *d) {
    iUrl url;
    init_Url(&url, d);
    return url.scheme;
}

iRangecc urlHost_String(const iString *d) {
    iUrl url;
    init_Url(&url, d);
    return url.host;
}

iRangecc urlDirectory_String(const iString *d) {
    iUrl parts;
    init_Url(&parts, d);
    if (size_Range(&parts.path) > 0 && !endsWith_Rangecc(parts.path, "/")) {
        /* Remove the last path segment. */
        while (parts.path.end > parts.path.start && parts.path.end[-1] != '/') {
            parts.path.end--;
        }
    }
    return parts.path;    
}

uint16_t urlPort_String(const iString *d) {
    iUrl url;
    init_Url(&url, d);
    return port_Url(&url);
}

iRangecc urlUser_String(const iString *d) {
    static iRegExp *userPats_[2];
    if (!userPats_[0]) {
        userPats_[0] = new_RegExp("~([^/?]+)", 0);
        userPats_[1] = new_RegExp("/users/([^/?]+)", caseInsensitive_RegExpOption);
    }
    iRegExpMatch m;
    init_RegExpMatch(&m);
    iRangecc found = iNullRange;
    iForIndices(i, userPats_) {
        if (matchString_RegExp(userPats_[i], d, &m)) {
            found = capturedRange_RegExpMatch(&m, 1);
        }
    }
    return found;
}

iRangecc urlRoot_String(const iString *d) {
    const char *rootEnd;
    const iRangecc user = urlUser_String(d);
    if (!isEmpty_Range(&user)) {
        rootEnd = user.end;
    }
    else {
        iUrl parts;
        init_Url(&parts, d);
        if (equalCase_Rangecc(parts.scheme, "about")) {
            return (iRangecc){ constBegin_String(d), parts.path.start };
        }
        rootEnd = parts.path.start;
    }
    return (iRangecc){ constBegin_String(d), rootEnd };
}

const iBlock *urlThemeSeed_String(const iString *url) {
    if (equalCase_Rangecc(urlScheme_String(url), "file")) {
        return collect_Block(new_Block(0));
    }
    const iRangecc user = urlUser_String(url);
    if (isEmpty_Range(&user)) {
        return collect_Block(newRange_Block(urlHost_String(url)));
    }
    return collect_Block(newRange_Block(user));
}

const iBlock *urlPaletteSeed_String(const iString *url) {
    if (equalCase_Rangecc(urlScheme_String(url), "file")) {
        return urlThemeSeed_String(url);
    }
    /* Check for a site-specific setting. */
    const iString *seed =
        valueString_SiteSpec(collectNewRange_String(urlRoot_String(url)), paletteSeed_SiteSpecKey);
    if (!isEmpty_String(seed)) {
        return utf8_String(seed);
    }
    return urlThemeSeed_String(url);
}

static iBool isAbsolutePath_(iRangecc path) {
    return isAbsolute_Path(collect_String(urlDecode_String(collect_String(newRange_String(path)))));
}

static iString *punyDecodeHost_(iRangecc host) {
    iString *result = new_String();
    iRangecc label = iNullRange;
    while (nextSplit_Rangecc(host, ".", &label)) {
        if (!isEmpty_String(result)) {
            appendChar_String(result, '.');
        }
        if (startsWithCase_Rangecc(label, "xn--")) {
            iString *dec = punyDecode_Rangecc((iRangecc){ label.start + 4, label.end });
            if (!isEmpty_String(dec)) {
                append_String(result, dec);
                continue;
            }
        }
        appendRange_String(result, label);
    }
    return result;
}

void urlDecodePath_String(iString *d) {
    iUrl url;
    init_Url(&url, d);
    if (isEmpty_Range(&url.path)) {
        return;
    }
    iString *decoded = new_String();
    appendRange_String(decoded, (iRangecc){ constBegin_String(d), url.path.start });
    iString *path    = newRange_String(url.path);
    iString *decPath = urlDecodeExclude_String(path, "%?/#"); /* don't decode reserved path chars */
    append_String(decoded, decPath);
    delete_String(decPath);
    delete_String(path);
    appendRange_String(decoded, (iRangecc){ url.path.end, constEnd_String(d) });
    set_String(d, decoded);
    delete_String(decoded);
}

void urlEncodePath_String(iString *d) {
    iUrl url;
    init_Url(&url, d);
    if (equalCase_Rangecc(url.scheme, "data")) {
        return;
    }
    if (isEmpty_Range(&url.path)) {
        return;
    }
    iString *encoded = new_String();
    appendRange_String(encoded, (iRangecc){ constBegin_String(d), url.path.start });
    iString *path    = newRange_String(url.path);
    iString *encPath = urlEncodeExclude_String(path, "%/= ");
    append_String(encoded, encPath);
    delete_String(encPath);
    delete_String(path);
    appendRange_String(encoded, (iRangecc){ url.path.end, constEnd_String(d) });
    set_String(d, encoded);
    delete_String(encoded);
}

void urlEncodeQuery_String(iString *d) {
    iUrl url;
    init_Url(&url, d);
    if (isEmpty_Range(&url.query)) {
        return;
    }
    iString encoded;
    init_String(&encoded);
    appendRange_String(&encoded, (iRangecc){ constBegin_String(d), url.query.start });
    iString query;
    url.query.start++; /* omit the question mark */
    initRange_String(&query, url.query);
    iString *encQuery = urlEncode_String(&query); /* fully encoded */
    appendCStr_String(&encoded, "?");
    append_String(&encoded, encQuery);    
    delete_String(encQuery);
    deinit_String(&query);
    appendRange_String(&encoded, (iRangecc){ url.query.end, constEnd_String(d) });
    set_String(d, &encoded);
    deinit_String(&encoded);
}

iBool isKnownScheme_Rangecc(iRangecc scheme) {
    if (isKnownUrlScheme_Rangecc(scheme)) {
        return iTrue;
    }
    static const char *uriSchemes[] = { "about", "data" };
    iForIndices(i, uriSchemes) {
        if (equalCase_Rangecc(scheme, uriSchemes[i])) {
            return iTrue;
        }
    }    
    return iFalse;
}

iBool isKnownUrlScheme_Rangecc(iRangecc scheme) {
    static const char *schemes[] = { "gemini", "gopher", "finger", "spartan",
                                     "http",   "https",  "file" };
    iForIndices(i, schemes) {
        if (equalCase_Rangecc(scheme, schemes[i])) {
            return iTrue;
        }
    }
    return iFalse;
}

const iString *absoluteUrl_String(const iString *d, const iString *urlMaybeRelative) {
    iUrl orig;
    iUrl rel;
    init_Url(&orig, d);
    init_Url(&rel, urlMaybeRelative);
    if (!isEmpty_Range(&rel.scheme) && !isKnownUrlScheme_Rangecc(rel.scheme) &&
        isEmpty_Range(&rel.host)) {
        /* Probably not an URL, so we can't make this absolute. */
        return urlMaybeRelative;
    }
    const iBool isRelative = !isDef_(rel.host);
    iRangecc scheme = isDef_(orig.scheme) ? orig.scheme : range_CStr("gemini");
    if (isDef_(rel.scheme)) {
        scheme = rel.scheme;
    }
    else if (isRelative && isDef_(orig.scheme)) {
        scheme = orig.scheme;
    }
    iString *absolute = collectNew_String();
    appendRange_String(absolute, scheme);
    appendCStr_String(absolute, "://");
    /* Authority. */ {
        const iUrl *selHost = isDef_(rel.host) ? &rel : &orig;
        const iBool isIPv6 = size_Range(&selHost->host) > 0 &&
                             iStrStrN(selHost->host.start, ":", size_Range(&selHost->host)) != NULL;
        iString *decHost = punyDecodeHost_(selHost->host);
        if (isIPv6) {
            appendCStr_String(absolute, "[");
        }
        append_String(absolute, decHost);
        if (isIPv6) {
            appendCStr_String(absolute, "]");
        }
        delete_String(decHost);
        /* Default Gemini port is removed as redundant; normalization. */
        if (!isEmpty_Range(&selHost->port) &&
            (!equalCase_Rangecc(scheme, "gemini") ||
             !equal_Rangecc(selHost->port, GEMINI_DEFAULT_PORT_CSTR))) {
            appendCStr_String(absolute, ":");
            appendRange_String(absolute, selHost->port);
        }
    }
    if (isDef_(rel.scheme) || isDef_(rel.host) || isAbsolutePath_(rel.path)) {
        if (!startsWith_Rangecc(rel.path, "/")) {
            appendCStr_String(absolute, "/");
        }
        appendRange_String(absolute, rel.path);
    }
    else if (isDef_(rel.path)) {
        if (!endsWith_Rangecc(orig.path, "/")) {
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
        /* If this is known to be a directory reference, append a slash. */
        if (!endsWith_String(absolute, "/") &&
            (equal_Rangecc(rel.path, "..") || endsWith_Rangecc(rel.path, "/.."))) {
            appendCStr_String(absolute, "/");
        }
    }
    else if (isDef_(rel.query)) {
        /* Just a new query. */
        appendRange_String(absolute, orig.path);
    }
    appendRange_String(absolute, rel.query);
    appendRange_String(absolute, rel.fragment);
    normalize_String(absolute);
    cleanUrlPath_String(absolute);
    return absolute;
}

iBool isLikelyUrl_String(const iString *d) {
    /* Guess whether a human intends the string to be an URL. This is supposed to be fuzzy;
       not completely per-spec: a) begins with a scheme; b) has something that looks like a
       hostname */
    iRegExp *pattern = new_RegExp("^([a-z]+:)?//.*|"
                                  "^(//)?([^/?#: ]+)([/?#:].*)$|"
                                  "^([-\\w]+(\\.[-\\w]+)+|localhost)$",
                                  caseInsensitive_RegExpOption);
    iRegExpMatch m;
    init_RegExpMatch(&m);
    const iBool likelyUrl = matchString_RegExp(pattern, d, &m);
    iRelease(pattern);
    return likelyUrl;
}

static iBool equalPuny_(const iString *d, iRangecc orig) {
    if (!endsWith_String(d, "-")) {
        return iFalse; /* This is a sufficient condition? */
    }
    if (size_String(d) != size_Range(&orig) + 1) {
        return iFalse;
    }
    return iCmpStrN(cstr_String(d), orig.start, size_Range(&orig)) == 0;
}

void punyEncodeDomain_Rangecc(iRangecc domain, iString *encoded_out) {
    /* The domain name needs to be split into labels. */
    iRangecc label   = iNullRange;
    iBool    isFirst = iTrue;
    while (nextSplit_Rangecc(domain, ".", &label)) {
        if (!isFirst) {
            appendChar_String(encoded_out, '.');
        }
        isFirst       = iFalse;
        iString *puny = punyEncode_Rangecc(label);
        if (!isEmpty_String(puny) && !equalPuny_(puny, label)) {
            appendCStr_String(encoded_out, "xn--");
            append_String(encoded_out, puny);
        }
        else {
            appendRange_String(encoded_out, label);
        }
        delete_String(puny);
    }
}

void punyEncodeUrlHost_String(iString *absoluteUrl) {
    iUrl url;
    init_Url(&url, absoluteUrl);
    if (equalCase_Rangecc(url.scheme, "data")) {
        return;
    }
    if (isEmpty_Range(&url.host)) {
        return;
    }
    iString *encoded = new_String();
    setRange_String(encoded, (iRangecc){ url.scheme.start, url.host.start });
    punyEncodeDomain_Rangecc(url.host, encoded);
    appendRange_String(encoded, (iRangecc){ url.host.end, constEnd_String(absoluteUrl) });
    set_String(absoluteUrl, encoded);
    delete_String(encoded);
}

iString *makeFileUrl_String(const iString *localFilePath) {
    iString *url = makeAbsolute_Path(collect_String(cleaned_Path(localFilePath)));
    replace_Block(&url->chars, '\\', '/'); /* in case it's a Windows path */
    set_String(url, collect_String(urlEncodeExclude_String(url, "/:")));
#if defined (iPlatformMsys)
    prependChar_String(url, '/'); /* three slashes */
#endif
    prependCStr_String(url, "file://");
    return url;
}

const char *makeFileUrl_CStr(const char *localFilePath) {
    return cstrCollect_String(makeFileUrl_String(collectNewCStr_String(localFilePath)));
}

iString *localFilePathFromUrl_String(const iString *d) {
    iUrl url;
    init_Url(&url, d);
    if (!equalCase_Rangecc(url.scheme, "file")) {
        return NULL;
    }
    iString *path = urlDecode_String(collect_String(newRange_String(url.path)));
#if defined (iPlatformMsys)
    /* Remove the extra slash from the beginning. */
    if (startsWith_String(path, "/")) {
        remove_Block(&path->chars, 0, 1);
    }
    replace_Block(&path->chars, '/', '\\');
#endif
    return path;
}

const iString *findContainerArchive_Path(const iString *path) {
    iBeginCollect();
    while (!isEmpty_String(path) && cmp_String(path, ".")) {
        iString *dir = newRange_String(dirName_Path(path));
        if (endsWithCase_String(dir, ".zip") ||
            endsWithCase_String(dir, ".gpub") ||
            endsWithCase_String(dir, ".fontpack")) {
            iEndCollect();
            return collect_String(dir);
        }
        path = collect_String(dir);
    }
    iEndCollect();
    return NULL;
}

const char *mimetypes[] = {
    ".gmi", "text/gemini",
    ".gemini", "text/gemini",
    ".pem", "application/x-pem-file",
    ".gpub", "application/gpub+zip",
    ".wav", "audio/wave", /* overrides mimetypes.i entry */
    ".fontpack", mimeType_FontPack,
    ".md", "text/markdown",
    ".mdown", "text/markdown",
    ".markdn", "text/markdown",
    ".markdown", "text/markdown",
#include "mimetypes.i"
#include "plaintext.i"
    NULL, NULL
};

const char *mediaTypeFromFileExtension_String(const iString *d) {
    for (int i = 0; mimetypes[i]; i += 2) {
        if (endsWithCase_String(d, mimetypes[i])) {
            return mimetypes[i + 1];
        }
    }
    return "application/octet-stream";
}

const char *mediaType_Path(const iString *path) {
    const char *mtype = mediaTypeFromFileExtension_String(path);
    if (iCmpStr(mtype, "application/octet-stream")) {
        return mtype; /* extension recognized */
    }
    /* If the file is reasonably small and looks like UTF-8, we'll display it as text/plain. */
    if (fileExists_FileInfo(path) && fileSize_FileInfo(path) <= 5000000) {
        iFile *f = new_File(path);
        if (open_File(f, readOnly_FileMode)) {
            iBlock *content = readAll_File(f);
            if (isUtf8_Rangecc(range_Block(content))) {
                mtype = "text/plain; charset=utf-8";
            }
            delete_Block(content);
        }
        iRelease(f);
    }
    return mtype;
}

static void replaceAllChars_String_(iString *d, char c, const char *replacement) {
    const char cstr[2] = { c, 0 };
    const size_t repLen = strlen(replacement);
    size_t pos = 0;
    for (;;) {
        pos = indexOfCStrFrom_String(d, cstr, pos);
        if (pos == iInvalidPos) break;
        remove_Block(&d->chars, pos, 1);
        insertData_Block(&d->chars, pos, replacement, repLen);
        pos += repLen;
    }    
}

void urlEncodeSpaces_String(iString *d) {
    replaceAllChars_String_(d, ' ', "%20");  /* spaces */
    replaceAllChars_String_(d, '\n', "%0A"); /* newlines */
}

const iString *withSpacesEncoded_String(const iString *d) {
    if (isDataUrl_String(d)) {
        return d;
    }
    /* Only make a copy if we need to modify the URL. */
    if (indexOfCStr_String(d, " ") != iInvalidPos) {
        iString *enc = copy_String(d);
        urlEncodeSpaces_String(enc);
        return collect_String(enc);
    }
    return d;
}

const iString *withScheme_String(const iString *d, const char *scheme) {
    iUrl parts;
    init_Url(&parts, d);
    if (!equalCase_Rangecc(parts.scheme, scheme)) {
        iString *repl = collectNewCStr_String(scheme);
        appendRange_String(repl, (iRangecc){ parts.scheme.end, constEnd_String(d) });
        return repl;
    }
    return d;
}

const iString *canonicalUrl_String(const iString *d) {
    /* The "canonical" form, used for internal storage and comparisons, is:
       - all non-reserved characters decoded (i.e., it's an IRI)
       - except spaces, which are always `%20`
       This means a canonical URL can be used on a gemtext link line without modifications. */
    iString *canon = NULL;
    iUrl parts;
    init_Url(&parts, d);
    /* Colons (0x3a) are in decoded form in the URL path. */
    if (iStrStrN(parts.path.start, "%3A", size_Range(&parts.path)) ||
        iStrStrN(parts.path.start, "%3a", size_Range(&parts.path))) {
        /* This is done separately to avoid the copy if %3A is not present; it's rare. */
        canon = copy_String(d);
        urlDecodePath_String(canon);
        iString *dec = maybeUrlDecodeExclude_String(canon, "% " URL_RESERVED_CHARS); /* decode everything else in all parts */
        if (dec) {
            set_String(canon, dec);
            delete_String(dec);
        }
    }
    else {
        canon = maybeUrlDecodeExclude_String(d, "% " URL_RESERVED_CHARS);
    }
    /* `canon` may now be NULL if nothing was decoded. */
    if (indexOfCStr_String(canon ? canon : d, " ") != iInvalidPos ||
        indexOfCStr_String(canon ? canon : d, "\n") != iInvalidPos) {
        if (!canon) {
            canon = copy_String(d);
        }
        urlEncodeSpaces_String(canon);
    }    
    return canon ? collect_String(canon) : d;
}

const iString *prettyDataUrl_String(const iString *d, int contentColor) {
    iUrl url;
    init_Url(&url, d);
    if (!equalCase_Rangecc(url.scheme, "data")) {
        return d;
    }
    iString *pretty = new_String();
    const char *comma = strchr(url.path.start, ',');
    if (!comma) {
        comma = iMin(constEnd_String(d), constBegin_String(d) + 256);
    }
    appendRange_String(pretty, (iRangecc){ constBegin_String(d), comma });
    if (size_Range(&url.path)) {
        if (contentColor != none_ColorId) {
            appendCStr_String(pretty, escape_Color(contentColor));
        }
        appendCStr_String(pretty, " (");
        appendCStr_String(pretty, formatCStrs_Lang("num.bytes.n", size_Range(&url.path)));
        appendCStr_String(pretty, ")");
    }
    return collect_String(pretty);
}

iRangecc mediaTypeWithoutParameters_Rangecc(iRangecc mime) {
    iRangecc part = iNullRange;
    nextSplit_Rangecc(mime, ";", &part);
    return part;
}

const iString *feedEntryOpenCommand_String(const iString *url, int newTab, int newWindow) {
    if (!isEmpty_String(url)) {
        iString *cmd = collectNew_String();
        const size_t fragPos = indexOf_String(url, '#');
        if (fragPos != iInvalidPos) {
            iString *head = newRange_String(
                (iRangecc){ constBegin_String(url) + fragPos + 1, constEnd_String(url) });
            format_String(cmd,
                          "open fromsidebar:1 newtab:%d newwindow:%d gotourlheading:%s url:%s",
                          newTab,
                          newWindow,
                          cstr_String(head),
                          cstr_Rangecc((iRangecc){ constBegin_String(url),
                                                   constBegin_String(url) + fragPos }));
            delete_String(head);
        }
        else {
            format_String(cmd,
                          "open fromsidebar:1 newtab:%d newwindow:%d url:%s",
                          newTab,
                          newWindow,
                          cstr_String(url));
        }
        return cmd;
    }
    return NULL;
}

static const struct {
    enum iGmStatusCode code;
    iGmError           err;
} errors_[] = {
    { unknownStatusCode_GmStatusCode, /* keep this as the first one (fallback return value) */
      { 0x1f4ab, /* dizzy */
        "${error.badstatus}",
        "${error.badstatus.msg}" } },
    { failedToOpenFile_GmStatusCode,
      { 0x1f4c1, /* file folder */
        "${error.openfile}",
        "${error.openfile.msg}" } },
    { invalidLocalResource_GmStatusCode,
      { 0,
        "${error.badresource}",
        "${error.badresource.msg}" } },
    { unsupportedMimeType_GmStatusCode,
      { 0x1f47d, /* alien */
        "${error.unsupported.media}",
        "${error.unsupported.media.msg}" } },
    { unsupportedProtocol_GmStatusCode,
      { 0x1f61e, /* disappointed */
        "${error.unsupported.protocol}",
        "${error.unsupported.protocol.msg}" } },
    { incompleteHeader_GmStatusCode,
      { 0x1f615, /* confused */
        "${error.incompleteheader}",
        "${error.incompleteheader.msg}" } },
    { invalidHeader_GmStatusCode,
      { 0x1f4a9, /* pile of poo */
        "${error.badheader}",
        "${error.badheader.msg}" } },
    { invalidRedirect_GmStatusCode,
      { 0x27a0, /* dashed arrow */
        "${error.badredirect}",
        "${error.badredirect.msg}" } },
    { schemeChangeRedirect_GmStatusCode,
      { 0x27a0, /* dashed arrow */
        "${error.schemeredirect}",
        "${error.schemeredirect.msg}"} },
    { tooManyRedirects_GmStatusCode,
      { 0x27a0, /* dashed arrow */
        "${error.manyredirects}",
        "${error.manyredirects.msg}"} },
    { tlsFailure_GmStatusCode,
      { 0x1f5a7, /* networked computers */
        "${error.tls}",
        "${error.tls.msg}" } },
    { tlsServerCertificateExpired_GmStatusCode,
      { 0x1f4C6, /* calendar */
        "${error.certexpired}",
        "${error.certexpired.msg}" } },
    { tlsServerCertificateNotVerified_GmStatusCode,
      { 0x1f645, /* no good */
        "${error.certverify}",
        "${error.certverify.msg}" } },
    { proxyCertificateExpired_GmStatusCode,
      { 0x1f4C6, /* calendar */
        "${error.proxyexpired}",
        "${error.proxyexpired.msg}" } },
    { proxyCertificateNotVerified_GmStatusCode,
      { 0x1f645, /* no good */
        "${error.proxyverify}",
        "${error.proxyverify.msg}" } },
    { ansiEscapes_GmStatusCode,
      { 0x1f5b3, /* old computer */
        "${error.ansi}",
        "${error.ansi.msg}" } },
    { missingGlyphs_GmStatusCode,
      { 0x1f520, /* ABCD */
        "${error.glyphs}",
        "${error.glyphs.msg}" } },
    { temporaryFailure_GmStatusCode,
      { 0x1f50c, /* electric plug */
        "${error.temporary}",
        "${error.temporary.msg}" } },
    { serverUnavailable_GmStatusCode,
      { 0x1f525, /* fire */
        "${error.unavail}",
        "${error.unavail.msg}" } },
    { cgiError_GmStatusCode,
      { 0x1f4a5, /* collision */
        "${error.cgi}",
        "${error.cgi.msg}" } },
    { proxyError_GmStatusCode,
      { 0x1f310, /* globe */
        "${error.proxy}",
        "${error.proxy.msg}" } },
    { slowDown_GmStatusCode,
      { 0x1f40c, /* snail */
        "${error.slowdown}",
        "${error.slowdown.msg}" } },
    { permanentFailure_GmStatusCode,
      { 0x1f6ab, /* no entry */
        "${error.permanent}",
        "${error.permanent.msg}" } },
    { notFound_GmStatusCode,
      { 0x1f50d, /* magnifying glass */
        "${error.notfound}",
        "${error.notfound.msg}" } },
    { gone_GmStatusCode,
      { 0x1f47b, /* ghost */
        "${error.gone}",
        "${error.gone.msg}" } },
    { proxyRequestRefused_GmStatusCode,
      { 0x1f6c2, /* passport control */
        "${error.proxyrefusal}",
        "${error.proxyrefusal.msg}" } },
    { badRequest_GmStatusCode,
      { 0x1f44e, /* thumbs down */
        "${error.badrequest}",
        "${error.badrequest.msg}" } },
    { clientCertificateRequired_GmStatusCode,
      { 0x1f511, /* key */
        "${error.cert.needed}",
        "${error.cert.needed.msg}" } },
    { certificateNotAuthorized_GmStatusCode,
      { 0x1f512, /* lock */
        "${error.cert.auth}",
        "${error.cert.auth.msg}" } },
    { certificateNotValid_GmStatusCode,
      { 0x1f6a8, /* revolving light */
        "${error.cert.invalid}",
        "${error.cert.invalid.msg}" } },
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

