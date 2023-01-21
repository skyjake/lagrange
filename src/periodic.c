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

static uint32_t postEvent_Periodic_(uint32_t interval, void *context) {
    iUnused(context);
    SDL_UserEvent ev = { .type      = SDL_USEREVENT,
                         .timestamp = SDL_GetTicks(),
                         .code      = periodic_UserEventCode };
    SDL_PushEvent((SDL_Event *) &ev);
    return interval;
}

static void startOrStopWakeupTimer_Periodic_(iPeriodic *d, iBool start) {
    if (start && !d->wakeupTimer) {
        d->wakeupTimer = SDL_AddTimer(postingInterval_Periodic_, postEvent_Periodic_, d);
    }
    else if (!start && d->wakeupTimer) {
        SDL_RemoveTimer(d->wakeupTimer);
        d->wakeupTimer = 0;
    }
}

static void removePending_Periodic_(iPeriodic *d) {
    iForEach(PtrSet, i, &d->pendingRemoval) {
        size_t pos;
        iPeriodicCommand key = { .context = *i.value };
        if (locate_SortedArray(&d->commands, &key, &pos)) {
            iPeriodicCommand *pc = at_SortedArray(&d->commands, pos);
            deinit_PeriodicCommand(pc);
            remove_Array(&d->commands.values, pos);
        }
    }
    clear_PtrSet(&d->pendingRemoval);
    if (isEmpty_SortedArray(&d->commands)) {
        startOrStopWakeupTimer_Periodic_(d, iFalse);
    }
}

static iBool isDispatching_;

iBool dispatchCommands_Periodic(iPeriodic *d) {
    const uint32_t now = SDL_GetTicks();
    if (now - d->lastPostTime < postingInterval_Periodic_) {
        return iFalse;
    }
    d->lastPostTime = now;
    iBool wasPosted = iFalse;
    lock_Mutex(d->mutex);
    isDispatching_ = iTrue;
    iAssert(isEmpty_PtrSet(&d->pendingRemoval));
    iConstForEach(Array, i, &d->commands.values) {
        const iPeriodicCommand *pc = i.value;
        iAssert(isInstance_Object(pc->context, &Class_Widget));
//        iAssert(~flags_Widget(constAs_Widget(pc->context)) & destroyPending_WidgetFlag);
        iAssert(!contains_PtrSet(&d->pendingRemoval, pc->context));
        iRoot *root = constAs_Widget(pc->context)->root;
        if (root) {
            const SDL_UserEvent ev = {
                .type     = SDL_USEREVENT,
                .code     = command_UserEventCode,
                .data1    = (void *) cstr_String(&pc->command),
                .data2    = root,
                .windowID = id_Window(root->window),
            };
            setCurrent_Window(root->window);
            setCurrent_Root(root);
            dispatchEvent_Widget(pc->context, (const SDL_Event *) &ev);
            wasPosted = iTrue;
        }
    }
    removePending_Periodic_(d);
    setCurrent_Root(NULL);
    isDispatching_ = iFalse;
    unlock_Mutex(d->mutex);
    return wasPosted;
}

void init_Periodic(iPeriodic *d) {
    d->mutex = new_Mutex();
    init_SortedArray(&d->commands, sizeof(iPeriodicCommand), cmp_PeriodicCommand_);
    d->lastPostTime = 0;
    init_PtrSet(&d->pendingRemoval);
    d->wakeupTimer = 0;
}

void deinit_Periodic(iPeriodic *d) {
    startOrStopWakeupTimer_Periodic_(d, iFalse);
    deinit_PtrSet(&d->pendingRemoval);
    iForEach(Array, i, &d->commands.values) {
        deinit_PeriodicCommand(i.value);
    }
    deinit_SortedArray(&d->commands);
    delete_Mutex(d->mutex);
}

void add_Periodic(iPeriodic *d, iAny *context, const char *command) {
    iWidget *contextWidget = as_Widget(context);
    iAssert(~flags_Widget(contextWidget) & destroyPending_WidgetFlag);
    contextWidget->flags2 |= usedAsPeriodicContext_WidgetFlag2;
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
    startOrStopWakeupTimer_Periodic_(d, iTrue);
    unlock_Mutex(d->mutex);
}

void remove_Periodic(iPeriodic *d, iAny *context) {
    lock_Mutex(d->mutex);
    insert_PtrSet(&d->pendingRemoval, context);
    if (!isDispatching_) {
        removePending_Periodic_(d);
    }
    unlock_Mutex(d->mutex);
}

iBool contains_Periodic(const iPeriodic *d, iAnyObject *context) {
    iPeriodicCommand key = { .context = context };
    return contains_SortedArray(&d->commands, &key);
}
