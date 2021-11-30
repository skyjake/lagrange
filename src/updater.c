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

#include "updater.h"

#if defined (LAGRANGE_ENABLE_WINSPARKLE)
#include <time.h>
#include <winsparkle.h>

static const char *signaturePublicKeyPem_ = 
    "-----BEGIN PUBLIC KEY-----\n"
    "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\n"
    "4YdR7KTyioKNHjvC5EG2bvHtYfw6ng6zSOY=\n"
    "-----END PUBLIC KEY-----";

void init_Updater(void) {
    win_sparkle_set_appcast_url("https://etc.skyjake.fi/lagrange/appcast-windows.xml");
    win_sparkle_set_dsa_pub_pem(signaturePublicKeyPem_);
    win_sparkle_set_app_details(L"Jaakko Keränen", 
                                L"Lagrange", 
                                L"" LAGRANGE_APP_VERSION);
    win_sparkle_init();
}

void deinit_Updater(void) {
    win_sparkle_cleanup();
}

void checkNow_Updater(void) {
    win_sparkle_check_update_with_ui();
}

#else
/* dummy as a fallback */
void init_Updater(void) {}
void deinit_Updater(void) {}
void checkNow_Updater(void) {}
#endif
