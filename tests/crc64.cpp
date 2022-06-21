/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2022 Natalia Portillo.
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
#include <cstring>

#include "../include/aaruformat.h"
#include "gtest/gtest.h"

#define EXPECTED_CRC64 0xbf09992cc5ede38e
#define EXPECTED_CRC64_15BYTES 0x797F3766FD93975B
#define EXPECTED_CRC64_31BYTES 0xCD9201905A7937FD
#define EXPECTED_CRC64_63BYTES 0x29F331FC90702BF4
#define EXPECTED_CRC64_2352BYTES 0x126435DB43477623

static const uint8_t* buffer;
static const uint8_t* buffer_misaligned;

class crc64Fixture : public ::testing::Test
{
  public:
    crc64Fixture()
    {
        // initialization;
        // can also be done in SetUp()
    }

  protected:
    void SetUp()
    {
        char path[PATH_MAX];
        char filename[PATH_MAX];

        getcwd(path, PATH_MAX);
        snprintf(filename, PATH_MAX, "%s/data/random", path);

        FILE* file = fopen(filename, "rb");
        buffer     = (const uint8_t*)malloc(1048576);
        fread((void*)buffer, 1, 1048576, file);
        fclose(file);

        buffer_misaligned = (const uint8_t*)malloc(1048577);
        memcpy((void*)(buffer_misaligned + 1), buffer, 1048576);
    }

    void TearDown()
    {
        free((void*)buffer);
        free((void*)buffer_misaligned);
    }

    ~crc64Fixture()
    {
        // resources cleanup, no exceptions allowed
    }

    // shared user data
};

TEST_F(crc64Fixture, crc64_auto)
{
    crc64_ctx* ctx = aaruf_crc64_init();
    uint64_t   crc;

    EXPECT_NE(ctx, nullptr);

    aaruf_crc64_update(ctx, buffer, 1048576);
    aaruf_crc64_final(ctx, &crc);
    aaruf_crc64_free(ctx);

    EXPECT_EQ(crc, EXPECTED_CRC64);
}

TEST_F(crc64Fixture, crc64_slicing)
{
    uint64_t crc = CRC64_ECMA_SEED;

    aaruf_crc64_slicing(&crc, buffer, 1048576);

    crc ^= CRC64_ECMA_SEED;

    EXPECT_EQ(crc, EXPECTED_CRC64);
}

TEST_F(crc64Fixture, crc64_auto_misaligned)
{
    crc64_ctx* ctx = aaruf_crc64_init();
    uint64_t   crc;

    EXPECT_NE(ctx, nullptr);

    aaruf_crc64_update(ctx, buffer_misaligned + 1, 1048576);
    aaruf_crc64_final(ctx, &crc);
    aaruf_crc64_free(ctx);

    EXPECT_EQ(crc, EXPECTED_CRC64);
}

TEST_F(crc64Fixture, crc64_slicing_misaligned)
{
    uint64_t crc = CRC64_ECMA_SEED;

    aaruf_crc64_slicing(&crc, buffer_misaligned + 1, 1048576);

    crc ^= CRC64_ECMA_SEED;

    EXPECT_EQ(crc, EXPECTED_CRC64);
}

TEST_F(crc64Fixture, crc64_auto_15bytes)
{
    crc64_ctx* ctx = aaruf_crc64_init();
    uint64_t   crc;

    EXPECT_NE(ctx, nullptr);

    aaruf_crc64_update(ctx, buffer, 15);
    aaruf_crc64_final(ctx, &crc);
    aaruf_crc64_free(ctx);

    EXPECT_EQ(crc, EXPECTED_CRC64_15BYTES);
}

TEST_F(crc64Fixture, crc64_slicing_15bytes)
{
    uint64_t crc = CRC64_ECMA_SEED;

    aaruf_crc64_slicing(&crc, buffer, 15);

    crc ^= CRC64_ECMA_SEED;

    EXPECT_EQ(crc, EXPECTED_CRC64_15BYTES);
}

TEST_F(crc64Fixture, crc64_auto_31bytes)
{
    crc64_ctx* ctx = aaruf_crc64_init();
    uint64_t   crc;

    EXPECT_NE(ctx, nullptr);

    aaruf_crc64_update(ctx, buffer, 31);
    aaruf_crc64_final(ctx, &crc);
    aaruf_crc64_free(ctx);

    EXPECT_EQ(crc, EXPECTED_CRC64_31BYTES);
}

TEST_F(crc64Fixture, crc64_slicing_31bytes)
{
    uint64_t crc = CRC64_ECMA_SEED;

    aaruf_crc64_slicing(&crc, buffer, 31);

    crc ^= CRC64_ECMA_SEED;

    EXPECT_EQ(crc, EXPECTED_CRC64_31BYTES);
}

TEST_F(crc64Fixture, crc64_auto_63bytes)
{
    crc64_ctx* ctx = aaruf_crc64_init();
    uint64_t   crc;

    EXPECT_NE(ctx, nullptr);

    aaruf_crc64_update(ctx, buffer, 63);
    aaruf_crc64_final(ctx, &crc);
    aaruf_crc64_free(ctx);

    EXPECT_EQ(crc, EXPECTED_CRC64_63BYTES);
}

TEST_F(crc64Fixture, crc64_slicing_63bytes)
{
    uint64_t crc = CRC64_ECMA_SEED;

    aaruf_crc64_slicing(&crc, buffer, 63);

    crc ^= CRC64_ECMA_SEED;

    EXPECT_EQ(crc, EXPECTED_CRC64_63BYTES);
}

TEST_F(crc64Fixture, crc64_auto_2352bytes)
{
    crc64_ctx* ctx = aaruf_crc64_init();
    uint64_t   crc;

    EXPECT_NE(ctx, nullptr);

    aaruf_crc64_update(ctx, buffer, 2352);
    aaruf_crc64_final(ctx, &crc);
    aaruf_crc64_free(ctx);

    EXPECT_EQ(crc, EXPECTED_CRC64_2352BYTES);
}

TEST_F(crc64Fixture, crc64_slicing_2352bytes)
{
    uint64_t crc = CRC64_ECMA_SEED;

    aaruf_crc64_slicing(&crc, buffer, 2352);

    crc ^= CRC64_ECMA_SEED;

    EXPECT_EQ(crc, EXPECTED_CRC64_2352BYTES);
}

#if defined(__x86_64__) || defined(__amd64) || defined(_M_AMD64) || defined(_M_X64) || defined(__I386__) ||            \
    defined(__i386__) || defined(__THW_INTEL) || defined(_M_IX86)
TEST_F(crc64Fixture, crc64_clmul)
{
    if(!have_clmul()) return;

    uint64_t crc = CRC64_ECMA_SEED;

    crc = ~aaruf_crc64_clmul(~crc, buffer, 1048576);

    crc ^= CRC64_ECMA_SEED;

    EXPECT_EQ(crc, EXPECTED_CRC64);
}

TEST_F(crc64Fixture, crc64_clmul_misaligned)
{
    if(!have_clmul()) return;

    uint64_t crc = CRC64_ECMA_SEED;

    crc = ~aaruf_crc64_clmul(~crc, buffer_misaligned + 1, 1048576);

    crc ^= CRC64_ECMA_SEED;

    EXPECT_EQ(crc, EXPECTED_CRC64);
}

TEST_F(crc64Fixture, crc64_clmul_15bytes)
{
    if(!have_clmul()) return;

    uint64_t crc = CRC64_ECMA_SEED;

    crc = ~aaruf_crc64_clmul(~crc, buffer, 15);

    crc ^= CRC64_ECMA_SEED;

    EXPECT_EQ(crc, EXPECTED_CRC64_15BYTES);
}

TEST_F(crc64Fixture, crc64_clmul_31bytes)
{
    if(!have_clmul()) return;

    uint64_t crc = CRC64_ECMA_SEED;

    crc = ~aaruf_crc64_clmul(~crc, buffer, 31);

    crc ^= CRC64_ECMA_SEED;

    EXPECT_EQ(crc, EXPECTED_CRC64_31BYTES);
}

TEST_F(crc64Fixture, crc64_clmul_63bytes)
{
    if(!have_clmul()) return;

    uint64_t crc = CRC64_ECMA_SEED;

    crc = ~aaruf_crc64_clmul(~crc, buffer, 63);

    crc ^= CRC64_ECMA_SEED;

    EXPECT_EQ(crc, EXPECTED_CRC64_63BYTES);
}

TEST_F(crc64Fixture, crc64_clmul_2352bytes)
{
    if(!have_clmul()) return;

    uint64_t crc = CRC64_ECMA_SEED;

    crc = ~aaruf_crc64_clmul(~crc, buffer, 2352);

    crc ^= CRC64_ECMA_SEED;

    EXPECT_EQ(crc, EXPECTED_CRC64_2352BYTES);
}
#endif

#if defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) || defined(_M_ARM)
TEST_F(crc64Fixture, crc64_vmull)
{
    if(!have_neon()) return;

    uint64_t crc = CRC64_ECMA_SEED;

    crc = ~aaruf_crc64_vmull(~crc, buffer, 1048576);

    crc ^= CRC64_ECMA_SEED;

    EXPECT_EQ(crc, EXPECTED_CRC64);
}

TEST_F(crc64Fixture, crc64_vmull_misaligned)
{
    if(!have_neon()) return;

    uint64_t crc = CRC64_ECMA_SEED;

    crc = ~aaruf_crc64_vmull(~crc, buffer_misaligned + 1, 1048576);

    crc ^= CRC64_ECMA_SEED;

    EXPECT_EQ(crc, EXPECTED_CRC64);
}

TEST_F(crc64Fixture, crc64_vmull_15bytes)
{
    if(!have_neon()) return;

    uint64_t crc = CRC64_ECMA_SEED;

    crc = ~aaruf_crc64_vmull(~crc, buffer, 15);

    crc ^= CRC64_ECMA_SEED;

    EXPECT_EQ(crc, EXPECTED_CRC64_15BYTES);
}

TEST_F(crc64Fixture, crc64_vmull_31bytes)
{
    if(!have_neon()) return;

    uint64_t crc = CRC64_ECMA_SEED;

    crc = ~aaruf_crc64_vmull(~crc, buffer, 31);

    crc ^= CRC64_ECMA_SEED;

    EXPECT_EQ(crc, EXPECTED_CRC64_31BYTES);
}

TEST_F(crc64Fixture, crc64_vmull_63bytes)
{
    if(!have_neon()) return;

    uint64_t crc = CRC64_ECMA_SEED;

    crc = ~aaruf_crc64_vmull(~crc, buffer, 63);

    crc ^= CRC64_ECMA_SEED;

    EXPECT_EQ(crc, EXPECTED_CRC64_63BYTES);
}

TEST_F(crc64Fixture, crc64_vmull_2352bytes)
{
    if(!have_neon()) return;

    uint64_t crc = CRC64_ECMA_SEED;

    crc = ~aaruf_crc64_vmull(~crc, buffer, 2352);

    crc ^= CRC64_ECMA_SEED;

    EXPECT_EQ(crc, EXPECTED_CRC64_2352BYTES);
}
#endif
