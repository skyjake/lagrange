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

#include "defs.h"
#include <the_Foundation/string.h>

void    init_Core       (void);
void    deinit_Core     (void);

void    setPhone_Core   (iBool isPhone); /* determined at runtime (iOS) */

void            setDataDir_Core (const char *path); /* base directory of "local:" URLs */
const iString * dataDir_Core    (void); /* absolute; empty if not set */

void    commitFile_Core (const char *path, const char *tempPathWithNewContents); /* latter will be removed */

/* User data change notifications, for syncing. May be called in any thread. */
enum iUserDataType {
    bookmarks_UserDataType,
    books_UserDataType,
    visited_UserDataType,
    identities_UserDataType,
    trusted_UserDataType,
    snippets_UserDataType,
};

typedef void (*iUserDataChangedFunc)(enum iUserDataType);

void    setUserDataChangedFunc_Core (iUserDataChangedFunc func);
void    notifyUserDataChanged_Core  (enum iUserDataType);
