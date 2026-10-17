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

#include "tooljob.h"
#include "app.h"

#include <lagrange/tools.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/thread.h>

struct Impl_ToolJob {
    const iTool  *tool;
    iString       mime;
    iBlock        body;
    iString       url;
    iThread      *thread;
    iToolProcess *toolProc;
    iBlock       *output;
};

static iPtrArray *orphans_; /* accessed only in the main thread */

static void deleteFinishedOrphans_ToolJob_(void) {
    if (!orphans_) {
        return;
    }
    iForEach(PtrArray, i, orphans_) {
        iToolJob *job = i.ptr;
        if (isFinished_Thread(job->thread)) {
            delete_ToolJob(job);
            remove_PtrArrayIterator(&i);
        }
    }
}

void init_ToolJob(iToolJob *d, const iTool *tool, const iString *mime, const iBlock *body,
                  const iString *url) {
    deleteFinishedOrphans_ToolJob_();
    d->tool = tool;
    initCopy_String(&d->mime, mime);
    initCopy_Block(&d->body, body);
    initCopy_String(&d->url, url);
    d->thread   = NULL;
    d->toolProc = new_ToolProcess();
    d->output   = NULL;
}

void deinit_ToolJob(iToolJob *d) {
    if (d->thread) {
        join_Thread(d->thread);
        iRelease(d->thread);
    }
    delete_ToolProcess(d->toolProc);
    delete_Block(d->output);
    deinit_String(&d->url);
    deinit_Block(&d->body);
    deinit_String(&d->mime);
}

iDefineTypeConstructionArgs(ToolJob,
                            (const iTool *tool, const iString *mime, const iBlock *body,
                             const iString *url),
                            tool, mime, body, url)

static iThreadResult run_ToolJob_(iThread *thread) {
    iToolJob *d = userData_Thread(thread);
    d->output = run_ToolProcess(d->toolProc, d->tool, &d->mime, &d->body, &d->url);
    notifyf_App("tool.finished job:%p", d);
    return 0;
}

void start_ToolJob(iToolJob *d) {
    iAssert(!d->thread);
    d->thread = new_Thread(run_ToolJob_);
    setName_Thread(d->thread, "ToolJob");
    setUserData_Thread(d->thread, d);
    start_Thread(d->thread);
}

void orphan_ToolJob(iToolJob *d) {
    cancel_ToolProcess(d->toolProc);
    if (!orphans_) {
        orphans_ = new_PtrArray();
    }
    pushBack_PtrArray(orphans_, d);
    deleteFinishedOrphans_ToolJob_();
}

const iTool *tool_ToolJob(const iToolJob *d) {
    return d->tool;
}

const iString *url_ToolJob(const iToolJob *d) {
    return &d->url;
}

const iBlock *body_ToolJob(const iToolJob *d) {
    return &d->body;
}

const iBlock *output_ToolJob(const iToolJob *d) {
    return d->output;
}
