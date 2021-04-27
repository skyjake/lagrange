/* Copyright 2021 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "periodic.h"
#include "ui/widget.h"
#include "ui/window.h"
#include "app.h"

#include <the_Foundation/string.h>
#include <the_Foundation/thread.h>
#include <SDL_events.h>
#include <SDL_timer.h>

iDeclareType(PeriodicCommand)

struct Impl_PeriodicCommand {
    iAny *  context;
    iString command;
};

static void init_PeriodicCommand(iPeriodicCommand *d, iAny *context, const char *command) {
    d->context = context;
    initCStr_String(&d->command, command);
}

static void deinit_PeriodicCommand(iPeriodicCommand *d) {
    deinit_String(&d->command);
}

static int cmp_PeriodicCommand_(const void *a, const void *b) {
    const iPeriodicCommand *elems[2] = { a, b };
    return iCmp(elems[0]->context, elems[1]->context);
}

iDefineTypeConstructionArgs(PeriodicCommand, (iAny *ctx, const char *cmd), ctx, cmd)

/*----------------------------------------------------------------------------------------------*/

static const uint32_t postingInterval_Periodic_ = 500;

iBool dispatchCommands_Periodic(iPeriodic *d) {
    const uint32_t now = SDL_GetTicks();
    if (now - d->lastPostTime < postingInterval_Periodic_) {
        return iFalse;
    }
    d->lastPostTime = now;
    iBool wasPosted = iFalse;
    lock_Mutex(d->mutex);
    iConstForEach(Array, i, &d->commands.values) {
        const iPeriodicCommand *pc = i.value;
        const SDL_UserEvent ev = {
            .type  = SDL_USEREVENT,
            .code  = command_UserEventCode,
            .data1 = (void *) cstr_String(&pc->command)
        };
        iAssert(isInstance_Object(pc->context, &Class_Widget));
        setCurrent_Root(findRoot_Window(get_Window(), pc->context));
        dispatchEvent_Widget(pc->context, (const SDL_Event *) &ev);
        wasPosted = iTrue;
    }
    setCurrent_Root(NULL);
    unlock_Mutex(d->mutex);
    return wasPosted;
}

void init_Periodic(iPeriodic *d) {
    d->mutex = new_Mutex();
    init_SortedArray(&d->commands, sizeof(iPeriodicCommand), cmp_PeriodicCommand_);
    d->lastPostTime = 0;
}

void deinit_Periodic(iPeriodic *d) {
    iForEach(Array, i, &d->commands.values) {
        deinit_PeriodicCommand(i.value);
    }
    deinit_SortedArray(&d->commands);
    delete_Mutex(d->mutex);
}

void add_Periodic(iPeriodic *d, iAny *context, const char *command) {
    lock_Mutex(d->mutex);
    size_t pos;
    iPeriodicCommand key = { .context = context };
    if (locate_SortedArray(&d->commands, &key, &pos)) {
        iPeriodicCommand *pc = at_SortedArray(&d->commands, pos);
        setCStr_String(&pc->command, command);
    }
    else {
        iPeriodicCommand pc;
        init_PeriodicCommand(&pc, context, command);
        insert_SortedArray(&d->commands, &pc);
    }
    unlock_Mutex(d->mutex);
}

void remove_Periodic(iPeriodic *d, iAny *context) {
    lock_Mutex(d->mutex);
    size_t pos;
    iPeriodicCommand key = { .context = context };
    if (locate_SortedArray(&d->commands, &key, &pos)) {
        iPeriodicCommand *pc = at_SortedArray(&d->commands, pos);
        deinit_PeriodicCommand(pc);
        remove_Array(&d->commands.values, pos);
    }
    unlock_Mutex(d->mutex);
}
