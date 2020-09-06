/* Copyright 2020 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "bookmarks.h"

#include <the_Foundation/file.h>
#include <the_Foundation/hash.h>
#include <the_Foundation/mutex.h>
#include <the_Foundation/path.h>

void init_Bookmark(iBookmark *d) {
    init_String(&d->url);
    init_String(&d->title);
    init_String(&d->tags);
    iZap(d->when);
}

void deinit_Bookmark(iBookmark *d) {
    deinit_String(&d->tags);
    deinit_String(&d->title);
    deinit_String(&d->url);
}

iDefineTypeConstruction(Bookmark)

static int cmpTimeDescending_Bookmark_(const iBookmark **a, const iBookmark **b) {
    return iCmp(seconds_Time(&(*b)->when), seconds_Time(&(*a)->when));
}

/*----------------------------------------------------------------------------------------------*/

static const char *fileName_Bookmarks_ = "bookmarks.txt";

struct Impl_Bookmarks {
    iMutex *mtx;
    int     idEnum;
    iHash   bookmarks;
};

iDefineTypeConstruction(Bookmarks)

void init_Bookmarks(iBookmarks *d) {
    d->mtx = new_Mutex();
    d->idEnum = 0;
    init_Hash(&d->bookmarks);
}

void deinit_Bookmarks(iBookmarks *d) {
    clear_Bookmarks(d);
    deinit_Hash(&d->bookmarks);
    delete_Mutex(d->mtx);
}

void clear_Bookmarks(iBookmarks *d) {
    lock_Mutex(d->mtx);
    iForEach(Hash, i, &d->bookmarks) {
        delete_Bookmark((iBookmark *) i.value);
    }
    clear_Hash(&d->bookmarks);
    d->idEnum = 0;
    unlock_Mutex(d->mtx);
}

static void insert_Bookmarks_(iBookmarks *d, iBookmark *bookmark) {
    lock_Mutex(d->mtx);
    bookmark->node.key = ++d->idEnum;
    insert_Hash(&d->bookmarks, &bookmark->node);
    unlock_Mutex(d->mtx);
}

void load_Bookmarks(iBookmarks *d, const char *dirPath) {
    clear_Bookmarks(d);
    iFile *f = newCStr_File(concatPath_CStr(dirPath, fileName_Bookmarks_));
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        const iRangecc src = range_Block(collect_Block(readAll_File(f)));
        iRangecc line = iNullRange;
        while (nextSplit_Rangecc(src, "\n", &line)) {
            /* Skip empty lines. */ {
                iRangecc ln = line;
                trim_Rangecc(&ln);
                if (isEmpty_Range(&ln)) {
                    continue;
                }
            }
            iBookmark *bm = new_Bookmark();
            bm->icon = strtoul(line.start, NULL, 16);
            line.start += 9;
            char *endPos;
            initSeconds_Time(&bm->when, strtod(line.start, &endPos));
            line.start = skipSpace_CStr(endPos);
            setRange_String(&bm->url, line);
            nextSplit_Rangecc(src, "\n", &line);
            setRange_String(&bm->title, line);
            nextSplit_Rangecc(src, "\n", &line);
            setRange_String(&bm->tags, line);
            insert_Bookmarks_(d, bm);
        }
    }
    iRelease(f);
}

void save_Bookmarks(const iBookmarks *d, const char *dirPath) {
    lock_Mutex(d->mtx);
    iFile *f = newCStr_File(concatPath_CStr(dirPath, fileName_Bookmarks_));
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        iString *str = collectNew_String();
        iConstForEach(Hash, i, &d->bookmarks) {
            const iBookmark *bm = (const iBookmark *) i.value;
            format_String(str,
                          "%08x %lf %s\n%s\n%s\n",
                          bm->icon,
                          seconds_Time(&bm->when),
                          cstr_String(&bm->url),
                          cstr_String(&bm->title),
                          cstr_String(&bm->tags));
            writeData_File(f, cstr_String(str), size_String(str));
        }
    }
    iRelease(f);
    unlock_Mutex(d->mtx);
}

void add_Bookmarks(iBookmarks *d, const iString *url, const iString *title, const iString *tags,
                   iChar icon) {
    lock_Mutex(d->mtx);
    iBookmark *bm = new_Bookmark();
    set_String(&bm->url, url);
    set_String(&bm->title, title);
    set_String(&bm->tags, tags);
    bm->icon = icon;
    initCurrent_Time(&bm->when);
    insert_Bookmarks_(d, bm);
    unlock_Mutex(d->mtx);
}

iBool remove_Bookmarks(iBookmarks *d, uint32_t id) {
    lock_Mutex(d->mtx);
    iBookmark *bm = (iBookmark *) remove_Hash(&d->bookmarks, id);
    if (bm) {
        delete_Bookmark(bm);
    }
    unlock_Mutex(d->mtx);
    return bm != NULL;
}

iBookmark *get_Bookmarks(iBookmarks *d, uint32_t id) {
    return (iBookmark *) value_Hash(&d->bookmarks, id);
}

const iPtrArray *list_Bookmarks(const iBookmarks *d, iBookmarksCompareFunc cmp,
                                iBookmarksFilterFunc filter, void *context) {
    lock_Mutex(d->mtx);
    iPtrArray *list = collectNew_PtrArray();
    iConstForEach(Hash, i, &d->bookmarks) {
        const iBookmark *bm = (const iBookmark *) i.value;
        if (!filter || filter(context, bm)) {
            pushBack_PtrArray(list, bm);
        }
    }
    unlock_Mutex(d->mtx);
    if (!cmp) cmp = cmpTimeDescending_Bookmark_;
    sort_Array(list, (int (*)(const void *, const void *)) cmp);
    return list;
}
