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

#include "lagrange/listfile.h"
#include "lagrange/core.h"

#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/path.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *headerFormat_ListFile_(int format) {
    return format_CStr("list fmt:%d\n", format);
}

const char *header_ListFile(void) {
    return headerFormat_ListFile_(default_ListFileFormat);
}

iBool isHeader_ListFile(iRangecc line) {
    iRangecc word = iNullRange;
    return nextSplit_Rangecc(line, " ", &word) && equal_Rangecc(word, "list");
}

static iBool isIgnored_ListFile_(iRangecc *line) {
    trimStart_Rangecc(line);
    return isEmpty_Range(line) || *line->start == '#';
}

int format_ListFile(iRangecc src) {
    iRangecc line = iNullRange;
    while (nextSplit_Rangecc(src, "\n", &line)) {
        iRangecc text = line;
        if (isIgnored_ListFile_(&text)) {
            continue;
        }
        if (isHeader_ListFile(text)) {
            iRangecc word = iNullRange;
            while (nextSplit_Rangecc(text, " ", &word)) {
                if (startsWith_Rangecc(word, "fmt:")) {
                    const int format = atoi(cstr_Rangecc((iRangecc){ word.start + 4, word.end }));
                    return format > 0 ? format : v1_ListFileFormat;
                }
            }
        }
        break;
    }
    return v1_ListFileFormat;
}

iBool nextLine_ListFile(iRangecc src, iRangecc *line) {
    iBool checkHeader = (line->start == NULL);
    while (nextSplit_Rangecc(src, "\n", line)) {
        iRangecc text = *line;
        if (isIgnored_ListFile_(&text)) {
            continue;
        }
        if (checkHeader) {
            checkHeader = iFalse;
            if (isHeader_ListFile(text)) {
                continue;
            }
        }
        *line = text;
        return iTrue;
    }
    return iFalse;
}

static void removeHeader_ListFile_(iBlock *data) {
    const iRangecc src  = range_Block(data);
    iRangecc       line = iNullRange;
    while (nextSplit_Rangecc(src, "\n", &line)) {
        iRangecc text = line;
        if (isIgnored_ListFile_(&text)) {
            continue;
        }
        if (isHeader_ListFile(text)) {
            const char *end = iMin(line.end + 1, src.end);
            remove_Block(data, line.start - src.start, end - line.start);
        }
        break;
    }
}

iBool save_ListFile(const char *path, const iBlock *content) {
    return saveFormat_ListFile(path, default_ListFileFormat, content);
}

iBool saveFormat_ListFile(const char *path, int format, const iBlock *content) {
    const char *tmpPath = format_CStr("%s.tmp", path);
    iBool       ok      = iFalse;
    iFile      *f       = newCStr_File(tmpPath);
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        const char *header = headerFormat_ListFile_(format);
        ok = writeData_File(f, header, strlen(header)) == strlen(header) &&
             write_File(f, content) == size_Block(content);
        close_File(f);
    }
    iRelease(f);
    if (ok) {
        commitFile_Core(path, tmpPath);
    }
    else {
        fprintf(stderr, "[ListFile] failed to write \"%s\"\n", tmpPath);
        removePath_CStr(tmpPath);
    }
    return ok;
}

const char *migrate_ListFile(const char *dir, const char *fileName, const char *oldFileName) {
    const char *path = concatPath_CStr(dir, fileName);
    if (fileExistsCStr_FileInfo(path)) {
        return path;
    }
    const char *oldPath = concatPath_CStr(dir, oldFileName);
    iFile      *f       = iClob(newCStr_File(oldPath));
    if (!open_File(f, readOnly_FileMode | text_FileMode)) {
        return path;
    }
    iBlock *content = collect_Block(readAll_File(f));
    close_File(f);
    removeHeader_ListFile_(content);
    /* The old file is left as is. */
    if (save_ListFile(path, content) && fileExistsCStr_FileInfo(path)) {
        return path;
    }
    return oldPath;
}
