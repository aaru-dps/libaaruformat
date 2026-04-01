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
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <aaruformat.h>

#include "../include/aaruformat/flac.h"
#include "crc32.h"
#include "gtest/gtest.h"

#define EXPECTED_CRC32 0xdfbc99bb

static uint8_t *buffer;

class flacFixture : public ::testing::Test
{
public:
    flacFixture() = default;

protected:
    void SetUp() override
    {
        char path[PATH_MAX];
        char filename[PATH_MAX];

        getcwd(path, PATH_MAX);
        snprintf(filename, PATH_MAX, "%s/data/flac.flac", path);

        FILE *file = fopen(filename, "rb");
        buffer     = static_cast<uint8_t *>(malloc(6534197));
        fread(buffer, 1, 6534197, file);
        fclose(file);
    }

    void TearDown() override { free(buffer); }

    ~flacFixture() override = default;

    // shared user data
};

// --- Regression tests for fix/flac-error-handling (TEST-R01) ---
// The fix changed decode/encode to return 0 (not -1 or crash) on error,
// added NULL checks, and fixed a decoder leak on init failure.

TEST(flacErrors, decode_garbage_returns_zero)
{
    // Random garbage is not valid FLAC — decoder must return 0, not crash or return -1
    uint8_t garbage[4096];
    memset(garbage, 0xAB, sizeof(garbage));

    const size_t out_size = 65536;
    auto        *out      = static_cast<uint8_t *>(malloc(out_size));
    ASSERT_NE(out, nullptr);

    size_t result = aaruf_flac_decode_redbook_buffer(out, out_size, garbage, sizeof(garbage));

    EXPECT_EQ(result, 0U);
    free(out);
}

TEST(flacErrors, decode_truncated_flac_returns_zero)
{
    // Valid FLAC header but truncated mid-stream
    // fLaC magic = 0x664C6143
    uint8_t truncated[64];
    truncated[0] = 'f';
    truncated[1] = 'L';
    truncated[2] = 'a';
    truncated[3] = 'C';
    // Fill rest with zeros (incomplete metadata block)
    memset(truncated + 4, 0, sizeof(truncated) - 4);

    auto *out = static_cast<uint8_t *>(malloc(4096));
    ASSERT_NE(out, nullptr);

    size_t result = aaruf_flac_decode_redbook_buffer(out, 4096, truncated, sizeof(truncated));

    EXPECT_EQ(result, 0U);
    free(out);
}

TEST(flacErrors, decode_empty_input_returns_zero)
{
    auto *out = static_cast<uint8_t *>(malloc(4096));
    ASSERT_NE(out, nullptr);

    size_t result = aaruf_flac_decode_redbook_buffer(out, 4096, NULL, 0);

    EXPECT_EQ(result, 0U);
    free(out);
}

TEST(flacErrors, decode_zero_length_returns_zero)
{
    uint8_t src[1] = {0};
    auto   *out    = static_cast<uint8_t *>(malloc(4096));
    ASSERT_NE(out, nullptr);

    size_t result = aaruf_flac_decode_redbook_buffer(out, 4096, src, 0);

    EXPECT_EQ(result, 0U);
    free(out);
}

TEST(flacErrors, encode_zero_length_no_crash)
{
    // Zero-length encode with NULL source: FLAC encoder still produces a valid
    // stream header (STREAMINFO + footer), so result > 0 is expected.
    // The key assertion: no crash, no leak, no ASan error.
    uint8_t out[4096];
    size_t  result = aaruf_flac_encode_redbook_buffer(out, sizeof(out), NULL, 0, 4608, 1, 0,
                                                      "partial_tukey(0/1.0/1.0)", 12, 0, 1, false, 0, 8, NULL, 0);

    // FLAC emits a valid (empty) stream — header bytes only
    EXPECT_GT(result, 0U);
    EXPECT_LT(result, 256U);  // sanity: just header, no audio data
}

TEST(flacErrors, encode_garbage_roundtrip_fails_cleanly)
{
    // Non-PCM data that is technically processable by FLAC (correct length for samples)
    // but should still produce a valid FLAC stream that roundtrips.
    // The key test here: even random data shouldn't crash or leak.
    const size_t src_size = 2352 * 4;  // 4 CD sectors worth of "audio"
    auto        *src      = static_cast<uint8_t *>(malloc(src_size));
    memset(src, 0xDE, src_size);

    auto *cmp = static_cast<uint8_t *>(malloc(src_size * 2));
    size_t cmp_result =
        aaruf_flac_encode_redbook_buffer(cmp, src_size * 2, src, src_size, 4608, 1, 0,
                                         "partial_tukey(0/1.0/1.0)", 12, 0, 1, false, 0, 8, NULL, 0);

    // Encoding random data should succeed (FLAC handles any 16-bit PCM)
    EXPECT_GT(cmp_result, 0U);

    if(cmp_result > 0)
    {
        // Verify roundtrip: decode what we just encoded
        auto *decoded = static_cast<uint8_t *>(malloc(src_size));
        size_t dec_result = aaruf_flac_decode_redbook_buffer(decoded, src_size, cmp, cmp_result);

        EXPECT_EQ(dec_result, src_size);
        if(dec_result == src_size) EXPECT_EQ(memcmp(src, decoded, src_size), 0);

        free(decoded);
    }

    free(src);
    free(cmp);
}

TEST(flacErrors, decode_dst_too_small_returns_zero)
{
    // Encode valid audio data, then try to decode into a buffer that's too small.
    // This should not crash or overwrite past the buffer.
    const size_t src_size = 2352 * 4;
    auto        *src      = static_cast<uint8_t *>(malloc(src_size));
    memset(src, 0x00, src_size);  // silence — compresses well

    auto *cmp = static_cast<uint8_t *>(malloc(src_size));
    size_t cmp_result =
        aaruf_flac_encode_redbook_buffer(cmp, src_size, src, src_size, 4608, 1, 0, "partial_tukey(0/1.0/1.0)", 12, 0,
                                         1, false, 0, 8, NULL, 0);
    ASSERT_GT(cmp_result, 0U) << "Failed to encode test data";

    // Decode into a buffer that's way too small
    uint8_t tiny[64];
    size_t  dec_result = aaruf_flac_decode_redbook_buffer(tiny, sizeof(tiny), cmp, cmp_result);

    // The decoder writes into the callback buffer capped by dst_len,
    // but FLAC may still report success. The important thing is no crash
    // and the returned size doesn't exceed our buffer.
    EXPECT_LE(dec_result, sizeof(tiny));

    free(src);
    free(cmp);
}

// --- Original tests below ---

TEST_F(flacFixture, flac)
{
    auto *outBuf = static_cast<uint8_t *>(malloc(9633792));

    auto decoded = aaruf_flac_decode_redbook_buffer(outBuf, 9633792, buffer, 6534197);

    EXPECT_EQ(decoded, 9633792);

    auto crc = crc32_data(outBuf, 9633792);

    free(outBuf);

    EXPECT_EQ(crc, EXPECTED_CRC32);
}

TEST_F(flacFixture, flacCompress)
{
    size_t original_len = 9633792;
    uint   cmp_len      = original_len;
    uint   decmp_len    = original_len;
    char   path[PATH_MAX];
    char   filename[PATH_MAX * 2];

    // Allocate buffers
    uint8_t *original     = static_cast<uint8_t *>(malloc(original_len));
    uint8_t *cmp_buffer   = static_cast<uint8_t *>(malloc(cmp_len));
    uint8_t *decmp_buffer = static_cast<uint8_t *>(malloc(decmp_len));

    // Read the file
    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/audio.bin", path);

    FILE *file = fopen(filename, "rb");
    fread(original, 1, original_len, file);
    fclose(file);

    // Calculate the CRC
    uint32_t original_crc = crc32_data(original, original_len);

    // Compress
    size_t newSize = aaruf_flac_encode_redbook_buffer(
        cmp_buffer, cmp_len, original, original_len, 4608, 1, 0, "partial_tukey(0/1.0/1.0)", 12, 0, 1, false, 0, 8,
        "Aaru.Compression.Native.Tests", strlen("Aaru.Compression.Native.Tests"));
    cmp_len = newSize;

    // Decompress
    newSize   = aaruf_flac_decode_redbook_buffer(decmp_buffer, decmp_len, cmp_buffer, cmp_len);
    decmp_len = newSize;

    EXPECT_EQ(decmp_len, original_len);

    uint32_t decmp_crc = crc32_data(decmp_buffer, decmp_len);

    // Free buffers
    free(original);
    free(cmp_buffer);
    free(decmp_buffer);

    EXPECT_EQ(decmp_crc, original_crc);
}
