#pragma once

#include <the_Foundation/string.h>

void            init_Lang       (void);
void            deinit_Lang     (void);

void            setCurrent_Lang (const char *language);
const iString * string_Lang     (const char *msgId);
const char *    cstr_Lang       (const char *msgId);
