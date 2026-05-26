# =============================================================================
#  tests/unit-tests/CMakeLists.txt
#
#  Helper macro that creates one gtest binary per ut_* directory,
#  discovers all TEST() cases automatically, and applies the "unit" label.
#
#  Each binary links against the cliforge_objects OBJECT library so it
#  gets the real production code (with coverage instrumentation when ON).
# =============================================================================

# ---------------------------------------------------------------------------
# Helper macro
#   cliforge_unit_test(TARGET  SOURCES...)
#   Creates a gtest executable, discovers tests, assigns label "unit".
# ---------------------------------------------------------------------------
macro(cliforge_unit_test TARGET)
    add_executable(${TARGET} ${ARGN})
    target_link_libraries(${TARGET} PRIVATE
        cliforge_objects
        GTest::gtest_main
    )
    target_include_directories(${TARGET} PRIVATE
        ${CLIFORGE_SRC_DIR}
        ${CLIFORGE_BUILD_DIR}
    )
    # C++17: enough for EXPECT_THAT, string_view, filesystem in tests
    set_target_properties(${TARGET} PROPERTIES CXX_STANDARD 17)

    gtest_discover_tests(${TARGET}
        PROPERTIES LABELS "unit;coverage"
    )
endmacro()

# ---------------------------------------------------------------------------
# ut_cf_util
# ---------------------------------------------------------------------------
cliforge_unit_test(ut_cf_util
    ut_cf_util/utest_cf_strlcpy.cpp
    ut_cf_util/utest_cf_unquote.cpp
    ut_cf_util/utest_cf_ident_to_c.cpp
    ut_cf_util/utest_cf_read_file.cpp
    ut_cf_util/utest_cf_str_eq_tok.cpp
)

# ---------------------------------------------------------------------------
# ut_cf_lex
# ---------------------------------------------------------------------------
cliforge_unit_test(ut_cf_lex
    ut_cf_lex/utest_cf_lex_run.cpp
)

# ---------------------------------------------------------------------------
# ut_cf_parse
# ---------------------------------------------------------------------------
cliforge_unit_test(ut_cf_parse
    ut_cf_parse/utest_cf_parse_meta.cpp
    ut_cf_parse/utest_cf_parse_option.cpp
    ut_cf_parse/utest_cf_parse_import.cpp
)

# ---------------------------------------------------------------------------
# ut_cf_gen
# ---------------------------------------------------------------------------
cliforge_unit_test(ut_cf_gen
    ut_cf_gen/utest_cf_generate.cpp
)
