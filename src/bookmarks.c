#include "bookmarks.h"

#include <the_Foundation/file.h>
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
    iArray bookmarks;
};

iDefineTypeConstruction(Bookmarks)

void init_Bookmarks(iBookmarks *d) {
    init_Array(&d->bookmarks, sizeof(iBookmark));
}

void deinit_Bookmarks(iBookmarks *d) {
    clear_Bookmarks(d);
    deinit_Array(&d->bookmarks);
}

void clear_Bookmarks(iBookmarks *d) {
    iForEach(Array, i, &d->bookmarks) {
        deinit_Bookmark(i.value);
    }
    clear_Array(&d->bookmarks);
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
            iBookmark bm;
            init_Bookmark(&bm);
            bm.icon = strtoul(line.start, NULL, 16);
            line.start += 9;
            char *endPos;
            initSeconds_Time(&bm.when, strtod(line.start, &endPos));
            line.start = skipSpace_CStr(endPos);
            setRange_String(&bm.url, line);
            nextSplit_Rangecc(&src, "\n", &line);
            setRange_String(&bm.title, line);
            nextSplit_Rangecc(&src, "\n", &line);
            setRange_String(&bm.tags, line);
            pushBack_Array(&d->bookmarks, &bm);
        }
    }
    iRelease(f);
}

void save_Bookmarks(const iBookmarks *d, const char *dirPath) {
    iFile *f = newCStr_File(concatPath_CStr(dirPath, fileName_Bookmarks_));
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        iString *str = collectNew_String();
        iConstForEach(Array, i, &d->bookmarks) {
            const iBookmark *bm = i.value;
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
    iBookmark bm;
    init_Bookmark(&bm);
    set_String(&bm.url, url);
    set_String(&bm.title, title);
    set_String(&bm.tags, tags);
    bm.icon = icon;
    initCurrent_Time(&bm.when);
    pushBack_Array(&d->bookmarks, &bm);
}

const iPtrArray *list_Bookmarks(const iBookmarks *d, iBookmarksFilterFunc filter,
                                iBookmarksCompareFunc cmp) {
    iPtrArray *list = collectNew_PtrArray();
    iConstForEach(Array, i, &d->bookmarks) {
        if (!filter || filter(i.value)) {
            pushBack_PtrArray(list, i.value);
        }
    }
    if (!cmp) cmp = cmpTimeDescending_Bookmark_;
    sort_Array(list, (int (*)(const void *, const void *)) cmp);
    return list;
}
