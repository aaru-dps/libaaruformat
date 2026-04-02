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
 * @file mode2_nocrc.cpp
 * @brief Regression tests for Mode 2 Form 2 NoCrc EDC fix.
 *
 * Covers fix/mode2-form2-nocrc-edc: when a Mode 2 Form 2 sector has zero
 * EDC on write (SectorStatusMode2Form2NoCrc), the read path must return
 * bytes 2348..2351 as zero via memset(data + 2348, 0, 4).
 *
 * Without the fix, those 4 bytes contain uninitialized data from the
 * read buffer, which silently corrupts the sector on roundtrip.
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

/// Media type for CD-ROM XA (Yellow Book), same as existing cdmode2 fixtures.
constexpr uint32_t kMediaTypeCdRomXa = 16;

/// CD sector size (user data portion for Mode 2).
constexpr uint32_t kSectorSizeMode2 = 2048;

/// Full raw CD sector size.
constexpr uint32_t kRawSectorSize = 2352;

/// EDC offset within a 2352-byte Mode 2 Form 2 sector.
constexpr int kEdcOffset = 2348;

/// User data offset within a Mode 2 sector (after sync + header + subheader).
constexpr int kUserDataOffset = 24;

/// User data length for Mode 2 Form 2.
constexpr int kUserDataLenForm2 = 2324;

/**
 * Build a valid Mode 2 Form 2 CD sector (2352 bytes) for the given LBA.
 *
 * Layout: Sync(12) + Header(4) + Subheader(8) + UserData(2324) + EDC(4)
 * The subheader has bit 5 set in both copies (Form 2).
 * User data is filled with @p fill byte. EDC is set to @p edc.
 */
void BuildMode2Form2Sector(uint8_t *sector, const int64_t lba, const uint8_t fill, const uint32_t edc)
{
    memset(sector, 0, kRawSectorSize);

    // Sync (12 bytes)
    memcpy(sector, kSyncPattern, sizeof(kSyncPattern));

    // Header: MSF in BCD + Mode 2
    const int64_t abs_frame = lba + 150;
    const auto    minute    = static_cast<uint8_t>(abs_frame / (60 * 75));
    const auto    second    = static_cast<uint8_t>((abs_frame / 75) % 60);
    const auto    frame     = static_cast<uint8_t>(abs_frame % 75);

    sector[12] = static_cast<uint8_t>((minute / 10) << 4 | (minute % 10));
    sector[13] = static_cast<uint8_t>((second / 10) << 4 | (second % 10));
    sector[14] = static_cast<uint8_t>((frame / 10) << 4 | (frame % 10));
    sector[15] = 0x02;  // Mode 2

    // Subheader copy 1: [file_number, channel, submode, coding_info]
    sector[18] = 0x20;  // submode bit 5 = Form 2
    // Subheader copy 2 (identical)
    sector[22] = 0x20;

    // User data (2324 bytes at offsets 24..2347)
    memset(sector + kUserDataOffset, fill, kUserDataLenForm2);

    // EDC (4 bytes at offsets 2348..2351)
    memcpy(sector + kEdcOffset, &edc, sizeof(edc));
}

}  // namespace

class Mode2NoCrcFixture : public testing::Test
{
};

/**
 * Core regression test: NoCrc sectors must read back with zero EDC bytes.
 *
 * Writes Mode 2 Form 2 sectors with EDC=0 (NoCrc condition), then reads
 * them back into a poisoned buffer and verifies bytes 2348..2351 are zero.
 */
TEST_F(Mode2NoCrcFixture, NoCrcEdcBytesAreZero)
{
    constexpr size_t kSectors  = 4;
    const char      *kFilename = "test_mode2_nocrc.aif";

    // --- Create image ---
    void *ctx = aaruf_create(kFilename, kMediaTypeCdRomXa, kSectorSizeMode2, kSectors, 0, 0,
                             "deduplicate=false;compress=false",
                             reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);
    ASSERT_NE(ctx, nullptr) << "Failed to create image";

    TrackEntry track{};
    track.sequence = 1;
    track.type     = kTrackTypeCdMode2Formless;
    track.start    = 0;
    track.end      = static_cast<int64_t>(kSectors) - 1;
    track.session  = 1;
    track.flags    = 0x04;  // Data track

    ASSERT_EQ(aaruf_set_tracks(ctx, &track, 1), AARUF_STATUS_OK) << "Failed to set tracks";

    // Write 4 sectors, each with zero EDC → triggers NoCrc status in DDT
    for(size_t i = 0; i < kSectors; ++i)
    {
        uint8_t sector[kRawSectorSize];
        BuildMode2Form2Sector(sector, static_cast<int64_t>(i), static_cast<uint8_t>(0x41 + i), /*edc=*/0);

        const int32_t rc = aaruf_write_sector_long(ctx, i, false, sector, SectorStatusDumped, kRawSectorSize);
        ASSERT_EQ(rc, AARUF_STATUS_OK) << "Failed to write sector " << i;
    }

    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK) << "Failed to close image after write";

    // --- Reopen & verify ---
    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr) << "Failed to reopen image";

    for(size_t i = 0; i < kSectors; ++i)
    {
        uint8_t buffer[kRawSectorSize];
        memset(buffer, 0xCC, sizeof(buffer));  // Poison buffer to expose uninitialized bytes

        uint32_t length = sizeof(buffer);
        uint8_t  status = 0;

        ASSERT_EQ(aaruf_read_sector_long(ctx, i, false, buffer, &length, &status), AARUF_STATUS_OK)
            << "Failed to read sector " << i;
        ASSERT_EQ(length, kRawSectorSize) << "Unexpected length for sector " << i;

        // THE FIX: bytes 2348..2351 must be zero, not uninitialized.
        // Without fix/mode2-form2-nocrc-edc these would be 0xCC (the poison byte).
        EXPECT_EQ(buffer[2348], 0x00) << "EDC byte 0 non-zero in NoCrc sector " << i;
        EXPECT_EQ(buffer[2349], 0x00) << "EDC byte 1 non-zero in NoCrc sector " << i;
        EXPECT_EQ(buffer[2350], 0x00) << "EDC byte 2 non-zero in NoCrc sector " << i;
        EXPECT_EQ(buffer[2351], 0x00) << "EDC byte 3 non-zero in NoCrc sector " << i;

        // Verify user data survived roundtrip
        const uint8_t fill = static_cast<uint8_t>(0x41 + i);
        for(int j = kUserDataOffset; j < kUserDataOffset + kUserDataLenForm2; ++j)
            EXPECT_EQ(buffer[j], fill) << "User data mismatch at offset " << j << " in sector " << i;

        // Verify subheader Form 2 bit preserved (at least one copy)
        EXPECT_TRUE((buffer[18] & 0x20) != 0 || (buffer[22] & 0x20) != 0)
            << "Form 2 subheader bit lost in sector " << i;
    }

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(kFilename);
}

/**
 * Sanity check: sectors with valid EDC must not get zeroed on roundtrip.
 *
 * Writes Mode 2 Form 2 sectors with a correctly computed EDC, reads them
 * back, and verifies the EDC is non-zero and the suffix is valid. This
 * confirms the NoCrc test above is meaningful — only zero-EDC sectors
 * receive the memset treatment.
 */
TEST_F(Mode2NoCrcFixture, ValidEdcSurvivesRoundtrip)
{
    constexpr size_t kSectors  = 2;
    const char      *kFilename = "test_mode2_valid_edc.aif";

    void *ctx = aaruf_create(kFilename, kMediaTypeCdRomXa, kSectorSizeMode2, kSectors, 0, 0,
                             "deduplicate=false;compress=false",
                             reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);
    ASSERT_NE(ctx, nullptr);

    TrackEntry track{};
    track.sequence = 1;
    track.type     = kTrackTypeCdMode2Formless;
    track.start    = 0;
    track.end      = static_cast<int64_t>(kSectors) - 1;
    track.session  = 1;
    track.flags    = 0x04;

    ASSERT_EQ(aaruf_set_tracks(ctx, &track, 1), AARUF_STATUS_OK);

    // Compute valid EDC using the library's own function
    void *ecc_ctx = aaruf_ecc_cd_init();
    ASSERT_NE(ecc_ctx, nullptr);

    for(size_t i = 0; i < kSectors; ++i)
    {
        uint8_t sector[kRawSectorSize];
        BuildMode2Form2Sector(sector, static_cast<int64_t>(i), static_cast<uint8_t>(0x55 + i), /*edc=*/0);

        // Compute valid EDC over subheader + user data (bytes 16..2347)
        const uint32_t edc = aaruf_edc_cd_compute(ecc_ctx, 0, sector, 0x91C, 0x10);
        memcpy(sector + kEdcOffset, &edc, sizeof(edc));

        ASSERT_EQ(aaruf_write_sector_long(ctx, i, false, sector, SectorStatusDumped, kRawSectorSize), AARUF_STATUS_OK)
            << "Failed to write sector " << i;
    }

    aaruf_ecc_cd_free(ecc_ctx);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    // --- Reopen & verify ---
    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);

    ecc_ctx = aaruf_ecc_cd_init();
    ASSERT_NE(ecc_ctx, nullptr);

    for(size_t i = 0; i < kSectors; ++i)
    {
        uint8_t buffer[kRawSectorSize];
        memset(buffer, 0xCC, sizeof(buffer));

        uint32_t length = sizeof(buffer);
        uint8_t  status = 0;

        ASSERT_EQ(aaruf_read_sector_long(ctx, i, false, buffer, &length, &status), AARUF_STATUS_OK)
            << "Failed to read sector " << i;
        ASSERT_EQ(length, kRawSectorSize);

        // EDC must be non-zero (valid, not NoCrc)
        uint32_t read_edc = 0;
        memcpy(&read_edc, buffer + kEdcOffset, sizeof(read_edc));
        EXPECT_NE(read_edc, 0U) << "EDC should be non-zero for valid sector " << i;

        // Recompute expected EDC from the read-back sector and compare.
        // (aaruf_ecc_cd_is_suffix_correct_mode2 also checks ECC P/Q which
        // Form 2 sectors don't have — the ECC area IS user data for Form 2.)
        const uint32_t expected_edc = aaruf_edc_cd_compute(ecc_ctx, 0, buffer, 0x91C, 0x10);
        EXPECT_EQ(read_edc, expected_edc) << "EDC mismatch for sector " << i;

        // Verify user data
        const uint8_t fill = static_cast<uint8_t>(0x55 + i);
        for(int j = kUserDataOffset; j < kUserDataOffset + kUserDataLenForm2; ++j)
            EXPECT_EQ(buffer[j], fill) << "User data mismatch at offset " << j << " in sector " << i;
    }

    aaruf_ecc_cd_free(ecc_ctx);
    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(kFilename);
}

/**
 * Mixed test: NoCrc and valid-EDC sectors in the same image.
 *
 * Verifies the DDT correctly tracks per-sector suffix status when both
 * NoCrc (SectorStatusMode2Form2NoCrc) and valid (SectorStatusMode2Form2Ok)
 * sectors coexist in a single track.
 */
TEST_F(Mode2NoCrcFixture, MixedNoCrcAndValidEdcPerSector)
{
    constexpr size_t kSectors  = 8;
    const char      *kFilename = "test_mode2_mixed.aif";

    void *ctx = aaruf_create(kFilename, kMediaTypeCdRomXa, kSectorSizeMode2, kSectors, 0, 0,
                             "deduplicate=false;compress=false",
                             reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);
    ASSERT_NE(ctx, nullptr);

    TrackEntry track{};
    track.sequence = 1;
    track.type     = kTrackTypeCdMode2Formless;
    track.start    = 0;
    track.end      = static_cast<int64_t>(kSectors) - 1;
    track.session  = 1;
    track.flags    = 0x04;

    ASSERT_EQ(aaruf_set_tracks(ctx, &track, 1), AARUF_STATUS_OK);

    void *ecc_ctx = aaruf_ecc_cd_init();
    ASSERT_NE(ecc_ctx, nullptr);

    // Even sectors: NoCrc (EDC=0). Odd sectors: valid EDC.
    for(size_t i = 0; i < kSectors; ++i)
    {
        uint8_t sector[kRawSectorSize];
        BuildMode2Form2Sector(sector, static_cast<int64_t>(i), static_cast<uint8_t>(0x30 + i), /*edc=*/0);

        if(i % 2 != 0)
        {
            // Odd sectors: compute and set valid EDC
            const uint32_t edc = aaruf_edc_cd_compute(ecc_ctx, 0, sector, 0x91C, 0x10);
            memcpy(sector + kEdcOffset, &edc, sizeof(edc));
        }
        // Even sectors: EDC remains 0 → NoCrc

        ASSERT_EQ(aaruf_write_sector_long(ctx, i, false, sector, SectorStatusDumped, kRawSectorSize), AARUF_STATUS_OK)
            << "write sector " << i;
    }

    aaruf_ecc_cd_free(ecc_ctx);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    // --- Reopen & verify ---
    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);

    ecc_ctx = aaruf_ecc_cd_init();
    ASSERT_NE(ecc_ctx, nullptr);

    for(size_t i = 0; i < kSectors; ++i)
    {
        uint8_t buffer[kRawSectorSize];
        memset(buffer, 0xCC, sizeof(buffer));

        uint32_t length = sizeof(buffer);
        uint8_t  status = 0;

        ASSERT_EQ(aaruf_read_sector_long(ctx, i, false, buffer, &length, &status), AARUF_STATUS_OK)
            << "read sector " << i;
        ASSERT_EQ(length, kRawSectorSize);

        uint32_t read_edc = 0;
        memcpy(&read_edc, buffer + kEdcOffset, sizeof(read_edc));

        if(i % 2 == 0)
        {
            // NoCrc sector: EDC must be zero
            EXPECT_EQ(read_edc, 0U) << "Even sector " << i << " should have zero EDC (NoCrc)";
        }
        else
        {
            // Valid EDC sector: EDC must be non-zero and match recomputation
            EXPECT_NE(read_edc, 0U) << "Odd sector " << i << " should have non-zero EDC";
            const uint32_t expected_edc = aaruf_edc_cd_compute(ecc_ctx, 0, buffer, 0x91C, 0x10);
            EXPECT_EQ(read_edc, expected_edc) << "EDC mismatch for odd sector " << i;
        }

        // User data must be correct regardless of EDC status
        const uint8_t fill = static_cast<uint8_t>(0x30 + i);
        for(int j = kUserDataOffset; j < kUserDataOffset + kUserDataLenForm2; ++j)
            EXPECT_EQ(buffer[j], fill) << "User data mismatch at offset " << j << " in sector " << i;
    }

    aaruf_ecc_cd_free(ecc_ctx);
    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(kFilename);
}
