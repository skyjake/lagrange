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

#include <the_Foundation/ptrarray.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/string.h>
#include <the_Foundation/stringarray.h>

/* Returns a complete "20" response, or NULL if the input was refused. */
typedef iBlock *(*iToolFilterFunc)(const iString *mime, const iBlock *body,
                                   const iString *requestUrl);

/* External programs configured in "tools.ini", or built-in tools. */

iDeclareType(Tool)
iDeclareTypeConstruction(Tool)

struct Impl_Tool {
    iString      id;
    iString      name;
    iStringArray *command;
    iString      mimePattern;
    iRegExp     *mimeRegex; /* NULL matches everything */
    iBool        isEnabled;
    iBool        isAuto;    /* filters responses automatically */
    iBool        inMenu;
    iString      key;       /* key shortcut, e.g., "ctrl+k" */
    iToolFilterFunc builtin; /* used instead of a command */
};

iBool   isUsable_Tool   (const iTool *);
iBool   matchMime_Tool  (const iTool *, const iString *mime);
iBool   parseOutput_Tool(const iBlock *output, iString *mime_out, iBlock *body_out);

/*----------------------------------------------------------------------------------------------*/

/* Runs tools as child processes. Another thread may cancel a run. */

iDeclareType(ToolProcess)
iDeclareTypeConstruction(ToolProcess)

/* Blocking. Returns NULL if the tool refused or the run was cancelled. */
iBlock *run_ToolProcess         (iToolProcess *, const iTool *tool, const iString *mime,
                                 const iBlock *body, const iString *requestUrl);
void    cancel_ToolProcess      (iToolProcess *); /* kills the running process; permanent */
iBool   isCancelled_ToolProcess (const iToolProcess *);

/*----------------------------------------------------------------------------------------------*/

iDeclareType(Tools)
iDeclareTypeConstruction(Tools)

void        addBuiltinFilter_Tools  (iTools *, iToolFilterFunc func);
iBool       willTryFilter_Tools     (const iTools *, const iString *mime);
iBlock *    tryFilter_Tools         (const iTools *, iToolProcess *, const iString *mime,
                                     const iBlock *body, const iString *requestUrl);

iBool       load_Tools              (iTools *, const char *saveDir); /* false if errors found */
void        addError_Tools          (iTools *, const iTool *tool, const char *msg);

const iPtrArray *list_Tools         (const iTools *);
const iTool *    find_Tools         (const iTools *, const iString *id);

const iString *debugInfo_Tools      (const iTools *);

iTools *    get_Tools               (void); /* global instance (set in init_Tools) */
