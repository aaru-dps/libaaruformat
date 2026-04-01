/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../include/aaruformat.h"
#include "gtest/gtest.h"

// --- Helpers ---

static bool copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if(!in) return false;

    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);

    auto *buf = static_cast<uint8_t *>(malloc(size));
    if(!buf)
    {
        fclose(in);
        return false;
    }

    size_t read = fread(buf, 1, size, in);
    fclose(in);
    if(read != static_cast<size_t>(size))
    {
        free(buf);
        return false;
    }

    FILE *out = fopen(dst, "wb");
    if(!out)
    {
        free(buf);
        return false;
    }

    size_t written = fwrite(buf, 1, size, out);
    fclose(out);
    free(buf);

    return written == static_cast<size_t>(size);
}

static bool corrupt_bytes(const char *filepath, long offset, const uint8_t *garbage, size_t len)
{
    FILE *f = fopen(filepath, "r+b");
    if(!f) return false;

    if(fseek(f, offset, SEEK_SET) != 0)
    {
        fclose(f);
        return false;
    }

    size_t written = fwrite(garbage, 1, len, f);
    fclose(f);
    return written == len;
}

class VerifyImageFixture : public testing::Test
{
};

// ==================== A. Happy path — v1 fixtures ====================
// V1 images use LZMA props in CRC + byte-swap.

TEST_F(VerifyImageFixture, verify_floptical_v1)
{
    char path[PATH_MAX], filename[PATH_MAX];
    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/floptical_v1.aif", path);

    void *context = aaruf_open(filename, false, NULL);
    ASSERT_NE(context, nullptr) << "Failed to open floptical_v1.aif";

    EXPECT_EQ(aaruf_verify_image(context), AARUF_STATUS_OK);
    EXPECT_EQ(aaruf_close(context), AARUF_STATUS_OK);
}

TEST_F(VerifyImageFixture, verify_mf2hd_v1)
{
    char path[PATH_MAX], filename[PATH_MAX];
    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/mf2hd_v1.aif", path);

    void *context = aaruf_open(filename, false, NULL);
    ASSERT_NE(context, nullptr) << "Failed to open mf2hd_v1.aif";

    EXPECT_EQ(aaruf_verify_image(context), AARUF_STATUS_OK);
    EXPECT_EQ(aaruf_close(context), AARUF_STATUS_OK);
}

TEST_F(VerifyImageFixture, verify_cdmode1_v1)
{
    char path[PATH_MAX], filename[PATH_MAX];
    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/cdmode1_v1.aif", path);

    void *context = aaruf_open(filename, false, NULL);
    ASSERT_NE(context, nullptr) << "Failed to open cdmode1_v1.aif";

    EXPECT_EQ(aaruf_verify_image(context), AARUF_STATUS_OK);
    EXPECT_EQ(aaruf_close(context), AARUF_STATUS_OK);
}

// ==================== A. Happy path — v2 fixtures ====================
// V2 images exclude LZMA props from CRC, no byte-swap.

TEST_F(VerifyImageFixture, verify_floptical_v2)
{
    char path[PATH_MAX], filename[PATH_MAX];
    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/floptical.aif", path);

    void *context = aaruf_open(filename, false, NULL);
    ASSERT_NE(context, nullptr) << "Failed to open floptical.aif";

    EXPECT_EQ(aaruf_verify_image(context), AARUF_STATUS_OK);
    EXPECT_EQ(aaruf_close(context), AARUF_STATUS_OK);
}

TEST_F(VerifyImageFixture, verify_mf2hd_v2)
{
    char path[PATH_MAX], filename[PATH_MAX];
    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/mf2hd.aif", path);

    void *context = aaruf_open(filename, false, NULL);
    ASSERT_NE(context, nullptr) << "Failed to open mf2hd.aif";

    EXPECT_EQ(aaruf_verify_image(context), AARUF_STATUS_OK);
    EXPECT_EQ(aaruf_close(context), AARUF_STATUS_OK);
}

TEST_F(VerifyImageFixture, verify_cdmode1_v2)
{
    char path[PATH_MAX], filename[PATH_MAX];
    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/cdmode1.aif", path);

    void *context = aaruf_open(filename, false, NULL);
    ASSERT_NE(context, nullptr) << "Failed to open cdmode1.aif";

    EXPECT_EQ(aaruf_verify_image(context), AARUF_STATUS_OK);
    EXPECT_EQ(aaruf_close(context), AARUF_STATUS_OK);
}

// ==================== B. Invalid context ====================

TEST_F(VerifyImageFixture, verify_null_context) { EXPECT_EQ(aaruf_verify_image(NULL), AARUF_ERROR_NOT_AARUFORMAT); }

TEST_F(VerifyImageFixture, verify_garbage_context)
{
    // 256 bytes of 0xFF — magic won't match AARU_MAGIC
    uint8_t garbage[256];
    memset(garbage, 0xFF, sizeof(garbage));

    EXPECT_EQ(aaruf_verify_image(garbage), AARUF_ERROR_NOT_AARUFORMAT);
}

// ==================== C. Data corruption detection ====================

TEST_F(VerifyImageFixture, verify_corrupt_data_v1)
{
    char path[PATH_MAX], src[PATH_MAX], dst[PATH_MAX];
    getcwd(path, PATH_MAX);
    snprintf(src, PATH_MAX, "%s/data/floptical_v1.aif", path);
    snprintf(dst, PATH_MAX, "%s/verify_corrupt_data_v1.aif", path);

    ASSERT_TRUE(copy_file(src, dst)) << "Failed to copy fixture";

    // First DBLK at offset 104, BlockHeader 36 bytes, LZMA payload starts at 140.
    // V1 CRC covers full cmpLength (19 bytes) including LZMA props. Corrupt at 145.
    uint8_t garbage[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    ASSERT_TRUE(corrupt_bytes(dst, 145, garbage, sizeof(garbage)));

    void *context = aaruf_open(dst, false, NULL);
    ASSERT_NE(context, nullptr) << "aaruf_open should succeed on corrupted data (lenient open)";

    EXPECT_EQ(aaruf_verify_image(context), AARUF_ERROR_INVALID_BLOCK_CRC);

    aaruf_close(context);
    remove(dst);
}

TEST_F(VerifyImageFixture, verify_corrupt_data_v2)
{
    char path[PATH_MAX], src[PATH_MAX], dst[PATH_MAX];
    getcwd(path, PATH_MAX);
    snprintf(src, PATH_MAX, "%s/data/floptical.aif", path);
    snprintf(dst, PATH_MAX, "%s/verify_corrupt_data_v2.aif", path);

    ASSERT_TRUE(copy_file(src, dst)) << "Failed to copy fixture";

    // DBLK at 512, payload at 548, LZMA props 548-552, CRC region 553-561 (9 bytes).
    // Corrupt 2 bytes within the CRC region.
    uint8_t garbage[2] = {0xDE, 0xAD};
    ASSERT_TRUE(corrupt_bytes(dst, 555, garbage, sizeof(garbage)));

    void *context = aaruf_open(dst, false, NULL);
    ASSERT_NE(context, nullptr) << "aaruf_open should succeed on corrupted data (lenient open)";

    EXPECT_EQ(aaruf_verify_image(context), AARUF_ERROR_INVALID_BLOCK_CRC);

    aaruf_close(context);
    remove(dst);
}

TEST_F(VerifyImageFixture, verify_corrupt_tracks_block)
{
    char path[PATH_MAX], src[PATH_MAX], dst[PATH_MAX];
    getcwd(path, PATH_MAX);
    snprintf(src, PATH_MAX, "%s/data/cdmode1_v1.aif", path);
    snprintf(dst, PATH_MAX, "%s/verify_corrupt_tracks.aif", path);

    ASSERT_TRUE(copy_file(src, dst)) << "Failed to copy fixture";

    // TracksBlock at offset 4407206. TracksHeader is 14 bytes (packed).
    // TrackEntry array starts at 4407220. Corrupt bytes in the track data.
    uint8_t garbage[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    ASSERT_TRUE(corrupt_bytes(dst, 4407224, garbage, sizeof(garbage)));

    void *context = aaruf_open(dst, false, NULL);
    ASSERT_NE(context, nullptr) << "aaruf_open should succeed on corrupted tracks data";

    EXPECT_EQ(aaruf_verify_image(context), AARUF_ERROR_INVALID_BLOCK_CRC);

    aaruf_close(context);
    remove(dst);
}

// ==================== D. Index corruption (in-memory) ====================

TEST_F(VerifyImageFixture, verify_invalid_index_offset)
{
    char path[PATH_MAX], filename[PATH_MAX];
    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/floptical_v1.aif", path);

    void *context = aaruf_open(filename, false, NULL);
    ASSERT_NE(context, nullptr);

    // Manipulate the in-memory index offset to point beyond the file
    auto *ctx              = static_cast<aaruformat_context *>(context);
    ctx->header.indexOffset = 0xFFFFFFFF00ULL;

    EXPECT_EQ(aaruf_verify_image(context), AARUF_ERROR_CANNOT_READ_HEADER);

    aaruf_close(context);
}

TEST_F(VerifyImageFixture, verify_bad_index_signature)
{
    char path[PATH_MAX], filename[PATH_MAX];
    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/floptical_v1.aif", path);

    void *context = aaruf_open(filename, false, NULL);
    ASSERT_NE(context, nullptr);

    // Point index offset to the middle of a DataBlock payload — the 4 bytes
    // there will be LZMA data, not a valid index signature
    auto *ctx              = static_cast<aaruformat_context *>(context);
    ctx->header.indexOffset = 200;

    EXPECT_EQ(aaruf_verify_image(context), AARUF_ERROR_CANNOT_READ_INDEX);

    aaruf_close(context);
}

// ==================== E. Programmatic round-trip ====================

TEST_F(VerifyImageFixture, verify_created_image)
{
    char path[PATH_MAX], random_file[PATH_MAX], test_file[PATH_MAX];
    getcwd(path, PATH_MAX);
    snprintf(random_file, PATH_MAX, "%s/data/random", path);
    snprintf(test_file, PATH_MAX, "%s/verify_roundtrip.aif", path);

    FILE *f = fopen(random_file, "rb");
    ASSERT_NE(f, nullptr) << "Failed to open random data file";

    auto *buffer = static_cast<uint8_t *>(malloc(1048576));
    ASSERT_NE(buffer, nullptr);
    fread(buffer, 1, 1048576, f);
    fclose(f);

    constexpr size_t total_sectors = 2048;
    constexpr size_t sector_size   = 512;

    void *context = aaruf_create(test_file, 1, sector_size, total_sectors, 0, 0, "deduplicate=true;compress=true",
                                 reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);
    ASSERT_NE(context, nullptr) << "Failed to create test image";

    for(size_t sector = 0; sector < total_sectors; ++sector)
    {
        const size_t  offset = sector * sector_size % 1048576;
        const int32_t result =
            aaruf_write_sector(context, sector, false, buffer + offset, SectorStatusDumped, sector_size);
        ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to write sector " << sector;
    }

    ASSERT_EQ(aaruf_close(context), AARUF_STATUS_OK) << "Failed to close image";
    free(buffer);

    // Reopen and verify
    context = aaruf_open(test_file, false, NULL);
    ASSERT_NE(context, nullptr) << "Failed to reopen test image";

    EXPECT_EQ(aaruf_verify_image(context), AARUF_STATUS_OK);

    aaruf_close(context);
    remove(test_file);
}
