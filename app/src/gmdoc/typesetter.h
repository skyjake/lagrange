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

#pragma once

#include "impl.h"

iDeclareType(TypesetterSeam)

struct Impl_TypesetterSeam {
    /* State where a line begins, for checking that a seeded layout matches the layout above
       it. The rest of the state is set by the lead-in line, or does not affect layout. */
    int              y;   /* not compared */
    int              gap; /* to the bottom of the previous text, for margin collapsing */
    enum iGmLineType prevType;
    enum iGmLineType prevNonBlankType;
    size_t           nextLink;
    int              pendingMargin;
    uint16_t         preId;
    iBool            isPreformat;
    iBool            enableIndents;
    iBool            isFirstText;
    iBool            addQuoteIcon;
    iBool            followsBlank;
    iBool            prevWrapParagraph;
};

struct Impl_Typesetter {
    iGmDocument          *doc;
    iArray               *layout; /* runs are added here */
    iArray                ownLayout;
    const iDocPrefs      *prefs;
    const iDocumentStyle *style;
    iBool                 isMono;
    iBool                 isGopher;
    iBool                 isPaged;
    iBool                 isNormalized;
    iBool                 isJustified;
    int                   outsideMargin;
    iRangecc              content;
    iRangecc              contentLine;
    /* Current line. */
    iRangecc              line; /* trimmed; modifying `contentLine` would confuse `nextSplit_Rangecc` */
    enum iGmLineType      type;
    float                 indent;
    iGmRun                run;
    /* State carried over between lines. */
    iInt2                 pos;
    iBool                 isFirstText;
    iBool                 addQuoteIcon;
    iBool                 isPreformat;
    int                   preFont;
    uint16_t              preId;
    iBool                 enableIndents;
    enum iGmLineType      prevType;
    enum iGmLineType      prevNonBlankType;
    iBool                 followsBlank;
    int                   pendingMargin;     /* added below the next line's top margin */
    int                   numHeading1;
    iBool                 prevWrapParagraph; /* previous line was a wrapped paragraph */
    iString               headingTitle;      /* the first top-level heading */
    iString               firstContentLine;  /* may be used as a title if one isn't specified */
    size_t                nextLink;          /* index of the next parsed link */
    int                   maxRunWidth;
    uint16_t              openPreId;         /* block whose end was not received when laid out */
    uint16_t              topLeftPreId;      /* last block whose position was set */
    iBool                 isWrapWidthChecked;
    iBool                 isAtTop;           /* no runs precede `layout` */
    int                   earlierBottom;     /* bottom of text that precedes `layout` */
    /* Anchored layout. */
    const char           *seamPos;           /* stop before this line */
    const char           *stopPos;           /* stop after the line containing this */
    iBool                 isAtSeam;
    iTypesetterSeam       seam;
    uint16_t              seedPreId;         /* blocks after this are positioned by this pass */
    const char           *keepPos;
    int                   syncSpan;          /* laid out synchronously below the kept run */
    iTypesetter          *above;             /* lays out the content above the seam */
};

iBool   isExpandingToLongLines_GmDocument_      (const iGmDocument *);
void    determinePlainTextWrapWidth_GmDocument_ (iGmDocument *);
iBool   beginLayout_GmDocument_                 (iGmDocument *, iBool keepTitle);

int     lastVisibleRunBottom_Typesetter_(const iTypesetter *);
void    init_Typesetter_                (iTypesetter *, iGmDocument *doc, iBool isOwnLayout);
void    deinit_Typesetter_              (iTypesetter *);
void    begin_Typesetter_               (iTypesetter *); /* at the top of the document */
void    beginAt_Typesetter_             (iTypesetter *, const char *lineStart);
iBool   typesetLines_Typesetter_        (iTypesetter *, int minHeight, double maxSeconds); /* True if caught up */
void    publish_Typesetter_             (iTypesetter *, iBool isCaughtUp);
iBool   typesetSome_Typesetter_         (iTypesetter *, int minHeight, double maxSeconds); /* lines and publish */
iBool   isSame_TypesetterSeam_          (const iTypesetterSeam *, const iTypesetterSeam *);
void    shift_Typesetter_               (iTypesetter *, int dy);
const iGmRun *findKeptRun_Typesetter_   (const iTypesetter *);
const iGmRun *layoutUntilKept_Typesetter_(iTypesetter *);
void    finish_Typesetter_              (iTypesetter *); /* whole-document steps */
void    rebase_Typesetter_              (iTypesetter *, const iGmRebase *);
