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
 * @file mode2_errored.cpp
 * @brief Regression tests for Mode 2 errored suffix restoration
 *        and disagreeing subheader form detection.
 *
 * Covers fix/mode2-errored-suffix-read: errored sectors now get
 * form-aware suffix restoration instead of bare data memcpy without suffix.
 *
 * Covers fix/mode2-form-detection-read: form detection uses
 * (data[0x12] & 0x20) || (data[0x16] & 0x20) — both subheader copies are
 * checked, so a disc with a corrupted first copy but valid second copy
 * still gets the correct Form 2 layout.
 */

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../include/aaruformat.h"
#include "gtest/gtest.h"

namespace
{

/// CD sync pattern: 0x00, 10x 0xFF, 0x00.
constexpr uint8_t kSyncPattern[12] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

constexpr uint32_t kMediaTypeCdRomXa = 16;
constexpr uint32_t kSectorSizeMode2  = 2048;
constexpr uint32_t kRawSectorSize    = 2352;

/// Write sync + header (MSF in BCD + Mode 2) into a 2352-byte sector buffer.
void WriteMode2Prefix(uint8_t *sector, const int64_t lba)
{
    memcpy(sector, kSyncPattern, sizeof(kSyncPattern));

    const int64_t abs_frame = lba + 150;
    const auto    minute    = static_cast<uint8_t>(abs_frame / (60 * 75));
    const auto    second    = static_cast<uint8_t>((abs_frame / 75) % 60);
    const auto    frame     = static_cast<uint8_t>(abs_frame % 75);

    sector[12] = static_cast<uint8_t>((minute / 10) << 4 | (minute % 10));
    sector[13] = static_cast<uint8_t>((second / 10) << 4 | (second % 10));
    sector[14] = static_cast<uint8_t>((frame / 10) << 4 | (frame % 10));
    sector[15] = 0x02;  // Mode 2
}

/**
 * Build a Mode 2 Form 2 sector with a specified (wrong) EDC.
 *
 * Layout: Sync(12) + Header(4) + Subheader(8) + UserData(2324) + EDC(4)
 */
void BuildMode2Form2ErroredSector(uint8_t *sector, const int64_t lba, const uint8_t fill, const uint32_t wrong_edc)
{
    memset(sector, 0, kRawSectorSize);
    WriteMode2Prefix(sector, lba);

    // Subheader: both copies Form 2 (bit 5 set)
    sector[18] = 0x20;
    sector[22] = 0x20;

    // User data (2324 bytes)
    memset(sector + 24, fill, 2324);

    // Wrong EDC (non-zero, triggers SectorStatusErrored)
    memcpy(sector + 2348, &wrong_edc, sizeof(wrong_edc));
}

/**
 * Build a Mode 2 Form 1 sector with a known-bad suffix.
 *
 * Layout: Sync(12) + Header(4) + Subheader(8) + UserData(2048) + EDC(4) + ECC_P(172) + ECC_Q(104)
 * The entire 280-byte suffix area (2072..2351) is filled with @p suffix_fill.
 */
void BuildMode2Form1ErroredSector(uint8_t *sector, const int64_t lba, const uint8_t data_fill,
                                  const uint8_t suffix_fill)
{
    memset(sector, 0, kRawSectorSize);
    WriteMode2Prefix(sector, lba);

    // Subheader: both copies Form 1 (bit 5 clear)
    sector[18] = 0x00;
    sector[22] = 0x00;

    // User data (2048 bytes)
    memset(sector + 24, data_fill, 2048);

    // Suffix: EDC(4) + ECC_P(172) + ECC_Q(104) = 280 bytes
    memset(sector + 2072, suffix_fill, 280);
}

/**
 * Build a Mode 2 sector with disagreeing subheader copies.
 *
 * Subheader copy 1 at byte 18: bit 5 CLEAR (Form 1)
 * Subheader copy 2 at byte 22: bit 5 SET (Form 2)
 *
 * The write path treats this as Form 2 via OR logic.
 * The read path (with fix) also treats it as Form 2.
 * Without the fix (checking only byte 0x12), this would be treated as Form 1.
 */
void BuildMode2DisagreeingSector(uint8_t *sector, const int64_t lba, const uint8_t fill, const uint32_t wrong_edc)
{
    memset(sector, 0, kRawSectorSize);
    WriteMode2Prefix(sector, lba);

    // Subheader copy 1: Form 1 (bit 5 clear)
    sector[18] = 0x00;
    // Subheader copy 2: Form 2 (bit 5 set)
    sector[22] = 0x20;

    // User data: 2324 bytes (Form 2 user data area)
    memset(sector + 24, fill, 2324);

    // Wrong EDC at Form 2 position (non-zero, triggers errored)
    memcpy(sector + 2348, &wrong_edc, sizeof(wrong_edc));
}

/// Helper: create a CD image with one Mode 2 Formless track of @p sectors sectors.
void *CreateMode2Image(const char *filename, const size_t sectors)
{
    void *ctx = aaruf_create(filename, kMediaTypeCdRomXa, kSectorSizeMode2, sectors, 0, 0,
                             "deduplicate=false;compress=false",
                             reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);
    if(ctx == nullptr) return nullptr;

    TrackEntry track{};
    track.sequence = 1;
    track.type     = kTrackTypeCdMode2Formless;
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

}  // namespace

class Mode2ErroredFixture : public testing::Test
{
};

// ---------------------------------------------------------------------------
//Errored Form 2 suffix restored correctly
// ---------------------------------------------------------------------------

/**
 * Writes Mode 2 Form 2 sectors with a wrong (non-zero) EDC, which triggers
 * SectorStatusErrored. On write the 4-byte EDC is stored in the suffix
 * buffer. On read, the errored path must restore those 4 bytes at offset
 * 2348 (Form 2 layout), not use Form 1 layout (offset 2072, 280 bytes).
 */
TEST_F(Mode2ErroredFixture, ErroredForm2SuffixRestored)
{
    constexpr size_t kSectors  = 4;
    const char      *kFilename = "test_mode2_errored_form2.aif";

    void *ctx = CreateMode2Image(kFilename, kSectors);
    ASSERT_NE(ctx, nullptr);

    constexpr uint32_t kWrongEdc = 0xDEADBEEF;

    for(size_t i = 0; i < kSectors; ++i)
    {
        uint8_t sector[kRawSectorSize];
        BuildMode2Form2ErroredSector(sector, static_cast<int64_t>(i), static_cast<uint8_t>(0xA0 + i), kWrongEdc);

        ASSERT_EQ(aaruf_write_sector_long(ctx, i, false, sector, SectorStatusDumped, kRawSectorSize), AARUF_STATUS_OK)
            << "write sector " << i;
    }

    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    // --- Reopen & verify ---
    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);

    for(size_t i = 0; i < kSectors; ++i)
    {
        uint8_t buffer[kRawSectorSize];
        memset(buffer, 0xCC, sizeof(buffer));

        uint32_t length = sizeof(buffer);
        uint8_t  status = 0;

        ASSERT_EQ(aaruf_read_sector_long(ctx, i, false, buffer, &length, &status), AARUF_STATUS_OK)
            << "read sector " << i;
        ASSERT_EQ(length, kRawSectorSize);

        // Verify the wrong EDC was preserved in the suffix (4 bytes at offset 2348)
        uint32_t read_edc = 0;
        memcpy(&read_edc, buffer + 2348, sizeof(read_edc));
        EXPECT_EQ(read_edc, kWrongEdc) << "EDC not restored for errored Form 2 sector " << i;

        // Verify user data intact (2324 bytes at offset 24 — Form 2 layout)
        const uint8_t fill = static_cast<uint8_t>(0xA0 + i);
        for(int j = 24; j < 24 + 2324; ++j)
            EXPECT_EQ(buffer[j], fill) << "User data mismatch at offset " << j << " in sector " << i;

        // Verify subheader Form 2 bit preserved
        EXPECT_NE(buffer[18] & 0x20, 0) << "Subheader copy 1 Form 2 bit lost in sector " << i;
        EXPECT_NE(buffer[22] & 0x20, 0) << "Subheader copy 2 Form 2 bit lost in sector " << i;
    }

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(kFilename);
}

// ---------------------------------------------------------------------------
//Errored Form 1 suffix restored correctly
// ---------------------------------------------------------------------------

/**
 * Writes Mode 2 Form 1 sectors with a fake suffix (280 bytes of known fill),
 * which triggers SectorStatusErrored. On read, the errored path must restore
 * the full 280-byte suffix at offset 2072 (Form 1 layout).
 */
TEST_F(Mode2ErroredFixture, ErroredForm1SuffixRestored)
{
    constexpr size_t kSectors  = 4;
    const char      *kFilename = "test_mode2_errored_form1.aif";

    void *ctx = CreateMode2Image(kFilename, kSectors);
    ASSERT_NE(ctx, nullptr);

    // Each sector: user data = 0xB0+i, suffix = 0xC0+i
    for(size_t i = 0; i < kSectors; ++i)
    {
        uint8_t sector[kRawSectorSize];
        BuildMode2Form1ErroredSector(sector, static_cast<int64_t>(i), static_cast<uint8_t>(0xB0 + i),
                                     static_cast<uint8_t>(0xC0 + i));

        ASSERT_EQ(aaruf_write_sector_long(ctx, i, false, sector, SectorStatusDumped, kRawSectorSize), AARUF_STATUS_OK)
            << "write sector " << i;
    }

    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    // --- Reopen & verify ---
    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);

    for(size_t i = 0; i < kSectors; ++i)
    {
        uint8_t buffer[kRawSectorSize];
        memset(buffer, 0xFF, sizeof(buffer));

        uint32_t length = sizeof(buffer);
        uint8_t  status = 0;

        ASSERT_EQ(aaruf_read_sector_long(ctx, i, false, buffer, &length, &status), AARUF_STATUS_OK)
            << "read sector " << i;
        ASSERT_EQ(length, kRawSectorSize);

        // Verify user data (2048 bytes at offset 24 — Form 1 layout)
        const uint8_t data_fill = static_cast<uint8_t>(0xB0 + i);
        for(int j = 24; j < 24 + 2048; ++j)
            EXPECT_EQ(buffer[j], data_fill) << "User data mismatch at offset " << j << " in sector " << i;

        // Verify the 280-byte suffix restored at offset 2072
        const uint8_t suffix_fill = static_cast<uint8_t>(0xC0 + i);
        for(int j = 2072; j < 2072 + 280; ++j)
            EXPECT_EQ(buffer[j], suffix_fill) << "Suffix mismatch at offset " << j << " in sector " << i;

        // Verify subheader Form 1 (bit 5 clear)
        EXPECT_EQ(buffer[18] & 0x20, 0) << "Subheader copy 1 should be Form 1 in sector " << i;
        EXPECT_EQ(buffer[22] & 0x20, 0) << "Subheader copy 2 should be Form 1 in sector " << i;
    }

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(kFilename);
}

// ---------------------------------------------------------------------------
//Mixed errored Form 1 and Form 2 in one image
// ---------------------------------------------------------------------------

/**
 * Verifies that errored Form 1 and Form 2 sectors can coexist in one track,
 * with the DDT suffix entries correctly distinguishing the form for each
 * sector. Even sectors are Form 2, odd sectors are Form 1.
 */
TEST_F(Mode2ErroredFixture, MixedErroredForm1AndForm2)
{
    constexpr size_t kSectors  = 8;
    const char      *kFilename = "test_mode2_errored_mixed.aif";

    void *ctx = CreateMode2Image(kFilename, kSectors);
    ASSERT_NE(ctx, nullptr);

    constexpr uint32_t kWrongEdc = 0xCAFEBABE;

    for(size_t i = 0; i < kSectors; ++i)
    {
        uint8_t sector[kRawSectorSize];

        if(i % 2 == 0)
        {
            // Even: Form 2 errored
            BuildMode2Form2ErroredSector(sector, static_cast<int64_t>(i), static_cast<uint8_t>(0x50 + i), kWrongEdc);
        }
        else
        {
            // Odd: Form 1 errored
            BuildMode2Form1ErroredSector(sector, static_cast<int64_t>(i), static_cast<uint8_t>(0x50 + i),
                                         static_cast<uint8_t>(0x60 + i));
        }

        ASSERT_EQ(aaruf_write_sector_long(ctx, i, false, sector, SectorStatusDumped, kRawSectorSize), AARUF_STATUS_OK)
            << "write sector " << i;
    }

    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);

    for(size_t i = 0; i < kSectors; ++i)
    {
        uint8_t buffer[kRawSectorSize];
        memset(buffer, 0xFF, sizeof(buffer));

        uint32_t length = sizeof(buffer);
        uint8_t  status = 0;

        ASSERT_EQ(aaruf_read_sector_long(ctx, i, false, buffer, &length, &status), AARUF_STATUS_OK)
            << "read sector " << i;

        if(i % 2 == 0)
        {
            // Even: Form 2 — 2324 bytes user data + 4 bytes EDC
            const uint8_t fill = static_cast<uint8_t>(0x50 + i);
            for(int j = 24; j < 24 + 2324; ++j)
                EXPECT_EQ(buffer[j], fill) << "Form 2 user data mismatch at offset " << j << " in sector " << i;

            uint32_t read_edc = 0;
            memcpy(&read_edc, buffer + 2348, sizeof(read_edc));
            EXPECT_EQ(read_edc, kWrongEdc) << "Form 2 EDC not restored for sector " << i;
        }
        else
        {
            // Odd: Form 1 — 2048 bytes user data + 280 bytes suffix
            const uint8_t data_fill   = static_cast<uint8_t>(0x50 + i);
            const uint8_t suffix_fill = static_cast<uint8_t>(0x60 + i);

            for(int j = 24; j < 24 + 2048; ++j)
                EXPECT_EQ(buffer[j], data_fill) << "Form 1 user data mismatch at offset " << j << " in sector " << i;
            for(int j = 2072; j < 2072 + 280; ++j)
                EXPECT_EQ(buffer[j], suffix_fill) << "Form 1 suffix mismatch at offset " << j << " in sector " << i;
        }
    }

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(kFilename);
}

// ---------------------------------------------------------------------------
//Disagreeing subheader copies — Form 2 must win via OR
// ---------------------------------------------------------------------------

/**
 * Writes sectors where subheader copy 1 says Form 1 (byte 18 bit 5 clear)
 * but subheader copy 2 says Form 2 (byte 22 bit 5 set). With a wrong EDC,
 * the sector enters the errored path.
 *
 * The fix ensures the read path uses:
 *   (data[0x12] & 0x20) || (data[0x16] & 0x20)
 *
 * Without the fix (only checking data[0x12]), the read path would use
 * Form 1 layout (2048 user + 280 suffix) instead of Form 2 (2324 user +
 * 4 suffix), corrupting bytes 2072..2347 of user data.
 */
TEST_F(Mode2ErroredFixture, DisagreeingSubheaderUsesForm2)
{
    constexpr size_t kSectors  = 4;
    const char      *kFilename = "test_mode2_disagreeing.aif";

    void *ctx = CreateMode2Image(kFilename, kSectors);
    ASSERT_NE(ctx, nullptr);

    constexpr uint32_t kWrongEdc = 0xBAADF00D;

    for(size_t i = 0; i < kSectors; ++i)
    {
        uint8_t sector[kRawSectorSize];
        BuildMode2DisagreeingSector(sector, static_cast<int64_t>(i), static_cast<uint8_t>(0xD0 + i), kWrongEdc);

        ASSERT_EQ(aaruf_write_sector_long(ctx, i, false, sector, SectorStatusDumped, kRawSectorSize), AARUF_STATUS_OK)
            << "write sector " << i;
    }

    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    // --- Reopen & verify ---
    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);

    for(size_t i = 0; i < kSectors; ++i)
    {
        uint8_t buffer[kRawSectorSize];
        memset(buffer, 0xFF, sizeof(buffer));

        uint32_t length = sizeof(buffer);
        uint8_t  status = 0;

        ASSERT_EQ(aaruf_read_sector_long(ctx, i, false, buffer, &length, &status), AARUF_STATUS_OK)
            << "read sector " << i;
        ASSERT_EQ(length, kRawSectorSize);

        // Verify subheader preserved: byte 18 Form 1, byte 22 Form 2
        EXPECT_EQ(buffer[18] & 0x20, 0) << "Subheader copy 1 should still say Form 1 in sector " << i;
        EXPECT_NE(buffer[22] & 0x20, 0) << "Subheader copy 2 should still say Form 2 in sector " << i;

        // THE FIX: Form 2 layout must be used.
        // User data: ALL 2324 bytes (24..2347) must match the fill pattern.
        // If the old bug existed (Form 1 layout), bytes 2072..2347 would be
        // overwritten with suffix data instead of user data.
        const uint8_t fill = static_cast<uint8_t>(0xD0 + i);
        for(int j = 24; j < 24 + 2324; ++j)
            EXPECT_EQ(buffer[j], fill) << "User data mismatch at offset " << j << " in sector " << i
                                       << " — if this fails at offset >= 2072, the old Form 1 bug is present";

        // EDC suffix: 4 bytes at offset 2348 must match the original wrong EDC
        uint32_t read_edc = 0;
        memcpy(&read_edc, buffer + 2348, sizeof(read_edc));
        EXPECT_EQ(read_edc, kWrongEdc) << "EDC not restored for sector " << i;
    }

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(kFilename);
}
