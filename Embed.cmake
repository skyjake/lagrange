# CMake Helper for Binary Resources
# Copyright: 2020 Jaakko Keränen <jaakko.keranen@iki.fi>
# License: BSD 2-Clause

option (ENABLE_RESOURCE_EMBED "Embed resources inside the executable" OFF)
# Note: If disabled, the Unix "cat" tool is required for concatenating
# the resources into a single "resources.binary" file.

function (embed_getname output fn)
    get_filename_component (name ${fn} NAME_WE)
    string (REPLACE "-" "" name ${name})
    string (REPLACE "=" "" name ${name})
    string (SUBSTRING ${name} 0 1 first)
    string (TOUPPER ${first} first)
    string (SUBSTRING ${name} 1 -1 remainder)
    set (name "${first}${remainder}")
    get_filename_component (ext ${fn} EXT)
    if (ext STREQUAL .ttf)
        set (resName "font")
    elseif (ext STREQUAL .png)
        set (resName "image")
    else ()
        set (resName "blob")
    endif ()
    set (resName "${resName}${name}_Embedded" PARENT_SCOPE)
endfunction (embed_getname)

function (embed_write path name fnSource fnHeader)
    message (STATUS "${path}")
    file (READ ${path} fileData HEX)
    string (REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," cList ${fileData})
    string (REGEX REPLACE
        "(0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],)"
        "\\1\n    " cList ${cList})
    string (LENGTH ${fileData} len)
    math (EXPR alen "${len} / 2")
    set (src "static const uint8_t bytes_${name}_[] = {\n")
    string (APPEND src "    ${cList}\n")
    string (APPEND src "};
static iBlockData data_${name}_ = {
    .refCount = 2, .data = iConstCast(char *, bytes_${name}_), .size = ${alen}, .allocSize = ${alen}
};
const iBlock ${name} = { &data_${name}_ };
")
    set (header "extern const iBlock ${name};\n")
    # Output the results.
    file (APPEND ${fnSource} "${src}")
    file (APPEND ${fnHeader} "${header}")
endfunction (embed_write)

function (embed_filesize path sizeVar)
    file (READ ${path} data HEX)
    string (LENGTH ${data} fsize)
    math (EXPR fsize "${fsize}/2")
    set (${sizeVar} ${fsize} PARENT_SCOPE)
endfunction (embed_filesize)

function (embed_make)
    set (EMB_H ${CMAKE_CURRENT_BINARY_DIR}/embedded.h)
    set (EMB_C ${CMAKE_CURRENT_BINARY_DIR}/embedded.c)
    if (ENABLE_RESOURCE_EMBED)
        set (needGen NO)
        if (NOT EXISTS ${EMB_H} OR NOT EXISTS ${EMB_C})
            set (needGen YES)
        else ()
            file (TIMESTAMP ${EMB_H} genTime %s)
            foreach (resPath ${ARGV})
                set (fn "${CMAKE_CURRENT_LIST_DIR}/${resPath}")
                file (TIMESTAMP ${fn} resTime %s)
                if (${resTime} GREATER ${genTime})
                    set (needGen YES)
                endif ()
            endforeach (resPath)
        endif ()
    else ()
        set (needGen YES)
    endif ()
    if (needGen)
        if (ENABLE_RESOURCE_EMBED)
            # Compose a source file with the resource data in an array.
            file (WRITE ${EMB_H} "#include <the_Foundation/block.h>\n")
            file (WRITE ${EMB_C} "#include \"embedded.h\"\n")
            foreach (fn ${ARGV})
                embed_getname (resName ${fn})
                embed_write (${fn} ${resName} ${EMB_C} ${EMB_H})
            endforeach (fn)
        else ()
            # Collect resources in a single binary file.
            set (EMB_BIN ${CMAKE_CURRENT_BINARY_DIR}/resources.binary)
            file (REMOVE ${EMB_BIN})
            execute_process (COMMAND cat ${ARGV} OUTPUT_FILE ${EMB_BIN}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
            set (offsets)
            set (fpos 0)
            foreach (fn ${ARGV})
                embed_filesize (${CMAKE_SOURCE_DIR}/${fn} fileSize)
                list (APPEND offsets "${fpos}")
                math (EXPR fpos "${fpos} + ${fileSize}")
            endforeach (fn)
            file (WRITE ${EMB_H} "#include <the_Foundation/block.h>\n
#define iHaveLoadEmbed 1
iBool load_Embed(const char *path);\n\n")
            file (WRITE ${EMB_C} [[
#include "embedded.h"
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>

iDeclareType(EmbedChunk)

struct Impl_EmbedChunk {
    size_t pos;
    size_t size;
};

static const iEmbedChunk chunks_Embed_[] = {
]])
            set (index 0)
            foreach (fn ${ARGV})
                embed_filesize (${CMAKE_SOURCE_DIR}/${fn} fileSize)
                list (GET offsets ${index} fpos)
                file (APPEND ${EMB_C} "    { ${fpos}, ${fileSize} },\n")
                math (EXPR index "${index} + 1")
            endforeach (fn)
            file (APPEND ${EMB_C} "};\n\n")
            foreach (fn ${ARGV})
                embed_getname (resName ${fn})
                file (APPEND ${EMB_H} "extern iBlock ${resName};\n")
                file (APPEND ${EMB_C} "iBlock ${resName};\n")
            endforeach (fn)
            file (APPEND ${EMB_C} "\nstatic iBlock *blocks_Embed_[] = {\n")
            foreach (fn ${ARGV})
                embed_getname (resName ${fn})
                file (APPEND ${EMB_C} "   &${resName},\n")
            endforeach (fn)
            file (APPEND ${EMB_C} [[
};

iBool load_Embed(const char *path) {
    const size_t fileSize = (size_t) fileSizeCStr_FileInfo(path);
    iFile *f = iClob(newCStr_File(path));
    if (open_File(f, readOnly_FileMode)) {
        iForIndices(i, blocks_Embed_) {
            const iEmbedChunk *chunk = &chunks_Embed_[i];
            iBlock *data = blocks_Embed_[i];
            if (chunk->pos + chunk->size > fileSize) {
                return iFalse;
            }
            init_Block(data, chunk->size);
            seek_File(f, chunk->pos);
            readData_File(f, chunk->size, data_Block(data));
        }
        return iTrue;
    }
    return iFalse;
}
]])
        endif ()
    endif ()
endfunction (embed_make)
