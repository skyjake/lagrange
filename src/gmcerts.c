#include "gmcerts.h"

struct Impl_GmCerts {
    iString saveDir;
};

iDefineTypeConstructionArgs(GmCerts, (const char *saveDir), saveDir)

void init_GmCerts(iGmCerts *d, const char *saveDir) {
    initCStr_String(&d->saveDir, saveDir);
}

void deinit_GmCerts(iGmCerts *d) {
    deinit_String(&d->saveDir);
}
