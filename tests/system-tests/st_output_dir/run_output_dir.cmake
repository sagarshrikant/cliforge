# =============================================================================
# run_output_dir.cmake — system test helper for -o / --output handling
#
# Called by CTest via:
#   cmake -DCLIFORGE_BIN=... -DSCHEMA_FILE=... -DOUT_DIR=... -P run_output_dir.cmake
#
# Regression guard for the bug where -o was honoured only when it appeared
# BEFORE the schema argument.  We require the output directory to be respected
# in every argument ordering:
#   1. <schema> -o <dir>        (option AFTER the file)
#   2. -o <dir> <schema>        (option BEFORE the file)
#   3. <schema> --output=<dir>  (joined long form, after the file)
#
# Crucially each run executes from a NEUTRAL working directory that is NOT the
# expected output dir.  If -o were ignored, the files would land in the cwd
# instead — so we assert both that they appear in <dir> AND that the cwd stays
# clean.  (The original test always passed -o before the file and ran from the
# output dir, which is exactly why it never caught the bug.)
# =============================================================================

foreach(VAR CLIFORGE_BIN SCHEMA_FILE OUT_DIR)
    if(NOT DEFINED ${VAR} OR "${${VAR}}" STREQUAL "")
        message(FATAL_ERROR "run_output_dir.cmake: ${VAR} is not set")
    endif()
endforeach()

# Neutral cwd: must differ from any expected output directory.
set(NEUTRAL_CWD "${OUT_DIR}/_cwd")

function(_expect_outputs LABEL DIR)
    file(REMOVE_RECURSE "${DIR}")
    file(MAKE_DIRECTORY "${DIR}")
    file(REMOVE_RECURSE "${NEUTRAL_CWD}")
    file(MAKE_DIRECTORY "${NEUTRAL_CWD}")

    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE RC
        OUTPUT_VARIABLE OUT
        ERROR_VARIABLE  ERR
        WORKING_DIRECTORY "${NEUTRAL_CWD}"
    )
    if(NOT RC EQUAL 0)
        message(FATAL_ERROR
            "[${LABEL}] cliforge exited ${RC}\nstdout: ${OUT}\nstderr: ${ERR}")
    endif()

    # The output dir must contain one of each generated file type.
    foreach(EXT h c md)
        file(GLOB FOUND "${DIR}/*.${EXT}")
        if(NOT FOUND)
            message(FATAL_ERROR
                "[${LABEL}] -o not honoured: no .${EXT} in ${DIR}\nstderr: ${ERR}")
        endif()
    endforeach()

    # The neutral cwd must NOT have received any generated files.
    file(GLOB LEAKED "${NEUTRAL_CWD}/*.h" "${NEUTRAL_CWD}/*.c"
                     "${NEUTRAL_CWD}/*.md")
    if(LEAKED)
        message(FATAL_ERROR
            "[${LABEL}] output leaked into cwd instead of ${DIR}: ${LEAKED}")
    endif()

    message(STATUS "st_output_dir [${LABEL}] PASS → ${DIR}")
endfunction()

_expect_outputs("after" "${OUT_DIR}/after"
    "${CLIFORGE_BIN}" "${SCHEMA_FILE}" -o "${OUT_DIR}/after")

_expect_outputs("before" "${OUT_DIR}/before"
    "${CLIFORGE_BIN}" -o "${OUT_DIR}/before" "${SCHEMA_FILE}")

_expect_outputs("equals" "${OUT_DIR}/equals"
    "${CLIFORGE_BIN}" "${SCHEMA_FILE}" "--output=${OUT_DIR}/equals")

message(STATUS "st_output_dir ALL PASS for ${SCHEMA_FILE}")
