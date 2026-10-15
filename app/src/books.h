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
#include <the_Foundation/stringarray.h>
#include <the_Foundation/time.h>

iDeclareType(Gempub)

iDeclareClass(Book)

enum iBooksSortOrder {
    recent_BooksSortOrder,
    title_BooksSortOrder,
    author_BooksSortOrder,
};

struct Impl_Book {
    iObject  object;
    iString  id;          /* from the title and author */
    iString  title;
    iString  author;
    iString  fileName;    /* in the books directory; empty if not downloaded */
    iString  url;         /* where the book can be downloaded from */
    iString  edition;     /* publication date of the latest known edition */
    iString  fileEdition; /* edition of the downloaded copy */
    uint64_t size;
    int      chapter;     /* negative if not read yet */
    int      numChapters;
    float    chapterPos;  /* fraction of the chapter where the visible pages begin */
    iTime    timestamp;   /* last opened or read */
    iTime    positionTime; /* position last changed while up to date with synced data */
    iTime    shelved;     /* put on the shelf; on it if later than `deleted` */
    iTime    deleted;     /* removed from the shelf */
    iTime    openedAt;    /* runtime only: when the current reading session began */
};

float           progress_Book       (const iBook *); /* 0...1 of the whole book */
iBool           isDownloadable_Book (const iBook *); /* source URL is available */
const iString * path_Book           (const iBook *); /* NULL if not on the shelf */
iBool           isObsolete_Book     (const iBook *); /* downloaded copy is off the shelf or an older edition */

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
const iBook *   findBySource_Books  (const iString *url, iBool isDownloaded);
iBool           isOnShelf_Books     (const iString *path);
const iString * sourceUrl_Books     (const iString *path); /* NULL if not on the shelf */
const iPtrArray*list_Books          (enum iBooksSortOrder); /* shelved, and downloaded or downloadable */
void            prune_Books         (void); /* forgets books that have been off the shelf for a while */
const iStringArray *listObsolete_Books(void); /* IDs of books with obsolete downloads */

void            update_Books        (const iGempub *, const iString *path);
void            beginReading_Books  (const iGempub *);
void            setPosition_Books   (const iGempub *, int chapter, int numChapters, float pos);
void            applySync_Books     (iStream *ins); /* replaces everything except local files */
const iPtrArray*listPositions_Books (iTime since); /* books whose position changed after `since` */
iBool           setSyncedPosition_Books(const iString *id, int chapter, int numChapters,
                                        float pos, iTime positionTime); /* if newer */
void            setSource_Books     (const iString *id, const iString *url);
void            resetPosition_Books (const iString *id); /* read from the beginning */
const iString * add_Books           (const iBlock *gempubData, const iString *fileName,
                                     const iString *replacePath,
                                     const iString *sourceUrl); /* returns the saved path */
void            removeDownload_Books(const iString *id); /* stays on the shelf if downloadable */
void            remove_Books        (const iString *id); /* off the shelf; the reading position is kept until pruned */
