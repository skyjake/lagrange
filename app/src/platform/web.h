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

#include <lagrange/prefs.h>
#include <the_Foundation/string.h>
#include <SDL3/SDL_events.h>

iDeclareType(Widget)

void    init_Web                    (void); /* after the app has launched */
void    runMainLoop_Web             (void (*iterate)(void *), void *context); /* never returns */
void    handleCommand_Web           (const char *cmd);
void    syncStorage_Web             (void); /* write saved files to persistent storage */

iBool   isStopped_Web               (void); /* another browser tab has taken over */
iBool   isApple_Web                 (void); /* macOS, iOS, or iPadOS */
iBool   isDarkSystemTheme_Web       (iBool *isHighContrast);
iBool   isFullscreen_Web            (void);
void    setFullscreen_Web           (iBool fullscreen);
void    setTextInputActive_Web      (iBool active); /* shows the on-screen keyboard */
iBool   processEvent_Web            (SDL_Event *); /* returns true if the event was consumed */
void    endWheelGesture_Web         (void); /* ignore the rest of a trackpad swipe */
int     swapAppleMods_Web           (int mods);
void    appleDefaultKey_Web         (int bindingId, int *key, int *mods); /* if Apple device */
void    pickFile_Web                (const char *cmd);
void    exportDownloadedFile_Web    (const iString *localPath, const iString *mime);

void    updateRelay_Web             (iPrefs *); /* apply relay prefs */
const iString *relayPublicKey_Web   (void); /* base64; generates the key if needed */
void    addRelayPrefs_Web           (iWidget *headings, iWidget *values); /* Preferences UI */
