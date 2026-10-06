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
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/stream.h>
#include <the_Foundation/string.h>
#include <the_Foundation/time.h>

iDeclareType(Gempub)

iDeclareClass(Book)

struct Impl_Book {
    iObject  object;
    iString  id;          /* from the title and author */
    iString  title;
    iString  author;
    iString  fileName;    /* in the books directory; empty if not on the shelf */
    uint64_t size;
    int      chapter;     /* negative if not read yet */
    int      numChapters;
    float    chapterPos;  /* fraction of the chapter where the visible pages begin */
    iTime    timestamp;   /* last opened or read */
};

float           progress_Book       (const iBook *); /* 0...1 of the whole book */
const iString * path_Book           (const iBook *); /* NULL if not on the shelf */

/*----------------------------------------------------------------------------------------------*/

void    init_Books          (const char *saveDir);
void    deinit_Books        (void);
void    save_Books          (void);

void    serialize_Books     (iStream *outs);
void    deserialize_Books   (iStream *ins, enum iImportMethod);

const iString * dir_Books           (void);
iString *       makeId_Books        (const iString *title, const iString *author);
const iString * id_Books            (const iGempub *);
const iBook *   find_Books          (const iString *id);
iBool           isOnShelf_Books     (const iString *path);
const iPtrArray*list_Books          (void); /* books on the shelf, most recently read first */

void            update_Books        (const iGempub *, const iString *path);
void            setPosition_Books   (const iGempub *, int chapter, int numChapters, float pos);
const iString * add_Books           (const iBlock *gempubData, const iString *fileName,
                                     const iString *replacePath); /* returns the saved path */
void            remove_Books        (const iString *path);
