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

#pragma once

#include "ui/util.h"

iDeclareType(Window)

enum iHapticEffect {
    tap_HapticEffect,
    gentleTap_HapticEffect,
};

void    setupApplication_iOS    (void);
void    setupWindow_iOS         (iWindow *window);
iBool   processEvent_iOS        (const SDL_Event *);
void    playHapticEffect_iOS    (enum iHapticEffect effect);
void    exportDownloadedFile_iOS(const iString *path);
void    pickFile_iOS            (const char *command); /* ` path:%s` will be appended */
void    openTextActivityView_iOS(const iString *text);
void    openFileActivityView_iOS(const iString *path);

iBool   isPhone_iOS             (void);
void    safeAreaInsets_iOS      (float *left, float *top, float *right, float *bottom);
int     displayRefreshRate_iOS  (void);
float   displayScale_iOS        (const iWindow *window);

/*----------------------------------------------------------------------------------------------*/

iDeclareType(AVFAudioPlayer)
iDeclareTypeConstruction(AVFAudioPlayer)

iBool   setInput_AVFAudioPlayer     (iAVFAudioPlayer *, const iString *mediaType, const iBlock *audioFileData);
void    play_AVFAudioPlayer         (iAVFAudioPlayer *);
void    stop_AVFAudioPlayer         (iAVFAudioPlayer *);
void    setPaused_AVFAudioPlayer    (iAVFAudioPlayer *, iBool paused);
void    setVolume_AVFAudioPlayer    (iAVFAudioPlayer *, float volume);

double  currentTime_AVFAudioPlayer  (const iAVFAudioPlayer *);
double  duration_AVFAudioPlayer     (const iAVFAudioPlayer *);
iBool   isStarted_AVFAudioPlayer    (const iAVFAudioPlayer *);
iBool   isPaused_AVFAudioPlayer     (const iAVFAudioPlayer *);

void    clearNowPlayingInfo_iOS     (void);
void    updateNowPlayingInfo_iOS    (void);
