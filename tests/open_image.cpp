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

    ASSERT_EQ(crc, 0x0439da56aa993cdf) << "Unexpected CRC64 for image data";

    // Close the image
    const int32_t close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
}

TEST_F(OpenImageFixture, open_mf2hd_v2)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];

    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/mf2hd.aif", path);

    // Attempt to open the image file
    void *context = aaruf_open(filename);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to open mf2hd.aif";

    // Get image info to verify it's a valid image
    ImageInfo     image_info;
    const int32_t result = aaruf_get_image_info(context, &image_info);

    ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to get image info";

    // Basic sanity checks on the image info
    ASSERT_EQ(image_info.HasPartitions, false) << "Image should not have partitions";
    ASSERT_EQ(image_info.HasSessions, false) << "Image should not have sessions";
    ASSERT_EQ(image_info.ImageSize, 49802) << "Unexpected image size";
    ASSERT_EQ(image_info.Sectors, 2880) << "Unexpected number of sectors";
    ASSERT_EQ(image_info.SectorSize, 512) << "Unexpected sector size";
    ASSERT_STREQ(image_info.Version, "2.0") << "Unexpected image version";
    ASSERT_STREQ(image_info.Application, "aaruformattool") << "Unexpected application name";
    ASSERT_STREQ(image_info.ApplicationVersion, "1.0") << "Unexpected application version";
    ASSERT_EQ(image_info.CreationTime, 134045317128230560ULL) << "Unexpected creation time";
    ASSERT_EQ(image_info.LastModificationTime, 134045317128230560ULL) << "Unexpected modification time";
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

    ASSERT_EQ(crc, 0x0439da56aa993cdf) << "Unexpected CRC64 for image data";

    // Close the image
    const int32_t close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
}

TEST_F(OpenImageFixture, open_floptical_v1)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];

    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/floptical_v1.aif", path);

    // Attempt to open the image file
    void *context = aaruf_open(filename);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to open floptical_v1.aif";

    // Get image info to verify it's a valid image
    ImageInfo     image_info;
    const int32_t result = aaruf_get_image_info(context, &image_info);

    ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to get image info";

    // Basic sanity checks on the image info
    ASSERT_EQ(image_info.HasPartitions, false) << "Image should not have partitions";
    ASSERT_EQ(image_info.HasSessions, false) << "Image should not have sessions";
    ASSERT_EQ(image_info.ImageSize, 248) << "Unexpected image size";
    ASSERT_EQ(image_info.Sectors, 40662) << "Unexpected number of sectors";
    ASSERT_EQ(image_info.SectorSize, 512) << "Unexpected sector size";
    ASSERT_STREQ(image_info.Version, "1.0") << "Unexpected image version";
    ASSERT_STREQ(image_info.Application, "Aaru") << "Unexpected application name";
    ASSERT_STREQ(image_info.ApplicationVersion, "5.0") << "Unexpected application version";
    ASSERT_EQ(image_info.CreationTime, 132336289721517033ULL) << "Unexpected creation time";
    ASSERT_EQ(image_info.LastModificationTime, 132336294722834003ULL) << "Unexpected modification time";
    ASSERT_EQ(image_info.MediaType, 662) << "Unexpected media type";
    ASSERT_EQ(image_info.MetadataMediaType, 1) << "Unexpected metadata media type";

    crc64_ctx *ctx = aaruf_crc64_init();
    uint64_t   crc = 0;

    for(int i = 0; i < 40662; i++)
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

    ASSERT_EQ(crc, 0x924451887b380a43) << "Unexpected CRC64 for image data";

    // Close the image
    const int32_t close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
}

TEST_F(OpenImageFixture, open_floptical_v2)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];

    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/floptical.aif", path);

    // Attempt to open the image file
    void *context = aaruf_open(filename);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to open floptical.aif";

    // Get image info to verify it's a valid image
    ImageInfo     image_info;
    const int32_t result = aaruf_get_image_info(context, &image_info);

    ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to get image info";

    // Basic sanity checks on the image info
    ASSERT_EQ(image_info.HasPartitions, false) << "Image should not have partitions";
    ASSERT_EQ(image_info.HasSessions, false) << "Image should not have sessions";
    ASSERT_EQ(image_info.ImageSize, 142) << "Unexpected image size";
    ASSERT_EQ(image_info.Sectors, 40662) << "Unexpected number of sectors";
    ASSERT_EQ(image_info.SectorSize, 512) << "Unexpected sector size";
    ASSERT_STREQ(image_info.Version, "2.0") << "Unexpected image version";
    ASSERT_STREQ(image_info.Application, "aaruformattool") << "Unexpected application name";
    ASSERT_STREQ(image_info.ApplicationVersion, "1.0") << "Unexpected application version";
    ASSERT_EQ(image_info.CreationTime, 134045315488892290ULL) << "Unexpected creation time";
    ASSERT_EQ(image_info.LastModificationTime, 134045315488892290ULL) << "Unexpected modification time";
    ASSERT_EQ(image_info.MediaType, 662) << "Unexpected media type";
    ASSERT_EQ(image_info.MetadataMediaType, 1) << "Unexpected metadata media type";

    crc64_ctx *ctx = aaruf_crc64_init();
    uint64_t   crc = 0;

    for(int i = 0; i < 40662; i++)
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

    ASSERT_EQ(crc, 0x924451887b380a43) << "Unexpected CRC64 for image data";

    // Close the image
    const int32_t close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
}

TEST_F(OpenImageFixture, open_gigamo_v1)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];

    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/gigamo_v1.aif", path);

    // Attempt to open the image file
    void *context = aaruf_open(filename);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to open gigamo_v1.aif";

    // Get image info to verify it's a valid image
    ImageInfo     image_info;
    const int32_t result = aaruf_get_image_info(context, &image_info);

    ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to get image info";

    // Basic sanity checks on the image info
    ASSERT_EQ(image_info.HasPartitions, false) << "Image should not have partitions";
    ASSERT_EQ(image_info.HasSessions, false) << "Image should not have sessions";
    ASSERT_EQ(image_info.ImageSize, 2835) << "Unexpected image size";
    ASSERT_EQ(image_info.Sectors, 605846) << "Unexpected number of sectors";
    ASSERT_EQ(image_info.SectorSize, 2048) << "Unexpected sector size";
    ASSERT_STREQ(image_info.Version, "1.0") << "Unexpected image version";
    ASSERT_STREQ(image_info.Application, "Aaru") << "Unexpected application name";
    ASSERT_STREQ(image_info.ApplicationVersion, "4.5") << "Unexpected application version";
    ASSERT_EQ(image_info.CreationTime, 132285153477491878ULL) << "Unexpected creation time";
    ASSERT_EQ(image_info.LastModificationTime, 132285158903202659ULL) << "Unexpected modification time";
    ASSERT_EQ(image_info.MediaType, 653) << "Unexpected media type";
    ASSERT_EQ(image_info.MetadataMediaType, 1) << "Unexpected metadata media type";

    crc64_ctx *ctx = aaruf_crc64_init();
    uint64_t   crc = 0;

    for(int i = 0; i < 605846; i++)
    {
        uint8_t  buffer[2048];
        uint32_t length = sizeof(buffer);

        const int32_t read_result = aaruf_read_sector(context, i, false, buffer, &length);
        EXPECT_EQ(read_result, AARUF_STATUS_OK) << "Failed to read sector " << i;
        EXPECT_EQ(length, 2048U) << "Unexpected length for sector " << i;
        aaruf_crc64_update(ctx, buffer, 2048);
    }

    aaruf_crc64_final(ctx, &crc);
    aaruf_crc64_free(ctx);

    ASSERT_EQ(crc, 0x96c05a76c6f7385b) << "Unexpected CRC64 for image data";

    // Close the image
    const int32_t close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
}

TEST_F(OpenImageFixture, open_gigamo_v2)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];

    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/gigamo.aif", path);

    // Attempt to open the image file
    void *context = aaruf_open(filename);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to open gigamo.aif";

    // Get image info to verify it's a valid image
    ImageInfo     image_info;
    const int32_t result = aaruf_get_image_info(context, &image_info);

    ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to get image info";

    // Basic sanity checks on the image info
    ASSERT_EQ(image_info.HasPartitions, false) << "Image should not have partitions";
    ASSERT_EQ(image_info.HasSessions, false) << "Image should not have sessions";
    ASSERT_EQ(image_info.ImageSize, 790) << "Unexpected image size";
    ASSERT_EQ(image_info.Sectors, 605846) << "Unexpected number of sectors";
    ASSERT_EQ(image_info.SectorSize, 2048) << "Unexpected sector size";
    ASSERT_STREQ(image_info.Version, "2.0") << "Unexpected image version";
    ASSERT_STREQ(image_info.Application, "aaruformattool") << "Unexpected application name";
    ASSERT_STREQ(image_info.ApplicationVersion, "1.0") << "Unexpected application version";
    ASSERT_EQ(image_info.CreationTime, 134045315684449130ULL) << "Unexpected creation time";
    ASSERT_EQ(image_info.LastModificationTime, 134045315684449130ULL) << "Unexpected modification time";
    ASSERT_EQ(image_info.MediaType, 653) << "Unexpected media type";
    ASSERT_EQ(image_info.MetadataMediaType, 1) << "Unexpected metadata media type";

    crc64_ctx *ctx = aaruf_crc64_init();
    uint64_t   crc = 0;

    for(int i = 0; i < 605846; i++)
    {
        uint8_t  buffer[2048];
        uint32_t length = sizeof(buffer);

        const int32_t read_result = aaruf_read_sector(context, i, false, buffer, &length);
        EXPECT_EQ(read_result, AARUF_STATUS_OK) << "Failed to read sector " << i;
        EXPECT_EQ(length, 2048U) << "Unexpected length for sector " << i;
        aaruf_crc64_update(ctx, buffer, 2048);
    }

    aaruf_crc64_final(ctx, &crc);
    aaruf_crc64_free(ctx);

    ASSERT_EQ(crc, 0x96c05a76c6f7385b) << "Unexpected CRC64 for image data";

    // Close the image
    const int32_t close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
}

TEST_F(OpenImageFixture, open_hifd_v1)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];

    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/hifd_v1.aif", path);

    // Attempt to open the image file
    void *context = aaruf_open(filename);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to open hifd_v1.aif";

    // Get image info to verify it's a valid image
    ImageInfo     image_info;
    const int32_t result = aaruf_get_image_info(context, &image_info);

    ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to get image info";

    // Basic sanity checks on the image info
    ASSERT_EQ(image_info.HasPartitions, false) << "Image should not have partitions";
    ASSERT_EQ(image_info.HasSessions, false) << "Image should not have sessions";
    ASSERT_EQ(image_info.ImageSize, 1060) << "Unexpected image size";
    ASSERT_EQ(image_info.Sectors, 393380) << "Unexpected number of sectors";
    ASSERT_EQ(image_info.SectorSize, 512) << "Unexpected sector size";
    ASSERT_STREQ(image_info.Version, "1.0") << "Unexpected image version";
    ASSERT_STREQ(image_info.Application, "Aaru") << "Unexpected application name";
    ASSERT_STREQ(image_info.ApplicationVersion, "5.0") << "Unexpected application version";
    ASSERT_EQ(image_info.CreationTime, 132388121214348634ULL) << "Unexpected creation time";
    ASSERT_EQ(image_info.LastModificationTime, 132388122021523861ULL) << "Unexpected modification time";
    ASSERT_EQ(image_info.MediaType, 663) << "Unexpected media type";
    ASSERT_EQ(image_info.MetadataMediaType, 1) << "Unexpected metadata media type";

    crc64_ctx *ctx = aaruf_crc64_init();
    uint64_t   crc = 0;

    for(int i = 0; i < 393380; i++)
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

    ASSERT_EQ(crc, 0xcf6865ab1977f144) << "Unexpected CRC64 for image data";

    // Close the image
    const int32_t close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
}

TEST_F(OpenImageFixture, open_hifd_v2)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];

    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/hifd.aif", path);

    // Attempt to open the image file
    void *context = aaruf_open(filename);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to open hifd.aif";

    // Get image info to verify it's a valid image
    ImageInfo     image_info;
    const int32_t result = aaruf_get_image_info(context, &image_info);

    ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to get image info";

    // Basic sanity checks on the image info
    ASSERT_EQ(image_info.HasPartitions, false) << "Image should not have partitions";
    ASSERT_EQ(image_info.HasSessions, false) << "Image should not have sessions";
    ASSERT_EQ(image_info.ImageSize, 1035) << "Unexpected image size";
    ASSERT_EQ(image_info.Sectors, 393380) << "Unexpected number of sectors";
    ASSERT_EQ(image_info.SectorSize, 512) << "Unexpected sector size";
    ASSERT_STREQ(image_info.Version, "2.0") << "Unexpected image version";
    ASSERT_STREQ(image_info.Application, "aaruformattool") << "Unexpected application name";
    ASSERT_STREQ(image_info.ApplicationVersion, "1.0") << "Unexpected application version";
    ASSERT_EQ(image_info.CreationTime, 134045316523097400ULL) << "Unexpected creation time";
    ASSERT_EQ(image_info.LastModificationTime, 134045316523097400ULL) << "Unexpected modification time";
    ASSERT_EQ(image_info.MediaType, 663) << "Unexpected media type";
    ASSERT_EQ(image_info.MetadataMediaType, 1) << "Unexpected metadata media type";

    crc64_ctx *ctx = aaruf_crc64_init();
    uint64_t   crc = 0;

    for(int i = 0; i < 393380; i++)
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

    ASSERT_EQ(crc, 0xcf6865ab1977f144) << "Unexpected CRC64 for image data";

    // Close the image
    const int32_t close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
}

TEST_F(OpenImageFixture, open_mo540_v1)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];

    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/mo540_v1.aif", path);

    // Attempt to open the image file
    void *context = aaruf_open(filename);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to open mo540_v1.aif";

    // Get image info to verify it's a valid image
    ImageInfo     image_info;
    const int32_t result = aaruf_get_image_info(context, &image_info);

    ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to get image info";

    // Basic sanity checks on the image info
    ASSERT_EQ(image_info.HasPartitions, false) << "Image should not have partitions";
    ASSERT_EQ(image_info.HasSessions, false) << "Image should not have sessions";
    ASSERT_EQ(image_info.ImageSize, 2307) << "Unexpected image size";
    ASSERT_EQ(image_info.Sectors, 1041500) << "Unexpected number of sectors";
    ASSERT_EQ(image_info.SectorSize, 512) << "Unexpected sector size";
    ASSERT_STREQ(image_info.Version, "1.0") << "Unexpected image version";
    ASSERT_STREQ(image_info.Application, "Aaru") << "Unexpected application name";
    ASSERT_STREQ(image_info.ApplicationVersion, "4.5") << "Unexpected application version";
    ASSERT_EQ(image_info.CreationTime, 132285167887353561ULL) << "Unexpected creation time";
    ASSERT_EQ(image_info.LastModificationTime, 132285245306181873ULL) << "Unexpected modification time";
    ASSERT_EQ(image_info.MediaType, 1) << "Unexpected media type";
    ASSERT_EQ(image_info.MetadataMediaType, 1) << "Unexpected metadata media type";

    crc64_ctx *ctx = aaruf_crc64_init();
    uint64_t   crc = 0;

    for(int i = 0; i < 1041500; i++)
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

    ASSERT_EQ(crc, 0xe50350dde624cd75) << "Unexpected CRC64 for image data";

    // Close the image
    const int32_t close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
}

TEST_F(OpenImageFixture, open_mo540_v2)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];

    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/mo540.aif", path);

    // Attempt to open the image file
    void *context = aaruf_open(filename);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to open mo540.aif";

    // Get image info to verify it's a valid image
    ImageInfo     image_info;
    const int32_t result = aaruf_get_image_info(context, &image_info);

    ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to get image info";

    // Basic sanity checks on the image info
    ASSERT_EQ(image_info.HasPartitions, false) << "Image should not have partitions";
    ASSERT_EQ(image_info.HasSessions, false) << "Image should not have sessions";
    ASSERT_EQ(image_info.ImageSize, 1273) << "Unexpected image size";
    ASSERT_EQ(image_info.Sectors, 1041500) << "Unexpected number of sectors";
    ASSERT_EQ(image_info.SectorSize, 512) << "Unexpected sector size";
    ASSERT_STREQ(image_info.Version, "2.0") << "Unexpected image version";
    ASSERT_STREQ(image_info.Application, "aaruformattool") << "Unexpected application name";
    ASSERT_STREQ(image_info.ApplicationVersion, "1.0") << "Unexpected application version";
    ASSERT_EQ(image_info.CreationTime, 134045317276362850ULL) << "Unexpected creation time";
    ASSERT_EQ(image_info.LastModificationTime, 134045317276362850ULL) << "Unexpected modification time";
    ASSERT_EQ(image_info.MediaType, 1) << "Unexpected media type";
    ASSERT_EQ(image_info.MetadataMediaType, 1) << "Unexpected metadata media type";

    crc64_ctx *ctx = aaruf_crc64_init();
    uint64_t   crc = 0;

    for(int i = 0; i < 1041500; i++)
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

    ASSERT_EQ(crc, 0xe50350dde624cd75) << "Unexpected CRC64 for image data";

    // Close the image
    const int32_t close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
}

TEST_F(OpenImageFixture, open_mo640_v1)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];

    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/mo640_v1.aif", path);

    // Attempt to open the image file
    void *context = aaruf_open(filename);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to open mo640_v1.aif";

    // Get image info to verify it's a valid image
    ImageInfo     image_info;
    const int32_t result = aaruf_get_image_info(context, &image_info);

    ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to get image info";

    // Basic sanity checks on the image info
    ASSERT_EQ(image_info.HasPartitions, false) << "Image should not have partitions";
    ASSERT_EQ(image_info.HasSessions, false) << "Image should not have sessions";
    ASSERT_EQ(image_info.ImageSize, 8655) << "Unexpected image size";
    ASSERT_EQ(image_info.Sectors, 310352) << "Unexpected number of sectors";
    ASSERT_EQ(image_info.SectorSize, 2048) << "Unexpected sector size";
    ASSERT_STREQ(image_info.Version, "1.0") << "Unexpected image version";
    ASSERT_STREQ(image_info.Application, "Aaru") << "Unexpected application name";
    ASSERT_STREQ(image_info.ApplicationVersion, "4.5") << "Unexpected application version";
    ASSERT_EQ(image_info.CreationTime, 132285163973454137ULL) << "Unexpected creation time";
    ASSERT_EQ(image_info.LastModificationTime, 132285166863805792ULL) << "Unexpected modification time";
    ASSERT_EQ(image_info.MediaType, 646) << "Unexpected media type";
    ASSERT_EQ(image_info.MetadataMediaType, 1) << "Unexpected metadata media type";

    crc64_ctx *ctx = aaruf_crc64_init();
    uint64_t   crc = 0;

    for(int i = 0; i < 310352; i++)
    {
        uint8_t  buffer[2048];
        uint32_t length = sizeof(buffer);

        const int32_t read_result = aaruf_read_sector(context, i, false, buffer, &length);
        EXPECT_EQ(read_result, AARUF_STATUS_OK) << "Failed to read sector " << i;
        EXPECT_EQ(length, 2048U) << "Unexpected length for sector " << i;
        aaruf_crc64_update(ctx, buffer, 2048);
    }

    aaruf_crc64_final(ctx, &crc);
    aaruf_crc64_free(ctx);

    ASSERT_EQ(crc, 0x6761926a7b8c1f54) << "Unexpected CRC64 for image data";

    // Close the image
    const int32_t close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
}

TEST_F(OpenImageFixture, open_mo640_v2)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];

    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/mo640.aif", path);

    // Attempt to open the image file
    void *context = aaruf_open(filename);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to open mo640.aif";

    // Get image info to verify it's a valid image
    ImageInfo     image_info;
    const int32_t result = aaruf_get_image_info(context, &image_info);

    ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to get image info";

    // Basic sanity checks on the image info
    ASSERT_EQ(image_info.HasPartitions, false) << "Image should not have partitions";
    ASSERT_EQ(image_info.HasSessions, false) << "Image should not have sessions";
    ASSERT_EQ(image_info.ImageSize, 556) << "Unexpected image size";
    ASSERT_EQ(image_info.Sectors, 310352) << "Unexpected number of sectors";
    ASSERT_EQ(image_info.SectorSize, 2048) << "Unexpected sector size";
    ASSERT_STREQ(image_info.Version, "2.0") << "Unexpected image version";
    ASSERT_STREQ(image_info.Application, "aaruformattool") << "Unexpected application name";
    ASSERT_STREQ(image_info.ApplicationVersion, "1.0") << "Unexpected application version";
    ASSERT_EQ(image_info.CreationTime, 134045318614141420ULL) << "Unexpected creation time";
    ASSERT_EQ(image_info.LastModificationTime, 134045318614141420ULL) << "Unexpected modification time";
    ASSERT_EQ(image_info.MediaType, 646) << "Unexpected media type";
    ASSERT_EQ(image_info.MetadataMediaType, 1) << "Unexpected metadata media type";

    crc64_ctx *ctx = aaruf_crc64_init();
    uint64_t   crc = 0;

    for(int i = 0; i < 310352; i++)
    {
        uint8_t  buffer[2048];
        uint32_t length = sizeof(buffer);

        const int32_t read_result = aaruf_read_sector(context, i, false, buffer, &length);
        EXPECT_EQ(read_result, AARUF_STATUS_OK) << "Failed to read sector " << i;
        EXPECT_EQ(length, 2048U) << "Unexpected length for sector " << i;
        aaruf_crc64_update(ctx, buffer, 2048);
    }

    aaruf_crc64_final(ctx, &crc);
    aaruf_crc64_free(ctx);

    ASSERT_EQ(crc, 0x6761926a7b8c1f54) << "Unexpected CRC64 for image data";

    // Close the image
    const int32_t close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
}

TEST_F(OpenImageFixture, open_cdmode1_v1)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];

    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/cdmode1_v1.aif", path);

    // Attempt to open the image file
    void *context = aaruf_open(filename);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to open cdmode1_v1.aif";

    // Get image info to verify it's a valid image
    ImageInfo     image_info;
    const int32_t result = aaruf_get_image_info(context, &image_info);

    ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to get image info";

    // Basic sanity checks on the image info
    ASSERT_EQ(image_info.HasPartitions, true) << "Image should not have partitions";
    ASSERT_EQ(image_info.HasSessions, true) << "Image should not have sessions";
    ASSERT_EQ(image_info.ImageSize, 4406638) << "Unexpected image size";
    ASSERT_EQ(image_info.Sectors, 9120) << "Unexpected number of sectors";
    ASSERT_EQ(image_info.SectorSize, 2048) << "Unexpected sector size";
    ASSERT_STREQ(image_info.Version, "1.0") << "Unexpected image version";
    ASSERT_STREQ(image_info.Application, "Aaru") << "Unexpected application name";
    ASSERT_STREQ(image_info.ApplicationVersion, "5.3") << "Unexpected application version";
    ASSERT_EQ(image_info.CreationTime, 134045761441240199ULL) << "Unexpected creation time";
    ASSERT_EQ(image_info.LastModificationTime, 134045761618355140ULL) << "Unexpected modification time";
    ASSERT_EQ(image_info.MediaType, 15) << "Unexpected media type";
    ASSERT_EQ(image_info.MetadataMediaType, 0) << "Unexpected metadata media type";

    crc64_ctx *ctx         = aaruf_crc64_init();
    uint64_t   crc         = 0;
    void      *ecc_context = aaruf_ecc_cd_init();

    for(int i = 0; i < 9120; i++)
    {
        uint8_t  buffer[2352];
        uint32_t length = sizeof(buffer);

        const int32_t read_result = aaruf_read_sector_long(context, i, false, buffer, &length);
        EXPECT_EQ(read_result, AARUF_STATUS_OK) << "Failed to read sector " << i;
        EXPECT_EQ(length, 2352U) << "Unexpected length for sector " << i;
        aaruf_crc64_update(ctx, buffer, 2352);
        EXPECT_EQ(aaruf_ecc_cd_is_suffix_correct(ecc_context, buffer), true)
            << "CD sector " << i << " has invalid ECC suffix";
    }

    aaruf_crc64_final(ctx, &crc);
    aaruf_crc64_free(ctx);

    ASSERT_EQ(crc, 0xAE1014831CD81711) << "Unexpected CRC64 for image data";

    // Close the image
    const int32_t close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
}

TEST_F(OpenImageFixture, open_cdmode1_v2)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];

    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/cdmode1.aif", path);

    // Attempt to open the image file
    void *context = aaruf_open(filename);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to open cdmode1.aif";

    // Get image info to verify it's a valid image
    ImageInfo     image_info;
    const int32_t result = aaruf_get_image_info(context, &image_info);

    ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to get image info";

    // Basic sanity checks on the image info
    ASSERT_EQ(image_info.HasPartitions, true) << "Image should not have partitions";
    ASSERT_EQ(image_info.HasSessions, true) << "Image should not have sessions";
    ASSERT_EQ(image_info.ImageSize, 4385095) << "Unexpected image size";
    ASSERT_EQ(image_info.Sectors, 9120) << "Unexpected number of sectors";
    ASSERT_EQ(image_info.SectorSize, 2048) << "Unexpected sector size";
    ASSERT_STREQ(image_info.Version, "2.0") << "Unexpected image version";
    ASSERT_STREQ(image_info.Application, "aaruformattool") << "Unexpected application name";
    ASSERT_STREQ(image_info.ApplicationVersion, "1.0") << "Unexpected application version";
    ASSERT_EQ(image_info.CreationTime, 134045908449642430ULL) << "Unexpected creation time";
    ASSERT_EQ(image_info.LastModificationTime, 134045908449642430ULL) << "Unexpected modification time";
    ASSERT_EQ(image_info.MediaType, 15) << "Unexpected media type";
    ASSERT_EQ(image_info.MetadataMediaType, 0) << "Unexpected metadata media type";

    crc64_ctx *ctx         = aaruf_crc64_init();
    uint64_t   crc         = 0;
    void      *ecc_context = aaruf_ecc_cd_init();

    for(int i = 0; i < 9120; i++)
    {
        uint8_t  buffer[2352];
        uint32_t length = sizeof(buffer);

        const int32_t read_result = aaruf_read_sector_long(context, i, false, buffer, &length);
        EXPECT_EQ(read_result, AARUF_STATUS_OK) << "Failed to read sector " << i;
        EXPECT_EQ(length, 2352U) << "Unexpected length for sector " << i;
        aaruf_crc64_update(ctx, buffer, 2352);
        EXPECT_EQ(aaruf_ecc_cd_is_suffix_correct(ecc_context, buffer), true)
            << "CD sector " << i << " has invalid ECC suffix";
    }

    aaruf_crc64_final(ctx, &crc);
    aaruf_crc64_free(ctx);

    ASSERT_EQ(crc, 0xAE1014831CD81711) << "Unexpected CRC64 for image data";

    // Close the image
    const int32_t close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close image";
}

TEST_F(OpenImageFixture, open_cdmode2_v1)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];

    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/cdmode2_v1.aif", path);

    // Attempt to open the image file
    void *context = aaruf_open(filename);

    // Verify that the file was successfully opened
    ASSERT_NE(context, nullptr) << "Failed to open cdmode2_v1.aif";

    // Get image info to verify it's a valid image
    ImageInfo     image_info;
    const int32_t result = aaruf_get_image_info(context, &image_info);

    ASSERT_EQ(result, AARUF_STATUS_OK) << "Failed to get image info";

    // Basic sanity checks on the image info
    ASSERT_EQ(image_info.HasPartitions, true) << "Image should not have partitions";
    ASSERT_EQ(image_info.HasSessions, true) << "Image should not have sessions";
    ASSERT_EQ(image_info.ImageSize, 4406729) << "Unexpected image size";
    ASSERT_EQ(image_info.Sectors, 9120) << "Unexpected number of sectors";
    ASSERT_EQ(image_info.SectorSize, 2048) << "Unexpected sector size";
    ASSERT_STREQ(image_info.Version, "1.0") << "Unexpected image version";
    ASSERT_STREQ(image_info.Application, "Aaru") << "Unexpected application name";
    ASSERT_STREQ(image_info.ApplicationVersion, "5.3") << "Unexpected application version";
    ASSERT_EQ(image_info.CreationTime, 134045763501979250ULL) << "Unexpected creation time";
    ASSERT_EQ(image_info.LastModificationTime, 134045763674961818ULL) << "Unexpected modification time";
    ASSERT_EQ(image_info.MediaType, 16) << "Unexpected media type";
    ASSERT_EQ(image_info.MetadataMediaType, 0) << "Unexpected metadata media type";

    crc64_ctx *ctx         = aaruf_crc64_init();
    uint64_t   crc         = 0;
    void      *ecc_context = aaruf_ecc_cd_init();

    for(int i = 0; i < 9120; i++)
    {
        uint8_t  buffer[2352];
        uint32_t length = sizeof(buffer);

        const int32_t read_result = aaruf_read_sector_long(context, i, false, buffer, &length);
        EXPECT_EQ(read_result, AARUF_STATUS_OK) << "Failed to read sector " << i;
        EXPECT_EQ(length, 2352U) << "Unexpected length for sector " << i;
        aaruf_crc64_update(ctx, buffer, 2352);
        EXPECT_EQ(aaruf_ecc_cd_is_suffix_correct_mode2(ecc_context, buffer), true)
            << "CD sector " << i << " has invalid ECC suffix";
    }

    aaruf_crc64_final(ctx, &crc);
    aaruf_crc64_free(ctx);

    ASSERT_EQ(crc, 0x5FD9D5979AD99BAB) << "Unexpected CRC64 for image data";

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
