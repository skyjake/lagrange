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

#include "lookup.h"

iDefineTypeConstruction(LookupResult)

void init_LookupResult(iLookupResult *d) {
    d->type = none_LookupResultType;
    d->relevance = 0;
    d->icon = 0;
    init_String(&d->label);
    init_String(&d->url);
    init_String(&d->meta);
    iZap(d->when);
}

void deinit_LookupResult(iLookupResult *d) {
    deinit_String(&d->meta);
    deinit_String(&d->url);
    deinit_String(&d->label);
}

iLookupResult *copy_LookupResult(const iLookupResult *d) {
    iLookupResult *copy = new_LookupResult();
    copy->type = d->type;
    copy->relevance = d->relevance;
    copy->icon = d->icon;
    set_String(&copy->label, &d->label);
    set_String(&copy->url, &d->url);
    set_String(&copy->meta, &d->meta);
    return copy;
}
