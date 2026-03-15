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

    constexpr size_t total_sectors = 128 * 1024 * 1024 / 512;  // 128 MiB / 512 bytes

    // Create image
    void *context = aaruf_create("test.aif", 1, 512, total_sectors, 0, 0, "deduplicate=false;compress=false",
                                 reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);

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
    context = aaruf_open("test.aif", false, NULL);
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
        uint32_t length        = sizeof(sector_buffer);
        uint8_t  sector_status = 0;

        const int32_t read_result = aaruf_read_sector(context, i, false, sector_buffer, &length, &sector_status);
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

    constexpr size_t total_sectors = 128 * 1024 * 1024 / 512;  // 128 MiB / 512 bytes

    // Create image
    void *context = aaruf_create("test.aif", 1, 512, total_sectors, 0, 0, "deduplicate=true;compress=false",
                                 reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);

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
    context = aaruf_open("test.aif", false, NULL);
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
        uint32_t length        = sizeof(sector_buffer);
        uint8_t  sector_status = 0;

        const int32_t read_result = aaruf_read_sector(context, i, false, sector_buffer, &length, &sector_status);
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

    constexpr size_t total_sectors = 128 * 1024 * 1024 / 512;  // 128 MiB / 512 bytes

    // Create image
    void *context = aaruf_create("test.aif", 1, 512, total_sectors, 0, 0, "deduplicate=false;compress=true",
                                 reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);

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
    context = aaruf_open("test.aif", false, NULL);
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
        uint32_t length        = sizeof(sector_buffer);
        uint8_t  sector_status = 0;

        const int32_t read_result = aaruf_read_sector(context, i, false, sector_buffer, &length, &sector_status);
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

TEST_F(CreateImageFixture, create_image_compresed_deduplicated)
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

    constexpr size_t total_sectors = 128 * 1024 * 1024 / 512;  // 128 MiB / 512 bytes

    // Create image
    void *context = aaruf_create("test.aif", 1, 512, total_sectors, 0, 0, "deduplicate=true;compress=true",
                                 reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);

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
    context = aaruf_open("test.aif", false, NULL);
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
        uint32_t length        = sizeof(sector_buffer);
        uint8_t  sector_status = 0;

        const int32_t read_result = aaruf_read_sector(context, i, false, sector_buffer, &length, &sector_status);
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

TEST_F(CreateImageFixture, create_image_resume)
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

    constexpr size_t total_sectors = 128 * 1024 * 1024 / 512;  // 128 MiB / 512 bytes

    // Create image
    void *context = aaruf_create("test.aif", 1, 512, total_sectors, 0, 0, "deduplicate=true;compress=true",
                                 reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to create test.aif";

    crc64_ctx *ctx           = aaruf_crc64_init();
    uint64_t   generated_crc = 0;

    // Write data in sectors of 512 bytes
    for(size_t sector = 0; sector < total_sectors / 2; ++sector)
    {
        const size_t  buffer_offset = sector * 512 % 1048576;  // Roll back to start when reaching end
        const int32_t write_result =
            aaruf_write_sector(context, sector, false, buffer + buffer_offset, SectorStatusDumped, 512);
        ASSERT_EQ(write_result, AARUF_STATUS_OK) << "Failed to write sector " << sector;

        aaruf_crc64_update(ctx, buffer + buffer_offset, 512);
    }

    // Close the image
    int32_t close_result = aaruf_close(context);
    ASSERT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";

    context = aaruf_open("test.aif", true, "deduplicate=true;compress=true");

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to re-open test.aif for resume";

    // Write data in sectors of 512 bytes
    for(size_t sector = total_sectors / 2; sector < total_sectors; ++sector)
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
    close_result = aaruf_close(context);
    ASSERT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
    free(buffer);

    // Reopen the image
    context = aaruf_open("test.aif", false, NULL);
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
        uint32_t length        = sizeof(sector_buffer);
        uint8_t  sector_status = 0;

        const int32_t read_result = aaruf_read_sector(context, i, false, sector_buffer, &length, &sector_status);
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

TEST_F(CreateImageFixture, create_image_table_shift_9)
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

    constexpr size_t total_sectors = 128 * 1024 * 1024 / 512;  // 128 MiB / 512 bytes

    // Create image
    void *context = aaruf_create("test.aif", 1, 512, total_sectors, 0, 0, "table_shift=9",
                                 reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);

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
    context = aaruf_open("test.aif", false, NULL);
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
        uint32_t length        = sizeof(sector_buffer);
        uint8_t  sector_status = 0;

        const int32_t read_result = aaruf_read_sector(context, i, false, sector_buffer, &length, &sector_status);
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

TEST_F(CreateImageFixture, create_audio_image)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];

    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/audio.bin", path);

    // Open audio file
    FILE *f = fopen(filename, "rb");
    ASSERT_NE(f, nullptr) << "Failed to open audio.bin data file";

    // Get file size
    fseek(f, 0, SEEK_END);
    const long audio_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Read entire audio file into buffer
    uint8_t *buffer = static_cast<uint8_t *>(malloc(audio_size));
    ASSERT_NE(buffer, nullptr) << "Failed to allocate memory for audio data";

    size_t bytes_read = fread(buffer, 1, audio_size, f);
    fclose(f);
    ASSERT_EQ(bytes_read, static_cast<size_t>(audio_size)) << "Failed to read complete audio data";

    // Calculate total size: three times the audio.bin size
    const size_t total_size = audio_size * 3;

    // Create image with default options (NULL for options means defaults)
    void *context = aaruf_create("test_audio.aif", 11, 2352, total_size / 2352, 0, 0, NULL,
                                 reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to create test_audio.aif";

    // Set up a single audio track spanning the entire media
    TrackEntry track;
    memset(&track, 0, sizeof(TrackEntry));
    track.sequence = 1;                            // Track 1
    track.type     = kTrackTypeAudio;                        // Audio track type (0)
    track.start    = 0;                            // Start at sector 0
    track.end      = (audio_size * 3 / 2352) - 1;  // End at last sector (inclusive)
    track.pregap   = 0;                            // No pregap
    track.session  = 1;                            // Session 1
    memset(track.isrc, 0, 13);                     // No ISRC
    track.flags = 0;                               // No special flags

    int32_t track_result = aaruf_set_tracks(context, &track, 1);
    ASSERT_EQ(track_result, AARUF_STATUS_OK) << "Failed to set tracks";

    crc64_ctx *ctx           = aaruf_crc64_init();
    uint64_t   generated_crc = 0;

    // Write audio data three times using write_sector_long
    size_t total_sectors = total_size / 2352;

    for(size_t sector = 0; sector < total_sectors; ++sector)
    {
        // Calculate offset in the original audio buffer (wrap around after each iteration)
        const size_t  buffer_offset = (sector * 2352) % audio_size;
        const int32_t write_result =
            aaruf_write_sector_long(context, sector, false, buffer + buffer_offset, SectorStatusDumped, 2352);
        ASSERT_EQ(write_result, AARUF_STATUS_OK) << "Failed to write sector " << sector;

        aaruf_crc64_update(ctx, buffer + buffer_offset, 2352);
    }

    aaruf_crc64_final(ctx, &generated_crc);
    aaruf_crc64_free(ctx);

    // Close the image
    int32_t close_result = aaruf_close(context);
    ASSERT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
    free(buffer);

    // Reopen the image
    context = aaruf_open("test_audio.aif", false, NULL);
    ASSERT_NE(context, nullptr) << "Failed to open test_audio.aif";

    // Get image info to verify it's a valid image
    ImageInfo     image_info;
    const int32_t result = aaruf_get_image_info(context, &image_info);

    ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to get image info";

    // Basic sanity checks on the image info
    ASSERT_EQ(image_info.HasPartitions, true) << "Image should not have partitions";
    ASSERT_EQ(image_info.HasSessions, true) << "Image should not have sessions";
    ASSERT_EQ(image_info.Sectors, total_sectors) << "Unexpected number of sectors";
    ASSERT_EQ(image_info.SectorSize, 2352) << "Unexpected sector size";
    ASSERT_STREQ(image_info.Version, "2.0") << "Unexpected image version";
    ASSERT_STREQ(image_info.Application, "gtest") << "Unexpected application name";
    ASSERT_STREQ(image_info.ApplicationVersion, "0.0") << "Unexpected application version";
    ASSERT_EQ(image_info.MediaType, 11) << "Unexpected media type";
    ASSERT_EQ(image_info.MetadataMediaType, 0) << "Unexpected metadata media type";

    ctx          = aaruf_crc64_init();
    uint64_t crc = 0;

    for(size_t i = 0; i < total_sectors; i++)
    {
        uint8_t  sector_buffer[2352];
        uint32_t length        = sizeof(sector_buffer);
        uint8_t  sector_status = 0;

        const int32_t read_result = aaruf_read_sector_long(context, i, false, sector_buffer, &length, &sector_status);
        EXPECT_EQ(read_result, AARUF_STATUS_OK) << "Failed to read sector " << i;
        EXPECT_EQ(length, 2352U) << "Unexpected length for sector " << i;
        aaruf_crc64_update(ctx, sector_buffer, 2352);
    }

    aaruf_crc64_final(ctx, &crc);
    aaruf_crc64_free(ctx);

    ASSERT_EQ(crc, generated_crc) << "Unexpected CRC64 for image data";

    // Close the image
    close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
}

TEST_F(CreateImageFixture, create_image_negative_sectors)
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

    constexpr size_t total_sectors = 128 * 1024 * 1024 / 512;  // 128 MiB / 512 bytes

    // Create image
    void *context = aaruf_create("test.aif", 1, 512, total_sectors, 300, 0, "deduplicate=true;compress=true",
                                 reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to create test.aif";

    crc64_ctx *ctx           = aaruf_crc64_init();
    uint64_t   generated_crc = 0;

    // Write data in sectors of 512 bytes
    for(size_t sector = 1; sector <= 150; ++sector)
    {
        const size_t  buffer_offset = sector * 512 % 1048576;  // Roll back to start when reaching end
        const int32_t write_result =
            aaruf_write_sector(context, sector, true, buffer + buffer_offset, SectorStatusDumped, 512);
        ASSERT_EQ(write_result, AARUF_STATUS_OK) << "Failed to write negative sector " << sector;

        aaruf_crc64_update(ctx, buffer + buffer_offset, 512);
    }

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
    context = aaruf_open("test.aif", false, NULL);
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

    for(int i = 1; i <= 150; i++)
    {
        uint8_t  sector_buffer[512];
        uint32_t length        = sizeof(sector_buffer);
        uint8_t  sector_status = 0;

        const int32_t read_result = aaruf_read_sector(context, i, true, sector_buffer, &length, &sector_status);
        EXPECT_EQ(read_result, AARUF_STATUS_OK) << "Failed to read negative sector " << i;
        EXPECT_EQ(length, 512U) << "Unexpected length for negative sector " << i;
        aaruf_crc64_update(ctx, sector_buffer, 512);
    }

    for(int i = 0; i < total_sectors; i++)
    {
        uint8_t  sector_buffer[512];
        uint32_t length        = sizeof(sector_buffer);
        uint8_t  sector_status = 0;

        const int32_t read_result = aaruf_read_sector(context, i, false, sector_buffer, &length, &sector_status);
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

TEST_F(CreateImageFixture, create_subchannel_uncompressed_image)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];

    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/audio.bin", path);

    // Open audio file
    FILE *f = fopen(filename, "rb");
    ASSERT_NE(f, nullptr) << "Failed to open audio.bin data file";

    // Get file size
    fseek(f, 0, SEEK_END);
    const long audio_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Read entire audio file into buffer
    uint8_t *buffer = static_cast<uint8_t *>(malloc(audio_size));
    ASSERT_NE(buffer, nullptr) << "Failed to allocate memory for audio data";

    size_t bytes_read = fread(buffer, 1, audio_size, f);
    fclose(f);
    ASSERT_EQ(bytes_read, static_cast<size_t>(audio_size)) << "Failed to read complete audio data";

    // Calculate total size: three times the audio.bin size
    const size_t total_size = audio_size * 3;

    // Create image with default options (NULL for options means defaults)
    void *context = aaruf_create("test_audio.aif", 11, 2352, total_size / 2352, 0, 0, "compress=false",
                                 reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to create test_audio.aif";

    // Set up a single audio track spanning the entire media
    TrackEntry track;
    memset(&track, 0, sizeof(TrackEntry));
    track.sequence = 1;                            // Track 1
    track.type     = kTrackTypeAudio;                        // Audio track type (0)
    track.start    = 0;                            // Start at sector 0
    track.end      = (audio_size * 3 / 2352) - 1;  // End at last sector (inclusive)
    track.pregap   = 0;                            // No pregap
    track.session  = 1;                            // Session 1
    memset(track.isrc, 0, 13);                     // No ISRC
    track.flags = 0;                               // No special flags

    int32_t track_result = aaruf_set_tracks(context, &track, 1);
    ASSERT_EQ(track_result, AARUF_STATUS_OK) << "Failed to set tracks";

    crc64_ctx *ctx           = aaruf_crc64_init();
    uint64_t   generated_crc = 0;

    // Write audio data three times using write_sector_long
    size_t total_sectors = total_size / 2352;

    for(size_t sector = 0; sector < total_sectors; ++sector)
    {
        // Calculate offset in the original audio buffer (wrap around after each iteration)
        const size_t  buffer_offset = (sector * 2352) % audio_size;
        const int32_t write_result =
            aaruf_write_sector_long(context, sector, false, buffer + buffer_offset, SectorStatusDumped, 2352);
        ASSERT_EQ(write_result, AARUF_STATUS_OK) << "Failed to write sector " << sector;

        aaruf_crc64_update(ctx, buffer + buffer_offset, 2352);
    }

    aaruf_crc64_final(ctx, &generated_crc);
    aaruf_crc64_free(ctx);

    // Write subchannel data for each sector and calculate CRC64
    crc64_ctx *subchannel_ctx           = aaruf_crc64_init();
    uint64_t   generated_subchannel_crc = 0;
    uint8_t    subchannel_data[96];

    for(size_t sector = 0; sector < total_sectors; ++sector)
    {
        // Generate subchannel pattern based on sector number (96 bytes per sector)
        for(size_t i = 0; i < 96; ++i) { subchannel_data[i] = static_cast<uint8_t>((sector * 96 + i) & 0xFF); }

        const int32_t subchannel_result =
            aaruf_write_sector_tag(context, sector, false, subchannel_data, 96, kSectorTagCdSubchannel);
        ASSERT_EQ(subchannel_result, AARUF_STATUS_OK) << "Failed to write subchannel for sector " << sector;

        // Update CRC64 with the subchannel data we just wrote
        aaruf_crc64_update(subchannel_ctx, subchannel_data, 96);
    }

    aaruf_crc64_final(subchannel_ctx, &generated_subchannel_crc);
    aaruf_crc64_free(subchannel_ctx);

    // Close the image
    int32_t close_result = aaruf_close(context);
    ASSERT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
    free(buffer);

    // Reopen the image
    context = aaruf_open("test_audio.aif", false, NULL);
    ASSERT_NE(context, nullptr) << "Failed to open test_audio.aif";

    // Get image info to verify it's a valid image
    ImageInfo     image_info;
    const int32_t result = aaruf_get_image_info(context, &image_info);

    ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to get image info";

    // Basic sanity checks on the image info
    ASSERT_EQ(image_info.HasPartitions, true) << "Image should not have partitions";
    ASSERT_EQ(image_info.HasSessions, true) << "Image should not have sessions";
    ASSERT_EQ(image_info.Sectors, total_sectors) << "Unexpected number of sectors";
    ASSERT_EQ(image_info.SectorSize, 2352) << "Unexpected sector size";
    ASSERT_STREQ(image_info.Version, "2.0") << "Unexpected image version";
    ASSERT_STREQ(image_info.Application, "gtest") << "Unexpected application name";
    ASSERT_STREQ(image_info.ApplicationVersion, "0.0") << "Unexpected application version";
    ASSERT_EQ(image_info.MediaType, 11) << "Unexpected media type";
    ASSERT_EQ(image_info.MetadataMediaType, 0) << "Unexpected metadata media type";

    ctx          = aaruf_crc64_init();
    uint64_t crc = 0;

    for(size_t i = 0; i < total_sectors; i++)
    {
        uint8_t  sector_buffer[2352];
        uint32_t length        = sizeof(sector_buffer);
        uint8_t  sector_status = 0;

        const int32_t read_result = aaruf_read_sector_long(context, i, false, sector_buffer, &length, &sector_status);
        EXPECT_EQ(read_result, AARUF_STATUS_OK) << "Failed to read sector " << i;
        EXPECT_EQ(length, 2352U) << "Unexpected length for sector " << i;
        aaruf_crc64_update(ctx, sector_buffer, 2352);
    }

    aaruf_crc64_final(ctx, &crc);
    aaruf_crc64_free(ctx);

    ASSERT_EQ(crc, generated_crc) << "Unexpected CRC64 for image data";

    // Read back and verify subchannel data
    crc64_ctx *subchannel_read_ctx = aaruf_crc64_init();
    uint64_t   read_subchannel_crc = 0;

    for(size_t i = 0; i < total_sectors; i++)
    {
        uint8_t  subchannel_buffer[96];
        uint32_t subchannel_length = sizeof(subchannel_buffer);

        const int32_t subchannel_read_result =
            aaruf_read_sector_tag(context, i, false, subchannel_buffer, &subchannel_length, kSectorTagCdSubchannel);
        EXPECT_EQ(subchannel_read_result, AARUF_STATUS_OK) << "Failed to read subchannel for sector " << i;
        EXPECT_EQ(subchannel_length, 96U) << "Unexpected subchannel length for sector " << i;
        aaruf_crc64_update(subchannel_read_ctx, subchannel_buffer, 96);
    }

    aaruf_crc64_final(subchannel_read_ctx, &read_subchannel_crc);
    aaruf_crc64_free(subchannel_read_ctx);

    ASSERT_EQ(read_subchannel_crc, generated_subchannel_crc) << "Unexpected CRC64 for subchannel data";

    // Close the image
    close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
}

TEST_F(CreateImageFixture, create_subchannel_compressed_image)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];

    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/audio.bin", path);

    // Open audio file
    FILE *f = fopen(filename, "rb");
    ASSERT_NE(f, nullptr) << "Failed to open audio.bin data file";

    // Get file size
    fseek(f, 0, SEEK_END);
    const long audio_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Read entire audio file into buffer
    uint8_t *buffer = static_cast<uint8_t *>(malloc(audio_size));
    ASSERT_NE(buffer, nullptr) << "Failed to allocate memory for audio data";

    size_t bytes_read = fread(buffer, 1, audio_size, f);
    fclose(f);
    ASSERT_EQ(bytes_read, static_cast<size_t>(audio_size)) << "Failed to read complete audio data";

    // Calculate total size: three times the audio.bin size
    const size_t total_size = audio_size * 3;

    // Create image with default options (NULL for options means defaults)
    void *context = aaruf_create("test_audio.aif", 11, 2352, total_size / 2352, 0, 0, "compress=true",
                                 reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to create test_audio.aif";

    // Set up a single audio track spanning the entire media
    TrackEntry track;
    memset(&track, 0, sizeof(TrackEntry));
    track.sequence = 1;                            // Track 1
    track.type     = kTrackTypeAudio;                        // Audio track type (0)
    track.start    = 0;                            // Start at sector 0
    track.end      = (audio_size * 3 / 2352) - 1;  // End at last sector (inclusive)
    track.pregap   = 0;                            // No pregap
    track.session  = 1;                            // Session 1
    memset(track.isrc, 0, 13);                     // No ISRC
    track.flags = 0;                               // No special flags

    int32_t track_result = aaruf_set_tracks(context, &track, 1);
    ASSERT_EQ(track_result, AARUF_STATUS_OK) << "Failed to set tracks";

    crc64_ctx *ctx           = aaruf_crc64_init();
    uint64_t   generated_crc = 0;

    // Write audio data three times using write_sector_long
    size_t total_sectors = total_size / 2352;

    for(size_t sector = 0; sector < total_sectors; ++sector)
    {
        // Calculate offset in the original audio buffer (wrap around after each iteration)
        const size_t  buffer_offset = (sector * 2352) % audio_size;
        const int32_t write_result =
            aaruf_write_sector_long(context, sector, false, buffer + buffer_offset, SectorStatusDumped, 2352);
        ASSERT_EQ(write_result, AARUF_STATUS_OK) << "Failed to write sector " << sector;

        aaruf_crc64_update(ctx, buffer + buffer_offset, 2352);
    }

    aaruf_crc64_final(ctx, &generated_crc);
    aaruf_crc64_free(ctx);

    // Write subchannel data for each sector and calculate CRC64
    crc64_ctx *subchannel_ctx           = aaruf_crc64_init();
    uint64_t   generated_subchannel_crc = 0;
    uint8_t    subchannel_data[96];

    for(size_t sector = 0; sector < total_sectors; ++sector)
    {
        // Generate subchannel pattern based on sector number (96 bytes per sector)
        for(size_t i = 0; i < 96; ++i) { subchannel_data[i] = static_cast<uint8_t>((sector * 96 + i) & 0xFF); }

        const int32_t subchannel_result =
            aaruf_write_sector_tag(context, sector, false, subchannel_data, 96, kSectorTagCdSubchannel);
        ASSERT_EQ(subchannel_result, AARUF_STATUS_OK) << "Failed to write subchannel for sector " << sector;

        // Update CRC64 with the subchannel data we just wrote
        aaruf_crc64_update(subchannel_ctx, subchannel_data, 96);
    }

    aaruf_crc64_final(subchannel_ctx, &generated_subchannel_crc);
    aaruf_crc64_free(subchannel_ctx);

    // Close the image
    int32_t close_result = aaruf_close(context);
    ASSERT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
    free(buffer);

    // Reopen the image
    context = aaruf_open("test_audio.aif", false, NULL);
    ASSERT_NE(context, nullptr) << "Failed to open test_audio.aif";

    // Get image info to verify it's a valid image
    ImageInfo     image_info;
    const int32_t result = aaruf_get_image_info(context, &image_info);

    ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to get image info";

    // Basic sanity checks on the image info
    ASSERT_EQ(image_info.HasPartitions, true) << "Image should not have partitions";
    ASSERT_EQ(image_info.HasSessions, true) << "Image should not have sessions";
    ASSERT_EQ(image_info.Sectors, total_sectors) << "Unexpected number of sectors";
    ASSERT_EQ(image_info.SectorSize, 2352) << "Unexpected sector size";
    ASSERT_STREQ(image_info.Version, "2.0") << "Unexpected image version";
    ASSERT_STREQ(image_info.Application, "gtest") << "Unexpected application name";
    ASSERT_STREQ(image_info.ApplicationVersion, "0.0") << "Unexpected application version";
    ASSERT_EQ(image_info.MediaType, 11) << "Unexpected media type";
    ASSERT_EQ(image_info.MetadataMediaType, 0) << "Unexpected metadata media type";

    ctx          = aaruf_crc64_init();
    uint64_t crc = 0;

    for(size_t i = 0; i < total_sectors; i++)
    {
        uint8_t  sector_buffer[2352];
        uint32_t length        = sizeof(sector_buffer);
        uint8_t  sector_status = 0;

        const int32_t read_result = aaruf_read_sector_long(context, i, false, sector_buffer, &length, &sector_status);
        EXPECT_EQ(read_result, AARUF_STATUS_OK) << "Failed to read sector " << i;
        EXPECT_EQ(length, 2352U) << "Unexpected length for sector " << i;
        aaruf_crc64_update(ctx, sector_buffer, 2352);
    }

    aaruf_crc64_final(ctx, &crc);
    aaruf_crc64_free(ctx);

    ASSERT_EQ(crc, generated_crc) << "Unexpected CRC64 for image data";

    // Read back and verify subchannel data
    crc64_ctx *subchannel_read_ctx = aaruf_crc64_init();
    uint64_t   read_subchannel_crc = 0;

    for(size_t i = 0; i < total_sectors; i++)
    {
        uint8_t  subchannel_buffer[96];
        uint32_t subchannel_length = sizeof(subchannel_buffer);

        const int32_t subchannel_read_result =
            aaruf_read_sector_tag(context, i, false, subchannel_buffer, &subchannel_length, kSectorTagCdSubchannel);
        EXPECT_EQ(subchannel_read_result, AARUF_STATUS_OK) << "Failed to read subchannel for sector " << i;
        EXPECT_EQ(subchannel_length, 96U) << "Unexpected subchannel length for sector " << i;
        aaruf_crc64_update(subchannel_read_ctx, subchannel_buffer, 96);
    }

    aaruf_crc64_final(subchannel_read_ctx, &read_subchannel_crc);
    aaruf_crc64_free(subchannel_read_ctx);

    ASSERT_EQ(read_subchannel_crc, generated_subchannel_crc) << "Unexpected CRC64 for subchannel data";

    // Close the image
    close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
}