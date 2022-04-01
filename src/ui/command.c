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

#include "command.h"
#include "app.h"

#include <the_Foundation/string.h>
#include <ctype.h>

iDeclareType(Token)

#define maxLen_Token 64
    
struct Impl_Token {
    char buf[64];
    size_t size;
};

static void init_Token(iToken *d, const char *label) {
    const size_t len = strlen(label);
    iAssert(len < sizeof(d->buf) - 3);
    d->buf[0] = ' ';
    memcpy(d->buf + 1, label, len);
    d->buf[1 + len] = ':';
    d->buf[1 + len + 1] = 0;
    d->size = len + 2;
}

static iRangecc find_Token(const iToken *d, const char *cmd) {
    iRangecc range = iNullRange;
    range.start = strstr(cmd, d->buf);
    if (range.start) {
        range.end = range.start + d->size;
    }
    return range;
}

iBool equal_Command(const char *cmdWithArgs, const char *cmd) {
    if (strchr(cmdWithArgs, ':')) {
        return startsWith_CStr(cmdWithArgs, cmd) && cmdWithArgs[strlen(cmd)] == ' ';
    }
    return equal_CStr(cmdWithArgs, cmd);
}

iBool equalArg_Command(const char *commandWithArgs, const char *command, const char *label,
                       const char *value) {
    return equal_Command(commandWithArgs, command) &&
           equal_Rangecc(range_Command(commandWithArgs, label), value);
}

int argLabel_Command(const char *cmd, const char *label) {
    iToken tok;
    init_Token(&tok, label);
    iRangecc ptr = find_Token(&tok, cmd);
    if (ptr.start) {
        return atoi(ptr.end);
    }
    return 0;
}

int arg_Command(const char *cmd) {
    return argLabel_Command(cmd, "arg");
}

uint32_t argU32Label_Command(const char *cmd, const char *label) {
    iToken tok;
    init_Token(&tok, label);
    const iRangecc ptr = find_Token(&tok, cmd);    
    if (ptr.start) {
        return (uint32_t) strtoul(ptr.end, NULL, 10);
    }
    return 0;
}

float argfLabel_Command(const char *cmd, const char *label) {
    iToken tok;
    init_Token(&tok, label);
    const iRangecc ptr = find_Token(&tok, cmd);    
    if (ptr.start) {
        return strtof(ptr.end, NULL);
    }
    return 0.0f;
}

float argf_Command(const char *cmd) {
    const char *ptr = strstr(cmd, " arg:");
    if (ptr) {
        return strtof(ptr + 5, NULL);
    }
    return 0;
}

void *pointerLabel_Command(const char *cmd, const char *label) {
    iToken tok;
    init_Token(&tok, label);
    const iRangecc ptr = find_Token(&tok, cmd);        
    if (ptr.start) {
        void *val = NULL;
        sscanf(ptr.end, "%p", &val);
        return val;
    }
    return NULL;
}

void *pointer_Command(const char *cmd) {
    return pointerLabel_Command(cmd, "ptr");
}

const char *suffixPtr_Command(const char *cmd, const char *label) {
    iToken tok;
    init_Token(&tok, label);
    const iRangecc ptr = find_Token(&tok, cmd);        
    if (ptr.start) {
        return ptr.end;
    }
    return NULL;
}

iString *suffix_Command(const char *cmd, const char *label) {
    return newCStr_String(suffixPtr_Command(cmd, label));
}

const iString *string_Command(const char *cmd, const char *label) {
    return collectNewRange_String(range_Command(cmd, label));
}

iRangecc range_Command(const char *cmd, const char *label) {
    iRangecc val = { suffixPtr_Command(cmd, label), NULL };
    if (val.start) {
        for (val.end = val.start; *val.end && !isspace(*val.end); val.end++) {}
        return val;
    }
    return iNullRange;
}

iInt2 dir_Command(const char *cmd) {
    const char *ptr = strstr(cmd, " dir:");
    if (ptr) {
        iInt2 dir;
        sscanf(ptr + 5, "%d%d", &dir.x, &dir.y);
        return dir;
    }
    return zero_I2();
}

iInt2 coord_Command(const char *cmd) {
    iInt2 coord = zero_I2();
    const char *ptr = strstr(cmd, " coord:");
    if (ptr) {
        sscanf(ptr + 7, "%d%d", &coord.x, &coord.y);
    }
    return coord;
}
