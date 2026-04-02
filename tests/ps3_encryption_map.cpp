/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * PS3 encryption map parsing, serialization, and sector lookup tests.
 */

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "gtest/gtest.h"

extern "C"
{
#include "../src/ps3/ps3_encryption_map.h"
}

/*
 * Build a synthetic sector 0 in big-endian format:
 *   offset 0x00: region_count = 2 (BE)
 *   offset 0x04: unknown = 0 (BE)
 *   offset 0x08: region[0].start = 0 (BE)
 *   offset 0x0C: region[0].end = 31 (BE)
 *   offset 0x10: region[1].start = 256 (BE)
 *   offset 0x14: region[1].end = 511 (BE)
 */
static void build_synthetic_sector0(uint8_t *buf, uint32_t size)
{
    memset(buf, 0, size);

    /* region_count = 2, big-endian */
    buf[0] = 0;
    buf[1] = 0;
    buf[2] = 0;
    buf[3] = 2;

    /* unknown field */
    buf[4] = 0;
    buf[5] = 0;
    buf[6] = 0;
    buf[7] = 0;

    /* region[0]: start=0, end=31 */
    buf[8]  = 0;
    buf[9]  = 0;
    buf[10] = 0;
    buf[11] = 0; /* start=0 */
    buf[12] = 0;
    buf[13] = 0;
    buf[14] = 0;
    buf[15] = 31; /* end=31 */

    /* region[1]: start=256, end=511 */
    buf[16] = 0;
    buf[17] = 0;
    buf[18] = 1;
    buf[19] = 0; /* start=256 */
    buf[20] = 0;
    buf[21] = 0;
    buf[22] = 1;
    buf[23] = 0xFF; /* end=511 */
}

/* Test: parse valid sector 0 */
TEST(PS3EncMap, ParseValid)
{
    uint8_t sector0[2048];
    build_synthetic_sector0(sector0, 2048);

    Ps3PlaintextRegion *regions = NULL;
    uint32_t            count   = 0;

    int32_t ret = ps3_parse_encryption_map(sector0, 2048, &regions, &count);
    ASSERT_EQ(0, ret);
    ASSERT_EQ(2u, count);
    ASSERT_NE(nullptr, regions);

    EXPECT_EQ(0u, regions[0].start_sector);
    EXPECT_EQ(31u, regions[0].end_sector);
    EXPECT_EQ(256u, regions[1].start_sector);
    EXPECT_EQ(511u, regions[1].end_sector);

    free(regions);
}

/* Test: parse zero regions */
TEST(PS3EncMap, ParseZeroRegions)
{
    uint8_t sector0[2048];
    memset(sector0, 0, 2048); /* region_count = 0 */

    Ps3PlaintextRegion *regions = NULL;
    uint32_t            count   = 99;

    int32_t ret = ps3_parse_encryption_map(sector0, 2048, &regions, &count);
    ASSERT_EQ(0, ret);
    EXPECT_EQ(0u, count);
    EXPECT_EQ(nullptr, regions);
}

/* Test: parse rejects too many regions */
TEST(PS3EncMap, ParseTooManyRegions)
{
    uint8_t sector0[2048];
    memset(sector0, 0, 2048);
    /* region_count = 65 (> max 64) */
    sector0[3] = 65;

    Ps3PlaintextRegion *regions = NULL;
    uint32_t            count   = 0;

    int32_t ret = ps3_parse_encryption_map(sector0, 2048, &regions, &count);
    EXPECT_LT(ret, 0);
}

/* Test: parse rejects invalid region (start > end) */
TEST(PS3EncMap, ParseInvalidRegion)
{
    uint8_t sector0[2048];
    memset(sector0, 0, 2048);
    sector0[3] = 1; /* 1 region */
    /* start=100, end=50 (invalid) */
    sector0[11] = 100;
    sector0[15] = 50;

    Ps3PlaintextRegion *regions = NULL;
    uint32_t            count   = 0;

    int32_t ret = ps3_parse_encryption_map(sector0, 2048, &regions, &count);
    EXPECT_LT(ret, 0);
}

/* Test: parse rejects too-short buffer */
TEST(PS3EncMap, ParseBufferTooShort)
{
    uint8_t sector0[8];
    memset(sector0, 0, 8);
    sector0[3] = 1; /* 1 region, but buffer is only 8 bytes (need 16) */

    Ps3PlaintextRegion *regions = NULL;
    uint32_t            count   = 0;

    int32_t ret = ps3_parse_encryption_map(sector0, 8, &regions, &count);
    EXPECT_LT(ret, 0);
}

/* Test: serialize and deserialize round-trip */
TEST(PS3EncMap, SerializeDeserializeRoundTrip)
{
    Ps3PlaintextRegion input[3] = {{0, 31}, {256, 511}, {1024, 2047}};

    uint8_t *data   = NULL;
    uint32_t length = 0;

    int32_t ret = ps3_serialize_encryption_map(input, 3, &data, &length);
    ASSERT_EQ(0, ret);
    ASSERT_NE(nullptr, data);
    ASSERT_EQ(4u + 3u * 8u, length); /* 4 + 24 = 28 */

    Ps3PlaintextRegion *output = NULL;
    uint32_t            count  = 0;

    ret = ps3_deserialize_encryption_map(data, length, &output, &count);
    ASSERT_EQ(0, ret);
    ASSERT_EQ(3u, count);
    ASSERT_NE(nullptr, output);

    for(uint32_t i = 0; i < 3; i++)
    {
        EXPECT_EQ(input[i].start_sector, output[i].start_sector);
        EXPECT_EQ(input[i].end_sector, output[i].end_sector);
    }

    free(data);
    free(output);
}

/* Test: serialize zero regions */
TEST(PS3EncMap, SerializeZeroRegions)
{
    uint8_t *data   = NULL;
    uint32_t length = 0;

    int32_t ret = ps3_serialize_encryption_map(NULL, 0, &data, &length);
    ASSERT_EQ(0, ret);
    ASSERT_NE(nullptr, data);
    ASSERT_EQ(4u, length);

    /* First 4 bytes should be 0 (LE) */
    EXPECT_EQ(0, data[0]);
    EXPECT_EQ(0, data[1]);
    EXPECT_EQ(0, data[2]);
    EXPECT_EQ(0, data[3]);

    free(data);
}

/* Test: sector encrypted — outside plaintext regions */
TEST(PS3EncMap, SectorEncryptedOutside)
{
    Ps3PlaintextRegion regions[2] = {{0, 31}, {256, 511}};

    /* Sector 32: between regions → encrypted */
    EXPECT_TRUE(ps3_is_sector_encrypted(regions, 2, 32));
    /* Sector 100: between regions → encrypted */
    EXPECT_TRUE(ps3_is_sector_encrypted(regions, 2, 100));
    /* Sector 255: just before second region → encrypted */
    EXPECT_TRUE(ps3_is_sector_encrypted(regions, 2, 255));
    /* Sector 512: just after second region → encrypted */
    EXPECT_TRUE(ps3_is_sector_encrypted(regions, 2, 512));
    /* Sector 10000: way past all regions → encrypted */
    EXPECT_TRUE(ps3_is_sector_encrypted(regions, 2, 10000));
}

/* Test: sector not encrypted — inside plaintext regions */
TEST(PS3EncMap, SectorPlaintextInside)
{
    Ps3PlaintextRegion regions[2] = {{0, 31}, {256, 511}};

    /* Sector 0: first sector of first region → plaintext */
    EXPECT_FALSE(ps3_is_sector_encrypted(regions, 2, 0));
    /* Sector 15: middle of first region → plaintext */
    EXPECT_FALSE(ps3_is_sector_encrypted(regions, 2, 15));
    /* Sector 31: last sector of first region → plaintext */
    EXPECT_FALSE(ps3_is_sector_encrypted(regions, 2, 31));
    /* Sector 256: first sector of second region → plaintext */
    EXPECT_FALSE(ps3_is_sector_encrypted(regions, 2, 256));
    /* Sector 400: middle of second region → plaintext */
    EXPECT_FALSE(ps3_is_sector_encrypted(regions, 2, 400));
    /* Sector 511: last sector of second region → plaintext */
    EXPECT_FALSE(ps3_is_sector_encrypted(regions, 2, 511));
}

/* Test: sector encrypted when no regions defined */
TEST(PS3EncMap, SectorEncryptedNoRegions)
{
    EXPECT_TRUE(ps3_is_sector_encrypted(NULL, 0, 0));
    EXPECT_TRUE(ps3_is_sector_encrypted(NULL, 0, 100));
}

/* Test: parse then use for sector lookup */
TEST(PS3EncMap, ParseThenLookup)
{
    uint8_t sector0[2048];
    build_synthetic_sector0(sector0, 2048);

    Ps3PlaintextRegion *regions = NULL;
    uint32_t            count   = 0;

    int32_t ret = ps3_parse_encryption_map(sector0, 2048, &regions, &count);
    ASSERT_EQ(0, ret);

    /* Sector 0: in first plaintext region */
    EXPECT_FALSE(ps3_is_sector_encrypted(regions, count, 0));
    /* Sector 50: between regions → encrypted */
    EXPECT_TRUE(ps3_is_sector_encrypted(regions, count, 50));
    /* Sector 300: in second plaintext region */
    EXPECT_FALSE(ps3_is_sector_encrypted(regions, count, 300));
    /* Sector 600: past all regions → encrypted */
    EXPECT_TRUE(ps3_is_sector_encrypted(regions, count, 600));

    free(regions);
}
