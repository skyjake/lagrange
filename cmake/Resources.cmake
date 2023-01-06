find_program (ZIP_EXECUTABLE zip DOC "ZIP archiver")
if (NOT ZIP_EXECUTABLE)
    message (FATAL_ERROR "Please install 'zip' for packaging resources.")
endif ()

function (make_resources dst)
    list (REMOVE_AT ARGV 0)
    set (files)
    set (absfiles)
    foreach (arg ${ARGV})
        get_filename_component (name ${arg} NAME)
        if (NOT "${name}" MATCHES "^\\..*")
            if ("${arg}" MATCHES "^res/(.*)")
                list (APPEND files ${CMAKE_MATCH_1})
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
        COMMAND ${ZIP_EXECUTABLE} -1 ${dst} VERSION ${files}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/res
        OUTPUT_QUIET
    )
    execute_process (
        COMMAND ${ZIP_EXECUTABLE} -1 -j ${dst} ${absfiles}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/res
        OUTPUT_QUIET
    )
    file (REMOVE ${versionTempPath})
endfunction ()
