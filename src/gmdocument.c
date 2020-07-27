#include "gmdocument.h"
#include "ui/color.h"
#include "ui/text.h"
#include "ui/metrics.h"

#include <the_Foundation/ptrarray.h>
#include <the_Foundation/regexp.h>

iDeclareType(GmLink)

struct Impl_GmLink {
    iString url;
};

void init_GmLink(iGmLink *d) {
    init_String(&d->url);
}

void deinit_GmLink(iGmLink *d) {
    deinit_String(&d->url);
}

iDefineTypeConstruction(GmLink)

struct Impl_GmDocument {
    iObject object;
    iString source;
    iInt2 size;
    iArray layout; /* contents of source, laid out in document space */
    iPtrArray links;
    iString title; /* the first top-level title */
};

iDefineObjectConstruction(GmDocument)

enum iGmLineType {
    text_GmLineType,
    bullet_GmLineType,
    preformatted_GmLineType,
    quote_GmLineType,
    header1_GmLineType,
    header2_GmLineType,
    header3_GmLineType,
    link_GmLineType,
    max_GmLineType,
};

static enum iGmLineType lineType_Rangecc_(const iRangecc *line) {
    if (isEmpty_Range(line)) {
        return text_GmLineType;
    }
    if (startsWith_Rangecc(line, "=>")) {
        return link_GmLineType;
    }
    if (startsWith_Rangecc(line, "###")) {
        return header3_GmLineType;
    }
    if (startsWith_Rangecc(line, "##")) {
        return header2_GmLineType;
    }
    if (startsWith_Rangecc(line, "#")) {
        return header1_GmLineType;
    }
    if (startsWith_Rangecc(line, "```")) {
        return preformatted_GmLineType;
    }
    if (*line->start == '>') {
        return quote_GmLineType;
    }
    if (size_Range(line) >= 2 && line->start[0] == '*' && isspace(line->start[1])) {
        return bullet_GmLineType;
    }
    return text_GmLineType;
}

static void trimLine_Rangecc_(iRangecc *line, enum iGmLineType type) {
    static const unsigned int skip[max_GmLineType] = { 0, 2, 3, 1, 1, 2, 3, 0 };
    line->start += skip[type];
    trim_Rangecc(line);
}

static int lastVisibleRunBottom_GmDocument_(const iGmDocument *d) {
    iReverseConstForEach(Array, i, &d->layout) {
        const iGmRun *run = i.value;
        if (isEmpty_Range(&run->text)) {
            continue;
        }
        return bottom_Rect(run->bounds);
    }
    return 0;
}

iInt2 measurePreformattedBlock_GmDocument_(const iGmDocument *d, const char *start, int font) {
    const iRangecc content = { start, constEnd_String(&d->source) };
    iRangecc line = iNullRange;
    nextSplit_Rangecc(&content, "\n", &line);
    iAssert(startsWith_Rangecc(&line, "```"));
    iRangecc preBlock = { line.end + 1, line.end + 1 };
    while (nextSplit_Rangecc(&content, "\n", &line)) {
        if (startsWith_Rangecc(&line, "```")) {
            break;
        }
        preBlock.end = line.end;
    }
    return measureRange_Text(font, preBlock);
}

static iRangecc addLink_GmDocument_(iGmDocument *d, iRangecc line, iGmLinkId *linkId) {
    iRegExp *pattern = new_RegExp("=>\\s*([^\\s]+)(\\s.*)?", caseInsensitive_RegExpOption);
    iRegExpMatch m;
    if (matchRange_RegExp(pattern, line, &m)) {
        iGmLink *link = new_GmLink();
        setRange_String(&link->url, capturedRange_RegExpMatch(&m, 1));
        pushBack_PtrArray(&d->links, link);
        *linkId = size_PtrArray(&d->links); /* index + 1 */
        iRangecc desc = capturedRange_RegExpMatch(&m, 2);
        trim_Rangecc(&desc);
        if (!isEmpty_Range(&desc)) {
            line = desc; /* Just show the description. */
        }
        else {
            line = capturedRange_RegExpMatch(&m, 1); /* Show the URL. */
        }
    }
    iRelease(pattern);
    return line;
}

static void clearLinks_GmDocument_(iGmDocument *d) {
    iForEach(PtrArray, i, &d->links) {
        delete_GmLink(i.ptr);
    }
    clear_PtrArray(&d->links);
}

static void doLayout_GmDocument_(iGmDocument *d) {
    /* TODO: Collect these parameters into a GmTheme. */
    static const int fonts[max_GmLineType] = {
        paragraph_FontId,
        paragraph_FontId, /* bullet */
        preformatted_FontId,
        quote_FontId,
        header1_FontId,
        header2_FontId,
        header3_FontId,
        regular_FontId,
        };
    static const int colors[max_GmLineType] = {
        gray75_ColorId,
        gray75_ColorId,
        cyan_ColorId,
        gray75_ColorId,
        white_ColorId,
        white_ColorId,
        white_ColorId,
        white_ColorId,
        };
    static const int indents[max_GmLineType] = {
        4, 10, 4, 10, 0, 0, 0, 0
    };
    static const float topMargin[max_GmLineType] = {
        0.0f, 0.5f, 1.0f, 0.5f, 2.0f, 2.0f, 1.5f, 1.0f
    };
    static const float bottomMargin[max_GmLineType] = {
        0.0f, 0.5f, 1.0f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f
    };
    static const char *bullet = "\u2022";
    clear_Array(&d->layout);
    clearLinks_GmDocument_(d);
    clear_String(&d->title);
    if (d->size.x <= 0 || isEmpty_String(&d->source)) {
        return;
    }
    const iRangecc   content     = range_String(&d->source);
    iInt2            pos         = zero_I2();
    iRangecc         line        = iNullRange;
    iRangecc         preAltText  = iNullRange;
    enum iGmLineType prevType    = text_GmLineType;
    iBool            isPreformat = iFalse;
    iBool            isFirstText = iTrue;
    int              preFont     = preformatted_FontId;
    while (nextSplit_Rangecc(&content, "\n", &line)) {
        iGmRun run;
        run.color = white_ColorId;
        run.linkId = 0;
        enum iGmLineType type;
        int indent = 0;
        if (!isPreformat) {
            type = lineType_Rangecc_(&line);
            indent = indents[type];
            if (type == preformatted_GmLineType) {
                isPreformat = iTrue;
                preFont = preformatted_FontId;
                /* Use a smaller font if the block contents are wide. */
                if (measurePreformattedBlock_GmDocument_(d, line.start, preFont).x >
                    d->size.x - indents[preformatted_GmLineType]) {
                    preFont = preformattedSmall_FontId;
                }
                trimLine_Rangecc_(&line, type);
                preAltText = line;
                /* TODO: store and link the alt text to this run */
                continue;
            }
            else if (type == link_GmLineType) {
                line = addLink_GmDocument_(d, line, &run.linkId);
                if (!run.linkId) {
                    /* Invalid formatting. */
                    type = text_GmLineType;
                }
            }
            trimLine_Rangecc_(&line, type);
            run.font = fonts[type];
        }
        else {
            /* Preformatted line. */
            type = preformatted_GmLineType;
            if (startsWithSc_Rangecc(&line, "```", &iCaseSensitive)) {
                isPreformat = iFalse;
                preAltText = iNullRange;
                continue;
            }
            run.font = preFont;
            indent = indents[type];
        }
        /* Empty lines don't produce text runs. */
        if (isEmpty_Range(&line)) {
            pos.y += lineHeight_Text(run.font);
            prevType = text_GmLineType;
            continue;
        }
        /* Check the margin vs. previous run. */
        if (!isPreformat || (prevType != preformatted_GmLineType)) {
            int required =
                iMax(topMargin[type], bottomMargin[prevType]) * lineHeight_Text(paragraph_FontId);
            if ((type == link_GmLineType && prevType == link_GmLineType) ||
                (type == quote_GmLineType && prevType == quote_GmLineType)) {
                /* No margin between consecutive links/quote lines. */
                required = 0;
            }
            if (isEmpty_Array(&d->layout)) {
                required = 0; /* top of document */
            }
            int delta = pos.y - lastVisibleRunBottom_GmDocument_(d);
            if (delta < required) {
                pos.y += required - delta;
            }
        }
        /* Document title. */
        if (type == header1_GmLineType && isEmpty_String(&d->title)) {
            setRange_String(&d->title, line);
        }
        /* List bullet. */
        run.color = colors[type];
        if (type == bullet_GmLineType) {
            run.bounds.pos = addX_I2(pos, indent * gap_UI);
            run.bounds.size = advance_Text(run.font, bullet);
            run.bounds.pos.x -= 4 * gap_UI - run.bounds.size.x / 2;
            run.text = (iRangecc){ bullet, bullet + strlen(bullet) };
            pushBack_Array(&d->layout, &run);
        }
        /* Special formatting for the first paragraph (e.g., subtitle, introduction, or lede). */
        if (type == text_GmLineType && isFirstText) {
            run.font = firstParagraph_FontId;
            run.color = gray88_ColorId;
            isFirstText = iFalse;
        }
        else if (type != header1_GmLineType) {
            isFirstText = iFalse;
        }
        iRangecc runLine = line;
        /* Create one or more runs for this line. */
        while (!isEmpty_Range(&runLine)) {
            run.bounds.pos = addX_I2(pos, indent * gap_UI);
            const char *contPos;
            const int avail = d->size.x - run.bounds.pos.x;
            const iInt2 dims =
                tryAdvance_Text(run.font, runLine, isPreformat ? 0 : avail, &contPos);
            run.bounds.size.x = iMax(avail, dims.x); /* Extends to the right edge for selection. */
            run.bounds.size.y = dims.y;
            run.visBounds = run.bounds;
            run.visBounds.size.x = dims.x;
            if (contPos > runLine.start) {
                run.text = (iRangecc){ runLine.start, contPos };
            }
            else {
                run.text = runLine;
                contPos = runLine.end;
            }
            pushBack_Array(&d->layout, &run);
            runLine.start = contPos;
            trimStart_Rangecc(&runLine);
            pos.y += lineHeight_Text(run.font);
        }
        prevType = type;
    }
    d->size.y = pos.y;
}

void init_GmDocument(iGmDocument *d) {
    init_String(&d->source);
    d->size = zero_I2();
    init_Array(&d->layout, sizeof(iGmRun));
    init_PtrArray(&d->links);
    init_String(&d->title);
}

void deinit_GmDocument(iGmDocument *d) {
    deinit_String(&d->title);
    clearLinks_GmDocument_(d);
    deinit_PtrArray(&d->links);
    deinit_Array(&d->layout);
    deinit_String(&d->source);
}

void setWidth_GmDocument(iGmDocument *d, int width) {
    d->size.x = width;
    doLayout_GmDocument_(d); /* TODO: just flag need-layout and do it later */
}

iLocalDef iBool isNormalizableSpace_(char ch) {
    return ch == ' ' || ch == '\t';
}

static void normalize_GmDocument(iGmDocument *d) {
    iString *normalized = new_String();
    iRangecc src = range_String(&d->source);
    iRangecc line = iNullRange;
    iBool isPreformat = iFalse;
    const int preTabWidth = 8; /* TODO: user-configurable parameter */
    while (nextSplit_Rangecc(&src, "\n", &line)) {
        if (isPreformat) {
            /* Replace any tab characters with spaces for visualization. */
            for (const char *ch = line.start; ch != line.end; ch++) {
                if (*ch == '\t') {
                    int column = ch - line.start;
                    int numSpaces = (column / preTabWidth + 1) * preTabWidth - column;
                    while (numSpaces-- > 0) {
                        appendCStrN_String(normalized, " ", 1);
                    }
                }
                else {
                    appendCStrN_String(normalized, ch, 1);
                }
            }
            appendCStr_String(normalized, "\n");
            if (lineType_Rangecc_(&line) == preformatted_GmLineType) {
                isPreformat = iFalse;
            }
            continue;
        }
        if (lineType_Rangecc_(&line) == preformatted_GmLineType) {
            isPreformat = iTrue;
            appendRange_String(normalized, line);
            appendCStr_String(normalized, "\n");
            continue;
        }
        iBool isPrevSpace = iFalse;
        for (const char *ch = line.start; ch != line.end; ch++) {
            char c = *ch;
            if (isNormalizableSpace_(c)) {
                if (isPrevSpace) {
                    continue; /* skip repeated spaces */
                }
                c = ' ';
                isPrevSpace = iTrue;
            }
            else {
                isPrevSpace = iFalse;
            }
            appendCStrN_String(normalized, &c, 1);
        }
        appendCStr_String(normalized, "\n");
    }
    set_String(&d->source, collect_String(normalized));
//    printf("normalized:\n%s\n", cstr_String(&d->source));
}

void setSource_GmDocument(iGmDocument *d, const iString *source, int width) {
    set_String(&d->source, source);
    normalize_GmDocument(d);
    setWidth_GmDocument(d, width);
    /* TODO: just flag need-layout and do it later */
}

void render_GmDocument(const iGmDocument *d, iRangei visRangeY, iGmDocumentRenderFunc render,
                       void *context) {
    iBool isInside = iFalse;
    /* TODO: Check lookup table for quick starting position. */
    iConstForEach(Array, i, &d->layout) {
        const iGmRun *run = i.value;
        if (isInside) {
            if (top_Rect(run->bounds) > visRangeY.end) {
                break;
            }
            render(context, run);
        }
        else if (bottom_Rect(run->bounds) >= visRangeY.start) {
            isInside = iTrue;
            render(context, run);
        }
    }
}

iInt2 size_GmDocument(const iGmDocument *d) {
    return d->size;
}

iRangecc findText_GmDocument(const iGmDocument *d, const iString *text, const char *start) {
    const char * src      = constBegin_String(&d->source);
    const size_t startPos = (start ? start - src : 0);
    const size_t pos =
        indexOfCStrFromSc_String(&d->source, cstr_String(text), startPos, &iCaseInsensitive);
    if (pos == iInvalidPos) {
        return iNullRange;
    }
    return (iRangecc){ src + pos, src + pos + size_String(text) };
}

iRangecc findTextBefore_GmDocument(const iGmDocument *d, const iString *text, const char *before) {
    iRangecc found = iNullRange;
    const char *start = constBegin_String(&d->source);
    if (!before) before = constEnd_String(&d->source);
    while (start < before) {
        iRangecc range = findText_GmDocument(d, text, start);
        if (range.start == NULL || range.start >= before) break;
        found = range;
        start = range.end;
    }
    return found;
}

const iGmRun *findRun_GmDocument(const iGmDocument *d, iInt2 pos) {
    /* TODO: Perf optimization likely needed; use a block map? */
    iConstForEach(Array, i, &d->layout) {
        const iGmRun *run = i.value;
        if (contains_Rect(run->bounds, pos)) {
            return run;
        }
    }
    return NULL;
}

const char *findLoc_GmDocument(const iGmDocument *d, iInt2 pos) {
    const iGmRun *run = findRun_GmDocument(d, pos);
    if (run) {
        return findLoc_GmRun(run, pos);
    }
    return NULL;
}

const iGmRun *findRunAtLoc_GmDocument(const iGmDocument *d, const char *textCStr) {
    iConstForEach(Array, i, &d->layout) {
        const iGmRun *run = i.value;
        if (contains_Range(&run->text, textCStr) || run->text.start > textCStr /* went past */) {
            return run;
        }
    }
    return NULL;
}

const iString *linkUrl_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    if (linkId > 0 && linkId <= size_PtrArray(&d->links)) {
        const iGmLink *link = constAt_PtrArray(&d->links, linkId - 1);
        return &link->url;
    }
    return NULL;
}

const iString *title_GmDocument(const iGmDocument *d) {
    return &d->title;
}

const char *findLoc_GmRun(const iGmRun *d, iInt2 pos) {
    const int x = pos.x - left_Rect(d->bounds);
    const char *loc;
    tryAdvanceNoWrap_Text(d->font, d->text, x, &loc);
    return loc;
}

iDefineClass(GmDocument)
