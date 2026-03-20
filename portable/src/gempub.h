/* Copyright 2021 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#pragma once

#include <the_Foundation/string.h>

iDeclareType(GmDocument)

iDeclareType(Gempub)
iDeclareTypeConstruction(Gempub)
        
enum iGempubProperty {
    title_GempubProperty,
    index_GempubProperty,
    author_GempubProperty,
    language_GempubProperty,
    description_GempubProperty,
    published_GempubProperty,
    publishDate_GempubProperty,
    revisionDate_GempubProperty,
    copyright_GempubProperty,
    license_GempubProperty,
    version_GempubProperty,
    cover_GempubProperty,            
    max_GempubProperty
};    
    
iBool       open_Gempub             (iGempub *, const iBlock *data);
iBool       openFile_Gempub         (iGempub *, const iString *path);
iBool       openUrl_Gempub          (iGempub *, const iString *url);
void        close_Gempub            (iGempub *);

void        setBaseUrl_Gempub       (iGempub *, const iString *baseUrl);

iBool       isOpen_Gempub           (const iGempub *);
iBool       isRemote_Gempub         (const iGempub *);
iString *   coverPageSource_Gempub  (const iGempub *);
iBool       preloadCoverImage_Gempub(const iGempub *, iGmDocument *doc);

const iString * property_Gempub         (const iGempub *, enum iGempubProperty);
const iString * coverPageUrl_Gempub     (const iGempub *);
const iString * indexPageUrl_Gempub     (const iGempub *);
const iString * navStartLinkUrl_Gempub  (const iGempub *); /* for convenience */
size_t          navSize_Gempub          (const iGempub *);
size_t          navIndex_Gempub         (const iGempub *, const iString *url);
const iString * navLinkUrl_Gempub       (const iGempub *, size_t index);
const iString * navLinkLabel_Gempub     (const iGempub *, size_t index);

extern const char *mimeType_Gempub;
