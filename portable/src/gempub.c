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
#include "gmdocument.h"
#include "gmrequest.h"
#include "ui/util.h"
#include "app.h"

#include <the_Foundation/archive.h>
#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <the_Foundation/regexp.h>

const char *mimeType_Gempub = "application/gpub+zip";

/*----------------------------------------------------------------------------------------------*/

iDeclareType(GempubNavLink)
 
struct Impl_GempubNavLink {
    iString url;
    iString label;
};
    
static void init_GempubNavLink(iGempubNavLink *d) {
    init_String(&d->url);
    init_String(&d->label);
}

static void deinit_GempubNavLink(iGempubNavLink *d) {
    deinit_String(&d->url);
    deinit_String(&d->label);   
}

iDefineTypeConstruction(GempubNavLink)
    
/*----------------------------------------------------------------------------------------------*/
    
struct Impl_Gempub {
    iArchive *arch;
    iString baseUrl;
    iString props[max_GempubProperty];
    iArray *navLinks; /* from index page */
};

iDefineTypeConstruction(Gempub)
    
static void parseNavigationLinks_Gempub_(const iGempub *d) {
    if (!isEmpty_Array(d->navLinks)) {
        return;
    }
    iGmRequest *index = iClob(new_GmRequest(certs_App()));
    setUrl_GmRequest(index, indexPageUrl_Gempub(d));
    submit_GmRequest(index); /* this is just a local file read */
    iAssert(isFinished_GmRequest(index));
    iRangecc src = iNullRange;
    iRegExp *linkPattern = iClob(newGemtextLink_RegExp());
    while (nextSplit_Rangecc(range_Block(body_GmRequest(index)), "\n", &src)) {
        iRangecc line = src;
        trim_Rangecc(&line);
        iRegExpMatch m;
        init_RegExpMatch(&m);
        if (matchRange_RegExp(linkPattern, line, &m)) {
            iBeginCollect();
            const iRangecc url = capturedRange_RegExpMatch(&m, 1);
            iUrl parts;
            init_Url(&parts, collectNewRange_String(url));
            if (isEmpty_Range(&parts.scheme)) {
                iGempubNavLink link;
                init_GempubNavLink(&link);
                set_String(&link.url, absoluteUrl_String(url_GmRequest(index), collectNewRange_String(url)));
                setRange_String(&link.label, capturedRange_RegExpMatch(&m, 2));
                trim_String(&link.label);
                if (isEmpty_String(&link.label)) {
                    setRange_String(&link.label, url);                    
                }
                pushBack_Array(d->navLinks, &link);
            }
            iEndCollect();
        }
    }
}
    
void init_Gempub(iGempub *d) {
    d->arch = NULL;
    init_String(&d->baseUrl);
    iForIndices(i, d->props) {
        init_String(&d->props[i]);
    }
    d->navLinks = new_Array(sizeof(iGempubNavLink));
}

void deinit_Gempub(iGempub *d) {
    iForEach(Array, n, d->navLinks) {
        deinit_GempubNavLink(n.value);
    }
    delete_Array(d->navLinks);
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
    setCStr_String(&d->props[title_GempubProperty], "${gempub.cover.untitled}");
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

void setBaseUrl_Gempub(iGempub *d, const iString *url) {
    set_String(&d->baseUrl, collect_String(urlDecodeExclude_String(url, "/:")));
}

iBool isOpen_Gempub(const iGempub *d) {
    return d->arch != NULL;
}

const iString *property_Gempub(const iGempub *d, enum iGempubProperty prop) {
    return &d->props[prop];
}

const iString *coverPageUrl_Gempub(const iGempub *d) {
    return &d->baseUrl;
}

const iString *indexPageUrl_Gempub(const iGempub *d) {
    iAssert(!isEmpty_String(&d->baseUrl));
    iString *dir = collect_String(copy_String(&d->baseUrl));
    appendCStr_String(dir, "/");
    return absoluteUrl_String(dir, &d->props[index_GempubProperty]);
}

const iString *navStartLinkUrl_Gempub(const iGempub *d) {
    parseNavigationLinks_Gempub_(d);
    if (isEmpty_Array(d->navLinks)) {
        return NULL; /* has no navigation structure */
    }
    return &((const iGempubNavLink *) constFront_Array(d->navLinks))->url;
}

size_t navSize_Gempub(const iGempub *d) {
    parseNavigationLinks_Gempub_(d);
    return size_Array(d->navLinks);
}

size_t navIndex_Gempub(const iGempub *d, const iString *url) {
    parseNavigationLinks_Gempub_(d);
    const iString *canonUrl = withSpacesEncoded_String(url);
    iConstForEach(Array, i, d->navLinks) {
        const iGempubNavLink *nav = i.value;
        if (equalCase_String(&nav->url, canonUrl)) {
            return index_ArrayConstIterator(&i);
        }
    }
    return iInvalidPos;
}

const iString *navLinkUrl_Gempub(const iGempub *d, size_t index) {
    parseNavigationLinks_Gempub_(d);
    if (index < size_Array(d->navLinks)) {
        return &constValue_Array(d->navLinks, index, iGempubNavLink).url;
    }
    return NULL;
}

const iString *navLinkLabel_Gempub(const iGempub *d, size_t index) {
    parseNavigationLinks_Gempub_(d);
    if (index < size_Array(d->navLinks)) {
        return &constValue_Array(d->navLinks, index, iGempubNavLink).label;
    }
    return NULL;    
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

iBool isRemote_Gempub(const iGempub *d) {
    return !equalCase_Rangecc(urlScheme_String(&d->baseUrl), "file");
}

iString *coverPageSource_Gempub(const iGempub *d) {
    iAssert(!isEmpty_String(&d->baseUrl));
    const iString *baseUrl = withSpacesEncoded_String(&d->baseUrl);
    iString *out = new_String();
    format_String(out, "# %s\n",
                  cstr_String(&d->props[title_GempubProperty]));
    if (!isEmpty_String(&d->props[description_GempubProperty])) {
        appendFormat_String(out, "%s\n", cstr_String(&d->props[description_GempubProperty]));
    }
    appendCStr_String(out, "\n");
    appendProperty_Gempub_(d, "${gempub.meta.author}:", author_GempubProperty, out);
    if (!isRemote_Gempub(d)) {
        appendFormat_String(out, "\n=> %s " book_Icon " ${gempub.cover.view}\n",
                            cstr_String(indexPageUrl_Gempub(d)));
        if (hasProperty_Gempub_(d, cover_GempubProperty)) {
            appendFormat_String(out, "\n=> %s  ${gempub.cover.image}\n",
                                cstrCollect_String(concat_Path(baseUrl, &d->props[cover_GempubProperty])));
        }
    }
    else {
        iString *key = collectNew_String(); /* TODO: add a helper for this */
        toString_Sym(SDLK_s, KMOD_PRIMARY, key);
        appendCStr_String(out, "\n${gempub.cover.viewlocal}\n");
//        appendFormat_String(out,
//                            cstr_Lang("error.unsupported.suggestsave"),
//                            cstr_String(key),
//                            saveToDownloads_Label);
//        appendCStr_String(out, "\n");
    }
    appendCStr_String(out, "\n## ${gempub.cover.aboutbook}\n");
    appendProperty_Gempub_(d, "${gempub.meta.version}:", version_GempubProperty, out);
    appendProperty_Gempub_(d, "${gempub.meta.revdate}:", revisionDate_GempubProperty, out);
    if (hasProperty_Gempub_(d, publishDate_GempubProperty)) {
        appendProperty_Gempub_(d, "${gempub.meta.pubdate}:", publishDate_GempubProperty, out);
    }
    else {
        appendProperty_Gempub_(d, "${gempub.meta.pub}:", published_GempubProperty, out);
    }
    appendProperty_Gempub_(d, "${gempub.meta.lang}:", language_GempubProperty, out);
    appendProperty_Gempub_(d, "${gempub.meta.license}:", license_GempubProperty, out);
    appendProperty_Gempub_(d, "\u00a9", copyright_GempubProperty, out);
    translate_Lang(out);
    return out;
}

iBool preloadCoverImage_Gempub(const iGempub *d, iGmDocument *doc) {
    iBool haveImage = iFalse;
    for (size_t linkId = 1; ; linkId++) {
        const iString *linkUrl = linkUrl_GmDocument(doc, linkId);
        if (!linkUrl) break;
        if (findLinkImage_Media(media_GmDocument(doc), linkId).type) {
            continue; /* got this already */
        }
        if (linkFlags_GmDocument(doc, linkId) & imageFileExtension_GmLinkFlag) {
            iString *imgEntryPath = collect_String(copy_String(linkUrl));
            remove_Block(&imgEntryPath->chars, 0, size_String(&d->baseUrl) + 1 /* slash, too */);
            setData_Media(media_GmDocument(doc),
                          linkId,
                          collectNewCStr_String(mediaType_Path(linkUrl)),
                          data_Archive(d->arch, imgEntryPath),
                          0);
            haveImage = iTrue;
        }
    }
    return haveImage;
}
