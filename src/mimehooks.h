#pragma once

#include <the_Foundation/audience.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/string.h>

iDeclareType(FilterHook)
iDeclareTypeConstruction(FilterHook)

struct Impl_FilterHook {
    iString  label;
    iString  mimePattern;
    iRegExp *mimeRegex;
    iString  command;
};

void    setMimePattern_FilterHook   (iFilterHook *, const iString *pattern);
void    setCommand_FilterHook       (iFilterHook *, const iString *command);

/*----------------------------------------------------------------------------------------------*/

iDeclareType(MimeHooks)
iDeclareTypeConstruction(MimeHooks)

iBool       willTryFilter_MimeHooks (const iMimeHooks *, const iString *mime);
iBlock *    tryFilter_MimeHooks     (const iMimeHooks *, const iString *mime,
                                     const iBlock *body, const iString *requestUrl);

void        load_MimeHooks          (iMimeHooks *, const char *saveDir);
void        save_MimeHooks          (const iMimeHooks *);

const iString *debugInfo_MimeHooks  (const iMimeHooks *);
