/* Copyright 2022 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "export.h"

#include "app.h"
#include "bookmarks.h"
#include "gmcerts.h"
#include "sitespec.h"
#include "visited.h"

#include <the_Foundation/buffer.h>
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/path.h>
#include <the_Foundation/time.h>

const char *mimeType_Export = "application/lagrange-export+zip";

struct Impl_Export {
    iArchive *arch;
};

iDefineTypeConstruction(Export)

static const char *metadataEntryName_Export_ = "lagrange-export.ini";

void init_Export(iExport *d) {
    d->arch = new_Archive();
}

void deinit_Export(iExport *d) {
    iRelease(d->arch);
}

void generate_Export(iExport *d) {
    generatePartial_Export(d, everything_ExportFlag);
}

void generatePartial_Export(iExport *d, int dataFlags) {
    openWritable_Archive(d->arch);
    iBuffer *buf  = new_Buffer();
    iString *meta = new_String();
    iDate    today;
    iTime    now;
    initCurrent_Date(&today);
    initCurrent_Time(&now);
    format_String(meta,
                  "# Lagrange user data exported on %s\n"
                  "version = \"" LAGRANGE_APP_VERSION "\"\n"
                  "timestamp = %llu\n",
                  cstrCollect_String(format_Date(&today, "%Y-%m-%d %H:%M")),
                  (unsigned long long) integralSeconds_Time(&now));
    /* Bookmarks. */
    if (dataFlags & bookmarks_ExportFlag) {
        openEmpty_Buffer(buf);
        serialize_Bookmarks(bookmarks_App(), stream_Buffer(buf));
        setDataCStr_Archive(d->arch, "bookmarks.ini", data_Buffer(buf));
        close_Buffer(buf);
    }
    /* Identities. */
    if (dataFlags & identitiesAndTrust_ExportFlag) {
        iBuffer *buf2 = new_Buffer();
        openEmpty_Buffer(buf2);
        openEmpty_Buffer(buf);
        serialize_GmCerts(certs_App(), stream_Buffer(buf), stream_Buffer(buf2));
        setDataCStr_Archive(d->arch, "trusted.txt", data_Buffer(buf));
        setDataCStr_Archive(d->arch, "idents.lgr", data_Buffer(buf2));
        iRelease(buf2);
        iForEach(DirFileInfo,
                 info,
                 iClob(new_DirFileInfo(collect_String(concatCStr_Path(dataDir_App(), "idents"))))) {
            const iString *idPath = path_FileInfo(info.value);
            const iRangecc baseName = baseName_Path(idPath);
            if (!startsWith_Rangecc(baseName, ".") &&
                (endsWith_Rangecc(baseName, ".crt") || endsWith_Rangecc(baseName, ".key"))) {
                iFile *f = new_File(idPath);
                if (open_File(f, readOnly_FileMode)) {
                    setData_Archive(d->arch,
                                    collectNewFormat_String("idents/%s", cstr_Rangecc(baseName)),
                                    collect_Block(readAll_File(f)));
                }
                iRelease(f);
            }
        }
        close_Buffer(buf);
    }
    /* Site-specific settings. */
    if (dataFlags & siteSpec_ExportFlag) {
        openEmpty_Buffer(buf);
        serialize_SiteSpec(stream_Buffer(buf));
        setDataCStr_Archive(d->arch, "sitespec.ini", data_Buffer(buf));
        close_Buffer(buf);
    }
    /* History of visited URLs. */
    if (dataFlags & visited_ExportFlag) {
        openEmpty_Buffer(buf);
        serialize_Visited(visited_App(), stream_Buffer(buf));
        setDataCStr_Archive(d->arch, "visited.txt", data_Buffer(buf));
        close_Buffer(buf);
    }
    /* Export metadata. */
    setDataCStr_Archive(d->arch, metadataEntryName_Export_, utf8_String(meta));
    delete_String(meta);
    iRelease(buf);
}

iBool load_Export(iExport *d, const iArchive *archive) {
    if (!detect_Export(archive)) {
        return iFalse;
    }
    iRelease(d->arch);
    d->arch = ref_Object(archive);
    /* TODO: Check that at least one of the expected files is there. */
    return iTrue;
}

iBuffer *openEntryBuffer_Export_(const iExport *d, const char *entryPath) {
    iBuffer *buf = new_Buffer();
    if (open_Buffer(buf, dataCStr_Archive(d->arch, entryPath))) {
        return buf;
    }
    iRelease(buf);
    return NULL;
}

void import_Export(const iExport *d, enum iImportMethod bookmarks, enum iImportMethod identities,
                   enum iImportMethod trusted, enum iImportMethod visited,
                   enum iImportMethod siteSpec) {
    if (bookmarks) {
        iBuffer *buf = openEntryBuffer_Export_(d, "bookmarks.ini");
        if (buf) {
            deserialize_Bookmarks(bookmarks_App(), stream_Buffer(buf), bookmarks);
            iRelease(buf);
            postCommand_App("bookmarks.changed");
        }
    }
    if (trusted) {
        iBuffer *buf = openEntryBuffer_Export_(d, "trusted.txt");
        if (buf) {
            deserializeTrusted_GmCerts(certs_App(), stream_Buffer(buf), trusted);
            iRelease(buf);
        }
    }
    if (identities) {
        /* First extract any missing .crt/.key files to the idents directory. */
        const iString *identsDir = collect_String(concatCStr_Path(dataDir_App(), "idents"));
        iConstForEach(StringSet, i,
                      iClob(listDirectory_Archive(d->arch, collectNewCStr_String("idents/")))) {
            iString *dataPath = concatCStr_Path(identsDir,
                                                cstr_Rangecc(baseNameSep_Path(i.value, "/")));
            if (identities == all_ImportMethod || !fileExists_FileInfo(dataPath)) {
                iFile *f = new_File(dataPath);
                if (open_File(f, writeOnly_FileMode)) {
                    write_File(f, data_Archive(d->arch, i.value));
                }
                else {
                    fprintf(stderr, "failed to write: %s\n", cstr_String(dataPath));
                }
                iRelease(f);
            }
            delete_String(dataPath);
        }
        iBuffer *buf = openEntryBuffer_Export_(d, "idents.lgr");
        if (buf) {
            deserializeIdentities_GmCerts(certs_App(), stream_Buffer(buf), identities);
            iRelease(buf);
            postCommand_App("idents.changed");
        }
    }
    if (visited) {
        iBuffer *buf = openEntryBuffer_Export_(d, "visited.txt");
        if (buf) {
            deserialize_Visited(visited_App(), stream_Buffer(buf), iTrue /* keep latest */);
            iRelease(buf);
            postCommand_App("visited.changed");
        }
    }
    if (siteSpec) {
        iBuffer *buf = openEntryBuffer_Export_(d, "sitespec.ini");
        if (buf) {
            deserialize_SiteSpec(stream_Buffer(buf), siteSpec);
            iRelease(buf);
        }
    }
}

iBool detect_Export(const iArchive *d) {
    if (entryCStr_Archive(d, metadataEntryName_Export_)) {
        return iTrue;
    }
    /* TODO: Additional checks? */
    return iFalse;
}

const iArchive *archive_Export(const iExport *d) {
    return d->arch;
}
