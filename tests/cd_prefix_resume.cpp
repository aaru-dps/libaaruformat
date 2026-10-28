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
 * @file cd_prefix_resume.cpp
 * @brief Regression tests for CD sector prefix/suffix storage across resume sessions and for the
 *        store-vs-regenerate classification of Mode 1 sectors.
 *
 * Covers the resume bug where a reopened image restarted the custom prefix/suffix arena at offset 0,
 * overwriting existing slots and leaving DDT indexes pointing past the rewritten block (garbage reads).
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

constexpr uint32_t kMediaTypeCdRom  = 15;
constexpr uint32_t kSectorSizeMode1 = 2048;
constexpr uint32_t kRawSectorSize   = 2352;

uint8_t ToBcd(const int value) { return static_cast<uint8_t>((value / 10) << 4 | (value % 10)); }

/// Writes a correct sync + BCD header for @p lba (Mode 1) into @p sector.
void WriteMode1Prefix(uint8_t *sector, const int64_t lba)
{
    memcpy(sector, kSyncPattern, sizeof(kSyncPattern));

    const int64_t abs_frame = lba + 150;
    sector[12]              = ToBcd(static_cast<int>(abs_frame / (60 * 75)));
    sector[13]              = ToBcd(static_cast<int>((abs_frame / 75) % 60));
    sector[14]              = ToBcd(static_cast<int>(abs_frame % 75));
    sector[15]              = 0x01;
}

/// Builds a Mode 1 sector with a valid ECC/EDC over @p fill user data (using the library reconstruction).
void BuildCorrectMode1Sector(void *ecc_ctx, uint8_t *sector, const int64_t lba, const uint8_t fill)
{
    memset(sector, 0, kRawSectorSize);
    WriteMode1Prefix(sector, lba);
    memset(sector + 16, fill, kSectorSizeMode1);
    aaruf_ecc_cd_reconstruct(ecc_ctx, sector, kTrackTypeCdMode1);
}

/// Builds a Mode 1 sector whose sync/header and ECC/EDC are all junk, so every part must be stored.
void BuildJunkMode1Sector(uint8_t *sector, const uint8_t fill)
{
    memset(sector, 0, kRawSectorSize);
    for(int i = 0; i < 16; ++i) sector[i] = static_cast<uint8_t>(fill + i);
    memset(sector + 16, fill, kSectorSizeMode1);
    for(int i = 2064; i < kRawSectorSize; ++i) sector[i] = static_cast<uint8_t>(fill ^ i);
}

void *CreateMode1Image(const char *filename, const size_t sectors)
{
    void *ctx = aaruf_create(filename, kMediaTypeCdRom, kSectorSizeMode1, sectors, 0, 0,
                             "deduplicate=false;compress=false", reinterpret_cast<const uint8_t *>("gtest"), 5, 0,
                             0, false);
    if(ctx == nullptr) return nullptr;

    TrackEntry track{};
    track.sequence = 1;
    track.type     = kTrackTypeCdMode1;
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

void ExpectSectorRoundTrips(void *ctx, const uint64_t lba, const uint8_t *expected)
{
    uint8_t buffer[kRawSectorSize];
    memset(buffer, 0xCC, sizeof(buffer));
    uint32_t length = sizeof(buffer);
    uint8_t  status = 0;

    ASSERT_EQ(aaruf_read_sector_long(ctx, lba, false, buffer, &length, &status), AARUF_STATUS_OK) << "sector " << lba;
    ASSERT_EQ(length, kRawSectorSize);
    EXPECT_EQ(memcmp(buffer, expected, kRawSectorSize), 0) << "sector " << lba << " does not round-trip";
}

}  // namespace

class CdPrefixResumeFixture : public testing::Test
{
};

/**
 * Writes junk sectors in a first session, closes, reopens in resume mode and writes more junk sectors.
 * Every sector written in either session must read back byte-exact afterwards.
 */
TEST_F(CdPrefixResumeFixture, CustomPrefixSuffixSurviveResume)
{
    constexpr size_t kSectors      = 12;
    constexpr size_t kFirstSession = 5;
    const char      *kFilename     = "test_cd_prefix_resume.aif";

    std::vector<uint8_t> expected(kSectors * kRawSectorSize);
    for(size_t i = 0; i < kSectors; ++i)
        BuildJunkMode1Sector(expected.data() + i * kRawSectorSize, static_cast<uint8_t>(0x10 + i));

    void *ctx = CreateMode1Image(kFilename, kSectors);
    ASSERT_NE(ctx, nullptr);

    for(size_t i = 0; i < kFirstSession; ++i)
        ASSERT_EQ(aaruf_write_sector_long(ctx, i, false, expected.data() + i * kRawSectorSize, SectorStatusDumped,
                                          kRawSectorSize),
                  AARUF_STATUS_OK)
            << "first session write " << i;

    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    // --- Resume: append more custom sectors ---
    ctx = aaruf_open(kFilename, true, "compress=false");
    ASSERT_NE(ctx, nullptr);

    for(size_t i = kFirstSession; i < kSectors; ++i)
        ASSERT_EQ(aaruf_write_sector_long(ctx, i, false, expected.data() + i * kRawSectorSize, SectorStatusDumped,
                                          kRawSectorSize),
                  AARUF_STATUS_OK)
            << "resumed session write " << i;

    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    // --- Verify every sector from both sessions ---
    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);

    for(size_t i = 0; i < kSectors; ++i) ExpectSectorRoundTrips(ctx, i, expected.data() + i * kRawSectorSize);

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(kFilename);
}

/**
 * A resumed session that adds no custom sectors must leave the stored arena intact.
 */
TEST_F(CdPrefixResumeFixture, ResumeWithoutNewCustomsKeepsArena)
{
    constexpr size_t kSectors  = 6;
    const char      *kFilename = "test_cd_prefix_resume_noop.aif";

    void *ecc_ctx = aaruf_ecc_cd_init();
    ASSERT_NE(ecc_ctx, nullptr);

    std::vector<uint8_t> expected(kSectors * kRawSectorSize);
    for(size_t i = 0; i < kSectors; ++i)
    {
        if(i < 3)
            BuildJunkMode1Sector(expected.data() + i * kRawSectorSize, static_cast<uint8_t>(0x40 + i));
        else
            BuildCorrectMode1Sector(ecc_ctx, expected.data() + i * kRawSectorSize, static_cast<int64_t>(i),
                                    static_cast<uint8_t>(0x40 + i));
    }

    void *ctx = CreateMode1Image(kFilename, kSectors);
    ASSERT_NE(ctx, nullptr);

    for(size_t i = 0; i < 3; ++i)
        ASSERT_EQ(aaruf_write_sector_long(ctx, i, false, expected.data() + i * kRawSectorSize, SectorStatusDumped,
                                          kRawSectorSize),
                  AARUF_STATUS_OK);

    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, true, "compress=false");
    ASSERT_NE(ctx, nullptr);

    for(size_t i = 3; i < kSectors; ++i)
        ASSERT_EQ(aaruf_write_sector_long(ctx, i, false, expected.data() + i * kRawSectorSize, SectorStatusDumped,
                                          kRawSectorSize),
                  AARUF_STATUS_OK);

    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);

    for(size_t i = 0; i < kSectors; ++i) ExpectSectorRoundTrips(ctx, i, expected.data() + i * kRawSectorSize);

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    aaruf_ecc_cd_free(ecc_ctx);
    remove(kFilename);
}

/**
 * Headers that are not valid BCD but decode to the expected LBA (e.g. 0x0A instead of 0x10) must be stored
 * verbatim, not regenerated. Also checks that a correct Mode 1 sector reports Mode1Correct while a sector
 * with a bad suffix reports Errored.
 */
TEST_F(CdPrefixResumeFixture, NonBcdHeaderIsStoredNotRegenerated)
{
    constexpr size_t kSectors  = 2;
    const char      *kFilename = "test_cd_prefix_nonbcd.aif";

    void *ecc_ctx = aaruf_ecc_cd_init();
    ASSERT_NE(ecc_ctx, nullptr);

    // Sector 0 is a correct sector. Sector 600 (00:10:00) gets its second byte written as 0x0A, which the
    // naive nibble decode reads as 10 just like the proper BCD 0x10.
    std::vector<uint8_t> expected(kSectors * kRawSectorSize);
    BuildCorrectMode1Sector(ecc_ctx, expected.data(), 0, 0x11);

    uint8_t *aliased = expected.data() + kRawSectorSize;
    BuildCorrectMode1Sector(ecc_ctx, aliased, 600, 0x22);
    ASSERT_EQ(aliased[13], 0x10);
    aliased[13] = 0x0A;  // non-BCD, decodes to 10 as well; ECC/EDC now stale -> stored too

    void *ctx = CreateMode1Image(kFilename, 601);
    ASSERT_NE(ctx, nullptr);

    ASSERT_EQ(aaruf_write_sector_long(ctx, 0, false, expected.data(), SectorStatusDumped, kRawSectorSize),
              AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_write_sector_long(ctx, 600, false, aliased, SectorStatusDumped, kRawSectorSize),
              AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);

    ExpectSectorRoundTrips(ctx, 0, expected.data());
    ExpectSectorRoundTrips(ctx, 600, aliased);

    uint8_t  buffer[kRawSectorSize];
    uint32_t length = sizeof(buffer);
    uint8_t  status = 0;
    ASSERT_EQ(aaruf_read_sector_long(ctx, 0, false, buffer, &length, &status), AARUF_STATUS_OK);
    EXPECT_EQ(status, SectorStatusMode1Correct);
    length = sizeof(buffer);
    ASSERT_EQ(aaruf_read_sector_long(ctx, 600, false, buffer, &length, &status), AARUF_STATUS_OK);
    EXPECT_EQ(status, SectorStatusErrored);

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    aaruf_ecc_cd_free(ecc_ctx);
    remove(kFilename);
}
