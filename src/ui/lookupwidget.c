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

#include "lookupwidget.h"
#include "lookup.h"
#include "listwidget.h"

#include <the_Foundation/mutex.h>
#include <the_Foundation/thread.h>

iDeclareType(LookupJob)

struct Impl_LookupJob {
    iString term;
    iPtrArray results;
};

static void init_LookupJob(iLookupJob *d) {
    init_String(&d->term);
    init_PtrArray(&d->results);
}

static void deinit_LookupJob(iLookupJob *d) {
    iForEach(PtrArray, i, &d->results) {
        delete_LookupResult(i.ptr);
    }
    deinit_PtrArray(&d->results);
    deinit_String(&d->term);
}

iDefineTypeConstruction(LookupJob)

struct Impl_LookupWidget {
    iWidget widget;
    iListWidget *list;
    iThread *work;
    iCondition jobAvailable; /* wakes up the work thread */
    iMutex *mtx;
    iString nextJob;
    iLookupJob *finishedJob;
};

static iThreadResult worker_LookupWidget_(iThread *thread) {
    iLookupWidget *d = userData_Thread(thread);
    lock_Mutex(d->mtx);
    for (;;) {
        wait_Condition(&d->jobAvailable, d->mtx);
        if (isEmpty_String(&d->nextJob)) {
            break; /* Time to quit. */
        }
        iLookupJob *job = new_LookupJob();
        set_String(&job->term, &d->nextJob);
        clear_String(&d->nextJob);
        unlock_Mutex(d->mtx);
        /* Do the lookup. */ {

        }
        /* Submit the result. */
        lock_Mutex(d->mtx);
        if (d->finishedJob) {
            /* Previous results haven't been taken yet. */
            delete_LookupJob(d->finishedJob);
        }
        d->finishedJob = job;
    }
    unlock_Mutex(d->mtx);
    printf("[LookupWidget] worker has quit\n"); fflush(stdout);
    return 0;
}

iDefineObjectConstruction(LookupWidget)

void init_LookupWidget(iLookupWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, "lookup");
    setFlags_Widget(w, resizeChildren_WidgetFlag, iTrue);
    d->list = addChild_Widget(w, iClob(new_ListWidget()));
    d->work = new_Thread(worker_LookupWidget_);
    setUserData_Thread(d->work, d);
    init_Condition(&d->jobAvailable);
    d->mtx = new_Mutex();
    init_String(&d->nextJob);
    d->finishedJob = NULL;
}

void deinit_LookupWidget(iLookupWidget *d) {
    /* Stop the worker. */ {
        iGuardMutex(d->mtx, {
            clear_String(&d->nextJob);
            signal_Condition(&d->jobAvailable);
        });
        join_Thread(d->work);
        iRelease(d->work);
    }
    delete_LookupJob(d->finishedJob);
    deinit_String(&d->nextJob);
    delete_Mutex(d->mtx);
    deinit_Condition(&d->jobAvailable);
}

static void draw_LookupWidget_(const iLookupWidget *d) {
    const iWidget *w = constAs_Widget(d);
    draw_Widget(w);
}

static iBool processEvent_LookupWidget_(iLookupWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    return processEvent_Widget(w, ev);
}

iBeginDefineSubclass(LookupWidget, Widget)
    .draw         = (iAny *) draw_LookupWidget_,
    .processEvent = (iAny *) processEvent_LookupWidget_,
iEndDefineSubclass(LookupWidget)
