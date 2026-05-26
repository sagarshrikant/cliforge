# =============================================================================
#  tests/system-tests/CMakeLists.txt
#
#  Black-box tests that spawn the cliforge binary, inspect its output files,
#  and optionally compile the generated C code under C89 / C99 / C11.
#
#  Layout:
#    st_pipeline/       -- golden-file checks: generate from a .cf, diff output
#    st_error_cases/    -- inject bad schemas and verify non-zero exit codes
#    st_generated_code/ -- compile generated cmdline.c under multiple C standards
#
#  Prerequisites:
#    CLIFORGE_BINARY    -- path to the cliforge executable (set by root CMake)
#    GCC_PATH           -- detected gcc binary (set by root CMake)
#    FIXTURE_DIR        -- path to tests/system-tests/fixtures/schemas/
# =============================================================================

cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# Locate required tools
# ---------------------------------------------------------------------------

# cliforge binary (built in this same build tree)
if(NOT DEFINED CLIFORGE_BINARY OR CLIFORGE_BINARY STREQUAL "")
    set(CLIFORGE_BINARY "$<TARGET_FILE:cliforge>")
endif()

# GCC — required for C-standard compilation tests
find_program(GCC_PATH gcc REQUIRED)
message(STATUS "  system-tests: gcc found at ${GCC_PATH}")

# Fixture schema directory
set(FIXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/schemas")

# Temporary output root: system tests write into build/system-test-outputs/
set(ST_OUTPUT_DIR "${CMAKE_BINARY_DIR}/system-test-outputs")

# ---------------------------------------------------------------------------
# Helper macro: add_system_test(NAME COMMAND...)
#
# Wraps add_test() and assigns the "system" and "coverage" labels so the
# test participates in ctest -L system and the coverage target.
# ---------------------------------------------------------------------------
macro(add_system_test NAME)
    add_test(NAME ${NAME} ${ARGN})
    set_tests_properties(${NAME} PROPERTIES LABELS "system;coverage")
endmacro()

# ---------------------------------------------------------------------------
# st_pipeline — generate from good schemas, verify zero exit and file presence
# ---------------------------------------------------------------------------

foreach(SCHEMA minimal all_types with_section with_import)
    set(_OUT "${ST_OUTPUT_DIR}/st_pipeline/${SCHEMA}")
    add_system_test(st_pipeline_${SCHEMA}
        COMMAND ${CMAKE_COMMAND}
            -DCLIFORGE_BIN=${CLIFORGE_BINARY}
            -DSCHEMA_FILE=${FIXTURE_DIR}/${SCHEMA}.cf
            -DOUT_DIR=${_OUT}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/st_pipeline/run_pipeline.cmake
    )
endforeach()

# ---------------------------------------------------------------------------
# st_error_cases — bad schemas must produce non-zero exit codes
# ---------------------------------------------------------------------------

foreach(SCHEMA bad_no_meta bad_unknown_type bad_dup_option bad_import_missing)
    add_system_test(st_error_${SCHEMA}
        COMMAND ${CMAKE_COMMAND}
            -DCLIFORGE_BIN=${CLIFORGE_BINARY}
            -DSCHEMA_FILE=${FIXTURE_DIR}/${SCHEMA}.cf
            -DOUT_DIR=${ST_OUTPUT_DIR}/st_errors/${SCHEMA}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/st_error_cases/run_error_case.cmake
    )
endforeach()

# ---------------------------------------------------------------------------
# st_generated_code — compile generated cmdline.c under C89 / C99 / C11
# ---------------------------------------------------------------------------

foreach(STD 89 99 11)
    set(_OUT "${ST_OUTPUT_DIR}/st_generated_code/c${STD}")
    add_system_test(st_generated_code_c${STD}
        COMMAND ${CMAKE_COMMAND}
            -DCLIFORGE_BIN=${CLIFORGE_BINARY}
            -DGCC_BIN=${GCC_PATH}
            -DSCHEMA_FILE=${FIXTURE_DIR}/all_types.cf
            -DOUT_DIR=${_OUT}
            -DCSTD=${STD}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/st_generated_code/compile_generated.cmake
    )
endforeach()
