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

/**
 * @file sector_tags.cpp
 * @brief Round-trip tests for per-sector tags written and read through the sector tag API.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../include/aaruformat.h"
#include "gtest/gtest.h"

class SectorTagsFixture : public testing::Test
{
};

/// Blu-ray sector EDC written as a sector tag can be read back as one, and is stored as a Blu-ray EDC block.
TEST_F(SectorTagsFixture, BlurayEdc)
{
    const char *kFilename = "test_sector_tags_bd_edc.aif";
    void       *ctx       = aaruf_create(kFilename, BDROM, 2048, 8, 0, 0, "deduplicate=false;compress=false",
                                         reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);
    ASSERT_NE(ctx, nullptr);

    uint8_t data[2048];
    for(uint64_t i = 0; i < 8; i++)
    {
        memset(data, static_cast<int>(i), sizeof(data));
        ASSERT_EQ(aaruf_write_sector(ctx, i, false, data, SectorStatusDumped, sizeof(data)), AARUF_STATUS_OK);

        const uint8_t edc[4] = {static_cast<uint8_t>(0xE0 + i), 0x11, 0x22, static_cast<uint8_t>(i)};
        ASSERT_EQ(aaruf_write_sector_tag(ctx, i, false, edc, sizeof(edc), kSectorTagBdSectorEdc), AARUF_STATUS_OK);
    }

    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);

    for(uint64_t i = 0; i < 8; i++)
    {
        uint8_t  edc[4] = {0};
        uint32_t length = sizeof(edc);
        ASSERT_EQ(aaruf_read_sector_tag(ctx, i, false, edc, &length, kSectorTagBdSectorEdc), AARUF_STATUS_OK) << i;
        EXPECT_EQ(edc[0], 0xE0 + i);
        EXPECT_EQ(edc[1], 0x11);
        EXPECT_EQ(edc[2], 0x22);
        EXPECT_EQ(edc[3], i);
    }

    // Blu-ray long sectors include it
    uint8_t  sector[2052];
    uint32_t length = sizeof(sector);
    uint8_t  status = 0;
    ASSERT_EQ(aaruf_read_sector_long(ctx, 3, false, sector, &length, &status), AARUF_STATUS_OK);
    EXPECT_EQ(length, 2052u);
    EXPECT_EQ(sector[2048], 0xE3);

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(kFilename);
}

namespace
{
/// Writes 4 long sectors with tags of tag_size bytes to a new image of media_type, reopens it and returns the
/// readable sector tag flags.
std::vector<bool> ReadableTagsAfterLongWrite(const char *filename, const uint32_t media_type, const uint32_t tag_size)
{
    void *ctx = aaruf_create(filename, media_type, 512, 4, 0, 0, "deduplicate=false;compress=false",
                             reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);
    EXPECT_NE(ctx, nullptr);
    if(ctx == nullptr) return {};

    uint8_t sector[536];
    for(uint64_t i = 0; i < 4; i++)
    {
        memset(sector, static_cast<int>(i + 1), sizeof(sector));
        EXPECT_EQ(aaruf_write_sector_long(ctx, i, false, sector, SectorStatusDumped, 512 + tag_size), AARUF_STATUS_OK);
    }

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(filename, false, nullptr);
    EXPECT_NE(ctx, nullptr);
    if(ctx == nullptr) return {};

    size_t length = 0;
    EXPECT_EQ(aaruf_get_readable_sector_tags(ctx, nullptr, &length), AARUF_ERROR_BUFFER_TOO_SMALL);
    std::vector<uint8_t> flags(length);
    EXPECT_EQ(aaruf_get_readable_sector_tags(ctx, flags.data(), &length), AARUF_STATUS_OK);
    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(filename);

    std::vector<bool> result(length / sizeof(bool));
    for(size_t i = 0; i < result.size(); i++) result[i] = reinterpret_cast<const bool *>(flags.data())[i];
    return result;
}
}  // namespace

/// Apple Sony and Priam DataTower tags are reported as readable under their own tag type.
TEST_F(SectorTagsFixture, AppleSonyAndPriamReadableTags)
{
    const auto sony = ReadableTagsAfterLongWrite("test_sector_tags_sony.aif", AppleSonySS, 12);
    ASSERT_GT(sony.size(), static_cast<size_t>(kSectorTagPriamDataTower));
    EXPECT_TRUE(sony[kSectorTagAppleSony]);
    EXPECT_FALSE(sony[kSectorTagPriamDataTower]);
    EXPECT_FALSE(sony[kSectorTagAppleProfile]);

    const auto priam = ReadableTagsAfterLongWrite("test_sector_tags_priam.aif", PriamDataTower, 24);
    ASSERT_GT(priam.size(), static_cast<size_t>(kSectorTagPriamDataTower));
    EXPECT_TRUE(priam[kSectorTagPriamDataTower]);
    EXPECT_FALSE(priam[kSectorTagAppleSony]);
    EXPECT_FALSE(priam[kSectorTagAppleProfile]);
}

/// Apple tags are stored by sector address over the user area and overflow: long writes, tag writes, long reads and
/// tag reads agree even when the image has negative sectors, which cannot carry tags.
TEST_F(SectorTagsFixture, AppleTagsWithNegativeSectors)
{
    const char *kFilename = "test_sector_tags_apple_negative.aif";
    void       *ctx       = aaruf_create(kFilename, AppleSonySS, 512, 8, 2, 1, "deduplicate=false;compress=false",
                                         reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);
    ASSERT_NE(ctx, nullptr);

    uint8_t sector[524];

    // Even sectors written long, odd ones as data plus tag; sector 8 is the overflow sector
    for(uint64_t i = 0; i < 9; i++)
    {
        for(int b = 0; b < 524; b++) sector[b] = static_cast<uint8_t>(i * 7 + b);

        if(i % 2 == 0)
            ASSERT_EQ(aaruf_write_sector_long(ctx, i, false, sector, SectorStatusDumped, 524), AARUF_STATUS_OK) << i;
        else
        {
            ASSERT_EQ(aaruf_write_sector(ctx, i, false, sector, SectorStatusDumped, 512), AARUF_STATUS_OK) << i;
            ASSERT_EQ(aaruf_write_sector_tag(ctx, i, false, sector + 512, 12, kSectorTagAppleSony), AARUF_STATUS_OK)
                << i;
        }
    }

    // Negative sectors keep their data but cannot carry tags
    memset(sector, 0x5A, sizeof(sector));
    EXPECT_EQ(aaruf_write_sector(ctx, 1, true, sector, SectorStatusDumped, 512), AARUF_STATUS_OK);
    EXPECT_EQ(aaruf_write_sector_tag(ctx, 1, true, sector + 512, 12, kSectorTagAppleSony),
              AARUF_ERROR_SECTOR_OUT_OF_BOUNDS);
    EXPECT_EQ(aaruf_write_sector_long(ctx, 2, true, sector, SectorStatusDumped, 524), AARUF_ERROR_SECTOR_OUT_OF_BOUNDS);

    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);

    for(uint64_t i = 0; i < 9; i++)
    {
        uint8_t expected[524];
        for(int b = 0; b < 524; b++) expected[b] = static_cast<uint8_t>(i * 7 + b);

        uint8_t  buffer[524];
        uint32_t length = sizeof(buffer);
        uint8_t  status = 0;
        ASSERT_EQ(aaruf_read_sector_long(ctx, i, false, buffer, &length, &status), AARUF_STATUS_OK) << i;
        EXPECT_EQ(length, 524u) << i;
        EXPECT_EQ(memcmp(buffer, expected, 524), 0) << i;

        uint8_t tag[12];
        length = sizeof(tag);
        ASSERT_EQ(aaruf_read_sector_tag(ctx, i, false, tag, &length, kSectorTagAppleSony), AARUF_STATUS_OK) << i;
        EXPECT_EQ(memcmp(tag, expected + 512, 12), 0) << i;
    }

    // Negative sector: plain data on long reads, no tag
    uint8_t  buffer[524];
    uint32_t length = sizeof(buffer);
    uint8_t  status = 0;
    EXPECT_EQ(aaruf_read_sector_long(ctx, 1, true, buffer, &length, &status), AARUF_STATUS_OK);
    EXPECT_EQ(length, 512u);
    uint8_t tag[12];
    length = sizeof(tag);
    EXPECT_EQ(aaruf_read_sector_tag(ctx, 1, true, tag, &length, kSectorTagAppleSony),
              AARUF_ERROR_SECTOR_TAG_NOT_PRESENT);

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(kFilename);
}

/// MODE 2 subheaders can be written and read as sector tags, and are reported as readable.
TEST_F(SectorTagsFixture, CdMode2Subheader)
{
    const char *kFilename = "test_sector_tags_subheader.aif";
    void       *ctx       = aaruf_create(kFilename, CDROMXA, 2048, 8, 0, 0, "deduplicate=false;compress=false",
                                         reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);
    ASSERT_NE(ctx, nullptr);

    TrackEntry track{};
    track.sequence = 1;
    track.type     = kTrackTypeCdMode2Formless;
    track.start    = 0;
    track.end      = 7;
    track.session  = 1;
    ASSERT_EQ(aaruf_set_tracks(ctx, &track, 1), AARUF_STATUS_OK);

    static const uint8_t kSync[12] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    uint8_t              sector[2352];

    for(uint64_t i = 0; i < 7; i++)
    {
        memset(sector, static_cast<int>(i), sizeof(sector));
        memcpy(sector, kSync, sizeof(kSync));
        sector[12] = 0x00;
        sector[13] = 0x02;
        sector[14] = static_cast<uint8_t>(i);
        sector[15] = 0x02;
        for(int b = 0; b < 8; b++) sector[16 + b] = static_cast<uint8_t>(0x10 * i + b);
        sector[18] = sector[22] = 0x08;  // Form 1
        ASSERT_EQ(aaruf_write_sector_long(ctx, i, false, sector, SectorStatusDumped, sizeof(sector)), AARUF_STATUS_OK)
            << i;
    }

    // Sector 7: user data plus a subheader written as a tag
    memset(sector, 0x77, 2048);
    ASSERT_EQ(aaruf_write_sector(ctx, 7, false, sector, SectorStatusDumped, 2048), AARUF_STATUS_OK);
    const uint8_t subheader7[8] = {1, 2, 0x20, 4, 1, 2, 0x20, 4};
    ASSERT_EQ(aaruf_write_sector_tag(ctx, 7, false, subheader7, 8, kSectorTagCdSubHeader), AARUF_STATUS_OK);

    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);

    for(uint64_t i = 0; i < 7; i++)
    {
        uint8_t  subheader[8];
        uint32_t length = sizeof(subheader);
        ASSERT_EQ(aaruf_read_sector_tag(ctx, i, false, subheader, &length, kSectorTagCdSubHeader), AARUF_STATUS_OK)
            << i;
        for(int b = 0; b < 8; b++)
            EXPECT_EQ(subheader[b], b == 2 || b == 6 ? 0x08 : static_cast<uint8_t>(0x10 * i + b)) << i << " " << b;
    }

    uint8_t  subheader[8];
    uint32_t length = sizeof(subheader);
    ASSERT_EQ(aaruf_read_sector_tag(ctx, 7, false, subheader, &length, kSectorTagCdSubHeader), AARUF_STATUS_OK);
    EXPECT_EQ(memcmp(subheader, subheader7, 8), 0);

    size_t flags_length = 0;
    ASSERT_EQ(aaruf_get_readable_sector_tags(ctx, nullptr, &flags_length), AARUF_ERROR_BUFFER_TOO_SMALL);
    std::vector<uint8_t> flags(flags_length);
    ASSERT_EQ(aaruf_get_readable_sector_tags(ctx, flags.data(), &flags_length), AARUF_STATUS_OK);
    EXPECT_TRUE(reinterpret_cast<const bool *>(flags.data())[kSectorTagCdSubHeader]);

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(kFilename);
}
