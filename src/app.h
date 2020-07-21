#pragma once

/* Application core: event loop, base event processing, audio synth. */

#include <the_Foundation/string.h>

iDeclareType(Window)

enum iUserEventCode {
    command_UserEventCode = 1,
};

const iString *execPath_App     (void);

int         run_App             (int argc, char **argv);
void        processEvents_App   (void);
void        refresh_App         (void);

iAny *      findWidget_App      (const char *id);
void        addTicker_App       (void (*ticker)(iAny *), iAny *context);

void        postCommand_App     (const char *command);
void        postCommandf_App    (const char *command, ...);

iLocalDef void postCommandString_App(const iString *command) {
    postCommand_App(cstr_String(command));
}

iBool       handleCommand_App   (const char *cmd);
