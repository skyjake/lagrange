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

#include "persistentstate.h"
#include <lagrange/defs.h>
#include <lagrange/lang.h>

int seconds_ReloadInterval(enum iReloadInterval d) {
    static const int mins[] = { 0, 1, 5, 15, 60, 4 * 60, 12 * 60, 24 * 60 };
    if (d < 0 || d >= max_ReloadInterval) return 0;
    return mins[d] * 60;
}

const char *label_ReloadInterval(enum iReloadInterval d) {
    switch (d) {
        case never_RelodPeriod:
            return cstr_Lang("reload.never");
        case day_ReloadInterval:
            return cstr_Lang("reload.onceperday");
        case minute_ReloadInterval:
        case fiveMinutes_ReloadInterval:
        case fifteenMinutes_ReloadInterval:
            return formatCStr_Lang("num.minutes.n", seconds_ReloadInterval(d) / 60);
        default:
            return formatCStr_Lang("num.hours.n", seconds_ReloadInterval(d) / 3600);
    }
    return "";
}

/*----------------------------------------------------------------------------------------------*/

void init_PersistentDocumentState(iPersistentDocumentState *d) {
    d->history        = new_History();
    d->url            = new_String();
    d->setIdentity    = NULL;
    d->reloadInterval = 0;
    d->generation     = 0;
}

void deinit_PersistentDocumentState(iPersistentDocumentState *d) {
    delete_Block(d->setIdentity);
    delete_String(d->url);
    delete_History(d->history);
}

void serialize_PersistentDocumentState(const iPersistentDocumentState *d, iStream *outs) {
    serializeWithContent_PersistentDocumentState(d, outs, iTrue);
}

void serializeWithContent_PersistentDocumentState(const iPersistentDocumentState *d, iStream *outs,
                                                 iBool withContent) {
    serialize_String(d->url, outs);
    uint16_t params = (d->reloadInterval & 7) | (iClamp(d->generation, 0, 15) << 4);
    writeU16_Stream(outs, params);
    /* Identity override. */ {
        iBlock empty;
        init_Block(&empty, 0);
        serialize_Block(d->setIdentity ? d->setIdentity : &empty, outs);
        deinit_Block(&empty);
    }
    serializeWithContent_History(d->history, outs, withContent);
}

void deserialize_PersistentDocumentState(iPersistentDocumentState *d, iStream *ins) {
    deserialize_String(d->url, ins);
    if (indexOfCStr_String(d->url, " ptr:0x") != iInvalidPos) {
        /* Oopsie, this should not have been written; invalid URL. */
        clear_String(d->url);
    }
    const uint16_t params = readU16_Stream(ins);
    d->reloadInterval = params & 7;
    d->generation     = params >> 4;
    if (version_Stream(ins) >= documentSetIdentity_FileVersion) {
        iBlock fp;
        init_Block(&fp, 0);
        deserialize_Block(&fp, ins);
        if (!isEmpty_Block(&fp)) {
            d->setIdentity = copy_Block(&fp);
        }
        deinit_Block(&fp);
    }
    deserialize_History(d->history, ins);
}

iDefineTypeConstruction(PersistentDocumentState)
