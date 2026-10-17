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

#pragma once

#include "defs.h"
#include <the_Foundation/block.h>
#include <the_Foundation/range.h>

/* Line-based user data files (.lst). The first line is a header: "list fmt:N". Empty lines
   and comment lines starting with "#" are ignored. Files without a header are format 1.
   The format number is specific to each file. */

enum iListFileFormat {
    v1_ListFileFormat      = 1,
    v2_ListFileFormat      = 2,
    default_ListFileFormat = v1_ListFileFormat,
};

const char *header_ListFile     (void); /* default format, with newline */
iBool       isHeader_ListFile   (iRangecc line);
int         format_ListFile     (iRangecc src); /* from the header; 1 if there is none */
iBool       nextLine_ListFile   (iRangecc src, iRangecc *line); /* like nextSplit_Rangecc */
iBool       save_ListFile       (const char *path, const iBlock *content); /* header is prepended */
iBool       saveFormat_ListFile (const char *path, int format, const iBlock *content);
const char *migrate_ListFile    (const char *dir, const char *fileName,
                                 const char *oldFileName); /* returns path to read */
