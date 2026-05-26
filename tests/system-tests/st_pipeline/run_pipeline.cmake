# =============================================================================
# run_pipeline.cmake — system test helper
#
# Called by CTest via:
#   cmake -DCLIFORGE_BIN=... -DSCHEMA_FILE=... -DOUT_DIR=... -P run_pipeline.cmake
#
# What it checks:
#   1. cliforge exits 0 on a valid schema.
#   2. At least one .h, one .c, and one .md file were produced in OUT_DIR.
#      (We do NOT assume a fixed base name — each schema sets its own output.)
# =============================================================================

foreach(VAR CLIFORGE_BIN SCHEMA_FILE OUT_DIR)
    if(NOT DEFINED ${VAR} OR "${${VAR}}" STREQUAL "")
        message(FATAL_ERROR "run_pipeline.cmake: ${VAR} is not set")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUT_DIR}")

# ---------------------------------------------------------------------------
# Step 1: Run cliforge
# ---------------------------------------------------------------------------
execute_process(
    COMMAND "${CLIFORGE_BIN}" -o "${OUT_DIR}" "${SCHEMA_FILE}"
    RESULT_VARIABLE CLIFORGE_RC
    OUTPUT_VARIABLE CLIFORGE_OUT
    ERROR_VARIABLE  CLIFORGE_ERR
)

if(NOT CLIFORGE_RC EQUAL 0)
    message(FATAL_ERROR
        "cliforge exited with code ${CLIFORGE_RC} on ${SCHEMA_FILE}\n"
        "stdout: ${CLIFORGE_OUT}\n"
        "stderr: ${CLIFORGE_ERR}")
endif()

# ---------------------------------------------------------------------------
# Step 2: At least one file of each type must exist and be non-empty.
#         We glob for them so the test is independent of meta.output value.
# ---------------------------------------------------------------------------
foreach(EXT h c md)
    file(GLOB FOUND_FILES "${OUT_DIR}/*.${EXT}")
    if(NOT FOUND_FILES)
        message(FATAL_ERROR
            "No .${EXT} file produced in ${OUT_DIR} for schema ${SCHEMA_FILE}\n"
            "cliforge stdout: ${CLIFORGE_OUT}\n"
            "cliforge stderr: ${CLIFORGE_ERR}")
    endif()
    foreach(F ${FOUND_FILES})
        file(SIZE "${F}" FSIZE)
        if(FSIZE EQUAL 0)
            message(FATAL_ERROR "Output file is empty: ${F}")
        endif()
    endforeach()
endforeach()

message(STATUS "st_pipeline PASS: ${SCHEMA_FILE} → ${OUT_DIR}/")
