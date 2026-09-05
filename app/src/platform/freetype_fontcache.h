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

/* System font enumeration and caching for the FreeType backend.
   Provides enumerateSystemFonts_FontPack_() which discovers all fonts
   available from the OS (via fontconfig on Linux, DirectWrite on Windows)
   and populates font packs for use by the Lagrange font system. */

#pragma once

#include "../fontpack.h"

/**
 * Enumerate all system fonts and create iFontFile/iFontSpec entries in the
 * global font pack list. Uses a binary disk cache ("lgFC" format) to avoid
 * re-scanning the OS font database on every launch.
 *
 * The background validation thread updates the cache when font directories
 * change and sets needRefresh on the iText instance.
 */
void enumerateSystemFonts_FontPack_(iFontPack *);

/**
 * Load system fonts from the binary cache if it is up-to-date, falling back to a
 * full enumeration if not. Always starts a background validation thread.
 *
 * @return iTrue if fonts were loaded from the existing cache (fast path).
 */
iBool loadCachedSystemFonts_FontPack_(iFontPack *);

/** Stop the background font cache validation thread (call on shutdown). */
void stopFontWorker_FtFontCache(void);
