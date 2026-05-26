# =============================================================================
# run_error_case.cmake — system test helper for error-injection cases
#
# Called by CTest via:
#   cmake -DCLIFORGE_BIN=... -DSCHEMA_FILE=... -DOUT_DIR=... -P run_error_case.cmake
#
# What it checks:
#   1. cliforge exits with a NON-ZERO code on the deliberately bad schema.
#   2. stderr is non-empty (cliforge must print something useful).
# =============================================================================

foreach(VAR CLIFORGE_BIN SCHEMA_FILE OUT_DIR)
    if(NOT DEFINED ${VAR} OR "${${VAR}}" STREQUAL "")
        message(FATAL_ERROR "run_error_case.cmake: ${VAR} is not set")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUT_DIR}")

execute_process(
    COMMAND "${CLIFORGE_BIN}" -o "${OUT_DIR}" "${SCHEMA_FILE}"
    RESULT_VARIABLE CLIFORGE_RC
    OUTPUT_VARIABLE CLIFORGE_OUT
    ERROR_VARIABLE  CLIFORGE_ERR
)

# --------------------------------------------------------------------------
# PASS condition: cliforge must NOT exit 0 on a bad schema
# --------------------------------------------------------------------------
if(CLIFORGE_RC EQUAL 0)
    message(FATAL_ERROR
        "cliforge exited 0 (success) on deliberately bad schema: ${SCHEMA_FILE}\n"
        "Expected a non-zero exit code.\n"
        "stdout: ${CLIFORGE_OUT}\n"
        "stderr: ${CLIFORGE_ERR}")
endif()

# --------------------------------------------------------------------------
# cliforge should print something to stderr
# --------------------------------------------------------------------------
if("${CLIFORGE_ERR}" STREQUAL "")
    message(FATAL_ERROR
        "cliforge produced no stderr output for bad schema: ${SCHEMA_FILE}\n"
        "Expected an error message.")
endif()

message(STATUS "st_error PASS: cliforge correctly rejected ${SCHEMA_FILE} "
               "(exit ${CLIFORGE_RC})")
