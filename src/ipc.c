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

static void doStopListening_Ipc_(iIpc *d) {
    if (d->isListening) {
        remove(lockFilePath_(d));
        d->isListening = iFalse;
    }
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

static void doListen_Ipc_(iIpc *d) {
    iFile *f = newCStr_File(lockFilePath_(d));
    if (open_File(f, writeOnly_FileMode)) {
        printf_Stream(stream_File(f), "%u", currentId_Process());
        d->isListening = iTrue;
    }
    iRelease(f);
}

static void postCommands_Ipc_(const iBlock *cmds) {
    iRangecc line = iNullRange;
    while (nextSplit_Rangecc(range_Block(cmds), "\n", &line)) {
        postCommand_App(cstr_Rangecc(line));
    }
}

/*----------------------------------------------------------------------------------------------*/
#if !defined (iPlatformMsys)

void deinit_Ipc(void) {
    iIpc *d = &ipc_;
    signal(SIGUSR1, SIG_IGN);
    doStopListening_Ipc_(d);
    deinit_String(&d->dir);
}

static void handleUserSignal_(int sig) {
    iIpc *d = &ipc_;
    iAssert(sig == SIGUSR1);
    iUnused(sig);
    const char *path = inputFilePath_(d, 0);
    iFile *f = newCStr_File(path);
    if (open_File(f, readOnly_FileMode)) {
        postCommands_Ipc_(collect_Block(readAll_File(f)));
    }
    iRelease(f);
    remove(path);
}

void listen_Ipc(void) {
    iIpc *d = &ipc_;
    signal(SIGUSR1, handleUserSignal_);
    doListen_Ipc_(d);
}

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
    if (!pid) return iFalse;
    iBool ok = iFalse;
    iFile *f = newCStr_File(inputFilePath_(&ipc_, pid));
    if (open_File(f, text_FileMode | append_FileMode)) {
        write_File(f, utf8_String(input));
        if (type != response_IpcWrite) {
            printf_Stream(stream_File(f), "\nipc.signal arg:%d%s\n", currentId_Process(),
                          type == commandAndRaise_IpcWrite ? " raise:1" : "");
        }
        close_File(f);
        ok = iTrue;
    }
    iRelease(f);
    return ok;
}

iString *communicate_Ipc(const iString *command, iBool requestRaise) {
    const iProcessId dst = check_Ipc();
    if (dst) {
        if (write_Ipc(dst, command, requestRaise ? commandAndRaise_IpcWrite : command_IpcWrite)) {
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

#endif
/*----------------------------------------------------------------------------------------------*/
#if defined (iPlatformMsys)
/* Windows doesn't have user signals, so we'll use one of the simpler native
   Win32 IPC APIs: mailslots. */

#include <the_Foundation/thread.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static iThread *listenThread_;
static HANDLE   listenSlot_;

static iThreadResult readSlotThread_Ipc_(iThread *thd) {
    iIpc *d = &ipc_;
    DWORD msgSize;
    while (d->isListening) {
        BOOL ok = GetMailslotInfo(listenSlot_, NULL, &msgSize, NULL, NULL);
        if (msgSize == MAILSLOT_NO_MESSAGE) {
            sleep_Thread(0.333);
            continue;
        }
        if (!ok) break;
        /* Read the message.*/
        DWORD readBytes = 0;
        iBlock *msg = new_Block(msgSize);
        if (ReadFile(listenSlot_, data_Block(msg), size_Block(msg), &readBytes, NULL)) {
            postCommands_Ipc_(msg);
        }
        delete_Block(msg);
    }
    return 0;
}

static const char *slotName_(int pid) {
    return format_CStr("\\\\.\\mailslot\\fi.skyjake.Lagrange\\%u", pid);
}

void deinit_Ipc(void) {
    iIpc *d = &ipc_;
    doStopListening_Ipc_(d);
    CloseHandle(listenSlot_);
    if (listenThread_) {
        join_Thread(listenThread_);
        iRelease(listenThread_);
    }
    deinit_String(&d->dir);
}

void listen_Ipc(void) {
    iIpc *d = &ipc_;
    /* Create a mailslot for listening. */
    listenSlot_ = CreateMailslotA(slotName_(currentId_Process()), 0, 1000, NULL);
    listenThread_ = new_Thread(readSlotThread_Ipc_);
    doListen_Ipc_(d);
    start_Thread(listenThread_);
}

iBool write_Ipc(iProcessId pid, const iString *input, enum iIpcWrite type) {
    if (!pid) return iFalse;
    iUnused(type);
    HANDLE slot = CreateFile(slotName_(pid),
                             GENERIC_WRITE,
                             FILE_SHARE_READ,
                             NULL,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL,
                             NULL);
    if (slot == INVALID_HANDLE_VALUE) {
        return iFalse;
    }
    DWORD writeBytes = 0;
    iBool ok =
        WriteFile(
            slot, constData_Block(utf8_String(input)), size_String(input), &writeBytes, NULL) &&
        writeBytes == size_String(input);
    CloseHandle(slot);
    return ok;
}

iString *communicate_Ipc(const iString *command, iBool requestRaise) {
    iProcessId pid = check_Ipc();
    if (!pid) {
        return NULL;
    }
    /* Open a mailslot for the response. */
    HANDLE responseSlot = CreateMailslotA(slotName_(currentId_Process()), 0, 1000, NULL);
    /* Write the commands. */
    if (!write_Ipc(pid, command, requestRaise ? commandAndRaise_IpcWrite : command_IpcWrite)) {
        CloseHandle(responseSlot);
        return NULL;
    }
    /* Read the response. */
    iString *output = NULL;
    DWORD msgSize = 0;
    iTime startTime;
    for (initCurrent_Time(&startTime); elapsedSeconds_Time(&startTime) < 2; ) {
        if (!GetMailslotInfo(responseSlot, NULL, &msgSize, NULL, NULL)) {
            break;
        }
        if (msgSize == MAILSLOT_NO_MESSAGE) {
            sleep_Thread(0.1);
            continue;
        }
        iBlock *resp = new_Block(msgSize);
        DWORD bytesRead = 0;
        ReadFile(responseSlot, data_Block(resp), msgSize, &bytesRead, NULL);
        output = newBlock_String(resp);
        delete_Block(resp);
        break;
    }
    CloseHandle(responseSlot);
    return output;
}

void signal_Ipc(iProcessId pid) {
    /* The write to the mailslot will trigger a read. */
    iUnused(pid);
}

#endif
