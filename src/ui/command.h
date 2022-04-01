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

#include <the_Foundation/range.h>
#include <the_Foundation/string.h>
#include <the_Foundation/vec2.h>

iBool       equal_Command           (const char *commandWithArgs, const char *command);
iBool       equalArg_Command        (const char *commandWithArgs, const char *command,
                                     const char *label, const char *value);

int         arg_Command             (const char *); /* arg: */
float       argf_Command            (const char *); /* arg: */
int         argLabel_Command        (const char *, const char *label);
uint32_t    argU32Label_Command     (const char *, const char *label);
float       argfLabel_Command       (const char *, const char *label);
void *      pointer_Command         (const char *); /* ptr: */
void *      pointerLabel_Command    (const char *, const char *label);
iInt2       coord_Command           (const char *);
iInt2       dir_Command             (const char *);

const iString * string_Command      (const char *, const char *label); /* space-delimited */
iRangecc        range_Command       (const char *, const char *label); /* space-delimited */
const char *    suffixPtr_Command   (const char *, const char *label); /* until end-of-command */
iString *       suffix_Command      (const char *, const char *label); /* until end-of-command */

iLocalDef iBool hasLabel_Command(const char *d, const char *label) {
    return suffixPtr_Command(d, label) != NULL;
}
iLocalDef const char *cstr_Command(const char *d, const char *label) {
    return cstr_Rangecc(range_Command(d, label));
}
