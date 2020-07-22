#pragma once

#include "gemini.h"
#include <the_Foundation/object.h>
#include <the_Foundation/rect.h>
#include <the_Foundation/string.h>

iDeclareType(GmRun)

struct Impl_GmRun {
    iRangecc text;
    iRect bounds; /* advance metrics */
    uint8_t font;
    uint8_t color;
    uint16_t linkId;
};

iDeclareType(GmDocument)
iDeclareClass(GmDocument)

iDeclareObjectConstruction(GmDocument)

void    setWidth_GmDocument     (iGmDocument *, int width);
void    setSource_GmDocument    (iGmDocument *, const iString *source, int width);

typedef void (*iGmDocumentRenderFunc)(void *, const iGmRun *);

void    render_GmDocument       (const iGmDocument *, iRangei visRangeY, iGmDocumentRenderFunc render, void *);
iInt2   size_GmDocument         (const iGmDocument *);
