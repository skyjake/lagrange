#pragma once

#include "util.h"

/* Platform-specific functionality for macOS */

void    setupApplication_MacOS  (void);
void    insertMenuItems_MacOS   (const char *menuLabel, const iMenuItem *items, size_t count);
void    handleCommand_MacOS     (const char *cmd);
