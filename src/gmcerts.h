#pragma once

#include <the_Foundation/tlsrequest.h>

iDeclareType(GmCerts)
iDeclareTypeConstructionArgs(GmCerts, const char *saveDir)

iBool   checkTrust_GmCerts  (iGmCerts *, iRangecc domain, const iTlsCertificate *cert);
