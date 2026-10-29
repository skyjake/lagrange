# RESOURCES_ZIP_LEVEL is the zip compression level (-0 stores the files uncompressed).
function (make_resources dst)
    if (NOT DEFINED RESOURCES_ZIP_LEVEL)
        set (RESOURCES_ZIP_LEVEL -1)
    endif ()
    find_program (ZIP_EXECUTABLE zip DOC "ZIP archiver")
    if (NOT ZIP_EXECUTABLE)
        message (FATAL_ERROR "Please install 'zip' for packaging resources.")
    endif ()
    list (REMOVE_AT ARGV 0)
    set (files)
    set (genfiles)
    set (absfiles)
    set (deps)
    # Generated resources are zipped relative to this directory.
    set (genDir ${CMAKE_CURRENT_BINARY_DIR}/res/)
    string (LENGTH ${genDir} genDirLen)
    foreach (arg ${ARGV})
        get_filename_component (name ${arg} NAME)
        string (FIND "${arg}" "${genDir}" genPos)
        if (NOT "${name}" MATCHES "^\\..*")
            if ("${arg}" MATCHES "^res/(.*)")
                list (APPEND files ${CMAKE_MATCH_1})
                list (APPEND deps ${CMAKE_CURRENT_SOURCE_DIR}/${arg})
            elseif (genPos EQUAL 0)
                string (SUBSTRING "${arg}" ${genDirLen} -1 relPath)
                list (APPEND genfiles ${relPath})
            else ()
                list (APPEND absfiles ${arg})
                list (APPEND deps ${arg})
            endif ()
        endif ()
    endforeach (arg)
    # Resources are packaged during configuration, so editing one must trigger a reconfigure.
    set_property (DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${deps})
    file (REMOVE ${dst})
    get_filename_component (dstName ${dst} NAME)
    message (STATUS "  ${dstName}")
    set (versionTempPath ${CMAKE_CURRENT_SOURCE_DIR}/res/VERSION)
    file (WRITE ${versionTempPath} ${PROJECT_VERSION})
    execute_process (
        COMMAND ${ZIP_EXECUTABLE} ${RESOURCES_ZIP_LEVEL} -X ${dst} VERSION ${files}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/res
        OUTPUT_QUIET
    )
    if (genfiles)
        execute_process (
            COMMAND ${ZIP_EXECUTABLE} ${RESOURCES_ZIP_LEVEL} -X ${dst} ${genfiles}
            WORKING_DIRECTORY ${genDir}
            OUTPUT_QUIET
        )
    endif ()
    execute_process (
        COMMAND ${ZIP_EXECUTABLE} ${RESOURCES_ZIP_LEVEL} -X -j ${dst} ${absfiles}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/res
        OUTPUT_QUIET
    )
    file (REMOVE ${versionTempPath})
endfunction ()
