function (make_resources dst)
    find_program (ZIP_EXECUTABLE zip DOC "ZIP archiver")
    if (NOT ZIP_EXECUTABLE)
        message (FATAL_ERROR "Please install 'zip' for packaging resources.")
    endif ()
    list (REMOVE_AT ARGV 0)
    set (files)
    set (genfiles)
    set (absfiles)
    # Generated resources are zipped relative to this directory.
    set (genDir ${CMAKE_CURRENT_BINARY_DIR}/res/)
    string (LENGTH ${genDir} genDirLen)
    foreach (arg ${ARGV})
        get_filename_component (name ${arg} NAME)
        string (FIND "${arg}" "${genDir}" genPos)
        if (NOT "${name}" MATCHES "^\\..*")
            if ("${arg}" MATCHES "^res/(.*)")
                list (APPEND files ${CMAKE_MATCH_1})
            elseif (genPos EQUAL 0)
                string (SUBSTRING "${arg}" ${genDirLen} -1 relPath)
                list (APPEND genfiles ${relPath})
            else ()
                set (absfiles ${arg})
            endif ()
        endif ()
    endforeach (arg)
    file (REMOVE ${dst})
    get_filename_component (dstName ${dst} NAME)
    message (STATUS "  ${dstName}")
    set (versionTempPath ${CMAKE_CURRENT_SOURCE_DIR}/res/VERSION)
    file (WRITE ${versionTempPath} ${PROJECT_VERSION})
    execute_process (
        COMMAND ${ZIP_EXECUTABLE} -1 -X ${dst} VERSION ${files}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/res
        OUTPUT_QUIET
    )
    if (genfiles)
        execute_process (
            COMMAND ${ZIP_EXECUTABLE} -1 -X ${dst} ${genfiles}
            WORKING_DIRECTORY ${genDir}
            OUTPUT_QUIET
        )
    endif ()
    execute_process (
        COMMAND ${ZIP_EXECUTABLE} -1 -X -j ${dst} ${absfiles}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/res
        OUTPUT_QUIET
    )
    file (REMOVE ${versionTempPath})
endfunction ()
