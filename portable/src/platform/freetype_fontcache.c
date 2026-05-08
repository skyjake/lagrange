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

/* System font enumeration for the FreeType backend.
   Linux: fontconfig, Windows: DirectWrite. (On macOS, not supported.)
   Font metadata (family name, style file paths, metrics) is cached in a binary
   file with magic "lgFC" to speed up subsequent launches. */

#include "freetype_fontcache.h"
#include "freetype_text.h"
#include "fontcache.h"
#include "../fontpack.h"
#include "../app.h"

#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/path.h>
#include <the_Foundation/string.h>
#include <the_Foundation/stringhash.h>
#include <the_Foundation/stringlist.h>
#include <the_Foundation/thread.h>
#include <the_Foundation/time.h>
#include <the_Foundation/stream.h>
#include <the_Foundation/block.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include FT_SFNT_NAMES_H

#if defined (LAGRANGE_ENABLE_FONTCONFIG)
#   include <fontconfig/fontconfig.h>
#endif

#if defined (LAGRANGE_ENABLE_DIRECTWRITE)
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <dwrite.h>
#   include <combaseapi.h>
#endif

#include <stdlib.h>
#include <string.h>

static const uint32_t magic_FtFontCache_   = 0x6C674643u; /* "lgFC" */
static const uint32_t version_FtFontCache_ = 2u; /* v2 adds mtime header */

static void extractMetrics_(const char *filePath, int colIndex, iFontCacheStyle *out) {
    FT_Face face = NULL;
    if (FT_New_Face(ftLibrary_FtText(), filePath, colIndex, &face) != 0 || !face) return;
    TT_OS2 *os2 = FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
    if (os2) {
        out->winAscent  = (int32_t) os2->usWinAscent;
        out->winDescent = (int32_t) os2->usWinDescent;
    }
    TT_HoriHeader *hhea = FT_Get_Sfnt_Table(face, FT_SFNT_HHEA);
    if (hhea) {
        out->ascent  = (int32_t) hhea->Ascender;
        out->descent = (int32_t) hhea->Descender;
        out->lineGap = (int32_t) hhea->Line_Gap;
    }
    out->unitsPerEm = (int32_t) face->units_per_EM;
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt) face->units_per_EM);
    if (FT_Load_Char(face, 'M', FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) == 0) {
        out->emAdvance = (int32_t) (face->glyph->advance.x >> 6);
    }
    else {
        out->emAdvance = (int32_t) face->units_per_EM;
    }
    FT_Done_Face(face);
}

/*----------------------------------------------------------------------------------------------*/
/* iFontFile construction from cache entry */

static iFontFile *makeFontFile_(const iFontCacheStyle *style, enum iFontStyle styleId) {
    if (isEmpty_String(&style->identifier)) return NULL;
    iFontFile *f = new_FontFile();
    set_String(&f->id, &style->identifier);
    f->colIndex   = style->colIndex;
    f->style      = styleId;
    f->ascent     = style->ascent;
    f->descent    = style->descent;
    f->lineGap    = style->lineGap;
    f->winAscent  = style->winAscent;
    f->winDescent = style->winDescent;
    f->emAdvance  = style->emAdvance;
    f->unitsPerEm = style->unitsPerEm;
    /* sourceData left empty: allocData_FontFile() uses f->id as the file path. */
    return f;
}

static iBool addEntryToFontPack_(const iFontCacheEntry *e, iFontPack *pack) {
    if (isEmpty_String(&e->styles[regular_FontStyle].identifier)) return iFalse;
    iFontFile *files[max_FontStyle];
    files[regular_FontStyle] = makeFontFile_(&e->styles[regular_FontStyle], regular_FontStyle);
    if (!files[regular_FontStyle]) return iFalse;
    for (int s = 1; s < max_FontStyle; s++) {
        files[s] = makeFontFile_(&e->styles[s], s);
        if (!files[s]) {
            files[s] = ref_Object(files[regular_FontStyle]);
        }
    }
    iFontSpec *spec = new_FontSpec();
    set_String(&spec->id, &e->id);
    set_String(&spec->name, &e->name);
    spec->flags = e->flags;
    /* Default scaling: 1:1. */
    for (int t = 0; t < 2; t++) {
        spec->heightScale[t]     = 1.0f;
        spec->glyphScale[t]      = 1.0f;
        spec->vertOffsetScale[t] = 1.0f;
    }
    for (int s = 0; s < max_FontStyle; s++) {
        spec->styles[s] = files[s];
    }
    addSpec_FontPack(pack, spec);
    for (int s = 0; s < max_FontStyle; s++) {
        iRelease(files[s]);
    }
    return iTrue;
}

/*----------------------------------------------------------------------------------------------*/
/* Font directory mtime helpers */

/* List of directories to watch for font changes. */
static const char *fontDirs_[] = {
#if defined (LAGRANGE_ENABLE_FONTCONFIG)
    "/usr/share/fonts",
    "/usr/local/share/fonts",
#endif
#if defined (LAGRANGE_ENABLE_DIRECTWRITE)
    "C:\\Windows\\Fonts",
#endif
#if defined (__ANDROID__)
    "/system/fonts",
    "/system/product/fonts",
#endif
    NULL
};

/* Milliseconds since epoch for an iTime value. */
iLocalDef uint64_t timeMillis_(const iTime *t) {
    return (uint64_t) integralSeconds_Time(t) * 1000u +
           (uint64_t) nanoSeconds_Time(t) / 1000000u;
}

/* Maximum mtime across all watched font directories, expressed in milliseconds. */
static uint64_t fontDirMtimeMillis_(void) {
    iTime maxTime;
    iZap(maxTime);
    for (int i = 0; fontDirs_[i]; i++) {
        iFileInfo *fi = newCStr_FileInfo(fontDirs_[i]);
        if (exists_FileInfo(fi)) {
            iTime t = lastModified_FileInfo(fi);
            max_Time(&maxTime, &t);
        }
        iRelease(fi);
    }
    /* Also watch user-local font directories. */ {
        iString *home = home_Path();
        const char *userFontDirs[] = { ".fonts", ".local/share/fonts", NULL };
        for (int i = 0; userFontDirs[i]; i++) {
            iString   *path = concatCStr_Path(home, userFontDirs[i]);
            iFileInfo *fi   = new_FileInfo(path);
            if (exists_FileInfo(fi)) {
                iTime t = lastModified_FileInfo(fi);
                max_Time(&maxTime, &t);
            }
            iRelease(fi);
            delete_String(path);
        }
        delete_String(home);
    }
    return timeMillis_(&maxTime);
}

/*----------------------------------------------------------------------------------------------*/
/* iFontCacheEntry (shared with apple_fontcache) is defined in fontcache.h.
   FreeType uses styles[s].identifier as the font file path
   and styles[s].colIndex as the face index. */

static iString *cacheFilePath_(void) {
    return concatCStr_Path(dataDir_App(), "ft_fonts.lgfc");
}

/* v2 cache header: magic(u32), version(u32), mtime_ms(u64), count(u32) */
static void writeCacheHeader_(iStream *out, uint64_t mtimeMs, uint32_t count) {
    writeU32_Stream(out, magic_FtFontCache_);
    writeU32_Stream(out, version_FtFontCache_);
    writeU64_Stream(out, mtimeMs);
    writeU32_Stream(out, count);
}

static iBool readCacheHeader_(iStream *in, uint64_t *mtimeMs_out, uint32_t *count_out) {
    const uint32_t magic   = readU32_Stream(in);
    const uint32_t version = readU32_Stream(in);
    if (magic != magic_FtFontCache_ || version != version_FtFontCache_) return iFalse;
    *mtimeMs_out = readU64_Stream(in);
    *count_out   = readU32_Stream(in);
    return iTrue;
}

static iBool readCache_(const iString *path, iFontPack *pack) {
    iFile *f = new_File(path);
    if (!open_File(f, readOnly_FileMode)) {
        iRelease(f);
        return iFalse;
    }
    uint64_t mtimeMs = 0;
    uint32_t count   = 0;
    if (!readCacheHeader_(stream_File(f), &mtimeMs, &count)) {
        iRelease(f);
        return iFalse;
    }
    for (uint32_t i = 0; i < count; i++) {
        iFontCacheEntry entry;
        init_FontCacheEntry(&entry);
        deserialize_FontCacheEntry(&entry, stream_File(f));
        addEntryToFontPack_(&entry, pack);
        deinit_FontCacheEntry(&entry);
    }
    iRelease(f);
    return count > 0;
}

static void writeCache_(const iString *path, const iArray *entries) {
    iFile *f = new_File(path);
    if (!open_File(f, writeOnly_FileMode)) {
        iRelease(f);
        return;
    }
    iStream *out = (iStream *) f;
    writeCacheHeader_(out, fontDirMtimeMillis_(), (uint32_t) size_Array(entries));
    iConstForEach(Array, i, entries) {
        serialize_FontCacheEntry(i.value, out);
    }
    iRelease(f);
}

/* Check whether the stored mtime in the cache matches current font directories. */
static iBool isCacheStale_(const iString *path) {
    iFile *f = new_File(path);
    if (!open_File(f, readOnly_FileMode)) {
        iRelease(f);
        return iTrue; /* no cache file = stale */
    }
    iStream  *in      = (iStream *) f;
    uint64_t  mtimeMs = 0;
    uint32_t  count   = 0;
    const iBool valid = readCacheHeader_(in, &mtimeMs, &count);
    iRelease(f);
    if (!valid || count == 0) return iTrue;
    return fontDirMtimeMillis_() != mtimeMs;
}

/*----------------------------------------------------------------------------------------------*/

#if defined (LAGRANGE_ENABLE_FONTCONFIG)

/* Normalize a family name to a spec ID: "sys-<alphanumeric-hyphenated>" */
static void makeSpecId_(const char *familyName, iString *id_out) {
    setCStr_String(id_out, "sys-");
    iBool needSep = iFalse;
    for (const char *p = familyName; *p; p++) {
        const unsigned char c = (unsigned char) *p;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            if (needSep && size_String(id_out) > 4) {
                appendChar_String(id_out, '-');
                needSep = iFalse;
            }
            appendChar_String(id_out, (iChar) c);
        }
        else if (c >= 'A' && c <= 'Z') {
            if (needSep && size_String(id_out) > 4) {
                appendChar_String(id_out, '-');
                needSep = iFalse;
            }
            appendChar_String(id_out, (iChar) (c + 32));
        }
        else if (size_String(id_out) > 4) {
            needSep = iTrue;
        }
    }
}

static enum iFontStyle fcStyleToFontStyle_(int weight, int slant) {
    const iBool isBold   = (weight >= FC_WEIGHT_BOLD);
    const iBool isItalic = (slant  != FC_SLANT_ROMAN);
    const iBool isLight  = (weight <= FC_WEIGHT_LIGHT);
    const iBool isSemi   = (weight >= FC_WEIGHT_DEMIBOLD && weight < FC_WEIGHT_BOLD);
    if (isBold && !isItalic) return bold_FontStyle;
    if (isItalic && !isBold) return italic_FontStyle;
    if (isLight)             return light_FontStyle;
    if (isSemi)              return semiBold_FontStyle;
    return regular_FontStyle;
}

static void enumerateFontconfig_(iArray *entries_out) {
    FcConfig *config = FcInitLoadConfigAndFonts();
    if (!config) return;
    FcPattern  *pat = FcPatternCreate();
    FcObjectSet *os = FcObjectSetBuild(FC_FAMILY, FC_FILE, FC_INDEX,
                                       FC_WEIGHT, FC_SLANT, FC_SPACING, NULL);
    FcFontSet *fs = FcFontList(config, pat, os);
    FcObjectSetDestroy(os);
    FcPatternDestroy(pat);
    if (!fs) { FcConfigDestroy(config); return; }

    /* Group fonts by family name (normalized to spec ID). */
    iStringHash familyMap; /* spec ID -> index in entries_out */
    init_StringHash(&familyMap);

    for (int i = 0; i < fs->nfont; i++) {
        FcPattern *font = fs->fonts[i];
        FcChar8 *familyCStr = NULL;
        FcChar8 *fileCStr   = NULL;
        int      colIndex   = 0;
        int      weight     = FC_WEIGHT_MEDIUM;
        int      slant      = FC_SLANT_ROMAN;
        int      spacing    = FC_PROPORTIONAL;
        if (FcPatternGetString(font, FC_FAMILY, 0, &familyCStr) != FcResultMatch) continue;
        if (FcPatternGetString(font, FC_FILE,   0, &fileCStr)   != FcResultMatch) continue;
        FcPatternGetInteger(font, FC_INDEX,   0, &colIndex);
        FcPatternGetInteger(font, FC_WEIGHT,  0, &weight);
        FcPatternGetInteger(font, FC_SLANT,   0, &slant);
        FcPatternGetInteger(font, FC_SPACING, 0, &spacing);

        iString specId;
        init_String(&specId);
        makeSpecId_((const char *) familyCStr, &specId);

        /* Find or create entry. */
        iAnyObject *existing = value_StringHash(&familyMap, &specId);
        iFontCacheEntry *entry;
        if (existing) {
            entry = at_Array(entries_out, (size_t) existing);
        }
        else {
            iFontCacheEntry newEntry;
            init_FontCacheEntry(&newEntry);
            set_String(&newEntry.id, &specId);
            setCStr_String(&newEntry.name, (const char *) familyCStr);
            if (spacing == FC_MONO) newEntry.flags |= monospace_FontSpecFlag;
            const size_t idx = size_Array(entries_out);
            pushBack_Array(entries_out, &newEntry);
            entry = at_Array(entries_out, idx);
            insertCStr_StringHash(&familyMap, cstr_String(&specId), (iAnyObject *)(size_t) idx);
        }
        deinit_String(&specId);

        const enum iFontStyle styleId = fcStyleToFontStyle_(weight, slant);
        iFontCacheStyle    *sty     = &entry->styles[styleId];
        if (!isEmpty_String(&sty->identifier)) {
            /* Style slot already filled. */
            continue;
        }
        setCStr_String(&sty->identifier, (const char *) fileCStr);
        sty->colIndex = colIndex;
        extractMetrics_((const char *) fileCStr, colIndex, sty);
    }
    deinit_StringHash(&familyMap);
    FcFontSetDestroy(fs);
    FcConfigDestroy(config);
}

#endif /* LAGRANGE_ENABLE_FONTCONFIG */

/*----------------------------------------------------------------------------------------------*/

#if defined (LAGRANGE_ENABLE_DIRECTWRITE)

/* Windows: enumerate fonts via DirectWrite.
   We resolve each IDWriteFont to a file path and extract metrics. */
static void enumerateDirectWrite_(iArray *entries_out) {
    IDWriteFactory *factory = NULL;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                   &__uuidof(IDWriteFactory),
                                   (IUnknown **) &factory))) {
        return;
    }
    IDWriteFontCollection *collection = NULL;
    factory->GetSystemFontCollection(&collection, FALSE);
    if (!collection) { factory->Release(); return; }

    const UINT32 familyCount = collection->GetFontFamilyCount();
    for (UINT32 fi = 0; fi < familyCount; fi++) {
        IDWriteFontFamily *family = NULL;
        if (FAILED(collection->GetFontFamily(fi, &family))) continue;

        /* Get family name in the current locale. */
        IDWriteLocalizedStrings *names = NULL;
        family->GetFamilyNames(&names);
        if (!names) { family->Release(); continue; }

        UINT32 nameIdx = 0;
        BOOL   exists  = FALSE;
        wchar_t localeName[LOCALE_NAME_MAX_LENGTH];
        GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH);
        names->FindLocaleName(localeName, &nameIdx, &exists);
        if (!exists) nameIdx = 0;
        UINT32 nameLen = 0;
        names->GetStringLength(nameIdx, &nameLen);
        wchar_t *wname = (wchar_t *) malloc((nameLen + 1) * sizeof(wchar_t));
        names->GetString(nameIdx, wname, nameLen + 1);
        names->Release();

        /* Convert to UTF-8 for the spec ID/name. */
        char utf8name[512] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, wname, -1, utf8name, sizeof(utf8name), NULL, NULL);
        free(wname);

        iString specId, dispName;
        init_String(&specId);
        init_String(&dispName);
        setCStr_String(&dispName, utf8name);
        makeSpecId_(utf8name, &specId);

        iFontCacheEntry entry;
        init_FontCacheEntry(&entry);
        set_String(&entry.id, &specId);
        set_String(&entry.name, &dispName);
        deinit_String(&specId);
        deinit_String(&dispName);

        /* Check first font for monospace. */
        const UINT32 fontCount = family->GetFontCount();
        for (UINT32 fj = 0; fj < fontCount; fj++) {
            IDWriteFont *font = NULL;
            if (FAILED(family->GetFont(fj, &font))) continue;

            const DWRITE_FONT_WEIGHT weight = font->GetWeight();
            const DWRITE_FONT_STYLE  style  = font->GetStyle();
            enum iFontStyle styleId;
            if (weight >= DWRITE_FONT_WEIGHT_BOLD && style == DWRITE_FONT_STYLE_NORMAL) {
                styleId = bold_FontStyle;
            }
            else if (style != DWRITE_FONT_STYLE_NORMAL && weight < DWRITE_FONT_WEIGHT_BOLD) {
                styleId = italic_FontStyle;
            }
            else if (weight <= DWRITE_FONT_WEIGHT_LIGHT) {
                styleId = light_FontStyle;
            }
            else if (weight >= DWRITE_FONT_WEIGHT_SEMI_BOLD && weight < DWRITE_FONT_WEIGHT_BOLD) {
                styleId = semiBold_FontStyle;
            }
            else {
                styleId = regular_FontStyle;
            }
            if (!isEmpty_String(&entry.styles[styleId].identifier)) {
                font->Release();
                continue;
            }
            /* Get font file path. */
            IDWriteFontFace *face = NULL;
            if (FAILED(font->CreateFontFace(&face))) { font->Release(); continue; }
            UINT32 fileCount = 0;
            face->GetFiles(&fileCount, NULL);
            if (fileCount > 0) {
                IDWriteFontFile **files = (IDWriteFontFile **) calloc(fileCount, sizeof(*files));
                face->GetFiles(&fileCount, files);
                IDWriteFontFile *ffile = files[0];
                IDWriteFontFileLoader *loader = NULL;
                const void *refKey    = NULL;
                UINT32      refKeyLen = 0;
                ffile->GetReferenceKey(&refKey, &refKeyLen);
                ffile->GetLoader(&loader);
                IDWriteLocalFontFileLoader *localLoader = NULL;
                if (loader && SUCCEEDED(loader->QueryInterface(&__uuidof(IDWriteLocalFontFileLoader),
                                                               (void **) &localLoader))) {
                    UINT32 pathLen = 0;
                    localLoader->GetFilePathLengthFromKey(refKey, refKeyLen, &pathLen);
                    wchar_t *wpath = (wchar_t *) malloc((pathLen + 1) * sizeof(wchar_t));
                    localLoader->GetFilePathFromKey(refKey, refKeyLen, wpath, pathLen + 1);
                    char pathUtf8[1024] = { 0 };
                    WideCharToMultiByte(CP_UTF8, 0, wpath, -1, pathUtf8, sizeof(pathUtf8), NULL, NULL);
                    free(wpath);
                    /* Get face index from the font face. */
                    const UINT32 faceIdx = face->GetIndex();
                    iFontCacheStyle *sty = &entry.styles[styleId];
                    setCStr_String(&sty->identifier, pathUtf8);
                    sty->colIndex = (int) faceIdx;
                    extractMetrics_(pathUtf8, faceIdx, sty);
                    localLoader->Release();
                }
                if (loader)  loader->Release();
                for (UINT32 k = 0; k < fileCount; k++) files[k]->Release();
                free(files);
            }
            face->Release();
            font->Release();
        }
        /* Fill missing styles with regular. */
        for (int s = 0; s < max_FontStyle; s++) {
            if (isEmpty_String(&entry.styles[s].identifier) &&
                !isEmpty_String(&entry.styles[regular_FontStyle].identifier)) {
                set_String(&entry.styles[s].identifier, &entry.styles[regular_FontStyle].identifier);
                entry.styles[s].colIndex   = entry.styles[regular_FontStyle].colIndex;
                entry.styles[s].ascent     = entry.styles[regular_FontStyle].ascent;
                entry.styles[s].descent    = entry.styles[regular_FontStyle].descent;
                entry.styles[s].lineGap    = entry.styles[regular_FontStyle].lineGap;
                entry.styles[s].winAscent  = entry.styles[regular_FontStyle].winAscent;
                entry.styles[s].winDescent = entry.styles[regular_FontStyle].winDescent;
                entry.styles[s].emAdvance  = entry.styles[regular_FontStyle].emAdvance;
                entry.styles[s].unitsPerEm = entry.styles[regular_FontStyle].unitsPerEm;
            }
        }
        if (!isEmpty_String(&entry.styles[regular_FontStyle].identifier)) {
            pushBack_Array(entries_out, &entry);
        }
        else {
            deinit_FontCacheEntry(&entry);
        }
        family->Release();
    }
    collection->Release();
    factory->Release();
}

#endif /* LAGRANGE_ENABLE_DIRECTWRITE */

/*----------------------------------------------------------------------------------------------*/
/* Background worker: validates and refreshes the font cache. */

iDeclareType(FtFontWorker)

struct Impl_FtFontWorker {
    iThread *thread;
    iString  cachePath;
    iBool    stopWorker;
};

static iFtFontWorker *worker_;

iDeclareTypeConstructionArgs(FtFontWorker, const iString *cachePath)

static iThreadResult refresh_FtFontWorker_(iThread *thread) {
    iFtFontWorker *d = userData_Thread(thread);
    if (!isCacheStale_(&d->cachePath) || d->stopWorker) {
        goto done_;
    }
    /* Directories changed: re-enumerate and write a fresh cache. */
    iArray entries;
    init_Array(&entries, sizeof(iFontCacheEntry));
#if defined (LAGRANGE_ENABLE_FONTCONFIG)
    enumerateFontconfig_(&entries);
#elif defined (LAGRANGE_ENABLE_DIRECTWRITE)
    enumerateDirectWrite_(&entries);
#endif
    if (!d->stopWorker) {
        writeCache_(&d->cachePath, &entries);
    }
    iForEach(Array, i, &entries) { deinit_FontCacheEntry(i.value); }
    deinit_Array(&entries);
    if (!d->stopWorker) {
        postCommand_App("font.reload");
    }
done_:
    worker_ = NULL;
    iRelease(d->thread);
    d->thread = NULL;
    delete_FtFontWorker(d);
    return 0;
}

void init_FtFontWorker(iFtFontWorker *d, const iString *cachePath) {
    d->thread     = NULL;
    d->stopWorker = iFalse;
    initCopy_String(&d->cachePath, cachePath);
    d->thread = new_Thread(refresh_FtFontWorker_);
    setName_Thread(d->thread, "FtFontWorker");
    setUserData_Thread(d->thread, d);
    start_Thread(d->thread);
}

void deinit_FtFontWorker(iFtFontWorker *d) {
    if (d->thread) {
        d->stopWorker = iTrue;
        join_Thread(d->thread);
        iReleasePtr(&d->thread);
    }
    deinit_String(&d->cachePath);
}

iDefineTypeConstructionArgs(FtFontWorker, (const iString *cachePath), cachePath)

static void startWorker_(const iString *cachePath) {
    if (!worker_) {
        worker_ = new_FtFontWorker(cachePath);
    }
}

/*----------------------------------------------------------------------------------------------*/

#if defined (__ANDROID__)

static void enumerateAndroid_(iArray *entries_out) {
    const char *androidFontDirs[] = { "/system/fonts", "/system/product/fonts", NULL };
    iStringHash familyMap;
    init_StringHash(&familyMap);
    for (int di = 0; androidFontDirs[di]; di++) {
        iString *dir = newCStr_String(androidFontDirs[di]);
        iFileInfo *fi = new_FileInfo(dir);
        if (!isDirectory_FileInfo(fi)) { iRelease(fi); delete_String(dir); continue; }
        iDirFileInfo *contents = directoryContents_FileInfo(fi);
        iForEach(DirFileInfo, entry, contents) {
            const iFileInfo *info = entry.value;
            const iString   *path = path_FileInfo(info);
            if (!endsWith_String(path, ".ttf") && !endsWith_String(path, ".otf") &&
                !endsWith_String(path, ".ttc")) continue;
            /* Derive a family name from the filename stem. */
            iRangecc baseName = baseName_Path(path);
            iString *stem = collect_String(newRange_String(baseName));
            /* Strip common suffix patterns like -Regular, -Bold, etc. */
            static const char *suffixes[] = {
                "-Regular", "-Bold", "-Italic", "-BoldItalic", "-Light",
                "-Medium", "-Thin", "-Black", "-Condensed", NULL
            };
            for (int si = 0; suffixes[si]; si++) {
                if (endsWith_String(stem, suffixes[si])) {
                    truncate_String(stem, size_String(stem) - strlen(suffixes[si]));
                }
            }
            /* Remove extension. */
            const size_t dot = lastIndexOfCStr_String(stem, ".");
            if (dot != iInvalidPos) truncate_String(stem, dot);
            /* Make spec ID. */
            iString specId;
            init_String(&specId);
            setCStr_String(&specId, "and-");
            iConstForEach(String, ch, stem) {
                const iChar c = ch.value;
                if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                    appendChar_String(&specId, c);
                }
                else if (c >= 'A' && c <= 'Z') {
                    appendChar_String(&specId, c + 32);
                }
                else if (size_String(&specId) > 4) {
                    appendChar_String(&specId, '-');
                }
            }
            /* Find or create entry. */
            iAnyObject *existing = value_StringHash(&familyMap, &specId);
            iFontCacheEntry *entry;
            if (existing) {
                entry = at_Array(entries_out, (size_t) existing);
            }
            else {
                iFontCacheEntry newEntry;
                init_FontCacheEntry(&newEntry);
                set_String(&newEntry.id, &specId);
                set_String(&newEntry.name, stem);
                const size_t idx = size_Array(entries_out);
                pushBack_Array(entries_out, &newEntry);
                entry = at_Array(entries_out, idx);
                insertCStr_StringHash(&familyMap, cstr_String(&specId),
                                      (iAnyObject *)(size_t) idx);
            }
            deinit_String(&specId);
            /* Determine style from filename and populate the style slot. */
            iBool isBold   = indexOfCStr_String(path, "Bold") != iInvalidPos || indexOfCStr_String(path, "bold") != iInvalidPos;
            iBool isItalic = indexOfCStr_String(path, "Italic")  != iInvalidPos ||
                             indexOfCStr_String(path, "italic")  != iInvalidPos ||
                             indexOfCStr_String(path, "Oblique") != iInvalidPos;
            iBool isLight  = indexOfCStr_String(path, "Light")   != iInvalidPos ||
                             indexOfCStr_String(path, "Thin")    != iInvalidPos;
            iBool isSemi   = indexOfCStr_String(path, "Medium")  != iInvalidPos ||
                             indexOfCStr_String(path, "SemiBold") != iInvalidPos;
            enum iFontStyle styleId;
            if (isBold && !isItalic)       styleId = bold_FontStyle;
            else if (isItalic && !isBold)  styleId = italic_FontStyle;
            else if (isLight)              styleId = light_FontStyle;
            else if (isSemi)               styleId = semiBold_FontStyle;
            else                           styleId = regular_FontStyle;
            if (!isEmpty_String(&entry->styles[styleId].identifier)) {
                /* Already have this style; try regular as fallback slot. */
                if (styleId != regular_FontStyle &&
                    isEmpty_String(&entry->styles[regular_FontStyle].identifier)) {
                    styleId = regular_FontStyle;
                }
                else {
                    continue; /* slot taken */
                }
            }
            set_String(&entry->styles[styleId].identifier, path);
            extractMetrics_(cstr_String(path), 0, &entry->styles[styleId]);
        }
        iRelease(contents);
        iRelease(fi);
        delete_String(dir);
    }
    /* Fill missing styles with regular. */
    iForEach(Array, i, entries_out) {
        iFontCacheEntry *e = i.value;
        if (isEmpty_String(&e->styles[regular_FontStyle].identifier)) continue;
        for (int s = 0; s < max_FontStyle; s++) {
            if (isEmpty_String(&e->styles[s].identifier)) {
                set_String(&e->styles[s].identifier, &e->styles[regular_FontStyle].identifier);
                e->styles[s].colIndex   = e->styles[regular_FontStyle].colIndex;
                e->styles[s].ascent     = e->styles[regular_FontStyle].ascent;
                e->styles[s].descent    = e->styles[regular_FontStyle].descent;
                e->styles[s].lineGap    = e->styles[regular_FontStyle].lineGap;
                e->styles[s].winAscent  = e->styles[regular_FontStyle].winAscent;
                e->styles[s].winDescent = e->styles[regular_FontStyle].winDescent;
                e->styles[s].emAdvance  = e->styles[regular_FontStyle].emAdvance;
                e->styles[s].unitsPerEm = e->styles[regular_FontStyle].unitsPerEm;
            }
        }
    }
    deinit_StringHash(&familyMap);
}

#endif /* __ANDROID__ */

void enumerateSystemFonts_FontPack_(iFontPack *pack) {
    iArray entries;
    init_Array(&entries, sizeof(iFontCacheEntry));
#if defined (LAGRANGE_ENABLE_FONTCONFIG)
    enumerateFontconfig_(&entries);
#elif defined (LAGRANGE_ENABLE_DIRECTWRITE)
    enumerateDirectWrite_(&entries);
#elif defined (__ANDROID__)
    enumerateAndroid_(&entries);
#endif
    writeCache_(collect_String(cacheFilePath_()), &entries);
    /* Populate the font pack and clean up. */
    iForEach(Array, i, &entries) {
        addEntryToFontPack_(i.value, pack);
        deinit_FontCacheEntry(i.value);
    }
    deinit_Array(&entries);
}

iBool loadCachedSystemFonts_FontPack_(iFontPack *pack) {
    iString *cachePath = cacheFilePath_();
    const iBool ok = readCache_(cachePath, pack);
    if (!ok) {
        /* First run or stale cache: enumerate synchronously. */
        enumerateSystemFonts_FontPack_(pack);
    }
    /* Start background thread to check if dirs have changed since last enumeration. */
    startWorker_(cachePath);
    iRelease(cachePath);
    return ok;
}

void stopFontWorker_FtFontCache(void) {
    if (worker_) {
        delete_FtFontWorker(worker_);
        worker_ = NULL;
    }
}
