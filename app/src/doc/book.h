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

#include <the_Foundation/string.h>

iDeclareType(DocumentWidget)

/* Reading a book: chapter navigation, reading position, and the book's progress bar. */

enum iDocumentBookFlag {
    readingOn_DocumentBookFlag           = iBit(1), /* next chapter of a book is opening */
    showLastPage_DocumentBookFlag        = iBit(2), /* new document opens at its last page */
    animateChapterSwipe_DocumentBookFlag = iBit(3), /* chapter slides in when loaded */
};

iDeclareType(DocumentBook)
iDeclareTypeConstruction(DocumentBook)

struct Impl_DocumentBook {
    iDocumentWidget *owner;
    int              flags;             /* see enum iDocumentBookFlag */
    size_t           pendingChapter;    /* being opened via the progress bar */
    size_t           progressChapter;   /* shown in the progress bar */
    uint32_t         progressAnimEnd;   /* moving to another chapter until this time */
    float            pendingChapterPos; /* where the visible pages end; negative if none */
    float            resumeChapterPos;  /* where the visible pages begin; negative if none */
    size_t           savedChapter;      /* book position last saved */
    size_t           savedColumn;
    iString          readingBookId;     /* book being read in this tab */
    iBool            didTurnPage;       /* since the book was opened */
    iString          offeredEdition;    /* user was asked about updating to this edition */
};

void    setOwner_DocumentBook           (iDocumentBook *, iDocumentWidget *owner);

iBool   isReadingOn_DocumentBook        (const iDocumentBook *); /* adjacent chapter opening */
int     chapterSwipeDir_DocumentBook    (const iDocumentBook *, const char *cmd);

void    resume_DocumentBook             (iDocumentBook *, size_t chapter, float chapterPos);
void    resetSavedPosition_DocumentBook (iDocumentBook *); /* new content is coming */
void    applyResume_DocumentBook        (iDocumentBook *);
void    beginPosition_DocumentBook      (iDocumentBook *);
void    didFinishRequest_DocumentBook   (iDocumentBook *);
void    checkSyncedPosition_DocumentBook(iDocumentBook *);
void    checkEdition_DocumentBook       (iDocumentBook *); /* updates to a newer edition */
void    jumpToPosition_DocumentBook     (iDocumentBook *, size_t chapter, float pos);
iBool   openAdjacentChapter_DocumentBook(iDocumentBook *, int dir, iBool isGesture);
iBool   moveProgress_DocumentBook       (iDocumentBook *, int thumb); /* progress bar dragged */
iBool   updateProgress_DocumentBook     (iDocumentBook *); /* False if not showing a chapter */
void    saveToShelf_DocumentBook        (iDocumentBook *, iBool overwrite);
void    openFromShelf_DocumentBook      (iDocumentBook *, const iString *path, iBool isReplaced);

iDocumentWidget *findTab_DocumentBook   (const iString *bookId, const iString *path); /* either may be NULL */
void    tidyShelf_DocumentBook          (void); /* removes obsolete downloads and prunes books */
