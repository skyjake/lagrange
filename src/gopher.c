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

#include "gopher.h"

#include <ctype.h>

iDefineTypeConstruction(Gopher)

iLocalDef iBool isLineTerminator_(const char *str) {
    return str[0] == '\r' && str[1] == '\n';
}

iLocalDef iBool isDiagram_(char ch) {
    return strchr("^*_-=~/|\\<>()[]{}", ch) != NULL;
}

static iBool isPreformatted_(iRangecc text) {
    int numDiag  = 0;
    int numSpace = 0;
    for (const char *ch = text.start; ch != text.end; ch++) {
        if (isDiagram_(*ch)) {
            if (++numDiag == 3)
                return iTrue;
        }
        else {
            numDiag = 0;
        }
        if (*ch == ' ' || *ch == '\n') {
            if (++numSpace == 3) return iTrue;
        }
        else {
            numSpace = 0;
        }
    }
    return iFalse;
}

static void setPre_Gopher_(iGopher *d, iBool pre) {
    if (pre && !d->isPre) {
        appendCStr_Block(d->output, "```\n");
    }
    else if (!pre && d->isPre) {
        appendCStr_Block(d->output, "```\n");
    }
    d->isPre = pre;
}

static iBool convertSource_Gopher_(iGopher *d) {
    iBool    converted = iFalse;
    iRangecc body      = range_Block(&d->source);
    iRegExp *pattern   = new_RegExp("(.)([^\t]*)\t([^\t]*)\t([^\t]*)\t([0-9]+)",
                                    caseInsensitive_RegExpOption);
    for (;;) {
        /* Find the end of the line. */
        iRangecc line = { body.start, body.start };
        while (line.end < body.end - 1 && !isLineTerminator_(line.end)) {
            line.end++;
        }
        if (line.end >= body.end - 1 || !isLineTerminator_(line.end)) {
            /* Not a complete line. */
            break;
        }
        body.start = line.end + 2;
        iRegExpMatch m;
        init_RegExpMatch(&m);
        if (matchRange_RegExp(pattern, line, &m)) {
            const char     lineType = *capturedRange_RegExpMatch(&m, 1).start;
            const iRangecc text     = capturedRange_RegExpMatch(&m, 2);
            const iRangecc path     = capturedRange_RegExpMatch(&m, 3);
            const iRangecc domain   = capturedRange_RegExpMatch(&m, 4);
            const iRangecc port     = capturedRange_RegExpMatch(&m, 5);
            iString *buf = new_String();
            switch (lineType) {
                case 'i':
                case '3': {
                    setPre_Gopher_(d, isPreformatted_(text));
                    appendData_Block(d->output, text.start, size_Range(&text));
                    appendCStr_Block(d->output, "\n");
                    break;
                }
                case '0':
                case '1':
                case '7':
                case '4':
                case '5':
                case '9':
                case 'g':
                case 'I':
                case 's': {
                    iBeginCollect();
                    setPre_Gopher_(d, iFalse);
                    format_String(buf,
                                  "=> gopher://%s:%s/%c%s %s\n",
                                  cstr_Rangecc(domain),
                                  cstr_Rangecc(port),
                                  lineType,
                                  cstrCollect_String(
                                      urlEncodeExclude_String(collectNewRange_String(path), "/")),
                                  cstr_Rangecc(text));
                    appendData_Block(d->output, constBegin_String(buf), size_String(buf));
                    iEndCollect();
                    break;
                }
                default:
                    break; /* Ignore unknown types. */
            }
            delete_String(buf);
        }
    }
    iRelease(pattern);
    remove_Block(&d->source, 0, body.start - constBegin_Block(&d->source));
    return converted;
}

void init_Gopher(iGopher *d) {
    d->socket = NULL;
    d->type = 0;
    init_Block(&d->source, 0);
    d->needQueryArgs = iFalse;
    d->isPre = iFalse;
    d->meta = NULL;
    d->output = NULL;
}

void deinit_Gopher(iGopher *d) {
    deinit_Block(&d->source);
    iReleasePtr(&d->socket);
}

void open_Gopher(iGopher *d, const iString *url) {
    iUrl parts;
    init_Url(&parts, url);
    if (!isEmpty_Range(&parts.path)) {
        if (*parts.path.start == '/') {
            parts.path.start++;
        }
    }
    /* Determine Gopher item type (finger is type 0). */
    if (equalCase_Rangecc(parts.scheme, "finger")) {
        d->type = '0';
    }
    else if (parts.path.start < parts.path.end) {
                d->type = *parts.path.start;
                parts.path.start++;
    }
    else {
      d->type = '1';
    }
    if (d->type == '7' && isEmpty_Range(&parts.query)) {
        /* Ask for the query parameters first. */
        d->needQueryArgs = iTrue;
        return;
    }
    /* MIME type determined by the item type. */
    switch (d->type) {
        case '0':
            setCStr_String(d->meta, "text/plain");
            break;
        case '1':
        case '7':
            setCStr_String(d->meta, "text/gemini");
            break;
        case '4':
            setCStr_String(d->meta, "application/mac-binhex");
            break;
        case 'g':
            setCStr_String(d->meta, "image/gif");
            break;
        case 'h':
            setCStr_String(d->meta, "text/html");
            break;
        case 'M':
            setCStr_String(d->meta, "multipart/mixed");
            break;
        case 'I':
            setCStr_String(d->meta, "image/generic");
            break;
        case 's':
            setCStr_String(d->meta, "audio/wave");
            break;
        default:
            setCStr_String(d->meta, "application/octet-stream");
            break;
    }
    d->isPre = iFalse;
    open_Socket(d->socket);
    writeData_Socket(d->socket, parts.path.start, size_Range(&parts.path));
    if (!isEmpty_Range(&parts.query)) {
        iAssert(*parts.query.start == '?');
        parts.query.start++;
        writeData_Socket(d->socket, "\t", 1);
        writeData_Socket(d->socket, parts.query.start, size_Range(&parts.query));
    }
    writeData_Socket(d->socket, "\r\n", 2);
}

void cancel_Gopher(iGopher *d) {
    if (d->socket) {
        close_Socket(d->socket);
    }
}

iBool processResponse_Gopher(iGopher *d, const iBlock *data) {
    iBool changed = iFalse;
    if (d->type == '1' || d->type == '7') {
        append_Block(&d->source, data);
        if (convertSource_Gopher_(d)) {
            changed = iTrue;
        }
    }
    else {
        append_Block(d->output, data);
        changed = iTrue;
    }
    return changed;
}
