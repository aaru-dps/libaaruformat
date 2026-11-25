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
 * @file multilevel_ddt.cpp
 * @brief Regression tests for writing multi-level DDTs out of order and after resuming.
 *
 * Writing a sector whose secondary DDT table had already been written to the file used to replace that table with
 * an empty one, losing every sector it referenced; resumed multi-level images could not be reopened after writing.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <tuple>
#include <vector>

#include "../include/aaruformat.h"
#include "gtest/gtest.h"

namespace
{

constexpr uint64_t kSectors    = 3000;
constexpr uint32_t kSectorSize = 512;

void Fill(uint8_t *data, const uint64_t sector, const uint8_t generation)
{
    for(uint32_t i = 0; i < kSectorSize; i++)
        data[i] = static_cast<uint8_t>(sector * 13 + i * 3 + generation * 101 + (sector >> 8));
}

/// Deterministic shuffle (no dependency on the standard library random engines).
std::vector<uint64_t> Shuffled(const uint64_t count, uint64_t seed)
{
    std::vector<uint64_t> order(count);
    std::iota(order.begin(), order.end(), 0);
    for(uint64_t i = count - 1; i > 0; i--)
    {
        seed             = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        const uint64_t j = (seed >> 33) % (i + 1);
        std::swap(order[i], order[j]);
    }
    return order;
}

void ExpectContents(const char *filename, const std::vector<uint8_t> &generation)
{
    void *ctx = aaruf_open(filename, false, nullptr);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(aaruf_verify_image(ctx), AARUF_STATUS_OK);

    uint8_t expected[kSectorSize];
    uint8_t buffer[kSectorSize];

    for(uint64_t s = 0; s < kSectors; s++)
    {
        uint32_t length = kSectorSize;
        uint8_t  status = 0;
        ASSERT_EQ(aaruf_read_sector(ctx, s, false, buffer, &length, &status), AARUF_STATUS_OK) << "sector " << s;
        ASSERT_EQ(status, SectorStatusDumped) << "sector " << s;
        Fill(expected, s, generation[s]);
        ASSERT_EQ(memcmp(buffer, expected, kSectorSize), 0) << "sector " << s;
    }

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
}

}  // namespace

class MultiLevelDdtFixture : public testing::TestWithParam<const char *>
{
};

/// Every sector written in random order, so secondary tables are evicted and reloaded constantly.
TEST_P(MultiLevelDdtFixture, RandomOrderWrite)
{
    const char *kFilename = "test_multilevel_random.aif";
    void       *ctx       = aaruf_create(kFilename, 1, kSectorSize, kSectors, 0, 0, GetParam(),
                                         reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);
    ASSERT_NE(ctx, nullptr);

    std::vector<uint8_t> generation(kSectors, 0);
    uint8_t              data[kSectorSize];

    for(const uint64_t s : Shuffled(kSectors, 42))
    {
        Fill(data, s, 0);
        ASSERT_EQ(aaruf_write_sector(ctx, s, false, data, SectorStatusDumped, kSectorSize), AARUF_STATUS_OK) << s;
    }

    // Rewrite some sectors, going back and forth between tables
    for(const uint64_t s : {5ULL, 2990ULL, 17ULL, 1500ULL, 6ULL, 2999ULL, 0ULL})
    {
        generation[s] = 1;
        Fill(data, s, 1);
        ASSERT_EQ(aaruf_write_sector(ctx, s, false, data, SectorStatusDumped, kSectorSize), AARUF_STATUS_OK) << s;
    }

    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ExpectContents(kFilename, generation);
    remove(kFilename);
}

/// Sequential image, resumed twice with writes spread over many secondary tables.
TEST_P(MultiLevelDdtFixture, ResumeAndRewrite)
{
    const char *kFilename = "test_multilevel_resume.aif";
    void       *ctx       = aaruf_create(kFilename, 1, kSectorSize, kSectors, 0, 0, GetParam(),
                                         reinterpret_cast<const uint8_t *>("gtest"), 5, 0, 0, false);
    ASSERT_NE(ctx, nullptr);

    std::vector<uint8_t> generation(kSectors, 0);
    uint8_t              data[kSectorSize];

    // Leave the second half unwritten for the first resume
    for(uint64_t s = 0; s < kSectors / 2; s++)
    {
        Fill(data, s, 0);
        ASSERT_EQ(aaruf_write_sector(ctx, s, false, data, SectorStatusDumped, kSectorSize), AARUF_STATUS_OK) << s;
    }

    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    for(uint8_t pass = 1; pass <= 2; pass++)
    {
        ctx = aaruf_open(kFilename, true, GetParam());
        ASSERT_NE(ctx, nullptr) << "resume " << static_cast<int>(pass);

        if(pass == 1)
            for(uint64_t s = kSectors / 2; s < kSectors; s++)
            {
                Fill(data, s, 0);
                ASSERT_EQ(aaruf_write_sector(ctx, s, false, data, SectorStatusDumped, kSectorSize), AARUF_STATUS_OK)
                    << s;
            }

        const std::vector<uint64_t> order = Shuffled(kSectors, 1000 + pass);
        for(size_t i = 0; i < 300; i++)
        {
            const uint64_t s = order[i];
            generation[s]    = pass;
            Fill(data, s, pass);
            ASSERT_EQ(aaruf_write_sector(ctx, s, false, data, SectorStatusDumped, kSectorSize), AARUF_STATUS_OK) << s;
        }

        ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
        ExpectContents(kFilename, generation);
    }

    remove(kFilename);
}

INSTANTIATE_TEST_SUITE_P(Options, MultiLevelDdtFixture,
                         testing::Values("deduplicate=false;compress=false;table_shift=4",
                                         "deduplicate=true;compress=true;table_shift=4",
                                         "deduplicate=false;compress=true;zstd=true;table_shift=5"));
