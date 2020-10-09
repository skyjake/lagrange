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
#include "buf.h"

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include <the_Foundation/buffer.h>
#include <the_Foundation/thread.h>
#include <SDL_audio.h>

/*----------------------------------------------------------------------------------------------*/

iDeclareType(ContentSpec)

enum iDecoderType {
    none_DecoderType,
    wav_DecoderType,
    vorbis_DecoderType,
    mpeg_DecoderType,
    midi_DecoderType,
};

struct Impl_ContentSpec {
    enum iDecoderType type;
    SDL_AudioFormat   inputFormat;
    SDL_AudioSpec     output;
    size_t            totalInputSize;
    uint64_t          totalSamples;
    iRanges           dataRange;
};

iDeclareType(Decoder)

struct Impl_Decoder {
    enum iDecoderType type;
    float             gain;
    iThread *         thread;
    SDL_AudioFormat   inputFormat;
    iInputBuf *       input;
    size_t            inputPos;
    size_t            totalInputSize;
    iSampleBuf        output;
    iMutex            outputMutex;
    uint64_t          currentSample;
    uint64_t          totalSamples; /* zero if unknown */
    iRanges           wavData;
};

enum iDecoderStatus {
    ok_DecoderStatus,
    needMoreInput_DecoderStatus,
};

static enum iDecoderStatus decodeWav_Decoder_(iDecoder *d, iRanges inputRange) {
    const uint8_t numChannels     = d->output.numChannels;
    const size_t  inputSampleSize = numChannels * SDL_AUDIO_BITSIZE(d->inputFormat) / 8;
    const size_t  vacancy         = vacancy_SampleBuf(&d->output);
    const size_t  inputBytePos    = inputSampleSize * d->inputPos;
    const size_t  avail =
        iMin(inputRange.end - inputBytePos, d->wavData.end - inputBytePos) / inputSampleSize;
    if (avail == 0) {
        return needMoreInput_DecoderStatus;
    }
    const size_t n = iMin(vacancy, avail);
    if (n == 0) {
        return ok_DecoderStatus;
    }
    void *samples = malloc(inputSampleSize * n);
    /* Get a copy of the input for mixing. */ {
        lock_Mutex(&d->input->mtx);
        iAssert(inputSampleSize * d->inputPos < size_Block(&d->input->data));
        memcpy(samples,
               constData_Block(&d->input->data) + inputSampleSize * d->inputPos,
               inputSampleSize * n);
        d->inputPos += n;
        unlock_Mutex(&d->input->mtx);
    }
    /* Gain. */ {
        const float gain = d->gain;
        if (d->inputFormat == AUDIO_F64LSB) {
            iAssert(d->output.format == AUDIO_F32);
            double *inValue  = samples;
            float * outValue = samples;
            for (size_t count = numChannels * n; count; count--) {
                *outValue++ = gain * *inValue++;
            }
        }
        else if (d->inputFormat == AUDIO_F32) {
            float *value = samples;
            for (size_t count = numChannels * n; count; count--, value++) {
                *value *= gain;
            }
        }
        else if (d->inputFormat == AUDIO_S24LSB) {
            iAssert(d->output.format == AUDIO_S16);
            const char *inValue  = samples;
            int16_t *   outValue = samples;
            for (size_t count = numChannels * n; count; count--, inValue += 3, outValue++) {
                memcpy(outValue, inValue, 2);
                *outValue *= gain;
            }
        }
        else {
            switch (SDL_AUDIO_BITSIZE(d->output.format)) {
                case 8: {
                    uint8_t *value = samples;
                    for (size_t count = numChannels * n; count; count--, value++) {
                        *value = (int) (*value - 127) * gain + 127;
                    }
                    break;
                }
                case 16: {
                    int16_t *value = samples;
                    for (size_t count = numChannels * n; count; count--, value++) {
                        *value *= gain;
                    }
                    break;
                }
                case 32: {
                    int32_t *value = samples;
                    for (size_t count = numChannels * n; count; count--, value++) {
                        *value *= gain;
                    }
                    break;
                }
            }
        }
    }
    iGuardMutex(&d->outputMutex, write_SampleBuf(&d->output, samples, n));
    d->currentSample += n;
    free(samples);
    return ok_DecoderStatus;
}

static iThreadResult run_Decoder_(iThread *thread) {
    iDecoder *d = userData_Thread(thread);
    /* Amount of data initially available. */
    lock_Mutex(&d->input->mtx);
    size_t inputSize = size_InputBuf(d->input);
    unlock_Mutex(&d->input->mtx);
    while (d->type) {
        iRanges inputRange = { d->inputPos, inputSize };
        iAssert(inputRange.start <= inputRange.end);
        if (!d->type) break;
        /* Have data to work on and a place to save output? */
        enum iDecoderStatus status = ok_DecoderStatus;
        if (!isEmpty_Range(&inputRange)) {
            switch (d->type) {
                case wav_DecoderType:
                    status = decodeWav_Decoder_(d, inputRange);
                    break;
                default:
                    break;
            }
        }
        if (status == needMoreInput_DecoderStatus) {
            lock_Mutex(&d->input->mtx);
            if (size_InputBuf(d->input) == inputSize) {
                wait_Condition(&d->input->changed, &d->input->mtx);
            }
            inputSize = size_InputBuf(d->input);
            unlock_Mutex(&d->input->mtx);
        }
        else {
            iGuardMutex(
                &d->outputMutex, if (isFull_SampleBuf(&d->output)) {
                    wait_Condition(&d->output.moreNeeded, &d->outputMutex);
                });
        }
    }
    return 0;
}

void init_Decoder(iDecoder *d, iInputBuf *input, const iContentSpec *spec) {
    d->type        = spec->type;
    d->gain        = 0.5f;
    d->input       = input;
    d->inputPos    = spec->dataRange.start;
    d->inputFormat = spec->inputFormat;
    d->totalInputSize = spec->totalInputSize;
    init_SampleBuf(&d->output,
                   spec->output.format,
                   spec->output.channels,
                   spec->output.samples * 2);
    d->currentSample = 0;
    d->totalSamples  = spec->totalSamples;
    init_Mutex(&d->outputMutex);
    d->thread = new_Thread(run_Decoder_);
    setUserData_Thread(d->thread, d);
    start_Thread(d->thread);
}

void deinit_Decoder(iDecoder *d) {
    d->type = none_DecoderType;
    signal_Condition(&d->output.moreNeeded);
    signal_Condition(&d->input->changed);
    join_Thread(d->thread);
    iRelease(d->thread);
    deinit_Mutex(&d->outputMutex);
    deinit_SampleBuf(&d->output);
}

static void start_Decoder_(iDecoder *d) {
    if (!d->thread && d->type != none_DecoderType) {
    }
}

iDefineTypeConstructionArgs(Decoder, (iInputBuf *input, const iContentSpec *spec),
                            input, spec)

/*----------------------------------------------------------------------------------------------*/

struct Impl_Player {
    SDL_AudioSpec     spec;
    SDL_AudioDeviceID device;
    iString           mime;
    iInputBuf *       data;
    iDecoder *        decoder;
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
    content.type = wav_DecoderType; /* TODO: from MIME */
    if (content.type == wav_DecoderType && dataSize >= 44) {
        /* Read the RIFF/WAVE header. */
        iStream *is = stream_Buffer(buf);
        char magic[4];
        readData_Buffer(buf, 4, magic);
        if (memcmp(magic, "RIFF", 4)) {
            /* Not WAV. */
            return content;
        }
        content.totalInputSize = readU32_Stream(is); /* file size */
        readData_Buffer(buf, 4, magic);
        if (memcmp(magic, "WAVE", 4)) {
            /* Not WAV. */
            return content;
        }
        /* Read all the chunks. */
        int16_t blockAlign = 0;
        while (!atEnd_Buffer(buf)) {
            readData_Buffer(buf, 4, magic);
            const size_t size = read32_Stream(is);
            if (memcmp(magic, "fmt ", 4) == 0) {
                if (size != 16 && size != 18) {
                    return content;
                }
                enum iWavFormat {
                    pcm_WavFormat = 1,
                    ieeeFloat_WavFormat = 3,
                };
                const int16_t  mode           = read16_Stream(is); /* 1 = PCM, 3 = IEEE_FLOAT */
                const int16_t  numChannels    = read16_Stream(is);
                const int32_t  freq           = read32_Stream(is);
                const uint32_t bytesPerSecond = readU32_Stream(is);
                blockAlign                    = read16_Stream(is);
                const int16_t  bitsPerSample  = read16_Stream(is);
                const uint16_t extSize        = (size == 18 ? readU16_Stream(is) : 0);
                iUnused(bytesPerSecond);
                if (mode != pcm_WavFormat && mode != ieeeFloat_WavFormat) { /* PCM or float */
                    return content;
                }
                if (extSize != 0) {
                    return content;
                }
                if (numChannels != 1 && numChannels != 2) {
                    return content;
                }
                if (bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24 &&
                    bitsPerSample != 32 && bitsPerSample != 64) {
                    return content;
                }
                if (bitsPerSample == 24 && blockAlign != 3 * numChannels) {
                    return content;
                }
                content.output.freq     = freq;
                content.output.channels = numChannels;
                if (mode == ieeeFloat_WavFormat) {
                    content.inputFormat   = (bitsPerSample == 32 ? AUDIO_F32 : AUDIO_F64LSB);
                    content.output.format = AUDIO_F32;
                }
                else if (bitsPerSample == 24) {
                    content.inputFormat   = AUDIO_S24LSB;
                    content.output.format = AUDIO_S16;
                }
                else {
                    content.inputFormat = content.output.format =
                        (bitsPerSample == 8 ? AUDIO_U8
                                            : bitsPerSample == 16 ? AUDIO_S16 : AUDIO_S32);
                }
            }
            else if (memcmp(magic, "data", 4) == 0) {
                content.dataRange    = (iRanges){ pos_Stream(is), pos_Stream(is) + size };
                content.totalSamples = (uint64_t) size_Range(&content.dataRange) / blockAlign;
                break;
            }
            else {
                seek_Stream(is, pos_Stream(is) + size);
            }
        }
    }
    iAssert(content.inputFormat == content.output.format ||
            (content.inputFormat == AUDIO_S24LSB && content.output.format == AUDIO_S16) ||
            (content.inputFormat == AUDIO_F64LSB && content.output.format == AUDIO_F32));
    content.output.samples = 2048;
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
    signal_Condition(&d->decoder->output.moreNeeded);
    unlock_Mutex(&d->decoder->outputMutex);
}

void init_Player(iPlayer *d) {
    iZap(d->spec);
    init_String(&d->mime);
    d->device  = 0;
    d->decoder = NULL;
    d->data    = new_InputBuf();
}

void deinit_Player(iPlayer *d) {
    stop_Player(d);
    delete_InputBuf(d->data);
    deinit_String(&d->mime);
}

iBool isStarted_Player(const iPlayer *d) {
    return d->device != 0;
}

iBool isPaused_Player(const iPlayer *d) {
    if (!d->device) return iTrue;
    return SDL_GetAudioDeviceStatus(d->device) == SDL_AUDIO_PAUSED;
}

void updateSourceData_Player(iPlayer *d, const iString *mimeType, const iBlock *data,
                             enum iPlayerUpdate update) {
    /* TODO: Add MIME as argument */
    iInputBuf *input = d->data;
    lock_Mutex(&input->mtx);
    if (mimeType) {
        set_String(&d->mime, mimeType);
    }
    switch (update) {
        case replace_PlayerUpdate:
            set_Block(&input->data, data);
            input->isComplete = iFalse;
            break;
        case append_PlayerUpdate: {
            const size_t oldSize = size_Block(&input->data);
            const size_t newSize = size_Block(data);
            iAssert(newSize >= oldSize);
            /* The old parts cannot have changed. */
            iAssert(memcmp(constData_Block(&input->data), constData_Block(data), oldSize) == 0);
            appendData_Block(&input->data, constBegin_Block(data) + oldSize, newSize - oldSize);
            input->isComplete = iFalse;
            break;
        }
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
    iContentSpec content    = contentSpec_Player_(d);
    content.output.callback = writeOutputSamples_Player_;
    content.output.userdata = d;
    d->device = SDL_OpenAudioDevice(NULL, SDL_FALSE /* playback */, &content.output, &d->spec, 0);
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

float time_Player(const iPlayer *d) {
    if (!d->decoder) return 0;
    return (float) ((double) d->decoder->currentSample / (double) d->spec.freq);
}

float duration_Player(const iPlayer *d) {
    if (!d->decoder) return 0;
    return (float) ((double) d->decoder->totalSamples / (double) d->spec.freq);
}

float streamProgress_Player(const iPlayer *d) {
    if (d->decoder->totalInputSize) {
        lock_Mutex(&d->data->mtx);
        const double inputSize = size_InputBuf(d->data);
        unlock_Mutex(&d->data->mtx);
        return (float) iMin(1.0, (double) inputSize / (double) d->decoder->totalInputSize);
    }
    return 0;
}

iString *metadataLabel_Player(const iPlayer *d) {
    return newFormat_String("%d-bit %s %d Hz", SDL_AUDIO_BITSIZE(d->decoder->inputFormat),
                            SDL_AUDIO_ISFLOAT(d->decoder->inputFormat) ? "float" : "integer",
                            d->spec.freq);
}
