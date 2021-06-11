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

#pragma once

#include <the_Foundation/block.h>

iDeclareType(Player)
iDeclareTypeConstruction(Player)    

enum iPlayerUpdate {
    replace_PlayerUpdate,
    append_PlayerUpdate,
    complete_PlayerUpdate,
};

enum iPlayerFlag {
    adjustingVolume_PlayerFlag = iBit(1),
    volumeGrabbed_PlayerFlag   = iBit(2),
};

enum iPlayerTag {
    title_PlayerTag,
    artist_PlayerTag,
    genre_PlayerTag,
    date_PlayerTag,
    max_PlayerTag,
};

void    updateSourceData_Player (iPlayer *, const iString *mimeType, const iBlock *data,
                                 enum iPlayerUpdate update);
size_t  sourceDataSize_Player   (const iPlayer *);

iBool   	start_Player            (iPlayer *);
void    	stop_Player             (iPlayer *);
void    	setPaused_Player        (iPlayer *, iBool isPaused);
void    	setVolume_Player        (iPlayer *, float volume);
void    	setFlags_Player         (iPlayer *, int flags, iBool set);
void    	setNotIdle_Player       (iPlayer *);
	
int     	flags_Player            (const iPlayer *);
const iString *tag_Player           (const iPlayer *, enum iPlayerTag tag);
iBool   	isStarted_Player        (const iPlayer *);
iBool   	isPaused_Player         (const iPlayer *);
float   	volume_Player           (const iPlayer *);
float   	time_Player             (const iPlayer *);
float   	duration_Player         (const iPlayer *);
float   	streamProgress_Player   (const iPlayer *); /* normalized 0...1 */

uint32_t    idleTimeMs_Player       (const iPlayer *);
iString *   metadataLabel_Player    (const iPlayer *);

iPlayer *   active_Player           (void);
