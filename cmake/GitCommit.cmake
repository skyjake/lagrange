# Writes a header with the current Git commit. The file is only touched when the commit changes.
# Usage: cmake -DGIT_EXECUTABLE=<git> -DSOURCE_DIR=<root> -DOUTPUT=<file> -P GitCommit.cmake
# Source packages have no .git but may provide the commit in a GIT_COMMIT file.

set (_commit "")
if (EXISTS ${SOURCE_DIR}/GIT_COMMIT)
    file (READ ${SOURCE_DIR}/GIT_COMMIT _commit)
    string (STRIP "${_commit}" _commit)
elseif (GIT_EXECUTABLE AND EXISTS ${SOURCE_DIR}/.git)
    execute_process (
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=10 HEAD
        WORKING_DIRECTORY ${SOURCE_DIR}
        OUTPUT_VARIABLE _out
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _result
        ERROR_QUIET
    )
    if (_result EQUAL 0)
        set (_commit ${_out})
    endif ()
endif ()
set (_content "#define LAGRANGE_GIT_COMMIT \"${_commit}\"\n")
set (_old "")
if (EXISTS ${OUTPUT})
    file (READ ${OUTPUT} _old)
endif ()
if (NOT _content STREQUAL _old)
    file (WRITE ${OUTPUT} "${_content}")
endif ()
