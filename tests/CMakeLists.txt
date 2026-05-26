# =============================================================================
#  tests/CMakeLists.txt
#
#  Test infrastructure for cliforge.
#
#  Layout:
#    unit-tests/   -- white-box gtest suites per source file
#    system-tests/ -- black-box tests spawning the cliforge binary
#
#  Labels (usable with ctest -L):
#    unit       -- fast, pure-C unit tests via gtest
#    system     -- black-box binary + generated-code tests
#    coverage   -- all tests; run under a coverage build
#
#  Coverage build:
#    cmake -S .. -B ../build-cov -DCLIFORGE_COVERAGE=ON -DCLIFORGE_BUILD_TESTS=ON
#    cmake --build ../build-cov
#    ctest --test-dir ../build-cov -L coverage
#    cmake --build ../build-cov --target cliforge_coverage
# =============================================================================

cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# Enable C++ for the test subtree only.
#
# The root CMakeLists.txt declares LANGUAGES C (tests are optional and the
# main tool is pure C).  gtest is a C++ framework, so we enable CXX here
# rather than forcing it on every build that has tests disabled.
# ---------------------------------------------------------------------------
enable_language(CXX)

# ---------------------------------------------------------------------------
# Google Test via FetchContent (pinned tag; no system dependency required)
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    # Keep gtest from polluting the install target
    EXCLUDE_FROM_ALL
)
# Prevent gtest from overriding our compiler/linker settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# Silence warnings inside googletest itself.
#
# Our project applies strict warning flags globally (via add_compile_options
# in the root CMakeLists.txt).  Those flags are inherited by FetchContent
# sub-projects, which causes two categories of noise:
#
#   1. C-only flags (-Wstrict-prototypes etc.) emitted on gtest's C++ files.
#   2. gtest's own template code triggering -Wdouble-promotion.
#
# Analogy: we enforce hard-hat rules in our own workshop, but we don't make
# the delivery driver wear one while dropping off a package at the door.
#
# Solution: pass -w (suppress all warnings) to the gtest targets only.
# Our production C code and our own test files still compile under full
# warning scrutiny.
# ---------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(_t gtest gtest_main)
        if(TARGET ${_t})
            target_compile_options(${_t} PRIVATE -w)
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Enable CTest
# enable_testing() is called in the root CMakeLists.txt so that ctest finds
# all tests when run from the root build directory.  We just load the
# GoogleTest helper module here.
# ---------------------------------------------------------------------------
include(GoogleTest)

# ---------------------------------------------------------------------------
# cliforge source objects (compiled once, shared by all unit test binaries)
#
# We compile the four cliforge source files as an OBJECT library so each
# test binary links against the same translation units.  The INTERFACE
# include directories propagate automatically to every consumer.
# ---------------------------------------------------------------------------
add_library(cliforge_objects OBJECT
    ${CLIFORGE_SRC_DIR}/cf_lex.c
    ${CLIFORGE_SRC_DIR}/cf_parse.c
    ${CLIFORGE_SRC_DIR}/cf_gen.c
    ${CLIFORGE_SRC_DIR}/cf_util.c
)
target_include_directories(cliforge_objects PUBLIC
    ${CLIFORGE_SRC_DIR}
    ${CLIFORGE_BUILD_DIR}       # cliforge_version.h
)

# Apply coverage flags to the object library when the coverage preset is on
if(CLIFORGE_COVERAGE)
    target_compile_options(cliforge_objects PUBLIC
        -O0 -g --coverage -fprofile-arcs -ftest-coverage
    )
    target_link_options(cliforge_objects PUBLIC --coverage)
endif()

# ---------------------------------------------------------------------------
# Sub-directories
# ---------------------------------------------------------------------------
add_subdirectory(unit-tests)
add_subdirectory(system-tests)

# ---------------------------------------------------------------------------
# Coverage target  (only created when -DCLIFORGE_COVERAGE=ON)
# ---------------------------------------------------------------------------
if(CLIFORGE_COVERAGE)
    find_program(LCOV_PATH  lcov  REQUIRED)
    find_program(GENHTML_PATH genhtml REQUIRED)

    # Output paths
    set(COVERAGE_DIR  "${CMAKE_BINARY_DIR}/coverage-report")
    set(COVERAGE_INFO "${CMAKE_BINARY_DIR}/coverage.info")
    set(COVERAGE_CLEAN "${CMAKE_BINARY_DIR}/coverage-clean.info")

    add_custom_target(cliforge_coverage
        COMMENT "Generating lcov/genhtml coverage report..."

        # 1. Reset counters
        COMMAND ${LCOV_PATH} --directory ${CMAKE_BINARY_DIR}
                             --zerocounters
                             --quiet

        # 2. Run all tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L coverage

        # 3. Capture coverage data
        COMMAND ${LCOV_PATH} --directory ${CMAKE_BINARY_DIR}
                             --capture
                             --output-file ${COVERAGE_INFO}
                             --quiet

        # 4. Remove noise: system headers, gtest internals, generated cmdline files
        COMMAND ${LCOV_PATH} --remove ${COVERAGE_INFO}
                             "/usr/*"
                             "*/googletest/*"
                             "*/googlemock/*"
                             "*/tests/*"
                             "*/build*/*"
                             "*/cliforge_version.h"
                             --output-file ${COVERAGE_CLEAN}
                             --quiet

        # 5. Generate HTML
        COMMAND ${GENHTML_PATH} ${COVERAGE_CLEAN}
                                --output-directory ${COVERAGE_DIR}
                                --title "cliforge coverage"
                                --quiet

        # 6. Print summary (fails the target if lines < 100%)
        COMMAND ${LCOV_PATH} --summary ${COVERAGE_CLEAN}

        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()
