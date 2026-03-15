/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * Minimal ISO 9660 reader tests using a synthetic test ISO.
 */

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gtest/gtest.h"

extern "C"
{
#include "../tool/iso9660_mini.h"
#include "../tool/sfo.h"
}

/* In-memory ISO image for testing */
static uint8_t *iso_data   = nullptr;
static uint32_t iso_size   = 0;
static bool     iso_loaded = false;

static void load_iso()
{
    if(iso_loaded) return;

    char path[PATH_MAX];
    getcwd(path, PATH_MAX);
    char filename[PATH_MAX];
    snprintf(filename, PATH_MAX, "%s/data/ps3_test.iso", path);

    FILE *f = fopen(filename, "rb");
    if(f == nullptr) return;

    fseek(f, 0, SEEK_END);
    iso_size = (uint32_t)ftell(f);
    rewind(f);

    iso_data = (uint8_t *)malloc(iso_size);
    if(iso_data != nullptr) fread(iso_data, 1, iso_size, f);
    fclose(f);
    iso_loaded = true;
}

/* Sector read callback: reads from in-memory ISO */
static int32_t test_read_sector(void *user_data, uint64_t sector, uint8_t *buffer)
{
    (void)user_data;
    uint64_t offset = sector * ISO9660_SECTOR_SIZE;

    if(offset + ISO9660_SECTOR_SIZE > iso_size) return -1;

    memcpy(buffer, iso_data + offset, ISO9660_SECTOR_SIZE);
    return 0;
}

class Iso9660Fixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        load_iso();
        ASSERT_NE(nullptr, iso_data) << "Cannot load ps3_test.iso";
    }
};

/* Test: read PARAM.SFO from /PS3_GAME/PARAM.SFO */
TEST_F(Iso9660Fixture, ReadParamSfo)
{
    uint8_t *data   = nullptr;
    uint32_t length = 0;

    int32_t ret = iso9660_read_file(test_read_sector, nullptr, "/PS3_GAME/PARAM.SFO", &data, &length);
    ASSERT_EQ(0, ret);
    ASSERT_NE(nullptr, data);
    EXPECT_GT(length, 0u);

    /* The file should be a valid SFO */
    SfoFile sfo;
    ret = ps3_parse_sfo(data, length, &sfo);
    EXPECT_EQ(0, ret);
    EXPECT_GT(sfo.entry_count, 0u);

    /* Verify a known field */
    const char *title = ps3_sfo_get_string(&sfo, "TITLE");
    EXPECT_NE(nullptr, title);
    if(title != nullptr) EXPECT_STREQ("Test Game Title", title);

    ps3_free_sfo(&sfo);
    free(data);
}

/* Test: path without leading slash */
TEST_F(Iso9660Fixture, ReadWithoutLeadingSlash)
{
    uint8_t *data   = nullptr;
    uint32_t length = 0;

    int32_t ret = iso9660_read_file(test_read_sector, nullptr, "PS3_GAME/PARAM.SFO", &data, &length);
    ASSERT_EQ(0, ret);
    ASSERT_NE(nullptr, data);
    EXPECT_GT(length, 0u);
    free(data);
}

/* Test: case insensitive path */
TEST_F(Iso9660Fixture, CaseInsensitive)
{
    uint8_t *data   = nullptr;
    uint32_t length = 0;

    int32_t ret = iso9660_read_file(test_read_sector, nullptr, "/ps3_game/param.sfo", &data, &length);
    ASSERT_EQ(0, ret);
    ASSERT_NE(nullptr, data);
    EXPECT_GT(length, 0u);
    free(data);
}

/* Test: nonexistent file returns error */
TEST_F(Iso9660Fixture, FileNotFound)
{
    uint8_t *data   = nullptr;
    uint32_t length = 0;

    int32_t ret = iso9660_read_file(test_read_sector, nullptr, "/NONEXISTENT/FILE.TXT", &data, &length);
    EXPECT_LT(ret, 0);
    EXPECT_EQ(nullptr, data);
}

/* Test: nonexistent directory returns error */
TEST_F(Iso9660Fixture, DirNotFound)
{
    uint8_t *data   = nullptr;
    uint32_t length = 0;

    int32_t ret = iso9660_read_file(test_read_sector, nullptr, "/PS3_GAME/NONEXISTENT.BIN", &data, &length);
    EXPECT_LT(ret, 0);
}

/* Test: NULL arguments */
TEST(Iso9660Errors, NullArgs)
{
    uint8_t *data;
    uint32_t length;

    EXPECT_LT(iso9660_read_file(nullptr, nullptr, "/test", &data, &length), 0);
    EXPECT_LT(iso9660_read_file(test_read_sector, nullptr, nullptr, &data, &length), 0);
    EXPECT_LT(iso9660_read_file(test_read_sector, nullptr, "/test", nullptr, &length), 0);
    EXPECT_LT(iso9660_read_file(test_read_sector, nullptr, "/test", &data, nullptr), 0);
}

/* Test: sector read failure */
static int32_t fail_read_sector(void * /*user_data*/, uint64_t /*sector*/, uint8_t * /*buffer*/) { return -1; }

TEST(Iso9660Errors, ReadFailure)
{
    uint8_t *data   = nullptr;
    uint32_t length = 0;

    int32_t ret = iso9660_read_file(fail_read_sector, nullptr, "/PS3_GAME/PARAM.SFO", &data, &length);
    EXPECT_LT(ret, 0);
}
