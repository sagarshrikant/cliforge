cmake_minimum_required(VERSION 3.16)
project(cliforge VERSION 0.4.0 LANGUAGES C)

# Emit compile_commands.json so editors/IntelliSense (and clang-tidy) resolve
# includes and macros accurately.  Harmless for non-IDE builds.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# C standard — C99 for the tool itself; generated code targets C89/C99/C11.
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Compiler warnings (GCC / Clang)
# ---------------------------------------------------------------------------
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # Flags valid for both C and C++ — applied globally.
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wdouble-promotion
        -Wformat=2
        -Wmissing-declarations
    )
    # C-only flags — applied via a generator expression so they are silently
    # ignored on C++ translation units (gtest, test files).
    # Analogy: a "C speakers only" label on a conference room — C++ files
    # walk past without triggering a complaint.
    add_compile_options(
        $<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>
        $<$<COMPILE_LANGUAGE:C>:-Wmissing-prototypes>
        $<$<COMPILE_LANGUAGE:C>:-Wold-style-definition>
    )
endif()

# ---------------------------------------------------------------------------
# Build type defaults
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
set(CLIFORGE_SOURCES
    src/main.c
    src/cf_lex.c
    src/cf_parse.c
    src/cf_gen.c
    src/cf_util.c
)

add_executable(cliforge ${CLIFORGE_SOURCES})
target_include_directories(cliforge PRIVATE src)

# When building for coverage the binary itself must also carry instrumentation
# so that system tests (which invoke the cliforge binary directly) produce
# .gcda files alongside the unit-test .gcda files.
if(CLIFORGE_COVERAGE)
    target_compile_options(cliforge PRIVATE
        -O0 -g --coverage -fprofile-arcs -ftest-coverage
    )
    target_link_options(cliforge PRIVATE --coverage)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS cliforge RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Install SDK headers (cf_argv_slice.h and any future cliforge/*.h).
# Result: /usr/include/cliforge/cf_argv_slice.h
install(DIRECTORY include/cliforge
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h")

# Expose include dir to subdirectories (e.g. examples) during in-tree builds.
# After install, /usr/include is already on the default include path.
set(CLIFORGE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include"
    CACHE INTERNAL "cliforge SDK include directory")

# ---------------------------------------------------------------------------
# Version header
# ---------------------------------------------------------------------------
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cliforge_version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cliforge_version.h"
    @ONLY
)
target_include_directories(cliforge PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# ---------------------------------------------------------------------------
# Examples (optional — skip with -DCLIFORGE_BUILD_EXAMPLES=OFF)
# ---------------------------------------------------------------------------
option(CLIFORGE_BUILD_EXAMPLES "Build example projects" ON)

if(CLIFORGE_BUILD_EXAMPLES)
    # Pass the build-tree binary path so the example CMakeLists doesn't
    # have to search for it.  $<TARGET_FILE:cliforge> resolves at build time.
    set(CLIFORGE_EXECUTABLE "$<TARGET_FILE:cliforge>" CACHE INTERNAL "")
    add_subdirectory(examples/calctool)
    # Make the example depend on the generator so it is always built first.
    add_dependencies(calctool_gen cliforge)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
option(CLIFORGE_BUILD_TESTS "Build unit and system tests (requires C++17)" OFF)
option(CLIFORGE_COVERAGE    "Build with gcov/lcov coverage instrumentation" OFF)

if(CLIFORGE_BUILD_TESTS)
    # enable_testing() MUST be called from the root CMakeLists so that ctest
    # launched in the root build directory can find all registered tests.
    enable_testing()

    # Pass source and build dirs so tests/CMakeLists.txt can find headers
    set(CLIFORGE_SRC_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/src"
        CACHE INTERNAL "cliforge source directory")
    set(CLIFORGE_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}"
        CACHE INTERNAL "cliforge build directory (for generated headers)")
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "cliforge ${PROJECT_VERSION}  build=${CMAKE_BUILD_TYPE}  cc=${CMAKE_C_COMPILER_ID}")
if(CLIFORGE_BUILD_EXAMPLES)
    message(STATUS "  examples: ON  (disable with -DCLIFORGE_BUILD_EXAMPLES=OFF)")
endif()
