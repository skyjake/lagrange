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
#include "resources.h"
#include "ui/window.h"
#include "gmrequest.h"
#include "app.h"

#if defined (iPlatformMsys)
#   include "win32.h"
#endif

#include <the_Foundation/archive.h>
#include <the_Foundation/array.h>
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/path.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/string.h>
#include <the_Foundation/stringlist.h>
#include <the_Foundation/toml.h>

const char *mimeType_FontPack = "application/lagrange-fontpack+zip";

float scale_FontSize(enum iFontSize size) {
    static const float sizes[max_FontSize] = {
        1.000, /* UI sizes */
        1.125,
        1.333,
        1.666,
        0.800,
        0.900,
        1.000, /* document sizes */
        1.200,
        1.333,
        1.666,
        2.000,
        0.684, 
        0.855, /* calibration: fits the Lagrange title screen with Normal line width */
    };
    if (size < 0 || size >= max_FontSize) {
        return 1.0f;
    }
    return sizes[size];
}

/*----------------------------------------------------------------------------------------------*/

iDefineObjectConstruction(FontFile)

void init_FontFile(iFontFile *d) {
    init_String(&d->id);
    d->colIndex  = 0;
    d->style     = regular_FontStyle;
    d->emAdvance = 0;
    d->ascent    = 0;
    d->descent   = 0;
    init_Block(&d->sourceData, 0);
#if defined (LAGRANGE_ENABLE_STB_TRUETYPE)
    iZap(d->stbInfo);
#endif
#if defined (LAGRANGE_ENABLE_HARFBUZZ)
    d->hbBlob = NULL;
    d->hbFace = NULL;
    d->hbFont = NULL;
#endif
}

static void load_FontFile_(iFontFile *d, const iBlock *data) {
#if defined (LAGRANGE_ENABLE_STB_TRUETYPE)
    set_Block(&d->sourceData, data);
#if 0
    /* Count the number of available fonts. */
    for (int i = 0; ; i++) {
        if (stbtt_GetFontOffsetForIndex(constData_Block(&d->sourceData), i) < 0) {
            printf("%s: contains %d fonts\n", cstr_String(&d->id), i);
            break;
        }
    }
#endif
    const size_t offset = stbtt_GetFontOffsetForIndex(constData_Block(&d->sourceData),
                                                      d->colIndex);
    stbtt_InitFont(&d->stbInfo, constData_Block(data), offset);
    /* Basic metrics. */
    stbtt_GetFontVMetrics(&d->stbInfo, &d->ascent, &d->descent, NULL);
    stbtt_GetCodepointHMetrics(&d->stbInfo, 'M', &d->emAdvance, NULL);
#endif
#if defined(LAGRANGE_ENABLE_HARFBUZZ)
    /* HarfBuzz will read the font data. */
    d->hbBlob = hb_blob_create(constData_Block(data), size_Block(&d->sourceData),
                               HB_MEMORY_MODE_READONLY, NULL, NULL);
    d->hbFace = hb_face_create(d->hbBlob, d->colIndex);
    d->hbFont = hb_font_create(d->hbFace);
#endif
}

static iBool detectMonospace_FontFile_(const iFontFile *d) {
#if defined (LAGRANGE_ENABLE_STB_TRUETYPE)
    int em, i, period;
    stbtt_GetCodepointHMetrics(&d->stbInfo, 'M', &em, NULL);
    stbtt_GetCodepointHMetrics(&d->stbInfo, 'i', &i, NULL);
    stbtt_GetCodepointHMetrics(&d->stbInfo, '.', &period, NULL);
    return em == i && em == period;
#else
    return iFalse;
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
#if defined (LAGRANGE_ENABLE_STB_TRUETYPE)
    iZap(d->stbInfo);
#endif
    clear_Block(&d->sourceData);
}

void deinit_FontFile(iFontFile *d) {
//    printf("FontFile %p {%s} is DESTROYED\n", d, cstr_String(&d->id));
    unload_FontFile_(d);
    deinit_Block(&d->sourceData);
    deinit_String(&d->id);
}

float scaleForPixelHeight_FontFile(const iFontFile *d, int pixelHeight) {
#if defined (LAGRANGE_ENABLE_STB_TRUETYPE)
    return stbtt_ScaleForPixelHeight(&d->stbInfo, pixelHeight);
#else
    return 1.0f;
#endif    
}

uint8_t *rasterizeGlyph_FontFile(const iFontFile *d, float xScale, float yScale, float xShift,
                                 uint32_t glyphIndex, int *w, int *h) {
#if defined (LAGRANGE_ENABLE_STB_TRUETYPE)
    return stbtt_GetGlyphBitmapSubpixel(
        &d->stbInfo, xScale, yScale, xShift, 0.0f, glyphIndex, w, h, 0, 0);
#else
    return NULL;
#endif
}

void measureGlyph_FontFile(const iFontFile *d, uint32_t glyphIndex,
                           float xScale, float yScale, float xShift,
                           int *x0, int *y0, int *x1, int *y1) {
#if defined (LAGRANGE_ENABLE_STB_TRUETYPE)
    stbtt_GetGlyphBitmapBoxSubpixel(
        &d->stbInfo, glyphIndex, xScale, yScale, xShift, 0.0f, x0, y0, x1, y1);
#endif
}

int glyphAdvance_FontFile(const iFontFile *d, uint32_t glyphIndex) {
#if defined (LAGRANGE_ENABLE_STB_TRUETYPE)
    int adv = 0;
    stbtt_GetGlyphHMetrics(&d->stbInfo, glyphIndex, &adv, NULL);
    return adv;
#else
    return 1;
#endif
}

/*----------------------------------------------------------------------------------------------*/

iDefineTypeConstruction(FontSpec)
    
void init_FontSpec(iFontSpec *d) {
    init_String(&d->id);
    init_String(&d->name);
    init_String(&d->sourcePath);
    d->flags      = 0;
    d->priority   = 0;
    for (int i = 0; i < 2; ++i) {
        d->heightScale[i]     = 1.0f;
        d->glyphScale[i]      = 1.0f;
        d->vertOffsetScale[i] = 1.0f;
    }
    iZap(d->styles);
}

void deinit_FontSpec(iFontSpec *d) {
    /* FontFile references are held by FontSpecs. */
    iForIndices(i, d->styles) {
        iRelease(d->styles[i]);
    }
    deinit_String(&d->sourcePath);
    deinit_String(&d->name);
    deinit_String(&d->id);
}

/*----------------------------------------------------------------------------------------------*/

iDeclareType(Fonts)

struct Impl_Fonts {
    iString   userDir;
    iPtrArray packs;
    iObjectList *files;
    iPtrArray specOrder; /* specs sorted by priority */
    iRegExp *indexPattern; /* collection index filename suffix */
};

static iFonts fonts_;

static void unloadFiles_Fonts_(iFonts *d) {
    /* TODO: Mark all files in font packs as not resident. */    
    clear_ObjectList(d->files);
}

static iFontFile *findFile_Fonts_(iFonts *d, const iString *id) {
    iForEach(ObjectList, i, d->files) {
        iFontFile *ff = i.object;
        if (equal_String(&ff->id, id)) {
            return ff;
        }
    }
    return NULL;
}

static void releaseUnusedFiles_Fonts_(iFonts *d) {
    iForEach(ObjectList, i, d->files) {
        iFontFile *ff = i.object;
        if (ff->object.refCount == 1) {
            /* No specs use this. */
            //printf("[Fonts] releasing unused font file: %p {%s}\n", ff, cstr_String(&ff->id));
            remove_ObjectListIterator(&i);
        }
    }
}

/*----------------------------------------------------------------------------------------------*/

struct Impl_FontPack {
    iString         id; /* lowercase filename without the .fontpack extension */
    int             version;
    iBool           isStandalone;
    iBool           isReadOnly;
    iPtrArray       fonts;   /* array of FontSpecs */
    const iArchive *archive; /* opened ZIP archive */
    iString *       loadPath;
    iFontSpec *     loadSpec;
};

iDefineTypeConstruction(FontPack)

void init_FontPack(iFontPack *d) {
    init_String(&d->id);
    d->version = 0;
    d->isStandalone = iFalse;
    d->isReadOnly = iFalse;
    init_PtrArray(&d->fonts);
    d->archive  = NULL;
    d->loadSpec = NULL;
    d->loadPath = NULL;
}

void deinit_FontPack(iFontPack *d) {
    iAssert(d->archive == NULL);
    iAssert(d->loadSpec == NULL);
    delete_String(d->loadPath);
    iForEach(PtrArray, i, &d->fonts) {
        delete_FontSpec(i.ptr);
    }
    deinit_PtrArray(&d->fonts);
    deinit_String(&d->id);
    releaseUnusedFiles_Fonts_(&fonts_);
}

iFontPackId id_FontPack(const iFontPack *d) {
    return (iFontPackId){ &d->id, d->version };
}

const iString *loadPath_FontPack(const iFontPack *d) {
    return d->loadPath;
}

const iPtrArray *listSpecs_FontPack(const iFontPack *d) {
    if (!d) return NULL;
    return &d->fonts;
}

void handleIniTable_FontPack_(void *context, const iString *table, iBool isStart) {
    iFontPack *d = context;
    if (isStart) {
        iAssert(!d->loadSpec);
        /* Each font ID must be unique in the non-standalone packs. */
        if (d->isStandalone || !findSpec_Fonts(cstr_String(table))) {
            d->loadSpec = new_FontSpec();
            set_String(&d->loadSpec->id, table);
            if (d->loadPath) {
                set_String(&d->loadSpec->sourcePath, d->loadPath);
            }
        }
    }
    else if (d->loadSpec) {
        /* Set fallback font files. */ {
            const iFontFile **styles = d->loadSpec->styles;
            if (!styles[regular_FontStyle]) {
                fprintf(stderr, "[FontPack] \"%s\" missing a regular style font file\n",
                        cstr_String(table));
                delete_FontSpec(d->loadSpec);
                d->loadSpec = NULL;
                return;
            }
            if (!styles[semiBold_FontStyle]) {
                styles[semiBold_FontStyle] = ref_Object(styles[bold_FontStyle]);
            }
            for (size_t s = 0; s < max_FontStyle; s++) {
                if (!styles[s]) {
                    styles[s] = ref_Object(styles[regular_FontStyle]);
                }
            }
        }
        pushBack_PtrArray(&d->fonts, d->loadSpec);
        d->loadSpec = NULL;
    }   
}

static iBlock *readFile_FontPack_(const iFontPack *d, const iString *path) {
    iBlock *data = NULL;
    if (d->archive) {
        /* Loading from a ZIP archive. */
        data = copy_Block(data_Archive(d->archive, path));
    }
    else if (d->loadPath) {
        /* Loading from a regular file. */
        iFile *srcFile = new_File(collect_String(concat_Path(d->loadPath, path)));
        if (open_File(srcFile, readOnly_FileMode)) {
            data = readAll_File(srcFile);
        }
        iRelease(srcFile);
    }
    return data;
}

static const char *styles_[max_FontStyle] = { "regular", "italic", "light", "semibold", "bold" };

void handleIniKeyValue_FontPack_(void *context, const iString *table, const iString *key,
                                 const iTomlValue *value) {
    iFontPack *d = context;
    if (isEmpty_String(table)) {
        if (!cmp_String(key, "version")) {
            d->version = number_TomlValue(value);
        }
        return;
    }
    if (!d->loadSpec) {
        return;
    }
    iUnused(table);
    if (!cmp_String(key, "name") && value->type == string_TomlType) {
        set_String(&d->loadSpec->name, value->value.string);        
    }
    else if (!cmp_String(key, "priority") && value->type == int64_TomlType) {
        d->loadSpec->priority = (int) value->value.int64;        
    }
    else if (!cmp_String(key, "height")) {
        d->loadSpec->heightScale[0] = d->loadSpec->heightScale[1] =
            iMin(2.0f, (float) number_TomlValue(value));
    }
    else if (!cmp_String(key, "glyphscale")) {
        d->loadSpec->glyphScale[0] = d->loadSpec->glyphScale[1] = (float) number_TomlValue(value);
    }
    else if (!cmp_String(key, "voffset")) {
        d->loadSpec->vertOffsetScale[0] = d->loadSpec->vertOffsetScale[1] =
            (float) number_TomlValue(value);
    }
    else if (startsWith_String(key, "ui.") || startsWith_String(key, "doc.")) {
        const int scope = startsWith_String(key, "ui.") ? 0 : 1;        
        if (endsWith_String(key, ".height")) {
            d->loadSpec->heightScale[scope] = iMin(2.0f, (float) number_TomlValue(value));
        }
        if (endsWith_String(key, ".glyphscale")) {
            d->loadSpec->glyphScale[scope] = (float) number_TomlValue(value);
        }
        else if (endsWith_String(key, ".voffset")) {
            d->loadSpec->vertOffsetScale[scope] = (float) number_TomlValue(value);
        }
    }
    else if (!cmp_String(key, "override") && value->type == boolean_TomlType) {
        iChangeFlags(d->loadSpec->flags, override_FontSpecFlag, value->value.boolean);
    }
    else if (!cmp_String(key, "monospace") && value->type == boolean_TomlType) {
        iChangeFlags(d->loadSpec->flags, monospace_FontSpecFlag, value->value.boolean);
    }
    else if (!cmp_String(key, "auxiliary") && value->type == boolean_TomlType) {
        iChangeFlags(d->loadSpec->flags, auxiliary_FontSpecFlag, value->value.boolean);
    }
    else if (!cmp_String(key, "allowspace") && value->type == boolean_TomlType) {
        iChangeFlags(d->loadSpec->flags, allowSpacePunct_FontSpecFlag, value->value.boolean);
    }
    else if (!cmp_String(key, "tweaks")) {
        iChangeFlags(d->loadSpec->flags, fixNunitoKerning_FontSpecFlag,
                     ((int) number_TomlValue(value)) & 1);
    }
    else if (value->type == string_TomlType) {
        iForIndices(i, styles_) {
            if (!cmp_String(key, styles_[i]) && !d->loadSpec->styles[i]) {
                iBeginCollect();
                iFontFile *ff = NULL;
                iString *cleanPath = collect_String(copy_String(value->value.string));
                int colIndex = 0;
                /* Remove the collection index from the path. */ {
                    iRegExpMatch m;
                    init_RegExpMatch(&m);
                    if (matchString_RegExp(fonts_.indexPattern, cleanPath, &m)) {
                        colIndex = toInt_String(collect_String(captured_RegExpMatch(&m, 1)));
                        removeEnd_String(cleanPath, size_Range(&m.range));
                    }
                }
                iString *fontFileId = concat_Path(d->loadPath, cleanPath);
                iAssert(!isEmpty_String(fontFileId));
                /* FontFiles share source data blocks. The entire FontFiles can be reused, too, 
                   if have the same collection index is in use. */
                iBlock *data = NULL;
                ff = findFile_Fonts_(&fonts_, fontFileId);
                if (ff) {
                    data = &ff->sourceData;
                }
                if (!ff || ff->colIndex != colIndex) {
                    if (!data) {
                        data = collect_Block(readFile_FontPack_(d, cleanPath));
                    }
                    if (data) {
                        ff = new_FontFile();
                        set_String(&ff->id, fontFileId);
                        ff->colIndex = colIndex;
                        load_FontFile_(ff, data);
                        pushBack_ObjectList(fonts_.files, ff); /* centralized ownership */
                        iRelease(ff);
                    }
                }
                d->loadSpec->styles[i] = ref_Object(ff);
                delete_String(fontFileId);
                iEndCollect();
                break;
            }
        }
    }
}

static iBool load_FontPack_(iFontPack *d, const iString *ini) {
    iBeginCollect();
    iBool ok = iFalse;
    iTomlParser *toml = collect_TomlParser(new_TomlParser());
    setHandlers_TomlParser(toml, handleIniTable_FontPack_, handleIniKeyValue_FontPack_, d);
    if (parse_TomlParser(toml, ini)) {
        ok = iTrue;
    }
    iAssert(d->loadSpec == NULL);
    iEndCollect();
    return ok;
}

static const char *fontpackIniEntryPath_ = "fontpack.ini";

iBool detect_FontPack(const iBlock *data) {
    iBool ok = iFalse;
    iArchive *zip = new_Archive();
    if (openData_Archive(zip, data) && entryCStr_Archive(zip, fontpackIniEntryPath_)) {
        iString ini;
        initBlock_String(&ini, dataCStr_Archive(zip, fontpackIniEntryPath_));
        if (isUtf8_Rangecc(range_String(&ini))) {
            /* Validate the TOML syntax without actually checking any values. */
            iTomlParser *toml = new_TomlParser();
            ok = parse_TomlParser(toml, &ini);
            delete_TomlParser(toml);
        }
        deinit_String(&ini);
    }
    iRelease(zip);
    return ok;
}

iBool loadArchive_FontPack(iFontPack *d, const iArchive *zip) {
    d->archive = zip;
    iBool ok = iFalse;
    const iBlock *iniData = dataCStr_Archive(zip, fontpackIniEntryPath_);
    if (iniData) {
        iString ini;
        initBlock_String(&ini, iniData);
        if (load_FontPack_(d, &ini)) {
            ok = iTrue;
        }
        deinit_String(&ini);
    }
    d->archive = NULL;
    return ok;
}

void setLoadPath_FontPack(iFontPack *d, const iString *path) {
    /* Note: `path` is for the local file system. */
    if (!d->loadPath) {
        d->loadPath = new_String();
    }
    set_String(d->loadPath, path);
    /* Pack ID is based on the file name. */
    setRange_String(&d->id, baseName_Path(path));
    setRange_String(&d->id, withoutExtension_Path(&d->id));
    replace_String(&d->id, " ", "-");
}

const iString *idFromUrl_FontPack(const iString *url) {
    iString *id = new_String();
    iUrl parts;
    init_Url(&parts, url);
    /* URLs always use slash as separator. */
    setRange_String(id, baseNameSep_Path(collectNewRange_String(parts.path), "/"));
    setRange_String(id, withoutExtension_Path(id));
    replace_String(id, " ", "-");
    return collect_String(id);
}

void setUrl_FontPack(iFontPack *d, const iString *url) {
    /* TODO: Should we remember the URL as well? */
    set_String(&d->id, idFromUrl_FontPack(url));
}

void setStandalone_FontPack(iFontPack *d, iBool standalone) {
    d->isStandalone = standalone;
}

void setReadOnly_FontPack(iFontPack *d, iBool readOnly) {
    d->isReadOnly = readOnly;
}

iBool isReadOnly_FontPack(const iFontPack *d) {
    return d->isReadOnly;
}

/*----------------------------------------------------------------------------------------------*/

static void unloadFonts_Fonts_(iFonts *d) {
    iForEach(PtrArray, i, &d->packs) {
        iFontPack *pack = i.ptr;
        delete_FontPack(pack);
    }
    clear_PtrArray(&d->packs);
}

static int cmpName_FontSpecPtr_(const void *a, const void *b) {
    const iFontSpec **p1 = (const iFontSpec **) a, **p2 = (const iFontSpec **) b;
    return cmpStringCase_String(&(*p1)->name, &(*p2)->name);
}

static int cmpPriority_FontSpecPtr_(const void *a, const void *b) {
    const iFontSpec **p1 = (const iFontSpec **) a, **p2 = (const iFontSpec **) b;
    const int cmp = -iCmp((*p1)->priority, (*p2)->priority); /* highest priority first */
    if (cmp) return cmp;
    return cmpName_FontSpecPtr_(a, b);
}

static int cmpSourceAndPriority_FontSpecPtr_(const void *a, const void *b) {
    const iFontSpec **p1 = (const iFontSpec **) a, **p2 = (const iFontSpec **) b;
    const int cmp = cmpStringCase_String(&(*p1)->sourcePath, &(*p2)->sourcePath);
    if (cmp) return cmp;
    return cmpPriority_FontSpecPtr_(a, b);
}

static void sortSpecs_Fonts_(iFonts *d) {
    clear_PtrArray(&d->specOrder);
    iConstForEach(PtrArray, p, &d->packs) {
        const iFontPack *pack = p.ptr;
        if (!isDisabled_FontPack(pack)) {
            iConstForEach(PtrArray, i, &pack->fonts) {
                pushBack_PtrArray(&d->specOrder, i.ptr);
            }
        }
    }
    sort_Array(&d->specOrder, cmpPriority_FontSpecPtr_);
}

static void disambiguateSpecs_Fonts_(iFonts *d) {
    /* Names of specs with the same human-readable label are augmented with the font ID. */
    const size_t numSpecs = size_PtrArray(&d->specOrder);
    for (size_t i = 0; i < numSpecs; i++) {
        iFontSpec *spec1 = at_PtrArray(&d->specOrder, i);
        for (size_t j = i + 1; j < numSpecs; j++) {
            iFontSpec *spec2 = at_PtrArray(&d->specOrder, j);
            if (equalCase_String(&spec1->name, &spec2->name)) {
                appendFormat_String(&spec1->name, " [%s]", cstr_String(&spec1->id));
                appendFormat_String(&spec2->name, " [%s]", cstr_String(&spec2->id));
            }
        }
    }    
}

static const iString *userFontsDirectory_Fonts_(const iFonts *d) {
    return collect_String(concatCStr_Path(&d->userDir, "fonts"));
}

void init_Fonts(const char *userDir) {
    iFonts *d = &fonts_;
    if (isTerminal_Platform()) {
        return; /* fonts not needed */
    }
    d->indexPattern = new_RegExp(":([0-9]+)$", 0);    
    initCStr_String(&d->userDir, userDir);
    const iString *userFontsDir = userFontsDirectory_Fonts_(d);
    makeDirs_Path(userFontsDir);
    init_PtrArray(&d->packs);
    d->files = new_ObjectList();
    init_PtrArray(&d->specOrder);
    /* Load the required fonts. */ {
        iFontPack *pack = new_FontPack();
        setCStr_String(&pack->id, "default");
        setReadOnly_FontPack(pack, iTrue);
        loadArchive_FontPack(pack, archive_Resources()); /* should never fail if we've made it this far */
        pushBack_PtrArray(&d->packs, pack);
#if defined (iPlatformMsys)
        /* The system UI font is used as the default font. */
        iString *winPath = collect_String(windowsDirectory_Win32());
        iString *segoePath = collect_String(concatCStr_Path(winPath, "Fonts\\segoeui.ttf"));
        if (fileExists_FileInfo(segoePath)) {
            iForEach(PtrArray, i, &pack->fonts) {
                iFontSpec *spec = i.ptr;
                if (!cmp_String(&spec->id, "default")) {
                    setCStr_String(&spec->id, "default-lgr"); /* being replaced */
                    break;
                }
            }
            iString *ini = collectNew_String();
            format_String(ini, 
                "[default]\n"
                "name    = \"Segoe UI\"\n"
                "regular = \"segoeui.ttf\"\n"
                "italic  = \"segoeuii.ttf\"\n"
                "bold    = \"segoeuib.ttf\"\n"
                "light   = \"segoeuil.ttf\"\n"
                "glyphscale = 0.9\n");
            iFontPack *sys = new_FontPack();
            sys->loadPath = concatCStr_Path(winPath, "Fonts");
            setCStr_String(&sys->id, "windows-system-fonts");
            setReadOnly_FontPack(sys, iTrue);            
            if (load_FontPack_(sys, ini)) {
                pushBack_PtrArray(&d->packs, sys);
            }
            else {
                delete_FontPack(sys);
            }
        }
#endif
#if defined (iPlatformAppleDesktop)
        pack = new_FontPack();
        setReadOnly_FontPack(pack, iTrue);
        pack->loadPath = newCStr_String("/System/Library/Fonts/");
        setCStr_String(&pack->id, "macos-system-fonts");
        iString ini;
        initBlock_String(&ini, &blobMacosSystemFontsIni_Resources);
        if (load_FontPack_(pack, &ini)) {
            pushBack_PtrArray(&d->packs, pack);
        }
        else {
            delete_FontPack(pack);
        }
        deinit_String(&ini);
#endif
    }
    /* Find and load .fontpack files in known locations. */ {
        const char *locations[] = {
            ".",
            "./fonts",
            "../share/lagrange", /* Note: These must match CMakeLists.txt install destination */
            "../../share/lagrange",
            cstr_String(userFontsDir),
            userDir,
        };
        const iString *execDir = collectNewRange_String(dirName_Path(execPath_App()));
        iForIndices(i, locations) {
            const iString *dir = collect_String(concatCStr_Path(execDir, locations[i]));
            iForEach(DirFileInfo, entry, iClob(new_DirFileInfo(dir))) {
                const iString *entryPath = path_FileInfo(entry.value);
                if (equalCase_Rangecc(baseName_Path(entryPath), "default.fontpack")) {
                    continue; /* The default pack only comes from resources.lgr. */
                }
                if (endsWithCase_String(entryPath, ".fontpack")) {
                    iArchive *arch = new_Archive();
                    if (openFile_Archive(arch, entryPath)) {
                        iFontPack *pack = new_FontPack();
                        setLoadPath_FontPack(pack, entryPath);
                        setReadOnly_FontPack(pack, !isWritable_FileInfo(entry.value));
#if defined (iPlatformApple)
                        if (startsWith_String(pack->loadPath, cstr_String(execDir))) {
                            setReadOnly_FontPack(pack, iTrue);
                        }
#endif
                        if (loadArchive_FontPack(pack, arch)) {
                            pushBack_PtrArray(&d->packs, pack);
                        }
                        else {
                            delete_FontPack(pack);
                            fprintf(stderr,
                                    "[fonts] errors detected in fontpack: %s\n",
                                    cstr_String(entryPath));
                        }
                    }
                    iRelease(arch);
                }
            }
        }
    }
    /* A standalone .ini file in the config directory. */ {
        const iString *userIni = collectNewCStr_String(concatPath_CStr(userDir, "fonts.ini"));
        iFile *f = new_File(userIni);
        if (open_File(f, text_FileMode | readOnly_FileMode)) {
            const iString *src = collect_String(readString_File(f));
            iFontPack *pack = new_FontPack();
            pack->loadPath = copy_String(userIni); /* no pack ID */
            if (load_FontPack_(pack, src)) {
                pushBack_PtrArray(&d->packs, pack);
            }
            else {
                delete_FontPack(pack);
                fprintf(stderr,
                        "[fonts] errors detected in fonts.ini: %s\n",
                        cstr_String(userIni));                
            }
        }
        iRelease(f);
    }
    /* Individual TrueType files in the user fonts directory. */ {
        iForEach(DirFileInfo, entry, iClob(new_DirFileInfo(userFontsDirectory_Fonts_(d)))) {
            const iString *entryPath = path_FileInfo(entry.value);
            if (endsWithCase_String(entryPath, ".ttf")) {
                iFile *f = new_File(entryPath);
                iFontFile *font = NULL;
                if (open_File(f, readOnly_FileMode)) {
                    iBlock *data = readAll_File(f);
                    font = new_FontFile();
                    load_FontFile_(font, data);
                    set_String(&font->id, entryPath);
                    pushBack_ObjectList(fonts_.files, font); /* centralized ownership */
                    iRelease(font);
                    delete_Block(data);
                }
                iRelease(f);
                if (!font) {
                    fprintf(stderr, "[fonts] failed to load: %s\n", cstr_String(entryPath));
                    continue;
                }
                iFontPack *pack = new_FontPack();
                setStandalone_FontPack(pack, iTrue);                
                iFontSpec *spec = new_FontSpec();
                spec->flags |= user_FontSpecFlag;
                if (detectMonospace_FontFile_(font)) {
                    spec->flags |= monospace_FontSpecFlag;
                }
                setRange_String(&spec->id, baseName_Path(collect_String(lower_String(&font->id))));
                setRange_String(&spec->id, withoutExtension_Path(&spec->id));                
                replace_String(&spec->id, " ", "-");
                setRange_String(&spec->name, baseName_Path(&font->id));
                setRange_String(&spec->name, withoutExtension_Path(&spec->name));
                set_String(&spec->sourcePath, entryPath);
                iForIndices(j, spec->styles) {
                    spec->styles[j] = ref_Object(font);
                }
                pushBack_PtrArray(&pack->fonts, spec);
                set_String(&pack->id, &spec->id);
                pack->loadPath = copy_String(entryPath);
                pushBack_PtrArray(&d->packs, pack);
            }
        }
    }
    sortSpecs_Fonts_(d);
    disambiguateSpecs_Fonts_(d);
#if !defined (NDEBUG)
    printf("[FontPack] %zu fonts available\n", size_Array(&d->specOrder));
#endif
}

void deinit_Fonts(void) {
    iFonts *d = &fonts_;
    if (isTerminal_Platform()) {
        return; /* fonts are not used */
    }
    unloadFonts_Fonts_(d);
    iAssert(isEmpty_ObjectList(d->files));
    deinit_PtrArray(&d->specOrder);
    deinit_PtrArray(&d->packs);
    iRelease(d->files);
    iRelease(d->indexPattern);
    deinit_String(&d->userDir);
}

const iPtrArray *listPacks_Fonts(void) {
    return &fonts_.packs;
}

const iFontSpec *findSpec_Fonts(const char *fontId) {
    iFonts *d = &fonts_;
    iConstForEach(PtrArray, i, &d->specOrder) {
        const iFontSpec *spec = i.ptr;
        if (!cmp_String(&spec->id, fontId)) {
            return spec;
        }
    }
    return NULL;
}

const iPtrArray *listSpecs_Fonts(iBool (*filterFunc)(const iFontSpec *)) {
    iFonts *d = &fonts_;
    iPtrArray *list = collectNew_PtrArray();
    iConstForEach(PtrArray, i, &d->specOrder) {
        if (filterFunc == NULL || filterFunc(i.ptr)) {
            pushBack_PtrArray(list, i.ptr);
        }
    }
    sort_Array(list, cmpName_FontSpecPtr_);
    return list;
}

const iPtrArray *listSpecsByPriority_Fonts(void) {
    return &fonts_.specOrder;
}

iString *infoText_FontPack(const iFontPack *d, iBool isFull) {
    const iFontPack *installed        = pack_Fonts(cstr_String(&d->id));
    const iBool      isInstalled      = (installed != NULL);
    const int        installedVersion = installed ? installed->version : 0;
    const iBool      isDisabled       = isDisabled_FontPack(d);
    iString         *str              = new_String();
    size_t           sizeInBytes      = 0;
    iPtrSet         *uniqueFiles      = new_PtrSet();
    iStringList     *names            = new_StringList();
    size_t           numNames         = 0;
    iBool            isAbbreviated    = iFalse;
    iConstForEach(PtrArray, i, listSpecs_FontPack(d)) {
        const iFontSpec *spec = i.ptr;
        numNames++;
        if (isFull || size_StringList(names) < 20) {
            pushBack_StringList(names, &spec->name);
        }
        else {
            isAbbreviated = iTrue;
        }
        iForIndices(j, spec->styles) {
            insert_PtrSet(uniqueFiles, spec->styles[j]->sourceData.i);
        }
    }
    iConstForEach(PtrSet, j, uniqueFiles) {
        sizeInBytes += ((const iBlockData *) *j.value)->size;
    }
    appendFormat_String(str, "%.1f ${mb} ", sizeInBytes / 1.0e6);
    if (size_PtrSet(uniqueFiles) > 1 || size_StringList(names) > 1) {
        appendFormat_String(str, "(");
        if (size_PtrSet(uniqueFiles) > 1) {
            appendCStr_String(str, formatCStrs_Lang("num.files.n", size_PtrSet(uniqueFiles)));
        }
        if (size_StringList(names) > 1) {
            if (!endsWith_String(str, "(")) {
                appendCStr_String(str, ", ");
            }
            appendCStr_String(str, formatCStrs_Lang("num.fonts.n", numNames));
        }
        appendFormat_String(str, ")");
    }
    appendFormat_String(str, " \u2014 %s%s\n", cstrCollect_String(joinCStr_StringList(names, ", ")),
                        isAbbreviated ? ", ..." : "");
    if (isInstalled && installedVersion != d->version) {
        appendCStr_String(str, format_Lang("${fontpack.meta.version}\n", d->version));
    }
    if (!isEmpty_String(&d->id)) {
        appendFormat_String(str, "%s %s%s\n",
                            isInstalled ? ballotChecked_Icon : ballotUnchecked_Icon,
                            isInstalled ? (installedVersion == d->version ? "${fontpack.meta.installed}"
                                           : format_CStr("${fontpack.meta.installed} (%s)",
                                                         format_Lang("${fontpack.meta.version}",
                                                                     installedVersion)))
                                           : "${fontpack.meta.notinstalled}",
                            isDisabled ? "${fontpack.meta.disabled}" : "");
    }
    iRelease(names);
    delete_PtrSet(uniqueFiles);
    return str;
}

const iArray *actions_FontPack(const iFontPack *d, iBool showInstalled) {
    iArray           *items     = new_Array(sizeof(iMenuItem));
    const iFontPackId fp        = id_FontPack(d);
    const char       *fpId      = cstr_String(fp.id);
    const iFontPack  *installed = pack_Fonts(fpId);
    const iBool       isEnabled = !isDisabled_FontPack(d);
    if (isInstalled_Fonts(fpId)) {
        if (d->version > installed->version) {
            pushBack_Array(
                items,
                &(iMenuItem){ format_Lang(add_Icon " ${fontpack.upgrade}", fpId, d->version),
                              SDLK_RETURN,
                              0,
                              "fontpack.install" });
        }
        if (iCmpStr(fpId, "windows-system-fonts") &&
            iCmpStr(fpId, "macos-system-fonts")) { /* system fonts can't be disabled */
            pushBack_Array(
                items,
                &(iMenuItem){ format_Lang(isEnabled ? close_Icon " ${fontpack.disable}"
                                                    : "${fontpack.enable}",
                                          fpId),
                              0,
                              0,
                              format_CStr("fontpack.enable arg:%d id:%s", !isEnabled, fpId) });
        }
        if (!d->isReadOnly && !d->isStandalone && installed->loadPath && d->loadPath &&
            !cmpString_String(installed->loadPath, d->loadPath)) {
            pushBack_Array(items,
                           &(iMenuItem){ format_Lang(delete_Icon " ${fontpack.delete}", fpId),
                                         0,
                                         0,
                                         format_CStr("fontpack.delete id:%s", fpId) });
        }
    }
    else if (d->isStandalone) {
        pushBack_Array(items,
                       &(iMenuItem){ format_Lang(add_Icon " " uiTextAction_ColorEscape
                                                          "\x1b[1m${fontpack.install}", fpId),
                                     SDLK_RETURN,
                                     0,
                                     "fontpack.install" });
        pushBack_Array(
            items, &(iMenuItem){ download_Icon " " saveToDownloads_Label, 0, 0, "document.save" });
    }
    if (showInstalled) {
        pushBack_Array(
            items,
            &(iMenuItem){
                fontpack_Icon " ${fontpack.open.aboutfonts}", 0, 0, "!open switch:1 url:about:fonts" });
    }
    return collect_Array(items);
}

iBool isDisabled_FontPack(const iFontPack *d) {
    return contains_StringSet(prefs_App()->disabledFontPacks, &d->id);
}

const iPtrArray *disabledSpecs_Fonts_(const iFonts *d) {
    iPtrArray *list = collectNew_PtrArray();
    iConstForEach(PtrArray, i, &d->packs) {
        const iFontPack *pack = i.ptr;
        if (isDisabled_FontPack(pack)) {
            iConstForEach(PtrArray, j, &pack->fonts) {
                pushBack_PtrArray(list, j.ptr);
            }
        }
    }
    return list;
}

static const char *boolStr_(int value) {
    return value ? "true" : "false";
}

static const iString *exportFontPackIni_Fonts_(const iFonts *d, const iRangecc packId) {
    iString *str = collectNew_String();
    const iFontPack *pack = pack_Fonts(cstr_Rangecc(packId));
    if (!pack) {
        appendFormat_String(str, "Fontpack \"%s\" not found.\n", cstr_Rangecc(packId));
        return str;
    }
    const iFontPackId fp = id_FontPack(pack);
    appendCStr_String(str, "To create a fontpack, add this fontpack.ini into a ZIP archive whose "
                      "name has the .fontpack file extension.\n```Fontpack configuration\n");
    appendFormat_String(str, "version = %d\n", fp.version);
    iConstForEach(PtrArray, i, &pack->fonts) {
        const iFontSpec *spec = i.ptr;
        appendFormat_String(str, "\n[%s]\n", cstr_String(&spec->id));
        appendFormat_String(str, "name = \"%s\"\n", cstrCollect_String(quote_String(&spec->name, iFalse)));
        appendFormat_String(str, "priority = %d\n", spec->priority);
        appendFormat_String(str, "override = %s\n", boolStr_(spec->flags & override_FontSpecFlag));
        appendFormat_String(str, "monospace = %s\n", boolStr_(spec->flags & monospace_FontSpecFlag));
        appendFormat_String(str, "auxiliary = %s\n", boolStr_(spec->flags & auxiliary_FontSpecFlag));
        appendFormat_String(str, "allowspace = %s\n", boolStr_(spec->flags & allowSpacePunct_FontSpecFlag));
        for (int j = 0; j < 2; ++j) {
            const char *scope = (j == 0 ? "ui" : "doc");
            appendFormat_String(str, "%s.height = %.3f\n", scope, spec->heightScale[j]);
            appendFormat_String(str, "%s.glyphscale = %.3f\n", scope, spec->glyphScale[j]);
            appendFormat_String(str, "%s.voffset = %.3f\n", scope, spec->vertOffsetScale[j]);
        }
        iForIndices(j, styles_) {
            appendFormat_String(str, "%s = \"%s\"\n", styles_[j],
                                cstrCollect_String(quote_String(&spec->sourcePath, iFalse)));
        }
    }
    appendCStr_String(str, "```\n");
    return str;
}

const iString *infoPage_Fonts(iRangecc query) {
    iFonts *d = &fonts_;
    if (!isEmpty_Range(&query)) {
        query.start++; /* skip the ? */
        return exportFontPackIni_Fonts_(d, query);
    }
    iString *str = collectNewCStr_String("# ${heading.fontpack.meta}\n"
         "=> gemini://skyjake.fi/fonts/  Download new fonts\n"
         "=> about:command?!open%20newtab:1%20gotoheading:2.4%20url:about:help  Using fonts in Lagrange\n"
         "=> about:command?!open%20newtab:1%20gotoheading:5%20url:about:help  How to create a fontpack\n");
    iPtrArray *specsByPack = collectNew_PtrArray();
    setCopy_PtrArray(specsByPack, &d->specOrder);
    sort_Array(specsByPack, cmpSourceAndPriority_FontSpecPtr_);
    iString *currentSourcePath = collectNew_String();
    for (int group = 0; group < 2; group++) {
        iBool isFirst = iTrue;
        iConstForEach(PtrArray, i, group == 0 ? specsByPack : disabledSpecs_Fonts_(d)) {
            const iFontSpec *spec = i.ptr;
            if (isEmpty_String(&spec->sourcePath)) {
                continue; /* built-in font */
            }
            if (!equal_String(&spec->sourcePath, currentSourcePath)) {
                set_String(currentSourcePath, &spec->sourcePath);
                /* Print some information about this page. */
                const iFontPack *pack = packByPath_Fonts(currentSourcePath);
                if (pack) {
                    if (!isDisabled_FontPack(pack) ^ group) {
                        if (isFirst) {
                            appendCStr_String(str, "\n## ");
                            appendCStr_String(str, group == 0 ? "${heading.fontpack.meta.enabled}"
                                                              : "${heading.fontpack.meta.disabled}");
                            appendCStr_String(str, "\n\n");
                            isFirst = iFalse;
                        }
                        const iString *packId = id_FontPack(pack).id;
                        appendFormat_String(str, "### %s\n",
                                            isEmpty_String(packId) ? "fonts.ini" :
                                            cstr_String(packId));
                        append_String(str, collect_String(infoText_FontPack(pack, iFalse)));
                        appendFormat_String(str, "=> %s ${fontpack.meta.viewfile}\n",
                                            cstrCollect_String(makeFileUrl_String(&spec->sourcePath)));
                        if (pack->isStandalone) {
                            appendFormat_String(str, "=> about:fonts?%s ${fontpack.export}\n",
                                                cstr_String(packId));
                        }
                        iConstForEach(Array, a, actions_FontPack(pack, iFalse)) {
                            const iMenuItem *item = a.value;
                            appendFormat_String(str,
                                                "=> about:command?%s %s\n",
                                                cstr_String(withSpacesEncoded_String(
                                                    collectNewCStr_String(item->command))),
                                                item->label);
                        }
                    }
                }
            }
        }
    }
    return str;
}

const iFontPack *pack_Fonts(const char *packId) {
    iFonts *d = &fonts_;
    if (!*packId) {
        return NULL;
    }
    iConstForEach(PtrArray, i, &d->packs) {
        const iFontPack *pack = i.ptr;
        if (!cmp_String(&pack->id, packId)) {
            return pack;
        }
    }
    return NULL;
}

const iFontPack *packByPath_Fonts(const iString *path) {
    iFonts *d = &fonts_;
    iConstForEach(PtrArray, i, &d->packs) {
        const iFontPack *pack = i.ptr;
        if (pack->loadPath && equal_String(pack->loadPath, path)) {
            return pack;
        }
    }
    return NULL;
}

void reload_Fonts(void) {
    iFonts *d = &fonts_;
    iString *userDir = copy_String(&d->userDir);
    deinit_Fonts(); /* `d->userDir` is freed */
    init_Fonts(cstr_String(userDir));
    resetFonts_App();
    invalidate_Window(get_MainWindow());
    delete_String(userDir);
}

void install_Fonts(const iString *packId, const iBlock *data) {
    if (!detect_FontPack(data)) {
        return;
    }
    /* Newly installed packs will never be disabled. */
    remove_StringSet(prefs_App()->disabledFontPacks, packId);
    iFonts *d = &fonts_;
    iFile *f = new_File(collect_String(concatCStr_Path(
        userFontsDirectory_Fonts_(d), format_CStr("%s.fontpack", cstr_String(packId)))));
    if (open_File(f, writeOnly_FileMode)) {
        write_File(f, data);
    }
    iRelease(f);
    /* Newly installed fontpacks may have a higher priority that overrides other fonts. */
    reload_Fonts();
    availableFontsChanged_App();
}

void installFontFile_Fonts(const iString *fileName, const iBlock *data) {
    iFonts *d = &fonts_;
    iFile *f = new_File(collect_String(concat_Path(userFontsDirectory_Fonts_(d), fileName)));
    if (open_File(f, writeOnly_FileMode)) {
        write_File(f, data);
    }
    iRelease(f);
    reload_Fonts();
    availableFontsChanged_App();
}

void enablePack_Fonts(const iString *packId, iBool enable) {
    iFonts *d = &fonts_;
    if (enable) {
        remove_StringSet(prefs_App()->disabledFontPacks, packId);
    }
    else {
        insert_StringSet(prefs_App()->disabledFontPacks, packId);
    }
    updateActive_Fonts();
    resetFonts_App();
    availableFontsChanged_App();
    invalidate_Window(get_MainWindow());
}

void updateActive_Fonts(void) {
    sortSpecs_Fonts_(&fonts_);
}

static void findCharactersInCMap_(iGmRequest *d, iGmRequest *req) {
    /* Note: Called in background thread. */
    iUnused(req);
    const iString *missingChars = userData_Object(d);
    if (isSuccess_GmStatusCode(status_GmRequest(d))) {
        iStringList *matchingPacks = new_StringList();
        iStringSet  *matchingSet   = new_StringSet();
        iChar needed[20];
        iChar minChar = UINT32_MAX, maxChar = 0;
        size_t numNeeded = 0;
        iConstForEach(String, ch, missingChars) {
            needed[numNeeded++] = ch.value;
            minChar = iMin(minChar, ch.value);
            maxChar = iMax(maxChar, ch.value);
            if (numNeeded == iElemCount(needed)) {
                /* Shouldn't be that many. */
                break;
            }
        }
        iBlock  *data = decompressGzip_Block(body_GmRequest(d));
        iRangecc line = iNullRange;
        while (nextSplit_Rangecc(range_Block(data), "\n", &line)) {
            iRangecc fontpackPath = iNullRange;
            for (const char *pos = line.start; pos < line.end; pos++) {
                if (*pos == ':') {
                    fontpackPath.start = line.start;
                    fontpackPath.end = pos;
                    line.start = pos + 1;
                    trimStart_Rangecc(&line);
                    break;
                }
            }
            if (fontpackPath.start) {
                /* Parse the character ranges and see if any match what we need. */
                const char *pos = line.start;
                while (pos < line.end) {
                    char *endp;
                    uint32_t first = strtoul(pos, &endp, 10);
                    uint32_t last  = first;
                    if (*endp == '-') {
                        last = strtoul(endp + 1, &endp, 10);        
                    }
                    if (maxChar < first) {
                        break; /* The rest are even higher. */
                    }
                    if (minChar <= last) {
                        for (size_t i = 0; i < numNeeded; i++) {
                            if (needed[i] >= first && needed[i] <= last) {
                                /* Got it. */
                                iString fp;
                                initRange_String(&fp, fontpackPath);
                                if (!contains_StringSet(matchingSet, &fp)) {
                                    pushBack_StringList(matchingPacks, &fp);
                                    insert_StringSet(matchingSet, &fp);
                                }
                                deinit_String(&fp);
                                break;
                            }
                        }
                    }
                    pos = endp + 1;
                }
            }
        }
        delete_Block(data);
        iString result;
        init_String(&result);
        format_String(&result, "font.found chars:%s packs:", cstr_String(missingChars));
        iConstForEach(StringList, s, matchingPacks) {
            if (s.pos != 0) {
                appendCStr_String(&result, ",");
            }
            append_String(&result, s.value);
        }
        postCommandString_Root(NULL, &result);
        deinit_String(&result);
        iRelease(matchingPacks);
        iRelease(matchingSet);
    }
    else {
        /* Report error. */
        postCommandf_Root(NULL,
                          "font.found chars:%s error:%d msg:\x1b[1m%s\x1b[0m\n%s",
                          cstr_String(missingChars),
                          status_GmRequest(d),
                          cstr_String(meta_GmRequest(d)),
                          cstr_String(url_GmRequest(d)));
    }
//    fflush(stdout);
    delete_String(userData_Object(d));
    /* We can't delete ourselves; threads must be joined from another thread. */
    SDL_PushEvent((SDL_Event *) &(SDL_UserEvent){
        .type = SDL_USEREVENT, .code = releaseObject_UserEventCode, .data1 = d });
}

void searchOnlineLibraryForCharacters_Fonts(const iString *chars) {
    /* Fetch the character map from skyjake.fi. */
    iGmRequest *req = new_GmRequest(certs_App());
    setUrl_GmRequest(req, collectNewCStr_String("gemini://skyjake.fi/fonts/cmap.txt.gz"));
    setUserData_Object(req, copy_String(chars));
    iConnect(GmRequest, req, finished, req, findCharactersInCMap_);
    submit_GmRequest(req);
}

iDefineClass(FontFile)
