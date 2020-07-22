#include "gmdocument.h"
#include "ui/color.h"
#include "ui/text.h"
#include "ui/metrics.h"

#include <the_Foundation/array.h>

struct Impl_GmDocument {
    iObject object;
    iString source;
    iInt2 size;
    iArray layout; /* contents of source, laid out in document space */
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
    max_GmLineType,
};

static enum iGmLineType lineType_Rangecc_(const iRangecc *line) {
    if (isEmpty_Range(line)) {
        return text_GmLineType;
    }
    if (startsWithSc_Rangecc(line, "###", &iCaseSensitive)) {
        return header3_GmLineType;
    }
    if (startsWithSc_Rangecc(line, "##", &iCaseSensitive)) {
        return header2_GmLineType;
    }
    if (startsWithSc_Rangecc(line, "#", &iCaseSensitive)) {
        return header1_GmLineType;
    }
    if (startsWithSc_Rangecc(line, "```", &iCaseSensitive)) {
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
    static const unsigned int skip[max_GmLineType] = { 0, 2, 3, 1, 1, 2, 3 };
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

static void doLayout_GmDocument_(iGmDocument *d) {
    if (d->size.x <= 0 || isEmpty_String(&d->source)) {
        return;
    }
    clear_Array(&d->layout);
    iBool isPreformat = iFalse;
    iInt2 pos = zero_I2();
    const iRangecc content = range_String(&d->source);
    iRangecc line = iNullRange;
    static const int fonts[max_GmLineType] = {
        paragraph_FontId,
        paragraph_FontId, /* bullet */
        preformatted_FontId,
        quote_FontId,
        header1_FontId,
        header2_FontId,
        header3_FontId
    };
    static const int indents[max_GmLineType] = {
        4, 10, 4, 10, 0, 0, 0
    };
    static const float topMargin[max_GmLineType] = {
        0.0f, 0.5f, 1.0f, 0.5f, 2.0f, 1.5f, 1.0f
    };
    static const float bottomMargin[max_GmLineType] = {
        0.0f, 0.5f, 1.0f, 0.5f, 1.0f, 1.0f, 1.0f
    };
    static const char *bullet = "\u2022";
    iRangecc preAltText = iNullRange;
    enum iGmLineType prevType = text_GmLineType;
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
                trimLine_Rangecc_(&line, type);
                preAltText = line;
                /* TODO: store and link the alt text to this run */
                continue;
            }
            trimLine_Rangecc_(&line, type);
            run.font = fonts[type];
        }
        else {
            type = preformatted_GmLineType;
            if (startsWithSc_Rangecc(&line, "```", &iCaseSensitive)) {
                isPreformat = iFalse;
                preAltText = iNullRange;
                continue;
            }
            run.font = preformatted_FontId;
            indent = indents[type];
        }
        /* Check the margin. */
        if (isEmpty_Range(&line)) {
            pos.y += lineHeight_Text(run.font);
            prevType = text_GmLineType;
            continue;
        }
        if (!isPreformat || (prevType != preformatted_GmLineType)) {
            int required =
                iMax(topMargin[type], bottomMargin[prevType]) * lineHeight_Text(paragraph_FontId);
            if (isEmpty_Array(&d->layout)) {
                required = 0; /* top of document */
            }
            int delta = pos.y - lastVisibleRunBottom_GmDocument_(d);
            if (delta < required) {
                pos.y += required - delta;
            }
        }
        if (type == bullet_GmLineType) {
            run.bounds.pos = addX_I2(pos, indent * gap_UI);
            run.bounds.size = advance_Text(run.font, bullet);
            run.bounds.pos.x -= 4 * gap_UI - run.bounds.size.x / 2;
            run.text = (iRangecc){ bullet, bullet + strlen(bullet) };
            pushBack_Array(&d->layout, &run);
        }
        run.text = line;
        run.bounds.pos = addX_I2(pos, indent * gap_UI);
        run.bounds.size = advanceRange_Text(run.font, line);
        pushBack_Array(&d->layout, &run);
        pos.y += run.bounds.size.y;
        prevType = type;
    }
    d->size.y = pos.y;
}

void init_GmDocument(iGmDocument *d) {
    init_String(&d->source);
    d->size = zero_I2();
    init_Array(&d->layout, sizeof(iGmRun));
}

void deinit_GmDocument(iGmDocument *d) {
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
    while (nextSplit_Rangecc(&src, "\n", &line)) {
        if (isPreformat) {
            appendRange_String(normalized, line);
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
            if (isNormalizableSpace_(*ch)) {
                if (isPrevSpace) {
                    continue;
                }
                isPrevSpace = iTrue;
            }
            else {
                isPrevSpace = iFalse;
            }
            appendCStrN_String(normalized, ch, 1);
        }
        appendCStr_String(normalized, "\n");
    }
    set_String(&d->source, collect_String(normalized));
    printf("normalized:\n%s\n", cstr_String(&d->source));
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

iDefineClass(GmDocument)
