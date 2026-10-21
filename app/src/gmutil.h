#pragma once
#include <lagrange/gmutil.h>

/* Shell-only extensions. */
const iString * prettyDataUrl_String    (const iString *, int contentColor); /* uses color */
const iString * navigationParentUrl_String(const iString *); /* NULL if there is no parent */
const iString * navigationRootUrl_String(const iString *);
