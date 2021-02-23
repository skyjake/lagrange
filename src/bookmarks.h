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

#pragma once

#include <the_Foundation/hash.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/string.h>
#include <the_Foundation/time.h>

iDeclareType(GmRequest)

iDeclareType(Bookmark)
iDeclareTypeConstruction(Bookmark)

struct Impl_Bookmark {
    iHashNode node;
    iString url;
    iString title;
    iString tags;
    iChar icon;
    iTime when;
    uint32_t sourceId; /* remote */
};

iLocalDef uint32_t  id_Bookmark (const iBookmark *d) { return d->node.key; }

iBool   hasTag_Bookmark     (const iBookmark *d, const char *tag);
void    addTag_Bookmark     (iBookmark *d, const char *tag);
void    removeTag_Bookmark  (iBookmark *d, const char *tag);

/*----------------------------------------------------------------------------------------------*/

iDeclareType(Bookmarks)
iDeclareTypeConstruction(Bookmarks)

void        clear_Bookmarks             (iBookmarks *);
void        load_Bookmarks              (iBookmarks *, const char *dirPath);
uint32_t    add_Bookmarks               (iBookmarks *, const iString *url, const iString *title,
                                         const iString *tags, iChar icon);
iBool       remove_Bookmarks            (iBookmarks *, uint32_t id);
iBookmark * get_Bookmarks               (iBookmarks *, uint32_t id);
void        fetchRemote_Bookmarks       (iBookmarks *);
void        requestFinished_Bookmarks   (iBookmarks *, iGmRequest *req);
iBool       updateBookmarkIcon_Bookmarks(iBookmarks *, const iString *url, iChar icon);
iChar       siteIcon_Bookmarks          (const iBookmarks *, iRangecc hostName);

void        save_Bookmarks              (const iBookmarks *, const char *dirPath);
uint32_t    findUrl_Bookmarks           (const iBookmarks *, const iString *url); /* O(n) */

typedef iBool (*iBookmarksFilterFunc) (void *context, const iBookmark *);
typedef int   (*iBookmarksCompareFunc)(const iBookmark **, const iBookmark **);

iBool   filterTagsRegExp_Bookmarks      (void *regExp, const iBookmark *);

/**
 * Lists all or a subset of the bookmarks in a sorted array of Bookmark pointers.
 *
 * @param filter  Filter function to determine which bookmarks should be returned.
 *                If NULL, all bookmarks are listed.
 * @param cmp     Sort function that compares Bookmark pointers. If NULL, the
 *                returned list is sorted by descending creation time.
 *
 * @return Collected array of bookmarks. Caller does not get ownership of the
 * listed bookmarks.
 */
const iPtrArray *list_Bookmarks(const iBookmarks *, iBookmarksCompareFunc cmp,
                                iBookmarksFilterFunc filter, void *context);

enum iBookmarkListType {
    listByFolder_BookmarkListType,
    listByTag_BookmarkListType,
    listByCreationTime_BookmarkListType,
};

const iString * bookmarkListPage_Bookmarks  (const iBookmarks *, enum iBookmarkListType listType);
