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

#include "lagrange/builtin.h"

#include <lagrange/lang.h>

#include <the_Foundation/array.h>
#include <the_Foundation/object.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/xml.h>

static iRegExp *xmlMimePattern_(void) {
    static iRegExp *xmlMime_;
    if (!xmlMime_) {
        xmlMime_ = new_RegExp("(application|text)/(atom\\+)?xml", caseInsensitive_RegExpOption);
    }
    return xmlMime_;
}

iBool isAtomXml_Builtin(const iString *mime) {
    iRegExpMatch m;
    init_RegExpMatch(&m);
    return matchString_RegExp(xmlMimePattern_(), mime, &m);
}

iBlock *translateAtomXml_Builtin(const iString *mime, const iBlock *source,
                                 const iString *requestUrl) {
    iUnused(requestUrl); /* TODO: Use for what? */
    iRegExpMatch m;
    init_RegExpMatch(&m);
    if (!matchString_RegExp(xmlMimePattern_(), mime, &m)) {
        return NULL;
    }
    iBlock *      output = NULL;
    iXmlDocument *doc    = new_XmlDocument();
    iString       src;
    initBlock_String(&src, source); /* assume it's UTF-8 */
    if (!parse_XmlDocument(doc, &src)) {
        goto finished;
    }
    const iXmlElement *feed = &doc->root;
    if (!equal_Rangecc(feed->name, "feed")) {
        goto finished;
    }
    if (!equal_Rangecc(attribute_XmlElement(feed, "xmlns"), "http://www.w3.org/2005/Atom")) {
        goto finished;
    }
    iString *title = collect_String(decodedContent_XmlElement(child_XmlElement(feed, "title")));
    if (isEmpty_String(title)) {
        goto finished;
    }
    iString *subtitle = collect_String(decodedContent_XmlElement(child_XmlElement(feed, "subtitle")));
    iString out;
    init_String(&out);
    format_String(&out,
                  "20 text/gemini\r\n"
                  "# %s\n\n",
                  cstr_String(title));
    if (!isEmpty_String(subtitle)) {
        appendFormat_String(&out, "## %s\n\n", cstr_String(subtitle));
    }
    appendCStr_String(&out, cstr_Lang("feeds.atom.translated"));
    appendCStr_String(&out, "\n\n");
    iRegExp *datePattern =
        iClob(new_RegExp("^\\s*([0-9][0-9][0-9][0-9]-[0-1][0-9]-[0-3][0-9])(T|\\s).*",
                         caseSensitive_RegExpOption));
    iBeginCollect();
    iConstForEach(PtrArray, i, &feed->children) {
        iEndCollect();
        iBeginCollect();
        const iXmlElement *entry = i.ptr;
        if (!equal_Rangecc(entry->name, "entry")) {
            continue;
        }
        title = collect_String(decodedContent_XmlElement(child_XmlElement(entry, "title")));
        if (isEmpty_String(title)) {
            continue;
        }
        const iString *published =
            collect_String(decodedContent_XmlElement(child_XmlElement(entry, "published")));
        const iString *updated =
            collect_String(decodedContent_XmlElement(child_XmlElement(entry, "updated")));
        iRegExpMatch m;
        init_RegExpMatch(&m);
        if (!matchString_RegExp(datePattern, updated, &m)) {
            init_RegExpMatch(&m);
            if (!matchString_RegExp(datePattern, published, &m)) {
                continue;
            }
        }
        iRangecc url = iNullRange;
        iConstForEach(PtrArray, j, &entry->children) {
            const iXmlElement *link = j.ptr;
            if (!equal_Rangecc(link->name, "link")) {
                continue;
            }
            const iRangecc href = attribute_XmlElement(link, "href");
            const iRangecc rel  = attribute_XmlElement(link, "rel");
            const iRangecc type = attribute_XmlElement(link, "type");
            if (startsWithCase_Rangecc(href, "gemini:")) {
                url = href;
                /* We're happy with the first gemini URL. */
                /* TODO: Are we? */
                break;
            }
            url = href;
            iUnused(rel, type);
        }
        if (isEmpty_Range(&url)) {
            continue;
        }
        appendFormat_String(&out, "=> %s %s - %s\n",
                            cstr_Rangecc(url),
                            cstr_Rangecc(capturedRange_RegExpMatch(&m, 1)),
                            cstr_String(title));
    }
    iEndCollect();
    output = copy_Block(utf8_String(&out));
    deinit_String(&out);
finished:
    delete_XmlDocument(doc);
    deinit_String(&src);
    return output;
}

/*----------------------------------------------------------------------------------------------*/
/* Unwrap Paragraphs: joins hard-wrapped lines of text. In hard-wrapped text, a line break is
   kept if the next word would have fit on the line. Otherwise, only broken sentences are joined. */

enum {
    minWrapWidth_Unwrap           = 40,  /* shorter lines were broken on purpose */
    maxWrapWidth_Unwrap           = 100, /* longer lines were not hard-wrapped */
    minWrappedLines_Unwrap        = 4,
    minBrokenSentenceWidth_Unwrap = 60,  /* shorter may be poetry */
};

enum iUnwrapPattern {
    /* Matched against the content of every line. */
    alnum_UnwrapPattern,
    breakable_UnwrapPattern,
    columns_UnwrapPattern,
    sentenceEnd_UnwrapPattern,
    lowercaseStart_UnwrapPattern,
    noSpaceStart_UnwrapPattern,
    noSpaceEnd_UnwrapPattern,
    /* Used for parsing. */
    prefix_UnwrapPattern,
    firstWord_UnwrapPattern,
    max_UnwrapPattern,
};

/* These scripts are written without spaces. */
#define cjk_Unwrap_ "[\\p{Han}\\p{Hiragana}\\p{Katakana}\\x{3000}-\\x{303f}\\x{ff00}-\\x{ffef}]"

static const char *patterns_Unwrap_[max_UnwrapPattern] = {
    [alnum_UnwrapPattern]          = "[\\p{L}\\p{N}]",
    [breakable_UnwrapPattern]      = " |" cjk_Unwrap_,
    [columns_UnwrapPattern]        = "   |\\t|\\|.*\\|", /* aligned columns or table cells */
    [sentenceEnd_UnwrapPattern]    = "[.!?:;\\x{2026}\\x{3002}\\x{ff01}\\x{ff1f}]"
                                     "[\"')\\]*_\\x{2019}\\x{201d}\\x{bb}\\x{300d}\\x{300f}]*$",
    [lowercaseStart_UnwrapPattern] = "^\\p{Ll}",
    [noSpaceStart_UnwrapPattern]   = "^" cjk_Unwrap_,
    [noSpaceEnd_UnwrapPattern]     = "(" cjk_Unwrap_ "|[\\p{L}\\p{N}]-)$", /* or hyphenated word */
    [prefix_UnwrapPattern]         = "^\\s*((?:>\\s*)*)((?:[-+*\\x{2022}\\x{2013}]|\\d{1,3}[.)])"
                                     "\\s|)\\s*", /* quotes and list markers */
    [firstWord_UnwrapPattern]      = "^(" cjk_Unwrap_ "|\\S+)",
};

enum iUnwrapLineType {
    verbatim_UnwrapLineType,
    text_UnwrapLineType,
    quote_UnwrapLineType,
    listItem_UnwrapLineType,
};

iDeclareType(UnwrapLine)

struct Impl_UnwrapLine {
    enum iUnwrapLineType type;
    iRangecc line;      /* excluding newline */
    iRangecc content;   /* excluding prefix, indentation, and trailing whitespace */
    int      indent;    /* columns before content */
    int      width;     /* columns */
    int      firstWord; /* columns */
    int      depth;     /* quote nesting */
    iBool    isItem;    /* begins with a list marker */
    int      matches;   /* bit per iUnwrapPattern */
};

static int columns_Unwrap_(iRangecc text) {
    int cols = 0;
    while (text.start < text.end) {
        iChar     ch  = 0;
        const int len = decodeBytes_MultibyteChar(text.start, text.end, &ch);
        text.start += iMax(1, len);
        cols += (ch == '\t' ? 4 : iMax(0, width_Char(ch)));
    }
    return cols;
}

static iBool has_UnwrapLine_(const iUnwrapLine *d, enum iUnwrapPattern pattern) {
    return (d->matches & (1 << pattern)) != 0;
}

static iBool isProse_UnwrapLine_(const iUnwrapLine *d) {
    return has_UnwrapLine_(d, alnum_UnwrapPattern) && !has_UnwrapLine_(d, columns_UnwrapPattern);
}

static void parse_UnwrapLine_(iUnwrapLine *d, iRangecc line, iBool *isPreformat,
                              iRegExp *const *rx) {
    if (endsWith_Rangecc(line, "\r")) {
        line.end--;
    }
    iZap(*d);
    d->line = d->content = line;
    if (startsWith_Rangecc(line, "```")) {
        *isPreformat = !*isPreformat;
        return;
    }
    if (*isPreformat || startsWith_Rangecc(line, "=>") || startsWith_Rangecc(line, "=:") ||
        startsWith_Rangecc(line, "#") || !isUtf8_Rangecc(line)) {
        return;
    }
    iRangecc content = line;
    trimEnd_Rangecc(&content);
    iRegExpMatch m;
    init_RegExpMatch(&m);
    matchRange_RegExp(rx[prefix_UnwrapPattern], content, &m);
    const iRangecc quote  = capturedRange_RegExpMatch(&m, 1);
    const iRangecc marker = capturedRange_RegExpMatch(&m, 2);
    content.start = end_RegExpMatch(&m);
    if (isEmpty_Range(&content)) {
        return; /* paragraph break */
    }
    for (const char *pos = quote.start; pos < quote.end; pos++) {
        d->depth += (*pos == '>');
    }
    d->isItem  = !isEmpty_Range(&marker);
    d->type    = d->depth    ? quote_UnwrapLineType
                 : d->isItem ? listItem_UnwrapLineType
                             : text_UnwrapLineType;
    d->content = content;
    d->indent  = columns_Unwrap_((iRangecc){ line.start, content.start });
    d->width   = d->indent + columns_Unwrap_(content);
    for (int i = 0; i < prefix_UnwrapPattern; i++) {
        init_RegExpMatch(&m);
        d->matches |= (matchRange_RegExp(rx[i], content, &m) ? 1 << i : 0);
    }
    init_RegExpMatch(&m);
    matchRange_RegExp(rx[firstWord_UnwrapPattern], content, &m);
    d->firstWord = columns_Unwrap_(capturedRange_RegExpMatch(&m, 0));
}

static iBool continuesRun_UnwrapLine_(const iUnwrapLine *first, const iUnwrapLine *d) {
    if (first->type == listItem_UnwrapLineType) {
        return d->type == text_UnwrapLineType && d->indent > 0;
    }
    return first->type != verbatim_UnwrapLineType && d->type == first->type &&
           d->depth == first->depth;
}

static size_t runEnd_UnwrapLine_(const iUnwrapLine *line, size_t start, size_t numLines) {
    size_t end = start + 1;
    while (end < numLines && continuesRun_UnwrapLine_(&line[start], &line[end])) {
        end++;
    }
    return end;
}

static iBool isWrapCandidate_UnwrapLine_(const iUnwrapLine *d) {
    /* Unbreakable lines may have overflowed the wrap width (e.g., long URLs). */
    return isProse_UnwrapLine_(d) && has_UnwrapLine_(d, breakable_UnwrapPattern) &&
           d->width <= maxWrapWidth_Unwrap;
}

static int hardWrapWidth_UnwrapLine_(const iUnwrapLine *line, size_t numLines) {
    /* In hard-wrapped text, most lines that continue to the next line end up close to the
       wrap width. Returns zero if the text does not appear to be hard-wrapped. */
    int count = 0;
    int numWidths[maxWrapWidth_Unwrap + 1] = { 0 };
    for (size_t i = 0; i < numLines; ) {
        const size_t end = runEnd_UnwrapLine_(line, i, numLines);
        for (size_t j = i; j + 1 < end; j++) {
            if (isWrapCandidate_UnwrapLine_(&line[j])) {
                numWidths[line[j].width]++;
                count++;
            }
        }
        i = end;
    }
    if (count < minWrappedLines_Unwrap) {
        return 0;
    }
    int wrapWidth = 0, numBelow = numWidths[0]; /* 90th percentile */
    while (numBelow <= count * 9 / 10) {
        numBelow += numWidths[++wrapWidth];
    }
    int numNear = 0;
    for (int w = wrapWidth * 3 / 4; w <= maxWrapWidth_Unwrap; w++) {
        numNear += numWidths[w];
    }
    /* Short texts need stronger evidence. */
    const int minNearPercent = (count < 10 ? 80 : 55);
    return wrapWidth >= minWrapWidth_Unwrap && numNear * 100 >= count * minNearPercent
               ? wrapWidth
               : 0;
}

static int runWrapWidth_UnwrapLine_(const iUnwrapLine *run, size_t count, int hardWrapWidth) {
    /* Longer runs may be wrapped differently than the rest of the text. */
    int width = 0, numCandidates = 0;
    for (size_t i = 0; i < count; i++) {
        if (isWrapCandidate_UnwrapLine_(&run[i])) {
            width = iMax(width, run[i].width);
            numCandidates++;
        }
    }
    return numCandidates >= 3 ? width : iMax(width, hardWrapWidth);
}

static iBool isHangingIndent_UnwrapLine_(const iUnwrapLine *d, int indent) {
    /* A label followed by text that is aligned with the next line, e.g., "Note:  Text". */
    const char *pos = d->line.start + indent;
    return indent >= 2 && indent < d->content.end - d->line.start && pos - 2 >= d->content.start &&
           pos[-1] == ' ' && pos[-2] == ' ' && *pos != ' ';
}

static iBool isSoftBreak_UnwrapLine_(const iUnwrapLine *prev, const iUnwrapLine *d,
                                     const iUnwrapLine *next, int wrapWidth) {
    if (!isProse_UnwrapLine_(d) || !isProse_UnwrapLine_(next) || next->isItem) {
        return iFalse;
    }
    /* Indentation starts a new paragraph, unless only the first line was indented. */
    if (d->type == next->type &&
        ((next->indent > d->indent && !isHangingIndent_UnwrapLine_(d, next->indent)) ||
         (next->indent < d->indent && prev && prev->indent == d->indent))) {
        return iFalse;
    }
    if (!wrapWidth || d->width > maxWrapWidth_Unwrap) {
        /* Not hard-wrapped, but a sentence may still have been broken. */
        return d->width >= minBrokenSentenceWidth_Unwrap &&
               !has_UnwrapLine_(d, sentenceEnd_UnwrapPattern) &&
               has_UnwrapLine_(next, lowercaseStart_UnwrapPattern);
    }
    /* Not all wrappers fill lines greedily. */
    return next->width <= maxWrapWidth_Unwrap && wrapWidth >= minWrapWidth_Unwrap &&
           (d->width >= wrapWidth * 4 / 5 || d->width + 1 + next->firstWord > wrapWidth);
}

static void appendTail_UnwrapLine_(const iUnwrapLine *d, iBlock *output) {
    /* Unjoined lines keep their trailing whitespace. */
    appendData_Block(output, d->content.end, d->line.end - d->content.end);
    appendCStr_Block(output, "\n");
}

iBlock *unwrapParagraphs_Builtin(const iString *mime, const iBlock *body,
                                 const iString *requestUrl) {
    iUnused(requestUrl);
    iRegExp *rx[max_UnwrapPattern];
    iForIndices(i, rx) {
        rx[i] = iClob(new_RegExp(patterns_Unwrap_[i], caseSensitive_RegExpOption));
    }
    const iRangecc src = range_Block(body);
    iArray lines;
    init_Array(&lines, sizeof(iUnwrapLine));
    iBool    isPreformat = iFalse;
    iRangecc seg         = iNullRange;
    while (nextSplit_Rangecc(src, "\n", &seg)) {
        iUnwrapLine line;
        parse_UnwrapLine_(&line, seg, &isPreformat, rx);
        pushBack_Array(&lines, &line);
    }
    iBlock *output = new_Block(0);
    printf_Block(output, "20 %s\r\n", cstr_String(mime));
    if (startsWith_Rangecc(src, "\n")) {
        appendCStr_Block(output, "\n"); /* skipped by nextSplit_Rangecc */
    }
    const iUnwrapLine *line          = constData_Array(&lines);
    const size_t       numLines      = size_Array(&lines);
    const int          hardWrapWidth = hardWrapWidth_UnwrapLine_(line, numLines);
    for (size_t i = 0; i < numLines; ) {
        const size_t end = runEnd_UnwrapLine_(line, i, numLines);
        const int wrapWidth =
            hardWrapWidth ? runWrapWidth_UnwrapLine_(&line[i], end - i, hardWrapWidth) : 0;
        for (size_t j = i; j < end; j++) {
            const iUnwrapLine *ln   = &line[j];
            const iUnwrapLine *prev = (j > i + 1 ? ln - 2 : NULL);
            if (j > i && isSoftBreak_UnwrapLine_(prev, ln - 1, ln, wrapWidth)) {
                const iBool noSpace = has_UnwrapLine_(ln - 1, noSpaceEnd_UnwrapPattern) ||
                                      has_UnwrapLine_(ln, noSpaceStart_UnwrapPattern);
                appendCStr_Block(output, noSpace ? "" : " ");
                appendData_Block(output, ln->content.start, size_Range(&ln->content));
                continue;
            }
            if (j > i) {
                appendTail_UnwrapLine_(ln - 1, output);
            }
            appendData_Block(output, ln->line.start, ln->content.end - ln->line.start);
        }
        appendTail_UnwrapLine_(&line[end - 1], output);
        i = end;
    }
    if (numLines && !endsWith_Rangecc(src, "\n")) {
        popBack_Block(output);
    }
    deinit_Array(&lines);
    return output;
}
