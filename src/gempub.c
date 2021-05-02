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

#include "gempub.h"
#include "gmutil.h"
#include "lang.h"
#include "defs.h"
#include "ui/util.h"

#include <the_Foundation/archive.h>
#include <the_Foundation/file.h>
#include <the_Foundation/path.h>

const char *mimeType_Gempub = "application/gpub+zip";

struct Impl_Gempub {
    iArchive *arch;
    iString baseUrl;
    iString props[max_GempubProperty];
};

iDefineTypeConstruction(Gempub)
    
void init_Gempub(iGempub *d) {
    d->arch = NULL;
    init_String(&d->baseUrl);
    iForIndices(i, d->props) {
        init_String(&d->props[i]);
    }    
}

void deinit_Gempub(iGempub *d) {
    iForIndices(i, d->props) {
        deinit_String(&d->props[i]);
    }
    deinit_String(&d->baseUrl);
    iRelease(d->arch);
}
    
static iBool parseMetadata_Gempub_(iGempub *d) {
    iAssert(isOpen_Archive(d->arch));
    /* Parse the metadata and check if the required contents are present. */
    const iBlock *metadata = dataCStr_Archive(d->arch, "metadata.txt");
    if (!metadata) {
        return iFalse;
    }
    static const char *labels[max_GempubProperty] = {
        "title:",
        "index:",
        "author:",
        "language:",
        "description:",
        "published:",
        "publishDate:",
        "revisionDate:",
        "copyright:",
        "license:",
        "version:",
        "cover:",
    };
    /* Default values. */
    setCStr_String(&d->props[title_GempubProperty], "Untitled Book");
    setCStr_String(&d->props[cover_GempubProperty],
                   entryCStr_Archive(d->arch, "cover.jpg") ? "cover.jpg" :
                   entryCStr_Archive(d->arch, "cover.png") ? "cover.png" : "");
    setCStr_String(&d->props[index_GempubProperty], "index.gmi");
    iRangecc line = iNullRange;
    while (nextSplit_Rangecc(range_Block(metadata), "\n", &line)) {
        iRangecc clean = line;
        trim_Rangecc(&clean);
        iForIndices(i, d->props) {
            if (startsWithCase_Rangecc(clean, labels[i])) {
                setRange_String(&d->props[i],
                                (iRangecc){ clean.start + strlen(labels[i]), clean.end });
                trim_String(&d->props[i]);
            }
        }
    }
    return iTrue;
}    

iBool open_Gempub(iGempub *d, const iBlock *data) {
    close_Gempub(d);
    d->arch = new_Archive();
    if (openData_Archive(d->arch, data) && parseMetadata_Gempub_(d)) {
        return iTrue;
    }
    close_Gempub(d);
    return iFalse;
}

iBool openFile_Gempub(iGempub *d, const iString *path) {
    close_Gempub(d);
    iFile *f = new_File(path);
    if (open_File(f, readOnly_FileMode)) {
        iBlock *data = readAll_File(f);
        open_Gempub(d, data);
        delete_Block(data);
        setBaseUrl_Gempub(d, collect_String(makeFileUrl_String(path)));
    }
    iRelease(f);
    return isOpen_Gempub(d);
}

iBool openUrl_Gempub(iGempub *d, const iString *url) {
    if (!openFile_Gempub(d, collect_String(localFilePathFromUrl_String(url)))) {
        setBaseUrl_Gempub(d, url);
        close_Gempub(d);
        return iFalse;
    }
    return iTrue;
}

void close_Gempub(iGempub *d) {
    if (d->arch) {
        iReleasePtr(&d->arch);
    }
    iForIndices(i, d->props) {
        clear_String(&d->props[i]);
    }
}

iBool isOpen_Gempub(const iGempub *d) {
    return d->arch != NULL;
}

void setBaseUrl_Gempub(iGempub *d, const iString *url) {
    set_String(&d->baseUrl, url);
}

static iBool hasProperty_Gempub_(const iGempub *d, enum iGempubProperty prop) {
    return !isEmpty_String(&d->props[prop]);
}

static void appendProperty_Gempub_(const iGempub *d, const char *label,
                                   enum iGempubProperty prop, iString *out) {
    if (hasProperty_Gempub_(d, prop)) {
        appendFormat_String(out, "%s %s\n", label, cstr_String(&d->props[prop]));
    }
}

static iBool isRemote_Gempub_(const iGempub *d) {
    return !equalCase_Rangecc(urlScheme_String(&d->baseUrl), "file");
}

iBlock *coverPageSource_Gempub(const iGempub *d) {
    iAssert(!isEmpty_String(&d->baseUrl));
    const iString *baseUrl = withSpacesEncoded_String(&d->baseUrl);
    iString *out = new_String();
    format_String(out, "# %s\n",
                  cstr_String(&d->props[title_GempubProperty]));
    if (!isEmpty_String(&d->props[description_GempubProperty])) {
        appendFormat_String(out, "%s\n", cstr_String(&d->props[description_GempubProperty]));
    }
    appendCStr_String(out, "\n");
    appendProperty_Gempub_(d, "Author:", author_GempubProperty, out);
    if (!isRemote_Gempub_(d)) {
        appendFormat_String(out, "\n=> %s " book_Icon " View Gempub contents\n",
                            cstrCollect_String(concat_Path(baseUrl, &d->props[index_GempubProperty])));
        if (hasProperty_Gempub_(d, cover_GempubProperty)) {
            appendFormat_String(out, "\n=> %s  Cover image\n",
                                cstrCollect_String(concat_Path(baseUrl, &d->props[cover_GempubProperty])));
        }
    }
    else {
        iString *key = collectNew_String(); /* TODO: add a helper for this */
        toString_Sym(SDLK_s, KMOD_PRIMARY, key);
        appendCStr_String(out, "\nThis Gempub book can be viewed after it has been saved locally. ");
        appendFormat_String(out,
                            cstr_Lang("error.unsupported.suggestsave"),
                            cstr_String(key),
                            saveToDownloads_Label);
        translate_Lang(out);
        appendCStr_String(out, "\n");
    }
    appendCStr_String(out, "\n## About this book\n");
    appendProperty_Gempub_(d, "Version:", version_GempubProperty, out);
    appendProperty_Gempub_(d, "Revision date:", revisionDate_GempubProperty, out);
    if (hasProperty_Gempub_(d, publishDate_GempubProperty)) {
        appendProperty_Gempub_(d, "Publish date:", publishDate_GempubProperty, out);
    }
    else {
        appendProperty_Gempub_(d, "Published:", published_GempubProperty, out);
    }
    appendProperty_Gempub_(d, "Language:", language_GempubProperty, out);
    appendProperty_Gempub_(d, "License:", license_GempubProperty, out);
    appendProperty_Gempub_(d, "\u00a9", copyright_GempubProperty, out);
    iBlock *output = copy_Block(utf8_String(out));
    delete_String(out);
    return output;
}
