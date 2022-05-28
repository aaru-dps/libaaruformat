/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2022 Natalia Portillo.
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

#include <aaruformat.h>

#include "../include/aaruformat/flac.h"
#include "crc32.h"
#include "gtest/gtest.h"

#define EXPECTED_CRC32 0xdfbc99bb

static const uint8_t* buffer;

class flacFixture : public ::testing::Test
{
  public:
    flacFixture()
    {
        // initialization;
        // can also be done in SetUp()
    }

  protected:
    void SetUp()
    {
        char path[PATH_MAX];
        char filename[PATH_MAX];

        getcwd(path, PATH_MAX);
        snprintf(filename, PATH_MAX, "%s/data/flac.flac", path);

        FILE* file = fopen(filename, "rb");
        buffer     = (const uint8_t*)malloc(6534197);
        fread((void*)buffer, 1, 6534197, file);
        fclose(file);
    }

    void TearDown() { free((void*)buffer); }

    ~flacFixture()
    {
        // resources cleanup, no exceptions allowed
    }

    // shared user data
};

TEST_F(flacFixture, flac)
{
    auto* outBuf = (uint8_t*)malloc(9633792);

    auto decoded = aaruf_flac_decode_redbook_buffer(outBuf, 9633792, buffer, 6534197);

    EXPECT_EQ(decoded, 9633792);

    auto crc = crc32_data(outBuf, 9633792);

    free(outBuf);

    EXPECT_EQ(crc, EXPECTED_CRC32);
}

TEST_F(flacFixture, flacCompress)
{
    size_t         original_len = 9633792;
    uint           cmp_len      = original_len;
    uint           decmp_len    = original_len;
    char           path[PATH_MAX];
    char           filename[PATH_MAX * 2];
    FILE*          file;
    uint32_t       original_crc, decmp_crc;
    const uint8_t* original;
    uint8_t*       cmp_buffer;
    uint8_t*       decmp_buffer;
    size_t         newSize;

    // Allocate buffers
    original     = (const uint8_t*)malloc(original_len);
    cmp_buffer   = (uint8_t*)malloc(cmp_len);
    decmp_buffer = (uint8_t*)malloc(decmp_len);

    // Read the file
    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/audio.bin", path);

    file = fopen(filename, "rb");
    fread((void*)original, 1, original_len, file);
    fclose(file);

    // Calculate the CRC
    original_crc = crc32_data(original, original_len);

    // Compress
    newSize = aaruf_flac_encode_redbook_buffer(cmp_buffer,
                                               cmp_len,
                                               original,
                                               original_len,
                                               4608,
                                               1,
                                               0,
                                               "partial_tukey(0/1.0/1.0)",
                                               12,
                                               0,
                                               1,
                                               false,
                                               0,
                                               8,
                                               "Aaru.Compression.Native.Tests",
                                               strlen("Aaru.Compression.Native.Tests"));
    cmp_len = newSize;

    // Decompress
    newSize   = aaruf_flac_decode_redbook_buffer(decmp_buffer, decmp_len, cmp_buffer, cmp_len);
    decmp_len = newSize;

    EXPECT_EQ(decmp_len, original_len);

    decmp_crc = crc32_data(decmp_buffer, decmp_len);

    // Free buffers
    free((void*)original);
    free(cmp_buffer);
    free(decmp_buffer);

    EXPECT_EQ(decmp_crc, original_crc);
}
