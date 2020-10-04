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

#include <the_Foundation/buffer.h>
#include <the_Foundation/thread.h>
#include <SDL_audio.h>

iDeclareType(InputBuf)

struct Impl_InputBuf {
    iMutex     mtx;
    iCondition changed;
    iBlock     data;
    iBool      isComplete;
};

void init_InputBuf(iInputBuf *d) {
    init_Mutex(&d->mtx);
    init_Condition(&d->changed);
    init_Block(&d->data, 0);
    d->isComplete = iTrue;
}

void deinit_InputBuf(iInputBuf *d) {
    deinit_Block(&d->data);
    deinit_Condition(&d->changed);
    deinit_Mutex(&d->mtx);
}

size_t size_InputBuf(const iInputBuf *d) {
    return size_Block(&d->data);
}

iDefineTypeConstruction(InputBuf)

/*----------------------------------------------------------------------------------------------*/

iDeclareType(SampleBuf)

struct Impl_SampleBuf {
    void * data;
    uint8_t numBits;
    uint8_t numChannels;
    uint8_t sampleSize; /* bytes; one sample includes values for all channels */
    size_t count;
    size_t head, tail;
};

void init_SampleBuf(iSampleBuf *d, size_t numChannels, size_t sampleSize, size_t count) {
    d->numChannels = numChannels;
    d->sampleSize  = sampleSize;
    d->numBits     = sampleSize / numChannels * 8;
    d->count       = count + 1; /* considered empty if head==tail */
    d->data        = malloc(d->sampleSize * d->count);
    d->head        = 0;
    d->tail        = 0;
}

void deinit_SampleBuf(iSampleBuf *d) {
    free(d->data);
}

size_t size_SampleBuf(const iSampleBuf *d) {
    return d->head - d->tail;
}

size_t vacancy_SampleBuf(const iSampleBuf *d) {
    return d->count - size_SampleBuf(d) - 1;
}

iBool isFull_SampleBuf(const iSampleBuf *d) {
    return vacancy_SampleBuf(d) == 0;
}

iLocalDef void *ptr_SampleBuf_(iSampleBuf *d, size_t pos) {
    return ((char *) d->data) + (d->sampleSize * pos);
}

void write_SampleBuf(iSampleBuf *d, const void *samples, const size_t n) {
    iAssert(n <= vacancy_SampleBuf(d));
    const size_t headPos = d->head % d->count;
    const size_t avail   = d->count - headPos;
    if (n > avail) {
        const char *in = samples;
        memcpy(ptr_SampleBuf_(d, headPos), in, d->sampleSize * avail);
        in += d->sampleSize * avail;
        memcpy(ptr_SampleBuf_(d, 0), in, d->sampleSize * (n - avail));
    }
    else {
        memcpy(ptr_SampleBuf_(d, headPos), samples, d->sampleSize * n);
    }
    d->head += n;
}

void read_SampleBuf(iSampleBuf *d, const size_t n, void *samples_out) {
    iAssert(n <= size_SampleBuf(d));
    const size_t tailPos = d->tail % d->count;
    const size_t avail   = d->count - tailPos;
    if (n > avail) {
        char *out = samples_out;
        memcpy(out, ptr_SampleBuf_(d, tailPos), d->sampleSize * avail);
        out += d->sampleSize * avail;
        memcpy(out, ptr_SampleBuf_(d, 0), d->sampleSize * (n - avail));
    }
    else {
        memcpy(samples_out, ptr_SampleBuf_(d, tailPos), d->sampleSize * n);
    }
    d->tail += n;
}

/*----------------------------------------------------------------------------------------------*/

iDeclareType(ContentSpec)

struct Impl_ContentSpec {
    SDL_AudioSpec spec;
    iRanges       wavData;
};

iDeclareType(Decoder)

enum iDecoderType {
    none_DecoderType,
    wav_DecoderType,
    mpeg_DecoderType,
    vorbis_DecoderType,
    midi_DecoderType,
};

struct Impl_Decoder {
    enum iDecoderType type;
    float             gain;
    iThread *         thread;
    iInputBuf *       input;
    size_t            inputPos;
    iSampleBuf        output;
    iMutex            outputMutex;
    iRanges           wavData;
};

static void parseWav_Decoder_(iDecoder *d, iRanges inputRange) {
    const size_t sampleSize = d->output.sampleSize;
    const size_t vacancy = vacancy_SampleBuf(&d->output);
    const size_t avail = iMin(inputRange.end - d->inputPos, d->wavData.end - d->inputPos) /
                         sampleSize;
    const size_t n = iMin(vacancy, avail);
    if (n == 0) return;
    void *samples = malloc(sampleSize * n);
    /* Get a copy of the input for mixing. */ {
        lock_Mutex(&d->input->mtx);
        memcpy(
            samples, constData_Block(&d->input->data) + sampleSize * d->inputPos, sampleSize * n);
        d->inputPos += n;
        unlock_Mutex(&d->input->mtx);
    }
    /* Gain. */ {
        const float gain = d->gain;
        if (d->output.numBits == 16) {
            int16_t *value = samples;
            for (size_t count = d->output.numChannels * n; count; count--, value++) {
                *value *= gain;
            }
        }
    }
    iGuardMutex(&d->outputMutex, write_SampleBuf(&d->output, samples, n));
    free(samples);
}

static iThreadResult run_Decoder_(iThread *thread) {
    iDecoder *d = userData_Thread(thread);
    while (d->type) {
        size_t inputSize = 0;
        /* Grab more input. */ {
            lock_Mutex(&d->input->mtx);
            wait_Condition(&d->input->changed, &d->input->mtx);
            inputSize = size_Block(&d->input->data);
            unlock_Mutex(&d->input->mtx);
        }
        iRanges inputRange = { d->inputPos, inputSize };
        iAssert(inputRange.start <= inputRange.end);
        if (!d->type) break;
        /* Have data to work on and a place to save output? */
        if (!isEmpty_Range(&inputRange) && !isFull_SampleBuf(&d->output)) {
            switch (d->type) {
                case wav_DecoderType:
                    parseWav_Decoder_(d, inputRange);
                    break;
                default:
                    break;
            }
        }
    }
    return 0;
}

void init_Decoder(iDecoder *d, iInputBuf *input, const iContentSpec *spec) {
    d->type     = wav_DecoderType;
    d->gain     = 0.5f;
    d->input    = input;
    d->inputPos = spec->wavData.start;
    init_SampleBuf(&d->output,
                   spec->spec.channels,
                   SDL_AUDIO_BITSIZE(spec->spec.format) / 8 * spec->spec.channels,
                   spec->spec.samples * 2);
    init_Mutex(&d->outputMutex);
    d->thread = new_Thread(run_Decoder_);
    setUserData_Thread(d->thread, d);
    start_Thread(d->thread);
}

void deinit_Decoder(iDecoder *d) {
    d->type = none_DecoderType;
    signal_Condition(&d->input->changed);
    join_Thread(d->thread);
    iRelease(d->thread);
    deinit_Mutex(&d->outputMutex);
    deinit_SampleBuf(&d->output);
}

iDefineTypeConstructionArgs(Decoder, (iInputBuf *input, const iContentSpec *spec),
                            input, spec)

/*----------------------------------------------------------------------------------------------*/

struct Impl_Player {
    SDL_AudioSpec spec;
    SDL_AudioDeviceID device;
    iInputBuf *data;
    iDecoder *decoder;
};

iDefineTypeConstruction(Player)

static size_t sampleSize_Player_(const iPlayer *d) {
    return d->spec.channels * SDL_AUDIO_BITSIZE(d->spec.format) / 8;
}

static int silence_Player_(const iPlayer *d) {
    return d->spec.silence;
}

static iContentSpec contentSpec_Player_(const iPlayer *d) {
    iContentSpec content;
    iZap(content);
    const size_t dataSize = size_InputBuf(d->data);
    iBuffer *buf = iClob(new_Buffer());
    open_Buffer(buf, &d->data->data);
    enum iDecoderType decType = wav_DecoderType; /* TODO: from MIME */
    if (decType == wav_DecoderType && dataSize >= 44) {
        /* Read the RIFF/WAVE header. */
        iStream *is = stream_Buffer(buf);
        char magic[4];
        readData_Buffer(buf, 4, magic);
        if (memcmp(magic, "RIFF", 4)) {
            /* Not WAV. */
            return content;
        }
        readU32_Stream(is); /* file size */
        readData_Buffer(buf, 4, magic);
        if (memcmp(magic, "WAVE", 4)) {
            /* Not WAV. */
            return content;
        }
        /* Read all the chunks. */
        while (!atEnd_Buffer(buf)) {
            readData_Buffer(buf, 4, magic);
            const size_t size = read32_Stream(is);
            if (memcmp(magic, "fmt ", 4) == 0) {
                if (size != 16) {
                    return content;
                }
                const int16_t  mode           = read16_Stream(is); /* 1 = PCM */
                const int16_t  numChannels    = read16_Stream(is);
                const int32_t  freq           = read32_Stream(is);
                const uint32_t bytesPerSecond = readU32_Stream(is);
                const int16_t  blockAlign     = read16_Stream(is);
                const int16_t  bitsPerSample  = read16_Stream(is);
                iUnused(bytesPerSecond);
                iUnused(blockAlign); /* TODO: Should use this one when reading samples? */
                if (mode != 1) { /* PCM */
                    return content;
                }
                if (numChannels != 1 && numChannels != 2) {
                    return content;
                }
                if (bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 32) {
                    return content;
                }
                content.spec.freq     = freq;
                content.spec.channels = numChannels;
                content.spec.format =
                    (bitsPerSample == 8 ? AUDIO_S8 : bitsPerSample == 16 ? AUDIO_S16 : AUDIO_S32);
            }
            else if (memcmp(magic, "data", 4) == 0) {
                content.wavData = (iRanges){ pos_Stream(is), pos_Stream(is) + size };
                break;
            }
            else {
                seek_Stream(is, pos_Stream(is) + size);
            }
        }
    }
    content.spec.samples = 2048;
    return content;
}

static void writeOutputSamples_Player_(void *plr, Uint8 *stream, int len) {
    iPlayer *d = plr;
    iAssert(d->decoder);
    const size_t sampleSize = sampleSize_Player_(d);
    const size_t count      = len / sampleSize;
    lock_Mutex(&d->decoder->outputMutex);
    if (size_SampleBuf(&d->decoder->output) >= count) {
        read_SampleBuf(&d->decoder->output, count, stream);
    }
    else {
        memset(stream, d->spec.silence, len);
    }
    unlock_Mutex(&d->decoder->outputMutex);
    /* Wake up decoder; there is more room for output. */
    signal_Condition(&d->data->changed);
}

void init_Player(iPlayer *d) {
    iZap(d->spec);
    d->device  = 0;
    d->decoder = NULL;
    d->data    = new_InputBuf();
}

void deinit_Player(iPlayer *d) {
    stop_Player(d);
    delete_InputBuf(d->data);
}

iBool isStarted_Player(const iPlayer *d) {
    return d->device != 0;
}

void setFormatHint_Player(iPlayer *d, const char *hint) {

}

void updateSourceData_Player(iPlayer *d, const iBlock *data, enum iPlayerUpdate update) {
    iInputBuf *input = d->data;
    lock_Mutex(&input->mtx);
    switch (update) {
        case replace_PlayerUpdate:
            set_Block(&input->data, data);
            input->isComplete = iFalse;
            break;
        case append_PlayerUpdate:
            append_Block(&input->data, data);
            input->isComplete = iFalse;
            break;
        case complete_PlayerUpdate:
            input->isComplete = iTrue;
            break;
    }
    signal_Condition(&input->changed);
    unlock_Mutex(&input->mtx);
}

iBool start_Player(iPlayer *d) {
    if (isStarted_Player(d)) {
        return iFalse;
    }
    iContentSpec content  = contentSpec_Player_(d);
    content.spec.callback = writeOutputSamples_Player_;
    content.spec.userdata = d;
    d->device = SDL_OpenAudioDevice(NULL, SDL_FALSE /* playback */, &content.spec, &d->spec, 0);
    if (!d->device) {
        return iFalse;
    }
    d->decoder = new_Decoder(d->data, &content);
    SDL_PauseAudioDevice(d->device, SDL_FALSE);
    return iTrue;
}

void setPaused_Player(iPlayer *d, iBool isPaused) {
    if (isStarted_Player(d)) {
        SDL_PauseAudioDevice(d->device, isPaused ? SDL_TRUE : SDL_FALSE);
    }
}

void stop_Player(iPlayer *d) {
    if (isStarted_Player(d)) {
        /* TODO: Stop the stream/decoder. */
        SDL_PauseAudioDevice(d->device, SDL_TRUE);
        SDL_CloseAudioDevice(d->device);
        d->device = 0;
        delete_Decoder(d->decoder);
        d->decoder = NULL;
    }
}
