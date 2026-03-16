/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * PARAM.SFO parser tests using a synthetic test file.
 */

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gtest/gtest.h"

extern "C"
{
#include "../tool/ps3/sfo.h"
}

class SfoFixture : public ::testing::Test
{
public:
    SfoFixture() = default;

protected:
    uint8_t *sfo_data     = nullptr;
    uint32_t sfo_data_len = 0;
    SfoFile  sfo;

    void SetUp() override
    {
        memset(&sfo, 0, sizeof(sfo));

        char path[PATH_MAX];
        char cwd[PATH_MAX];
        getcwd(cwd, PATH_MAX);
        snprintf(path, PATH_MAX, "%s/data/ps3_param.sfo", cwd);

        FILE *fp = fopen(path, "rb");
        ASSERT_NE(nullptr, fp) << "Cannot open " << path;

        fseek(fp, 0, SEEK_END);
        sfo_data_len = (uint32_t)ftell(fp);
        rewind(fp);

        sfo_data = static_cast<uint8_t *>(malloc(sfo_data_len));
        ASSERT_NE(nullptr, sfo_data);
        ASSERT_EQ(sfo_data_len, fread(sfo_data, 1, sfo_data_len, fp));
        fclose(fp);
    }

    void TearDown() override
    {
        ps3_free_sfo(&sfo);
        free(sfo_data);
    }

    ~SfoFixture() override = default;
};

/* Test: parse succeeds */
TEST_F(SfoFixture, ParseSucceeds)
{
    int32_t ret = ps3_parse_sfo(sfo_data, sfo_data_len, &sfo);
    ASSERT_EQ(0, ret);
    EXPECT_EQ(8u, sfo.entry_count);
}

/* Test: TITLE key */
TEST_F(SfoFixture, Title)
{
    ps3_parse_sfo(sfo_data, sfo_data_len, &sfo);
    const char *val = ps3_sfo_get_string(&sfo, "TITLE");
    ASSERT_NE(nullptr, val);
    EXPECT_STREQ("Test Game Title", val);
}

/* Test: TITLE_ID key */
TEST_F(SfoFixture, TitleId)
{
    ps3_parse_sfo(sfo_data, sfo_data_len, &sfo);
    const char *val = ps3_sfo_get_string(&sfo, "TITLE_ID");
    ASSERT_NE(nullptr, val);
    EXPECT_STREQ("BLES99999", val);
}

/* Test: CATEGORY key */
TEST_F(SfoFixture, Category)
{
    ps3_parse_sfo(sfo_data, sfo_data_len, &sfo);
    const char *val = ps3_sfo_get_string(&sfo, "CATEGORY");
    ASSERT_NE(nullptr, val);
    EXPECT_STREQ("DG", val);
}

/* Test: APP_VER key */
TEST_F(SfoFixture, AppVer)
{
    ps3_parse_sfo(sfo_data, sfo_data_len, &sfo);
    const char *val = ps3_sfo_get_string(&sfo, "APP_VER");
    ASSERT_NE(nullptr, val);
    EXPECT_STREQ("01.00", val);
}

/* Test: VERSION key */
TEST_F(SfoFixture, Version)
{
    ps3_parse_sfo(sfo_data, sfo_data_len, &sfo);
    const char *val = ps3_sfo_get_string(&sfo, "VERSION");
    ASSERT_NE(nullptr, val);
    EXPECT_STREQ("02.50", val);
}

/* Test: PS3_SYSTEM_VER key */
TEST_F(SfoFixture, SystemVer)
{
    ps3_parse_sfo(sfo_data, sfo_data_len, &sfo);
    const char *val = ps3_sfo_get_string(&sfo, "PS3_SYSTEM_VER");
    ASSERT_NE(nullptr, val);
    EXPECT_STREQ("04.21", val);
}

/* Test: non-existent key returns NULL */
TEST_F(SfoFixture, NonExistentKey)
{
    ps3_parse_sfo(sfo_data, sfo_data_len, &sfo);
    const char *val = ps3_sfo_get_string(&sfo, "NONEXISTENT");
    EXPECT_EQ(nullptr, val);
}

/* Test: NULL arguments return error */
TEST(SfoErrors, NullArgs)
{
    SfoFile sfo;
    uint8_t dummy[20] = {0};

    EXPECT_LT(ps3_parse_sfo(NULL, 10, &sfo), 0);
    EXPECT_LT(ps3_parse_sfo(dummy, 10, NULL), 0);
}

/* Test: too-small buffer returns error */
TEST(SfoErrors, TooSmall)
{
    SfoFile sfo;
    uint8_t buf[10] = {0};
    EXPECT_LT(ps3_parse_sfo(buf, 10, &sfo), 0);
}

/* Test: wrong magic returns error */
TEST(SfoErrors, WrongMagic)
{
    SfoFile sfo;
    uint8_t buf[20];
    memset(buf, 0, 20);
    buf[0] = 0xFF; /* bad magic */
    EXPECT_LT(ps3_parse_sfo(buf, 20, &sfo), 0);
}

/* Test: free on zeroed struct is safe */
TEST(SfoErrors, FreeZeroed)
{
    SfoFile sfo;
    memset(&sfo, 0, sizeof(sfo));
    ps3_free_sfo(&sfo); /* should not crash */
    ps3_free_sfo(NULL); /* should not crash */
}

/* Test: get_string with NULL sfo returns NULL */
TEST(SfoErrors, GetStringNull)
{
    EXPECT_EQ(nullptr, ps3_sfo_get_string(NULL, "TITLE"));

    SfoFile sfo;
    memset(&sfo, 0, sizeof(sfo));
    EXPECT_EQ(nullptr, ps3_sfo_get_string(&sfo, NULL));
}
