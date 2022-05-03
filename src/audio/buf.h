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

#if defined (LAGRANGE_ENABLE_AUDIO)

#include "the_Foundation/block.h"
#include "the_Foundation/mutex.h"

#include <SDL_audio.h>

iDeclareType(InputBuf)
iDeclareType(SampleBuf)

#if !defined (AUDIO_S24LSB)
#   define AUDIO_S24LSB     0x8018  /* 24-bit integer samples */
#endif
#if !defined (AUDIO_F64LSB)
#   define AUDIO_F64LSB     0x8140  /* 64-bit floating point samples */
#endif

struct Impl_InputBuf {
    iMutex     mtx;
    iCondition changed;
    iBlock     data;
    iBool      isComplete;
};

iDeclareTypeConstruction(InputBuf)

size_t  size_InputBuf   (const iInputBuf *);

/*----------------------------------------------------------------------------------------------*/

struct Impl_SampleBuf {
    SDL_AudioFormat format;
    uint8_t         numChannels;
    uint8_t         sampleSize; /* as bytes; one sample includes values for all channels */
    void *          data;
    size_t          count;
    size_t          head, tail;
    iCondition      moreNeeded;
};

iDeclareTypeConstructionArgs(SampleBuf, SDL_AudioFormat format, size_t numChannels, size_t count)

size_t  size_SampleBuf      (const iSampleBuf *);
iBool   isFull_SampleBuf    (const iSampleBuf *);
size_t  vacancy_SampleBuf   (const iSampleBuf *);

iLocalDef void *ptr_SampleBuf_(iSampleBuf *d, size_t pos) {
    return ((char *) d->data) + (d->sampleSize * pos);
}

void    write_SampleBuf     (iSampleBuf *, const void *samples, const size_t n);
void    read_SampleBuf      (iSampleBuf *, const size_t n, void *samples_out);

#endif /* LAGRANGE_ENABLE_AUDIO */
