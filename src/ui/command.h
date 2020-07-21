#pragma once

#include <the_Foundation/vec2.h>

iBool   equal_Command           (const char *commandWithArgs, const char *command);

int     arg_Command             (const char *); /* arg: */
float   argf_Command            (const char *); /* arg: */
int     argLabel_Command        (const char *, const char *label);
void *  pointer_Command         (const char *); /* ptr: */
void *  pointerLabel_Command    (const char *, const char *label);
iInt2   coord_Command           (const char *);
iInt2   dir_Command             (const char *);

const iString * string_Command  (const char *, const char *label);
const char *    valuePtr_Command(const char *, const char *label);
