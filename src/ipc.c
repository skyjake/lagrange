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

#include "ipc.h"
#include "app.h"

#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/mutex.h>
#include <the_Foundation/path.h>
#include <the_Foundation/process.h>
#include <the_Foundation/time.h>

#include <signal.h>

iDeclareType(Ipc)

struct Impl_Ipc {
    iString dir;
    iBool isListening;
};

static iIpc ipc_;

static const char *lockFilePath_(const iIpc *d) {
    return concatPath_CStr(cstr_String(&d->dir), ".pid");
}

static const char *inputFilePath_(const iIpc *d, int pid) {
    return concatPath_CStr(cstr_String(&d->dir),
                           format_CStr(".run.%d.cfg", pid ? pid : currentId_Process()));
}

void init_Ipc(const char *runDir) {
    iIpc *d = &ipc_;
    initCStr_String(&d->dir, runDir);
    d->isListening = iFalse;
    signal(SIGUSR1, SIG_IGN);
}

void deinit_Ipc(void) {
    iIpc *d = &ipc_;
    signal(SIGUSR1, SIG_IGN);
    if (d->isListening) {
        remove(lockFilePath_(d));
    }
    deinit_String(&d->dir);
}

static void handleUserSignal_(int sig) {
    iIpc *d = &ipc_;
    iAssert(sig == SIGUSR1);
    iUnused(sig);
    const char *path = inputFilePath_(d, 0);
    iFile *f = newCStr_File(path);
    if (open_File(f, readOnly_FileMode)) {
        iString *cmds = new_String();
        initBlock_String(cmds, collect_Block(readAll_File(f)));
        iRangecc line = iNullRange;
        while (nextSplit_Rangecc(range_String(cmds), "\n", &line)) {
            postCommand_App(cstr_Rangecc(line));
        }
        delete_String(cmds);
    }
    iRelease(f);
    remove(path);
}

void listen_Ipc(void) {
    iIpc *d = &ipc_;
    signal(SIGUSR1, handleUserSignal_);
    iFile *f = newCStr_File(lockFilePath_(d));
    if (open_File(f, writeOnly_FileMode)) {
        printf_Stream(stream_File(f), "%u", currentId_Process());
        d->isListening = iTrue;
    }
    iRelease(f);
}

iProcessId check_Ipc(void) {
    const iIpc *d = &ipc_;
    iProcessId pid = 0;
    iFile *f = newCStr_File(lockFilePath_(d));
    if (open_File(f, readOnly_FileMode)) {
        const iBlock *running = collect_Block(readAll_File(f));
        close_File(f);
        pid = atoi(constData_Block(running));
        if (!exists_Process(pid)) {
            pid = 0;
            remove(cstr_String(path_File(f))); /* Stale. */
        }
    }
    iRelease(f);
    return pid;
}

/*----------------------------------------------------------------------------------------------*/

iDeclareType(IpcResponse)

struct Impl_IpcResponse {
    iString *output;
    iBool success;
    iMutex mtx;
    iCondition finished;
};

static void init_IpcResponse(iIpcResponse *d) {
    d->output = new_String();
    d->success = iFalse;
    init_Mutex(&d->mtx);
    init_Condition(&d->finished);
}

static void deinit_IpcResponse(iIpcResponse *d) {
    deinit_Condition(&d->finished);
    deinit_Mutex(&d->mtx);
    delete_String(d->output);
}

iDefineTypeConstruction(IpcResponse)

static iIpcResponse *response_;

static void handleSignal_IpcResponse_(int sig) {
    iUnused(sig);
    iAssert(response_);
    iIpcResponse *d = response_;
    lock_Mutex(&d->mtx);
    iFile *f = newCStr_File(inputFilePath_(&ipc_, 0));
    if (open_File(f, text_FileMode | readOnly_FileMode)) {
        iBlock *input = readAll_File(f);
        close_File(f);
        remove(cstr_String(path_File(f)));
        setBlock_String(d->output, input);
        d->success = iTrue;
        delete_Block(input);
    }
    iRelease(f);
    signal_Condition(&d->finished);
    unlock_Mutex(&d->mtx);
}

iBool write_Ipc(iProcessId pid, const iString *input, enum iIpcWrite type) {
    iBool ok = iFalse;
    iFile *f = newCStr_File(inputFilePath_(&ipc_, pid));
    if (open_File(f, text_FileMode | append_FileMode)) {
        write_File(f, utf8_String(input));
        if (type == command_IpcWrite) {
            printf_Stream(stream_File(f), "\nipc.signal arg:%d\n", currentId_Process());
        }
        close_File(f);
        ok = iTrue;
    }
    iRelease(f);
    return ok;
}

iString *communicate_Ipc(const iString *command) {
    const iProcessId dst = check_Ipc();
    if (dst) {
        if (write_Ipc(dst, command, command_IpcWrite)) {
            response_ = new_IpcResponse();
            signal(SIGUSR1, handleSignal_IpcResponse_);
            lock_Mutex(&response_->mtx);
            if (kill(dst, SIGUSR1) == 0) {
                iTime until;
                initTimeout_Time(&until, 1.0);
                waitTimeout_Condition(&response_->finished, &response_->mtx, &until);
            }
            unlock_Mutex(&response_->mtx);
            if (!response_->success) {
                delete_IpcResponse(response_);
                response_ = NULL;
            }
        }
    }
    signal(SIGUSR1, SIG_IGN);
    if (response_) {
        iString *result = copy_String(response_->output);
        trimEnd_String(result);
        delete_IpcResponse(response_);
        response_ = NULL;
        return result;
    }
    return NULL;
}

void signal_Ipc(iProcessId pid) {
    kill(pid, SIGUSR1);
}
