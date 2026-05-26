# =============================================================================
#  examples/calctool/CMakeLists.txt
#
#  Full calctool example demonstrating cliforge multi-lib option passing.
#
#  Build targets:
#    arith_gen / cmp_gen / trig_gen / stats_gen / ops_linear_gen
#      — run cliforge on each lib's .cf to produce cmdline_<lib>.h/.c
#
#    arith_lib / cmp_lib       — static libraries
#    trig_lib  / stats_lib     — shared libraries  (-ltrig, -lstats)
#    ops_linear_plugin         — shared library loaded at runtime via dlopen
#
#    calctool                  — the example executable
#
#  Usage (standalone):
#    cmake -S . -B build -DCLIFORGE_EXECUTABLE=/path/to/cliforge
#    cmake --build build
#    ./build/calctool eval
#    ./build/calctool --arith.div-zero=nan eval
#    ./build/calctool --trig.units=degrees eval
#    ./build/calctool --plugin name=ops_linear,path=./build/libops_linear.so eval
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(calctool_example C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Shared libs need position-independent code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)
endif()

# ---------------------------------------------------------------------------
# Locate cliforge
# ---------------------------------------------------------------------------
if(NOT CLIFORGE_EXECUTABLE)
    find_program(CLIFORGE_EXECUTABLE cliforge
        HINTS "${CMAKE_BINARY_DIR}" "${CMAKE_BINARY_DIR}/../"
    )
endif()
if(NOT CLIFORGE_EXECUTABLE)
    message(FATAL_ERROR "cliforge not found. Pass -DCLIFORGE_EXECUTABLE=/path/to/cliforge")
endif()
message(STATUS "cliforge: ${CLIFORGE_EXECUTABLE}")

# ---------------------------------------------------------------------------
# Generated output directories
# ---------------------------------------------------------------------------
set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GEN_DIR}")

# ---------------------------------------------------------------------------
# Helper macro: run cliforge on one .cf and define a _gen target
#
#   cliforge_schema(<TARGET_NAME> <SCHEMA_FILE> <OUTPUT_STEM>)
#   Creates:
#     ${GEN_DIR}/<stem>.h  ${GEN_DIR}/<stem>.c  ${GEN_DIR}/<stem>.md
#   Plus a custom target named <TARGET_NAME> that depends on all three.
# ---------------------------------------------------------------------------
macro(cliforge_schema TARGET_NAME SCHEMA_FILE OUTPUT_STEM)
    set(_h   "${GEN_DIR}/${OUTPUT_STEM}.h")
    set(_c   "${GEN_DIR}/${OUTPUT_STEM}.c")
    set(_md  "${GEN_DIR}/${OUTPUT_STEM}.md")
    add_custom_command(
        OUTPUT "${_h}" "${_c}" "${_md}"
        COMMAND "${CLIFORGE_EXECUTABLE}" -o "${GEN_DIR}" "${SCHEMA_FILE}"
        DEPENDS "${SCHEMA_FILE}" "${CLIFORGE_EXECUTABLE}"
        COMMENT "cliforge: generating ${OUTPUT_STEM}.h/.c from ${SCHEMA_FILE}"
        VERBATIM
    )
    add_custom_target(${TARGET_NAME} DEPENDS "${_h}" "${_c}" "${_md}")
endmacro()

# ---------------------------------------------------------------------------
# 1. Generate parsers for every library schema
# ---------------------------------------------------------------------------
cliforge_schema(arith_gen
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/arith/arith.cf"
    "cmdline_arith")

cliforge_schema(cmp_gen
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/cmp/cmp.cf"
    "cmdline_cmp")

cliforge_schema(trig_gen
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/trig/trig.cf"
    "cmdline_trig")

cliforge_schema(stats_gen
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/stats/stats.cf"
    "cmdline_stats")

cliforge_schema(ops_linear_gen
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/ops_linear/ops_linear.cf"
    "cmdline_ops_linear")

# Generate main calctool schema
cliforge_schema(calctool_gen
    "${CMAKE_CURRENT_SOURCE_DIR}/calctool.cf"
    "cmdline")

# ---------------------------------------------------------------------------
# 2. arith — static library
# ---------------------------------------------------------------------------
add_library(arith_lib STATIC
    lib/arith/arith.c
    "${GEN_DIR}/cmdline_arith.c"
)
target_include_directories(arith_lib PUBLIC
    lib/arith
    "${GEN_DIR}"
)
add_dependencies(arith_lib arith_gen)

# ---------------------------------------------------------------------------
# 3. cmp — static library
# ---------------------------------------------------------------------------
add_library(cmp_lib STATIC
    lib/cmp/cmp.c
    "${GEN_DIR}/cmdline_cmp.c"
)
target_include_directories(cmp_lib PUBLIC
    lib/cmp
    "${GEN_DIR}"
)
add_dependencies(cmp_lib cmp_gen)

# ---------------------------------------------------------------------------
# 4. trig — shared library  (-ltrig)
# ---------------------------------------------------------------------------
add_library(trig_lib SHARED
    lib/trig/trig.c
    "${GEN_DIR}/cmdline_trig.c"
)
set_target_properties(trig_lib PROPERTIES OUTPUT_NAME "trig")
target_include_directories(trig_lib PUBLIC
    lib/trig
    "${GEN_DIR}"
)
target_link_libraries(trig_lib PRIVATE m)
add_dependencies(trig_lib trig_gen)

# ---------------------------------------------------------------------------
# 5. stats — shared library  (-lstats)
# ---------------------------------------------------------------------------
add_library(stats_lib SHARED
    lib/stats/stats.c
    "${GEN_DIR}/cmdline_stats.c"
)
set_target_properties(stats_lib PROPERTIES OUTPUT_NAME "stats")
target_include_directories(stats_lib PUBLIC
    lib/stats
    "${GEN_DIR}"
)
target_link_libraries(stats_lib PRIVATE m)
add_dependencies(stats_lib stats_gen)

# ---------------------------------------------------------------------------
# 6. ops_linear — shared library loaded at runtime via dlopen
# ---------------------------------------------------------------------------
add_library(ops_linear_plugin SHARED
    lib/ops_linear/ops_linear.c
    "${GEN_DIR}/cmdline_ops_linear.c"
)
set_target_properties(ops_linear_plugin PROPERTIES OUTPUT_NAME "ops_linear")
target_include_directories(ops_linear_plugin PUBLIC
    lib/ops_linear
    "${GEN_DIR}"
)
target_link_libraries(ops_linear_plugin PRIVATE m)
add_dependencies(ops_linear_plugin ops_linear_gen)

# ---------------------------------------------------------------------------
# 7. calctool executable
# ---------------------------------------------------------------------------
add_executable(calctool
    src/main.c
    "${GEN_DIR}/cmdline.c"
)

# CLIFORGE_INCLUDE_DIR is set by the parent CMakeLists when building in-tree.
# For standalone builds (after `make install`), the headers are already under
# /usr/include (the default system include path), so nothing extra is needed.
if(CLIFORGE_INCLUDE_DIR)
    set(_cf_inc "${CLIFORGE_INCLUDE_DIR}")
else()
    # Standalone: fall back to the system include root (already on the path,
    # but making it explicit avoids a hard-coded /usr/include).
    set(_cf_inc "")
endif()

target_include_directories(calctool PRIVATE
    src
    lib/arith
    lib/cmp
    lib/trig
    lib/stats
    lib/ops_linear
    "${GEN_DIR}"
    ${_cf_inc}          # <cliforge/cf_argv_slice.h>
)

target_link_libraries(calctool PRIVATE
    arith_lib          # static
    cmp_lib            # static
    trig_lib           # shared (-ltrig)
    stats_lib          # shared (-lstats)
    m                  # math
    ${CMAKE_DL_LIBS}   # -ldl for dlopen
)

add_dependencies(calctool calctool_gen)

# Make sure calctool can find the shared libs at runtime from the build dir
set_target_properties(calctool PROPERTIES
    BUILD_RPATH "${CMAKE_CURRENT_BINARY_DIR}"
)

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "
calctool example configured.

Build:    cmake --build build
Run:      ./build/calctool eval
Try also: ./build/calctool --arith.div-zero=nan eval
          ./build/calctool --trig.units=degrees eval
          ./build/calctool --stats.mode=population eval
          ./build/calctool --plugin name=ops_linear,path=./build/libops_linear.so eval
")
