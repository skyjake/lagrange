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

#include "fontpack.h"

#include <the_Foundation/archive.h>
#include <the_Foundation/array.h>
#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/string.h>
#include <the_Foundation/toml.h>

iDeclareType(Fonts)    
    
struct Impl_Fonts {
    iString userDir;
    iPtrArray files;
};

static iFonts fonts_;

iDefineTypeConstruction(FontFile)

void init_FontFile(iFontFile *d) {
    d->style = regular_FontStyle;
    init_Block(&d->sourceData, 0);
    iZap(d->stbInfo);
#if defined (LAGRANGE_ENABLE_HARFBUZZ)
    d->hbBlob = NULL;
    d->hbFace = NULL;
    d->hbFont = NULL;
#endif
}

static void load_FontFile_(iFontFile *d, const iBlock *data) {
    set_Block(&d->sourceData, data);
    stbtt_InitFont(&d->stbInfo, constData_Block(&d->sourceData), 0);
#if defined(LAGRANGE_ENABLE_HARFBUZZ)
    /* HarfBuzz will read the font data. */
    d->hbBlob = hb_blob_create(constData_Block(&d->sourceData), size_Block(&d->sourceData),
                               HB_MEMORY_MODE_READONLY, NULL, NULL);
    d->hbFace = hb_face_create(d->hbBlob, 0);
    d->hbFont = hb_font_create(d->hbFace);
#endif    
}

static void unload_FontFile_(iFontFile *d) {
#if defined(LAGRANGE_ENABLE_HARFBUZZ)
    /* HarfBuzz objects. */
    hb_font_destroy(d->hbFont);
    hb_face_destroy(d->hbFace);
    hb_blob_destroy(d->hbBlob);
    d->hbFont = NULL;
    d->hbFace = NULL;
    d->hbBlob = NULL;
#endif    
    clear_Block(&d->sourceData);
    iZap(d->stbInfo);
}

void deinit_FontFile(iFontFile *d) {
    unload_FontFile_(d);
    deinit_Block(&d->sourceData);
}

/*----------------------------------------------------------------------------------------------*/


iDefineTypeConstruction(FontSpec)
    
void init_FontSpec(iFontSpec *d) {
    init_String(&d->id);
    init_String(&d->name);    
}

void deinit_FontSpec(iFontSpec *d) {
    deinit_String(&d->name);
    deinit_String(&d->id);
}

/*----------------------------------------------------------------------------------------------*/

iDeclareType(FontPack)
iDeclareTypeConstruction(FontPack)
    
struct Impl_FontPack {
    iArchive * archive; /* opened ZIP archive */
    iArray     fonts;   /* array of FontSpecs */
    iString *  loadPath;
    iFontSpec *loadSpec;
};

void init_FontPack(iFontPack *d) {
    d->archive = NULL;
    init_Array(&d->fonts, sizeof(iFontSpec));
    d->loadSpec = NULL;
    d->loadPath = NULL;
}

void deinit_FontPack(iFontPack *d) {
    iForEach(Array, i, &d->fonts) {
        deinit_FontSpec(i.value);
    }
    deinit_Array(&d->fonts);
}

void handleIniTable_FontPack_(void *context, const iString *table, iBool isStart) {
    iFontPack *d = context;
    if (isStart) {
        iAssert(!d->loadSpec);
        d->loadSpec = new_FontSpec();
        set_String(&d->loadSpec->id, table);
    }
    else {
        pushBack_Array(&d->fonts, d->loadSpec);
        d->loadSpec = NULL;
    }   
}

void handleIniKeyValue_FontPack_(void *context, const iString *table, const iString *key,
                                 const iTomlValue *value) {
    iFontPack *d = context;
    if (!d->loadSpec) return;
    iUnused(table);
    if (!cmp_String(key, "name") && value->type == string_TomlType) {
        set_String(&d->loadSpec->name, value->value.string);        
    }
    else if (!cmp_String(key, "priority") && value->type == int64_TomlType) {
        d->loadSpec->priority = (int) value->value.int64;        
    }
    else if (!cmp_String(key, "scaling")) {
        d->loadSpec->scaling = (float) number_TomlValue(value);
    }
    else if (!cmp_String(key, "monospace") && value->type == boolean_TomlType) {
        iChangeFlags(d->loadSpec->flags, monospace_FontSpecFlag, value->value.boolean);
    }
    else if (!cmp_String(key, "auxiliary") && value->type == boolean_TomlType) {
        iChangeFlags(d->loadSpec->flags, auxiliary_FontSpecFlag, value->value.boolean);
    }
    else if (!cmp_String(key, "arabic") && value->type == boolean_TomlType) {
        iChangeFlags(d->loadSpec->flags, arabic_FontSpecFlag, value->value.boolean);
    }
    else if (value->type == string_TomlType) {
        const char *styles[max_FontStyle] = { "regular", "italic", "light", "semibold", "bold" };
        iForIndices(i, styles) {
            if (!cmp_String(key, styles[i]) && !d->loadSpec->styles[i]) {
                iFontFile *ff = NULL;
                /* Loading from a regular file. */
                iFile *srcFile = new_File(collect_String(concat_Path(d->loadPath,
                                                                     value->value.string)));
                if (open_File(srcFile, readOnly_FileMode)) {
                    ff = new_FontFile();
                    iBlock *data = readAll_File(srcFile);
                    load_FontFile_(ff, data);
                    delete_Block(data);
                    pushBack_PtrArray(&fonts_.files, ff); /* centralized ownership */
                    d->loadSpec->styles[i] = ff;
                }
                iRelease(srcFile);
                break;
            }
        }
    }
}

iBool loadIniFile_FontPack(iFontPack *d, const iString *iniPath) {
    iBeginCollect();
    iBool ok = iFalse;
    iFile *f = iClob(new_File(iniPath));
    if (open_File(f, text_FileMode | readOnly_FileMode)) {
        d->loadPath = collect_String(newRange_String(dirName_Path(iniPath)));
        iString *src = collect_String(readString_File(f));
        iTomlParser *ini = collect_TomlParser(new_TomlParser());
        setHandlers_TomlParser(ini, 0, 0, d);
        parse_TomlParser(ini, src);
        iAssert(d->loadSpec == NULL);
        d->loadPath = NULL;
        ok = iTrue;
    }
    iEndCollect();
    return ok;
}

/*----------------------------------------------------------------------------------------------*/

static void unloadFiles_Fonts_(iFonts *d) {
    iForEach(PtrArray, i, &d->files) {
        delete_FontFile(i.ptr);
    }
    clear_PtrArray(&d->files);
}

void init_Fonts(const char *userDir) {
    iFonts *d = &fonts_;
    initCStr_String(&d->userDir, userDir);
    init_PtrArray(&d->files);
    /* Load the required fonts. */
    
}

void deinit_Fonts(void) {
    iFonts *d = &fonts_;
    unloadFiles_Fonts_(d);
    deinit_PtrArray(&d->files);
    deinit_String(&d->userDir);
}
