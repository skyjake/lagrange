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

#include "player.h"

#include <the_Foundation/thread.h>
#include <SDL_audio.h>

iDeclareType(Decoder)

enum iDecoderType {
    wav_DecoderType,
    mpeg_DecoderType,
    vorbis_DecoderType,
    midi_DecoderType,
};

struct Impl_Decoder {
    enum iDecoderType type;
    size_t inPos;
//    iBlock samples;
};

struct Impl_Player {
    SDL_AudioSpec spec;
    SDL_AudioDeviceID device;
    iMutex mtx;
    iBlock data;
    iBool isDataComplete;
};

void init_Player(iPlayer *d) {
    iZap(d->spec);
    d->device = 0;
    init_Mutex(&d->mtx);
    init_Block(&d->data, 0);
    d->isDataComplete = iFalse;
}

void deinit_Player(iPlayer *d) {
    stop_Player(d);
    deinit_Block(&d->data);
}

iBool isStarted_Player(const iPlayer *d) {
    return d->device != 0;
}

void setFormatHint_Player(iPlayer *d, const char *hint) {

}

void updateSourceData_Player(iPlayer *d, const iBlock *data, enum iPlayerUpdate update) {
    lock_Mutex(&d->mtx);
    switch (update) {
        case replace_PlayerUpdate:
            set_Block(&d->data, data);
            d->isDataComplete = iFalse;
            break;
        case append_PlayerUpdate:
            append_Block(&d->data, data);
            d->isDataComplete = iFalse;
            break;
        case complete_PlayerUpdate:
            d->isDataComplete = iTrue;
            break;
    }
    unlock_Mutex(&d->mtx);
}

static void writeOutputSamples_Player_(void *plr, Uint8 *stream, int len) {
    iPlayer *d = plr;
    memset(stream, 0, len);
    /* TODO: Copy samples from the decoder's ring buffer. */
}

iBool start_Player(iPlayer *d) {
    if (isStarted_Player(d)) {
        return iFalse;
    }
    SDL_AudioSpec conf;
    iZap(conf);
    conf.freq     = 44100; /* TODO: from content */
    conf.format   = AUDIO_S16;
    conf.channels = 2; /* TODO: from content */
    conf.samples  = 2048;
    conf.callback = writeOutputSamples_Player_;
    conf.userdata = d;
    d->device     = SDL_OpenAudioDevice(NULL, SDL_FALSE /* playback */, &conf, &d->spec, 0);
    if (!d->device) {
        return iFalse;
    }
    /* TODO: Start the stream/decoder thread. */
    /* TODO: Audio device is unpaused when there are samples ready to play. */
    return iTrue;
}

void stop_Player(iPlayer *d) {
    if (isStarted_Player(d)) {
        /* TODO: Stop the stream/decoder. */
        SDL_PauseAudioDevice(d->device, SDL_TRUE);
        SDL_CloseAudioDevice(d->device);
        d->device = 0;
    }
}
