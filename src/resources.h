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

#include <the_Foundation/block.h>

iDeclareType(Archive)

iBool               init_Resources      (const char *path);
void                deinit_Resources    (void);

const iArchive *    archive_Resources   (void);

extern iBlock blobAbout_Resources;
extern iBlock blobHelp_Resources;
extern iBlock blobLagrange_Resources;
extern iBlock blobLicense_Resources;
extern iBlock blobVersion_0_13_Resources;
extern iBlock blobVersion_1_5_Resources;
extern iBlock blobVersion_Resources;
extern iBlock blobArghelp_Resources;
extern iBlock blobCs_Resources;
extern iBlock blobDe_Resources;
extern iBlock blobEn_Resources;
extern iBlock blobEo_Resources;
extern iBlock blobEs_Resources;
extern iBlock blobEs_MX_Resources;
extern iBlock blobFi_Resources;
extern iBlock blobFr_Resources;
extern iBlock blobGl_Resources;
extern iBlock blobHu_Resources;
extern iBlock blobIa_Resources;
extern iBlock blobIe_Resources;
extern iBlock blobIsv_Resources;
extern iBlock blobIt_Resources;
extern iBlock blobNl_Resources;
extern iBlock blobPl_Resources;
extern iBlock blobRu_Resources;
extern iBlock blobSk_Resources;
extern iBlock blobSr_Resources;
extern iBlock blobTok_Resources;
extern iBlock blobTr_Resources;
extern iBlock blobUk_Resources;
extern iBlock blobZh_Hans_Resources;
extern iBlock blobZh_Hant_Resources;
extern iBlock imageShadow_Resources;
extern iBlock fontpackDefault_Resources;
extern iBlock imageLagrange64_Resources;
extern iBlock blobMacosSystemFontsIni_Resources;
extern iBlock blobCacertPem_Resources;
