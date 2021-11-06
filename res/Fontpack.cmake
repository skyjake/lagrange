find_program (ZIP_EXECUTABLE zip DOC "ZIP archiver")
if (NOT ZIP_EXECUTABLE)
    message (FATAL_ERROR "Please install 'zip' to create fontpacks.")
endif ()

function (make_fontpack src)
    get_filename_component (dst ${src} NAME)
    set (fn ${CMAKE_BINARY_DIR}/${dst})
    execute_process (COMMAND ${CMAKE_COMMAND} -E remove ${fn})
    file (GLOB files RELATIVE ${CMAKE_SOURCE_DIR}/${src}
        ${CMAKE_SOURCE_DIR}/${src}/*
    )
    message (STATUS "  ${src}")
    execute_process (
        COMMAND ${ZIP_EXECUTABLE} -0 ${fn} ${files}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/${src}
        OUTPUT_QUIET
    )
endfunction ()
