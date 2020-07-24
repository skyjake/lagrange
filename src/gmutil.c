#include "gmutil.h"

#include <the_Foundation/regexp.h>
#include <the_Foundation/object.h>

void init_Url(iUrl *d, const iString *text) {
    iRegExp *pattern =
        new_RegExp("(.+)://([^/:?]*)(:[0-9]+)?([^?]*)(\\?.*)?", caseInsensitive_RegExpOption);
    iRegExpMatch m;
    if (matchString_RegExp(pattern, text, &m)) {
        d->protocol = capturedRange_RegExpMatch(&m, 1);
        d->host     = capturedRange_RegExpMatch(&m, 2);
        d->port     = capturedRange_RegExpMatch(&m, 3);
        if (!isEmpty_Range(&d->port)) {
            /* Don't include the colon. */
            d->port.start++;
        }
        d->path  = capturedRange_RegExpMatch(&m, 4);
        d->query = capturedRange_RegExpMatch(&m, 5);
    }
    else {
        iZap(*d);
    }
    iRelease(pattern);
}
