#include "gmdocument.h"
#include "ui/color.h"
#include "ui/text.h"
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

static void doLayout_GmDocument_(iGmDocument *d) {
    if (d->size.x <= 0 || isEmpty_String(&d->source)) {
        return;
    }
    clear_Array(&d->layout);
    iBool isPreformat = iFalse;
    iInt2 pos = zero_I2();
    const iRangecc content = range_String(&d->source);
    iRangecc line = iNullRange;
    const int fonts[max_GmLineType] = {
        paragraph_FontId,
        paragraph_FontId,
        preformatted_FontId,
        quote_FontId,
        header1_FontId,
        header2_FontId,
        header3_FontId
    };
    while (nextSplit_Rangecc(&content, "\n", &line)) {
        enum iGmLineType type = lineType_Rangecc_(&line);
        iGmRun run;
        run.text = line;
        run.font = fonts[type];
        run.color = white_ColorId;
        run.bounds.pos = pos;
        run.bounds.size = advanceN_Text(run.font, line.start, size_Range(&line));
        run.linkId = 0;
        pushBack_Array(&d->layout, &run);
        pos.y += run.bounds.size.y;
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

void setSource_GmDocument(iGmDocument *d, const iString *source, int width) {
    set_String(&d->source, source);
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
