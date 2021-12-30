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
#include "ui/root.h"
#include "app.h"

#include <the_Foundation/file.h>
#include <the_Foundation/mutex.h>
#include <the_Foundation/path.h>
#include <the_Foundation/stringset.h>
#include <math.h>

static const size_t maxStack_History_ = 50; /* back/forward navigable items */

void init_RecentUrl(iRecentUrl *d) {
    init_String(&d->url);
    d->normScrollY    = 0;
    d->cachedResponse = NULL;
    d->cachedDoc      = NULL;
    d->flags          = 0;
}

void deinit_RecentUrl(iRecentUrl *d) {
    iRelease(d->cachedDoc);
    deinit_String(&d->url);
    delete_GmResponse(d->cachedResponse);
}

iDefineTypeConstruction(RecentUrl)

iRecentUrl *copy_RecentUrl(const iRecentUrl *d) {
    iRecentUrl *copy = new_RecentUrl();
    set_String(&copy->url, &d->url);
    copy->normScrollY    = d->normScrollY;
    copy->cachedResponse = d->cachedResponse ? copy_GmResponse(d->cachedResponse) : NULL;
    copy->cachedDoc      = ref_Object(d->cachedDoc);
    copy->flags          = d->flags;
    return copy;
}

size_t cacheSize_RecentUrl(const iRecentUrl *d) {
    size_t size = 0;
    if (d->cachedResponse) {
        size += size_String(&d->cachedResponse->meta);
        size += size_Block(&d->cachedResponse->body);
    }
    return size;    
}

size_t memorySize_RecentUrl(const iRecentUrl *d) {
    size_t size = cacheSize_RecentUrl(d);
    if (d->cachedDoc) {
        size += memorySize_GmDocument(d->cachedDoc);
    }
    return size;
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

void lock_History(iHistory *d) {
    lock_Mutex(d->mtx);
}

void unlock_History(iHistory *d) {
    unlock_Mutex(d->mtx);
}

iMemInfo memoryUsage_History(const iHistory *d) {
    iMemInfo mem = { 0, 0 };
    iConstForEach(Array, i, &d->recent) {
        const iRecentUrl *item = i.value;
        mem.cacheSize  += cacheSize_RecentUrl(item);
        mem.memorySize += memorySize_RecentUrl(item);
    }
    return mem;
}

iString *debugInfo_History(const iHistory *d) {
    iString *str = new_String();
    format_String(str,
                  "```\n"
                  "Idx |   Cache |   Memory | SP%% | URL\n"
                  "----+---------+----------+-----+-----\n");
    size_t totalCache = 0;
    size_t totalMemory = 0;
    iConstForEach(Array, i, &d->recent) {
        const iRecentUrl *item = i.value;
        appendFormat_String(
            str, " %2zu | ", size_Array(&d->recent) - index_ArrayConstIterator(&i) - 1);
        const size_t cacheSize = cacheSize_RecentUrl(item);
        const size_t memSize   = memorySize_RecentUrl(item);
        if (cacheSize) {
            appendFormat_String(str, "%7zu", cacheSize);
            totalCache += cacheSize;
        }
        else {
            appendFormat_String(str, "     --");
        }
        appendCStr_String(str, " | ");
        if (memSize) {
            appendFormat_String(str, "%8zu", memSize);
            totalMemory += memSize;
        }
        else {
            appendFormat_String(str, "      --");
        }
        appendFormat_String(str,
                            " | %3d | %s\n",
                            iRound(100.0f * item->normScrollY),
                            cstr_String(&item->url));
    }
    appendFormat_String(str, "\n```\n");
    appendFormat_String(str,
                        "Total cached data: %.3f MB\n"
                        "Total memory usage: %.3f MB\n"
                        "Navigation position: %zu\n\n",
                        totalCache / 1.0e6f,
                        totalMemory / 1.0e6f,
                        d->recentPos);
    return str;
}

void serialize_History(const iHistory *d, iStream *outs) {
    lock_Mutex(d->mtx);
    writeU16_Stream(outs, d->recentPos);
    writeU16_Stream(outs, size_Array(&d->recent));
    iConstForEach(Array, i, &d->recent) {
        const iRecentUrl *item = i.value;
        serialize_String(&item->url, outs);
        write32_Stream(outs, item->normScrollY * 1.0e6f);
        writeU16_Stream(outs, item->flags);
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
        set_String(&item.url, canonicalUrl_String(&item.url));
        item.normScrollY = (float) read32_Stream(ins) / 1.0e6f;
        if (version_Stream(ins) >= addedRecentUrlFlags_FileVersion) {
            item.flags = readU16_Stream(ins);
        }
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

#if 0
iRecentUrl *findUrl_History(iHistory *d, const iString *url, int timeDir) {
    url = canonicalUrl_String(url);
//    if (!timeDir) {
//        timeDir = -1;
//    }
    lock_Mutex(d->mtx);
    for (size_t i = size_Array(&d->recent) - 1 - d->recentPos; i < size_Array(&d->recent);
         i += timeDir) {
        iRecentUrl *item = at_Array(&d->recent, i);
        if (cmpStringCase_String(url, &item->url) == 0) {
            unlock_Mutex(d->mtx);
            return item; /* FIXME: Returning an internal pointer; should remain locked. */
        }
        if (!timeDir) break;
    }
    unlock_Mutex(d->mtx);
    return NULL;
}
#endif

void replace_History(iHistory *d, const iString *url) {
    url = canonicalUrl_String(url);
    lock_Mutex(d->mtx);
    /* Update in the history. */
    iRecentUrl *item = mostRecentUrl_History(d);
    if (item) {
        set_String(&item->url, url);
    }
    unlock_Mutex(d->mtx);
}

void add_History(iHistory *d, const iString *url) {
    url = canonicalUrl_String(url);
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

void undo_History(iHistory *d) {
    lock_Mutex(d->mtx);
    if (!isEmpty_Array(&d->recent) || d->recentPos != 0) {
        deinit_RecentUrl(back_Array(&d->recent));
        popBack_Array(&d->recent);
    }
    unlock_Mutex(d->mtx);    
}

iRecentUrl *precedingLocked_History(iHistory *d) {
    /* NOTE: Manual lock and unlock are required when using this; returning an internal pointer. */
    iBool ok = iFalse;
    //lock_Mutex(d->mtx);
    const size_t lastIndex = size_Array(&d->recent) - 1;
    if (!isEmpty_Array(&d->recent) && d->recentPos < lastIndex) {
        return at_Array(&d->recent, lastIndex - (d->recentPos + 1));
//        set_String(&recent_out->url, &recent->url);
//        recent_out->normScrollY = recent->normScrollY;
//        iChangeRef(recent_out->cachedDoc, recent->cachedDoc);
        /* Cached response is not returned, would involve a deep copy. */
//        ok = iTrue;
    }
    //unlock_Mutex(d->mtx);
//    return ok;
    return NULL;
}

#if 0
iBool following_History(iHistory *d, iRecentUrl *recent_out) {
    iBool ok = iFalse;
    lock_Mutex(d->mtx);
    if (!isEmpty_Array(&d->recent) && d->recentPos > 0) {
        const iRecentUrl *recent = constAt_Array(&d->recent, d->recentPos - 1);
        set_String(&recent_out->url, &recent->url);
        recent_out->normScrollY = recent->normScrollY;
        recent_out->cachedDoc = ref_Object(recent->cachedDoc);
        /* Cached response is not returned, would involve a deep copy. */
        ok = iTrue;
    }
    unlock_Mutex(d->mtx);
    return ok;
}
#endif

iBool goBack_History(iHistory *d) {
    lock_Mutex(d->mtx);
    if (!isEmpty_Array(&d->recent) && d->recentPos < size_Array(&d->recent) - 1) {
        d->recentPos++;
        postCommandf_Root(get_Root(),
                          "open history:1 scroll:%f url:%s",
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
        postCommandf_Root(get_Root(),
                          "open history:1 scroll:%f url:%s",
                          mostRecentUrl_History(d)->normScrollY,
                          cstr_String(url_History(d, d->recentPos)));
        unlock_Mutex(d->mtx);
        return iTrue;
    }
    unlock_Mutex(d->mtx);
    return iFalse;
}

iBool atNewest_History(const iHistory *d) {
    iBool isLatest;
    iGuardMutex(d->mtx, isLatest = (d->recentPos == 0));
    return isLatest;
}

iBool atOldest_History(const iHistory *d) {
    iBool isOldest;
    iGuardMutex(d->mtx, isOldest = (isEmpty_Array(&d->recent) ||
                                    d->recentPos == size_Array(&d->recent) - 1));
    return isOldest;
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

void setCachedDocument_History(iHistory *d, iGmDocument *doc) {
    lock_Mutex(d->mtx);
    iRecentUrl *item = mostRecentUrl_History(d);
    iAssert(size_GmDocument(doc).x > 0);
    if (item) {
#if !defined (NDEBUG)
        if (!equal_String(url_GmDocument(doc), &item->url)) {
            printf("[History] Cache mismatch! Expecting data for item {%s} but document URL is {%s}\n",
                   cstr_String(&item->url),
                   cstr_String(url_GmDocument(doc)));
        }
#endif
        if (item->cachedDoc != doc) {
            iRelease(item->cachedDoc);
            item->cachedDoc = ref_Object(doc);
        }
    }
    unlock_Mutex(d->mtx);
}

size_t cacheSize_History(const iHistory *d) {
    size_t cached = 0;
    lock_Mutex(d->mtx);
    iConstForEach(Array, i, &d->recent) {
        const iRecentUrl *url = i.value;
        cached += cacheSize_RecentUrl(url);
    }
    unlock_Mutex(d->mtx);
    return cached;
}

size_t memorySize_History(const iHistory *d) {
    size_t bytes = 0;
    lock_Mutex(d->mtx);
    iConstForEach(Array, i, &d->recent) {
        const iRecentUrl *url = i.value;
        bytes += memorySize_RecentUrl(url);
    }
    unlock_Mutex(d->mtx);
    return bytes;
}

void clearCache_History(iHistory *d) {
    lock_Mutex(d->mtx);
    iForEach(Array, i, &d->recent) {
        iRecentUrl *url = i.value;
        if (url->cachedResponse) {
            delete_GmResponse(url->cachedResponse);
            url->cachedResponse = NULL;
        }
        iReleasePtr(&url->cachedDoc); /* release all cached documents and media as well */
    }
    unlock_Mutex(d->mtx);
}

void invalidateCachedLayout_History(iHistory *d) {
    lock_Mutex(d->mtx);
    iForEach(Array, i, &d->recent) {
        iRecentUrl *url = i.value;
        if (url->cachedDoc) {
            invalidateLayout_GmDocument(url->cachedDoc);
        }
    }
    unlock_Mutex(d->mtx);
}

size_t pruneLeastImportant_History(iHistory *d) {
    size_t delta  = 0;
    size_t chosen = iInvalidPos;
    double score  = 0.0f;
    iTime now;
    initCurrent_Time(&now);
    lock_Mutex(d->mtx);
    iConstForEach(Array, i, &d->recent) {
        const iRecentUrl *url = i.value;
        if (url->cachedResponse) {
            const double urlScore =
                cacheSize_RecentUrl(url) *
                pow(secondsSince_Time(&now, &url->cachedResponse->when) / 60.0, 1.25);
            if (urlScore > score) {
                chosen = index_ArrayConstIterator(&i);
                score  = urlScore;
            }
        }
    }
    if (chosen != iInvalidPos) {
        iRecentUrl *url = at_Array(&d->recent, chosen);
        delta = cacheSize_RecentUrl(url);
        delete_GmResponse(url->cachedResponse);
        url->cachedResponse = NULL;
        iReleasePtr(&url->cachedDoc);
    }
    unlock_Mutex(d->mtx);
    return delta;
}

size_t pruneLeastImportantMemory_History(iHistory *d) {
    size_t delta  = 0;
    size_t chosen = iInvalidPos;
    double score  = 0.0f;
    iTime now;
    initCurrent_Time(&now);
    lock_Mutex(d->mtx);
    iConstForEach(Array, i, &d->recent) {
        const iRecentUrl *url = i.value;
        if (d->recentPos == size_Array(&d->recent) - index_ArrayConstIterator(&i) - 1) {
            continue; /* Not the current navigation position. */
        }
        if (url->cachedDoc) {
            const double urlScore =
                memorySize_RecentUrl(url) *
                (url->cachedResponse
                     ? pow(secondsSince_Time(&now, &url->cachedResponse->when) / 60.0, 1.25)
                     : 1.0);
            if (urlScore > score) {
                chosen = index_ArrayConstIterator(&i);
                score  = urlScore;
            }
        }
    }
    if (chosen != iInvalidPos) {
        iRecentUrl *url = at_Array(&d->recent, chosen);
        const size_t before = memorySize_RecentUrl(url);
        iReleasePtr(&url->cachedDoc);
        delta = before - memorySize_RecentUrl(url);
    }
    unlock_Mutex(d->mtx);
    return delta;
}

void invalidateTheme_History(iHistory *d) {
    lock_Mutex(d->mtx);
    iForEach(Array, i, &d->recent) {
        iRecentUrl *r = i.value;
        if (r->cachedDoc) {
            invalidatePalette_GmDocument(r->cachedDoc);
        }
    }
    unlock_Mutex(d->mtx);
}

const iStringArray *searchContents_History(const iHistory *d, const iRegExp *pattern) {
    iStringArray *urls = iClob(new_StringArray());
    lock_Mutex(d->mtx);
    iStringSet inserted;
    init_StringSet(&inserted);
    iReverseConstForEach(Array, i, &d->recent) {
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
                const int prefix = iMin(10, cap.start);
                cap.start   = cap.start - prefix;
                cap.end     = iMin(cap.end + 30, (int) size_Block(&resp->body));
                const size_t maxLen = 60;
                if (size_Range(&cap) > maxLen) {
                    cap.end = cap.start + maxLen;
                }
                iString content;
                initRange_String(&content, (iRangecc){ m.subject + cap.start, m.subject + cap.end });
                /* This needs cleaning up; highlight the matched word. */
                replace_Block(&content.chars, '\n', ' ');
                replace_Block(&content.chars, '\r', ' ');
                if (prefix + size_Range(&m.range) < size_String(&content)) {
                    insertData_Block(&content.chars, prefix + size_Range(&m.range), uiText_ColorEscape, 2);
                }
                insertData_Block(&content.chars, prefix, uiTextStrong_ColorEscape, 2);
                format_String(
                    &entry, "match len:%zu str:%s", size_String(&content), cstr_String(&content));
                deinit_String(&content);
                appendFormat_String(&entry, " url:%s", cstr_String(&url->url));
                if (!contains_StringSet(&inserted, &url->url)) {
                    pushFront_StringArray(urls, &entry);
                    insert_StringSet(&inserted, &url->url);
                }
                deinit_String(&entry);
            }
        }
    }
    deinit_StringSet(&inserted);
    unlock_Mutex(d->mtx);
    return urls;
}
