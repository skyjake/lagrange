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

#include "lagrange/sitespec.h"
#include "lagrange/core.h"
#include "lagrange/gmutil.h"

#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/path.h>
#include <the_Foundation/stringhash.h>
#include <the_Foundation/stringarray.h>
#include <the_Foundation/toml.h>

iDeclareClass(SiteParams)
iDeclareObjectConstruction(SiteParams)

struct Impl_SiteParams {
    iObject  object;
    uint16_t titanPort;
    iString  titanIdentity; /* fingerprint */
    iString  titanToken;
    int      dismissWarnings;
    int      tlsSessionCache;
    iStringArray usedIdentities; /* fingerprints; latest ones at the end */
    iStringSet promptPaths;
    iScopeRule rule; /* document preferences */
};

void init_SiteParams(iSiteParams *d) {
    d->titanPort = 0; /* undefined */
    init_String(&d->titanIdentity);
    init_String(&d->titanToken);
    d->dismissWarnings = 0;
    d->tlsSessionCache = iTrue;
    init_StringArray(&d->usedIdentities);
    init_StringSet(&d->promptPaths);
    init_ScopeRule(&d->rule, site_ScopeRuleKind);
}

void deinit_SiteParams(iSiteParams *d) {
    deinit_ScopeRule(&d->rule);
    deinit_StringSet(&d->promptPaths);
    deinit_StringArray(&d->usedIdentities);
    deinit_String(&d->titanToken);
    deinit_String(&d->titanIdentity);
}

static size_t findUsedIdentity_SiteParams_(const iSiteParams *d, const iString *fingerprint) {
    iConstForEach(StringArray, i, &d->usedIdentities) {
        if (equal_String(i.value, fingerprint)) {
            return index_StringArrayConstIterator(&i);
        }
    }
    return iInvalidPos;
}

iDefineClass(SiteParams)
iDefineObjectConstruction(SiteParams)

/*----------------------------------------------------------------------------------------------*/

struct Impl_SiteSpec {
    iString     saveDir; /* prefs directory */
    iString     oldSaveDir;
    iStringHash sites;
    iSiteParams *loadParams;
    enum iImportMethod loadMethod;
};

static iSiteSpec   siteSpec_;
static const char *fileName_SiteSpec_ = "sitespec.ini";

static void loadOldFormat_SiteSpec_(iSiteSpec *d) {
    clear_StringHash(&d->sites);
    iFile *f = iClob(new_File(collect_String(concatCStr_Path(&d->oldSaveDir, "sitespec.txt"))));
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        iString *src = collect_String(readString_File(f));
        iRangecc split = iNullRange;
        iString *key = collectNew_String();
        iSiteParams *params = NULL;
        while (nextSplit_Rangecc(range_String(src), "\n", &split)) {
            iRangecc line = split;
            trim_Rangecc(&line);
            if (isEmpty_Range(&line)) {
                continue;
            }
            if (startsWith_Rangecc(line, "# ")) {
                if (params && !isEmpty_String(key)) {
                    insert_StringHash(&d->sites, key, params);
                    iReleasePtr(&params);
                }
                line.start += 2;
                setRange_String(key, line);
                params = new_SiteParams();
                continue;
            }
            if (startsWith_Rangecc(line, "titanPort: ")) {
                line.start += 11;
                if (params) {
                    params->titanPort = atoi(cstr_Rangecc(line));
                }
                continue;
            }
        }
        if (params && !isEmpty_String(key)) {
            insert_StringHash(&d->sites, key, params);
            iReleasePtr(&params);
        }
    }
}

static void handleIniTable_SiteSpec_(void *context, const iString *table, iBool isStart) {
    iSiteSpec *d = context;
    if (isStart) {
        iAssert(d->loadParams == NULL);
        d->loadParams = new_SiteParams();
    }
    else {
        iAssert(d->loadParams != NULL);
        if (d->loadMethod == all_ImportMethod ||
            (d->loadMethod == ifMissing_ImportMethod && !contains_StringHash(&d->sites, table))) {
            set_String(&d->loadParams->rule.key, table);
            fillMissing_ScopeRules(&d->loadParams->rule);
            insert_StringHash(&d->sites, table, d->loadParams);
        }
        iReleasePtr(&d->loadParams);
    }
}

static void handleIniKeyValue_SiteSpec_(void *context, const iString *table, const iString *key,
                                        const iTomlValue *value) {
    iSiteSpec *d = context;
    iUnused(table);
    if (!d->loadParams) {
        return;
    }
    if (!cmp_String(key, "titanPort")) {
        d->loadParams->titanPort = number_TomlValue(value);
    }
    else if (!cmp_String(key, "titanIdentity") && value->type == string_TomlType) {
        set_String(&d->loadParams->titanIdentity, value->value.string);
    }
    else if (!cmp_String(key, "titanToken") && value->type == string_TomlType) {
        set_String(&d->loadParams->titanToken, value->value.string);
    }
    else if (!cmp_String(key, "dismissWarnings") && value->type == int64_TomlType) {
        d->loadParams->dismissWarnings = (int) value->value.int64;
    }
    else if (!cmp_String(key, "tlsSessionCache") && value->type == boolean_TomlType) {
        d->loadParams->tlsSessionCache = value->value.boolean;
    }
    else if (!cmp_String(key, "usedIdentities") && value->type == string_TomlType) {
        iRangecc seg = iNullRange;
        while (nextSplit_Rangecc(range_String(value->value.string), " ", &seg)) {
            pushBack_StringArray(&d->loadParams->usedIdentities, collectNewRange_String(seg));
        }
    }
    else if (!cmp_String(key, "promptPaths") && value->type == string_TomlType) {
        iRangecc seg = iNullRange;
        while (nextSplit_Rangecc(range_String(value->value.string), " ", &seg)) {
            insert_StringSet(&d->loadParams->promptPaths, collectNewRange_String(seg));
        }
    }
    else {
        parseKey_ScopeRule(&d->loadParams->rule, key, value);
    }
}

static iBool load_SiteSpec_(iSiteSpec *d, const iString *dir) {
    iBool ok = iFalse;
    iFile *f = new_File(collect_String(concatCStr_Path(dir, fileName_SiteSpec_)));
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        ok = deserialize_SiteSpec(stream_File(f), all_ImportMethod);
    }
    iRelease(f);
    iAssert(d->loadParams == NULL);
    return ok;
}

iBool deserialize_SiteSpec(iStream *ins, enum iImportMethod loadMethod) {
    iSiteSpec *d = &siteSpec_;
    d->loadMethod = loadMethod;
    iTomlParser *toml = new_TomlParser();
    setHandlers_TomlParser(toml, handleIniTable_SiteSpec_, handleIniKeyValue_SiteSpec_, d);
    iBool ok = parse_TomlParser(toml, collect_String(readString_Stream(ins)));
    delete_TomlParser(toml);
    return ok;
}

void serialize_SiteSpec(iStream *out) {
    iSiteSpec *d = &siteSpec_;
    iString *buf = new_String();
    iConstForEach(StringHash, i, &d->sites) {
        iBeginCollect();
        const iBlock *     key    = &i.value->keyBlock;
        const iSiteParams *params = i.value->object;
        clear_String(buf);
        if (params->titanPort) {
            appendFormat_String(buf, "titanPort = %u\n", params->titanPort);
        }
        if (!isEmpty_String(&params->titanIdentity)) {
            appendFormat_String(
                buf, "titanIdentity = \"%s\"\n", cstr_String(&params->titanIdentity));
        }
        if (!isEmpty_String(&params->titanToken)) {
            appendFormat_String(
                buf,
                "titanToken = \"%s\"\n",
                cstrCollect_String(quote_String(&params->titanToken, iFalse)));
        }
        if (params->dismissWarnings) {
            appendFormat_String(buf, "dismissWarnings = 0x%x\n", params->dismissWarnings);
        }
        if (!params->tlsSessionCache) {
            appendCStr_String(buf, "tlsSessionCache = false\n");
        }
        if (!isEmpty_StringArray(&params->usedIdentities)) {
            appendFormat_String(
                buf,
                "usedIdentities = \"%s\"\n",
                cstrCollect_String(joinCStr_StringArray(&params->usedIdentities, " ")));
        }
        if (!isEmpty_StringSet(&params->promptPaths)) {
            appendFormat_String(buf,
                                "promptPaths = \"%s\"\n",
                                cstrCollect_String(joinCStr_StringSet(&params->promptPaths, " ")));
        }
        serialize_ScopeRule(&params->rule, buf);
        if (!isEmpty_String(buf)) {
            writeData_Stream(out, "[", 1);
            writeData_Stream(out, constData_Block(key), size_Block(key));
            writeData_Stream(out, "]\n", 2);
            appendCStr_String(buf, "\n");
            write_Stream(out, utf8_String(buf));
        }
        iEndCollect();
    }
    delete_String(buf);
}

static void save_SiteSpec_(iSiteSpec *d) {
    makeDirs_Path(&d->saveDir);
    const iString *path     = collect_String(concatCStr_Path(&d->saveDir, fileName_SiteSpec_));
    const iString *tempPath = collect_String(concatCStr_Path(&d->saveDir, "sitespec.ini.tmp"));
    iFile *f = new_File(tempPath);
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        serialize_SiteSpec(stream_File(f));
        close_File(f);
        commitFile_Core(cstr_String(path), cstr_String(tempPath));
    }
    iRelease(f);
    notifyUserDataChanged_Core(sites_UserDataType);
}

void save_SiteSpec(void) {
    save_SiteSpec_(&siteSpec_);
}

void replace_SiteSpec(iStream *ins) {
    iSiteSpec *d = &siteSpec_;
    clear_StringHash(&d->sites);
    deserialize_SiteSpec(ins, all_ImportMethod);
    save_SiteSpec_(d);
}

void init_SiteSpec(const char *saveDir) {
    iSiteSpec *d = &siteSpec_;
    d->loadParams = NULL;
    init_StringHash(&d->sites);
    initCStr_String(&d->oldSaveDir, saveDir);
    initCStr_String(&d->saveDir, concatPath_CStr(saveDir, "prefs"));
    const iString *path    = collect_String(concatCStr_Path(&d->saveDir, fileName_SiteSpec_));
    const iString *oldPath = collect_String(concatCStr_Path(&d->oldSaveDir, fileName_SiteSpec_));
    if (!fileExists_FileInfo(&d->saveDir) && fileExists_FileInfo(oldPath)) {
        /* The file was in the parent directory before v2.0. */
        makeDirs_Path(&d->saveDir);
        renamePath_CStr(cstr_String(oldPath), cstr_String(path));
    }
    if (fileExists_FileInfo(path)) {
        load_SiteSpec_(d, &d->saveDir);
    }
    else if (!load_SiteSpec_(d, &d->oldSaveDir)) { /* e.g., restored from an old backup */
        loadOldFormat_SiteSpec_(d);
    }
}

void deinit_SiteSpec(void) {
    iSiteSpec *d = &siteSpec_;
    deinit_StringHash(&d->sites);
    deinit_String(&d->saveDir);
    deinit_String(&d->oldSaveDir);
}

static iSiteParams *findParams_SiteSpec_(iSiteSpec *d, const iString *site) {
    const iString *hashKey = collect_String(lower_String(site));
    iSiteParams *params = value_StringHash(&d->sites, hashKey);
    if (!params) {
        params = new_SiteParams();
        set_String(&params->rule.key, hashKey);
        insert_StringHash(&d->sites, hashKey, params);
        iRelease(params); /* kept by the hash */
    }
    return params;
}

const iScopeRule *constRule_SiteSpec(const iString *site) {
    const iSiteParams *params =
        constValue_StringHash(&siteSpec_.sites, collect_String(lower_String(site)));
    return params ? &params->rule : NULL;
}

iScopeRule *rule_SiteSpec(const iString *site) {
    return &findParams_SiteSpec_(&siteSpec_, site)->rule;
}

void setValue_SiteSpec(const iString *site, enum iSiteSpecKey key, int value) {
    iSiteSpec *d = &siteSpec_;
    iSiteParams *params = findParams_SiteSpec_(d, site);
    iBool needSave = iFalse;
    switch (key) {
        case titanPort_SiteSpecKey:
            params->titanPort = iClamp(value, 0, 0xffff);
            needSave = iTrue;
            break;
        case dismissWarnings_SiteSpecKey:
            params->dismissWarnings = value;
            needSave = iTrue;
            break;
        case tlsSessionCache_SiteSpeckey:
            if (value != params->tlsSessionCache) {
                params->tlsSessionCache = value;
                needSave = iTrue;
            }
            break;
        default:
            break;
    }
    if (needSave) {
        save_SiteSpec_(d);
    }
}

void setValueString_SiteSpec(const iString *site, enum iSiteSpecKey key, const iString *value) {
    iSiteSpec *d = &siteSpec_;
    iSiteParams *params = findParams_SiteSpec_(d, site);
    iBool needSave = iFalse;
    switch (key) {
        case titanIdentity_SiteSpecKey:
            if (!equal_String(&params->titanIdentity, value)) {
                needSave = iTrue;
                set_String(&params->titanIdentity, value);
            }
            break;
        case titanToken_SiteSpecKey:
            if (!equal_String(&params->titanToken, value)) {
                needSave = iTrue;
                set_String(&params->titanToken, value);
            }
            break;
        default:
            break;
    }
    if (needSave) {
        save_SiteSpec_(d);
    }
}

static void insertOrRemoveString_SiteSpec_(iSiteSpec *d, const iString *site, enum iSiteSpecKey key,
                                           const iString *value, iBool doInsert) {
    iSiteParams *params = findParams_SiteSpec_(d, site);
    iBool needSave = iFalse;
    switch (key) {
        case usedIdentities_SiteSpecKey: {
            const size_t index = findUsedIdentity_SiteParams_(params, value);
            if (doInsert && index == iInvalidPos) {
                pushBack_StringArray(&params->usedIdentities, value);
                needSave = iTrue;
            }
            else if (!doInsert && index != iInvalidPos) {
                remove_StringArray(&params->usedIdentities, index);
                needSave = iTrue;
            }
            break;
        }
        case promptPaths_SiteSpecKey: {
            if (contains_StringSet(&params->promptPaths, value)) {
                if (!doInsert) {
                    remove_StringSet(&params->promptPaths, value);
                    needSave = iTrue;
                }
            }
            else if (doInsert) {
                insert_StringSet(&params->promptPaths, value);
                needSave = iTrue;
            }
            break;
        }
        default:
            break;
    }
    if (needSave) {
        save_SiteSpec_(d);
    }
}

void insertString_SiteSpec(const iString *site, enum iSiteSpecKey key, const iString *value) {
    insertOrRemoveString_SiteSpec_(&siteSpec_, site, key, value, iTrue);
}

void removeString_SiteSpec(const iString *site, enum iSiteSpecKey key, const iString *value) {
    insertOrRemoveString_SiteSpec_(&siteSpec_, site, key, value, iFalse);
}

const iStringArray *strings_SiteSpec(const iString *site, enum iSiteSpecKey key) {
    const iSiteParams *params = findParams_SiteSpec_(&siteSpec_, site);
    if (key == usedIdentities_SiteSpecKey) {
        return &params->usedIdentities;
    }
    return NULL;
}

const iStringSet *stringSet_SiteSpec(const iString *site, enum iSiteSpecKey key) {
    const iSiteParams *params = findParams_SiteSpec_(&siteSpec_, site);
    if (key == promptPaths_SiteSpecKey) {
        return &params->promptPaths;
    }
    return NULL;
}

int value_SiteSpec(const iString *site, enum iSiteSpecKey key) {
    iSiteSpec *d = &siteSpec_;
    const iSiteParams *params = constValue_StringHash(&d->sites, collect_String(lower_String(site)));
    if (!params) {
        /* Default values. */
        switch (key) {
            case tlsSessionCache_SiteSpeckey:
                return 1;
            default:
                return 0;
        }
    }
    switch (key) {
        case titanPort_SiteSpecKey:
            return params->titanPort;
        case dismissWarnings_SiteSpecKey:
            return params->dismissWarnings;
        case tlsSessionCache_SiteSpeckey:
            return params->tlsSessionCache;
        default:
            return 0;
    }
}

const iString *valueString_SiteSpec(const iString *site, enum iSiteSpecKey key) {
    iSiteSpec *d = &siteSpec_;
    const iSiteParams *params = constValue_StringHash(&d->sites, collect_String(lower_String(site)));
    if (!params) {
        return 0;
    }
    switch (key) {
        case titanIdentity_SiteSpecKey:
            return &params->titanIdentity;
        case titanToken_SiteSpecKey:
            return &params->titanToken;
        default:
            return collectNew_String();
    }
}

iBool isPromptUrl_SiteSpec(const iString *url) {
    iUrl parts;
    iString root;
    iString path;
    init_Url(&parts, canonicalUrl_String(url));
    initRange_String(&root, urlRoot_String(url));
    initRange_String(&path, parts.path);
    const iBool isPromptUrl = contains_StringSet(stringSet_SiteSpec(&root, promptPaths_SiteSpecKey),
                                                 &path);
    deinit_String(&path);
    deinit_String(&root);
    return isPromptUrl;
}
