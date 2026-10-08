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

#include "storage.h"

#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/mutex.h>
#include <the_Foundation/path.h>
#include <stdio.h>

struct Impl_Storage {
    iMutex      mtx;
    iString     rootDir;
    iStringSet *allowList;
};

iDefineTypeConstructionArgs(Storage, (const iString *rootDir), rootDir)

static const char *header_Storage_ = "LGSYNC ";

void init_Storage(iStorage *d, const iString *rootDir) {
    init_Mutex(&d->mtx);
    initCopy_String(&d->rootDir, rootDir);
    d->allowList = NULL;
    makeDirs_Path(&d->rootDir);
}

void deinit_Storage(iStorage *d) {
    iRelease(d->allowList);
    deinit_String(&d->rootDir);
    deinit_Mutex(&d->mtx);
}

void setAllowList_Storage(iStorage *d, const iStringSet *fingerprints) {
    iRelease(d->allowList);
    d->allowList = fingerprints ? copy_StringSet(fingerprints) : NULL;
}

iBool isAllowed_Storage(const iStorage *d, const iString *fingerprint) {
    return !d->allowList || contains_StringSet(d->allowList, fingerprint);
}

static const iString *path_Storage_(const iStorage *d, const iString *fingerprint) {
    return collect_String(concat_Path(&d->rootDir, collectNewFormat_String(
                                                        "%s.dat", cstr_String(fingerprint))));
}

static iBlock *read_Storage_(const iStorage *d, const iString *fingerprint, uint64_t *gen_out) {
    *gen_out = 0;
    iFile *f = new_File(path_Storage_(d, fingerprint));
    iBlock *data = NULL;
    if (open_File(f, readOnly_FileMode)) {
        iBlock *all = readAll_File(f);
        const char *start = cstr_Block(all);
        const char *eol   = memchr(start, '\n', size_Block(all));
        if (eol && startsWith_CStr(start, header_Storage_)) {
            *gen_out = strtoull(start + strlen(header_Storage_), NULL, 10);
            data = newData_Block(eol + 1, size_Block(all) - (eol + 1 - start));
        }
        delete_Block(all);
    }
    iRelease(f);
    return data;
}

iBlock *load_Storage(iStorage *d, const iString *fingerprint, uint64_t *generation_out) {
    iBlock *data;
    iGuardMutex(&d->mtx, data = read_Storage_(d, fingerprint, generation_out));
    return data;
}

enum iStoreResult store_Storage(iStorage *d, const iString *fingerprint, uint64_t baseGeneration,
                                const iBlock *data, uint64_t *newGeneration_out) {
    enum iStoreResult result = error_StoreResult;
    lock_Mutex(&d->mtx);
    uint64_t gen = 0;
    delete_Block(read_Storage_(d, fingerprint, &gen));
    if (gen != baseGeneration) {
        result = stale_StoreResult;
    }
    else {
        const iString *path    = path_Storage_(d, fingerprint);
        const iString *tmpPath = collectNewFormat_String("%s.tmp", cstr_String(path));
        iFile *f = new_File(tmpPath);
        if (open_File(f, writeOnly_FileMode)) {
            printf_Stream(stream_File(f), "%s%llu\n", header_Storage_,
                          (unsigned long long) gen + 1);
            const iBool ok = write_File(f, data) == size_Block(data);
            close_File(f);
            if (ok && rename(cstr_String(tmpPath), cstr_String(path)) == 0) {
                *newGeneration_out = gen + 1;
                result = ok_StoreResult;
            }
        }
        iRelease(f);
    }
    unlock_Mutex(&d->mtx);
    return result;
}
