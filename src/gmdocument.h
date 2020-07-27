#pragma once

#include "gemini.h"
#include <the_Foundation/object.h>
#include <the_Foundation/rect.h>
#include <the_Foundation/string.h>

iDeclareType(GmRun)

typedef uint16_t iGmLinkId;

struct Impl_GmRun {
    iRangecc text;
    iRect bounds; /* advance metrics */
    uint8_t font;
    uint8_t color;
    iGmLinkId linkId; /* zero for non-links */
};

iDeclareClass(GmDocument)
iDeclareObjectConstruction(GmDocument)

void    setWidth_GmDocument     (iGmDocument *, int width);
void    setSource_GmDocument    (iGmDocument *, const iString *source, int width);

typedef void (*iGmDocumentRenderFunc)(void *, const iGmRun *);

void    render_GmDocument       (const iGmDocument *, iRangei visRangeY, iGmDocumentRenderFunc render, void *);
iInt2   size_GmDocument         (const iGmDocument *);

iRangecc        findText_GmDocument         (const iGmDocument *, const iString *text, const char *start);
iRangecc        findTextBefore_GmDocument   (const iGmDocument *, const iString *text, const char *before);

const iGmRun *  findRun_GmDocument      (const iGmDocument *, iInt2 pos);
const iGmRun *  findRunCStr_GmDocument  (const iGmDocument *, const char *textCStr);
const iString * linkUrl_GmDocument      (const iGmDocument *, iGmLinkId linkId);
const iString * title_GmDocument        (const iGmDocument *);
