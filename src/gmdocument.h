#pragma once

#include "gemini.h"
#include <the_Foundation/object.h>
#include <the_Foundation/rect.h>
#include <the_Foundation/string.h>

iDeclareType(GmRun)

typedef uint16_t iGmLinkId;

enum iGmLinkFlags {
    remote_GmLinkFlag = 0x1,
    http_GmLinkFlag   = 0x2,
    gopher_GmLinkFlag = 0x4,
    file_GmLinkFlag   = 0x8,
};

struct Impl_GmRun {
    iRangecc text;
    iRect bounds; /* used for hit testing, extends to edge */
    iRect visBounds; /* actual text bounds */
    uint8_t font;
    uint8_t color;
    iGmLinkId linkId; /* zero for non-links */
};

const char *    findLoc_GmRun   (const iGmRun *, iInt2 pos);

iDeclareClass(GmDocument)
iDeclareObjectConstruction(GmDocument)

void    setWidth_GmDocument     (iGmDocument *, int width);
void    setHost_GmDocument      (iGmDocument *, const iString *host); /* local host name */
void    setSource_GmDocument    (iGmDocument *, const iString *source, int width);

typedef void (*iGmDocumentRenderFunc)(void *, const iGmRun *);

void    render_GmDocument       (const iGmDocument *, iRangei visRangeY, iGmDocumentRenderFunc render, void *);
iInt2   size_GmDocument         (const iGmDocument *);

iRangecc        findText_GmDocument         (const iGmDocument *, const iString *text, const char *start);
iRangecc        findTextBefore_GmDocument   (const iGmDocument *, const iString *text, const char *before);

const iGmRun *  findRun_GmDocument      (const iGmDocument *, iInt2 pos);
const char *    findLoc_GmDocument      (const iGmDocument *, iInt2 pos);
const iGmRun *  findRunAtLoc_GmDocument (const iGmDocument *, const char *loc);
const iString * linkUrl_GmDocument      (const iGmDocument *, iGmLinkId linkId);
int             linkFlags_GmDocument    (const iGmDocument *, iGmLinkId linkId);
enum iColorId   linkColor_GmDocument    (const iGmDocument *, iGmLinkId linkId);
const iString * title_GmDocument        (const iGmDocument *);
