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
#include <the_Foundation/regexp.h>
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
#include FT_MULTIPLE_MASTERS_H

#if defined (LAGRANGE_ENABLE_FONTCONFIG)
#   include <fontconfig/fontconfig.h>
#endif

#if defined (LAGRANGE_ENABLE_DIRECTWRITE)
#   define WIN32_LEAN_AND_MEAN
#   define COBJMACROS
#   include <windows.h>
#   include <initguid.h>
#   include <dwrite.h>
#   include <combaseapi.h>
#endif

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t magic_FtFontCache_   = 0x6C674643u; /* "lgFC" */
static const uint32_t version_FtFontCache_ = 5u; /* bump to reset old cached data */

static void extractMetrics_(FT_Library lib, const char *filePath, int colIndex,
                            iFontCacheStyle *out) {
    FT_Face face = NULL;
    if (FT_New_Face(lib, filePath, colIndex, &face) != 0 || !face) {
        return;
    }
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
    f->numAxes    = style->numAxes;
    memcpy(f->axes, style->axes, sizeof(f->axes));
    allocData_FontFile(f); /* sourceData is empty; loads via f->id file path */
    return f;
}

#define defaultSystemGlyphScale_FreeType_ 0.866f

static float glyphScaleFromMetrics_(const iFontFile *reg) {
    /* FIXME: This is probably the same for both FreeType and Core Text. */
    if (!reg) return defaultSystemGlyphScale_FreeType_;
    const int   sTypoTotal = reg->ascent - reg->descent;       /* descent is negative */
    const int   winTotal   = reg->winAscent + reg->winDescent; /* 0 if unavailable */
    const int   naturalH   = iMax(sTypoTotal, winTotal) + iMax(0, reg->lineGap);
    const float gs = (sTypoTotal > 0 && naturalH > 0) ? (float) sTypoTotal / (float) naturalH
                                                      : defaultSystemGlyphScale_FreeType_;
    return iMax(gs, 0.8f); /* floor: don't over-shrink fonts with very large Win metrics */
}

/* The UI has been designed assuming the metrics of the built-in Roboto font: hhea ascent
   of 1900 and descent of -500 design units, with `ui.glyphscale = 0.775` (see fontpack.ini). */
#define robotoAscentRatio_  (1900.0f / 2400.0f)
#define robotoUIGlyphScale_ 0.775f

static float uiGlyphScaleFromMetrics_(const iFontFile *reg) {
    /* Scale the glyphs so the ascent matches Roboto's in the UI. The baseline is placed at
       `height * glyphScale * ascent / (ascent - descent)`. */
    const int total = reg ? reg->ascent - reg->descent : 0;
    if (!reg || reg->ascent <= 0 || total <= 0) {
        return glyphScaleFromMetrics_(reg);
    }
    const float gs = robotoUIGlyphScale_ * robotoAscentRatio_ * (float) total / (float) reg->ascent;
    return iMin(gs, 1.0f);
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
    for (int s = 0; s < max_FontStyle; s++) {
        spec->styles[s] = ref_Object(files[s]);
    }
    /* Default scaling based on metrics. */
    spec->glyphScale[0] = uiGlyphScaleFromMetrics_(spec->styles[0]);
    spec->glyphScale[1] = glyphScaleFromMetrics_(spec->styles[0]);
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
#if defined (LAGRANGE_ENABLE_FONTCONFIG)
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
#endif
    return timeMillis_(&maxTime);
}

/*----------------------------------------------------------------------------------------------*/
/* iFontCacheEntry (shared with apple_fontcache) is defined in fontcache.h.
   FreeType uses styles[s].identifier as the font file path
   and styles[s].colIndex as the face index. */

static iString *cacheFilePath_(void) {
    return concatCStr_Path(dataDir_App(), "system-fonts.lgr");
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
    /* An empty cache is not stale: re-enumerating would find nothing, and reload endlessly. */
    if (!valid) return iTrue;
    return fontDirMtimeMillis_() != mtimeMs;
}

/*----------------------------------------------------------------------------------------------*/

/* Normalize a family name to a spec ID: "system-<alphanumeric-hyphenated>" */
static void makeSpecId_(iRangecc familyName, iString *id_out) {
    static const char *prefix = "system-";
    setCStr_String(id_out, prefix);
    iBool needSep = iFalse;
    for (const char *p = familyName.start; p < familyName.end; p++) {
        const unsigned char c = (unsigned char) *p;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            if (needSep && size_String(id_out) > strlen(prefix)) {
                appendChar_String(id_out, '-');
                needSep = iFalse;
            }
            appendChar_String(id_out, (iChar) c);
        }
        else if (c >= 'A' && c <= 'Z') {
            if (needSep && size_String(id_out) > strlen(prefix)) {
                appendChar_String(id_out, '-');
                needSep = iFalse;
            }
            appendChar_String(id_out, (iChar) (c + 32));
        }
        else if (size_String(id_out) > strlen(prefix)) {
            needSep = iTrue;
        }
    }
}

#if defined (LAGRANGE_ENABLE_FONTCONFIG)

static enum iFontStyle fcStyleToFontStyle_(int weight, int slant) {
    const iBool isBold   = (weight >= FC_WEIGHT_BOLD);
    const iBool isItalic = (slant  != FC_SLANT_ROMAN);
    const iBool isLight  = (weight <= FC_WEIGHT_LIGHT);
    const iBool isSemi   = (weight >= FC_WEIGHT_DEMIBOLD && weight < FC_WEIGHT_BOLD);
    if (isBold)   return bold_FontStyle;     /* bold or bold-italic -> bold slot */
    if (isSemi)   return semiBold_FontStyle;
    if (isItalic) return italic_FontStyle;
    if (isLight)  return light_FontStyle;
    return regular_FontStyle;
}

static int fcStyleWeight_(enum iFontStyle style) {
    switch (style) {
        case light_FontStyle:    return FC_WEIGHT_LIGHT;
        case semiBold_FontStyle: return FC_WEIGHT_DEMIBOLD;
        case bold_FontStyle:     return FC_WEIGHT_BOLD;
        default:                 return FC_WEIGHT_REGULAR;
    }
}

static void enumerateFontconfig_(FT_Library lib, iArray *entries_out) {
    FcConfig *config = FcInitLoadConfigAndFonts();
    if (!config) return;
    FcPattern  *pat = FcPatternCreate();
    FcObjectSet *os = FcObjectSetBuild(FC_FAMILY, FC_FILE, FC_INDEX,
                                       FC_WEIGHT, FC_SLANT, FC_SPACING,
                                       FC_FONTFORMAT, NULL);
    FcFontSet   *fs = FcFontList(config, pat, os);
    FcObjectSetDestroy(os);
    FcPatternDestroy(pat);
    if (!fs) {
        FcConfigDestroy(config);
        return;
    }
    /* Distance of each filled style slot from its nominal weight, parallel to entries_out. */
    iArray slotDists;
    init_Array(&slotDists, sizeof(int) * max_FontStyle);
    /* Two passes: non-italic faces first so they take priority in shared style slots.
       Both bold and bold-italic map to bold_FontStyle, so without this ordering a
       bold-italic face returned first by fontconfig would lock out the plain bold face. */
    for (int pass = 0; pass < 2; pass++) {
    if (pass == 1) {
        /* Italic faces cannot replace roman ones. */
        iForEach(Array, j, &slotDists) {
            int *dists = j.value;
            for (int k = 0; k < max_FontStyle; k++) {
                if (dists[k] != INT_MAX) dists[k] = 0;
            }
        }
    }
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
        FcChar8 *formatCStr = NULL;
        FcPatternGetString(font, FC_FONTFORMAT, 0, &formatCStr);
        if (!formatCStr ||
            (iCmpStrN((const char *) formatCStr, "TrueType", 8) != 0 &&
             iCmpStrN((const char *) formatCStr, "CFF",      3) != 0)) {
            continue; /* skip non-OpenType formats (PCF bitmaps, Type 1, etc.) */
        }
        FcPatternGetInteger(font, FC_INDEX,   0, &colIndex);
        FcPatternGetInteger(font, FC_WEIGHT,  0, &weight);
        FcPatternGetInteger(font, FC_SLANT,   0, &slant);
        FcPatternGetInteger(font, FC_SPACING, 0, &spacing);
        if (pass == 0 && slant != FC_SLANT_ROMAN) continue;
        if (pass == 1 && slant == FC_SLANT_ROMAN) continue;
        iFontCacheEntry *entry = NULL;
        int             *dists = NULL;
        /* Find or create entry via linear scan (one-time init, so fast enough). */ {
            iString specId;
            init_String(&specId);
            makeSpecId_(range_CStr((const char *) familyCStr), &specId);
            iForEach(Array, j, entries_out) {
                if (equal_String(&((iFontCacheEntry *) j.value)->id, &specId)) {
                    entry = j.value;
                    dists = at_Array(&slotDists, index_ArrayIterator(&j));
                    break;
                }
            }
            if (!entry) {
                iFontCacheEntry newEntry;
                init_FontCacheEntry(&newEntry);
                set_String(&newEntry.id, &specId);
                setCStr_String(&newEntry.name, (const char *) familyCStr);
                if (spacing == FC_MONO) newEntry.flags |= monospace_FontSpecFlag;
                pushBack_Array(entries_out, &newEntry);
                entry = back_Array(entries_out);
                int newDists[max_FontStyle];
                for (int k = 0; k < max_FontStyle; k++) newDists[k] = INT_MAX;
                pushBack_Array(&slotDists, newDists);
                dists = back_Array(&slotDists);
            }
            deinit_String(&specId);
        }
        /* Prefer the face closest to the nominal weight; weight metadata may be bogus. */
        const enum iFontStyle styleId = fcStyleToFontStyle_(weight, slant);
        const int             dist    = iAbsi(weight - fcStyleWeight_(styleId));
        if (dist >= dists[styleId]) {
            continue;
        }
        dists[styleId] = dist;
        iFontCacheStyle *sty = &entry->styles[styleId];
        setCStr_String(&sty->identifier, (const char *) fileCStr);
        sty->colIndex = colIndex;
    }
    } /* pass */
    iForEach(Array, j, entries_out) {
        iFontCacheEntry *entry = j.value;
        for (int k = 0; k < max_FontStyle; k++) {
            iFontCacheStyle *sty = &entry->styles[k];
            if (!isEmpty_String(&sty->identifier)) {
                extractMetrics_(lib, cstr_String(&sty->identifier), sty->colIndex, sty);
            }
        }
    }
    deinit_Array(&slotDists);
    FcFontSetDestroy(fs);
    FcConfigDestroy(config);
}

#endif /* LAGRANGE_ENABLE_FONTCONFIG */

/*----------------------------------------------------------------------------------------------*/

#if defined (LAGRANGE_ENABLE_DIRECTWRITE)

/* Windows: enumerate fonts via DirectWrite.
   We resolve each IDWriteFont to a file path and extract metrics. */
static void enumerateDirectWrite_(FT_Library lib, iArray *entries_out) {
    IDWriteFactory *factory = NULL;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                   &IID_IDWriteFactory,
                                   (IUnknown **) &factory))) {
        return;
    }
    IDWriteFontCollection *collection = NULL;
    IDWriteFactory_GetSystemFontCollection(factory, &collection, FALSE);
    if (!collection) { IDWriteFactory_Release(factory); return; }

    const UINT32 familyCount = IDWriteFontCollection_GetFontFamilyCount(collection);
    for (UINT32 fi = 0; fi < familyCount; fi++) {
        IDWriteFontFamily *family = NULL;
        if (FAILED(IDWriteFontCollection_GetFontFamily(collection, fi, &family))) continue;

        /* Get family name in the current locale. */
        IDWriteLocalizedStrings *names = NULL;
        IDWriteFontFamily_GetFamilyNames(family, &names);
        if (!names) { IDWriteFontFamily_Release(family); continue; }

        UINT32 nameIdx = 0;
        BOOL   exists  = FALSE;
        wchar_t localeName[LOCALE_NAME_MAX_LENGTH];
        GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH);
        IDWriteLocalizedStrings_FindLocaleName(names, localeName, &nameIdx, &exists);
        if (!exists) nameIdx = 0;
        UINT32 nameLen = 0;
        IDWriteLocalizedStrings_GetStringLength(names, nameIdx, &nameLen);
        wchar_t *wname = (wchar_t *) malloc((nameLen + 1) * sizeof(wchar_t));
        IDWriteLocalizedStrings_GetString(names, nameIdx, wname, nameLen + 1);
        IDWriteLocalizedStrings_Release(names);

        /* Convert to UTF-8 for the spec ID/name. */
        char utf8name[512] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, wname, -1, utf8name, sizeof(utf8name), NULL, NULL);
        free(wname);

        iString specId, dispName;
        init_String(&specId);
        init_String(&dispName);
        setCStr_String(&dispName, utf8name);
        makeSpecId_(range_CStr(utf8name), &specId);

        iFontCacheEntry entry;
        init_FontCacheEntry(&entry);
        set_String(&entry.id, &specId);
        set_String(&entry.name, &dispName);
        deinit_String(&specId);
        deinit_String(&dispName);

        /* Check first font for monospace. */
        const UINT32 fontCount = IDWriteFontFamily_GetFontCount(family);
        for (UINT32 fj = 0; fj < fontCount; fj++) {
            IDWriteFont *font = NULL;
            if (FAILED(IDWriteFontFamily_GetFont(family, fj, &font))) continue;

            const DWRITE_FONT_WEIGHT weight = IDWriteFont_GetWeight(font);
            const DWRITE_FONT_STYLE  style  = IDWriteFont_GetStyle(font);
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
                IDWriteFont_Release(font);
                continue;
            }
            /* Get font file path. */
            IDWriteFontFace *face = NULL;
            if (FAILED(IDWriteFont_CreateFontFace(font, &face))) { IDWriteFont_Release(font); continue; }
            UINT32 fileCount = 0;
            IDWriteFontFace_GetFiles(face, &fileCount, NULL);
            if (fileCount > 0) {
                IDWriteFontFile **files = (IDWriteFontFile **) calloc(fileCount, sizeof(*files));
                IDWriteFontFace_GetFiles(face, &fileCount, files);
                IDWriteFontFile *ffile = files[0];
                IDWriteFontFileLoader *loader = NULL;
                const void *refKey    = NULL;
                UINT32      refKeyLen = 0;
                IDWriteFontFile_GetReferenceKey(ffile, &refKey, &refKeyLen);
                IDWriteFontFile_GetLoader(ffile, &loader);
                IDWriteLocalFontFileLoader *localLoader = NULL;
                if (loader && SUCCEEDED(IDWriteFontFileLoader_QueryInterface(
                                  loader, &IID_IDWriteLocalFontFileLoader, (void **) &localLoader))) {
                    UINT32 pathLen = 0;
                    IDWriteLocalFontFileLoader_GetFilePathLengthFromKey(localLoader, refKey, refKeyLen, &pathLen);
                    wchar_t *wpath = (wchar_t *) malloc((pathLen + 1) * sizeof(wchar_t));
                    IDWriteLocalFontFileLoader_GetFilePathFromKey(localLoader, refKey, refKeyLen, wpath, pathLen + 1);
                    char pathUtf8[1024] = { 0 };
                    WideCharToMultiByte(CP_UTF8, 0, wpath, -1, pathUtf8, sizeof(pathUtf8), NULL, NULL);
                    free(wpath);
                    /* Get face index from the font face. */
                    const UINT32 faceIdx = IDWriteFontFace_GetIndex(face);
                    iFontCacheStyle *sty = &entry.styles[styleId];
                    setCStr_String(&sty->identifier, pathUtf8);
                    sty->colIndex = (int) faceIdx;
                    extractMetrics_(lib, pathUtf8, faceIdx, sty);
                    IDWriteLocalFontFileLoader_Release(localLoader);
                }
                if (loader)  IDWriteFontFileLoader_Release(loader);
                for (UINT32 k = 0; k < fileCount; k++) IDWriteFontFile_Release(files[k]);
                free(files);
            }
            IDWriteFontFace_Release(face);
            IDWriteFont_Release(font);
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
        IDWriteFontFamily_Release(family);
    }
    IDWriteFontCollection_Release(collection);
    IDWriteFactory_Release(factory);
}

#endif /* LAGRANGE_ENABLE_DIRECTWRITE */

/*----------------------------------------------------------------------------------------------*/

#if defined (__ANDROID__)

static uint32_t tag_(const char *s) {
    return ((uint32_t) (uint8_t) s[0] << 24) | ((uint32_t) (uint8_t) s[1] << 16) |
           ((uint32_t) (uint8_t) s[2] << 8) | (uint32_t) (uint8_t) s[3];
}

static void setAxis_(iFontAxis *axes, int *numAxes, uint32_t tag, float value) {
    for (int i = 0; i < *numAxes; i++) {
        if (axes[i].tag == tag) {
            axes[i].value = value;
            return;
        }
    }
    if (*numAxes < maxAxes_FontFile) {
        axes[(*numAxes)++] = (iFontAxis){ tag, value };
    }
}

static iFontCacheEntry *findEntry_(iArray *entries, const iString *id) {
    iForEach(Array, i, entries) {
        iFontCacheEntry *e = i.value;
        if (equal_String(&e->id, id)) {
            return e;
        }
    }
    return NULL;
}

static const struct {
    const char *word;
    int         weight;
} styleWords_[] = {
    { "Thin", 100 },     { "ExtraLight", 200 }, { "UltraLight", 200 }, { "Light", 300 },
    { "Regular", 400 },  { "Book", 400 },       { "VF", 400 },         { "Medium", 500 },
    { "SemiBold", 600 }, { "DemiBold", 600 },   { "Bold", 700 },       { "ExtraBold", 800 },
    { "UltraBold", 800 }, { "Black", 900 },     { "Heavy", 900 },
};

static const int slotWeights_[max_FontStyle] = {
    [regular_FontStyle]  = 400,
    [italic_FontStyle]   = 400,
    [light_FontStyle]    = 300,
    [semiBold_FontStyle] = 600,
    [bold_FontStyle]     = 700,
};

static iRangecc fileStem_(const iString *path) {
    iRangecc stem = baseName_Path(path);
    for (const char *p = stem.end; p > stem.start; p--) {
        if (p[-1] == '.') {
            stem.end = p - 1;
            break;
        }
    }
    return stem;
}

/* Splits a file name like "NotoSerif-BoldItalic.ttf" to the family and style. */
static iRangecc parseFileStyle_(const iString *path, int *weight_out, iBool *isItalic_out) {
    const iRangecc stem = fileStem_(path);
    *weight_out   = 400;
    *isItalic_out = iFalse;
    const char *dash = NULL;
    for (const char *p = stem.start; p < stem.end; p++) {
        if (*p == '-') dash = p;
    }
    if (!dash || dash == stem.start) {
        return stem;
    }
    iRangecc suffix = { dash + 1, stem.end };
    iBool isItalic = iFalse;
    if (endsWith_Rangecc(suffix, "Italic")) {
        isItalic = iTrue;
        suffix.end -= 6;
    }
    int weight = isEmpty_Range(&suffix) && isItalic ? 400 : 0;
    iForIndices(i, styleWords_) {
        if (equal_Rangecc(suffix, styleWords_[i].word)) {
            weight = styleWords_[i].weight;
            break;
        }
    }
    if (!weight) {
        return stem; /* not a style suffix */
    }
    *weight_out   = weight;
    *isItalic_out = isItalic;
    return (iRangecc){ stem.start, dash };
}

static int styleSlot_(int weight, iBool isItalic) {
    if (isItalic) {
        return weight == 400 ? italic_FontStyle : -1; /* no slot for other italics */
    }
    return weight < 400 ? light_FontStyle : weight == 400 ? regular_FontStyle
         : weight < 700 ? semiBold_FontStyle : bold_FontStyle;
}

static int slotDistance_(const iString *path, int slot) {
    int   weight;
    iBool isItalic;
    parseFileStyle_(path, &weight, &isItalic);
    return iAbs(weight - slotWeights_[slot]);
}

static iFontCacheEntry *findOrAddEntry_(iArray *entries, iRangecc name) {
    iString id;
    init_String(&id);
    makeSpecId_(name, &id);
    iFontCacheEntry *entry = findEntry_(entries, &id);
    if (!entry) {
        iFontCacheEntry newEntry;
        init_FontCacheEntry(&newEntry);
        set_String(&newEntry.id, &id);
        setRange_String(&newEntry.name, name);
        pushBack_Array(entries, &newEntry);
        entry = back_Array(entries);
    }
    deinit_String(&id);
    return entry;
}

static void copyStyle_(iFontCacheStyle *dst, const iFontCacheStyle *src) {
    set_String(&dst->identifier, &src->identifier);
    dst->colIndex   = src->colIndex;
    dst->ascent     = src->ascent;
    dst->descent    = src->descent;
    dst->lineGap    = src->lineGap;
    dst->winAscent  = src->winAscent;
    dst->winDescent = src->winDescent;
    dst->emAdvance  = src->emAdvance;
    dst->unitsPerEm = src->unitsPerEm;
    dst->numAxes    = src->numAxes;
    memcpy(dst->axes, src->axes, sizeof(dst->axes));
}

static iBool hasAxis_(const FT_MM_Var *mm, const char *tag) {
    for (FT_UInt i = 0; i < mm->num_axis; i++) {
        if (mm->axis[i].tag == tag_(tag)) {
            return iTrue;
        }
    }
    return iFalse;
}

/* Styles copied from a variable regular font are instanced to their weights. */
static void instanceVariableStyles_(FT_Library lib, iFontCacheEntry *e, const iBool *isCopied) {
    const iFontCacheStyle *regular = &e->styles[regular_FontStyle];
    FT_Face    face = NULL;
    FT_MM_Var *mm   = NULL;
    if (FT_New_Face(lib, cstr_String(&regular->identifier), regular->colIndex, &face) == 0) {
        if (FT_HAS_MULTIPLE_MASTERS(face) && FT_Get_MM_Var(face, &mm) == 0) {
            const iBool hasWght = hasAxis_(mm, "wght");
            const iBool hasItal = hasAxis_(mm, "ital");
            for (int s = 0; s < max_FontStyle && hasWght; s++) {
                if (s != regular_FontStyle && !isCopied[s]) continue;
                iFontCacheStyle *sty = &e->styles[s];
                setAxis_(sty->axes, &sty->numAxes, tag_("wght"), slotWeights_[s]);
                if (hasItal) {
                    setAxis_(sty->axes, &sty->numAxes, tag_("ital"),
                             s == italic_FontStyle ? 1.0f : 0.0f);
                }
            }
            FT_Done_MM_Var(lib, mm);
        }
        FT_Done_Face(face);
    }
}

static void enumerateAndroid_(FT_Library lib, iArray *entries_out) {
    const char *androidFontDirs[] = { "/system/fonts", "/system/product/fonts", NULL };
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
            int            weight;
            iBool          isItalic;
            const iRangecc family = parseFileStyle_(path, &weight, &isItalic);
            const int      slot   = styleSlot_(weight, isItalic);
            if (slot < 0) continue;
            iFontCacheEntry *entry = findOrAddEntry_(entries_out, family);
            iFontCacheStyle *sty   = &entry->styles[slot];
            if (!isEmpty_String(&sty->identifier)) {
                if (slot == regular_FontStyle) {
                    /* Every regular file stays available, e.g., for glyph fallback. */
                    entry = findOrAddEntry_(entries_out, fileStem_(path));
                    sty   = &entry->styles[slot];
                    if (!isEmpty_String(&sty->identifier)) continue;
                }
                else if (iAbs(weight - slotWeights_[slot]) >= slotDistance_(&sty->identifier, slot)) {
                    continue;
                }
            }
            set_String(&sty->identifier, path);
            sty->colIndex = 0;
            extractMetrics_(lib, cstr_String(path), 0, sty);
        }
        iRelease(contents);
        iRelease(fi);
        delete_String(dir);
    }
    iForEach(Array, i, entries_out) {
        iFontCacheEntry *e = i.value;
        iFontCacheStyle *regular = &e->styles[regular_FontStyle];
        /* A family without a regular style uses its closest one instead. */
        static const int substitutes[] = {
            semiBold_FontStyle, light_FontStyle, bold_FontStyle, italic_FontStyle
        };
        iForIndices(k, substitutes) {
            if (!isEmpty_String(&regular->identifier)) break;
            copyStyle_(regular, &e->styles[substitutes[k]]);
        }
        if (isEmpty_String(&regular->identifier)) continue;
        iBool isCopied[max_FontStyle] = { iFalse };
        for (int s = 0; s < max_FontStyle; s++) {
            if (isEmpty_String(&e->styles[s].identifier)) {
                copyStyle_(&e->styles[s], regular);
                isCopied[s] = iTrue;
            }
        }
        instanceVariableStyles_(lib, e, isCopied);
    }
}

static iBool hasAttr_(iRangecc attrs, const char *attr) {
    return iStrStrN(attrs.start, attr, size_Range(&attrs)) != NULL;
}

static iRangecc attrValue_(iRangecc attrs, const char *name) {
    const size_t nameLen = strlen(name);
    for (const char *pos = attrs.start; pos + nameLen + 2 <= attrs.end; pos++) {
        if ((pos == attrs.start || isspace((unsigned char) pos[-1])) &&
            !iCmpStrN(pos, name, nameLen) && pos[nameLen] == '=' && pos[nameLen + 1] == '"') {
            const char *start = pos + nameLen + 2;
            const char *end   = memchr(start, '"', attrs.end - start);
            if (end) {
                return (iRangecc){ start, end };
            }
        }
    }
    return iNullRange;
}

static iString *readFontsXml_(void) {
    iFile *f = newCStr_File("/system/etc/fonts.xml");
    if (!open_File(f, readOnly_FileMode)) {
        iRelease(f);
        return NULL;
    }
    iBlock *xml = readAll_File(f);
    iRelease(f);
    /* Comments may contain markup. */
    iString *src = new_String();
    const char *pos = constData_Block(xml);
    for (const char *open; (open = strstr(pos, "<!--")) != NULL; ) {
        appendRange_String(src, (iRangecc){ pos, open });
        const char *close = strstr(open + 4, "-->");
        pos = close ? close + 3 : open + strlen(open);
    }
    appendCStr_String(src, pos);
    delete_Block(xml);
    return src;
}

/* Font files of the unnamed fallback families in fonts.xml, in the system's order of
   preference. Emoji families are listed separately. */
static void androidFallbackPaths_(iStringList *paths_out, iStringList *emojiPaths_out) {
    iString *src = readFontsXml_();
    if (!src) {
        return;
    }
    iRegExp *familyPattern = new_RegExp("(?s)<family\\b([^>]*)>(.*?)</family>", 0);
    iRegExp *fontPattern   = new_RegExp("<font\\b([^>]*)>\\s*([^<\\s]+)", 0);
    iRegExpMatch m;
    init_RegExpMatch(&m);
    while (matchString_RegExp(familyPattern, src, &m)) {
        const iRangecc attrs = capturedRange_RegExpMatch(&m, 1);
        /* Named families are the generic ones (sans-serif, serif, etc.). The "compact"
           variants are the defaults, like in Android's own text rendering. */
        if (hasAttr_(attrs, "name=") || hasAttr_(attrs, "ignore=\"true\"") ||
            hasAttr_(attrs, "variant=\"elegant\"")) {
            continue;
        }
        iStringList *list = hasAttr_(attrs, "und-Zsye") ? emojiPaths_out : paths_out;
        iRangecc file = iNullRange;
        iRegExpMatch fm;
        init_RegExpMatch(&fm);
        while (matchRange_RegExp(fontPattern, capturedRange_RegExpMatch(&m, 2), &fm)) {
            const iRangecc fontAttrs = capturedRange_RegExpMatch(&fm, 1);
            const iBool    isRegular = hasAttr_(fontAttrs, "weight=\"400\"") &&
                                       hasAttr_(fontAttrs, "style=\"normal\"");
            if (isRegular || isEmpty_Range(&file)) {
                file = capturedRange_RegExpMatch(&fm, 2);
            }
            if (isRegular) break;
        }
        if (isEmpty_Range(&file)) continue;
        iString *path = newCStr_String("/system/fonts/");
        appendRange_String(path, file);
        iBool isListed = iFalse;
        iConstForEach(StringList, i, list) {
            if (equal_String(i.value, path)) {
                isListed = iTrue;
                break;
            }
        }
        if (!isListed) {
            pushBack_StringList(list, path);
        }
        delete_String(path);
    }
    iRelease(fontPattern);
    iRelease(familyPattern);
    delete_String(src);
}

iDeclareType(AndroidFont)

struct Impl_AndroidFont {
    int       weight;
    iBool     isItalic;
    char      file[128];
    int       colIndex;
    int       numAxes;
    iFontAxis axes[maxAxes_FontFile];
};

/* Fonts of the "sans-serif" family in fonts.xml. */
static size_t sansSerifFonts_(iAndroidFont *fonts, size_t maxFonts) {
    iString *src = readFontsXml_();
    if (!src) {
        return 0;
    }
    size_t   n             = 0;
    iRegExp *familyPattern = new_RegExp("(?s)<family\\b([^>]*)>(.*?)</family>", 0);
    iRegExp *fontPattern   = new_RegExp("(?s)<font\\b([^>]*)>\\s*([^<\\s]+)(.*?)</font>", 0);
    iRegExp *axisPattern   = new_RegExp("<axis\\b([^>]*)>", 0);
    iRegExpMatch m;
    init_RegExpMatch(&m);
    while (n == 0 && matchString_RegExp(familyPattern, src, &m)) {
        if (!equal_Rangecc(attrValue_(capturedRange_RegExpMatch(&m, 1), "name"), "sans-serif")) {
            continue;
        }
        iRegExpMatch fm;
        init_RegExpMatch(&fm);
        while (n < maxFonts && matchRange_RegExp(fontPattern, capturedRange_RegExpMatch(&m, 2), &fm)) {
            const iRangecc attrs  = capturedRange_RegExpMatch(&fm, 1);
            const iRangecc file   = capturedRange_RegExpMatch(&fm, 2);
            const iRangecc weight = attrValue_(attrs, "weight");
            const iRangecc index  = attrValue_(attrs, "index");
            if (size_Range(&file) >= sizeof(fonts[n].file)) {
                continue;
            }
            iAndroidFont *font = &fonts[n++];
            iZap(*font);
            memcpy(font->file, file.start, size_Range(&file));
            font->weight   = isEmpty_Range(&weight) ? 400 : atoi(weight.start);
            font->isItalic = equal_Rangecc(attrValue_(attrs, "style"), "italic");
            font->colIndex = isEmpty_Range(&index) ? 0 : atoi(index.start);
            iRegExpMatch am;
            init_RegExpMatch(&am);
            while (matchRange_RegExp(axisPattern, capturedRange_RegExpMatch(&fm, 3), &am)) {
                const iRangecc axisAttrs = capturedRange_RegExpMatch(&am, 1);
                const iRangecc tag       = attrValue_(axisAttrs, "tag");
                const iRangecc value     = attrValue_(axisAttrs, "stylevalue");
                if (size_Range(&tag) == 4 && !isEmpty_Range(&value)) {
                    setAxis_(font->axes, &font->numAxes, tag_(tag.start), strtof(value.start, NULL));
                }
            }
        }
    }
    iRelease(axisPattern);
    iRelease(fontPattern);
    iRelease(familyPattern);
    delete_String(src);
    return n;
}

iFontSpec *newDefaultSpec_FtFontCache(void) {
    /* Same weights as the bundled Roboto files. */
    static const struct {
        int   weight;
        iBool isItalic;
    } styles_[max_FontStyle] = {
        [regular_FontStyle]  = { 400, iFalse },
        [italic_FontStyle]   = { 400, iTrue },
        [light_FontStyle]    = { 100, iFalse },
        [semiBold_FontStyle] = { 700, iFalse },
        [bold_FontStyle]     = { 900, iFalse },
    };
    iAndroidFont fonts[32];
    size_t       numFonts = sansSerifFonts_(fonts, iElemCount(fonts));
    if (numFonts == 0) {
        iZap(fonts[0]);
        fonts[0].weight = 400;
        strcpy(fonts[0].file, "Roboto-Regular.ttf");
        numFonts = 1;
    }
    iFontSpec *spec = new_FontSpec();
    setCStr_String(&spec->id, "default");
    setCStr_String(&spec->name, "Roboto");
    for (int s = 0; s < max_FontStyle; s++) {
        const iAndroidFont *best      = NULL;
        int                 bestScore = INT_MAX;
        for (size_t i = 0; i < numFonts; i++) {
            const int score = (fonts[i].isItalic != styles_[s].isItalic ? 10000 : 0) +
                              iAbs(fonts[i].weight - styles_[s].weight);
            if (score < bestScore) {
                best      = &fonts[i];
                bestScore = score;
            }
        }
        iFontFile *ff = new_FontFile();
        format_String(&ff->id, "/system/fonts/%s", best->file);
        ff->colIndex = best->colIndex;
        ff->style    = s;
        ff->numAxes  = best->numAxes;
        memcpy(ff->axes, best->axes, sizeof(ff->axes));
        /* Variable fonts are instanced to the style. Ignored by static fonts. */
        setAxis_(ff->axes, &ff->numAxes, tag_("wght"), styles_[s].weight);
        setAxis_(ff->axes, &ff->numAxes, tag_("ital"), styles_[s].isItalic ? 1.0f : 0.0f);
        allocData_FontFile(ff);
        if (!ff->data) {
            iRelease(ff);
            continue;
        }
        spec->styles[s] = ff;
    }
    if (!spec->styles[regular_FontStyle]) {
        delete_FontSpec(spec);
        return NULL;
    }
    for (int s = 0; s < max_FontStyle; s++) {
        if (!spec->styles[s]) {
            spec->styles[s] = ref_Object(spec->styles[regular_FontStyle]);
        }
    }
    return spec;
}

#endif /* __ANDROID__ */

/* Uses a private FreeType library because this may run in a background thread. */
static void enumerate_(iArray *entries_out) {
    FT_Library lib = NULL;
    if (FT_Init_FreeType(&lib) != 0) {
        return;
    }
#if defined (LAGRANGE_ENABLE_FONTCONFIG)
    enumerateFontconfig_(lib, entries_out);
#elif defined (LAGRANGE_ENABLE_DIRECTWRITE)
    enumerateDirectWrite_(lib, entries_out);
#elif defined (__ANDROID__)
    enumerateAndroid_(lib, entries_out);
#endif
    FT_Done_FreeType(lib);
}

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
    enumerate_(&entries);
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

void enumerateSystemFonts_FontPack_(iFontPack *pack) {
    iArray entries;
    init_Array(&entries, sizeof(iFontCacheEntry));
    enumerate_(&entries);
    writeCache_(collect_String(cacheFilePath_()), &entries);
    /* Populate the font pack and clean up. */
    iForEach(Array, i, &entries) {
        addEntryToFontPack_(i.value, pack);
        deinit_FontCacheEntry(i.value);
    }
    deinit_Array(&entries);
}

iBool loadCachedSystemFonts_FontPack_(iFontPack *pack) {
    iString *cachePath = collect_String(cacheFilePath_());
    const iBool ok = readCache_(cachePath, pack);
    if (!ok) {
        /* First run or stale cache: enumerate synchronously. */
        enumerateSystemFonts_FontPack_(pack);
    }
    /* Start background thread to check if dirs have changed since last enumeration. */
    startWorker_(cachePath);
    return ok;
}

void stopFontWorker_FtFontCache(void) {
    if (worker_) {
        delete_FtFontWorker(worker_);
        worker_ = NULL;
    }
}

#if defined (__ANDROID__)
static const iFontSpec *findSpecByPath_(const iString *path) {
    iConstForEach(PtrArray, i, listSpecsByPriority_Fonts()) {
        const iFontSpec *spec = i.ptr;
        for (int s = 0; s < max_FontStyle; s++) {
            if (spec->styles[s] && equal_String(&spec->styles[s]->id, path)) {
                return spec;
            }
        }
    }
    return NULL;
}

static const iStringList *androidPaths_(iBool emoji) {
    static iStringList *paths_[2]; /* fonts.xml does not change while running */
    if (!paths_[0]) {
        paths_[0] = new_StringList();
        paths_[1] = new_StringList();
        androidFallbackPaths_(paths_[0], paths_[1]);
    }
    return paths_[emoji ? 1 : 0];
}

static void appendSpecs_(const iStringList *paths, iPtrArray *specs_out) {
    iConstForEach(StringList, i, paths) {
        const iFontSpec *spec = findSpecByPath_(i.value);
        if (spec && indexOf_PtrArray(specs_out, spec) == iInvalidPos) {
            pushBack_PtrArray(specs_out, spec);
        }
    }
}
#endif

void systemFallbackSpecs_FtFontCache(iPtrArray *specs_out) {
#if defined (__ANDROID__)
    appendSpecs_(androidPaths_(iFalse), specs_out);
#else
    iUnused(specs_out);
#endif
}

void systemEmojiSpecs_FtFontCache(iPtrArray *specs_out) {
#if defined (__ANDROID__)
    appendSpecs_(androidPaths_(iTrue), specs_out);
#else
    iUnused(specs_out);
#endif
}
