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
 * @file dpm.cpp
 * @brief Tests for the Data Position Measurement block: storing it on new images, keeping, replacing and removing
 *        it on resumed images, validating it, and verifying its CRC.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../include/aaruformat.h"
#include "gtest/gtest.h"

namespace
{

constexpr uint32_t kMediaTypeCdRom  = 15;
constexpr uint32_t kSectorSizeMode1 = 2048;
constexpr size_t   kSectors         = 64;

/// Builds a serialized DPM block with @p entries entries, one layer end and one calibration, angles scaled by @p seed.
std::vector<uint8_t> BuildDpm(const uint32_t entries, const uint64_t seed)
{
    DpmHeader header{};
    header.identifier     = DataPositionMeasurementBlock;
    header.entries        = entries;
    header.calibrations   = 1;
    header.nominalSpacing = 50;
    header.timingUnit     = 1;
    header.speed          = 0xFFFF;
    header.layerEnds      = 1;
    header.flags          = DPM_FLAG_OPPOSITE_TRACK_PATH;

    std::vector<uint8_t> payload;

    const uint64_t layer_end = 1000;
    payload.insert(payload.end(), reinterpret_cast<const uint8_t *>(&layer_end),
                   reinterpret_cast<const uint8_t *>(&layer_end) + sizeof(layer_end));

    for(uint32_t i = 0; i < entries; i++)
    {
        DpmEntry entry{};
        entry.lba    = i * 50;
        entry.angle  = i * seed;
        entry.status = i == 0 ? DpmStatusUnknown : DpmStatusMeasured;
        payload.insert(payload.end(), reinterpret_cast<const uint8_t *>(&entry),
                       reinterpret_cast<const uint8_t *>(&entry) + sizeof(entry));
    }

    DpmCalibration calibration{};
    calibration.lba                 = 0;
    calibration.rotationPeriod      = 7500000;
    calibration.sectorsPerTurnX1000 = 9718000;
    payload.insert(payload.end(), reinterpret_cast<const uint8_t *>(&calibration),
                   reinterpret_cast<const uint8_t *>(&calibration) + sizeof(calibration));

    header.length = payload.size();
    header.crc64  = aaruf_crc64_data(payload.data(), static_cast<uint32_t>(payload.size()));

    std::vector<uint8_t> block(sizeof(DpmHeader));
    memcpy(block.data(), &header, sizeof(DpmHeader));
    block.insert(block.end(), payload.begin(), payload.end());

    return block;
}

/// Creates a Mode 1 CD image with all its sectors written, leaving it open for writing.
void *CreateImage(const char *filename)
{
    void *ctx = aaruf_create(filename, kMediaTypeCdRom, kSectorSizeMode1, kSectors, 0, 0,
                             "deduplicate=false;compress=false", reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0,
                             false);
    if(ctx == nullptr) return nullptr;

    TrackEntry track{};
    track.sequence = 1;
    track.type     = kTrackTypeCdMode1;
    track.start    = 0;
    track.end      = kSectors - 1;
    track.session  = 1;
    track.flags    = 0x04;

    if(aaruf_set_tracks(ctx, &track, 1) != AARUF_STATUS_OK)
    {
        aaruf_close(ctx);
        return nullptr;
    }

    uint8_t sector[kSectorSizeMode1];

    for(size_t i = 0; i < kSectors; i++)
    {
        memset(sector, static_cast<int>(i), sizeof(sector));

        if(aaruf_write_sector(ctx, i, false, sector, SectorStatusDumped, sizeof(sector)) != AARUF_STATUS_OK)
        {
            aaruf_close(ctx);
            return nullptr;
        }
    }

    return ctx;
}

/// Gets the DPM block of an image, empty if it has none.
std::vector<uint8_t> GetDpm(void *ctx)
{
    size_t length = 0;

    if(aaruf_get_dpm(ctx, nullptr, &length) != AARUF_ERROR_BUFFER_TOO_SMALL) return {};

    std::vector<uint8_t> block(length);

    if(aaruf_get_dpm(ctx, block.data(), &length) != AARUF_STATUS_OK) return {};

    return block;
}

/// Counts the DPM index entries of an opened image, and returns the offset of the last one.
size_t CountDpmIndexEntries(void *context, uint64_t *offset)
{
    const auto *ctx   = static_cast<aaruformat_context *>(context);
    size_t      count = 0;

    for(unsigned int i = 0; i < utarray_len(ctx->index_entries); i++)
    {
        const auto *entry = static_cast<IndexEntry *>(utarray_eltptr(ctx->index_entries, i));

        if(entry->blockType != DataPositionMeasurementBlock) continue;

        count++;
        if(offset != nullptr) *offset = entry->offset;
    }

    return count;
}

}  // namespace

class DpmFixture : public testing::Test
{
};

TEST_F(DpmFixture, RoundTripsOnNewImage)
{
    const char                *kFilename = "test_dpm_roundtrip.aif";
    const std::vector<uint8_t> written   = BuildDpm(100, 131700);

    void *ctx = CreateImage(kFilename);
    ASSERT_NE(ctx, nullptr);

    ASSERT_EQ(aaruf_set_dpm(ctx, written.data(), written.size()), AARUF_STATUS_OK);
    EXPECT_EQ(GetDpm(ctx), written) << "DPM must be readable while writing";
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(GetDpm(ctx), written);
    EXPECT_EQ(CountDpmIndexEntries(ctx, nullptr), 1U);
    EXPECT_EQ(aaruf_verify_image(ctx), AARUF_STATUS_OK);
    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    remove(kFilename);
}

TEST_F(DpmFixture, ImageWithoutDpmReportsNotPresent)
{
    const char *kFilename = "test_dpm_absent.aif";

    void *ctx = CreateImage(kFilename);
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);

    size_t length = 1234;
    EXPECT_EQ(aaruf_get_dpm(ctx, nullptr, &length), AARUF_ERROR_METADATA_NOT_PRESENT);
    EXPECT_EQ(length, 0U);
    EXPECT_EQ(CountDpmIndexEntries(ctx, nullptr), 0U);
    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    remove(kFilename);
}

TEST_F(DpmFixture, SizeQuery)
{
    const char                *kFilename = "test_dpm_size.aif";
    const std::vector<uint8_t> written   = BuildDpm(10, 131700);

    void *ctx = CreateImage(kFilename);
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(aaruf_set_dpm(ctx, written.data(), written.size()), AARUF_STATUS_OK);

    size_t length = 0;
    EXPECT_EQ(aaruf_get_dpm(ctx, nullptr, &length), AARUF_ERROR_BUFFER_TOO_SMALL);
    EXPECT_EQ(length, written.size());

    std::vector<uint8_t> small(written.size() - 1);
    length = small.size();
    EXPECT_EQ(aaruf_get_dpm(ctx, small.data(), &length), AARUF_ERROR_BUFFER_TOO_SMALL);
    EXPECT_EQ(length, written.size());

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(kFilename);
}

TEST_F(DpmFixture, SetRejectsInvalidBlocks)
{
    const char *kFilename = "test_dpm_invalid.aif";

    void *ctx = CreateImage(kFilename);
    ASSERT_NE(ctx, nullptr);

    std::vector<uint8_t> block = BuildDpm(10, 131700);
    block[sizeof(DpmHeader) + 20]++;
    EXPECT_EQ(aaruf_set_dpm(ctx, block.data(), block.size()), AARUF_ERROR_INVALID_BLOCK_CRC);

    block = BuildDpm(10, 131700);
    EXPECT_EQ(aaruf_set_dpm(ctx, block.data(), block.size() - 1), AARUF_ERROR_INCORRECT_DATA_SIZE);

    block = BuildDpm(10, 131700);
    reinterpret_cast<DpmHeader *>(block.data())->entries++;
    EXPECT_EQ(aaruf_set_dpm(ctx, block.data(), block.size()), AARUF_ERROR_INCORRECT_DATA_SIZE);

    block = BuildDpm(10, 131700);
    reinterpret_cast<DpmHeader *>(block.data())->identifier = DumpHardwareBlock;
    EXPECT_EQ(aaruf_set_dpm(ctx, block.data(), block.size()), AARUF_ERROR_CANNOT_READ_BLOCK);

    EXPECT_EQ(aaruf_set_dpm(ctx, block.data(), 4), AARUF_ERROR_INCORRECT_DATA_SIZE);

    size_t length = 0;
    EXPECT_EQ(aaruf_get_dpm(ctx, nullptr, &length), AARUF_ERROR_METADATA_NOT_PRESENT);

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    remove(kFilename);
}

TEST_F(DpmFixture, ReadOnlyImageRefusesChanges)
{
    const char                *kFilename = "test_dpm_readonly.aif";
    const std::vector<uint8_t> written   = BuildDpm(10, 131700);

    void *ctx = CreateImage(kFilename);
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(aaruf_set_dpm(ctx, written.data(), written.size()), AARUF_READ_ONLY);
    EXPECT_EQ(aaruf_clear_dpm(ctx), AARUF_READ_ONLY);
    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    remove(kFilename);
}

TEST_F(DpmFixture, ResumeKeepsDpm)
{
    const char                *kFilename = "test_dpm_resume_keep.aif";
    const std::vector<uint8_t> written   = BuildDpm(100, 131700);

    void *ctx = CreateImage(kFilename);
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(aaruf_set_dpm(ctx, written.data(), written.size()), AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, true, "compress=false");
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(GetDpm(ctx), written) << "DPM must be readable on resumed images";
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(GetDpm(ctx), written);
    EXPECT_EQ(CountDpmIndexEntries(ctx, nullptr), 1U);
    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    remove(kFilename);
}

TEST_F(DpmFixture, ResumeAddsDpm)
{
    const char                *kFilename = "test_dpm_resume_add.aif";
    const std::vector<uint8_t> written   = BuildDpm(100, 131700);

    void *ctx = CreateImage(kFilename);
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, true, "compress=false");
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(aaruf_set_dpm(ctx, written.data(), written.size()), AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(GetDpm(ctx), written);
    EXPECT_EQ(CountDpmIndexEntries(ctx, nullptr), 1U);
    EXPECT_EQ(aaruf_verify_image(ctx), AARUF_STATUS_OK);
    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    remove(kFilename);
}

TEST_F(DpmFixture, ResumeReplacesDpm)
{
    const char                *kFilename = "test_dpm_resume_replace.aif";
    const std::vector<uint8_t> first     = BuildDpm(100, 131700);
    const std::vector<uint8_t> second    = BuildDpm(150, 224800);

    void *ctx = CreateImage(kFilename);
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(aaruf_set_dpm(ctx, first.data(), first.size()), AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, true, "compress=false");
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(aaruf_set_dpm(ctx, second.data(), second.size()), AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(GetDpm(ctx), second);
    EXPECT_EQ(CountDpmIndexEntries(ctx, nullptr), 1U) << "the old DPM must not stay in the index";
    EXPECT_EQ(aaruf_verify_image(ctx), AARUF_STATUS_OK);
    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    remove(kFilename);
}

TEST_F(DpmFixture, ResumeClearsDpm)
{
    const char                *kFilename = "test_dpm_resume_clear.aif";
    const std::vector<uint8_t> written   = BuildDpm(100, 131700);

    void *ctx = CreateImage(kFilename);
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(aaruf_set_dpm(ctx, written.data(), written.size()), AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, true, "compress=false");
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(aaruf_clear_dpm(ctx), AARUF_STATUS_OK);

    size_t length = 0;
    EXPECT_EQ(aaruf_get_dpm(ctx, nullptr, &length), AARUF_ERROR_METADATA_NOT_PRESENT);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(aaruf_get_dpm(ctx, nullptr, &length), AARUF_ERROR_METADATA_NOT_PRESENT);
    EXPECT_EQ(CountDpmIndexEntries(ctx, nullptr), 0U);
    EXPECT_EQ(aaruf_verify_image(ctx), AARUF_STATUS_OK);
    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    remove(kFilename);
}

TEST_F(DpmFixture, VerifyDetectsCorruptedDpm)
{
    const char                *kFilename = "test_dpm_corrupt.aif";
    const std::vector<uint8_t> written   = BuildDpm(100, 131700);

    void *ctx = CreateImage(kFilename);
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(aaruf_set_dpm(ctx, written.data(), written.size()), AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    uint64_t offset = 0;
    ctx             = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(CountDpmIndexEntries(ctx, &offset), 1U);
    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    // Flip a byte inside the DPM payload
    FILE *file = fopen(kFilename, "r+b");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(fseek(file, static_cast<long>(offset + sizeof(DpmHeader) + 30), SEEK_SET), 0);
    const int byte = fgetc(file);
    ASSERT_NE(byte, EOF);
    ASSERT_EQ(fseek(file, static_cast<long>(offset + sizeof(DpmHeader) + 30), SEEK_SET), 0);
    fputc(byte ^ 0xFF, file);
    fclose(file);

    ctx = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(ctx, nullptr);

    size_t length = 0;
    EXPECT_EQ(aaruf_get_dpm(ctx, nullptr, &length), AARUF_ERROR_METADATA_NOT_PRESENT)
        << "a corrupted DPM must be ignored on open";
    EXPECT_EQ(aaruf_verify_image(ctx), AARUF_ERROR_INVALID_BLOCK_CRC);
    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    remove(kFilename);
}
