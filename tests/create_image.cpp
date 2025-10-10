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

class CreateImageFixture : public testing::Test
{
public:
    CreateImageFixture() = default;

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

TEST_F(CreateImageFixture, create_image_uncompresed_duplicated)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];

    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/random", path);

    // Open random file
    FILE *f = fopen(filename, "rb");
    ASSERT_NE(f, nullptr) << "Failed to open random data file";

    uint8_t *buffer = static_cast<uint8_t *>(malloc(1048576));
    fread(buffer, 1, 1048576, f);
    fclose(f);

    // Static fake UTF-16LE hex string saying "gtest" with interlaced zeroes
    static const uint8_t fake_utf16le_gtest[] = {0x67, 0x00, 0x74, 0x00, 0x65, 0x00, 0x73, 0x00, 0x74, 0x00};

    constexpr size_t total_sectors = 128 * 1024 * 1024 / 512;  // 128 MiB / 512 bytes

    // Create image
    void *context = aaruf_create("test.aif", 1, 512, total_sectors, 0, 0, "deduplicate=false;compress=false",
                                 fake_utf16le_gtest, 10, 0, 0, false);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to create test.aif";

    crc64_ctx *ctx           = aaruf_crc64_init();
    uint64_t   generated_crc = 0;

    // Write data in sectors of 512 bytes
    for(size_t sector = 0; sector < total_sectors; ++sector)
    {
        const size_t  buffer_offset = sector * 512 % 1048576;  // Roll back to start when reaching end
        const int32_t write_result =
            aaruf_write_sector(context, sector, false, buffer + buffer_offset, SectorStatusDumped, 512);
        ASSERT_EQ(write_result, AARUF_STATUS_OK) << "Failed to write sector " << sector;

        aaruf_crc64_update(ctx, buffer + buffer_offset, 512);
    }

    aaruf_crc64_final(ctx, &generated_crc);
    aaruf_crc64_free(ctx);

    // Close the image
    int32_t close_result = aaruf_close(context);
    ASSERT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
    free(buffer);

    // Reopen the image
    context = aaruf_open("test.aif");
    ASSERT_NE(context, nullptr) << "Failed to open test.aif";

    // Get image info to verify it's a valid image
    ImageInfo     image_info;
    const int32_t result = aaruf_get_image_info(context, &image_info);

    ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to get image info";

    // Basic sanity checks on the image info
    ASSERT_EQ(image_info.HasPartitions, false) << "Image should not have partitions";
    ASSERT_EQ(image_info.HasSessions, false) << "Image should not have sessions";
    ASSERT_EQ(image_info.Sectors, total_sectors) << "Unexpected number of sectors";
    ASSERT_EQ(image_info.SectorSize, 512) << "Unexpected sector size";
    ASSERT_STREQ(image_info.Version, "2.0") << "Unexpected image version";
    ASSERT_STREQ(image_info.Application, "gtest") << "Unexpected application name";
    ASSERT_STREQ(image_info.ApplicationVersion, "0.0") << "Unexpected application version";
    ASSERT_EQ(image_info.MediaType, 1) << "Unexpected media type";
    ASSERT_EQ(image_info.MetadataMediaType, 1) << "Unexpected metadata media type";

    ctx          = aaruf_crc64_init();
    uint64_t crc = 0;

    for(int i = 0; i < total_sectors; i++)
    {
        uint8_t  sector_buffer[512];
        uint32_t length = sizeof(sector_buffer);

        const int32_t read_result = aaruf_read_sector(context, i, false, sector_buffer, &length);
        EXPECT_EQ(read_result, AARUF_STATUS_OK) << "Failed to read sector " << i;
        EXPECT_EQ(length, 512U) << "Unexpected length for sector " << i;
        aaruf_crc64_update(ctx, sector_buffer, 512);
    }

    aaruf_crc64_final(ctx, &crc);
    aaruf_crc64_free(ctx);

    ASSERT_EQ(crc, generated_crc) << "Unexpected CRC64 for image data";

    // Close the image
    close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
}

TEST_F(CreateImageFixture, create_image_uncompresed_deduplicated)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];

    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/random", path);

    // Open random file
    FILE *f = fopen(filename, "rb");
    ASSERT_NE(f, nullptr) << "Failed to open random data file";

    uint8_t *buffer = static_cast<uint8_t *>(malloc(1048576));
    fread(buffer, 1, 1048576, f);
    fclose(f);

    // Static fake UTF-16LE hex string saying "gtest" with interlaced zeroes
    static const uint8_t fake_utf16le_gtest[] = {0x67, 0x00, 0x74, 0x00, 0x65, 0x00, 0x73, 0x00, 0x74, 0x00};

    constexpr size_t total_sectors = 128 * 1024 * 1024 / 512;  // 128 MiB / 512 bytes

    // Create image
    void *context = aaruf_create("test.aif", 1, 512, total_sectors, 0, 0, "deduplicate=true;compress=false",
                                 fake_utf16le_gtest, 10, 0, 0, false);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to create test.aif";

    crc64_ctx *ctx           = aaruf_crc64_init();
    uint64_t   generated_crc = 0;

    // Write data in sectors of 512 bytes
    for(size_t sector = 0; sector < total_sectors; ++sector)
    {
        const size_t  buffer_offset = sector * 512 % 1048576;  // Roll back to start when reaching end
        const int32_t write_result =
            aaruf_write_sector(context, sector, false, buffer + buffer_offset, SectorStatusDumped, 512);
        ASSERT_EQ(write_result, AARUF_STATUS_OK) << "Failed to write sector " << sector;

        aaruf_crc64_update(ctx, buffer + buffer_offset, 512);
    }

    aaruf_crc64_final(ctx, &generated_crc);
    aaruf_crc64_free(ctx);

    // Close the image
    int32_t close_result = aaruf_close(context);
    ASSERT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
    free(buffer);

    // Reopen the image
    context = aaruf_open("test.aif");
    ASSERT_NE(context, nullptr) << "Failed to open test.aif";

    // Get image info to verify it's a valid image
    ImageInfo     image_info;
    const int32_t result = aaruf_get_image_info(context, &image_info);

    ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to get image info";

    // Basic sanity checks on the image info
    ASSERT_EQ(image_info.HasPartitions, false) << "Image should not have partitions";
    ASSERT_EQ(image_info.HasSessions, false) << "Image should not have sessions";
    ASSERT_EQ(image_info.Sectors, total_sectors) << "Unexpected number of sectors";
    ASSERT_EQ(image_info.SectorSize, 512) << "Unexpected sector size";
    ASSERT_STREQ(image_info.Version, "2.0") << "Unexpected image version";
    ASSERT_STREQ(image_info.Application, "gtest") << "Unexpected application name";
    ASSERT_STREQ(image_info.ApplicationVersion, "0.0") << "Unexpected application version";
    ASSERT_EQ(image_info.MediaType, 1) << "Unexpected media type";
    ASSERT_EQ(image_info.MetadataMediaType, 1) << "Unexpected metadata media type";

    ctx          = aaruf_crc64_init();
    uint64_t crc = 0;

    for(int i = 0; i < total_sectors; i++)
    {
        uint8_t  sector_buffer[512];
        uint32_t length = sizeof(sector_buffer);

        const int32_t read_result = aaruf_read_sector(context, i, false, sector_buffer, &length);
        EXPECT_EQ(read_result, AARUF_STATUS_OK) << "Failed to read sector " << i;
        EXPECT_EQ(length, 512U) << "Unexpected length for sector " << i;
        aaruf_crc64_update(ctx, sector_buffer, 512);
    }

    aaruf_crc64_final(ctx, &crc);
    aaruf_crc64_free(ctx);

    ASSERT_EQ(crc, generated_crc) << "Unexpected CRC64 for image data";

    // Close the image
    close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
}

TEST_F(CreateImageFixture, create_image_compresed_duplicated)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];

    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/random", path);

    // Open random file
    FILE *f = fopen(filename, "rb");
    ASSERT_NE(f, nullptr) << "Failed to open random data file";

    uint8_t *buffer = static_cast<uint8_t *>(malloc(1048576));
    fread(buffer, 1, 1048576, f);
    fclose(f);

    // Static fake UTF-16LE hex string saying "gtest" with interlaced zeroes
    static const uint8_t fake_utf16le_gtest[] = {0x67, 0x00, 0x74, 0x00, 0x65, 0x00, 0x73, 0x00, 0x74, 0x00};

    constexpr size_t total_sectors = 128 * 1024 * 1024 / 512;  // 128 MiB / 512 bytes

    // Create image
    void *context = aaruf_create("test.aif", 1, 512, total_sectors, 0, 0, "deduplicate=false;compress=true",
                                 fake_utf16le_gtest, 10, 0, 0, false);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to create test.aif";

    crc64_ctx *ctx           = aaruf_crc64_init();
    uint64_t   generated_crc = 0;

    // Write data in sectors of 512 bytes
    for(size_t sector = 0; sector < total_sectors; ++sector)
    {
        const size_t  buffer_offset = sector * 512 % 1048576;  // Roll back to start when reaching end
        const int32_t write_result =
            aaruf_write_sector(context, sector, false, buffer + buffer_offset, SectorStatusDumped, 512);
        ASSERT_EQ(write_result, AARUF_STATUS_OK) << "Failed to write sector " << sector;

        aaruf_crc64_update(ctx, buffer + buffer_offset, 512);
    }

    aaruf_crc64_final(ctx, &generated_crc);
    aaruf_crc64_free(ctx);

    // Close the image
    int32_t close_result = aaruf_close(context);
    ASSERT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
    free(buffer);

    // Reopen the image
    context = aaruf_open("test.aif");
    ASSERT_NE(context, nullptr) << "Failed to open test.aif";

    // Get image info to verify it's a valid image
    ImageInfo     image_info;
    const int32_t result = aaruf_get_image_info(context, &image_info);

    ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to get image info";

    // Basic sanity checks on the image info
    ASSERT_EQ(image_info.HasPartitions, false) << "Image should not have partitions";
    ASSERT_EQ(image_info.HasSessions, false) << "Image should not have sessions";
    ASSERT_EQ(image_info.Sectors, total_sectors) << "Unexpected number of sectors";
    ASSERT_EQ(image_info.SectorSize, 512) << "Unexpected sector size";
    ASSERT_STREQ(image_info.Version, "2.0") << "Unexpected image version";
    ASSERT_STREQ(image_info.Application, "gtest") << "Unexpected application name";
    ASSERT_STREQ(image_info.ApplicationVersion, "0.0") << "Unexpected application version";
    ASSERT_EQ(image_info.MediaType, 1) << "Unexpected media type";
    ASSERT_EQ(image_info.MetadataMediaType, 1) << "Unexpected metadata media type";

    ctx          = aaruf_crc64_init();
    uint64_t crc = 0;

    for(int i = 0; i < total_sectors; i++)
    {
        uint8_t  sector_buffer[512];
        uint32_t length = sizeof(sector_buffer);

        const int32_t read_result = aaruf_read_sector(context, i, false, sector_buffer, &length);
        EXPECT_EQ(read_result, AARUF_STATUS_OK) << "Failed to read sector " << i;
        EXPECT_EQ(length, 512U) << "Unexpected length for sector " << i;
        aaruf_crc64_update(ctx, sector_buffer, 512);
    }

    aaruf_crc64_final(ctx, &crc);
    aaruf_crc64_free(ctx);

    ASSERT_EQ(crc, generated_crc) << "Unexpected CRC64 for image data";

    // Close the image
    close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
}