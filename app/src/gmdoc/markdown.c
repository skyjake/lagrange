/* Copyright 2026 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "markdown.h"

#include <the_Foundation/regexp.h>

#include <ctype.h>

iDeclareType(PendingLink)
struct Impl_PendingLink {
    iString *url;
    iString *title;
};

static void addPendingLink_(void *context, const iRegExpMatch *m) {
    pushBack_Array(context, &(iPendingLink){
        .url   = captured_RegExpMatch(m, 2),
        .title = captured_RegExpMatch(m, 1)
    });
}

static void addPendingNamedLink_(void *context, const iRegExpMatch *m) {
    pushBack_Array(context, &(iPendingLink){
        .url   = newFormat_String("[]%s", cstr_Rangecc(capturedRange_RegExpMatch(m, 2))),
        .title = captured_RegExpMatch(m, 1)
    });
}

static void flushPendingLinks_(iArray *links, const iString *source, iString *out) {
    iRegExp *namePattern = new_RegExp("\n\\s*\\[(.+?)\\]\\s*:\\s*([^\n]+)", 0);
    if (!endsWith_String(out, "\n")) {
        appendCStr_String(out, "\n");
    }
    iForEach(Array, i, links) {
        iPendingLink *pending = i.value;
        const char *url = cstr_String(pending->url);
        if (startsWith_CStr(url, "[]")) {
            /* Find the matching named link. */
            iRegExpMatch m;
            init_RegExpMatch(&m);
            while (matchString_RegExp(namePattern, source, &m)) {
                if (equal_Rangecc(capturedRange_RegExpMatch(&m, 1), url + 2)) {
                    url = cstrCollect_String(captured_RegExpMatch(&m, 2));
                    break;
                }
            }
        }
        appendFormat_String(out, "\n=> %s %s", url, cstr_String(pending->title));
        delete_String(pending->url);
        delete_String(pending->title);
    }
    clear_Array(links);
    iRelease(namePattern);
}

void convertMarkdownToGemtext_GmDocument_(iGmDocument *d) {
    iAssert(d->origFormat == markdown_SourceFormat);
    /* Get rid of indented preformats. */ {
        iArray        *pendingLinks     = collectNew_Array(sizeof(iPendingLink));
        const iRegExp *imageLinkPattern = iClob(new_RegExp("\n?!\\[(.+)\\]\\(([^)]+)\\)\n?", 0));
        const iRegExp *linkPattern      = iClob(new_RegExp("\\[(.+?)\\]\\(([^)]+)\\)", 0));
        const iRegExp *standaloneLinkPattern = iClob(new_RegExp("^[\\s*_]*\\[(.+?)\\]\\(([^)]+)\\)[\\s*_]*$", 0));
        const iRegExp *namedLinkPattern = iClob(new_RegExp("\\[(.+?)\\]\\[(.+?)\\]", 0));
        const iRegExp *namePattern      = iClob(new_RegExp("\\s*\\[(.+?)\\]\\s*:\\s*([^\n]+)", 0));
        iString result;
        init_String(&result);
        replace_String(&d->source, "&nbsp;", "\u00a0");
        replaceRegExp_String(&d->source, iClob(new_RegExp("```", 0)), "\n```\n", NULL, NULL);
        iRangecc line = iNullRange;
        iBool isPre = iFalse;
        iBool isBlock = iFalse;
        iBool isLastEmpty = iFalse;
        while (nextSplit_Rangecc(range_String(&d->source), "\n", &line)) {
            if (!isPre && !isBlock) {
                if (equal_Rangecc(line, "```")) {
                    isBlock = iTrue;
                    appendCStr_String(&result, "\n```");
                    continue;
                }
                if (*line.start == '#') {
                    flushPendingLinks_(pendingLinks, &d->source, &result);
                }
                if (isEmpty_Range(&line)) {
                    isLastEmpty = iTrue;
                    continue;
                }
                if (isLastEmpty) {
                    appendCStr_String(&result, "\n\n");
                }
                else if (size_Range(&line) >= 2 && isdigit(line.start[0]) &&
                         (line.start[1] == '.' ||
                          (isdigit(line.start[1]) && line.start[2] == '.'))) {
                    appendCStr_String(&result, "\n\n");
                }
                else if (endsWith_String(&result, "  ") ||
                         *line.start == '*' || *line.start == '>' || *line.start == '#' ||
                         (*line.start == '|' && endsWith_String(&result, "|"))) {
                    appendCStr_String(&result, "\n");
                }
                else {
                    appendCStr_String(&result, " ");
                }
                isLastEmpty = iFalse;
            }
            else if (isBlock) {
                if (equal_Rangecc(line, "```")) {
                    isBlock = iFalse;
                    appendCStr_String(&result, "\n```\n");
                }
                else {
                    appendCStr_String(&result, "\n");
                    appendRange_String(&result, line);
                }
                continue;
            }
            if (startsWith_Rangecc(line, "    ")) {
                line.start += 4;
                if (!isPre) {
                    while (!isEmpty_String(&result) &&
                           (last_String(&result) == ' ' || last_String(&result) == '\t')) {
                        removeEnd_String(&result, 1);
                    }
                    appendCStr_String(&result, "```\n");
                    isPre = iTrue;
                }
            }
            else if (isPre) {
                if (!endsWith_String(&result, "\n")) {
                    appendCStr_String(&result, "\n");
                }
                appendCStr_String(&result, "```\n");
                if (equal_Rangecc(line, "```")) {
                    line.start = line.end; /* don't repeat it */
                }
                isPre = iFalse;
            }
            if (isPre) {
                appendRange_String(&result, line);
                appendCStr_String(&result, "\n");
            }
            else {
                iString ln;
                initRange_String(&ln, line);
                replaceRegExp_String(&ln, namePattern, "", NULL, 0);
                replaceRegExp_String(&ln, standaloneLinkPattern, "\n=> \\2 \\1", NULL, NULL);
                replaceRegExp_String(&ln, imageLinkPattern, "\n=> \\2 \\1\n", NULL, NULL);
                replaceRegExp_String(&ln, namedLinkPattern, "\\1", addPendingNamedLink_, pendingLinks);
                replaceRegExp_String(&ln, linkPattern, "\\1", addPendingLink_, pendingLinks);
                replaceRegExp_String(&ln, iClob(new_RegExp("\\*\\*(.+?)\\*\\*", 0)), "\x1b[1m\\1\x1b[0m", NULL, NULL);
                replaceRegExp_String(&ln, iClob(new_RegExp("__(.+?)__", 0)), "\x1b[1m\\1\x1b[0m", NULL, NULL);
                replaceRegExp_String(&ln, iClob(new_RegExp("\\*(.+?)\\*", 0)), "\x1b[3m\\1\x1b[0m", NULL, NULL);
                replaceRegExp_String(&ln, iClob(new_RegExp("\\b_([^_]+?)_\\b", 0)), "\x1b[3m\\1\x1b[0m", NULL, NULL);
                replaceRegExp_String(&ln, iClob(new_RegExp("(?<!`)`([^`]+?)`(?!`)", 0)), "\x1b[11m\\1\x1b[0m", NULL, NULL);
                replace_String(&ln, "\\_", "_");
                append_String(&result, &ln);
                deinit_String(&ln);
            }
        }
        flushPendingLinks_(pendingLinks, &d->source, &result);
        set_String(&d->source, &result);
        deinit_String(&result);
    }
    /* Replace Markdown syntax with equivalent Gemtext, where possible. */
    replaceRegExp_String(&d->source, iClob(new_RegExp("(\\s*\n){2,}", 0)), "\n\n", NULL, NULL); /* normalize paragraph breaks */
    // printf("Converted:\n%s", cstr_String(&d->source));
    d->format = gemini_SourceFormat;
}
