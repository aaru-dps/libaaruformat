/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2021 Natalia Portillo.
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

#include "crc32.h"
#include "gtest/gtest.h"

#define EXPECTED_CRC32 0x954bf76e

static const uint8_t* buffer;

class lzmaFixture : public ::testing::Test
{
  public:
    lzmaFixture()
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
        snprintf(filename, PATH_MAX, "%s/data/lzma.bin", path);

        FILE* file = fopen(filename, "rb");
        buffer     = (const uint8_t*)malloc(1200275);
        fread((void*)buffer, 1, 1200275, file);
        fclose(file);
    }

    void TearDown() { free((void*)buffer); }

    ~lzmaFixture()
    {
        // resources cleanup, no exceptions allowed
    }

    // shared user data
};

TEST_F(lzmaFixture, lzma)
{
    uint8_t params[] = {0x5D, 0x00, 0x00, 0x00, 0x02};
    size_t  destLen  = 8388608;
    size_t  srcLen   = 1200275;
    auto*   outBuf   = (uint8_t*)malloc(8388608);

    auto err = aaruf_lzma_decode_buffer(outBuf, &destLen, buffer, &srcLen, params, 5);

    EXPECT_EQ(err, 0);
    EXPECT_EQ(destLen, 8388608);

    auto crc = crc32_data(outBuf, 8388608);

    free(outBuf);

    EXPECT_EQ(crc, EXPECTED_CRC32);
}

TEST_F(lzmaFixture, lzmaCompress)
{
    size_t         original_len = 8388608;
    size_t         cmp_len      = original_len;
    size_t         decmp_len    = original_len;
    char           path[PATH_MAX];
    char           filename[PATH_MAX * 2];
    FILE*          file;
    uint32_t       original_crc, decmp_crc;
    const uint8_t* original;
    uint8_t*       cmp_buffer;
    uint8_t*       decmp_buffer;
    int            err;
    uint8_t        props[5];
    size_t         props_len = 5;

    // Allocate buffers
    original     = (const uint8_t*)malloc(original_len);
    cmp_buffer   = (uint8_t*)malloc(cmp_len);
    decmp_buffer = (uint8_t*)malloc(decmp_len);

    // Read the file
    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/data.bin", path);

    file = fopen(filename, "rb");
    fread((void*)original, 1, original_len, file);
    fclose(file);

    // Calculate the CRC
    original_crc = crc32_data(original, original_len);

    // Compress
    err = aaruf_lzma_encode_buffer(
        cmp_buffer, &cmp_len, original, original_len, props, &props_len, 9, 1048576, 3, 0, 2, 273, 2);
    EXPECT_EQ(err, 0);

    // Decompress
    err = aaruf_lzma_decode_buffer(decmp_buffer, &decmp_len, cmp_buffer, &cmp_len, props, props_len);
    EXPECT_EQ(err, 0);

    EXPECT_EQ(decmp_len, original_len);

    decmp_crc = crc32_data(decmp_buffer, decmp_len);

    // Free buffers
    free((void*)original);
    free(cmp_buffer);
    free(decmp_buffer);

    EXPECT_EQ(decmp_crc, original_crc);
}
