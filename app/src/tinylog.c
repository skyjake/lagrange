/* Copyright 2026 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "tinylog.h"

#include <lagrange/gmutil.h>
#include <the_Foundation/stringset.h>
#include <ctype.h>

static iBool readDigits_(const char **pos, const char *end, int count, int *value_out) {
    int value = 0;
    const char *p = *pos;
    for (int i = 0; i < count; i++, p++) {
        if (p >= end || !isdigit((unsigned char) *p)) {
            return iFalse;
        }
        value = value * 10 + (*p - '0');
    }
    *pos = p;
    *value_out = value;
    return iTrue;
}

static iBool readChar_(const char **pos, const char *end, char ch) {
    if (*pos < end && **pos == ch) {
        (*pos)++;
        return iTrue;
    }
    return iFalse;
}

static void skipSpace_(const char **pos, const char *end) {
    while (*pos < end && (**pos == ' ' || **pos == '\t')) {
        (*pos)++;
    }
}

static int64_t daysFromCivil_(int y, int m, int d) {
    /* Days since 1970-01-01 in the proleptic Gregorian calendar. */
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civilFromDays_(int64_t z, int *y_out, int *m_out, int *d_out) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp  = (5 * doy + 2) / 153;
    const int     d   = (int) (doy - (153 * mp + 2) / 5 + 1);
    const int     m   = (int) (mp < 10 ? mp + 3 : mp - 9);
    *y_out = (int) (yoe + era * 400 + (m <= 2));
    *m_out = m;
    *d_out = d;
}

iBool parse_TinylogStamp(iTinylogStamp *d, iRangecc text) {
    iZap(*d);
    const char *p   = text.start;
    const char *end = text.end;
    skipSpace_(&p, end);
    if (!readDigits_(&p, end, 4, &d->year) || !readChar_(&p, end, '-') ||
        !readDigits_(&p, end, 2, &d->month) || !readChar_(&p, end, '-') ||
        !readDigits_(&p, end, 2, &d->day)) {
        return iFalse;
    }
    if (d->month < 1 || d->month > 12 || d->day < 1 || d->day > 31) {
        return iFalse;
    }
    const char *timeStart = p;
    if (!readChar_(&p, end, 'T')) {
        skipSpace_(&p, end);
    }
    if (readDigits_(&p, end, 2, &d->hour) && readChar_(&p, end, ':') &&
        readDigits_(&p, end, 2, &d->minute) && d->hour < 24 && d->minute < 60) {
        d->hasTime = iTrue;
        const char *secStart = p;
        if (readChar_(&p, end, ':') && readDigits_(&p, end, 2, &d->second) && d->second <= 60) {
            d->hasSeconds = iTrue;
        }
        else {
            p = secStart;
            d->second = 0;
        }
        skipSpace_(&p, end);
        if (p < end && (*p == '+' || *p == '-')) {
            const int sign = (*p == '-' ? -1 : 1);
            const char *zoneStart = p++;
            int hours, minutes = 0;
            if (readDigits_(&p, end, 2, &hours)) {
                readChar_(&p, end, ':');
                readDigits_(&p, end, 2, &minutes);
                d->gmtOffsetMinutes = sign * (hours * 60 + minutes);
                d->hasZone = iTrue;
            }
            else {
                p = zoneStart;
            }
        }
        else if (p < end && *p == 'Z') {
            d->hasZone = iTrue;
        }
        else if (end - p >= 3 && (!iCmpStrNCase(p, "UTC", 3) || !iCmpStrNCase(p, "GMT", 3))) {
            d->hasZone = iTrue;
        }
        /* Other timezone abbreviations are treated as UTC. */
    }
    else {
        p = timeStart;
        d->hour = d->minute = 0;
    }
    d->utc.ts.tv_sec = (time_t) (daysFromCivil_(d->year, d->month, d->day) * 86400 +
                                 d->hour * 3600 + d->minute * 60 + d->second -
                                 d->gmtOffsetMinutes * 60);
    d->utc.ts.tv_nsec = 0;
    return iTrue;
}

iBool matches_TinylogStamp(const iTinylogStamp *d, const iTinylogStamp *ref) {
    if (!d->hasTime || !d->hasZone) {
        /* Compare the wall-clock time as written. */
        if (d->year != ref->year || d->month != ref->month || d->day != ref->day) {
            return iFalse;
        }
        if (!d->hasTime) {
            return iTrue;
        }
        if (d->hour != ref->hour || d->minute != ref->minute) {
            return iFalse;
        }
        return !d->hasSeconds || !ref->hasSeconds || d->second == ref->second;
    }
    const int64_t a = d->utc.ts.tv_sec;
    const int64_t b = ref->utc.ts.tv_sec;
    if (d->hasSeconds && ref->hasSeconds) {
        return a == b;
    }
    return a / 60 == b / 60;
}

iString *format_TinylogStamp(const iTinylogStamp *d) {
    iString *str = newFormat_String("%04d-%02d-%02d", d->year, d->month, d->day);
    if (d->hasTime) {
        appendFormat_String(str, " %02d:%02d", d->hour, d->minute);
        if (d->hasSeconds) {
            appendFormat_String(str, ":%02d", d->second);
        }
        if (d->hasZone) {
            const int offset = iAbs(d->gmtOffsetMinutes);
            appendFormat_String(str, " %c%02d%02d",
                                d->gmtOffsetMinutes < 0 ? '-' : '+',
                                offset / 60, offset % 60);
        }
    }
    return str;
}

static iString *normalizedId_TinylogStamp_(const iTinylogStamp *d) {
    const int64_t secs = d->utc.ts.tv_sec;
    int64_t days = secs / 86400;
    int64_t rem  = secs % 86400;
    if (rem < 0) {
        rem += 86400;
        days--;
    }
    int y, m, day;
    civilFromDays_(days, &y, &m, &day);
    return newFormat_String("%04d%02d%02dT%02d%02d%02dZ",
                            y, m, day, (int) (rem / 3600), (int) (rem / 60 % 60), (int) (rem % 60));
}

/*----------------------------------------------------------------------------------------------*/

static void init_TinylogEntry_(iTinylogEntry *d) {
    iZap(d->stamp);
    init_String(&d->id);
    d->range   = iNullRange;
    d->content = iNullRange;
}

static void deinit_TinylogEntry_(iTinylogEntry *d) {
    deinit_String(&d->id);
}

static iBool isPreformatToggle_(iRangecc line) {
    return startsWith_Rangecc(line, "```");
}

static iBool entryHeading_(iRangecc line, iRangecc *text_out) {
    if (!startsWith_Rangecc(line, "##") || startsWith_Rangecc(line, "###")) {
        return iFalse;
    }
    line.start += 2;
    trim_Rangecc(&line);
    *text_out = line;
    return iTrue;
}

static iBool isHeading1_(iRangecc line) {
    return startsWith_Rangecc(line, "#") && !startsWith_Rangecc(line, "##");
}

static iRangecc metadataValue_(iRangecc line, const char *key) {
    trim_Rangecc(&line);
    if (startsWithCase_Rangecc(line, key)) {
        line.start += strlen(key);
        trim_Rangecc(&line);
        return line;
    }
    return iNullRange;
}

void init_Tinylog(iTinylog *d) {
    init_String(&d->source);
    init_String(&d->title);
    init_String(&d->author);
    init_String(&d->avatar);
    d->isAuthorDeclared = iFalse;
    init_Array(&d->entries, sizeof(iTinylogEntry));
}

static void clearEntries_Tinylog_(iTinylog *d) {
    iForEach(Array, i, &d->entries) {
        deinit_TinylogEntry_(i.value);
    }
    clear_Array(&d->entries);
}

void deinit_Tinylog(iTinylog *d) {
    clearEntries_Tinylog_(d);
    deinit_Array(&d->entries);
    deinit_String(&d->avatar);
    deinit_String(&d->author);
    deinit_String(&d->title);
    deinit_String(&d->source);
}

iDefineTypeConstruction(Tinylog)

void parse_Tinylog(iTinylog *d, const iString *source, const iString *feedUrl) {
    clearEntries_Tinylog_(d);
    set_String(&d->source, source);
    clear_String(&d->title);
    clear_String(&d->author);
    clear_String(&d->avatar);
    d->isAuthorDeclared = iFalse;
    const iRangecc src   = range_String(&d->source);
    iRangecc       line  = iNullRange;
    iBool          isPre = iFalse;
    while (nextSplit_Rangecc(src, "\n", &line)) {
        iRangecc trimmed = line;
        trimEnd_Rangecc(&trimmed);
        if (isPreformatToggle_(trimmed)) {
            isPre = !isPre;
            continue;
        }
        if (isPre) {
            continue;
        }
        iRangecc headingText;
        iTinylogStamp stamp;
        if (entryHeading_(trimmed, &headingText) && parse_TinylogStamp(&stamp, headingText)) {
            if (!isEmpty_Array(&d->entries)) {
                ((iTinylogEntry *) back_Array(&d->entries))->range.end = line.start;
            }
            iTinylogEntry entry;
            init_TinylogEntry_(&entry);
            entry.stamp       = stamp;
            entry.range.start = line.start;
            entry.content     = (iRangecc){ line.end, line.end };
            pushBack_Array(&d->entries, &entry);
            continue;
        }
        if (isEmpty_Array(&d->entries)) {
            /* Header section. */
            iRangecc       author = metadataValue_(trimmed, "author:");
            const iRangecc avatar = metadataValue_(trimmed, "avatar:");
            while (author.end > author.start && author.end[-1] == '/') {
                author.end--;
            }
            if (isEmpty_String(&d->title) && isHeading1_(trimmed)) {
                iRangecc title = { trimmed.start + 1, trimmed.end };
                trim_Rangecc(&title);
                setRange_String(&d->title, title);
            }
            else if (!isEmpty_Range(&author) && *author.start == '@') {
                setRange_String(&d->author, author);
                d->isAuthorDeclared = iTrue;
            }
            else if (!isEmpty_Range(&avatar)) {
                iRangecc word = iNullRange;
                nextSplit_Rangecc(avatar, " ", &word);
                setRange_String(&d->avatar, word);
            }
        }
    }
    if (!isEmpty_Array(&d->entries)) {
        ((iTinylogEntry *) back_Array(&d->entries))->range.end = src.end;
    }
    /* Finalize the entries. */ {
        iStringSet *ids = new_StringSet();
        iForEach(Array, i, &d->entries) {
            iTinylogEntry *entry = i.value;
            entry->content.end   = entry->range.end;
            trim_Rangecc(&entry->content);
            iString *id = normalizedId_TinylogStamp_(&entry->stamp);
            set_String(&entry->id, id);
            for (int n = 2; contains_StringSet(ids, &entry->id); n++) {
                format_String(&entry->id, "%s-%d", cstr_String(id), n);
            }
            insert_StringSet(ids, &entry->id);
            delete_String(id);
        }
        iRelease(ids);
    }
    if (isEmpty_String(&d->author) && feedUrl) {
        iString *author = derivedAuthor_Tinylog(feedUrl);
        set_String(&d->author, author);
        delete_String(author);
    }
    if (isEmpty_String(&d->title)) {
        set_String(&d->title, &d->author);
    }
}

const iTinylogEntry *findEntry_Tinylog(const iTinylog *d, const iString *id) {
    iConstForEach(Array, i, &d->entries) {
        const iTinylogEntry *entry = i.value;
        if (equal_String(&entry->id, id)) {
            return entry;
        }
    }
    return NULL;
}

const iTinylogEntry *findStamp_Tinylog(const iTinylog *d, const iTinylogStamp *stamp) {
    iConstForEach(Array, i, &d->entries) {
        const iTinylogEntry *entry = i.value;
        if (matches_TinylogStamp(stamp, &entry->stamp)) {
            return entry;
        }
    }
    return NULL;
}

/*----------------------------------------------------------------------------------------------*/

iDeclareType(LinkLine)

struct Impl_LinkLine {
    iRangecc url;
    iRangecc label;
};

static iBool parseLink_(iRangecc line, iLinkLine *link_out) {
    if (!startsWith_Rangecc(line, "=>")) {
        return iFalse;
    }
    line.start += 2;
    trim_Rangecc(&line);
    if (isEmpty_Range(&line)) {
        return iFalse;
    }
    const char *p = line.start;
    while (p < line.end && *p != ' ' && *p != '\t') {
        p++;
    }
    link_out->url   = (iRangecc){ line.start, p };
    link_out->label = (iRangecc){ p, line.end };
    trim_Rangecc(&link_out->label);
    return iTrue;
}

static iBool parseResponse_(iRangecc label, iTinylogStamp *stamp_out) {
    /* "RE: @author[@capsule] <date>" */
    if (!startsWithCase_Rangecc(label, "re:")) {
        return iFalse;
    }
    label.start += 3;
    trimStart_Rangecc(&label);
    if (!startsWith_Rangecc(label, "@")) {
        return iFalse;
    }
    while (label.start < label.end && *label.start != ' ' && *label.start != '\t') {
        label.start++;
    }
    return parse_TinylogStamp(stamp_out, label);
}

static iBool isLocalScheme_(const iString *url) {
    const iRangecc scheme = urlScheme_String(url);
    return isLocalFileScheme_Rangecc(scheme) || equalCase_Rangecc(scheme, "about");
}

iString *excerpt_TinylogEntry(const iTinylogEntry *d) {
    const size_t maxLen  = 150;
    iString     *text    = new_String();
    iRangecc     line    = iNullRange;
    iRangecc     preText = iNullRange;
    iBool        isPre   = iFalse;
    while (nextSplit_Rangecc(d->content, "\n", &line)) {
        iRangecc t = line;
        trim_Rangecc(&t);
        if (isEmpty_Range(&t)) {
            continue;
        }
        if (isPreformatToggle_(t)) {
            isPre = !isPre;
            continue;
        }
        if (isPre) {
            if (!preText.start) {
                preText = t;
            }
            continue;
        }
        else {
            iLinkLine link;
            if (parseLink_(t, &link)) {
                iTinylogStamp stamp;
                if (parseResponse_(link.label, &stamp)) {
                    continue;
                }
                t = isEmpty_Range(&link.label) ? link.url : link.label;
            }
            else {
                while (t.start < t.end && strchr("#*>", *t.start)) {
                    t.start++;
                }
                trimStart_Rangecc(&t);
            }
        }
        if (!isEmpty_Range(&t)) {
            setRange_String(text, t);
            break;
        }
    }
    if (isEmpty_String(text) && preText.start) {
        setRange_String(text, preText);
    }
    if (isEmpty_String(text)) {
        iRangecc heading = iNullRange;
        nextSplit_Rangecc(d->range, "\n", &heading);
        entryHeading_(heading, &heading);
        setRange_String(text, heading);
    }
    if (length_String(text) > maxLen) {
        truncate_String(text, maxLen - 1);
        trimEnd_String(text);
        appendCStr_String(text, "\u2026");
    }
    return text;
}

iString *content_TinylogEntry(const iTinylogEntry *d, const iString *feedUrl,
                              iTinylogResponseFunc responseFunc, void *context) {
    iString *out   = new_String();
    iRangecc line  = iNullRange;
    iBool    isPre = iFalse;
    while (nextSplit_Rangecc(d->content, "\n", &line)) {
        iRangecc t = line;
        trimEnd_Rangecc(&t);
        if (isEmpty_Range(&t) && isEmpty_String(out)) {
            continue; /* response link was omitted */
        }
        if (isPreformatToggle_(t)) {
            isPre = !isPre;
        }
        iLinkLine link;
        if (!isPre && parseLink_(t, &link)) {
            const iString *url =
                absoluteUrl_String(feedUrl, collect_String(newRange_String(link.url)));
            if (isLocalScheme_(url)) {
                /* Remote content must not refer to local resources. The space prevents the
                   text from being parsed as a link or a preformatted block. */
                appendCStr_String(out, " ");
                appendRange_String(out, isEmpty_Range(&link.label) ? link.url : link.label);
            }
            else {
                iTinylogStamp stamp;
                if (responseFunc && parseResponse_(link.label, &stamp) &&
                    responseFunc(context, url, &stamp)) {
                    continue;
                }
                appendFormat_String(out, "=> %s", cstr_String(url));
                if (!isEmpty_Range(&link.label)) {
                    appendCStr_String(out, " ");
                    appendRange_String(out, link.label);
                }
            }
        }
        else {
            appendRange_String(out, t);
        }
        appendCStr_String(out, "\n");
    }
    trimEnd_String(out);
    appendCStr_String(out, "\n");
    if (isPre) {
        appendCStr_String(out, "```\n");
    }
    return out;
}

/*----------------------------------------------------------------------------------------------*/

static iBool isDateStart_(iRangecc text) {
    iTinylogStamp stamp;
    return parse_TinylogStamp(&stamp, (iRangecc){ text.start, iMin(text.end, text.start + 10) });
}

iBool isTinylog_Source(const iString *source) {
    /* Gemfeed indices may also have dated headings, but those are followed by dated links. */
    const iRangecc src             = range_String(source);
    iRangecc       line            = iNullRange;
    iBool          isPre           = iFalse;
    iBool          isFirstHeading  = iTrue;
    iBool          isFirstDated    = iFalse;
    iBool          isAuthorDeclared = iFalse;
    size_t         numDated        = 0;
    size_t         numDatedLinks   = 0;
    while (nextSplit_Rangecc(src, "\n", &line)) {
        iRangecc t = line;
        trimEnd_Rangecc(&t);
        if (isPreformatToggle_(t)) {
            isPre = !isPre;
            continue;
        }
        if (isPre) {
            continue;
        }
        iRangecc  text;
        iLinkLine link;
        if (entryHeading_(t, &text)) {
            iTinylogStamp stamp;
            const iBool isDated = parse_TinylogStamp(&stamp, text);
            if (isFirstHeading) {
                isFirstDated   = isDated;
                isFirstHeading = iFalse;
            }
            numDated += isDated;
        }
        else if (parseLink_(t, &link) && isDateStart_(link.label)) {
            numDatedLinks++;
        }
        else if (numDated == 0) {
            const iRangecc author = metadataValue_(t, "author:");
            if (!isEmpty_Range(&author) && *author.start == '@') {
                isAuthorDeclared = iTrue;
            }
        }
    }
    if (numDated == 0) {
        return iFalse;
    }
    return isAuthorDeclared || (isFirstDated && numDatedLinks < numDated);
}

iDeclareType(EntryOrder)

struct Impl_EntryOrder {
    int64_t utc;
    size_t  index;
};

static int cmpNewestFirst_EntryOrder_(const void *a, const void *b) {
    const iEntryOrder *x = a, *y = b;
    if (x->utc != y->utc) {
        return x->utc > y->utc ? -1 : 1;
    }
    return iCmp(x->index, y->index);
}

static int cmpIndex_(const void *a, const void *b) {
    return iCmp(*(const size_t *) a, *(const size_t *) b);
}

iString *truncated_Tinylog(const iString *source, size_t maxSize) {
    /* The newest entries that fit are kept, in source order. */
    if (size_String(source) <= maxSize) {
        return copy_String(source);
    }
    iTinylog *log = new_Tinylog();
    parse_Tinylog(log, source, NULL);
    if (isEmpty_Array(&log->entries)) {
        delete_Tinylog(log);
        return copy_String(source);
    }
    const char          *start = constBegin_String(&log->source);
    const iTinylogEntry *first = constAt_Array(&log->entries, 0);
    iArray order;
    init_Array(&order, sizeof(iEntryOrder));
    iConstForEach(Array, i, &log->entries) {
        const iTinylogEntry *entry = i.value;
        pushBack_Array(&order,
                       &(iEntryOrder){ entry->stamp.utc.ts.tv_sec, index_ArrayConstIterator(&i) });
    }
    sort_Array(&order, cmpNewestFirst_EntryOrder_);
    iSortedArray kept;
    init_SortedArray(&kept, sizeof(size_t), cmpIndex_);
    size_t used = first->range.start - start; /* header */
    iConstForEach(Array, j, &order) {
        const iEntryOrder   *ord   = j.value;
        const iTinylogEntry *entry = constAt_Array(&log->entries, ord->index);
        const size_t         size  = size_Range(&entry->range);
        if (!isEmpty_SortedArray(&kept) && used + size > maxSize) {
            break; /* at least the newest entry is kept */
        }
        insert_SortedArray(&kept, &ord->index);
        used += size;
    }
    iString *result = newRange_String((iRangecc){ start, first->range.start });
    iConstForEach(Array, k, &kept.values) {
        const iTinylogEntry *entry = constAt_Array(&log->entries, *(const size_t *) k.value);
        if (!isEmpty_String(result) && !endsWith_String(result, "\n")) {
            appendCStr_String(result, "\n");
        }
        appendRange_String(result, entry->range);
    }
    deinit_SortedArray(&kept);
    deinit_Array(&order);
    delete_Tinylog(log);
    return result;
}

static void appendSlug_(iString *out, iRangecc text) {
    const size_t start = size_String(out);
    for (const char *ch = text.start; ch < text.end; ch++) {
        const unsigned char c = (unsigned char) *ch;
        if (isalnum(c) || c == '.' || c == '_' || c >= 0x80) {
            appendData_Block(&out->chars, &(char){ (char) tolower(c) }, 1);
        }
        else if (size_String(out) > start && !endsWith_String(out, "-")) {
            appendCStr_String(out, "-");
        }
    }
    while (size_String(out) > start && endsWith_String(out, "-")) {
        removeEnd_String(out, 1);
    }
}

iString *derivedAuthor_Tinylog(const iString *feedUrl) {
    iUrl parts;
    init_Url(&parts, feedUrl);
    iString *author = newCStr_String("@");
    iRangecc dir    = parts.path;
    while (dir.end > dir.start && dir.end[-1] != '/') {
        dir.end--; /* remove the file name */
    }
    while (dir.end > dir.start && dir.end[-1] == '/') {
        dir.end--;
    }
    iRangecc name = dir;
    for (const char *ch = dir.end; ch > dir.start; ch--) {
        if (ch[-1] == '/') {
            name.start = ch;
            break;
        }
    }
    if (startsWith_Rangecc(name, "~")) {
        name.start++;
    }
    if (!isEmpty_Range(&name)) {
        append_String(author, collect_String(urlDecode_String(collectNewRange_String(name))));
        appendCStr_String(author, "@");
    }
    iString *host = collectNewRange_String(parts.host);
    append_String(author, collect_String(lower_String(host)));
    return author;
}

iString *uriBase_Tinylog(const iString *feedUrl, const iString *author) {
    iUrl parts;
    init_Url(&parts, feedUrl);
    iString *base = newCStr_String("local:/tinylog/");
    appendSlug_(base, parts.host);
    if (!isEmpty_Range(&parts.port) && !equal_Rangecc(parts.port, "1965")) {
        appendCStr_String(base, "_");
        appendRange_String(base, parts.port);
    }
    appendCStr_String(base, "/");
    iRangecc name = range_String(author);
    if (startsWith_Rangecc(name, "@")) {
        name.start++;
    }
    for (const char *ch = name.start; ch < name.end; ch++) {
        if (*ch == '@') {
            name.end = ch;
            break;
        }
    }
    const size_t len = size_String(base);
    appendSlug_(base, name);
    if (size_String(base) == len) {
        appendSlug_(base, parts.host);
    }
    return base;
}

iString *pathSlug_Tinylog(const iString *feedUrl) {
    iUrl parts;
    init_Url(&parts, feedUrl);
    iString *slug = new_String();
    appendSlug_(slug, parts.path);
    if (isEmpty_String(slug)) {
        setCStr_String(slug, "root");
    }
    return slug;
}
