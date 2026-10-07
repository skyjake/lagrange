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

#include "lagrange/core.h"

#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <the_Foundation/string.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined (iPlatformAndroidMobile)
#   include <fcntl.h>
#   include <unistd.h>
#endif

iDeclareType(Core)

struct Impl_Core {
    iBool   isPhone;
    iString dataDir;
};

static iCore core_;

void init_Core(void) {
    iCore *d = &core_;
    d->isPhone = iFalse;
    init_String(&d->dataDir);
}

void deinit_Core(void) {
    deinit_String(&core_.dataDir);
}

void setDataDir_Core(const char *path) {
    iString *dir = makeAbsolute_Path(collect_String(cleanedCStr_Path(path)));
    set_String(&core_.dataDir, dir);
    delete_String(dir);
}

const iString *dataDir_Core(void) {
    return &core_.dataDir;
}

void setPhone_Core(iBool isPhone) {
    core_.isPhone = isPhone;
}

iBool isPhone_Core(void) {
    return core_.isPhone;
}

static void syncFile_Core_(const char *path) {
#if defined (iPlatformAndroidMobile)
    /* Make sure the new content is durable on disk before it replaces the old file: a
       rename() is atomic but not durable by itself, and Android is more likely to kill
       the process (or the whole device may lose power) right after this. */
    const int fd = open(path, O_WRONLY);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
#else
    iUnused(path);
#endif
}

#if !defined (iPlatformMsys) && !defined (iPlatformWindows)
static iBool copyFile_Core_(const char *fromPath, const char *toPath) {
    iBool ok = iFalse;
    iFile *src = newCStr_File(fromPath);
    if (open_File(src, readOnly_FileMode)) {
        iBlock *data = readAll_File(src);
        iFile *dst = newCStr_File(toPath);
        if (open_File(dst, writeOnly_FileMode)) {
            ok = write_File(dst, data) == size_Block(data);
        }
        iRelease(dst);
        delete_Block(data);
    }
    iRelease(src);
    return ok;
}
#endif

void commitFile_Core(const char *path, const char *tempPathWithNewContents) {
    syncFile_Core_(tempPathWithNewContents);
#if defined (iPlatformMsys) || defined (iPlatformWindows)
    iString *oldPath = collectNewCStr_String(path);
    appendCStr_String(oldPath, ".old");
    renamePath_CStr(path, cstr_String(oldPath)); /* move the old file out of the way */
    renamePath_CStr(tempPathWithNewContents, path);
    removePath_CStr(cstr_String(oldPath));
#else
    /* Symlinks are kept by replacing the file they point to. */
    char *target = realpath(path, NULL);
    const char *dest = target ? target : path;
    if (rename(tempPathWithNewContents, dest) != 0) { /* atomic; replaces destination file */
        /* Destination may be on a different file system. */
        if (copyFile_Core_(tempPathWithNewContents, dest)) {
            syncFile_Core_(dest);
            removePath_CStr(tempPathWithNewContents);
        }
        else {
            fprintf(stderr, "[Core] failed to write \"%s\": %s\n", dest, strerror(errno));
        }
    }
    free(target);
#endif
}
