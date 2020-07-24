#pragma once

#include <the_Foundation/range.h>
#include <the_Foundation/string.h>

iDeclareType(Url)

struct Impl_Url {
    iRangecc protocol;
    iRangecc host;
    iRangecc port;
    iRangecc path;
    iRangecc query;
};

void init_Url(iUrl *d, const iString *text);
