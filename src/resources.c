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

#include "resources.h"

#include <the_Foundation/archive.h>
#include <the_Foundation/version.h>

#if defined (iPlatformAndroidMobile)
#   include <SDL_rwops.h>
#endif

static iArchive *archive_;

iBlock blobAbout_Resources;
iBlock blobHelp_Resources;
iBlock blobLagrange_Resources;
iBlock blobLicense_Resources;
iBlock blobVersion_0_13_Resources;
iBlock blobVersion_1_5_Resources;
iBlock blobVersion_1_10_Resources;
iBlock blobVersion_Resources;
iBlock blobArghelp_Resources;
iBlock blobCs_Resources;
iBlock blobDe_Resources;
iBlock blobEn_Resources;
iBlock blobEo_Resources;
iBlock blobEs_Resources;
iBlock blobEs_MX_Resources;
iBlock blobEu_Resources;
iBlock blobFi_Resources;
iBlock blobFr_Resources;
iBlock blobGl_Resources;
iBlock blobHu_Resources;
iBlock blobIa_Resources;
iBlock blobIe_Resources;
iBlock blobIsv_Resources;
iBlock blobIt_Resources;
iBlock blobJa_Resources;
iBlock blobNl_Resources;
iBlock blobPl_Resources;
iBlock blobRu_Resources;
iBlock blobSk_Resources;
iBlock blobSr_Resources;
iBlock blobTok_Resources;
iBlock blobTr_Resources;
iBlock blobUk_Resources;
iBlock blobZh_Hans_Resources;
iBlock blobZh_Hant_Resources;
iBlock imageLogo_Resources;
iBlock imageShadow_Resources;
iBlock imageLagrange64_Resources;
iBlock blobMacosSystemFontsIni_Resources;
iBlock blobCacertPem_Resources;

static struct {
    iBlock *data;
    const char *archivePath;
} entries_[] = {
    { &blobAbout_Resources, "about/about.gmi" },
    { &blobLagrange_Resources, "about/lagrange.gmi" },
    { &blobLicense_Resources, "about/license.gmi" },
#if defined (iPlatformAppleMobile)
    { &blobHelp_Resources, "about/ios-help.gmi" },
    { &blobVersion_Resources, "about/ios-version.gmi" },
#elif defined (iPlatformAndroidMobile)
    { &blobHelp_Resources, "about/android-help.gmi" },
    { &blobVersion_Resources, "about/android-version.gmi" },
#else
    { &blobHelp_Resources, "about/help.gmi" },
    { &blobVersion_0_13_Resources, "about/version-0.13.gmi" },
    { &blobVersion_1_5_Resources, "about/version-1.5.gmi" },
    { &blobVersion_1_10_Resources, "about/version-1.10.gmi" },
    { &blobVersion_Resources, "about/version.gmi" },
#endif
    { &blobArghelp_Resources, "arg-help.txt" },
    { &blobCs_Resources, "lang/cs.bin" },
    { &blobDe_Resources, "lang/de.bin" },
    { &blobEn_Resources, "lang/en.bin" },
    { &blobEo_Resources, "lang/eo.bin" },
    { &blobEs_Resources, "lang/es.bin" },
    { &blobEs_MX_Resources, "lang/es_MX.bin" },
    { &blobEu_Resources, "lang/eu.bin" },
    { &blobFi_Resources, "lang/fi.bin" },
    { &blobFr_Resources, "lang/fr.bin" },
    { &blobGl_Resources, "lang/gl.bin" },
    { &blobHu_Resources, "lang/hu.bin" },
    { &blobIa_Resources, "lang/ia.bin" },
    { &blobIe_Resources, "lang/ie.bin" },
    { &blobIsv_Resources, "lang/isv.bin" },
    { &blobIt_Resources, "lang/it.bin" },
    { &blobJa_Resources, "lang/ja.bin" },
    { &blobNl_Resources, "lang/nl.bin" },
    { &blobPl_Resources, "lang/pl.bin" },
    { &blobRu_Resources, "lang/ru.bin" },
    { &blobSk_Resources, "lang/sk.bin" },
    { &blobSr_Resources, "lang/sr.bin" },
    { &blobTok_Resources, "lang/tok.bin" },
    { &blobTr_Resources, "lang/tr.bin" },
    { &blobUk_Resources, "lang/uk.bin" },
    { &blobZh_Hans_Resources, "lang/zh_Hans.bin" },
    { &blobZh_Hant_Resources, "lang/zh_Hant.bin" },
    { &imageLogo_Resources, "logo.png" },
    { &imageShadow_Resources, "shadow.png" },
    { &imageLagrange64_Resources, "lagrange-64.png" },
    { &blobMacosSystemFontsIni_Resources, "macos-system-fonts.ini" },
    { &blobCacertPem_Resources, "cacert.pem" },
};

iBool init_Resources(const char *path) {
    archive_ = new_Archive();
    iBool ok = iFalse;
#if defined (iPlatformAndroidMobile)
    /* Resources are bundled as assets so they cannot be loaded as a regular file.
       Fortunately, SDL implements a file wrapper. */
    SDL_RWops *io = SDL_RWFromFile(path, "rb");
    if (io) {
        iBlock buf;
        init_Block(&buf, (size_t) SDL_RWsize(io));
        SDL_RWread(io, data_Block(&buf), size_Block(&buf), 1);
        SDL_RWclose(io);
        ok = openData_Archive(archive_, &buf);
        deinit_Block(&buf);
    }
#else
    ok = openFile_Archive(archive_, collectNewCStr_String(path));
#endif
    if (ok) {
        iVersion appVer;
        init_Version(&appVer, range_CStr(LAGRANGE_APP_VERSION));
        iVersion resVer;
        init_Version(&resVer, range_Block(dataCStr_Archive(archive_, "VERSION")));
        if (!cmp_Version(&resVer, &appVer)) {
            iForIndices(i, entries_) {
                const iBlock *data = dataCStr_Archive(archive_, entries_[i].archivePath);
                if (data) {
                    initCopy_Block(entries_[i].data, data);
                }
            }
            return iTrue;
        }
        fprintf(stderr, "[Resources] %s: version mismatch (%s != " LAGRANGE_APP_VERSION ")\n",
                path, cstr_Block(dataCStr_Archive(archive_, "VERSION")));
    }
    iRelease(archive_);
    return iFalse;
}

void deinit_Resources(void) {
    iForIndices(i, entries_) {
        deinit_Block(entries_[i].data);
    }
    iRelease(archive_);
}

const iArchive *archive_Resources(void) {
    return archive_;
}
