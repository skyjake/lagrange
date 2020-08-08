#pragma once

#include "gmrequest.h"

#include <the_Foundation/array.h>
#include <the_Foundation/string.h>
#include <the_Foundation/time.h>

iDeclareType(VisitedUrl)
iDeclareTypeConstruction(VisitedUrl)

struct Impl_VisitedUrl {
    iString url;
    iTime   when;
};

iDeclareType(Visited)
iDeclareTypeConstruction(Visited)

void    clear_Visited           (iVisited *);
void    load_Visited            (iVisited *, const char *dirPath);
void    save_Visited            (const iVisited *, const char *dirPath);

iTime   urlVisitTime_Visited    (const iVisited *, const iString *url);
void    visitUrl_Visited        (iVisited *, const iString *url); /* adds URL to the visited URLs set */
