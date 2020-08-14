#pragma once

#include <the_Foundation/hash.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/string.h>
#include <the_Foundation/time.h>

iDeclareType(Bookmark)
iDeclareTypeConstruction(Bookmark)

struct Impl_Bookmark {
    iHashNode node;
    iString url;
    iString title;
    iString tags;
    iChar icon;
    iTime when;
};

iLocalDef uint32_t  id_Bookmark (const iBookmark *d) { return d->node.key; }

iDeclareType(Bookmarks)
iDeclareTypeConstruction(Bookmarks)

void    clear_Bookmarks     (iBookmarks *);
void    load_Bookmarks      (iBookmarks *, const char *dirPath);
void    save_Bookmarks      (const iBookmarks *, const char *dirPath);

void    add_Bookmarks       (iBookmarks *, const iString *url, const iString *title, const iString *tags, iChar icon);
void    remove_Bookmarks    (iBookmarks *, uint32_t id);

typedef iBool (*iBookmarksFilterFunc) (const iBookmark *);
typedef int   (*iBookmarksCompareFunc)(const iBookmark **, const iBookmark **);

/**
 * Lists all or a subset of the bookmarks in a sorted array of Bookmark pointers.
 *
 * @param filter  Filter function to determine which bookmarks should be returned.
 *                If NULL, all bookmarks are listed.
 * @param cmp     Sort function that compares Bookmark pointers. If NULL, the
 *                returned list is sorted by descending creation time.
 *
 * @return Collected array of bookmarks. Caller does not get ownership of the
 * list or the bookmarks.
 */
const iPtrArray *list_Bookmarks(const iBookmarks *, iBookmarksFilterFunc filter,
                                iBookmarksCompareFunc cmp);
