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

#include "books.h"
#include "gempub.h"

#include <lagrange/lang.h>

#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/mutex.h>
#include <the_Foundation/path.h>
#include <the_Foundation/stringhash.h>
#include <the_Foundation/stringset.h>
#include <the_Foundation/toml.h>
#include <math.h>
#if !defined (iPlatformMsys) && !defined (iPlatformWindows)
#   include <limits.h>
#   include <stdlib.h>
#endif

static void init_Book(iBook *d) {
    init_String(&d->id);
    init_String(&d->title);
    init_String(&d->author);
    init_String(&d->fileName);
    init_String(&d->url);
    d->size        = 0;
    d->chapter     = -1;
    d->numChapters = 0;
    d->chapterPos  = 0.0f;
    iZap(d->timestamp);
}

static void deinit_Book(iBook *d) {
    deinit_String(&d->url);
    deinit_String(&d->fileName);
    deinit_String(&d->author);
    deinit_String(&d->title);
    deinit_String(&d->id);
}

iDefineObjectConstruction(Book)
iDefineClass(Book)

float progress_Book(const iBook *d) {
    if (d->chapter < 0 || d->numChapters <= 0) {
        return 0.0f;
    }
    return iClamp((d->chapter + d->chapterPos) / d->numChapters, 0.0f, 1.0f);
}

const iString *path_Book(const iBook *d) {
    if (isEmpty_String(&d->fileName)) {
        return NULL;
    }
    return collect_String(concat_Path(dir_Books(), &d->fileName));
}

/*----------------------------------------------------------------------------------------------*/

iDeclareClass(ShelfFile)

struct Impl_ShelfFile {
    iObject  object;
    iBook   *book; /* not owned */
    uint64_t size;
};

static void init_ShelfFile(iShelfFile *d, iBook *book, uint64_t size) {
    d->book = book;
    d->size = size;
}

static void deinit_ShelfFile(iShelfFile *d) {
    iUnused(d);
}

iDefineObjectConstructionArgs(ShelfFile, (iBook *book, uint64_t size), book, size)
iDefineClass(ShelfFile)

/*----------------------------------------------------------------------------------------------*/

static const char *fileName_Books_ = "books.ini";

iDeclareType(Books)

struct Impl_Books {
    iMutex      *mtx;
    iString      saveDir;
    iString      dir;
    iStringHash *books;   /* by ID */
    iStringHash *files;   /* ShelfFiles by name in the books directory; copies may share a book */
};

static iBooks *books_;

static void load_Books_(iBooks *d);

void init_Books(const char *saveDir) {
    iAssert(!books_);
    iBooks *d = books_ = iMalloc(Books);
    d->mtx = new_Mutex();
    initCStr_String(&d->saveDir, saveDir);
    initCStr_String(&d->dir, concatPath_CStr(saveDir, "books"));
    d->books = new_StringHash();
    d->files = new_StringHash();
    makeDirs_Path(&d->dir);
    load_Books_(d);
}

void deinit_Books(void) {
    iAssert(books_);
    iBooks *d = books_;
    iRelease(d->files);
    iRelease(d->books);
    deinit_String(&d->dir);
    deinit_String(&d->saveDir);
    delete_Mutex(d->mtx);
    free(d);
    books_ = NULL;
}

void serialize_Books(iStream *outs) {
    iBooks *d = books_;
    lock_Mutex(d->mtx);
    iString *str = new_String();
    iConstForEach(StringHash, i, d->books) {
        const iBook *book = value_StringHashNode(i.value);
        format_String(str,
                      "[%s]\ntitle = \"%s\"\nauthor = \"%s\"\n",
                      cstr_String(&book->id),
                      cstrCollect_String(quote_String(&book->title, iFalse)),
                      cstrCollect_String(quote_String(&book->author, iFalse)));
        if (!isEmpty_String(&book->fileName)) {
            appendFormat_String(str,
                                "file = \"%s\"\nsize = %llu\n",
                                cstrCollect_String(quote_String(&book->fileName, iFalse)),
                                (unsigned long long) book->size);
        }
        if (!isEmpty_String(&book->url)) {
            appendFormat_String(str,
                                "url = \"%s\"\n",
                                cstrCollect_String(quote_String(&book->url, iFalse)));
        }
        appendFormat_String(str,
                            "chapter = %d\nchapters = %d\npos = %f\ntimestamp = %lld\n\n",
                            book->chapter,
                            book->numChapters,
                            book->chapterPos,
                            (long long) integralSeconds_Time(&book->timestamp));
        write_Stream(outs, utf8_String(str));
    }
    delete_String(str);
    unlock_Mutex(d->mtx);
}

iDeclareType(BooksLoader)

struct Impl_BooksLoader {
    iBooks            *books;
    enum iImportMethod method;
    iBool              isImport;
    iBook             *book;
};

static void insertFile_Books_(iBooks *d, iBook *book) {
    if (!isEmpty_String(&book->fileName)) {
        insert_StringHash(d->files, &book->fileName, iClob(new_ShelfFile(book, book->size)));
    }
}

static void handleTable_BooksLoader_(void *context, const iString *table, iBool isStart) {
    iBooksLoader *d = context;
    if (isStart) {
        iAssert(!d->book);
        d->book = new_Book();
        set_String(&d->book->id, table);
        return;
    }
    if (!d->book) {
        return;
    }
    iBook *book = d->book;
    iBook *old  = value_StringHash(d->books->books, &book->id);
    if (d->isImport) {
        /* Files are specific to the exporting device. */
        clear_String(&book->fileName);
        book->size = 0;
    }
    if (!old) {
        insert_StringHash(d->books->books, &book->id, book);
        insertFile_Books_(d->books, book);
        iReleasePtr(&d->book);
        return;
    }
    if (isEmpty_String(&old->url)) {
        set_String(&old->url, &book->url);
    }
    if (d->method == all_ImportMethod &&
        integralSeconds_Time(&book->timestamp) > integralSeconds_Time(&old->timestamp)) {
        /* The newer position is kept. */
        set_String(&old->title, &book->title);
        set_String(&old->author, &book->author);
        old->chapter     = book->chapter;
        old->numChapters = book->numChapters;
        old->chapterPos  = book->chapterPos;
        old->timestamp   = book->timestamp;
    }
    iReleasePtr(&d->book);
}

static void handleKeyValue_BooksLoader_(void *context, const iString *table, const iString *key,
                                        const iTomlValue *value) {
    iBooksLoader *d = context;
    iBook        *book = d->book;
    iUnused(table);
    if (!book) {
        return;
    }
    if (value->type == string_TomlType) {
        if (!cmp_String(key, "title")) {
            set_String(&book->title, value->value.string);
        }
        else if (!cmp_String(key, "author")) {
            set_String(&book->author, value->value.string);
        }
        else if (!cmp_String(key, "file")) {
            set_String(&book->fileName, value->value.string);
        }
        else if (!cmp_String(key, "url")) {
            set_String(&book->url, value->value.string);
        }
        return;
    }
    const double num = number_TomlValue(value);
    if (!cmp_String(key, "size")) {
        book->size = (uint64_t) num;
    }
    else if (!cmp_String(key, "chapter")) {
        book->chapter = (int) num;
    }
    else if (!cmp_String(key, "chapters")) {
        book->numChapters = (int) num;
    }
    else if (!cmp_String(key, "pos")) {
        book->chapterPos = iClamp((float) num, 0.0f, 1.0f);
    }
    else if (!cmp_String(key, "timestamp")) {
        initSeconds_Time(&book->timestamp, num);
    }
}

static void deserialize_Books_(iBooks *d, iStream *ins, enum iImportMethod method,
                               iBool isImport) {
    if (method == none_ImportMethod) {
        return;
    }
    lock_Mutex(d->mtx);
    iBooksLoader loader = { .books = d, .method = method, .isImport = isImport };
    iTomlParser *toml = new_TomlParser();
    setHandlers_TomlParser(toml, handleTable_BooksLoader_, handleKeyValue_BooksLoader_, &loader);
    parse_TomlParser(toml, collect_String(newBlock_String(collect_Block(readAll_Stream(ins)))));
    delete_TomlParser(toml);
    iReleasePtr(&loader.book);
    unlock_Mutex(d->mtx);
}

void deserialize_Books(iStream *ins, enum iImportMethod method) {
    deserialize_Books_(books_, ins, method, iTrue);
    save_Books();
}

static void load_Books_(iBooks *d) {
    iFile *f = newCStr_File(concatPath_CStr(cstr_String(&d->saveDir), fileName_Books_));
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        deserialize_Books_(d, stream_File(f), all_ImportMethod, iFalse);
    }
    iRelease(f);
}

void save_Books(void) {
    iBooks *d = books_;
    iFile *f = newCStr_File(concatPath_CStr(cstr_String(&d->saveDir), fileName_Books_));
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        serialize_Books(stream_File(f));
    }
    iRelease(f);
}

const iString *dir_Books(void) {
    return &books_->dir;
}

iString *makeId_Books(const iString *title, const iString *author) {
    iString *key = lower_String(title);
    trim_String(key);
    appendCStr_String(key, "\n");
    iString *auth = lower_String(author);
    trim_String(auth);
    append_String(key, auth);
    delete_String(auth);
    uint8_t md5[16];
    md5_Block(utf8_String(key), md5);
    delete_String(key);
    iBlock *digest = newData_Block(md5, 8);
    iString *id = hexEncode_Block(digest);
    delete_Block(digest);
    return id;
}

const iString *id_Books(const iGempub *gempub) {
    return collect_String(makeId_Books(property_Gempub(gempub, title_GempubProperty),
                                       property_Gempub(gempub, author_GempubProperty)));
}

const iBook *find_Books(const iString *id) {
    iBooks *d = books_;
    lock_Mutex(d->mtx);
    const iBook *book = constValue_StringHash(d->books, id);
    unlock_Mutex(d->mtx);
    return book;
}

static iBool isInDir_(const iString *path, const iString *dir) {
    return size_String(path) > size_String(dir) + 1 && startsWith_String(path, cstr_String(dir)) &&
           cstr_String(path)[size_String(dir)] == iPathSeparator[0] &&
           indexOfCStrFrom_String(path, iPathSeparator, size_String(dir) + 1) == iInvalidPos;
}

iBool isOnShelf_Books(const iString *path) {
    const iString *clean = collect_String(cleaned_Path(path));
    if (isInDir_(clean, dir_Books())) {
        return iTrue;
    }
#if !defined (iPlatformMsys) && !defined (iPlatformWindows)
    /* Symbolic links may lead to the same directory. */
    char realDir[PATH_MAX], realFile[PATH_MAX];
    if (realpath(cstr_String(dir_Books()), realDir) && realpath(cstr_String(clean), realFile)) {
        return isInDir_(collectNewCStr_String(realFile), collectNewCStr_String(realDir));
    }
#endif
    return iFalse;
}

static iBook *bookForGempub_Books_(iBooks *d, const iGempub *gempub) {
    const iString *id   = id_Books(gempub);
    iBook         *book = value_StringHash(d->books, id);
    if (!book) {
        book = new_Book();
        set_String(&book->id, id);
        insert_StringHash(d->books, id, book);
        iRelease(book);
    }
    set_String(&book->title, property_Gempub(gempub, title_GempubProperty));
    translate_Lang(&book->title); /* may be a placeholder */
    set_String(&book->author, property_Gempub(gempub, author_GempubProperty));
    return book;
}

static void setFile_Books_(iBooks *d, iBook *book, const iString *fileName, uint64_t size) {
    iShelfFile *file = value_StringHash(d->files, fileName);
    if (file && file->book != book && equal_String(&file->book->fileName, fileName)) {
        clear_String(&file->book->fileName);
        file->book->size = 0;
    }
    insert_StringHash(d->files, fileName, iClob(new_ShelfFile(book, size)));
    const iString *path = path_Book(book);
    if (!path || equal_String(&book->fileName, fileName) || !fileExists_FileInfo(path)) {
        set_String(&book->fileName, fileName);
        book->size = size;
    }
}

static int cmpTimestamp_Book_(const void *a, const void *b) {
    const iBook *x = *(const iBook **) a;
    const iBook *y = *(const iBook **) b;
    const int64_t tx = integralSeconds_Time(&x->timestamp);
    const int64_t ty = integralSeconds_Time(&y->timestamp);
    if (tx != ty) {
        return tx > ty ? -1 : 1;
    }
    return cmpStringCase_String(&x->title, &y->title);
}

const iPtrArray *list_Books(void) {
    iBooks    *d       = books_;
    iPtrArray *shelf   = collectNew_PtrArray();
    iBool      changed = iFalse;
    lock_Mutex(d->mtx);
    iStringSet *present = iClob(new_StringSet());
    iForEach(DirFileInfo, i, iClob(new_DirFileInfo(&d->dir))) {
        const iString *path = path_FileInfo(i.value);
        const iString *name = collectNewRange_String(baseName_Path(path));
        if (!endsWithCase_String(name, ".gpub") || startsWith_String(name, ".")) {
            continue;
        }
        const uint64_t size = size_FileInfo(i.value);
        iShelfFile    *file = value_StringHash(d->files, name);
        iBook         *book = file && file->size == size ? file->book : NULL;
        if (!book) {
            /* Not seen before or changed, so the metadata must be read. */
            iGempub *gempub = new_Gempub();
            if (openFile_Gempub(gempub, path)) {
                book = bookForGempub_Books_(d, gempub);
                if (!isValid_Time(&book->timestamp)) {
                    book->timestamp = lastModified_FileInfo(i.value);
                }
                setFile_Books_(d, book, name, size);
                changed = iTrue;
            }
            delete_Gempub(gempub);
        }
        if (book) {
            insert_StringSet(present, name);
            if (indexOf_PtrArray(shelf, book) == iInvalidPos) {
                pushBack_PtrArray(shelf, book);
            }
        }
    }
    /* Forget files that no longer exist. */
    iStringSet *gone = iClob(new_StringSet());
    iConstForEach(StringHash, j, d->files) {
        const iString *name = key_StringHashConstIterator(&j);
        if (!contains_StringSet(present, name)) {
            insert_StringSet(gone, name);
        }
    }
    iConstForEach(StringSet, k, gone) {
        remove_StringHash(d->files, k.value);
    }
    iConstForEach(StringHash, m, d->books) {
        iBook *book = value_StringHashNode(m.value);
        if (!isEmpty_String(&book->fileName) && !contains_StringSet(present, &book->fileName)) {
            /* Another copy may remain. */
            clear_String(&book->fileName);
            book->size = 0;
            iConstForEach(StringHash, n, d->files) {
                const iShelfFile *file = value_StringHashNode(n.value);
                if (file->book == book) {
                    set_String(&book->fileName, key_StringHashConstIterator(&n));
                    book->size = file->size;
                    break;
                }
            }
            changed = iTrue;
        }
        if (isEmpty_String(&book->fileName) && !isEmpty_String(&book->url)) {
            pushBack_PtrArray(shelf, book); /* can be downloaded */
        }
    }
    unlock_Mutex(d->mtx);
    sort_Array(shelf, cmpTimestamp_Book_);
    if (changed) {
        save_Books();
    }
    return shelf;
}

void update_Books(const iGempub *gempub, const iString *path) {
    iBooks *d = books_;
    lock_Mutex(d->mtx);
    iBook *book = bookForGempub_Books_(d, gempub);
    if (isOnShelf_Books(path)) {
        setFile_Books_(d, book,
                       collectNewRange_String(baseName_Path(path)),
                       fileSize_FileInfo(path));
    }
    initCurrent_Time(&book->timestamp);
    unlock_Mutex(d->mtx);
    save_Books();
}

void setPosition_Books(const iGempub *gempub, int chapter, int numChapters, float pos) {
    iBooks *d = books_;
    lock_Mutex(d->mtx);
    iBook *book = bookForGempub_Books_(d, gempub);
    const iBool isChanged = book->chapter != chapter || book->numChapters != numChapters ||
                            fabsf(book->chapterPos - pos) > 0.0001f;
    if (isChanged) {
        book->chapter     = chapter;
        book->numChapters = numChapters;
        book->chapterPos  = pos;
        initCurrent_Time(&book->timestamp);
    }
    unlock_Mutex(d->mtx);
    if (isChanged) {
        save_Books();
    }
}

static iString *cleanFileName_Books_(const iString *fileName) {
    iString *name = new_String();
    iConstForEach(String, i, fileName) {
        const iChar ch = i.value;
        appendChar_String(name, ch < 0x20 || (ch < 0x80 && strchr("/\\:*?\"<>|", ch)) ? '_' : ch);
    }
    if (length_String(name) > 100) {
        truncate_String(name, 100);
    }
    trim_String(name);
    while (startsWith_String(name, ".")) {
        remove_Block(&name->chars, 0, 1);
    }
    if (isEmpty_String(name)) {
        setCStr_String(name, "book");
    }
    if (!endsWithCase_String(name, ".gpub")) {
        appendCStr_String(name, ".gpub");
    }
    return name;
}

static iString *uniquePath_Books_(const iString *fileName) {
    iString *path = concat_Path(dir_Books(), fileName);
    const size_t extPos = size_String(path) - 5; /* ".gpub" */
    for (int n = 2; fileExists_FileInfo(path); n++) {
        iString *alt = newRange_String((iRangecc){ cstr_String(path), cstr_String(path) + extPos });
        appendFormat_String(alt, " %d.gpub", n);
        if (!fileExists_FileInfo(alt)) {
            delete_String(path);
            path = alt;
            break;
        }
        delete_String(alt);
    }
    return path;
}

const iString *add_Books(const iBlock *gempubData, const iString *fileName,
                         const iString *replacePath, const iString *sourceUrl) {
    iGempub *gempub = new_Gempub();
    if (!open_Gempub(gempub, gempubData)) {
        delete_Gempub(gempub);
        return NULL;
    }
    makeDirs_Path(dir_Books());
    iString *path = replacePath ? copy_String(replacePath)
                                : uniquePath_Books_(collect_String(cleanFileName_Books_(fileName)));
    iFile *f = new_File(path);
    iBool  ok = iFalse;
    if (open_File(f, writeOnly_FileMode)) {
        ok = write_File(f, gempubData) == size_Block(gempubData);
        close_File(f);
    }
    iRelease(f);
    if (ok) {
        if (sourceUrl) {
            lock_Mutex(books_->mtx);
            set_String(&bookForGempub_Books_(books_, gempub)->url, sourceUrl);
            unlock_Mutex(books_->mtx);
        }
        update_Books(gempub, path);
    }
    delete_Gempub(gempub);
    if (!ok) {
        delete_String(path);
        return NULL;
    }
    return collect_String(path);
}

void remove_Books(const iString *id) {
    iBooks *d = books_;
    lock_Mutex(d->mtx);
    iBook         *book = value_StringHash(d->books, id);
    const iString *path = book ? path_Book(book) : NULL;
    if (book) {
        clear_String(&book->url);
    }
    unlock_Mutex(d->mtx);
    if (path && isOnShelf_Books(path)) {
        removePath_CStr(cstr_String(path));
    }
    list_Books(); /* forgets the file */
    save_Books();
}
