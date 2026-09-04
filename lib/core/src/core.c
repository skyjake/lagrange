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

#include <the_Foundation/path.h>
#include <the_Foundation/string.h>

#if defined (iPlatformAndroidMobile)
#   include <fcntl.h>
#   include <unistd.h>
#endif

iDeclareType(Core)

struct Impl_Core {
    iBool isPhone;
};

static iCore core_;

void init_Core(void) {
    iCore *d = &core_;
    d->isPhone = iFalse;
}

void deinit_Core(void) {
    /* nothing to do yet */
}

void setPhone_Core(iBool isPhone) {
    core_.isPhone = isPhone;
}

iBool isPhone_Core(void) {
    return core_.isPhone;
}

void commitFile_Core(const char *path, const char *tempPathWithNewContents) {
#if defined (iPlatformAndroidMobile)
    /* Make sure the new content is durable on disk before it replaces the old file: a
       rename() is atomic but not durable by itself, and Android is more likely to kill
       the process (or the whole device may lose power) right after this. */ {
        const int fd = open(tempPathWithNewContents, O_WRONLY);
        if (fd >= 0) {
            fsync(fd);
            close(fd);
        }
    }
#endif
#if defined (iPlatformMsys) || defined (iPlatformWindows)
    iString *oldPath = collectNewCStr_String(path);
    appendCStr_String(oldPath, ".old");
    renamePath_CStr(path, cstr_String(oldPath)); /* move the old file out of the way */
    renamePath_CStr(tempPathWithNewContents, path);
    removePath_CStr(cstr_String(oldPath));
#else
    renamePath_CStr(tempPathWithNewContents, path); /* atomic; replaces destination file */
#endif
}
