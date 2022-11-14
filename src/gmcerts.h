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

#include "defs.h"
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/stringset.h>
#include <the_Foundation/tlsrequest.h>

iDeclareType(GmIdentity)
iDeclareTypeConstruction(GmIdentity)
iDeclareTypeSerialization(GmIdentity)

enum iGmIdentityFlags {
    temporary_GmIdentityFlag = 0x1, /* not saved persistently */
    imported_GmIdentityFlag  = 0x2, /* user-provided files */
};

struct Impl_GmIdentity {
    iBlock fingerprint;
    iTlsCertificate *cert;
    iStringSet *useUrls;
    iChar icon;
    iString notes; /* private, local usage notes */
    int flags;
};

iBool   isUsed_GmIdentity           (const iGmIdentity *);
iBool   isUsedOn_GmIdentity         (const iGmIdentity *, const iString *url);
iBool   isUsedOnDomain_GmIdentity   (const iGmIdentity *, const iRangecc domain);

void    setUse_GmIdentity           (iGmIdentity *, const iString *url, iBool use);
void    clearUse_GmIdentity         (iGmIdentity *);
const iString *findUse_GmIdentity   (const iGmIdentity *, const iString *url);

const iString *name_GmIdentity(const iGmIdentity *);

/*----------------------------------------------------------------------------------------------*/

iDeclareType(GmCerts)
iDeclareTypeConstructionArgs(GmCerts, const char *saveDir)

typedef iBool (*iGmCertsIdentityFilterFunc)(void *context, const iGmIdentity *);

iBool               checkTrust_GmCerts      (iGmCerts *, iRangecc domain, uint16_t port,
                                             const iTlsCertificate *cert);
void                setTrusted_GmCerts      (iGmCerts *, iRangecc domain, uint16_t port,
                                             const iBlock *fingerprint, const iDate *validUntil);
iTime               domainValidUntil_GmCerts(const iGmCerts *, iRangecc domain, uint16_t port);

/**
 * Create a new self-signed TLS client certificate for identifying the user.
 * @a commonName and the other name parameters are inserted in the subject field
 * of the certificate.
 *
 * @param flags       Identity flags. A temporary identity is not saved persistently and
 *                    will be erased when the application is shut down.
 * @param validUntil  Expiration date. Must be in the future.
 *
 * @returns Created identity. GmCerts retains ownership of returned object.
 */
iGmIdentity *       newIdentity_GmCerts     (iGmCerts *, int flags, iDate validUntil,
                                             const iString *commonName, const iString *email,
                                             const iString *userId, const iString *domain,
                                             const iString *org, const iString *country);

void                importIdentity_GmCerts  (iGmCerts *, iTlsCertificate *cert,
                                             const iString *notes); /* takes ownership */
void                deleteIdentity_GmCerts  (iGmCerts *, iGmIdentity *identity);
void                saveIdentities_GmCerts  (const iGmCerts *);
void                serialize_GmCerts       (const iGmCerts *, iStream *trusted, iStream *identsMeta);
void                deserializeTrusted_GmCerts      (iGmCerts *, iStream *ins, enum iImportMethod method);
iBool               deserializeIdentities_GmCerts   (iGmCerts *, iStream *ins, enum iImportMethod method);

const iString *     certificatePath_GmCerts (const iGmCerts *, const iGmIdentity *identity);

iGmIdentity *       identity_GmCerts        (iGmCerts *, unsigned int id);
iGmIdentity *       findIdentity_GmCerts    (iGmCerts *, const iBlock *fingerprint);
iGmIdentity *       findIdentityFuzzy_GmCerts(iGmCerts *, const iString *fuzzy);
const iGmIdentity * constIdentity_GmCerts   (const iGmCerts *, unsigned int id);
const iGmIdentity * identityForUrl_GmCerts  (const iGmCerts *, const iString *url);
const iPtrArray *   identities_GmCerts      (const iGmCerts *);
const iPtrArray *   listIdentities_GmCerts  (const iGmCerts *, iGmCertsIdentityFilterFunc filter, void *context);

void                signIn_GmCerts          (iGmCerts *, iGmIdentity *identity, const iString *url);
void                signOut_GmCerts         (iGmCerts *, const iString *url);

iBool               verifyDomain_GmCerts    (const iTlsCertificate *cert, iRangecc domain);
