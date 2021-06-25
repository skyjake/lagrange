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

#include "defs.h"

#include <the_Foundation/array.h>
#include <the_Foundation/string.h>
#include <the_Foundation/vec2.h>

/* GmTypesetter has two jobs: it normalizes incoming source text, and typesets it as a
   sequence of GmRuns. New data can be appended progressively. */

iDeclareType(GmTypesetter)
iDeclareTypeConstruction(GmTypesetter)
        
void    reset_GmTypesetter      (iGmTypesetter *, enum iSourceFormat format);
void    setWidth_GmTypesetter   (iGmTypesetter *, int width);
void    addInput_GmTypesetter   (iGmTypesetter *, const iString *source);
iBool   getRuns_GmTypesetter    (iGmTypesetter *, iArray *runs_out); /* returns false when no output generated */
void    skip_GmTypesetter       (iGmTypesetter *, int ySkip);
