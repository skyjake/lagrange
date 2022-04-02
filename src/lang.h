/* Copyright 2021 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include <the_Foundation/string.h>

void            init_Lang       (void);
void            deinit_Lang     (void);

void            setCurrent_Lang (const char *language);
const char *    code_Lang       (void); /* ISO 639 */
iRangecc        range_Lang      (iRangecc msgId);

const char *    cstr_Lang       (const char *msgId);
const iString * string_Lang     (const char *msgId);

void            translate_Lang      (iString *textWithIds);
const char *    translateCStr_Lang  (const char *textWithIds);

const char *    cstrCount_Lang      (const char *msgId, int count);
const char *    formatCStr_Lang     (const char *formatMsgId, int count);
const char *    formatCStrs_Lang    (const char *formatMsgId, size_t count);
const char *    format_Lang         (const char *formatTextWithIds, ...);

iString *   timeFormatHourPreference_Lang   (const char *formatMsgId);
