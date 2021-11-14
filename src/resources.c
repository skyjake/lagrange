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

static iArchive *archive_;
    
iBlock blobAbout_Resources;
iBlock blobHelp_Resources;
iBlock blobLagrange_Resources;
iBlock blobLicense_Resources;
iBlock blobVersion_Resources;
iBlock blobArghelp_Resources;
iBlock blobCs_Resources;
iBlock blobDe_Resources;
iBlock blobEn_Resources;
iBlock blobEo_Resources;
iBlock blobEs_Resources;
iBlock blobEs_MX_Resources;
iBlock blobFi_Resources;
iBlock blobFr_Resources;
iBlock blobGl_Resources;
iBlock blobHu_Resources;
iBlock blobIa_Resources;
iBlock blobIe_Resources;
iBlock blobIsv_Resources;
iBlock blobPl_Resources;
iBlock blobRu_Resources;
iBlock blobSk_Resources;
iBlock blobSr_Resources;
iBlock blobTok_Resources;
iBlock blobUk_Resources;
iBlock blobZh_Hans_Resources;
iBlock blobZh_Hant_Resources;
iBlock imageShadow_Resources;
iBlock imageLagrange64_Resources;

static struct {
    iBlock *data;
    const char *archivePath;
} entries_[] = {
    { &blobAbout_Resources, "about/about.gmi" },
    { &blobHelp_Resources, "about/help.gmi" },
    { &blobLagrange_Resources, "about/lagrange.gmi" },
    { &blobLicense_Resources, "about/license.gmi" },
    { &blobVersion_Resources, "about/version.gmi" },
    { &blobArghelp_Resources, "arg-help.txt" },
    { &blobCs_Resources, "lang/cs.bin" },
    { &blobDe_Resources, "lang/de.bin" },
    { &blobEn_Resources, "lang/en.bin" },
    { &blobEo_Resources, "lang/eo.bin" },
    { &blobEs_Resources, "lang/es.bin" },
    { &blobEs_MX_Resources, "lang/es_MX.bin" },
    { &blobFi_Resources, "lang/fi.bin" },
    { &blobFr_Resources, "lang/fr.bin" },
    { &blobGl_Resources, "lang/gl.bin" },
    { &blobHu_Resources, "lang/hu.bin" },
    { &blobIa_Resources, "lang/ia.bin" },
    { &blobIe_Resources, "lang/ie.bin" },
    { &blobIsv_Resources, "lang/isv.bin" },
    { &blobPl_Resources, "lang/pl.bin" },
    { &blobRu_Resources, "lang/ru.bin" },
    { &blobSk_Resources, "lang/sk.bin" },
    { &blobSr_Resources, "lang/sr.bin" },
    { &blobTok_Resources, "lang/tok.bin" },
    { &blobUk_Resources, "lang/uk.bin" },
    { &blobZh_Hans_Resources, "lang/zh_Hans.bin" },
    { &blobZh_Hant_Resources, "lang/zh_Hant.bin" },
    { &imageShadow_Resources, "shadow.png" },
    { &imageLagrange64_Resources, "lagrange-64.png" },
};

iBool init_Resources(const char *path) {
    archive_ = new_Archive();
    if (openFile_Archive(archive_, collectNewCStr_String(path))) {
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
