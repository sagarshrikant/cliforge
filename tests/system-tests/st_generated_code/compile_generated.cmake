# =============================================================================
# compile_generated.cmake — compile generated cmdline.c under a C standard
#
# Arguments:
#   CLIFORGE_BIN   path to cliforge executable
#   GCC_BIN        path to gcc
#   SCHEMA_FILE    input .cf schema
#   OUT_DIR        where to write generated + compiled files
#   CSTD           C standard: 89, 99, or 11
# =============================================================================

foreach(VAR CLIFORGE_BIN GCC_BIN SCHEMA_FILE OUT_DIR CSTD)
    if(NOT DEFINED ${VAR} OR "${${VAR}}" STREQUAL "")
        message(FATAL_ERROR "compile_generated.cmake: ${VAR} is not set")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUT_DIR}")

# ---------------------------------------------------------------------------
# Step 1: Generate
# ---------------------------------------------------------------------------
execute_process(
    COMMAND "${CLIFORGE_BIN}" -o "${OUT_DIR}" "${SCHEMA_FILE}"
    RESULT_VARIABLE GEN_RC
    ERROR_VARIABLE  GEN_ERR
)
if(NOT GEN_RC EQUAL 0)
    message(FATAL_ERROR
        "cliforge failed (rc=${GEN_RC}) on ${SCHEMA_FILE}\n"
        "stderr: ${GEN_ERR}")
endif()

# ---------------------------------------------------------------------------
# Step 2: Find the generated .c file (name comes from meta.output in schema)
# ---------------------------------------------------------------------------
file(GLOB C_FILES "${OUT_DIR}/*.c")
if(NOT C_FILES)
    message(FATAL_ERROR "No .c file found in ${OUT_DIR} after generation")
endif()
# Take the first one (each test runs in its own OUT_DIR so there is only one)
list(GET C_FILES 0 C_FILE)

# ---------------------------------------------------------------------------
# Step 3: Compile
# ---------------------------------------------------------------------------
set(OBJ "${OUT_DIR}/out_c${CSTD}.o")

execute_process(
    COMMAND "${GCC_BIN}"
            -std=c${CSTD}
            -Wall -Wextra -Wpedantic
            -I "${OUT_DIR}"
            -c "${C_FILE}"
            -o "${OBJ}"
    RESULT_VARIABLE GCC_RC
    OUTPUT_VARIABLE GCC_OUT
    ERROR_VARIABLE  GCC_ERR
)
if(NOT GCC_RC EQUAL 0)
    message(FATAL_ERROR
        "gcc -std=c${CSTD} failed on ${C_FILE}\n"
        "${GCC_OUT}\n${GCC_ERR}")
endif()

message(STATUS
    "st_generated_code PASS: ${C_FILE} compiles clean under -std=c${CSTD}")
