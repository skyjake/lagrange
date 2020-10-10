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

#include "buf.h"

iDefineTypeConstruction(InputBuf)

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

/*----------------------------------------------------------------------------------------------*/

iDefineTypeConstructionArgs(SampleBuf, (SDL_AudioFormat format, size_t numChannels, size_t count),
                            format, numChannels, count)

void init_SampleBuf(iSampleBuf *d, SDL_AudioFormat format, size_t numChannels, size_t count) {
    d->format      = format;
    d->numChannels = numChannels;
    d->sampleSize  = SDL_AUDIO_BITSIZE(format) / 8 * numChannels;
    d->count       = count + 1; /* considered empty if head==tail */
    d->data        = malloc(d->sampleSize * d->count);
    d->head        = 0;
    d->tail        = 0;
    init_Condition(&d->moreNeeded);
}

void deinit_SampleBuf(iSampleBuf *d) {
    deinit_Condition(&d->moreNeeded);
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
