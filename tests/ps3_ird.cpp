/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * IRD parser tests using a real BLES-00905.ird file (Fallout: New Vegas).
 */

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "gtest/gtest.h"

extern "C"
{
#include "../tool/ird.h"
}

class IrdFixture : public ::testing::Test
{
public:
    IrdFixture() = default;

protected:
    char    ird_path[PATH_MAX];
    IrdFile ird;

    void SetUp() override
    {
        char cwd[PATH_MAX];
        getcwd(cwd, PATH_MAX);
        snprintf(ird_path, PATH_MAX, "%s/data/BLES-00905.ird", cwd);
        memset(&ird, 0, sizeof(ird));
    }

    void TearDown() override { ps3_free_ird(&ird); }

    ~IrdFixture() override = default;
};

/* Test: parse succeeds */
TEST_F(IrdFixture, ParseSucceeds)
{
    int32_t ret = ps3_parse_ird(ird_path, &ird);
    ASSERT_EQ(0, ret);
    EXPECT_TRUE(ird.valid);
}

/* Test: version is 9 */
TEST_F(IrdFixture, Version)
{
    ps3_parse_ird(ird_path, &ird);
    EXPECT_EQ(9, ird.version);
}

/* Test: game ID */
TEST_F(IrdFixture, GameId)
{
    ps3_parse_ird(ird_path, &ird);
    EXPECT_STREQ("BLES00905", ird.game_id);
}

/* Test: game name */
TEST_F(IrdFixture, GameName)
{
    ps3_parse_ird(ird_path, &ird);
    EXPECT_STREQ("Fallout: New Vegas", ird.game_name);
}

/* Test: update/system version */
TEST_F(IrdFixture, UpdateVer)
{
    ps3_parse_ird(ird_path, &ird);
    EXPECT_STREQ("3.41", ird.update_ver);
}

/* Test: game version */
TEST_F(IrdFixture, GameVer)
{
    ps3_parse_ird(ird_path, &ird);
    EXPECT_STREQ("01.00", ird.game_ver);
}

/* Test: app version */
TEST_F(IrdFixture, AppVer)
{
    ps3_parse_ird(ird_path, &ird);
    EXPECT_STREQ("01.00", ird.app_ver);
}

/* Test: has PIC data */
TEST_F(IrdFixture, HasPic)
{
    ps3_parse_ird(ird_path, &ird);
    EXPECT_TRUE(ird.has_pic);

    /* PIC should not be all zeros */
    uint8_t zeros[115] = {0};
    EXPECT_NE(0, memcmp(ird.pic, zeros, 115));
}

/* Test: d1 key is not all zeros */
TEST_F(IrdFixture, D1NotZero)
{
    ps3_parse_ird(ird_path, &ird);

    uint8_t zeros[16] = {0};
    EXPECT_NE(0, memcmp(ird.d1, zeros, 16));
}

/* Test: d2 key is not all zeros */
TEST_F(IrdFixture, D2NotZero)
{
    ps3_parse_ird(ird_path, &ird);

    uint8_t zeros[16] = {0};
    EXPECT_NE(0, memcmp(ird.d2, zeros, 16));
}

/* Test: header gz data present */
TEST_F(IrdFixture, HeaderGzPresent)
{
    ps3_parse_ird(ird_path, &ird);
    EXPECT_NE(nullptr, ird.header_gz);
    EXPECT_GT(ird.header_gz_len, 0u);
}

/* Test: footer gz data present */
TEST_F(IrdFixture, FooterGzPresent)
{
    ps3_parse_ird(ird_path, &ird);
    EXPECT_NE(nullptr, ird.footer_gz);
    EXPECT_GT(ird.footer_gz_len, 0u);
}

/* Test: NULL path returns error */
TEST(IrdErrors, NullPath)
{
    IrdFile ird;
    EXPECT_LT(ps3_parse_ird(NULL, &ird), 0);
}

/* Test: non-existent file returns error */
TEST(IrdErrors, NonExistentFile)
{
    IrdFile ird;
    EXPECT_LT(ps3_parse_ird("/tmp/nonexistent_ird_file_12345.ird", &ird), 0);
}

/* Test: free on zeroed struct is safe */
TEST(IrdErrors, FreeZeroed)
{
    IrdFile ird;
    memset(&ird, 0, sizeof(ird));
    ps3_free_ird(&ird); /* should not crash */
    ps3_free_ird(NULL); /* should not crash */
}
