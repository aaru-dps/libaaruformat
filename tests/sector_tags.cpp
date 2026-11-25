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
