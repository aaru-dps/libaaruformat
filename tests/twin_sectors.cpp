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
 * @file twin_sectors.cpp
 * @brief Tests for twin sectors (several physical sectors sharing one address).
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../include/aaruformat.h"
#include "gtest/gtest.h"

namespace
{

constexpr uint8_t kSyncPattern[12] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

constexpr uint32_t kRawSectorSize = 2352;

std::vector<uint8_t> Pattern(const size_t length, const uint8_t seed)
{
    std::vector<uint8_t> data(length);
    for(size_t i = 0; i < length; i++) data[i] = static_cast<uint8_t>(seed * 31 + i * 7 + (i >> 8));
    return data;
}

void WriteCdPrefix(uint8_t *sector, const int64_t lba, const uint8_t mode)
{
    memcpy(sector, kSyncPattern, sizeof(kSyncPattern));

    const int64_t abs_frame = lba + 150;
    const auto    minute    = static_cast<uint8_t>(abs_frame / (60 * 75));
    const auto    second    = static_cast<uint8_t>(abs_frame / 75 % 60);
    const auto    frame     = static_cast<uint8_t>(abs_frame % 75);

    sector[12] = static_cast<uint8_t>(minute / 10 << 4 | minute % 10);
    sector[13] = static_cast<uint8_t>(second / 10 << 4 | second % 10);
    sector[14] = static_cast<uint8_t>(frame / 10 << 4 | frame % 10);
    sector[15] = mode;
}

std::vector<uint8_t> Mode1Sector(void *ecc, const int64_t lba, const uint8_t seed)
{
    std::vector<uint8_t> sector(kRawSectorSize, 0);
    WriteCdPrefix(sector.data(), lba, 0x01);
    const std::vector<uint8_t> user = Pattern(2048, seed);
    memcpy(sector.data() + 16, user.data(), 2048);
    aaruf_ecc_cd_reconstruct(ecc, sector.data(), kTrackTypeCdMode1);
    return sector;
}

std::vector<uint8_t> Mode2Form1Sector(void *ecc, const int64_t lba, const uint8_t seed, const uint8_t file)
{
    std::vector<uint8_t> sector(kRawSectorSize, 0);
    WriteCdPrefix(sector.data(), lba, 0x02);
    sector[16] = sector[20] = file;
    sector[17] = sector[21] = 0x01;
    sector[18] = sector[22]         = 0x08;  // Form 1 data
    const std::vector<uint8_t> user = Pattern(2048, seed);
    memcpy(sector.data() + 24, user.data(), 2048);
    aaruf_ecc_cd_reconstruct(ecc, sector.data(), kTrackTypeCdMode2Form1);
    return sector;
}

std::vector<uint8_t> Mode2Form2Sector(void *ecc, const int64_t lba, const uint8_t seed)
{
    std::vector<uint8_t> sector(kRawSectorSize, 0);
    WriteCdPrefix(sector.data(), lba, 0x02);
    sector[18] = sector[22]         = 0x20;  // Form 2
    const std::vector<uint8_t> user = Pattern(2324, seed);
    memcpy(sector.data() + 24, user.data(), 2324);
    aaruf_ecc_cd_reconstruct(ecc, sector.data(), kTrackTypeCdMode2Form2);
    return sector;
}

void *CreateCd(const char *filename, const size_t sectors, const uint8_t track_type, const char *options)
{
    void *ctx = aaruf_create(filename, CDROM, 2048, sectors, 0, 0, options, reinterpret_cast<const uint8_t *>("gtest"),
                             5, 0, 0, false);
    if(ctx == nullptr) return nullptr;

    TrackEntry track{};
    track.sequence = 1;
    track.type     = track_type;
    track.start    = 0;
    track.end      = static_cast<int64_t>(sectors) - 1;
    track.session  = 1;
    track.flags    = 0x04;

    if(aaruf_set_tracks(ctx, &track, 1) != AARUF_STATUS_OK)
    {
        aaruf_close(ctx);
        return nullptr;
    }

    return ctx;
}

std::vector<uint8_t> ReadTwin(void *ctx, const uint64_t sector, const bool negative, const uint32_t index,
                              uint8_t *status, int32_t *res)
{
    uint32_t length = 0;
    *res            = aaruf_read_sector_twin(ctx, sector, negative, index, nullptr, &length, status);
    if(*res != AARUF_ERROR_BUFFER_TOO_SMALL) return {};

    std::vector<uint8_t> data(length);
    *res = aaruf_read_sector_twin(ctx, sector, negative, index, data.data(), &length, status);
    data.resize(length);
    return data;
}

std::vector<uint8_t> ReadTwinLong(void *ctx, const uint64_t sector, const uint32_t index, uint8_t *status, int32_t *res)
{
    std::vector<uint8_t> data(4096);
    uint32_t             length = static_cast<uint32_t>(data.size());
    *res                        = aaruf_read_sector_twin_long(ctx, sector, false, index, data.data(), &length, status);
    data.resize(*res == AARUF_STATUS_OK ? length : 0);
    return data;
}

uint32_t TwinCount(void *ctx, const uint64_t sector, const bool negative = false)
{
    uint32_t count = 0xFFFFFFFF;
    EXPECT_EQ(aaruf_get_sector_twin_count(ctx, sector, negative, &count), AARUF_STATUS_OK);
    return count;
}

std::vector<TwinSectorInfo> TwinList(void *ctx)
{
    size_t  length = 0;
    int32_t res    = aaruf_get_twin_sectors(ctx, nullptr, &length);
    if(length == 0)
    {
        EXPECT_EQ(res, AARUF_STATUS_OK);
        return {};
    }
    EXPECT_EQ(res, AARUF_ERROR_BUFFER_TOO_SMALL);
    std::vector<TwinSectorInfo> list(length / sizeof(TwinSectorInfo));
    EXPECT_EQ(aaruf_get_twin_sectors(ctx, list.data(), &length), AARUF_STATUS_OK);
    return list;
}

}  // namespace

class TwinSectorsFixture : public testing::Test
{
};

/// Floppy-like media: twins with different lengths and statuses round-trip; twin #0 stays transparent.
TEST_F(TwinSectorsFixture, BlockMediaRoundTrip)
{
    const char *kFilename = "test_twin_block.aif";
    void       *ctx       = aaruf_create(kFilename, DOS_35_HD, 512, 20, 0, 0, "deduplicate=false;compress=false",
                                         reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);
    ASSERT_NE(ctx, nullptr);

    for(uint64_t i = 0; i < 10; i++)
    {
        auto data = Pattern(512, static_cast<uint8_t>(i));
        ASSERT_EQ(aaruf_write_sector(ctx, i, false, data.data(), SectorStatusDumped, 512), AARUF_STATUS_OK);
    }

    auto twin1 = Pattern(512, 0x51);
    auto twin2 = Pattern(256, 0x52);
    ASSERT_EQ(aaruf_write_sector_twin(ctx, 5, false, 1, twin1.data(), SectorStatusDumped, 512), AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_write_sector_twin(ctx, 5, false, 2, twin2.data(), SectorStatusErrored, 256), AARUF_STATUS_OK);

    // Gap is not allowed
    EXPECT_EQ(aaruf_write_sector_twin(ctx, 5, false, 4, twin1.data(), SectorStatusDumped, 512),
              AARUF_ERROR_TWIN_INDEX_OUT_OF_RANGE);
    EXPECT_EQ(aaruf_write_sector_twin(ctx, 6, false, 2, twin1.data(), SectorStatusDumped, 512),
              AARUF_ERROR_TWIN_INDEX_OUT_OF_RANGE);

    EXPECT_EQ(TwinCount(ctx, 5), 3u);
    EXPECT_EQ(TwinCount(ctx, 4), 1u);
    EXPECT_EQ(TwinCount(ctx, 15), 0u);

    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);

    EXPECT_EQ(aaruf_verify_image(ctx), AARUF_STATUS_OK);

    // Twin-unaware read: twin #0 with its real status
    {
        std::vector<uint8_t> data(512);
        uint32_t             length = 512;
        uint8_t              status = 0;
        ASSERT_EQ(aaruf_read_sector(ctx, 5, false, data.data(), &length, &status), AARUF_STATUS_OK);
        EXPECT_EQ(status, SectorStatusDumped);
        EXPECT_EQ(data, Pattern(512, 5));
    }

    EXPECT_EQ(TwinCount(ctx, 5), 3u);
    EXPECT_EQ(TwinCount(ctx, 4), 1u);
    EXPECT_EQ(TwinCount(ctx, 15), 0u);

    uint8_t status = 0;
    int32_t res    = 0;

    EXPECT_EQ(ReadTwin(ctx, 5, false, 0, &status, &res), Pattern(512, 5));
    EXPECT_EQ(res, AARUF_STATUS_OK);
    EXPECT_EQ(status, SectorStatusDumped);

    EXPECT_EQ(ReadTwin(ctx, 5, false, 1, &status, &res), twin1);
    EXPECT_EQ(res, AARUF_STATUS_OK);
    EXPECT_EQ(status, SectorStatusDumped);

    EXPECT_EQ(ReadTwin(ctx, 5, false, 2, &status, &res), twin2);
    EXPECT_EQ(res, AARUF_STATUS_OK);
    EXPECT_EQ(status, SectorStatusErrored);

    std::vector<uint8_t> buffer(512);
    uint32_t             length = 512;
    EXPECT_EQ(aaruf_read_sector_twin(ctx, 5, false, 3, buffer.data(), &length, &status),
              AARUF_ERROR_TWIN_INDEX_OUT_OF_RANGE);
    EXPECT_EQ(aaruf_read_sector_twin(ctx, 4, false, 1, buffer.data(), &length, &status),
              AARUF_ERROR_TWIN_INDEX_OUT_OF_RANGE);

    // Neighbours are unaffected
    EXPECT_EQ(ReadTwin(ctx, 6, false, 0, &status, &res), Pattern(512, 6));

    auto list = TwinList(ctx);
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].sector_address, 5u);
    EXPECT_EQ(list[0].count, 3u);
    EXPECT_EQ(list[0].negative, 0);

    // Writing to a read-only image fails
    EXPECT_EQ(aaruf_write_sector_twin(ctx, 5, false, 1, twin1.data(), SectorStatusDumped, 512), AARUF_READ_ONLY);

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(kFilename);
}

/// Replacing, appending, writing a twin before twin #0, re-writing twin #0 and clearing twins.
TEST_F(TwinSectorsFixture, ReplaceAppendClear)
{
    const char *kFilename = "test_twin_replace.aif";
    void       *ctx       = aaruf_create(kFilename, DOS_35_HD, 512, 16, 0, 0, "deduplicate=true;compress=true",
                                         reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);
    ASSERT_NE(ctx, nullptr);

    for(uint64_t i = 0; i < 16; i++)
    {
        if(i == 3) continue;
        auto data = Pattern(512, static_cast<uint8_t>(i));
        ASSERT_EQ(aaruf_write_sector(ctx, i, false, data.data(), SectorStatusDumped, 512), AARUF_STATUS_OK);
    }

    // Twin written before twin #0 exists
    auto early = Pattern(512, 0x33);
    ASSERT_EQ(aaruf_write_sector_twin(ctx, 3, false, 1, early.data(), SectorStatusDumped, 512), AARUF_STATUS_OK);
    EXPECT_EQ(TwinCount(ctx, 3), 2u);
    auto zero = Pattern(512, 3);
    ASSERT_EQ(aaruf_write_sector(ctx, 3, false, zero.data(), SectorStatusErrored, 512), AARUF_STATUS_OK);

    // Twin identical to another sector (deduplicated)
    auto dup = Pattern(512, 9);
    ASSERT_EQ(aaruf_write_sector_twin(ctx, 7, false, 1, dup.data(), SectorStatusDumped, 512), AARUF_STATUS_OK);
    auto other = Pattern(512, 0x77);
    ASSERT_EQ(aaruf_write_sector_twin(ctx, 7, false, 2, other.data(), SectorStatusDumped, 512), AARUF_STATUS_OK);
    // Replace twin #1
    auto replaced = Pattern(512, 0x71);
    ASSERT_EQ(aaruf_write_sector_twin(ctx, 7, false, 1, replaced.data(), SectorStatusErrored, 512), AARUF_STATUS_OK);
    // Re-write twin #0 keeps the twins
    auto zero7 = Pattern(512, 0x70);
    ASSERT_EQ(aaruf_write_sector(ctx, 7, false, zero7.data(), SectorStatusDumped, 512), AARUF_STATUS_OK);
    EXPECT_EQ(TwinCount(ctx, 7), 3u);

    // Twins added then cleared
    ASSERT_EQ(aaruf_write_sector_twin(ctx, 10, false, 1, other.data(), SectorStatusDumped, 512), AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_clear_sector_twins(ctx, 10, false), AARUF_STATUS_OK);
    EXPECT_EQ(TwinCount(ctx, 10), 1u);
    EXPECT_EQ(aaruf_clear_sector_twins(ctx, 11, false), AARUF_STATUS_OK);

    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(aaruf_verify_image(ctx), AARUF_STATUS_OK);

    uint8_t status = 0;
    int32_t res    = 0;

    EXPECT_EQ(ReadTwin(ctx, 3, false, 0, &status, &res), zero);
    EXPECT_EQ(status, SectorStatusErrored);
    EXPECT_EQ(ReadTwin(ctx, 3, false, 1, &status, &res), early);
    EXPECT_EQ(status, SectorStatusDumped);

    EXPECT_EQ(TwinCount(ctx, 7), 3u);
    EXPECT_EQ(ReadTwin(ctx, 7, false, 0, &status, &res), zero7);
    EXPECT_EQ(status, SectorStatusDumped);
    EXPECT_EQ(ReadTwin(ctx, 7, false, 1, &status, &res), replaced);
    EXPECT_EQ(status, SectorStatusErrored);
    EXPECT_EQ(ReadTwin(ctx, 7, false, 2, &status, &res), other);
    EXPECT_EQ(status, SectorStatusDumped);
    EXPECT_EQ(ReadTwin(ctx, 9, false, 0, &status, &res), dup);

    EXPECT_EQ(TwinCount(ctx, 10), 1u);
    EXPECT_EQ(ReadTwin(ctx, 10, false, 0, &status, &res), Pattern(512, 10));
    EXPECT_EQ(status, SectorStatusDumped);

    auto list = TwinList(ctx);
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].sector_address, 3u);
    EXPECT_EQ(list[1].sector_address, 7u);

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(kFilename);
}

/// Twins in a multi-level DDT (several secondary tables involved), on negative and overflow sectors.
/// Twins are written right after their twin #0, as a dumper finds them (rewriting an already flushed secondary
/// table is not supported by the multi-level DDT writer).
TEST_F(TwinSectorsFixture, MultiLevelNegativeOverflow)
{
    const char        *kFilename = "test_twin_multilevel.aif";
    constexpr uint64_t kSectors  = 2000;
    void *ctx = aaruf_create(kFilename, 1, 512, kSectors, 4, 4, "deduplicate=false;compress=true;table_shift=4",
                             reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);
    ASSERT_NE(ctx, nullptr);

    auto neg_twin = Pattern(512, 0xC2);
    auto ovf_twin = Pattern(512, 0xD1);
    auto more     = Pattern(512, 0xEE);

    for(uint64_t i = 4; i >= 1; i--)
    {
        auto data = Pattern(512, static_cast<uint8_t>(0xA0 + i));
        ASSERT_EQ(aaruf_write_sector(ctx, i, true, data.data(), SectorStatusDumped, 512), AARUF_STATUS_OK);
        if(i == 2)
            ASSERT_EQ(aaruf_write_sector_twin(ctx, 2, true, 1, neg_twin.data(), SectorStatusDumped, 512),
                      AARUF_STATUS_OK);
    }

    for(uint64_t i = 0; i < kSectors; i++)
    {
        auto data = Pattern(512, static_cast<uint8_t>(i));
        ASSERT_EQ(aaruf_write_sector(ctx, i, false, data.data(), SectorStatusDumped, 512), AARUF_STATUS_OK);

        if(i == 10 || i == 500 || i == 1999)
        {
            auto twin = Pattern(512, static_cast<uint8_t>(i + 1));
            ASSERT_EQ(aaruf_write_sector_twin(ctx, i, false, 1, twin.data(), SectorStatusDumped, 512), AARUF_STATUS_OK);
        }

        if(i == 500)
            ASSERT_EQ(aaruf_write_sector_twin(ctx, 500, false, 2, more.data(), SectorStatusErrored, 512),
                      AARUF_STATUS_OK);
    }

    for(uint64_t i = 0; i < 4; i++)
    {
        auto data = Pattern(512, static_cast<uint8_t>(0xB1 + i));
        ASSERT_EQ(aaruf_write_sector(ctx, kSectors + i, false, data.data(), SectorStatusDumped, 512), AARUF_STATUS_OK);
        if(i == 1)
            ASSERT_EQ(aaruf_write_sector_twin(ctx, kSectors + 1, false, 1, ovf_twin.data(), SectorStatusDumped, 512),
                      AARUF_STATUS_OK);
    }

    // Creating twins in a secondary table already flushed is refused instead of losing that table
    EXPECT_EQ(aaruf_write_sector_twin(ctx, 100, false, 1, more.data(), SectorStatusDumped, 512),
              AARUF_ERROR_CANNOT_SET_DDT_ENTRY);
    // ...but more twins for an existing group are fine, as the DDT is not touched
    auto extra = Pattern(512, 0xEF);
    EXPECT_EQ(aaruf_write_sector_twin(ctx, 10, false, 2, extra.data(), SectorStatusDumped, 512), AARUF_STATUS_OK);

    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(aaruf_verify_image(ctx), AARUF_STATUS_OK);

    uint8_t status = 0;
    int32_t res    = 0;

    EXPECT_EQ(TwinCount(ctx, 100), 1u);
    EXPECT_EQ(TwinCount(ctx, 10), 3u);
    EXPECT_EQ(ReadTwin(ctx, 10, false, 2, &status, &res), extra);

    for(uint64_t i = 0; i < kSectors; i++)
    {
        ASSERT_EQ(ReadTwin(ctx, i, false, 0, &status, &res), Pattern(512, static_cast<uint8_t>(i))) << i;
        ASSERT_EQ(status, SectorStatusDumped) << i;
    }

    for(uint64_t s : {10ULL, 500ULL, 1999ULL})
        EXPECT_EQ(ReadTwin(ctx, s, false, 1, &status, &res), Pattern(512, static_cast<uint8_t>(s + 1)));

    EXPECT_EQ(TwinCount(ctx, 500), 3u);
    EXPECT_EQ(ReadTwin(ctx, 500, false, 2, &status, &res), more);
    EXPECT_EQ(status, SectorStatusErrored);
    EXPECT_EQ(TwinCount(ctx, 1000), 1u);

    EXPECT_EQ(TwinCount(ctx, 2, true), 2u);
    EXPECT_EQ(ReadTwin(ctx, 2, true, 0, &status, &res), Pattern(512, 0xA2));
    EXPECT_EQ(ReadTwin(ctx, 2, true, 1, &status, &res), neg_twin);
    EXPECT_EQ(ReadTwin(ctx, 3, true, 0, &status, &res), Pattern(512, 0xA3));
    EXPECT_EQ(ReadTwin(ctx, kSectors + 1, false, 0, &status, &res), Pattern(512, 0xB2));
    EXPECT_EQ(ReadTwin(ctx, kSectors + 1, false, 1, &status, &res), ovf_twin);

    auto list = TwinList(ctx);
    ASSERT_EQ(list.size(), 5u);
    EXPECT_EQ(list[0].negative, 1);
    EXPECT_EQ(list[0].sector_address, 2u);
    EXPECT_EQ(list[4].sector_address, kSectors + 1);

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(kFilename);
}

/// Twins loaded on resume are kept, extended and new ones added.
TEST_F(TwinSectorsFixture, Resume)
{
    const char        *kFilename = "test_twin_resume.aif";
    constexpr uint64_t kSectors  = 200;
    void              *ctx       = aaruf_create(kFilename, 1, 512, kSectors, 0, 0, "deduplicate=true;compress=true",
                                                reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);
    ASSERT_NE(ctx, nullptr);

    for(uint64_t i = 0; i < kSectors; i++)
    {
        auto data = Pattern(512, static_cast<uint8_t>(i));
        ASSERT_EQ(aaruf_write_sector(ctx, i, false, data.data(), SectorStatusDumped, 512), AARUF_STATUS_OK);
    }

    auto first = Pattern(512, 0x11);
    ASSERT_EQ(aaruf_write_sector_twin(ctx, 50, false, 1, first.data(), SectorStatusDumped, 512), AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, true, "deduplicate=true;compress=true");
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(TwinCount(ctx, 50), 2u);
    auto more = Pattern(512, 0xEE);
    ASSERT_EQ(aaruf_write_sector_twin(ctx, 50, false, 2, more.data(), SectorStatusErrored, 512), AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_write_sector_twin(ctx, 100, false, 1, more.data(), SectorStatusDumped, 512), AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(aaruf_verify_image(ctx), AARUF_STATUS_OK);

    uint8_t status = 0;
    int32_t res    = 0;

    EXPECT_EQ(TwinCount(ctx, 50), 3u);
    EXPECT_EQ(ReadTwin(ctx, 50, false, 0, &status, &res), Pattern(512, 50));
    EXPECT_EQ(status, SectorStatusDumped);
    EXPECT_EQ(ReadTwin(ctx, 50, false, 1, &status, &res), first);
    EXPECT_EQ(ReadTwin(ctx, 50, false, 2, &status, &res), more);
    EXPECT_EQ(status, SectorStatusErrored);
    EXPECT_EQ(TwinCount(ctx, 100), 2u);
    EXPECT_EQ(ReadTwin(ctx, 100, false, 0, &status, &res), Pattern(512, 100));
    EXPECT_EQ(ReadTwin(ctx, 100, false, 1, &status, &res), more);

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(kFilename);
}

/// CD Mode 1: twins are decomposed like twin #0 and rebuilt bit-exactly, each with its own subchannel.
TEST_F(TwinSectorsFixture, CdMode1LongAndSubchannel)
{
    const char *kFilename = "test_twin_cd_mode1.aif";
    void       *ecc       = aaruf_ecc_cd_init();
    ASSERT_NE(ecc, nullptr);

    constexpr size_t kSectors = 8;
    void            *ctx      = CreateCd(kFilename, kSectors, kTrackTypeCdMode1, "deduplicate=false;compress=true");
    ASSERT_NE(ctx, nullptr);

    for(size_t i = 0; i < kSectors; i++)
    {
        auto raw = Mode1Sector(ecc, static_cast<int64_t>(i), static_cast<uint8_t>(i));
        ASSERT_EQ(aaruf_write_sector_long(ctx, i, false, raw.data(), SectorStatusDumped, kRawSectorSize),
                  AARUF_STATUS_OK);
        auto sub = Pattern(96, static_cast<uint8_t>(0x60 + i));
        ASSERT_EQ(aaruf_write_sector_tag(ctx, i, false, sub.data(), 96, kSectorTagCdSubchannel), AARUF_STATUS_OK);
    }

    // Twin #1: valid sector (prefix/suffix regenerable), different data and subchannel
    auto twin1     = Mode1Sector(ecc, 4, 0x91);
    auto twin1_sub = Pattern(96, 0x99);
    // Twin #2: same data as twin #0 but a corrupted header and suffix (stored in the arenas)
    auto twin2     = Mode1Sector(ecc, 4, 4);
    twin2[13] ^= 0x10;
    twin2[2100] ^= 0xFF;

    ASSERT_EQ(aaruf_write_sector_twin_long(ctx, 4, false, 1, twin1.data(), SectorStatusDumped, kRawSectorSize),
              AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_write_sector_twin_tag(ctx, 4, false, 1, twin1_sub.data(), 96, kSectorTagCdSubchannel),
              AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_write_sector_twin_long(ctx, 4, false, 2, twin2.data(), SectorStatusDumped, kRawSectorSize),
              AARUF_STATUS_OK);

    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(aaruf_verify_image(ctx), AARUF_STATUS_OK);

    uint8_t status = 0;
    int32_t res    = 0;

    EXPECT_EQ(ReadTwinLong(ctx, 4, 0, &status, &res), Mode1Sector(ecc, 4, 4));
    EXPECT_EQ(res, AARUF_STATUS_OK);
    EXPECT_EQ(ReadTwinLong(ctx, 4, 1, &status, &res), twin1);
    EXPECT_EQ(res, AARUF_STATUS_OK);
    EXPECT_EQ(status, SectorStatusMode1Correct);
    EXPECT_EQ(ReadTwinLong(ctx, 4, 2, &status, &res), twin2);
    EXPECT_EQ(res, AARUF_STATUS_OK);
    EXPECT_EQ(status, SectorStatusErrored);

    // Neighbours and plain reads unaffected
    EXPECT_EQ(ReadTwinLong(ctx, 3, 0, &status, &res), Mode1Sector(ecc, 3, 3));
    EXPECT_EQ(ReadTwinLong(ctx, 5, 0, &status, &res), Mode1Sector(ecc, 5, 5));

    // User data only
    auto data1 = ReadTwin(ctx, 4, false, 1, &status, &res);
    EXPECT_EQ(res, AARUF_STATUS_OK);
    EXPECT_EQ(data1, std::vector<uint8_t>(twin1.begin() + 16, twin1.begin() + 16 + 2048));

    // Per-twin subchannel
    uint8_t  sub[96];
    uint32_t length = sizeof(sub);
    ASSERT_EQ(aaruf_read_sector_twin_tag(ctx, 4, false, 0, sub, &length, kSectorTagCdSubchannel), AARUF_STATUS_OK);
    EXPECT_EQ(std::vector<uint8_t>(sub, sub + 96), Pattern(96, 0x64));
    length = sizeof(sub);
    ASSERT_EQ(aaruf_read_sector_twin_tag(ctx, 4, false, 1, sub, &length, kSectorTagCdSubchannel), AARUF_STATUS_OK);
    EXPECT_EQ(std::vector<uint8_t>(sub, sub + 96), twin1_sub);
    length = sizeof(sub);
    ASSERT_EQ(aaruf_read_sector_twin_tag(ctx, 4, false, 2, sub, &length, kSectorTagCdSubchannel), AARUF_STATUS_OK);
    EXPECT_EQ(std::vector<uint8_t>(sub, sub + 96), std::vector<uint8_t>(96, 0));
    length = sizeof(sub);
    ASSERT_EQ(aaruf_read_sector_tag(ctx, 5, false, sub, &length, kSectorTagCdSubchannel), AARUF_STATUS_OK);
    EXPECT_EQ(std::vector<uint8_t>(sub, sub + 96), Pattern(96, 0x65));

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    aaruf_ecc_cd_free(ecc);
    remove(kFilename);
}

/// CD Mode 2: per-twin subheader and suffix, Form 1 and Form 2, plus arena entries surviving a resume.
TEST_F(TwinSectorsFixture, CdMode2SubheaderSuffixResume)
{
    const char *kFilename = "test_twin_cd_mode2.aif";
    void       *ecc       = aaruf_ecc_cd_init();
    ASSERT_NE(ecc, nullptr);

    constexpr size_t kSectors = 8;
    void            *ctx = CreateCd(kFilename, kSectors, kTrackTypeCdMode2Formless, "deduplicate=false;compress=false");
    ASSERT_NE(ctx, nullptr);

    for(size_t i = 0; i < kSectors; i++)
    {
        auto raw = Mode2Form1Sector(ecc, static_cast<int64_t>(i), static_cast<uint8_t>(i), 1);
        ASSERT_EQ(aaruf_write_sector_long(ctx, i, false, raw.data(), SectorStatusDumped, kRawSectorSize),
                  AARUF_STATUS_OK);
    }

    auto form1_twin = Mode2Form1Sector(ecc, 2, 0x22, 7);  // different file number in the subheader
    auto form2_twin = Mode2Form2Sector(ecc, 2, 0x23);
    auto bad_twin   = Mode2Form1Sector(ecc, 2, 0x24, 1);
    bad_twin[2200] ^= 0x55;  // corrupted suffix -> arena
    bad_twin[14] ^= 0x01;    // corrupted header -> arena

    ASSERT_EQ(aaruf_write_sector_twin_long(ctx, 2, false, 1, form1_twin.data(), SectorStatusDumped, kRawSectorSize),
              AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_write_sector_twin_long(ctx, 2, false, 2, form2_twin.data(), SectorStatusDumped, kRawSectorSize),
              AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    // Resume: errored components go to the arenas after the existing ones
    ctx = aaruf_open(kFilename, true, "deduplicate=false;compress=false");
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(aaruf_write_sector_twin_long(ctx, 2, false, 3, bad_twin.data(), SectorStatusDumped, kRawSectorSize),
              AARUF_STATUS_OK);
    auto bad0 = Mode2Form1Sector(ecc, 6, 6, 1);
    bad0[2300] ^= 0xAA;
    ASSERT_EQ(aaruf_write_sector_long(ctx, 6, false, bad0.data(), SectorStatusDumped, kRawSectorSize), AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(aaruf_verify_image(ctx), AARUF_STATUS_OK);

    uint8_t status = 0;
    int32_t res    = 0;

    EXPECT_EQ(TwinCount(ctx, 2), 4u);
    EXPECT_EQ(ReadTwinLong(ctx, 2, 0, &status, &res), Mode2Form1Sector(ecc, 2, 2, 1));
    EXPECT_EQ(ReadTwinLong(ctx, 2, 1, &status, &res), form1_twin);
    EXPECT_EQ(ReadTwinLong(ctx, 2, 2, &status, &res), form2_twin);
    EXPECT_EQ(ReadTwinLong(ctx, 2, 3, &status, &res), bad_twin);
    EXPECT_EQ(res, AARUF_STATUS_OK);
    EXPECT_EQ(ReadTwinLong(ctx, 6, 0, &status, &res), bad0);
    EXPECT_EQ(ReadTwinLong(ctx, 3, 0, &status, &res), Mode2Form1Sector(ecc, 3, 3, 1));

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    aaruf_ecc_cd_free(ecc);
    remove(kFilename);
}

/// DVD long sectors: ID/IED/CPR_MAI/EDC are per twin.
TEST_F(TwinSectorsFixture, DvdLong)
{
    const char *kFilename = "test_twin_dvd.aif";
    void       *ctx       = aaruf_create(kFilename, DVDROM, 2048, 8, 0, 0, "deduplicate=false;compress=false",
                                         reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);
    ASSERT_NE(ctx, nullptr);

    for(uint64_t i = 0; i < 8; i++)
    {
        auto raw = Pattern(2064, static_cast<uint8_t>(i));
        ASSERT_EQ(aaruf_write_sector_long(ctx, i, false, raw.data(), SectorStatusDumped, 2064), AARUF_STATUS_OK);
    }

    auto twin = Pattern(2064, 0x42);
    ASSERT_EQ(aaruf_write_sector_twin_long(ctx, 1, false, 1, twin.data(), SectorStatusDumped, 2064), AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(aaruf_verify_image(ctx), AARUF_STATUS_OK);

    uint8_t status = 0;
    int32_t res    = 0;

    EXPECT_EQ(ReadTwinLong(ctx, 1, 0, &status, &res), Pattern(2064, 1));
    EXPECT_EQ(ReadTwinLong(ctx, 1, 1, &status, &res), twin);
    EXPECT_EQ(res, AARUF_STATUS_OK);
    EXPECT_EQ(ReadTwinLong(ctx, 2, 0, &status, &res), Pattern(2064, 2));

    uint8_t  ied[2];
    uint32_t length = sizeof(ied);
    ASSERT_EQ(aaruf_read_sector_twin_tag(ctx, 1, false, 1, ied, &length, kSectorTagDvdSectorIed), AARUF_STATUS_OK);
    EXPECT_EQ(ied[0], twin[4]);
    EXPECT_EQ(ied[1], twin[5]);

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(kFilename);
}

/// Apple Sony media: each twin carries its own 12-byte tag.
TEST_F(TwinSectorsFixture, AppleSonyTag)
{
    const char *kFilename = "test_twin_apple.aif";
    void       *ctx       = aaruf_create(kFilename, AppleSonySS, 512, 8, 0, 0, "deduplicate=false;compress=false",
                                         reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);
    ASSERT_NE(ctx, nullptr);

    for(uint64_t i = 0; i < 8; i++)
    {
        auto raw = Pattern(524, static_cast<uint8_t>(i));
        ASSERT_EQ(aaruf_write_sector_long(ctx, i, false, raw.data(), SectorStatusDumped, 524), AARUF_STATUS_OK);
    }

    auto twin = Pattern(524, 0x5A);
    ASSERT_EQ(aaruf_write_sector_twin_long(ctx, 3, false, 1, twin.data(), SectorStatusDumped, 524), AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(aaruf_verify_image(ctx), AARUF_STATUS_OK);

    uint8_t status = 0;
    int32_t res    = 0;

    EXPECT_EQ(ReadTwinLong(ctx, 3, 0, &status, &res), Pattern(524, 3));
    EXPECT_EQ(ReadTwinLong(ctx, 3, 1, &status, &res), twin);
    EXPECT_EQ(res, AARUF_STATUS_OK);
    EXPECT_EQ(ReadTwinLong(ctx, 4, 0, &status, &res), Pattern(524, 4));

    uint8_t  tag[12];
    uint32_t length = sizeof(tag);
    ASSERT_EQ(aaruf_read_sector_twin_tag(ctx, 3, false, 1, tag, &length, kSectorTagAppleSony), AARUF_STATUS_OK);
    EXPECT_EQ(std::vector<uint8_t>(tag, tag + 12), std::vector<uint8_t>(twin.begin() + 512, twin.end()));

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(kFilename);
}

/// An image without twins has no twin table, and a corrupted twin table does not prevent opening it.
TEST_F(TwinSectorsFixture, NoTwinsAndDamagedTable)
{
    const char *kFilename = "test_twin_damaged.aif";
    void       *ctx       = aaruf_create(kFilename, DOS_35_HD, 512, 8, 0, 0, "deduplicate=false;compress=false",
                                         reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);
    ASSERT_NE(ctx, nullptr);

    for(uint64_t i = 0; i < 8; i++)
    {
        auto data = Pattern(512, static_cast<uint8_t>(i));
        ASSERT_EQ(aaruf_write_sector(ctx, i, false, data.data(), SectorStatusDumped, 512), AARUF_STATUS_OK);
    }

    EXPECT_TRUE(TwinList(ctx).empty());
    auto twin = Pattern(512, 0xFE);
    ASSERT_EQ(aaruf_write_sector_twin(ctx, 1, false, 1, twin.data(), SectorStatusDumped, 512), AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    // Locate the TWTB and corrupt its payload
    FILE *file = fopen(kFilename, "r+b");
    ASSERT_NE(file, nullptr);
    std::vector<uint8_t> bytes;
    fseek(file, 0, SEEK_END);
    bytes.resize(static_cast<size_t>(ftell(file)));
    fseek(file, 0, SEEK_SET);
    ASSERT_EQ(fread(bytes.data(), 1, bytes.size(), file), bytes.size());
    // First occurrence is the block itself, the index entry that references it comes later
    size_t position = 0;
    for(size_t i = 0; i + 4 <= bytes.size() && position == 0; i++)
        if(memcmp(bytes.data() + i, "TWTB", 4) == 0) position = i;
    ASSERT_NE(position, 0u);
    fseek(file, static_cast<long>(position + sizeof(TwinSectorTableHeader) + 2), SEEK_SET);
    const uint8_t garbage = 0xA5;
    fwrite(&garbage, 1, 1, file);
    fclose(file);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);
    EXPECT_NE(aaruf_verify_image(ctx), AARUF_STATUS_OK);

    // Twin #0 still readable; twins gone
    uint8_t status = 0;
    int32_t res    = 0;
    EXPECT_EQ(ReadTwin(ctx, 1, false, 0, &status, &res), Pattern(512, 1));
    EXPECT_EQ(status, SectorStatusDumped);
    EXPECT_EQ(TwinCount(ctx, 1), 1u);

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(kFilename);
}

/// With erasure coding, a damaged twin sector table is recovered on open.
TEST_F(TwinSectorsFixture, ErasureCodingRecoversTable)
{
    const char *kFilename = "test_twin_ec.aif";
    void       *ctx       = aaruf_create(kFilename, DOS_35_HD, 512, 64, 0, 0, "deduplicate=false;compress=false",
                                         reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 1, false);
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(aaruf_set_erasure_coding(ctx, 1 /* RS-Vandermonde */, 4, 2), AARUF_STATUS_OK);

    for(uint64_t i = 0; i < 64; i++)
    {
        auto data = Pattern(512, static_cast<uint8_t>(i));
        ASSERT_EQ(aaruf_write_sector(ctx, i, false, data.data(), SectorStatusDumped, 512), AARUF_STATUS_OK);
    }

    auto twin = Pattern(512, 0xFE);
    ASSERT_EQ(aaruf_write_sector_twin(ctx, 20, false, 1, twin.data(), SectorStatusDumped, 512), AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    FILE *file = fopen(kFilename, "r+b");
    ASSERT_NE(file, nullptr);
    std::vector<uint8_t> bytes;
    fseek(file, 0, SEEK_END);
    bytes.resize(static_cast<size_t>(ftell(file)));
    fseek(file, 0, SEEK_SET);
    ASSERT_EQ(fread(bytes.data(), 1, bytes.size(), file), bytes.size());
    size_t position = 0;
    for(size_t i = 0; i + 4 <= bytes.size() && position == 0; i++)
        if(memcmp(bytes.data() + i, "TWTB", 4) == 0) position = i;
    ASSERT_NE(position, 0u);
    fseek(file, static_cast<long>(position + sizeof(TwinSectorTableHeader) + 2), SEEK_SET);
    const uint8_t garbage = static_cast<uint8_t>(bytes[position + sizeof(TwinSectorTableHeader) + 2] ^ 0xFF);
    fwrite(&garbage, 1, 1, file);
    fclose(file);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);

    uint8_t status = 0;
    int32_t res    = 0;
    EXPECT_EQ(TwinCount(ctx, 20), 2u);
    EXPECT_EQ(ReadTwin(ctx, 20, false, 1, &status, &res), twin);

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(kFilename);
}
