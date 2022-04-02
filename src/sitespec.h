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

#include "defs.h"
#include <the_Foundation/stringarray.h>

iDeclareType(SiteSpec)

enum iSiteSpecKey {
    titanPort_SiteSpecKey,       /* int */
    titanIdentity_SiteSpecKey,   /* String */
    dismissWarnings_SiteSpecKey, /* int */
    usedIdentities_SiteSpecKey,  /* StringArray */
    paletteSeed_SiteSpecKey,     /* String */
    tlsSessionCache_SiteSpeckey, /* int */
};

void    init_SiteSpec       (const char *saveDir);
void    deinit_SiteSpec     (void);

void    serialize_SiteSpec      (iStream *);
iBool   deserialize_SiteSpec    (iStream *, enum iImportMethod);

/* changes saved immediately */
void    setValue_SiteSpec       (const iString *site, enum iSiteSpecKey key, int value); 
void    setValueString_SiteSpec (const iString *site, enum iSiteSpecKey key, const iString *value);
void    insertString_SiteSpec   (const iString *site, enum iSiteSpecKey key, const iString *value);
void    removeString_SiteSpec   (const iString *site, enum iSiteSpecKey key, const iString *value);

int                 value_SiteSpec          (const iString *site, enum iSiteSpecKey key);
const iString *     valueString_SiteSpec    (const iString *site, enum iSiteSpecKey key);
const iStringArray *strings_SiteSpec        (const iString *site, enum iSiteSpecKey key);
