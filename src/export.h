/* Copyright 2022 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "defs.h"
#include <the_Foundation/archive.h>

extern const char *mimeType_Export;

iDeclareType(Export)
iDeclareTypeConstruction(Export)

enum iExportFlags {
    bookmarks_ExportFlag          = iBit(1),
    identitiesAndTrust_ExportFlag = iBit(2),
    visited_ExportFlag            = iBit(3),
    siteSpec_ExportFlag           = iBit(4),
    everything_ExportFlag         = 0xff,
};
    
void    generate_Export         (iExport *);
void    generatePartial_Export  (iExport *, int dataFlags);

iBool   load_Export     (iExport *, const iArchive *archive);
void    import_Export   (const iExport *,
                         enum iImportMethod bookmarks,
                         enum iImportMethod identities,
                         enum iImportMethod trusted,
                         enum iImportMethod visited,
                         enum iImportMethod siteSpec);

iBool   detect_Export   (const iArchive *);

const iArchive *    archive_Export  (const iExport *);
