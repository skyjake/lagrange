/* Copyright 2024 Christoph Liebender <christoph@liebender.dev>
   Copyright 2025 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "jpegxl.h"

#include <the_Foundation/block.h>
#include <the_Foundation/map.h>

#include <jxl/types.h>
#include <jxl/decode.h>
#include <jxl/codestream_header.h>
#include <jxl/resizable_parallel_runner.h>

iDeclareType(Media);

typedef uint16_t iGmLinkId;

struct Impl_Jpegxl {
    iMap *jxlDecoderMap;
};

iDefineTypeConstruction(Jpegxl);

void init_Jpegxl(iJpegxl *d) {
    d->jxlDecoderMap = NULL; /* created lazily */
}

void deinit_Jpegxl(iJpegxl *d) {
    clear_Jpegxl(d);
    delete_Map(d->jxlDecoderMap);
}

/*-----------------------------------------------------------------------------------------------*/

iDeclareType(StatefulJxlDecoder);

struct Impl_StatefulJxlDecoder {
    iMapNode    node;
    JxlDecoder *decoder;
    iInt2       imSize;
    uint8_t    *buffer;
    size_t      bufferSize;
    void       *opaqueRunner;
    size_t      nSeenBytes;
    iBlock     *blockHandle;
};

iDeclareTypeConstructionArgs(StatefulJxlDecoder, iGmLinkId linkId, int wantedEvents);
iDefineTypeConstructionArgs(StatefulJxlDecoder, (iGmLinkId linkId, int wantedEvents), linkId,
                            wantedEvents);

void init_StatefulJxlDecoder(iStatefulJxlDecoder *d, iGmLinkId linkId, int wantedEvents) {
    memset(d, 0, sizeof(iStatefulJxlDecoder));

    d->node.key    = linkId;
    d->decoder     = JxlDecoderCreate(NULL);
    d->blockHandle = new_Block(0);

    if (!(d->opaqueRunner = JxlResizableParallelRunnerCreate(NULL)))
        fprintf(stderr, "[media] JxlResizableParallelRunnerCreate failed\n");

    if (d->opaqueRunner &&
        JXL_DEC_SUCCESS !=
            JxlDecoderSetParallelRunner(d->decoder, JxlResizableParallelRunner, d->opaqueRunner))
        fprintf(stderr, "[media] JxlDecoderSetParallelRunner failed\n");

    if (JXL_DEC_SUCCESS != JxlDecoderSubscribeEvents(d->decoder, wantedEvents))
        fprintf(stderr, "[media] JxlDecoderSubscribeEvents failed\n");
}

void deinit_StatefulJxlDecoder(iStatefulJxlDecoder *d) {
    JxlDecoderDestroy(d->decoder);
    JxlResizableParallelRunnerDestroy(d->opaqueRunner);
    if (d->blockHandle)
        clear_Block(d->blockHandle);
    free(d->buffer);
}

static int compare_MapNode_(iMapKey a, iMapKey b) {
    return (a > b) - (a < b);
}

static uint8_t *loadJxl_(iJpegxl *jxl, const iBlock *data, iInt2 *imSize, iGmLinkId linkId,
                         iBool isPartial) {
    iStatefulJxlDecoder *d;
    JxlBasicInfo         info;
    JxlDecoderStatus     status;
    uint8_t             *imgData = NULL;

    const JxlPixelFormat format = {
        .num_channels = 4, .data_type = JXL_TYPE_UINT8, .endianness = JXL_NATIVE_ENDIAN, .align = 0
    };

    if (size_Block(data) == 0) {
        return NULL;
    }

    if (!jxl->jxlDecoderMap) {
        jxl->jxlDecoderMap = new_Map(compare_MapNode_);
    }

    iAssert(jxl->jxlDecoderMap);

    if (contains_Map(jxl->jxlDecoderMap, linkId)) {
        d = (iStatefulJxlDecoder *) value_Map(jxl->jxlDecoderMap, linkId);
    }
    else {
        d = new_StatefulJxlDecoder(linkId, JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE);
        if (isPartial) insert_Map(jxl->jxlDecoderMap, &d->node);
    }

    // add refcount s.t. data is not freed until decoder is done
    set_Block(d->blockHandle, data);

    JxlDecoderSetInput(d->decoder,
                       constData_Block(d->blockHandle) + d->nSeenBytes,
                       size_Block(d->blockHandle) - d->nSeenBytes);

    if (!isPartial) JxlDecoderCloseInput(d->decoder);

    while (true) {
        switch (status = JxlDecoderProcessInput(d->decoder)) {
            case JXL_DEC_BASIC_INFO:
                if (JXL_DEC_SUCCESS != JxlDecoderGetBasicInfo(d->decoder, &info)) {
                    fprintf(stderr, "[media] JxlDecoderGetBasicInfo failed\n");
                    goto err;
                }
                d->imSize = init_I2(info.xsize, info.ysize);
                JxlResizableParallelRunnerSetThreads(
                    d->opaqueRunner,
                    JxlResizableParallelRunnerSuggestThreads(info.xsize, info.ysize));
                break;
            case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
                if (JXL_DEC_SUCCESS !=
                    JxlDecoderImageOutBufferSize(d->decoder, &format, &d->bufferSize)) {
                    fprintf(stderr, "JxlDecoderImageOutBufferSize failed\n");
                    goto err;
                }
                d->buffer = realloc(d->buffer, d->bufferSize);
                if (JXL_DEC_SUCCESS !=
                    JxlDecoderSetImageOutBuffer(d->decoder, &format, d->buffer, d->bufferSize)) {
                    fprintf(stderr, "JxlDecoderSetImageOutBuffer failed\n");
                    goto err;
                }
                break;
            case JXL_DEC_NEED_MORE_INPUT:
                if (!isPartial) {
                    fprintf(stderr, "[media] Incomplete JXL data\n");
                    goto err;
                }
            case JXL_DEC_FULL_IMAGE:
                d->nSeenBytes = size_Block(d->blockHandle) - JxlDecoderReleaseInput(d->decoder);
                clear_Block(d->blockHandle);

                if (status != JXL_DEC_NEED_MORE_INPUT ||
                    JXL_DEC_SUCCESS == JxlDecoderFlushImage(d->decoder)) {
                    printf("[media] flushed jxl after %lu bytes\n", d->nSeenBytes);
                    *imSize = d->imSize;
                    imgData = malloc(d->bufferSize);
                    memcpy(imgData, d->buffer, d->bufferSize);
                }

                goto ret;
            case JXL_DEC_SUCCESS:
                *imSize   = d->imSize;
                imgData   = d->buffer;
                d->buffer = NULL;
                goto ret;
            case JXL_DEC_ERROR:
                fprintf(stderr, "[media] JXL decoder error\n");
                goto err;
            default:
                fprintf(stderr, "[media] JXL unknown decoder status\n");
                goto err;
        } /* switch (status) */
    } /* while */
ret:
    if (!isPartial) {
    err:
        remove_Map(jxl->jxlDecoderMap, d->node.key);
        collect_StatefulJxlDecoder(d);
    }
    return imgData;
}

uint8_t *decodeImage_Jpegxl(iJpegxl *d, uint16_t linkId, const iBlock *data, iBool isPartial,
                            iInt2 *imageSize_out) {
    return loadJxl_(d, data, imageSize_out, linkId, isPartial);
}

void clear_Jpegxl(iJpegxl *d) {
    if (d->jxlDecoderMap) {
        iMapIterator         jxlDecoderMapIterator;
        iStatefulJxlDecoder *decoder;
        init_MapIterator(&jxlDecoderMapIterator, d->jxlDecoderMap);
        while ((decoder = (iStatefulJxlDecoder *) remove_MapIterator(&jxlDecoderMapIterator))) {
            collect_StatefulJxlDecoder(decoder);
            next_MapIterator(&jxlDecoderMapIterator);
        }
        clear_Map(d->jxlDecoderMap);
    }
}

void cancel_Jpegxl(iJpegxl *d, uint16_t linkId) {
    /* abort possible in-progress decoding */
    if (d->jxlDecoderMap) {
        iStatefulJxlDecoder *jxlDecoder =
            (iStatefulJxlDecoder *) remove_Map(d->jxlDecoderMap, linkId);
        if (jxlDecoder) delete_StatefulJxlDecoder(jxlDecoder);
    }
}
