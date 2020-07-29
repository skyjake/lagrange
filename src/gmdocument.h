#pragma once

#include "gemini.h"
#include <the_Foundation/object.h>
#include <the_Foundation/rect.h>
#include <the_Foundation/string.h>

#include <SDL_render.h>

iDeclareType(GmRun)

typedef uint16_t iGmLinkId;

enum iGmLinkFlags {
    userFriendly_GmLinkFlag       = 0x1,
    remote_GmLinkFlag             = 0x2,
    http_GmLinkFlag               = 0x4,
    gopher_GmLinkFlag             = 0x8,
    file_GmLinkFlag               = 0x10,
    imageFileExtension_GmLinkFlag = 0x20,
    audioFileExtension_GmLinkFlag = 0x40,
    content_GmLinkFlag            = 0x80, /* content visible below */
};

iDeclareType(GmImageInfo)

struct Impl_GmImageInfo {
    iInt2 size;
    size_t numBytes;
    const char *mime;
};

struct Impl_GmRun {
    iRangecc  text;
    uint8_t   font;
    uint8_t   color;
    iRect     bounds;    /* used for hit testing, may extend to edges */
    iRect     visBounds; /* actual visual bounds */
    iGmLinkId linkId;    /* zero for non-links */
    uint16_t  imageId;   /* zero for images */
};

const char *    findLoc_GmRun   (const iGmRun *, iInt2 pos);

iDeclareClass(GmDocument)
iDeclareObjectConstruction(GmDocument)

enum iGmDocumentFormat {
    gemini_GmDocumentFormat,
    plainText_GmDocumentFormat,
};

void    setFormat_GmDocument    (iGmDocument *, enum iGmDocumentFormat format);
void    setWidth_GmDocument     (iGmDocument *, int width);
void    setHost_GmDocument      (iGmDocument *, const iString *host); /* local host name */
void    setSource_GmDocument    (iGmDocument *, const iString *source, int width);
void    setImage_GmDocument     (iGmDocument *, iGmLinkId linkId, const iString *mime, const iBlock *data);

void    reset_GmDocument        (iGmDocument *); /* free images */

typedef void (*iGmDocumentRenderFunc)(void *, const iGmRun *);

void    render_GmDocument       (const iGmDocument *, iRangei visRangeY, iGmDocumentRenderFunc render, void *);
iInt2   size_GmDocument         (const iGmDocument *);

iRangecc        findText_GmDocument         (const iGmDocument *, const iString *text, const char *start);
iRangecc        findTextBefore_GmDocument   (const iGmDocument *, const iString *text, const char *before);

const iGmRun *  findRun_GmDocument      (const iGmDocument *, iInt2 pos);
const char *    findLoc_GmDocument      (const iGmDocument *, iInt2 pos);
const iGmRun *  findRunAtLoc_GmDocument (const iGmDocument *, const char *loc);
const iString * linkUrl_GmDocument      (const iGmDocument *, iGmLinkId linkId);
uint16_t        linkImage_GmDocument    (const iGmDocument *, iGmLinkId linkId);
int             linkFlags_GmDocument    (const iGmDocument *, iGmLinkId linkId);
enum iColorId   linkColor_GmDocument    (const iGmDocument *, iGmLinkId linkId);
iBool           isMediaLink_GmDocument  (const iGmDocument *, iGmLinkId linkId);
const iString * title_GmDocument        (const iGmDocument *);

SDL_Texture *   imageTexture_GmDocument (const iGmDocument *, uint16_t imageId);
void            imageInfo_GmDocument    (const iGmDocument *, uint16_t imageId, iGmImageInfo *info_out);
