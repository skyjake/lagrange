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

#pragma once

#include <the_Foundation/array.h>
#include <the_Foundation/string.h>
#include <the_Foundation/time.h>

iDeclareType(TinylogStamp)

/* Timestamp as written in the source. */
struct Impl_TinylogStamp {
    int   year, month, day;
    int   hour, minute, second;
    int   gmtOffsetMinutes;
    iBool hasTime;
    iBool hasSeconds;
    iBool hasZone;
    iTime utc;
};

iBool   parse_TinylogStamp      (iTinylogStamp *, iRangecc text);
iBool   matches_TinylogStamp    (const iTinylogStamp *, const iTinylogStamp *reference);

iDeclareType(TinylogEntry)

struct Impl_TinylogEntry {
    iTinylogStamp stamp;
    iString  id;      /* normalized UTC timestamp; unique within the tinylog */
    iRangecc range;   /* entire section in the source, including the heading */
    iRangecc content; /* without the heading and surrounding empty lines */
};

iDeclareType(Tinylog)
iDeclareTypeConstruction(Tinylog)

struct Impl_Tinylog {
    iString source;
    iString title;  /* declared or derived */
    iString author; /* declared or derived ("@name[@capsule]") */
    iString avatar;
    iBool   isAuthorDeclared;
    iArray  entries; /* TinylogEntry, in source order */
};

void    parse_Tinylog           (iTinylog *, const iString *source, const iString *feedUrl);
const iTinylogEntry *findEntry_Tinylog  (const iTinylog *, const iString *id);
const iTinylogEntry *findStamp_Tinylog  (const iTinylog *, const iTinylogStamp *stamp);

iString *   excerpt_TinylogEntry    (const iTinylogEntry *);

/* Rewrites links of the entry content so they are absolute relative to the feed URL. Links to
   local resources are shown as plain text. A response link ("RE: @author <date>") is omitted
   if `responseFunc` returns true. */
typedef iBool (*iTinylogResponseFunc)(void *context, const iString *url,
                                      const iTinylogStamp *stamp);

iString *   content_TinylogEntry    (const iTinylogEntry *, const iString *feedUrl,
                                     iTinylogResponseFunc responseFunc, void *context);

/*----------------------------------------------------------------------------------------------*/

iBool       isTinylog_Source        (const iString *source);
iString *   truncated_Tinylog       (const iString *source, size_t maxSize);
iString *   derivedAuthor_Tinylog   (const iString *feedUrl);
iString *   uriBase_Tinylog         (const iString *feedUrl, const iString *author);
iString *   pathSlug_Tinylog        (const iString *feedUrl);
