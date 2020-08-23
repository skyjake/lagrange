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

#pragma once

#include <the_Foundation/tlsrequest.h>

iDeclareType(GmIdentity)
iDeclareTypeConstruction(GmIdentity)
iDeclareTypeSerialization(GmIdentity)

enum iGmIdentityFlags {
    temporary_GmIdentityFlag = 0x1, /* not saved persistently */
};

struct Impl_GmIdentity {
//    iString fileName;
    iBlock fingerprint;
    iTlsCertificate *cert;
    iSortedArray useUrls; /* Strings */
    iChar icon;
    iString notes; /* private, local usage notes */
    int flags;
};

iDeclareType(GmCerts)
iDeclareTypeConstructionArgs(GmCerts, const char *saveDir)

iBool               checkTrust_GmCerts      (iGmCerts *, iRangecc domain, const iTlsCertificate *cert);

const iGmIdentity * identityForUrl_GmCerts  (const iGmCerts *, const iString *url);

iGmIdentity *       newIdentity_GmCerts     (iGmCerts *, int flags, iDate validUntil,
                                             const iString *commonName, const iString *userId,
                                             const iString *org, const iString *country);
