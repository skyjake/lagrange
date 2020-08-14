#include "bookmarks.h"

#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <the_Foundation/hash.h>

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
    int   idEnum;
    iHash bookmarks;
};

iDefineTypeConstruction(Bookmarks)

void init_Bookmarks(iBookmarks *d) {
    d->idEnum = 0;
    init_Hash(&d->bookmarks);
}

void deinit_Bookmarks(iBookmarks *d) {
    clear_Bookmarks(d);
    deinit_Hash(&d->bookmarks);
}

void clear_Bookmarks(iBookmarks *d) {
    iForEach(Hash, i, &d->bookmarks) {
        delete_Bookmark((iBookmark *) i.value);
    }
    clear_Hash(&d->bookmarks);
    d->idEnum = 0;
}

static void insert_Bookmarks_(iBookmarks *d, iBookmark *bookmark) {
    bookmark->node.key = ++d->idEnum;
    insert_Hash(&d->bookmarks, &bookmark->node);
}

void load_Bookmarks(iBookmarks *d, const char *dirPath) {
    clear_Bookmarks(d);
    iFile *f = newCStr_File(concatPath_CStr(dirPath, fileName_Bookmarks_));
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        const iRangecc src = range_Block(collect_Block(readAll_File(f)));
        iRangecc line = iNullRange;
        while (nextSplit_Rangecc(&src, "\n", &line)) {
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
            nextSplit_Rangecc(&src, "\n", &line);
            setRange_String(&bm->title, line);
            nextSplit_Rangecc(&src, "\n", &line);
            setRange_String(&bm->tags, line);
            insert_Bookmarks_(d, bm);
        }
    }
    iRelease(f);
}

void save_Bookmarks(const iBookmarks *d, const char *dirPath) {
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
}

void add_Bookmarks(iBookmarks *d, const iString *url, const iString *title, const iString *tags,
                   iChar icon) {
    iBookmark *bm = new_Bookmark();
    set_String(&bm->url, url);
    set_String(&bm->title, title);
    set_String(&bm->tags, tags);
    bm->icon = icon;
    initCurrent_Time(&bm->when);
    insert_Bookmarks_(d, bm);
}

iBool remove_Bookmarks(iBookmarks *d, uint32_t id) {
    iBookmark *bm = (iBookmark *) remove_Hash(&d->bookmarks, id);
    if (bm) {
        delete_Bookmark(bm);
        return iTrue;
    }
    return iFalse;
}

const iPtrArray *list_Bookmarks(const iBookmarks *d, iBookmarksFilterFunc filter,
                                iBookmarksCompareFunc cmp) {
    iPtrArray *list = collectNew_PtrArray();
    iConstForEach(Hash, i, &d->bookmarks) {
        const iBookmark *bm = (const iBookmark *) i.value;
        if (!filter || filter(bm)) {
            pushBack_PtrArray(list, bm);
        }
    }
    if (!cmp) cmp = cmpTimeDescending_Bookmark_;
    sort_Array(list, (int (*)(const void *, const void *)) cmp);
    return list;
}
