/* ============================================================================
 * utest_cf_read_file.cpp — unit tests for cf_read_file()
 *
 * cf_read_file(path, &out_len):
 *   Reads entire file into a malloc'd buffer.
 *   Returns NULL on error (file not found, permission, etc.).
 *   Sets *out_len to byte count (not including any implicit NUL).
 * ========================================================================= */
#include <gtest/gtest.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>

extern "C" {
#include "cf_util.h"
}

/* Non-existent file returns NULL. */
TEST(CfReadFile, NonExistent_001)
{
    unsigned int len = 99;
    char *buf = cf_read_file("/tmp/cliforge_test_nonexistent_xyz.cf", &len);
    EXPECT_EQ(buf, nullptr);
}

/* Write a known file, read it back, verify contents. */
TEST(CfReadFile, ReadContents_002)
{
    const char *path = "/tmp/cliforge_ut_read_002.txt";
    const char *content = "hello cliforge\n";
    FILE *f = fopen(path, "w");
    ASSERT_NE(f, nullptr);
    fputs(content, f);
    fclose(f);

    unsigned int len = 0;
    char *buf = cf_read_file(path, &len);
    ASSERT_NE(buf, nullptr);
    EXPECT_EQ(len, (unsigned int)strlen(content));
    EXPECT_EQ(memcmp(buf, content, len), 0);
    free(buf);
    remove(path);
}

/* Empty file: returns valid (non-NULL) buffer, len == 0. */
TEST(CfReadFile, EmptyFile_003)
{
    const char *path = "/tmp/cliforge_ut_read_003.txt";
    FILE *f = fopen(path, "w");
    ASSERT_NE(f, nullptr);
    fclose(f);

    unsigned int len = 99;
    char *buf = cf_read_file(path, &len);
    ASSERT_NE(buf, nullptr);
    EXPECT_EQ(len, 0u);
    free(buf);
    remove(path);
}
