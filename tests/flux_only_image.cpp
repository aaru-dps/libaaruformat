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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "../include/aaruformat.h"
#include "gtest/gtest.h"

namespace
{
/// Fill a buffer with a deterministic, easy-to-validate pattern.
void fill_pattern(uint8_t *buf, size_t length, uint32_t seed)
{
    for(size_t i = 0; i < length; ++i) buf[i] = static_cast<uint8_t>((seed + i * 7u) & 0xFF);
}
}  // namespace

class FluxOnlyImageFixture : public testing::Test
{
public:
    FluxOnlyImageFixture() = default;

protected:
    void SetUp() override {}
    void TearDown() override {}
};

/// Create a flux-only image (all sector counts zero), write a flux capture, close, reopen, verify
/// and read back the flux capture. Sector reads must be rejected with AARUF_ERROR_USER_DATA_NOT_PRESENT.
TEST_F(FluxOnlyImageFixture, create_open_read_verify_flux_only)
{
    constexpr const char *kPath = "test_flux_only.aif";

    // Flux-only intent: user_sectors=0, negative=0, overflow=0, is_tape=false.
    void *context = aaruf_create(kPath, /*media_type=*/1, /*sector_size=*/512, /*user_sectors=*/0, /*negative=*/0,
                                 /*overflow=*/0, "compress=false",
                                 reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, /*is_tape=*/false);
    ASSERT_NE(context, nullptr) << "Failed to create flux-only image";

    constexpr uint32_t kDataLen  = 4096;
    constexpr uint32_t kIndexLen = 64;
    uint8_t            data[kDataLen];
    uint8_t            index[kIndexLen];
    fill_pattern(data, kDataLen, 0xA5u);
    fill_pattern(index, kIndexLen, 0x5Au);

    int32_t flux_write = aaruf_write_flux_capture(context,
                                                  /*head=*/0, /*track=*/0, /*subtrack=*/0,
                                                  /*capture_index=*/0,
                                                  /*data_resolution=*/25000000ULL,
                                                  /*index_resolution=*/25000000ULL, data, kDataLen, index, kIndexLen);
    ASSERT_EQ(flux_write, AARUF_STATUS_OK) << "Failed to write flux capture";

    // Sector writes against a flux-only image must be rejected, not silently accepted.
    uint8_t       sector[512];
    fill_pattern(sector, sizeof(sector), 0u);
    const int32_t sector_write =
        aaruf_write_sector(context, /*sector_address=*/0, /*negative=*/false, sector, SectorStatusDumped, 512);
    EXPECT_EQ(sector_write, AARUF_ERROR_USER_DATA_NOT_PRESENT)
        << "Sector write to flux-only image should be rejected";

    int32_t close_result = aaruf_close(context);
    ASSERT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close flux-only image";

    context = aaruf_open(kPath, /*resume=*/false, /*options=*/nullptr);
    ASSERT_NE(context, nullptr) << "Failed to reopen flux-only image";

    EXPECT_EQ(aaruf_verify_image(context), AARUF_STATUS_OK)
        << "Flux-only image should pass verification";

    uint32_t read_index_len = kIndexLen;
    uint32_t read_data_len  = kDataLen;
    uint8_t  read_index[kIndexLen];
    uint8_t  read_data[kDataLen];
    int32_t  flux_read = aaruf_read_flux_capture(context, /*head=*/0, /*track=*/0, /*subtrack=*/0,
                                                /*capture_index=*/0, read_index, &read_index_len, read_data,
                                                &read_data_len);
    ASSERT_EQ(flux_read, AARUF_STATUS_OK) << "Failed to read back flux capture";
    EXPECT_EQ(read_data_len, kDataLen) << "Unexpected flux data length";
    EXPECT_EQ(read_index_len, kIndexLen) << "Unexpected flux index length";
    EXPECT_EQ(memcmp(read_data, data, kDataLen), 0) << "Flux data round-trip mismatch";
    EXPECT_EQ(memcmp(read_index, index, kIndexLen), 0) << "Flux index round-trip mismatch";

    // Sector reads against a flux-only image must surface the dedicated error code.
    uint32_t sector_length = sizeof(sector);
    uint8_t  sector_status = 0;
    int32_t  sector_read =
        aaruf_read_sector(context, /*sector_address=*/0, /*negative=*/false, sector, &sector_length, &sector_status);
    EXPECT_EQ(sector_read, AARUF_ERROR_USER_DATA_NOT_PRESENT)
        << "Sector read on flux-only image must return AARUF_ERROR_USER_DATA_NOT_PRESENT";
    EXPECT_EQ(sector_status, SectorStatusNotDumped)
        << "Sector status should report not-dumped for flux-only images";

    sector_length = sizeof(sector);
    sector_status = 0;
    int32_t sector_long_read = aaruf_read_sector_long(context, /*sector_address=*/0, /*negative=*/false, sector,
                                                      &sector_length, &sector_status);
    EXPECT_EQ(sector_long_read, AARUF_ERROR_USER_DATA_NOT_PRESENT)
        << "Long sector read on flux-only image must return AARUF_ERROR_USER_DATA_NOT_PRESENT";

    uint32_t tag_length = sizeof(sector);
    int32_t  tag_read =
        aaruf_read_sector_tag(context, /*sector_address=*/0, /*negative=*/false, sector, &tag_length,
                              kSectorTagCdSubchannel);
    EXPECT_EQ(tag_read, AARUF_ERROR_USER_DATA_NOT_PRESENT)
        << "Sector tag read on flux-only image must return AARUF_ERROR_USER_DATA_NOT_PRESENT";

    close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close reopened flux-only image";

    unlink(kPath);
}

/// A standard image (with sectors) that has no flux data should still respond to flux reads with
/// AARUF_ERROR_FLUX_DATA_NOT_FOUND. Regression check for the "DDT-present, flux-absent" path.
TEST_F(FluxOnlyImageFixture, regular_image_flux_read_returns_not_found)
{
    constexpr const char *kPath         = "test_regular_no_flux.aif";
    constexpr size_t      kTotalSectors = 16;

    void *context = aaruf_create(kPath, /*media_type=*/1, /*sector_size=*/512, kTotalSectors, /*negative=*/0,
                                 /*overflow=*/0, "compress=false",
                                 reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, /*is_tape=*/false);
    ASSERT_NE(context, nullptr) << "Failed to create regular image";

    uint8_t sector[512];
    fill_pattern(sector, sizeof(sector), 0x42u);
    for(size_t i = 0; i < kTotalSectors; ++i)
    {
        const int32_t write_result = aaruf_write_sector(context, i, false, sector, SectorStatusDumped, 512);
        ASSERT_EQ(write_result, AARUF_STATUS_OK) << "Failed to write sector " << i;
    }

    int32_t close_result = aaruf_close(context);
    ASSERT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close regular image";

    context = aaruf_open(kPath, false, nullptr);
    ASSERT_NE(context, nullptr) << "Failed to reopen regular image";

    EXPECT_EQ(aaruf_verify_image(context), AARUF_STATUS_OK) << "Regular image should pass verification";

    uint8_t  read_data[16];
    uint8_t  read_index[16];
    uint32_t read_data_len  = sizeof(read_data);
    uint32_t read_index_len = sizeof(read_index);

    int32_t flux_read = aaruf_read_flux_capture(context, 0, 0, 0, 0, read_index, &read_index_len, read_data,
                                                &read_data_len);
    EXPECT_EQ(flux_read, AARUF_ERROR_FLUX_DATA_NOT_FOUND)
        << "Regular image without flux should return AARUF_ERROR_FLUX_DATA_NOT_FOUND";

    close_result = aaruf_close(context);
    EXPECT_EQ(close_result, AARUF_STATUS_OK) << "Failed to close reopened regular image";

    unlink(kPath);
}
