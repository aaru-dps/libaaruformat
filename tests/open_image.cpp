/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2025 Natalia Portillo.
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
#include <cstring>

#include "../include/aaruformat.h"
#include "gtest/gtest.h"

class OpenImageFixture : public testing::Test
{
public:
    OpenImageFixture() = default;

protected:
    void SetUp() override
    {
        // Test data path will be set by CMake
    }

    void TearDown() override
    {
        // Cleanup if needed
    }
};

// Test opening mf2hd_v1.aif downloaded via ExternalData
TEST_F(OpenImageFixture, open_mf2hd_v1)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];

    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/mf2hd_v1.aif", path);

    // Attempt to open the image file
    void *context = aaruf_open(filename);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to open mf2hd_v1.aif";

    // Get image info to verify it's a valid image
    ImageInfo     image_info;
    const int32_t result = aaruf_get_image_info(context, &image_info);

    ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to get image info";

    // Basic sanity checks on the image info
    ASSERT_EQ(image_info.HasPartitions, false) << "Image should not have partitions";
    ASSERT_EQ(image_info.HasSessions, false) << "Image should not have sessions";
    ASSERT_EQ(image_info.ImageSize, 50162) << "Unexpected image size";
    ASSERT_EQ(image_info.Sectors, 2880) << "Unexpected number of sectors";
    ASSERT_EQ(image_info.SectorSize, 512) << "Unexpected sector size";
    ASSERT_STREQ(image_info.Version, "1.0") << "Unexpected image version";
    ASSERT_STREQ(image_info.Application, "Aaru") << "Unexpected application name";
    ASSERT_STREQ(image_info.ApplicationVersion, "6.0") << "Unexpected application version";
    ASSERT_EQ(image_info.CreationTime, 133986244248621873ULL) << "Unexpected creation time";
    ASSERT_EQ(image_info.LastModificationTime, 133986244253238013ULL) << "Unexpected modification time";
    ASSERT_EQ(image_info.MediaType, 199) << "Unexpected media type";
    ASSERT_EQ(image_info.MetadataMediaType, 1) << "Unexpected metadata media type";

    crc64_ctx *ctx = aaruf_crc64_init();
    uint64_t   crc = 0;

    for(int i = 0; i < 2880; i++)
    {
        uint8_t  buffer[512];
        uint32_t length = sizeof(buffer);

        const int32_t read_result = aaruf_read_sector(context, i, false, buffer, &length);
        EXPECT_EQ(read_result, AARUF_STATUS_OK) << "Failed to read sector " << i;
        EXPECT_EQ(length, 512U) << "Unexpected length for sector " << i;
        aaruf_crc64_update(ctx, buffer, 512);
    }

    aaruf_crc64_final(ctx, &crc);
    aaruf_crc64_free(ctx);

    EXPECT_EQ(crc, 0x0439da56aa993cdf) << "Unexpected CRC64 for image data";

    // Close the image
    const int32_t close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
}

// Test opening a non-existent file
TEST_F(OpenImageFixture, OpenNonExistentFile)
{
    const void *context = aaruf_open("/nonexistent/path/to/file.aif");

    // Should return NULL for non-existent files
    EXPECT_EQ(context, nullptr) << "Opening non-existent file should return NULL";
}

// Test opening with NULL path
TEST_F(OpenImageFixture, OpenNullPath)
{
    const void *context = aaruf_open(nullptr);

    // Should return NULL for NULL path
    EXPECT_EQ(context, nullptr) << "Opening NULL path should return NULL";
}
