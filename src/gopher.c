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
#include "prefs.h"
#include "app.h"

#include <ctype.h>

iDefineTypeConstruction(Gopher)

iLocalDef iBool isCRLFLineTerminator_(const char *str) {
    return str[0] == '\r' && str[1] == '\n';
}

iLocalDef iBool isLineTerminator_(const char *str) {
    return isCRLFLineTerminator_(str) || str[0] == '\n';
}

iLocalDef iBool isDiagram_(char ch) {
    return strchr("^*_-=~/|\\<>()[]{}", ch) != NULL;
}

iLocalDef iBool isBoxDrawing_Char(iChar c) {
    return (c >= 0x2500 && c <= 0x257f);
}

static iBool isPreformatted_(iRangecc text) {
    int  numDiag   = 0;
    int  numSpace  = 0;
    int  numRepeat = 0;
    iChar chPrev   = 0;
    if (!prefs_App()->geminiStyledGopher) {
        return iFalse; /* just regular text */
    }
    trimEnd_Rangecc(&text);
    for (const char *chPos = text.start; chPos < text.end; ) {
        iChar ch = 0;
        int len = decodeBytes_MultibyteChar(chPos, text.end, &ch);
        if (len <= 0) break;
        chPos += len;
        if (isBoxDrawing_Char(ch)) {
            if (++numDiag == 3) return iTrue;
            continue;
        }
        if (ch != '.' && ch == chPrev) {
            if (numRepeat++ == 6) {
                return iTrue;
            }
        }
        else {
            numRepeat = 0;
        }
        chPrev = ch;
        if (ch < 128 && isDiagram_(ch)) {
            if (++numDiag == 3)
                return iTrue;
        }
        else {
            numDiag = 0;
        }
        if (ch == ' ' || ch == '\n') {
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

static void appendEscapedLineToOutput_Gopher_(iGopher *d, iRangecc text, size_t n) {
    if (!isEmpty_Range(&text)) {
        const char ch = *text.start;
        if (ch == '#' || ch == '>' || ch == '*') {
            appendCStr_Block(d->output, "\u200b"); /* zero-width space */
        }
        appendData_Block(d->output, text.start, n);
    }
    appendCStr_Block(d->output, "\n");
}

static iRegExp *makeMenuItemPattern_(void) {
    return new_RegExp("(.)([^\t]*)\t([^\t]*)\t([^\t]*)\t([0-9]+)?", 0);
}

static iBool convertSource_Gopher_(iGopher *d) {
    iBool    converted = iFalse;
    iRangecc body      = range_Block(&d->source);
    iRegExp *pattern   = makeMenuItemPattern_();
    for (;;) {
        /* Find the end of the line. */
        iRangecc line = { body.start, body.start };
        while (line.end < body.end - 1 && !isLineTerminator_(line.end)) {
            line.end++;
        }
        if (line.end >= body.end - 1 || !isLineTerminator_(line.end)) {
            /* Not a complete line. More may be coming later. */
            break;
        }
        body.start = line.end + (isCRLFLineTerminator_(line.end) ? 2 : 1);
        trimEnd_Rangecc(&line);
        iRegExpMatch m;
        init_RegExpMatch(&m);
        if (equal_Rangecc(line, ".")) {
            break; /* terminator */
        }
        if (matchRange_RegExp(pattern, line, &m)) {
            const char     lineType = *capturedRange_RegExpMatch(&m, 1).start;
            const iRangecc text     = capturedRange_RegExpMatch(&m, 2);
            const iRangecc selector = capturedRange_RegExpMatch(&m, 3);
            const iRangecc domain   = capturedRange_RegExpMatch(&m, 4);
            const iRangecc port     = capturedRange_RegExpMatch(&m, 5);
            iString *buf = new_String();
            switch (lineType) {
                case 'i': {
                    setPre_Gopher_(d, isPreformatted_(text));
                    appendEscapedLineToOutput_Gopher_(d, text, size_Range(&text));
                    break;
                }
                case '3': {
                    /* The server is reporting some kind of an error. */
                    format_String(buf, warning_Icon " %s\n", cstr_Rangecc(text));
                    append_Block(d->output, utf8_String(buf));
                    break;
                }
                case '0':
                case '1':
                case '7':
                case '4':
                case '5':
                case '9':
                case 'g':
                case 'p':
                case 'I':
                case 's': {
                    iBeginCollect();
                    setPre_Gopher_(d, iFalse);
                    format_String(buf,
                                  "=> gopher://%s:%s/%c%s %s\n",
                                  cstr_Rangecc(domain),
                                  isEmpty_Range(&port) ? "70" : cstr_Rangecc(port),
                                  lineType,
                                  cstrCollect_String(
                                      urlEncodeExclude_String(collectNewRange_String(selector), "/")),
                                  cstr_Rangecc(text));
                    append_Block(d->output, utf8_String(buf));
                    iEndCollect();
                    break;
                }
                case '8':
                case 'T': {
                    iBeginCollect();
                    setPre_Gopher_(d, iFalse);
                    format_String(buf,
                                  "=> %s://%s%s%s:%s %s\n",
                                  lineType == '8' ? "telnet" : "tn3270",
                                  cstr_Rangecc(selector),
                                  !isEmpty_Range(&selector) ? "@" : "",
                                  cstr_Rangecc(domain),
                                  cstr_Rangecc(port),
                                  cstr_Rangecc(text));
                    append_Block(d->output, utf8_String(buf));
                    iEndCollect();
                    break;
                }
                case 'h': {
                    iBeginCollect();
                    setPre_Gopher_(d, iFalse);
                    if (startsWith_Rangecc(selector, "URL:")) {
                        format_String(buf,
                                      "=> %s %s\n",
                                      cstr_String(withSpacesEncoded_String(collectNewRange_String(
                                          (iRangecc){ selector.start + 4, selector.end }))),
                                      cstr_Rangecc(text));
                    }
                    append_Block(d->output, utf8_String(buf));
                    iEndCollect();
                    break;
                }
                default: /* all unknown types */
                    setPre_Gopher_(d, iFalse);
                    appendEscapedLineToOutput_Gopher_(d, text, size_Range(&text));
                    setPre_Gopher_(d, iTrue);
                    appendEscapedLineToOutput_Gopher_(d,
                                                      selector,
                                                      !isEmpty_Range(&port) &&
                                                              port.end > selector.start
                                                          ? port.end - selector.start
                                                          : size_Range(&selector));
                    break;
            }
            delete_String(buf);
        }
        else {
#if !defined (NDEBUG)
            printf("[Gopher] unrecognized: {%s}\n", cstr_Rangecc(line));
#endif
        }
    }
    iRelease(pattern);
    /* Remove the part of the source that was successfully converted. This leaves any partially
       received lines to the next conversion attempt. */
    remove_Block(&d->source, 0, body.start - constBegin_Block(&d->source));
    return converted;
}

void init_Gopher(iGopher *d) {
    d->socket        = NULL;
    d->type          = 0;
    d->needQueryArgs = iFalse;
    d->isPre         = iFalse;
    d->meta          = NULL;
    d->output        = NULL;
    init_Block(&d->source, 0);
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
    const iString *reqPath =
        collect_String(urlDecode_String(collectNewRange_String(parts.path)));
    if (d->type == '7' && isEmpty_Range(&parts.query) && !contains_String(reqPath, '\t')) {
        /* Ask for the query parameters first. */
        d->needQueryArgs = iTrue;
        return;
    }
    /* MIME type determined by the item type. */
    switch (d->type) {
        case '0': {
            setCStr_String(d->meta, "text/plain");
            break;
        }
        case '1':
        case '7':
            setCStr_String(d->meta, "text/gophermap");
            break;
        case '4':
            setCStr_String(d->meta, "application/mac-binhex");
            break;
        case 'g':
            setCStr_String(d->meta, "image/gif");
            break;
        case 'p':
            setCStr_String(d->meta, "image/png");
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
        case 's': {
            const char *detected = mediaTypeFromFileExtension_String(reqPath);
            if (startsWith_CStr(detected, "audio/")) {
                setCStr_String(d->meta, detected); /* could be .mp3, for example */
            }
            else {
                setCStr_String(d->meta,  "audio/wave");
            }
            break;
        }
        default:
            setCStr_String(d->meta, "application/octet-stream");
            break;
    }
    d->isPre = iFalse;
    open_Socket(d->socket);
    writeData_Socket(d->socket, cstr_String(reqPath), size_String(reqPath));
    if (!isEmpty_Range(&parts.query)) {
        iAssert(*parts.query.start == '?');
        parts.query.start++;
        writeData_Socket(d->socket, "\t", 1);
        const iString *reqQuery =
            collect_String(urlDecode_String(collectNewRange_String(parts.query)));
        writeData_Socket(d->socket, cstr_String(reqQuery), size_String(reqQuery));
    }
    writeData_Socket(d->socket, "\r\n", 2);
}

void cancel_Gopher(iGopher *d) {
    if (d->socket) {
        close_Socket(d->socket);
    }
}

static iBool isMenuSyntax_Gopher_(const iGopher *d, iBool *isValidUtf8_out) {
    iRangecc buf = range_Block(d->output);
    if (!isUtf8_Rangecc(buf)) {
        if (isValidUtf8_out) *isValidUtf8_out = iFalse;
        return iFalse; /* could be binary, or another encoding... */
    }
    if (isValidUtf8_out) *isValidUtf8_out = iTrue;
    if (endsWith_Rangecc(buf, ".\r\n")) {
        buf.end -= 3; /* this is fine, but doesn't match the menu pattern */
    }
    iRegExp *pattern = makeMenuItemPattern_();
    iBool    isMenu  = iTrue;
    iRangecc line    = iNullRange;
    while (nextSplit_Rangecc(buf, "\r\n", &line)) {
        iRegExpMatch m;
        init_RegExpMatch(&m);
        if (!matchRange_RegExp(pattern, line, &m)) {
            isMenu = iFalse;
            break;
        }
    }
    iRelease(pattern);
    return isMenu;
}

iBool checkFormat_Gopher(iGopher *d) {
    iBool isValidUtf8 = iTrue;
    if (d->type != '1' && d->type != '7' && isMenuSyntax_Gopher_(d, &isValidUtf8)) {
        /* It looks like we actually received a gophermap! Let's convert it now. */
        setCStr_String(d->meta, "text/gophermap");
        set_Block(&d->source, d->output);
        clear_Block(d->output);
        convertSource_Gopher_(d);
        return iTrue;
    }
    else if (d->type == '0' && !isValidUtf8) {
        /* We expected text, but this is something else. */
        setCStr_String(d->meta, "application/octet-stream");
        return iTrue;
    }
    return iFalse;
}

iBool processResponse_Gopher(iGopher *d, const iBlock *data) {
    if (d->type == '1' || d->type == '7') {
        /* We expect the response is a gophermap. Converting as we receive allows us to
           present the page immediately. */
        iBool changed = iFalse;
        append_Block(&d->source, data);
        if (convertSource_Gopher_(d)) {
            changed = iTrue;
        }
        return changed;
    }
    else {
        const size_t oldSize = size_Block(d->output);
        append_Block(d->output, data);
        if (d->type == '0') {
            /* Text content will be terminated by `.`. */
            const char  *out = cstr_Block(d->output);
            const size_t n   = size_Block(d->output);
            if (n >= 3 && !memcmp(out + n - 3, ".\r\n", 3)) {
                truncate_Block(d->output, n - 3);
            }
        }
        return oldSize != size_Block(d->output);
    }
}

void setUrlItemType_Gopher(iString *url, char itemType) {
    iUrl parts;
    init_Url(&parts, url);
    if (equalCase_Rangecc(parts.scheme, "gopher")) {
        if (parts.path.start && size_Range(&parts.path) >= 2) {
            ((char *) parts.path.start)[1] = itemType;
        }
    }
}
