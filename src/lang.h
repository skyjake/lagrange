#pragma once

#include <the_Foundation/string.h>

void            init_Lang       (void);
void            deinit_Lang     (void);

void            setCurrent_Lang (const char *language);
iRangecc        range_Lang      (iRangecc msgId);

const char *    cstr_Lang       (const char *msgId);
const iString * string_Lang     (const char *msgId);

void            translate_Lang      (iString *textWithIds);
const char *    translateCStr_Lang  (const char *textWithIds);
