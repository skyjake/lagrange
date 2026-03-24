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

#include "apple_text.h"
#include "app.h"

#include <the_Foundation/thread.h>
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/path.h>

/*----------------------------------------------------------------------------------------------*/
/* Cache for enumerated system fonts. Enables quick initialization of the backend by skipping
   the potentially expensive Core Text enumeration. A background worker validates and refreshes
   the enumerated fonts. */

static const uint32_t magic_FontCacheFile_   = 0x6C674643u; /* "lgFC" */
static const uint32_t version_FontCacheFile_ = 3u;

iDeclareType(FontCacheEntry)

struct Impl_FontCacheEntry {
    iString  id;
    iString  name;
    uint32_t flags;
    iString  psNames  [max_FontStyle]; /* PostScript name; if same as [0] = no distinct variant */
    int32_t  ascent   [max_FontStyle];
    int32_t  descent  [max_FontStyle];
    int32_t  lineGap  [max_FontStyle];
    int32_t  emAdvance [max_FontStyle];
    int32_t  unitsPerEm[max_FontStyle];
};

static void init_FontCacheEntry(iFontCacheEntry *d) {
    iZap(*d);
    init_String(&d->id);
    init_String(&d->name);
    iForIndices(i, d->psNames) {
        init_String(&d->psNames[i]);
    }
}

static void deinit_FontCacheEntry(iFontCacheEntry *d) {
    iForIndices(i, d->psNames) {
        deinit_String(&d->psNames[i]);
    }
    deinit_String(&d->name);
    deinit_String(&d->id);
}

static void serialize_FontCacheEntry(const iFontCacheEntry *d, iStream *outs) {
    serialize_String(&d->id, outs);
    serialize_String(&d->name, outs);
    writeU32_Stream(outs, d->flags);
    iForIndices(i, d->psNames) {
        serialize_String(&d->psNames[i], outs);
        write32_Stream(outs, d->ascent[i]);
        write32_Stream(outs, d->descent[i]);
        write32_Stream(outs, d->lineGap[i]);
        write32_Stream(outs, d->emAdvance[i]);
        write32_Stream(outs, d->unitsPerEm[i]);
    }
}

static void deserialize_FontCacheEntry(iFontCacheEntry *d, iStream *ins) {
    deserialize_String(&d->id, ins);
    deserialize_String(&d->name, ins);
    d->flags = readU32_Stream(ins);
    iForIndices(i, d->psNames) {
        deserialize_String(&d->psNames[i], ins);
        d->ascent[i]    = read32_Stream(ins);
        d->descent[i]   = read32_Stream(ins);
        d->lineGap[i]   = read32_Stream(ins);
        d->emAdvance[i]  = read32_Stream(ins);
        d->unitsPerEm[i] = read32_Stream(ins);
    }
}

static void fontDirModTime_(const iString *path, iDate *date_out) {
    iZap(*date_out);
    if (isEmpty_String(path)) return;
    iFileInfo *info = new_FileInfo(path);
    if (isDirectory_FileInfo(info)) {
        iTime t = lastModified_FileInfo(info);
        init_Date(date_out, &t);
    }
    iRelease(info);
}

static void currentFontDirModTimes_(iDate *libFonts_out, iDate *userLibFonts_out) {
    iString *path = concatCStr_Path(collect_String(home_Path()), "Library/Fonts");
    fontDirModTime_(path, userLibFonts_out);
    setCStr_String(path, "/Library/Fonts");
    fontDirModTime_(path, libFonts_out);
    delete_String(path);
}

static float glyphScaleFromMetrics_(const iFontFile *regularFace) {
    if (!regularFace) return defaultSystemGlyphScale_AppleText;
    const int totalEm  = regularFace->ascent - regularFace->descent; /* descent is negative */
    const int naturalH = totalEm + iMax(0, regularFace->lineGap);
    return (totalEm > 0 && naturalH > 0) ? (float) totalEm / (float) naturalH
                                         : defaultSystemGlyphScale_AppleText;
}

iBool tryLoadCached_FontPack_(iFontPack *pack, const iString *cachePath) {
    iFile *f = new_File(cachePath);
    iBool ok = iFalse;
    if (open_File(f, readOnly_FileMode)) {
        iStream *ins = stream_File(f);
        const uint32_t magic   = readU32_Stream(ins);
        const uint32_t version = readU32_Stream(ins);
        const uint32_t ctVer   = readU32_Stream(ins);
        if (magic == magic_FontCacheFile_ && version == version_FontCacheFile_ &&
            ctVer == CTGetCoreTextVersion()) {
            /* Skip the stored mtimes; used only by the background worker. */
            iDate tmp;
            deserialize_Date(&tmp, ins); /* /Library/Fonts mtime */
            deserialize_Date(&tmp, ins); /* ~/Library/Fonts mtime */
            const uint32_t count = readU32_Stream(ins);
            ok = iTrue;
            iFontCacheEntry entry;
            init_FontCacheEntry(&entry);
            for (uint32_t i = 0; i < count && !atEnd_Stream(ins); i++) {
                deserialize_FontCacheEntry(&entry, ins);
                /* Build iFontSpec + iFontFile stubs; CTFontRef remains NULL until first use. */
                iFontSpec *spec = new_FontSpec();
                set_String(&spec->id, &entry.id);
                set_String(&spec->name, &entry.name);
                spec->flags    = (int) entry.flags;
                spec->priority = 1;
                iFontFile *regularFile = NULL;
                for (int s = 0; s < max_FontStyle; s++) {
                    iFontFile *ff;
                    if (s != regular_FontStyle && regularFile &&
                        equal_String(&entry.psNames[s], &entry.psNames[regular_FontStyle])) {
                        ff = ref_Object(regularFile); /* no distinct variant; share regular */
                    }
                    else {
                        ff = new_FontFile();
                        set_String(&ff->id, &entry.psNames[s]);
                        ff->ascent     = entry.ascent[s];
                        ff->descent    = entry.descent[s];
                        ff->lineGap    = entry.lineGap[s];
                        ff->emAdvance  = entry.emAdvance[s];
                        ff->unitsPerEm = entry.unitsPerEm[s];
                        /* ff->data remains NULL; created lazily in ensureCtFont_AppleFont_ */
                    }
                    spec->styles[s] = ff;
                    if (s == regular_FontStyle) {
                        regularFile = ff;
                    }
                }
                const float gs      = glyphScaleFromMetrics_(regularFile);
                spec->glyphScale[0] = gs;
                spec->glyphScale[1] = gs;
                iAssert(gs <= 1.0f);
                addSpec_FontPack(pack, spec);
            }
            deinit_FontCacheEntry(&entry);
        }
        close_File(f);
    }
    iRelease(f);
    return ok && !isEmpty_PtrArray(listSpecs_FontPack(pack));
}

void saveCached_FontPack_(const iFontPack *pack, const iString *cachePath) {
    iFile *f = new_File(cachePath);
    if (open_File(f, writeOnly_FileMode)) {
        iStream *outs = stream_File(f);
        writeU32_Stream(outs, magic_FontCacheFile_);
        writeU32_Stream(outs, version_FontCacheFile_);
        writeU32_Stream(outs, CTGetCoreTextVersion());
        /* Snapshot the current font directory mtimes for later comparison. */
        iDate libModTime, userLibModTime;
        currentFontDirModTimes_(&libModTime, &userLibModTime);
        serialize_Date(&libModTime, outs);
        serialize_Date(&userLibModTime, outs);
        /* Write one entry per spec. */
        const iPtrArray *specs = listSpecs_FontPack(pack);
        writeU32_Stream(outs, (uint32_t) size_PtrArray(specs));
        iFontCacheEntry entry;
        init_FontCacheEntry(&entry);
        iConstForEach(PtrArray, i, specs) {
            const iFontSpec *spec = i.ptr;
            set_String(&entry.id, &spec->id);
            set_String(&entry.name, &spec->name);
            entry.flags = (uint32_t) spec->flags;
            for (int s = 0; s < max_FontStyle; s++) {
                const iFontFile *ff = spec->styles[s];
                if (ff) {
                    set_String(&entry.psNames[s], &ff->id);
                    entry.ascent[s]     = (int32_t) ff->ascent;
                    entry.descent[s]    = (int32_t) ff->descent;
                    entry.lineGap[s]    = (int32_t) ff->lineGap;
                    entry.emAdvance[s]  = (int32_t) ff->emAdvance;
                    entry.unitsPerEm[s] = (int32_t) ff->unitsPerEm;
                }
            }
            serialize_FontCacheEntry(&entry, outs);
        }
        deinit_FontCacheEntry(&entry);
        close_File(f);
    }
    iRelease(f);
}

/*- System font enumeration --------------------------------------------------------------------*/

static void setCFStringRef_String_(iString *d, CFStringRef src) {
    if (!src) {
        clear_String(d);
        return;
    }
    /* Determine the maximum required buffer size for a UTF-8 conversion. */
    const CFIndex len =
        CFStringGetMaximumSizeForEncoding(CFStringGetLength(src), kCFStringEncodingUTF8);
    resize_Block(&d->chars, len);
    if (CFStringGetCString(
            src, data_Block(&d->chars), size_Block(&d->chars) + 1, kCFStringEncodingUTF8)) {
        /* Trim down to the actual size. */
        resize_Block(&d->chars, strlen(cstr_String(d)));
    }
    else {
        clear_String(d);
    }
}

static iFontFile *namedFontFile_(CTFontRef font) {
    /* Create a named FontFile from a CTFont; id = PostScript name, no sourceData.
       Returns NULL if the font is inaccessible or has no metrics. */
    if (!font) return NULL;
    CFStringRef ps = CTFontCopyName(font, kCTFontPostScriptNameKey);
    if (!ps) return NULL;
    char        buf[256]; /* PostScript names are pretty short */
    const iBool ok = CFStringGetCString(ps, buf, sizeof(buf), kCFStringEncodingUTF8);
    CFRelease(ps);
    if (!ok || buf[0] == '\0') return NULL;
    iFontFile *ff = new_FontFile();
    setCStr_String(&ff->id, buf);
    allocData_FontFile(ff); /* fills ascent/descent/emAdvance from named lookup */
    if (!ff->data) {
        iRelease(ff);
        return NULL;
    }
    return ff;
}

static CTFontRef styleVariant_(CTFontRef base, CTFontSymbolicTraits traits) {
    /* Return a CTFont with the requested symbolic traits, or NULL if the family has no such
       variant (or the result is the same font as `base`). Caller must CFRelease result. */
    CTFontRef var = CTFontCreateCopyWithSymbolicTraits(base, 0.0, NULL, traits, traits);
    if (!var) return NULL;
    CFStringRef vn   = CTFontCopyName(var, kCTFontPostScriptNameKey);
    CFStringRef bn   = CTFontCopyName(base, kCTFontPostScriptNameKey);
    const iBool same = (vn && bn && CFStringCompare(vn, bn, 0) == kCFCompareEqualTo);
    if (vn) CFRelease(vn);
    if (bn) CFRelease(bn);
    if (same) {
        CFRelease(var);
        return NULL;
    }
    return var;
}

static CTFontRef weightVariant_(CFStringRef familyName, CTFontRef base, CGFloat targetWeight) {
    /* Enumerate all upright non-bold members of the family and return the one whose
       weight is closest to targetWeight, as long as it is distinct from base.
       This avoids accidentally picking italic or bold-condensed variants that happen
       to carry the requested weight. Caller must CFRelease result. */
    CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attrs, kCTFontFamilyNameAttribute, familyName);
    CTFontDescriptorRef familyDesc = CTFontDescriptorCreateWithAttributes(attrs);
    CFRelease(attrs);
    /* NULL mandatory set: all attributes in the descriptor are mandatory (family name only). */
    CFArrayRef allDescs = CTFontDescriptorCreateMatchingFontDescriptors(familyDesc, NULL);
    CFRelease(familyDesc);
    if (!allDescs) {
        return NULL;
    }
    CFStringRef         basePSName = CTFontCopyName(base, kCTFontPostScriptNameKey);
    CTFontDescriptorRef bestDesc   = NULL;
    CGFloat             bestDiff   = 1000.0;
    for (CFIndex i = 0; i < CFArrayGetCount(allDescs); i++) {
        CTFontDescriptorRef desc = (CTFontDescriptorRef) CFArrayGetValueAtIndex(allDescs, i);
        CFDictionaryRef     traits =
            (CFDictionaryRef) CTFontDescriptorCopyAttribute(desc, kCTFontTraitsAttribute);
        if (!traits) {
            continue;
        }
        /* Skip italic and bold variants. We want a clean upright light face. */
        CFNumberRef symNum = (CFNumberRef) CFDictionaryGetValue(traits, kCTFontSymbolicTrait);
        CTFontSymbolicTraits symTraits = 0;
        if (symNum) CFNumberGetValue(symNum, kCFNumberSInt32Type, &symTraits);
        CGFloat     weight = 0.0;
        CFNumberRef wNum   = (CFNumberRef) CFDictionaryGetValue(traits, kCTFontWeightTrait);
        if (wNum) CFNumberGetValue(wNum, kCFNumberCGFloatType, &weight);
        CFRelease(traits);
        if (symTraits & (kCTFontTraitItalic | kCTFontTraitBold | kCTFontTraitCondensed)) {
            continue;
        }        /* Must be distinct from base. */
        CFStringRef psName =
            (CFStringRef) CTFontDescriptorCopyAttribute(desc, kCTFontNameAttribute);
        const iBool isSame =
            (psName && basePSName && CFStringCompare(psName, basePSName, 0) == kCFCompareEqualTo);
        if (psName) {
            CFRelease(psName);
        }
        if (isSame) {
            continue;
        }
        const CGFloat diff = fabs(weight - targetWeight);
        if (diff < bestDiff) {
            bestDiff = diff;
            bestDesc = desc;
        }
    }

    CTFontRef result = bestDesc ? CTFontCreateWithFontDescriptor(bestDesc, 12.0, NULL) : NULL;
    if (basePSName) CFRelease(basePSName);
    CFRelease(allDescs);
    return result;
}

void enumerateSystemFonts_FontPack_(iFontPack *pack) {
    CFArrayRef families = CTFontManagerCopyAvailableFontFamilyNames();
    if (!families) return;
    const CFIndex n = CFArrayGetCount(families);
    iString       id;
    iString       familyName;
    init_String(&id);
    init_String(&familyName);
    for (CFIndex i = 0; i < n; i++) {
        CFStringRef family = CFArrayGetValueAtIndex(families, i);
        /* Skip private/internal fonts whose names begin with '.'. */
        if (CFStringGetLength(family) == 0 || CFStringGetCharacterAtIndex(family, 0) == '.') {
            continue;
        }
        /* Get the family name as an iString (handles any length, full UTF-8). */
        setCFStringRef_String_(&familyName, family);
        if (isEmpty_String(&familyName)) {
            continue;
        }
        /* Build the apple-* spec ID: lowercase ASCII alphanumeric, non-alphanum -> hyphen.
           The needSep flag defers hyphens so no trailing separator is possible. */
        setCStr_String(&id, "apple-");
        iBool needSep = iFalse;
        iConstForEach(String, it, &familyName) {
            const iChar ch = it.value;
            if (isAlphaNumeric_Char(ch)) {
                if (needSep) {
                    appendChar_String(&id, '-');
                    needSep = iFalse;
                }
                appendChar_String(&id, lower_Char(ch));
            }
            else if (size_String(&id) > 6) {
                needSep = iTrue;
            }
        }
        /* Create the base (regular) CTFont for this family. */
        CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                                                 1,
                                                                 &kCFTypeDictionaryKeyCallBacks,
                                                                 &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(attrs, kCTFontFamilyNameAttribute, family);
        CTFontDescriptorRef baseDesc = CTFontDescriptorCreateWithAttributes(attrs);
        CFRelease(attrs);
        CTFontRef baseFont = CTFontCreateWithFontDescriptor(baseDesc, 12.0, NULL);
        CFRelease(baseDesc);
        if (!baseFont) {
            continue;
        }
        /* Find style variants: bold, italic, light (~-0.4), semibold (~0.3). */
        CTFontRef boldFont     = styleVariant_(baseFont, kCTFontTraitBold);
        CTFontRef italicFont   = styleVariant_(baseFont, kCTFontTraitItalic);
        CTFontRef lightFont    = weightVariant_(family, baseFont, -0.4); /* light */
        CTFontRef semiboldFont = weightVariant_(family, baseFont, 0.3);  /* semibold */
        /* Create iFontFile entries (PostScript-name based). */
        iFontFile *files[max_FontStyle];
        files[regular_FontStyle] = namedFontFile_(baseFont);
        if (!files[regular_FontStyle]) {
            /* Font is inaccessible; skip this family. */
            if (boldFont) CFRelease(boldFont);
            if (italicFont) CFRelease(italicFont);
            if (lightFont) CFRelease(lightFont);
            if (semiboldFont) CFRelease(semiboldFont);
            CFRelease(baseFont);
            continue;
        }
        files[bold_FontStyle]     = namedFontFile_(boldFont);
        files[italic_FontStyle]   = namedFontFile_(italicFont);
        files[light_FontStyle]    = namedFontFile_(lightFont);
        files[semiBold_FontStyle] = namedFontFile_(semiboldFont);
        /* Fill any missing styles with a reference to the regular file. */
        for (int s = 0; s < max_FontStyle; s++) {
            if (!files[s]) {
                files[s] = ref_Object(files[regular_FontStyle]);
            }
        }
        /* Build the iFontSpec. */
        iBool      isMono = (CTFontGetSymbolicTraits(baseFont) & kCTFontTraitMonoSpace) != 0;
        iFontSpec *spec   = new_FontSpec();
        set_String(&spec->id, &id);
        set_String(&spec->name, &familyName);
        spec->priority = 1;
        spec->flags   |= ignoreAsFallback_FontSpecFlag; /* excluded from cascade; selected explicitly */
        /* Compute glyphScale from the regular face's line spacing metrics so that each font
           is rendered at the correct proportional size within the typesetter's line box. */ {
            const float gs      = glyphScaleFromMetrics_(files[regular_FontStyle]);
            spec->glyphScale[0] = gs;
            spec->glyphScale[1] = gs;
            iAssert(gs <= 1.0f);
        }
        if (isMono) spec->flags |= monospace_FontSpecFlag;
        for (int s = 0; s < max_FontStyle; s++) {
            spec->styles[s] = files[s]; /* iFontSpec takes ownership of each ref */
        }
        addSpec_FontPack(pack, spec);
        /* Release CTFont variant refs (iFontFile already holds its own via data). */
        if (boldFont) CFRelease(boldFont);
        if (italicFont) CFRelease(italicFont);
        if (lightFont) CFRelease(lightFont);
        if (semiboldFont) CFRelease(semiboldFont);
        CFRelease(baseFont);
    }
    deinit_String(&familyName);
    deinit_String(&id);
    CFRelease(families);
}

/*----------------------------------------------------------------------------------------------*/

iDeclareType(FontWorker)

struct Impl_FontWorker {
    iThread *thread;
    iString  cachePath;
    iBool    stopWorker;
};

static iFontWorker *worker_;

iDeclareTypeConstructionArgs(FontWorker, const iString *cachePath)

static iThreadResult refresh_FontWorker_(iThread *thread) {
    iFontWorker *d = userData_Thread(thread);
    /* Check if font directories have changed since the cache was written. */
    iBool needsRefresh = iTrue; /* assume refresh needed if cache is unreadable */
    iFile *f = new_File(&d->cachePath);
    if (open_File(f, readOnly_FileMode)) {
        iStream *ins   = stream_File(f);
        uint32_t magic = readU32_Stream(ins);
        uint32_t ver   = readU32_Stream(ins);
        uint32_t ctVer = readU32_Stream(ins);
        if (magic == magic_FontCacheFile_ &&
            ver   == version_FontCacheFile_ &&
            ctVer == CTGetCoreTextVersion()) {
            iDate cachedLib, cachedUserLib;
            deserialize_Date(&cachedLib,     ins);
            deserialize_Date(&cachedUserLib, ins);
            iDate currentLib, currentUserLib;
            currentFontDirModTimes_(&currentLib, &currentUserLib);
            iTime cl, cc, ul, uc;
            init_Time(&cl, &cachedLib);     init_Time(&cc, &currentLib);
            init_Time(&ul, &cachedUserLib); init_Time(&uc, &currentUserLib);
            needsRefresh = (cmp_Time(&cl, &cc) != 0 || cmp_Time(&ul, &uc) != 0);
        }
        close_File(f);
    }
    iRelease(f);
    if (!needsRefresh || d->stopWorker) {
        goto done_;
    }
    /* Font directories changed: re-enumerate and save a fresh cache. */
    iFontPack *fresh = new_FontPack();
    setReadOnly_FontPack(fresh, iTrue);
    enumerateSystemFonts_FontPack_(fresh);
    if (!d->stopWorker) {
        saveCached_FontPack_(fresh, &d->cachePath);
    }
    delete_FontPack(fresh);
    if (!d->stopWorker) {
        /* Ask the main thread to reload fonts with the fresh cache. */
        postCommand_App("font.reload");
    }
done_:
    /* Self-destruct: clear global pointer, release thread handle (skips join in deinit),
       then release self. deinit_FontWorker sees d->thread == NULL and skips join_Thread. */
    worker_ = NULL;
    iRelease(d->thread);
    d->thread = NULL;
    delete_FontWorker(d);
    return 0;
}

static void start_FontWorker_(iFontWorker *d) {
    iAssert(!d->thread);
    d->stopWorker = iFalse;
    d->thread = new_Thread(refresh_FontWorker_);
    setName_Thread(d->thread, "FontWorker");
    setUserData_Thread(d->thread, d);
    start_Thread(d->thread);
}

static void stop_FontWorker_(iFontWorker *d) {
    if (d->thread) {
        d->stopWorker = iTrue;
        join_Thread(d->thread);
        iReleasePtr(&d->thread);
    }
}

void init_FontWorker(iFontWorker *d, const iString *cachePath) {
    d->thread = NULL;
    d->stopWorker = iFalse;
    initCopy_String(&d->cachePath, cachePath);
    start_FontWorker_(d);
}

void deinit_FontWorker(iFontWorker *d) {
    stop_FontWorker_(d);
    deinit_String(&d->cachePath);
}

iDefineTypeConstructionArgs(FontWorker, (const iString *cachePath), cachePath)

static void startWorker_AppleText_(const iString *cachePath) {
    if (!worker_) {
        worker_ = new_FontWorker(cachePath);
    }
}

static void stopWorker_AppleText_(void) {
    if (worker_) {
        delete_FontWorker(worker_);
        worker_ = NULL;
    }
}

void loadCachedFontPack_AppleText(const iString *cacheFile, iFontPack *pack) {
    /* Load from cache; fall back to synchronous enumeration on first run or OS update. */
    if (!tryLoadCached_FontPack_(pack, cacheFile)) {
        enumerateSystemFonts_FontPack_(pack);
        saveCached_FontPack_(pack, cacheFile);
    }
    /* Validate cache in background; re-enumerates and reloads only if dirs have changed. */
    startWorker_AppleText_(cacheFile);
}