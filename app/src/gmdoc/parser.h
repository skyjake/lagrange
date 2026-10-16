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

/* Extracts the width-independent parts of a document: links, headings, and preformatted
   blocks. These are parsed once per source and reused by every layout. */

#include "impl.h"

void    init_GmParser                   (iGmParser *);
void    deinit_GmParser                 (iGmParser *);

void    invalidateParse_GmDocument_     (iGmDocument *); /* parsed again before the next layout */
void    ensureParsed_GmDocument_        (iGmDocument *); /* all of the source */
void    parseUntil_GmDocument_          (iGmDocument *, const char *pos); /* lines starting before */
void    parsePre_GmDocument_            (iGmDocument *, uint16_t preId); /* through the end of block */

iBool   isPreOpen_GmDocument_           (const iGmDocument *, uint16_t preId); /* end not received */
size_t  preBefore_GmDocument_           (const iGmDocument *, const char *pos);
size_t  linksBefore_GmDocument_         (const iGmDocument *, const char *pos);
const char *findLeadIn_GmDocument_      (const iGmDocument *, const char *lineStart);
