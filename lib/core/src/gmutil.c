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

#include "lagrange/gmutil.h"
#include "lagrange/core.h"
#include "lagrange/sitespec.h"

#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/object.h>
#include <the_Foundation/path.h>
#include <the_Foundation/regexp.h>

iRegExp *newGemtextLink_RegExp(void) {
    return new_RegExp("=>\\s*([^\\s]+)(\\s.*)?", 0);
}

static iBool isSchemeChar_(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '.' || c == '+';
}

static iBool isDigit_(char c) {
    return c >= '0' && c <= '9';
}

static iBool isIPv6Char_(char c) {
    return isDigit_(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == ':';
}

static iBool isFileUrl_(const char *start, const char *end) {
    static const char prefix_[] = "file://";
    if (end - start < 7) {
        return iFalse;
    }
    for (int i = 0; i < 7; i++) {
        const char c = start[i];
        if ((c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c) != prefix_[i]) {
            return iFalse;
        }
    }
    return iTrue;
}

static iBool parseHostPort_(iUrl *d, const char *pos, const char *end) {
    /* `host[:port]` or `[IPv6][:port]`; anything after them is ignored. */
    const char *i = pos;
    while (i < end && *i != ':' && *i != '[' && *i != ']') {
        i++;
    }
    if (i == pos) {
        if (pos == end || *pos != '[') {
            return iFalse;
        }
        for (i = pos + 1; i < end && isIPv6Char_(*i); i++) {}
        if (i == pos + 1 || i == end || *i != ']') {
            return iFalse;
        }
        i++;
    }
    d->host = (iRangecc){ pos, i };
    d->port = (iRangecc){ i, i };
    if (end - i >= 2 && *i == ':' && isDigit_(i[1])) {
        const char *port = ++i;
        while (i < end && isDigit_(*i)) {
            i++;
        }
        d->port = (iRangecc){ port, i };
    }
    return iTrue;
}

static void parseAuthority_(iUrl *d) {
    /* The authority is searched for `[user@]host[:port]` from left to right. */
    const iRangecc auth = d->host;
    for (const char *pos = auth.start; pos < auth.end; pos++) {
        const char *at = pos;
        while (at < auth.end && *at != '@') {
            at++;
        }
        if (at > pos && at < auth.end && parseHostPort_(d, at + 1, auth.end)) {
            d->user = (iRangecc){ pos, at };
            return;
        }
        if (parseHostPort_(d, pos, auth.end)) {
            d->user = (iRangecc){ pos, pos };
            return;
        }
    }
}

void init_Url(iUrl *d, const iString *text) {
    iZap(*d);
    if (!text) {
        return;
    }
    const char *start = constBegin_String(text);
    const char *end   = constEnd_String(text);
    /* Handle "file:" as a special case since it only has the path part. */
    if (isFileUrl_(start, end)) {
        d->scheme = (iRangecc){ start, start + 4 };
        d->path   = (iRangecc){ start + 7, end };
        return;
    }
    /* Components are split as in RFC 3986 (Appendix B). Missing ones are empty ranges at the
       position where they would be. */
    const char *pos = start;
    const char *i   = start;
    while (i < end && isSchemeChar_(*i)) {
        i++;
    }
    if (i > start && i < end && *i == ':') {
        d->scheme = (iRangecc){ start, i };
        pos = i + 1;
    }
    else {
        d->scheme = (iRangecc){ start, start };
    }
    d->host = (iRangecc){ d->scheme.end, d->scheme.end };
    if (end - pos >= 2 && pos[0] == '/' && pos[1] == '/') {
        for (i = pos + 2; i < end && *i != '/' && *i != '?' && *i != '#'; i++) {}
        if (i > pos + 2) {
            d->host = (iRangecc){ pos + 2, i };
        }
        pos = i;
    }
    for (i = pos; i < end && *i != '?' && *i != '#'; i++) {}
    d->path = (iRangecc){ pos, i };
    pos = i;
    if (pos < end && *pos == '?') {
        for (i = pos + 1; i < end && *i != '#'; i++) {}
    }
    d->query = (iRangecc){ pos, i };
    pos = i;
    if (pos < end && *pos == '#') {
        for (i = pos + 1; i < end && *i != '\n'; i++) {}
    }
    d->fragment = (iRangecc){ pos, i };
    d->port     = (iRangecc){ d->host.end, d->host.end };
    parseAuthority_(d);
    /* Remove brackets from an IPv6 literal. */
    if (size_Range(&d->host) > 2 && d->host.start[0] == '[' && d->host.end[-1] == ']') {
        d->host.start++;
        d->host.end--;
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
    if (isEmpty_Range(&d->scheme) || equalFastCase_Rangecc(d->scheme, "gemini")) {
        port = GEMINI_DEFAULT_PORT;
    }
    else if (isGopherScheme_Rangecc(d->scheme)) {
        port = 70;
    }
    else if (equalFastCase_Rangecc(d->scheme, "finger")) {
        port = 79;
    }
    else if (equalFastCase_Rangecc(d->scheme, "http")) {
        port = 80;
    }
    else if (equalFastCase_Rangecc(d->scheme, "spartan")) {
        port = 300;
    }
    else if (equalFastCase_Rangecc(d->scheme, "guppy")) {
        port = 6775;
    }
    else if (equalFastCase_Rangecc(d->scheme, "https")) {
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

static iBool isDefaultPort_Url_(const iUrl *d) {
    return (equalFastCase_Rangecc(d->scheme, "gemini") &&
            equal_Rangecc(d->port, GEMINI_DEFAULT_PORT_CSTR)) ||
           (isGopherScheme_Rangecc(d->scheme) && equal_Rangecc(d->port, "70"));
}

const iString *urlDefaultPortStripped_String(const iString *d) {
    iUrl parts;
    init_Url(&parts, d);
    if (isDefaultPort_Url_(&parts)) {
        iString *stripped = copy_String(d);
        stripDefaultUrlPort_String(stripped);
        return collect_String(stripped);
    }
    return d;
}

void stripDefaultUrlPort_String(iString *d) {
    iUrl parts;
    init_Url(&parts, d);
    if (isDefaultPort_Url_(&parts)) {
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
    return startsWithFastCase_String(d, "data:");
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

iRangecc urlHostWithPort_String(const iString *d) {
    iUrl url;
    init_Url(&url, d);
    return (iRangecc){ url.host.start, !isEmpty_Range(&url.port) ? url.port.end : url.host.end };
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
        userPats_[0] = new_RegExp("^/~([^/?]+)", 0);
        userPats_[1] = new_RegExp("^/users/([^/?]+)", caseInsensitive_RegExpOption);
    }
    iUrl url;
    init_Url(&url, d);
    iRegExpMatch m;
    iRangecc found = iNullRange;
    if (isGopherScheme_Rangecc(url.scheme)) {
        /* The path is "/" + item type + selector; only the tilde convention applies,
           and only after a valid item type character. The selector may or may not
           have its own leading slash, e.g. "1~user/" and "1/~user/" are equivalent. */
        static iRegExp *gopherTildePat_;
        if (!gopherTildePat_) {
            gopherTildePat_ = new_RegExp("^/[a-z0-9]/?~([^/?]+)", caseInsensitive_RegExpOption);
        }
        init_RegExpMatch(&m);
        if (matchRange_RegExp(gopherTildePat_, url.path, &m)) {
            found = capturedRange_RegExpMatch(&m, 1);
        }
    }
    else {
        iForIndices(i, userPats_) {
            init_RegExpMatch(&m);
            if (matchRange_RegExp(userPats_[i], url.path, &m)) {
                found = capturedRange_RegExpMatch(&m, 1);
                break;
            }
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
        if (equalFastCase_Rangecc(parts.scheme, "about")) {
            return (iRangecc){ constBegin_String(d), parts.path.start };
        }
        rootEnd = parts.path.start;
    }
    return (iRangecc){ constBegin_String(d), rootEnd };
}

iRangecc urlPath_String(const iString *d) {
    iUrl url;
    init_Url(&url, d);
    return url.path;
}

const iBlock *urlThemeSeed_String(const iString *url) {
    if (isLocalFileScheme_Rangecc(urlScheme_String(url))) {
        return collect_Block(new_Block(0));
    }
    const iRangecc user = urlUser_String(url);
    if (isEmpty_Range(&user)) {
        return collect_Block(newRange_Block(urlHost_String(url)));
    }
    return collect_Block(newRange_Block(user));
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
        if (startsWithFastCase_Rangecc(label, "xn--")) {
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

iString *withUrlParameters_String(const iString *d, ...) {
    iUrl url;
    init_Url(&url, d);
    iString *updated = new_String();
    appendRange_String(updated, (iRangecc){ constBegin_String(d), url.path.end });
    va_list args;
    for (va_start(args, d);;) {
        const char *key = va_arg(args, const char *);
        if (!key) break;
        const char *value = va_arg(args, const char *);
        appendCStr_String(updated, ";");
        appendCStr_String(updated, key);
        if (value) {
            appendCStr_String(updated, "=");
            appendCStr_String(updated, value);
        }
    }
    appendRange_String(updated, (iRangecc){ url.path.end, constEnd_String(d) });
    va_end(args);
    return updated;
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
        if (equalFastCase_Rangecc(scheme, uriSchemes[i])) {
            return iTrue;
        }
    }
    return iFalse;
}

iBool isKnownUrlScheme_Rangecc(iRangecc scheme) {
    static const char *schemes[] = { "gemini", "gopher", "gophers", "finger", "spartan",
                                     "http",   "https",  "file",    "local" };
    iForIndices(i, schemes) {
        if (equalFastCase_Rangecc(scheme, schemes[i])) {
            return iTrue;
        }
    }
    return iFalse;
}

iBool isGopherScheme_Rangecc(iRangecc scheme) {
    return equalFastCase_Rangecc(scheme, "gopher") || equalFastCase_Rangecc(scheme, "gophers");
}

iBool isTlsScheme_Rangecc(iRangecc scheme) {
    return equalFastCase_Rangecc(scheme, "gophers") || equalFastCase_Rangecc(scheme, "gemini") ||
           equalFastCase_Rangecc(scheme, "titan") || equalFastCase_Rangecc(scheme, "misfin");
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
    if (equalFastCase_Rangecc(rel.scheme, "misfin")) {
        /* Use as-is. */
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
            (!equalFastCase_Rangecc(scheme, "gemini") ||
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
    if (!isGopherScheme_Rangecc(scheme) && !equalFastCase_Rangecc(scheme, "finger")) {
        /* Gopher/finger paths are opaque selectors, not hierarchical paths, so dot-segments
           and doubled slashes in them must be preserved as-is instead of being cleaned up. */
        cleanUrlPath_String(absolute);
    }
    if (equalFastCase_Rangecc(scheme, "local") && startsWith_String(absolute, "local:///")) {
        remove_Block(&absolute->chars, 6, 2); /* no authority: "local:/path" */
    }
    return absolute;
}

iBool isLikelyUrl_String(const iString *d) {
    if (endsWith_String(d, ":")) {
        return iFalse;
    }
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

iBool isTitanUrl_String(const iString *d) {
    return equalFastCase_Rangecc(urlScheme_String(d), "titan");
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
    if (equalFastCase_Rangecc(url.scheme, "data")) {
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
#if defined (iPlatformMsys) || defined (iPlatformWindows)
    prependChar_String(url, '/'); /* three slashes */
#endif
    prependCStr_String(url, "file://");
    return url;
}

const char *makeFileUrl_CStr(const char *localFilePath) {
    return cstrCollect_String(makeFileUrl_String(collectNewCStr_String(localFilePath)));
}

#if defined (iPlatformAppleMobile)
/* The app's container directory may move, e.g., when the app is updated or reinstalled.
   Paths saved earlier are rebased to the current container. */
static iBool rebaseAppContainerPath_(iString *path) {
    static const char *containers_ = "/Containers/Data/Application/";
    const size_t pos = indexOfCStr_String(path, containers_);
    if (pos == iInvalidPos) {
        return iFalse;
    }
    const iString *home    = collect_String(home_Path());
    const size_t   homePos = indexOfCStr_String(home, containers_);
    if (homePos == iInvalidPos) {
        return iFalse;
    }
    const size_t idStart = pos + strlen(containers_);
    size_t       idEnd   = indexOfCStrFrom_String(path, "/", idStart);
    if (idEnd == iInvalidPos) {
        idEnd = size_String(path);
    }
    const iRangecc oldId = { constBegin_String(path) + idStart, constBegin_String(path) + idEnd };
    const iRangecc newId = { constBegin_String(home) + homePos + strlen(containers_),
                             constEnd_String(home) };
    if (equal_Rangecc(oldId, cstrCollect_String(newRange_String(newId)))) {
        return iFalse;
    }
    iString *rebased = newRange_String((iRangecc){ constBegin_String(path), oldId.start });
    appendRange_String(rebased, newId);
    appendRange_String(rebased, (iRangecc){ oldId.end, constEnd_String(path) });
    set_String(path, rebased);
    delete_String(rebased);
    return iTrue;
}
#endif

iBool isLocalFileScheme_Rangecc(iRangecc scheme) {
    return equalFastCase_Rangecc(scheme, "file") || equalFastCase_Rangecc(scheme, "local");
}

static iString *localFilePath_(const iString *d, iBool *isRebased) {
    iUrl url;
    init_Url(&url, d);
    const iBool isLocal = equalFastCase_Rangecc(url.scheme, "local");
    *isRebased = iFalse;
    if (!isLocal && !equalFastCase_Rangecc(url.scheme, "file")) {
        return NULL;
    }
    iString *path = urlDecode_String(collect_String(newRange_String(url.path)));
    if (isLocal) {
        /* Relative to the user's data directory. */
        prepend_String(path, dataDir_Core());
    }
#if defined (iPlatformMsys) || defined (iPlatformWindows)
    else if (startsWith_String(path, "/")) {
        /* Remove the extra slash from the beginning. */
        remove_Block(&path->chars, 0, 1);
    }
    replace_Block(&path->chars, '/', '\\');
#endif
#if defined (iPlatformAppleMobile)
    if (!isLocal) {
        *isRebased = rebaseAppContainerPath_(path);
    }
#endif
    return path;
}

iString *localFilePathFromUrl_String(const iString *d) {
    iBool isRebased;
    return localFilePath_(d, &isRebased);
}

/* Files in the user's data directory have a "local:" URL that is independent of where the
   directory is. Returns NULL if the URL does not need to change. */
static iString *localUrlFromFileUrl_(const iString *fileUrl) {
    const iString *dataDir = dataDir_Core();
    iBool          isRebased;
    iString *      path = localFilePath_(fileUrl, &isRebased);
    iString *      url  = NULL;
    const iRangecc rest = { constBegin_String(path) + iMin(size_String(path), size_String(dataDir)),
                            constEnd_String(path) };
    if (!isEmpty_String(dataDir) && startsWith_String(path, cstr_String(dataDir)) &&
        startsWith_Rangecc(rest, iPathSeparator)) {
        iString *rel = newRange_String(rest);
        replace_Block(&rel->chars, '\\', '/');
        url = urlEncodeExclude_String(rel, "/");
        prependCStr_String(url, "local:");
        delete_String(rel);
    }
    else if (isRebased) {
        url = makeFileUrl_String(path);
    }
    delete_String(path);
    return url;
}

const iString *findContainerArchive_Path(const iString *path) {
    iBeginCollect();
    while (!isEmpty_String(path) && cmp_String(path, ".")) {
        iString *dir = newRange_String(dirName_Path(path));
        if (endsWithFastCase_String(dir, ".zip") ||
            endsWithFastCase_String(dir, ".gpub") ||
            endsWithFastCase_String(dir, ".fontpack")) {
            iEndCollect();
            return collect_String(dir);
        }
        path = collect_String(dir);
    }
    iEndCollect();
    return NULL;
}

static const char *mimetypes_[] = {
    ".gmi", "text/gemini",
    ".gemini", "text/gemini",
    ".pem", "application/x-pem-file",
    ".gpub", "application/gpub+zip",
    ".wav", "audio/wave", /* overrides mimetypes.i entry */
    ".fontpack", "application/lagrange-fontpack+zip",
    ".md", "text/markdown",
    ".mdown", "text/markdown",
    ".markdn", "text/markdown",
    ".markdown", "text/markdown",
#include "mimetypes.i"
#include "plaintext.i"
    NULL, NULL
};

const char *mediaTypeFromFileExtension_String(const iString *d) {
    for (int i = 0; mimetypes_[i]; i += 2) {
        if (endsWithFastCase_String(d, mimetypes_[i])) {
            return mimetypes_[i + 1];
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
    if (!equalFastCase_Rangecc(parts.scheme, scheme)) {
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
       This means a canonical URL can be used on a gemtext link line without modifications.
       Files in the user's data directory use the "local:" scheme. */
    if (startsWithFastCase_String(d, "file:")) {
        iString *local = localUrlFromFileUrl_(d);
        if (local) {
            d = collect_String(local);
        }
    }
    iString *canon = maybeUrlDecodeExclude_String(d, " " URL_DECODE_EXCLUDE_CHARS);
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

iRangecc mediaTypeWithoutParameters_Rangecc(iRangecc mime) {
    iRangecc part = iNullRange;
    nextSplit_Rangecc(mime, ";", &part);
    return part;
}

iBool equalMediaType_String(const iString *d, const char *mediaType) {
    return equal_Rangecc(mediaTypeWithoutParameters_Rangecc(range_String(d)), mediaType);
}

const iString *feedEntryOpenCommand_String(const iString *url, int newTab, int newWindow,
                                           iBool isReader) {
    if (!isEmpty_String(url)) {
        iString *cmd = collectNew_String();
        const size_t fragPos = indexOf_String(url, '#');
        if (fragPos != iInvalidPos) {
            iString *head = newRange_String(
                (iRangecc){ constBegin_String(url) + fragPos + 1, constEnd_String(url) });
            format_String(cmd,
                          "open fromsidebar:1 reader:%d newtab:%d newwindow:%d gotourlheading:%s url:%s",
                          isReader,
                          newTab,
                          newWindow,
                          cstr_String(head),
                          cstr_Rangecc((iRangecc){ constBegin_String(url),
                                                   constBegin_String(url) + fragPos }));
            delete_String(head);
        }
        else {
            format_String(cmd, "open fromsidebar:1 reader:%d newtab:%d newwindow:%d url:%s",
                          isReader, newTab, newWindow, cstr_String(url));
        }
        return cmd;
    }
    return collectNew_String();
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
    { unsupportedMimeTypeShownAsUtf8_GmStatusCode,
      { 0x1f524, /* abc */
        "${error.showutf8}",
        "${error.showutf8.msg}" } },
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

const iBlock *urlPaletteSeed_String(const iString *url) {
    if (isLocalFileScheme_Rangecc(urlScheme_String(url))) {
        return urlThemeSeed_String(url);
    }
    /* Check for a site-specific setting. */
    const iScopeRule *rule = constRule_SiteSpec(collectNewRange_String(urlRoot_String(url)));
    const iString *seed = rule ? rule->prefs.paletteSeed : NULL;
    if (seed && !isEmpty_String(seed)) {
        return utf8_String(seed);
    }
    return urlThemeSeed_String(url);
}
