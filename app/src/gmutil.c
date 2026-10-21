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
#include <lagrange/lang.h>
#include "color.h"
#include <lagrange/gopher.h>

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

static void adjustNavigationUrl_(iString *url) {
    /* Always go to a gophermap. */
    setUrlItemType_Gopher(url, '1');
    /* Hierarchical navigation doesn't make sense with Titan. */
    if (startsWith_String(url, "titan://")) {
        /* We have no way of knowing if the corresponding URL is valid for Gemini,
           but let's try anyway. */
        set_String(url, withScheme_String(url, "gemini"));
        stripUrlPort_String(url);
    }
}

const iString *navigationParentUrl_String(const iString *d) {
    iUrl parts;
    init_Url(&parts, d);
    if (equalCase_Rangecc(parts.scheme, "gemini")) {
        /* Check for default index pages according to Gemini Best Practices ("Filenames"):
           gemini://geminiprotocol.net/docs/best-practices.gmi */
        if (endsWith_Rangecc(parts.path, "/index.gmi")) {
            parts.path.end -= 9;
        }
        else if (endsWith_Rangecc(parts.path, "/index.gemini")) {
            parts.path.end -= 12;
        }
    }
    if (size_Range(&parts.path) <= 1) {
        return NULL;
    }
    /* Remove the last path segment. */
    if (parts.path.end[-1] == '/') {
        parts.path.end--;
    }
    while (parts.path.end > parts.path.start) {
        if (parts.path.end[-1] == '/') break;
        parts.path.end--;
    }
    iString *parentUrl = collectNewRange_String((iRangecc){ constBegin_String(d), parts.path.end });
    adjustNavigationUrl_(parentUrl);
    if (!cmpCase_String(parentUrl, "about:")) {
        setCStr_String(parentUrl, "about:about");
    }
    return parentUrl;
}

const iString *navigationRootUrl_String(const iString *d) {
    iString *rootUrl = collectNewRange_String(urlRoot_String(d));
    adjustNavigationUrl_(rootUrl);
    if (!cmpCase_String(rootUrl, "about:")) {
        setCStr_String(rootUrl, "about:about");
    }
    else {
        appendCStr_String(rootUrl, "/");
    }
    return rootUrl;
}
