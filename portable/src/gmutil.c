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
#include "lang.h"
#include "ui/color.h"

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
