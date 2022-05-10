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
#include "defs.h"
#include "buf.h"
#include "lang.h"

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include <the_Foundation/buffer.h>
#include <the_Foundation/thread.h>
#include <SDL_audio.h>
#include <SDL_timer.h>
#include <SDL.h>

#if defined (LAGRANGE_ENABLE_MPG123)
#   include <mpg123.h>
#endif
#if defined (iPlatformAppleMobile)
#   include "../ios.h"
#endif

/*----------------------------------------------------------------------------------------------*/

iDeclareType(AVFAudioPlayer) /* iOS */

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
    size_t            inputStartPos;
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
    unsigned int      outputFreq;
    iSampleBuf        output;
    iMutex            outputMutex;
    iArray            pendingOutput;
    uint64_t          currentSample;
    uint64_t          totalSamples; /* zero if unknown */
    iMutex            tagMutex;
    iString           tags[max_PlayerTag];
    stb_vorbis *      vorbis;
#if defined (LAGRANGE_ENABLE_MPG123)
    mpg123_handle *   mpeg;
    mpg123_id3v1 *    id3v1;
    mpg123_id3v2 *    id3v2;
#endif
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
    const size_t  avail           = (inputRange.end - inputBytePos) / inputSampleSize;
    if (avail == 0) {
        return needMoreInput_DecoderStatus;
    }
    const size_t n = iMin(vacancy, avail);
    if (n == 0) {
        return ok_DecoderStatus;
    }
    void *samples = malloc(inputSampleSize * n);
    /* Get a copy of the input for further processing. */ {
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

static void writePending_Decoder_(iDecoder *d) {
    /* Write as much as we can. */
    lock_Mutex(&d->outputMutex);
    size_t avail = vacancy_SampleBuf(&d->output);
    size_t n = iMin(avail, size_Array(&d->pendingOutput));
    write_SampleBuf(&d->output, constData_Array(&d->pendingOutput), n);
    removeN_Array(&d->pendingOutput, 0, n);
    unlock_Mutex(&d->outputMutex);
    d->currentSample += n;
}

static enum iDecoderStatus decodeVorbis_Decoder_(iDecoder *d) {
    const iBlock *input = &d->input->data;
    if (!d->vorbis) {
        lock_Mutex(&d->input->mtx);
        int error;
        int consumed;
        d->vorbis = stb_vorbis_open_pushdata(
            constData_Block(input), (int) size_Block(input), &consumed, &error, NULL);
        if (!d->vorbis) {
            return needMoreInput_DecoderStatus;
        }
        d->inputPos += consumed;
        unlock_Mutex(&d->input->mtx);
        /* Check the metadata. */ {
            const stb_vorbis_comment com = stb_vorbis_get_comment(d->vorbis);
            //        printf("vendor: {%s}\n", comment.vendor);
            lock_Mutex(&d->tagMutex);
            for (int i = 0; i < com.comment_list_length; ++i) {
                const char *comStr = com.comment_list[i];
                if (!iCmpStrN(comStr, "ARTIST=", 7)) {
                    setCStr_String(&d->tags[artist_PlayerTag], comStr + 7);
                }
                else if (!iCmpStrN(comStr, "DATE=", 5)) {
                    setCStr_String(&d->tags[date_PlayerTag], comStr + 5);
                }
                else if (!iCmpStrN(comStr, "TITLE=", 6)) {
                    setCStr_String(&d->tags[title_PlayerTag], comStr + 6);
                }
                else if (!iCmpStrN(comStr, "GENRE=", 6)) {
                    setCStr_String(&d->tags[genre_PlayerTag], comStr + 6);
                }
            }
            unlock_Mutex(&d->tagMutex);
        }
    }
    if (d->totalSamples == 0 && d->input->isComplete) {
        /* Time to check the stream size. */
        lock_Mutex(&d->input->mtx);
        d->totalInputSize = size_Block(input);
        int error = 0;
        stb_vorbis *vrb = stb_vorbis_open_memory(constData_Block(input), (int) size_Block(input),
                                                 &error, NULL);
        if (vrb) {
            d->totalSamples = stb_vorbis_stream_length_in_samples(vrb);
            stb_vorbis_close(vrb);
        }
        unlock_Mutex(&d->input->mtx);
    }
    enum iDecoderStatus status = ok_DecoderStatus;
    while (size_Array(&d->pendingOutput) < d->output.count) {
        /* Try to decode some input. */
        lock_Mutex(&d->input->mtx);
        int     count     = 0;
        float **samples   = NULL;
        int     remaining = d->inputPos < size_Block(input) ? size_Block(input) - d->inputPos : 0;
        int     consumed  = stb_vorbis_decode_frame_pushdata(
            d->vorbis, constData_Block(input) + d->inputPos, remaining, NULL, &samples, &count);
        d->inputPos += consumed;
        iAssert(d->inputPos <= size_Block(input));
        unlock_Mutex(&d->input->mtx);
        if (count == 0) {
            if (consumed == 0) {
                status = needMoreInput_DecoderStatus;
                break;
            }
            else continue;
        }
        /* Apply gain. */ {
            const float gain = d->gain;
            float sample[2];
            for (size_t i = 0; i < (size_t) count; ++i) {
                for (size_t chan = 0; chan < d->output.numChannels; chan++) {
                    sample[chan] = samples[chan][i] * gain;
                }
                pushBack_Array(&d->pendingOutput, sample);
            }
        }
    }
    writePending_Decoder_(d);
    return status;
}

#if defined (LAGRANGE_ENABLE_MPG123)
static const char *mpegStr_(const mpg123_string *str) {
    return str ? str->p : "";
}
#endif

enum iDecoderStatus decodeMpeg_Decoder_(iDecoder *d) {
    enum iDecoderStatus status = ok_DecoderStatus;
#if defined (LAGRANGE_ENABLE_MPG123)
    const iBlock *input = &d->input->data;
    if (!d->mpeg) {
        d->inputPos = 0;
        d->mpeg = mpg123_new(NULL, NULL);
        mpg123_format_none(d->mpeg);
        mpg123_format(d->mpeg, d->outputFreq, d->output.numChannels, MPG123_ENC_SIGNED_16);
        mpg123_open_feed(d->mpeg);
    }
    /* Feed more input. */ {
        lock_Mutex(&d->input->mtx);
        if (d->input->isComplete) {
            d->totalInputSize = size_Block(input);
        }
        if (d->inputPos < size_Block(input)) {
            mpg123_feed(d->mpeg, constData_Block(input) + d->inputPos, size_Block(input) - d->inputPos);
            if (d->inputPos == 0) {
                long r; int ch, enc;
                mpg123_getformat(d->mpeg, &r, &ch, &enc);
                iAssert(r == d->outputFreq);
                iAssert(ch == d->output.numChannels);
                iAssert(enc == MPG123_ENC_SIGNED_16);
            }
            d->inputPos = size_Block(input);
        }
        unlock_Mutex(&d->input->mtx);
    }
    while (size_Array(&d->pendingOutput) < d->output.count) {
        int16_t buffer[512];
        size_t bytesRead = 0;
        const int rc = mpg123_read(d->mpeg, (uint8_t *) buffer, sizeof(buffer), &bytesRead);
        const float gain = d->gain;
        for (size_t i = 0; i < bytesRead / 2; i++) {
            buffer[i] *= gain;
        }
        pushBackN_Array(&d->pendingOutput, buffer, bytesRead / 2 / d->output.numChannels);
        if (rc == MPG123_NEED_MORE) {
            status = needMoreInput_DecoderStatus;
            break;
        }
        else if (rc == MPG123_DONE || bytesRead == 0) {
            break;
        }
    }
    if (!d->id3v1 &&!d->id3v2) {
        mpg123_id3(d->mpeg, &d->id3v1, &d->id3v2);
        /* TODO: These tags can change during decoding, so checking just once isn't quite right.
           Shouldn't check every time either, though... */
        if (d->id3v2) {
            lock_Mutex(&d->tagMutex);
            setCStr_String(&d->tags[title_PlayerTag], mpegStr_(d->id3v2->title));
            setCStr_String(&d->tags[artist_PlayerTag], mpegStr_(d->id3v2->artist));
            setCStr_String(&d->tags[genre_PlayerTag], mpegStr_(d->id3v2->genre));
            setCStr_String(&d->tags[date_PlayerTag], mpegStr_(d->id3v2->year));
            unlock_Mutex(&d->tagMutex);
        }
    }
    /* Check if we know the total length already. This info should be available eventually. */
    const off_t off = mpg123_length(d->mpeg);
    if (off > 0) {
        d->totalSamples = off;
    }
    writePending_Decoder_(d);
#endif
    return status;
}

static iThreadResult run_Decoder_(iThread *thread) {
    iDecoder *d = userData_Thread(thread);
    while (d->type) {
        /* Check amount of data available. */
        lock_Mutex(&d->input->mtx);
        size_t inputSize = size_InputBuf(d->input);
        unlock_Mutex(&d->input->mtx);
        iRanges inputRange = { d->inputPos, inputSize };
        iAssert(inputRange.start <= inputRange.end);
        if (!d->type) break;
        /* Have data to work on and a place to save output? */
        enum iDecoderStatus status = ok_DecoderStatus;
        switch (d->type) {
            case wav_DecoderType:
                status = decodeWav_Decoder_(d, inputRange);
                break;
            case vorbis_DecoderType:
                status = decodeVorbis_Decoder_(d);
                break;
            case mpeg_DecoderType:
                status = decodeMpeg_Decoder_(d);
                break;
            default:
                break;
        }
        if (status == needMoreInput_DecoderStatus) {
            lock_Mutex(&d->input->mtx);
            if (size_InputBuf(d->input) == inputSize) {
                wait_Condition(&d->input->changed, &d->input->mtx);
            }
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
    d->type           = spec->type;
    d->gain           = 1.0f;
    d->input          = input;
    d->inputPos       = spec->inputStartPos;
    d->inputFormat    = spec->inputFormat;
    d->totalInputSize = spec->totalInputSize;
    d->outputFreq     = spec->output.freq;
    d->currentSample  = 0;
    d->totalSamples   = spec->totalSamples;
    init_Array(&d->pendingOutput, spec->output.channels * SDL_AUDIO_BITSIZE(spec->output.format) / 8);
    init_SampleBuf(&d->output,
                   spec->output.format,
                   spec->output.channels,
                   spec->output.samples * 2);
    init_Mutex(&d->tagMutex);
    iForIndices(i, d->tags) {
        init_String(&d->tags[i]);
    }
    d->vorbis = NULL;
#if defined (LAGRANGE_ENABLE_MPG123)
    d->mpeg  = NULL;
    d->id3v1 = NULL;
    d->id3v2 = NULL;
#endif
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
    deinit_Array(&d->pendingOutput);
    iForIndices(i, d->tags) {
        deinit_String(&d->tags[i]);
    }
    deinit_Mutex(&d->tagMutex);
    if (d->vorbis) {
        stb_vorbis_close(d->vorbis);
    }
#if defined (LAGRANGE_ENABLE_MPG123)
    if (d->mpeg) {
        mpg123_close(d->mpeg);
        mpg123_delete(d->mpeg);
    }
#endif
}

iDefineTypeConstructionArgs(Decoder, (iInputBuf *input, const iContentSpec *spec),
                            input, spec)

/*----------------------------------------------------------------------------------------------*/

struct Impl_Player {
    SDL_AudioSpec     spec;
    SDL_AudioDeviceID device;
    iString           mime;
    float             volume;
    int               flags;
    iInputBuf *       data;
    uint32_t          lastInteraction;
    iDecoder *        decoder;
    iAVFAudioPlayer * avfPlayer; /* iOS */
};

static iPlayer *activePlayer_;

iDefineTypeConstruction(Player)

static size_t sampleSize_Player_(const iPlayer *d) {
    return d->spec.channels * SDL_AUDIO_BITSIZE(d->spec.format) / 8;
}

static int silence_Player_(const iPlayer *d) {
    return d->spec.silence;
}

static iRangecc mediaType_(const iString *str) {
    iRangecc part = iNullRange;
    nextSplit_Rangecc(range_String(str), ";", &part);
    return part;
}

static iContentSpec contentSpec_Player_(const iPlayer *d) {
    iContentSpec content;
    iZap(content);
    const size_t dataSize = size_InputBuf(d->data);
    iBuffer *buf = iClob(new_Buffer());
    open_Buffer(buf, &d->data->data);
    const iRangecc mediaType = mediaType_(&d->mime);
    if (equal_Rangecc(mediaType, "audio/wave") || equal_Rangecc(mediaType, "audio/wav") ||
        equal_Rangecc(mediaType, "audio/x-wav") || equal_Rangecc(mediaType, "audio/x-pn-wav")) {
        content.type = wav_DecoderType;
    }
    else if (equal_Rangecc(mediaType, "audio/vorbis") || equal_Rangecc(mediaType, "audio/ogg") ||
             equal_Rangecc(mediaType, "audio/x-vorbis+ogg")) {
        content.type = vorbis_DecoderType;
    }
#if defined (LAGRANGE_ENABLE_MPG123)
    else if (equal_Rangecc(mediaType, "audio/mpeg") || equal_Rangecc(mediaType, "audio/mp3")) {
        content.type = mpeg_DecoderType;
    }
#endif
    else {
        /* TODO: Could try decoders to see if one works? */
        content.type = none_DecoderType;
    }
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
                content.inputStartPos = pos_Stream(is);
                content.totalSamples  = (uint64_t) size / blockAlign;
                break;
            }
            else {
                seek_Stream(is, pos_Stream(is) + size);
            }
        }
    }
    else if (content.type == vorbis_DecoderType) {
        /* Try to decode what we have and see if it looks like Vorbis. */
        int consumed = 0;
        int error = 0;
        stb_vorbis *vrb = stb_vorbis_open_pushdata(
            constData_Block(&d->data->data), size_Block(&d->data->data), &consumed, &error, NULL);
        if (!vrb) {
            if (error != VORBIS_need_more_data) {
                content.type = none_DecoderType;
            }
            return content;
        }
        const stb_vorbis_info info = stb_vorbis_get_info(vrb);
        const int numChannels = info.channels;
        if (numChannels != 1 && numChannels != 2) {
            return content;
        }
        content.output.freq     = info.sample_rate;
        content.output.channels = numChannels;
        content.output.format   = AUDIO_F32;
        content.inputFormat     = AUDIO_F32; /* actually stb_vorbis provides floats */
        stb_vorbis_close(vrb);
    }
    else if (content.type == mpeg_DecoderType) {
#if defined (LAGRANGE_ENABLE_MPG123)
        mpg123_handle *mh = mpg123_new(NULL, NULL);
        mpg123_open_feed(mh);
        mpg123_feed(mh, constData_Block(&d->data->data), size_Block(&d->data->data));
        long rate     = 0;
        int  channels = 0;
        int  encoding = 0;
        if (mpg123_getformat(mh, &rate, &channels, &encoding) == MPG123_OK) {
            content.output.freq     = rate;
            content.output.channels = channels;
            content.inputFormat     = AUDIO_S16;
            content.output.format   = AUDIO_S16;
        }
        mpg123_close(mh);
        mpg123_delete(mh);
#endif
    }
    iAssert(content.inputFormat == content.output.format ||
            (content.inputFormat == AUDIO_S24LSB && content.output.format == AUDIO_S16) ||
            (content.inputFormat == AUDIO_F64LSB && content.output.format == AUDIO_F32));
    content.output.samples = isAndroid_Platform() ? content.output.freq / 2 : 8192;
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
    d->device    = 0;
    d->decoder   = NULL;
    d->avfPlayer = NULL;
    d->data      = new_InputBuf();
    d->volume    = 1.0f;
    d->flags     = 0;
}

void deinit_Player(iPlayer *d) {
    stop_Player(d);
    delete_InputBuf(d->data);
    deinit_String(&d->mime);
#if defined (iPlatformAppleMobile)
    if (d->avfPlayer) {
        delete_AVFAudioPlayer(d->avfPlayer);
        if (activePlayer_ == d) {
            clearNowPlayingInfo_iOS();
        }
    }
#endif
    if (activePlayer_ == d) {
        activePlayer_ = NULL;
    }
}

iBool isStarted_Player(const iPlayer *d) {
#if defined (iPlatformAppleMobile)
    if (d->avfPlayer) {
        return isStarted_AVFAudioPlayer(d->avfPlayer);
    }
#endif
    return d->device != 0;
}

iBool isPaused_Player(const iPlayer *d) {
#if defined (iPlatformAppleMobile)
    if (d->avfPlayer) {
        return isPaused_AVFAudioPlayer(d->avfPlayer);
    }
#endif
    if (!d->device) return iTrue;
    return SDL_GetAudioDeviceStatus(d->device) == SDL_AUDIO_PAUSED;
}

float volume_Player(const iPlayer *d) {
    return d->volume;
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
            if (input->isComplete) {
                iAssert(newSize == oldSize);
                break;
            }
            /* The old parts cannot have changed. */
            appendData_Block(&input->data, constBegin_Block(data) + oldSize, newSize - oldSize);
            break;
        }
        case complete_PlayerUpdate:
            if (!input->isComplete) {
                input->isComplete = iTrue;
#if defined (iPlatformAppleMobile)
                iAssert(d->avfPlayer == NULL);
                d->avfPlayer = new_AVFAudioPlayer();
                if (!setInput_AVFAudioPlayer(d->avfPlayer, &d->mime, &input->data)) {
                    delete_AVFAudioPlayer(d->avfPlayer);
                    d->avfPlayer = NULL;
                }
#endif
            }
            break;
    }
    signal_Condition(&input->changed);
    unlock_Mutex(&input->mtx);
}

size_t sourceDataSize_Player(const iPlayer *d) {
    lock_Mutex(&d->data->mtx);
    const size_t size = size_Block(&d->data->data);
    unlock_Mutex(&d->data->mtx);
    return size;
}

static iBool setupSDLAudio_(iBool init) {
    static iBool isAudioInited_ = iFalse;
    if (init && !isAudioInited_) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            fprintf(stderr, "[SDL] audio init failed: %s\n", SDL_GetError());
            return iFalse;
        }
        isAudioInited_ = iTrue;
    }
    else if (!init && isAudioInited_ && !isAndroid_Platform()) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        isAudioInited_ = iFalse;
    }
    return isAudioInited_;
}

iBool start_Player(iPlayer *d) {
    if (isStarted_Player(d)) {
        return iFalse;
    }
#if defined (iPlatformAppleMobile)
    if (d->avfPlayer) {
        play_AVFAudioPlayer(d->avfPlayer);
        setNotIdle_Player(d);
        activePlayer_ = d;
        return iTrue;
    }
#endif
    iContentSpec content = contentSpec_Player_(d);
    if (!content.output.freq) {
        return iFalse;
    }
    content.output.callback = writeOutputSamples_Player_;
    content.output.userdata = d;
    if (!setupSDLAudio_(iTrue)) {
        return iFalse;
    }
    d->device = SDL_OpenAudioDevice(NULL, SDL_FALSE /* playback */, &content.output, &d->spec, 0);
    if (!d->device) {
        return iFalse;
    }
    d->decoder = new_Decoder(d->data, &content);
    d->decoder->gain = d->volume;
    SDL_PauseAudioDevice(d->device, SDL_FALSE);
    setNotIdle_Player(d);
    activePlayer_ = d;
    return iTrue;
}

void setPaused_Player(iPlayer *d, iBool isPaused) {
#if defined (iPlatformAppleMobile)
    if (d->avfPlayer) {
        setPaused_AVFAudioPlayer(d->avfPlayer, isPaused);
        return;
    }
#endif
    if (isStarted_Player(d)) {
        SDL_PauseAudioDevice(d->device, isPaused ? SDL_TRUE : SDL_FALSE);
        setNotIdle_Player(d);
    }
}

void stop_Player(iPlayer *d) {
#if defined (iPlatformAppleMobile)
    if (d->avfPlayer) {
        stop_AVFAudioPlayer(d->avfPlayer);
        return;
    }
#endif
    if (isStarted_Player(d)) {
        /* TODO: Stop the stream/decoder. */
        SDL_PauseAudioDevice(d->device, SDL_TRUE);
        SDL_CloseAudioDevice(d->device);
        d->device = 0;
        delete_Decoder(d->decoder);
        d->decoder = NULL;
        setupSDLAudio_(iFalse);
    }
}

void setVolume_Player(iPlayer *d, float volume) {
    d->volume = iClamp(volume, 0, 1);
    if (d->decoder) {
        d->decoder->gain = d->volume;
    }
#if defined (iPlatformAppleMobile)
    if (d->avfPlayer) {
        setVolume_AVFAudioPlayer(d->avfPlayer, volume);
    }
#endif
    setNotIdle_Player(d);
}

void setFlags_Player(iPlayer *d, int flags, iBool set) {
    iChangeFlags(d->flags, flags, set);
    setNotIdle_Player(d);
}

void setNotIdle_Player(iPlayer *d) {
    d->lastInteraction = SDL_GetTicks();
}

int flags_Player(const iPlayer *d) {
    return d->flags;
}

const iString *tag_Player(const iPlayer *d, enum iPlayerTag tag) {
    const iString *str = NULL;
    if (d->decoder) {
        lock_Mutex(&d->decoder->tagMutex);
        str = collect_String(copy_String(&d->decoder->tags[tag]));
        unlock_Mutex(&d->decoder->tagMutex);
    }
    return str ? str : collectNew_String();
}

float time_Player(const iPlayer *d) {
#if defined (iPlatformAppleMobile)
    if (d->avfPlayer) {
        return currentTime_AVFAudioPlayer(d->avfPlayer);
    }
#endif
    if (!d->decoder) return 0;
    return (float) ((double) d->decoder->currentSample / (double) d->spec.freq);
}

float duration_Player(const iPlayer *d) {
#if defined (iPlatformAppleMobile)
    if (d->avfPlayer) {
        return duration_AVFAudioPlayer(d->avfPlayer);
    }
#endif
    if (!d->decoder) return 0;
    return (float) ((double) d->decoder->totalSamples / (double) d->spec.freq);
}

float streamProgress_Player(const iPlayer *d) {
    if (d->decoder && d->decoder->totalInputSize) {
        lock_Mutex(&d->data->mtx);
        const double inputSize = size_InputBuf(d->data);
        unlock_Mutex(&d->data->mtx);
        return (float) iMin(1.0, (double) inputSize / (double) d->decoder->totalInputSize);
    }
    return 0;
}

uint32_t idleTimeMs_Player(const iPlayer *d) {
    return SDL_GetTicks() - d->lastInteraction;
}

iString *metadataLabel_Player(const iPlayer *d) {
    iString *meta = new_String();
    if (d->decoder) {
        lock_Mutex(&d->decoder->tagMutex);
        const iString *tags = d->decoder->tags;
        if (!isEmpty_String(&tags[title_PlayerTag])) {
            appendFormat_String(meta, "${audio.meta.title}: %s\n", cstr_String(&tags[title_PlayerTag]));
        }
        if (!isEmpty_String(&tags[artist_PlayerTag])) {
            appendFormat_String(meta, "${audio.meta.artist}: %s\n", cstr_String(&tags[artist_PlayerTag]));
        }
        if (!isEmpty_String(&tags[genre_PlayerTag])) {
            appendFormat_String(meta, "${audio.meta.genre}: %s\n", cstr_String(&tags[genre_PlayerTag]));
        }
        if (!isEmpty_String(&tags[date_PlayerTag])) {
            appendFormat_String(meta, "${audio.meta.date}: %s\n", cstr_String(&tags[date_PlayerTag]));
        }
        unlock_Mutex(&d->decoder->tagMutex);
    }
    if (d->decoder) {
        appendFormat_String(meta,
                            translateCStr_Lang("${n.bit} %s %d ${hz}"), /* translation adds %d */
                            SDL_AUDIO_BITSIZE(d->decoder->inputFormat),
                            cstr_Lang(SDL_AUDIO_ISFLOAT(d->decoder->inputFormat)
                                          ? "numbertype.float"
                                          : "numbertype.integer"),
                            d->spec.freq);
    }
    return meta;
}

iPlayer *active_Player(void) {
    return activePlayer_;
}
