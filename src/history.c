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

#include "history.h"
#include "app.h"

#include <the_Foundation/file.h>
#include <the_Foundation/mutex.h>
#include <the_Foundation/path.h>

static const size_t maxStack_History_ = 50; /* back/forward navigable items */

void init_RecentUrl(iRecentUrl *d) {
    init_String(&d->url);
    d->normScrollY = 0;
    d->cachedResponse = NULL;
}

void deinit_RecentUrl(iRecentUrl *d) {
    deinit_String(&d->url);
    delete_GmResponse(d->cachedResponse);
}

iDefineTypeConstruction(RecentUrl)

iRecentUrl *copy_RecentUrl(const iRecentUrl *d) {
    iRecentUrl *copy = new_RecentUrl();
    set_String(&copy->url, &d->url);
    copy->normScrollY = d->normScrollY;
    copy->cachedResponse = d->cachedResponse ? copy_GmResponse(d->cachedResponse) : NULL;
    return copy;
}

/*----------------------------------------------------------------------------------------------*/

struct Impl_History {
    iMutex *mtx;
    iArray recent;    /* TODO: should be specific to a DocumentWidget */
    size_t recentPos; /* zero at the latest item */
};

iDefineTypeConstruction(History)

void init_History(iHistory *d) {
    d->mtx = new_Mutex();
    init_Array(&d->recent, sizeof(iRecentUrl));
    d->recentPos = 0;
}

void deinit_History(iHistory *d) {
    iGuardMutex(d->mtx, {
        clear_History(d);
        deinit_Array(&d->recent);
    });
    delete_Mutex(d->mtx);
}

iHistory *copy_History(const iHistory *d) {
    lock_Mutex(d->mtx);
    iHistory *copy = new_History();
    iConstForEach(Array, i, &d->recent) {
        pushBack_Array(&copy->recent, copy_RecentUrl(i.value));
    }
    copy->recentPos = d->recentPos;
    unlock_Mutex(d->mtx);
    return copy;
}

void serialize_History(const iHistory *d, iStream *outs) {
    lock_Mutex(d->mtx);
    writeU16_Stream(outs, d->recentPos);
    writeU16_Stream(outs, size_Array(&d->recent));
    iConstForEach(Array, i, &d->recent) {
        const iRecentUrl *item = i.value;
        serialize_String(&item->url, outs);
        write32_Stream(outs, item->normScrollY * 1.0e6f);
        if (item->cachedResponse) {
            write8_Stream(outs, 1);
            serialize_GmResponse(item->cachedResponse, outs);
        }
        else {
            write8_Stream(outs, 0);
        }
    }
    unlock_Mutex(d->mtx);
}

void deserialize_History(iHistory *d, iStream *ins) {
    clear_History(d);
    lock_Mutex(d->mtx);
    d->recentPos = readU16_Stream(ins);
    size_t count = readU16_Stream(ins);
    while (count--) {
        iRecentUrl item;
        init_RecentUrl(&item);
        deserialize_String(&item.url, ins);
        item.normScrollY = (float) read32_Stream(ins) / 1.0e6f;
        if (read8_Stream(ins)) {
            item.cachedResponse = new_GmResponse();
            deserialize_GmResponse(item.cachedResponse, ins);
        }
        pushBack_Array(&d->recent, &item);
    }
    unlock_Mutex(d->mtx);
}

void clear_History(iHistory *d) {
    lock_Mutex(d->mtx);
    iForEach(Array, s, &d->recent) {
        deinit_RecentUrl(s.value);
    }
    clear_Array(&d->recent);
    unlock_Mutex(d->mtx);
}

iRecentUrl *recentUrl_History(iHistory *d, size_t pos) {
    if (isEmpty_Array(&d->recent)) return NULL;
    return &value_Array(&d->recent, size_Array(&d->recent) - 1 - pos, iRecentUrl);
}

const iRecentUrl *constRecentUrl_History(const iHistory *d, size_t pos) {
    if (isEmpty_Array(&d->recent)) return NULL;
    return &constValue_Array(&d->recent, size_Array(&d->recent) - 1 - pos, iRecentUrl);
}

iRecentUrl *mostRecentUrl_History(iHistory *d) {
    return recentUrl_History(d, d->recentPos);
}

const iRecentUrl *constMostRecentUrl_History(const iHistory *d) {
    return constRecentUrl_History(d, d->recentPos);
}

const iString *url_History(const iHistory *d, size_t pos) {
    const iRecentUrl *item = constRecentUrl_History(d, pos);
    if (item) {
        return &item->url;
    }
    return collectNew_String();
}

iRecentUrl *findUrl_History(iHistory *d, const iString *url) {
    lock_Mutex(d->mtx);
    iReverseForEach(Array, i, &d->recent) {
        if (cmpStringCase_String(url, &((iRecentUrl *) i.value)->url) == 0) {
            unlock_Mutex(d->mtx);
            return i.value;
        }
    }
    unlock_Mutex(d->mtx);
    return NULL;
}

void replace_History(iHistory *d, const iString *url) {
    lock_Mutex(d->mtx);
    /* Update in the history. */
    iRecentUrl *item = mostRecentUrl_History(d);
    if (item) {
        set_String(&item->url, url);
    }
    unlock_Mutex(d->mtx);
}

void add_History(iHistory *d, const iString *url ){
    lock_Mutex(d->mtx);
    /* Cut the trailing history items. */
    if (d->recentPos > 0) {
        for (size_t i = 0; i < d->recentPos - 1; i++) {
            deinit_RecentUrl(recentUrl_History(d, i));
        }
        removeN_Array(&d->recent, size_Array(&d->recent) - d->recentPos, iInvalidSize);
        d->recentPos = 0;
    }
    /* Insert new item. */
    const iRecentUrl *lastItem = recentUrl_History(d, 0);
    if (!lastItem || cmpString_String(&lastItem->url, url) != 0) {
        iRecentUrl item;
        init_RecentUrl(&item);
        set_String(&item.url, url);
        pushBack_Array(&d->recent, &item);
        /* Limit the number of items. */
        if (size_Array(&d->recent) > maxStack_History_) {
            deinit_RecentUrl(front_Array(&d->recent));
            remove_Array(&d->recent, 0);
        }
    }
    unlock_Mutex(d->mtx);
}

iBool goBack_History(iHistory *d) {
    lock_Mutex(d->mtx);
    if (d->recentPos < size_Array(&d->recent) - 1) {
        d->recentPos++;
        postCommandf_App("open history:1 scroll:%f url:%s",
                         mostRecentUrl_History(d)->normScrollY,
                         cstr_String(url_History(d, d->recentPos)));
        unlock_Mutex(d->mtx);
        return iTrue;
    }
    unlock_Mutex(d->mtx);
    return iFalse;
}

iBool goForward_History(iHistory *d) {
    lock_Mutex(d->mtx);
    if (d->recentPos > 0) {
        d->recentPos--;
        postCommandf_App("open history:1 scroll:%f url:%s",
                         mostRecentUrl_History(d)->normScrollY,
                         cstr_String(url_History(d, d->recentPos)));
        unlock_Mutex(d->mtx);
        return iTrue;
    }
    unlock_Mutex(d->mtx);
    return iFalse;
}

const iGmResponse *cachedResponse_History(const iHistory *d) {
    const iRecentUrl *item = constMostRecentUrl_History(d);
    return item ? item->cachedResponse : NULL;
}

void setCachedResponse_History(iHistory *d, const iGmResponse *response) {
    lock_Mutex(d->mtx);
    iRecentUrl *item = mostRecentUrl_History(d);
    if (item) {
        delete_GmResponse(item->cachedResponse);
        item->cachedResponse = NULL;
        if (category_GmStatusCode(response->statusCode) == categorySuccess_GmStatusCode) {
            item->cachedResponse = copy_GmResponse(response);
        }
    }
    unlock_Mutex(d->mtx);
}

const iStringArray *searchContents_History(const iHistory *d, const iRegExp *pattern) {
    iStringArray *urls = iClob(new_StringArray());
    lock_Mutex(d->mtx);
    iConstForEach(Array, i, &d->recent) {
        const iRecentUrl *url = i.value;
        const iGmResponse *resp = url->cachedResponse;
        if (resp && category_GmStatusCode(resp->statusCode) == categorySuccess_GmStatusCode) {
            if (indexOfCStrSc_String(&resp->meta, "text/", &iCaseInsensitive) == iInvalidPos) {
                continue;
            }
            iRegExpMatch m;
            init_RegExpMatch(&m);
            if (matchRange_RegExp(pattern, range_Block(&resp->body), &m)) {
                iString entry;
                init_String(&entry);
                iRangei cap = m.range;
                cap.start   = iMax(cap.start - 4, 0);
                cap.end     = iMin(cap.end + 10, (int) size_Block(&resp->body));
                iString content;
                initRange_String(&content, (iRangecc){ m.subject + cap.start, m.subject + cap.end });
                /* This needs cleaning up; highlight the matched word. */ {

                }
                format_String(&entry, "match len:%zu str:%s", size_String(&content), cstr_String(&content));
                deinit_String(&content);
                //appendRange_String(&entry, );
                appendFormat_String(&entry, " url:%s", cstr_String(&url->url));
                pushBack_StringArray(urls, &entry);
                deinit_String(&entry);
            }
        }
    }
    unlock_Mutex(d->mtx);
    return urls;
}
