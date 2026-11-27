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
 * @file flush_checkpoint.cpp
 * @brief Tests for using an image being written as a random access disk: reading sectors back before their data
 *        block is written, checkpoints with aaruf_flush(), and changing compression while writing.
 *
 * A copy of the file taken while the image is still open stands for the image left behind if the program stopped at
 * that moment: it must open as it was at the last checkpoint.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <numeric>
#include <string>
#include <vector>

#include "../include/aaruformat.h"
#include "gtest/gtest.h"

namespace
{

constexpr uint64_t kSectors    = 20000;
constexpr uint32_t kSectorSize = 512;

void Fill(uint8_t *data, const uint64_t sector, const uint32_t generation)
{
    for(uint32_t i = 0; i < kSectorSize; i++)
        data[i] = static_cast<uint8_t>(sector * 7 + i * 5 + generation * 31 + (sector >> 9) + (i >> 7) * generation);
}

/// Deterministic pseudo random numbers (no dependency on the standard library random engines).
uint64_t Next(uint64_t &seed)
{
    seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return seed >> 33;
}

void CopyFile(const char *from, const char *to)
{
    std::ifstream in(from, std::ios::binary);
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    out << in.rdbuf();
}

void *Create(const char *filename, const char *options)
{
    return aaruf_create(filename, 1, kSectorSize, kSectors, 0, 0, options, reinterpret_cast<const uint8_t *>("gtest"),
                        5, 0, 0, false);
}

/// Generation 0 means never written.
void ExpectSector(void *ctx, const uint64_t sector, const uint32_t generation)
{
    uint8_t  buffer[kSectorSize];
    uint8_t  expected[kSectorSize];
    uint32_t length = kSectorSize;
    uint8_t  status = 0;

    const int32_t res = aaruf_read_sector(ctx, sector, false, buffer, &length, &status);

    if(generation == 0)
    {
        ASSERT_EQ(res, AARUF_STATUS_SECTOR_NOT_DUMPED) << "sector " << sector;
        return;
    }

    ASSERT_EQ(res, AARUF_STATUS_OK) << "sector " << sector;
    ASSERT_EQ(status, SectorStatusDumped) << "sector " << sector;
    ASSERT_EQ(length, kSectorSize) << "sector " << sector;
    Fill(expected, sector, generation);
    ASSERT_EQ(memcmp(buffer, expected, kSectorSize), 0) << "sector " << sector << " generation " << generation;
}

void ExpectContents(const char *filename, const std::vector<uint32_t> &generation)
{
    void *ctx = aaruf_open(filename, false, nullptr);
    ASSERT_NE(ctx, nullptr) << filename;

    for(uint64_t s = 0; s < kSectors; s++)
    {
        ExpectSector(ctx, s, generation[s]);
        if(testing::Test::HasFatalFailure()) break;
    }

    EXPECT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
}

void Write(void *ctx, const uint64_t sector, const uint32_t generation, std::vector<uint32_t> &generations)
{
    uint8_t data[kSectorSize];
    Fill(data, sector, generation);
    ASSERT_EQ(aaruf_write_sector(ctx, sector, false, data, SectorStatusDumped, kSectorSize), AARUF_STATUS_OK)
        << "sector " << sector;
    generations[sector] = generation;
}

}  // namespace

class FlushCheckpointFixture : public testing::TestWithParam<const char *>
{
};

/// Sectors read back right after writing them, while their data block is still being filled, and after rewriting.
TEST_P(FlushCheckpointFixture, ReadBackOpenBlock)
{
    const char *kFilename = "test_flush_readback.aif";
    void       *ctx       = Create(kFilename, GetParam());
    ASSERT_NE(ctx, nullptr);

    std::vector<uint32_t> generation(kSectors, 0);

    for(uint64_t s = 100; s < 200; s++)
    {
        Write(ctx, s, 1, generation);
        ExpectSector(ctx, s, 1);
        ExpectSector(ctx, 100, 1);
    }

    // Rewriting sectors of the same open block and of a block already written
    Write(ctx, 150, 2, generation);
    ExpectSector(ctx, 150, 2);
    ExpectSector(ctx, 149, 1);
    ExpectSector(ctx, 300, 0);

    // Identical data written twice, which deduplication points to the first copy
    Write(ctx, 5000, 1, generation);
    Write(ctx, 5001, 1, generation);
    uint8_t same[kSectorSize];
    Fill(same, 5000, 1);
    ASSERT_EQ(aaruf_write_sector(ctx, 5002, false, same, SectorStatusDumped, kSectorSize), AARUF_STATUS_OK);
    uint8_t  buffer[kSectorSize];
    uint32_t length = kSectorSize;
    uint8_t  status = 0;
    ASSERT_EQ(aaruf_read_sector(ctx, 5002, false, buffer, &length, &status), AARUF_STATUS_OK);
    EXPECT_EQ(memcmp(buffer, same, kSectorSize), 0);
    ASSERT_EQ(aaruf_write_sector(ctx, 5002, false, same, SectorStatusNotDumped, kSectorSize), AARUF_STATUS_OK);

    // More than a block of writes, so the earlier blocks are written in the meantime
    for(uint64_t s = 10000; s < 15000; s++) Write(ctx, s, 3, generation);
    for(uint64_t s = 100; s < 200; s++) ExpectSector(ctx, s, generation[s]);
    for(uint64_t s = 10000; s < 15000; s += 7) ExpectSector(ctx, s, 3);

    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    generation[5002] = 0;
    ExpectContents(kFilename, generation);
    remove(kFilename);
}

/// An image created and closed without writing anything opens with every sector not dumped, also after resuming.
TEST_P(FlushCheckpointFixture, CreateCloseEmpty)
{
    const char *kFilename = "test_flush_empty.aif";
    void       *ctx       = Create(kFilename, GetParam());
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    std::vector<uint32_t> generation(kSectors, 0);
    ExpectContents(kFilename, generation);

    ctx = aaruf_open(kFilename, true, GetParam());
    ASSERT_NE(ctx, nullptr);
    Write(ctx, 42, 1, generation);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    ExpectContents(kFilename, generation);

    remove(kFilename);
}

/// Copies of the file taken after each checkpoint open with the sectors written up to that checkpoint, even with
/// more sectors written afterwards, and can be resumed.
TEST_P(FlushCheckpointFixture, CheckpointSurvivesStop)
{
    const char *kFilename = "test_flush_checkpoint.aif";
    const char *kCopy     = "test_flush_checkpoint_copy.aif";
    void       *ctx       = Create(kFilename, GetParam());
    ASSERT_NE(ctx, nullptr);

    // Flushing before anything is written gives a valid empty image
    ASSERT_EQ(aaruf_flush(ctx), AARUF_STATUS_OK);
    CopyFile(kFilename, kCopy);
    std::vector<uint32_t> generation(kSectors, 0);
    ExpectContents(kCopy, generation);

    uint64_t              seed = 7;
    std::vector<uint32_t> checkpoint;

    for(uint32_t round = 1; round <= 4; round++)
    {
        for(int i = 0; i < 3000; i++) Write(ctx, Next(seed) % kSectors, round, generation);
        ASSERT_EQ(aaruf_flush(ctx), AARUF_STATUS_OK);

        checkpoint = generation;

        // Sectors written after the checkpoint, never flushed
        for(int i = 0; i < 1500; i++) Write(ctx, Next(seed) % kSectors, round + 100, generation);

        CopyFile(kFilename, kCopy);
        ExpectContents(kCopy, checkpoint);
        if(HasFatalFailure()) return;
    }

    // The stopped image, resumed and written again
    void *copy = aaruf_open(kCopy, true, GetParam());
    ASSERT_NE(copy, nullptr);
    for(uint64_t s = 0; s < kSectors; s += 50) Write(copy, s, 200, checkpoint);
    ASSERT_EQ(aaruf_close(copy), AARUF_STATUS_OK);
    ExpectContents(kCopy, checkpoint);

    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    ExpectContents(kFilename, generation);

    remove(kFilename);
    remove(kCopy);
}

/// Random writes and rewrites, read back at once and checked after flushes, resumes and closing.
TEST_P(FlushCheckpointFixture, RandomRewrites)
{
    const char *kFilename = "test_flush_random.aif";
    void       *ctx       = Create(kFilename, GetParam());
    ASSERT_NE(ctx, nullptr);

    std::vector<uint32_t> generation(kSectors, 0);
    uint64_t              seed  = 1234;
    uint32_t              stamp = 1;

    for(int session = 0; session < 3; session++)
    {
        for(int i = 0; i < 6000; i++)
        {
            const uint64_t s = Next(seed) % kSectors;
            Write(ctx, s, stamp++, generation);
            ExpectSector(ctx, s, generation[s]);

            const uint64_t r = Next(seed) % kSectors;
            ExpectSector(ctx, r, generation[r]);
            if(HasFatalFailure()) return;

            if(i % 1000 == 999) ASSERT_EQ(aaruf_flush(ctx), AARUF_STATUS_OK);
        }

        ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
        ExpectContents(kFilename, generation);

        ctx = aaruf_open(kFilename, true, GetParam());
        ASSERT_NE(ctx, nullptr);
        for(uint64_t s = 0; s < kSectors; s += 97) ExpectSector(ctx, s, generation[s]);
    }

    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);
    ExpectContents(kFilename, generation);
    remove(kFilename);
}

/// Compression turned on while writing applies from the next block, and everything reads back.
TEST_P(FlushCheckpointFixture, SetCompressionWhileWriting)
{
    const char *kFilename = "test_flush_set_compression.aif";
    void       *ctx       = Create(kFilename, GetParam());
    ASSERT_NE(ctx, nullptr);

    std::vector<uint32_t> generation(kSectors, 0);

    for(uint64_t s = 0; s < kSectors / 2; s++) Write(ctx, s, 1, generation);
    ASSERT_EQ(aaruf_set_compression(ctx, true, true), AARUF_STATUS_OK);
    for(uint64_t s = kSectors / 2; s < kSectors; s++) Write(ctx, s, 1, generation);
    ASSERT_EQ(aaruf_flush(ctx), AARUF_STATUS_OK);
    ASSERT_EQ(aaruf_set_compression(ctx, true, false), AARUF_STATUS_OK);
    for(uint64_t s = 0; s < 3000; s++) Write(ctx, s, 2, generation);
    ASSERT_EQ(aaruf_set_compression(ctx, false, false), AARUF_STATUS_OK);
    for(uint64_t s = 3000; s < 6000; s++) Write(ctx, s, 2, generation);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ExpectContents(kFilename, generation);

    void *reader = aaruf_open(kFilename, false, nullptr);
    ASSERT_NE(reader, nullptr);
    EXPECT_EQ(aaruf_set_compression(reader, true, false), AARUF_READ_ONLY);
    EXPECT_EQ(aaruf_flush(reader), AARUF_READ_ONLY);
    EXPECT_EQ(aaruf_close(reader), AARUF_STATUS_OK);

    remove(kFilename);
}

INSTANTIATE_TEST_SUITE_P(Options, FlushCheckpointFixture,
                         testing::Values("deduplicate=true;compress=false;table_shift=0",
                                         "deduplicate=false;compress=false;table_shift=0",
                                         "deduplicate=true;compress=false;table_shift=12",
                                         "deduplicate=false;compress=false;table_shift=4",
                                         "deduplicate=true;compress=true;table_shift=12",
                                         "deduplicate=false;compress=true;zstd=true;table_shift=6"));

/// Resuming an image with zstd options writes the new blocks with zstd.
TEST(FlushCheckpoint, ResumeKeepsZstdOptions)
{
    const char *kFilename = "test_flush_resume_zstd.aif";
    const char *kOptions  = "compress=true;zstd=true;zstd_level=3;table_shift=8";
    void       *ctx       = Create(kFilename, kOptions);
    ASSERT_NE(ctx, nullptr);

    std::vector<uint32_t> generation(kSectors, 0);
    for(uint64_t s = 0; s < 1000; s++) Write(ctx, s, 1, generation);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ctx = aaruf_open(kFilename, true, kOptions);
    ASSERT_NE(ctx, nullptr);
    const aaruformat_context *actx = static_cast<const aaruformat_context *>(ctx);
    EXPECT_TRUE(actx->use_zstd);
    EXPECT_EQ(actx->zstd_level, 3);
    for(uint64_t s = 1000; s < 9000; s++) Write(ctx, s, 2, generation);
    ASSERT_EQ(aaruf_close(ctx), AARUF_STATUS_OK);

    ExpectContents(kFilename, generation);
    remove(kFilename);
}
