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

#include <cstdlib>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

extern "C"
{
#include "../src/lib/gf256.h"
#include "../src/lib/reed_solomon.h"
}

/* =========================================================================
 * GF(2^8) basic arithmetic tests
 * ========================================================================= */

class GF256Test : public ::testing::Test {};

TEST_F(GF256Test, MulIdentity)
{
    for(int a = 0; a < 256; a++)
    {
        EXPECT_EQ(gf256_mul((uint8_t)a, 1), (uint8_t)a);
        EXPECT_EQ(gf256_mul(1, (uint8_t)a), (uint8_t)a);
    }
}

TEST_F(GF256Test, MulZero)
{
    for(int a = 0; a < 256; a++)
    {
        EXPECT_EQ(gf256_mul((uint8_t)a, 0), 0);
        EXPECT_EQ(gf256_mul(0, (uint8_t)a), 0);
    }
}

TEST_F(GF256Test, MulCommutative)
{
    /* Test a subset of pairs for commutativity */
    for(int a = 0; a < 256; a += 7)
        for(int b = 0; b < 256; b += 11)
            EXPECT_EQ(gf256_mul((uint8_t)a, (uint8_t)b), gf256_mul((uint8_t)b, (uint8_t)a));
}

TEST_F(GF256Test, MulInverse)
{
    for(int a = 1; a < 256; a++)
    {
        uint8_t inv = gf256_inv((uint8_t)a);
        EXPECT_EQ(gf256_mul((uint8_t)a, inv), 1) << "Failed for a=" << a;
    }
}

TEST_F(GF256Test, DivRoundTrip)
{
    for(int a = 1; a < 256; a += 3)
        for(int b = 1; b < 256; b += 5)
        {
            uint8_t product  = gf256_mul((uint8_t)a, (uint8_t)b);
            uint8_t quotient = gf256_div(product, (uint8_t)b);
            EXPECT_EQ(quotient, (uint8_t)a) << "a=" << a << " b=" << b;
        }
}

/* =========================================================================
 * GF(2^8) region operations
 * ========================================================================= */

class GF256RegionTest : public ::testing::Test
{
  protected:
    static constexpr size_t REGION_SIZE = 4096;

    void SetUp() override
    {
        src_.resize(REGION_SIZE);
        dst_.resize(REGION_SIZE);
        ref_.resize(REGION_SIZE);

        /* Fill with deterministic pseudo-random data */
        srand(42);
        for(size_t i = 0; i < REGION_SIZE; i++)
            src_[i] = (uint8_t)(rand() & 0xFF);
    }

    std::vector<uint8_t> src_, dst_, ref_;
};

TEST_F(GF256RegionTest, XorRegion)
{
    memset(dst_.data(), 0, REGION_SIZE);
    gf256_xor_region(dst_.data(), src_.data(), REGION_SIZE);

    /* dst should now equal src */
    EXPECT_EQ(memcmp(dst_.data(), src_.data(), REGION_SIZE), 0);

    /* XOR again should give zeros */
    gf256_xor_region(dst_.data(), src_.data(), REGION_SIZE);
    for(size_t i = 0; i < REGION_SIZE; i++)
        EXPECT_EQ(dst_[i], 0) << "i=" << i;
}

TEST_F(GF256RegionTest, MulRegionCoeff1IsXor)
{
    memset(dst_.data(), 0, REGION_SIZE);
    gf256_mul_region(dst_.data(), src_.data(), 1, REGION_SIZE);
    EXPECT_EQ(memcmp(dst_.data(), src_.data(), REGION_SIZE), 0);
}

TEST_F(GF256RegionTest, MulRegionCoeff0IsNoop)
{
    memset(dst_.data(), 0xAA, REGION_SIZE);
    memcpy(ref_.data(), dst_.data(), REGION_SIZE);
    gf256_mul_region(dst_.data(), src_.data(), 0, REGION_SIZE);
    EXPECT_EQ(memcmp(dst_.data(), ref_.data(), REGION_SIZE), 0);
}

TEST_F(GF256RegionTest, MulRegionMatchesScalar)
{
    /* Verify SIMD path (if any) matches scalar computation for a set of coefficients */
    for(int coeff = 2; coeff < 256; coeff += 37)
    {
        memset(dst_.data(), 0, REGION_SIZE);
        gf256_mul_region(dst_.data(), src_.data(), (uint8_t)coeff, REGION_SIZE);

        /* Compute scalar reference */
        memset(ref_.data(), 0, REGION_SIZE);
        for(size_t i = 0; i < REGION_SIZE; i++)
            ref_[i] = gf256_mul(src_[i], (uint8_t)coeff);

        EXPECT_EQ(memcmp(dst_.data(), ref_.data(), REGION_SIZE), 0) << "coeff=" << coeff;
    }
}

TEST_F(GF256RegionTest, MulRegionAccumulates)
{
    /* Verify that mul_region XOR-accumulates: dst[i] ^= mul(src[i], coeff) */
    memset(dst_.data(), 0x55, REGION_SIZE);
    memcpy(ref_.data(), dst_.data(), REGION_SIZE);

    uint8_t coeff = 0x37;
    gf256_mul_region(dst_.data(), src_.data(), coeff, REGION_SIZE);

    for(size_t i = 0; i < REGION_SIZE; i++)
        EXPECT_EQ(dst_[i], (uint8_t)(ref_[i] ^ gf256_mul(src_[i], coeff))) << "i=" << i;
}

TEST_F(GF256RegionTest, MulRegionOddSizes)
{
    /* Test non-aligned sizes: 1, 15, 17, 31, 33, 63, 65 */
    uint8_t coeff = 0xAB;
    size_t sizes[] = {1, 15, 17, 31, 33, 63, 65, 100, 255};
    for(size_t sz : sizes)
    {
        memset(dst_.data(), 0, sz);
        gf256_mul_region(dst_.data(), src_.data(), coeff, sz);

        for(size_t i = 0; i < sz; i++)
            EXPECT_EQ(dst_[i], gf256_mul(src_[i], coeff)) << "sz=" << sz << " i=" << i;
    }
}

/* =========================================================================
 * Reed-Solomon codec tests
 * ========================================================================= */

class ReedSolomonTest : public ::testing::Test
{
  protected:
    static constexpr size_t SHARD_SIZE = 1024;

    /** Fill shard with deterministic data based on its index. */
    static void fill_shard(uint8_t *shard, uint16_t index)
    {
        srand(index * 12345 + 67890);
        for(size_t i = 0; i < SHARD_SIZE; i++)
            shard[i] = (uint8_t)(rand() & 0xFF);
    }

    /** Allocate and fill K data shards + M empty parity shards. */
    void setup_shards(uint16_t K, uint16_t M)
    {
        total_ = K + M;
        shards_.resize(total_);
        present_.resize(total_, 1);
        backup_.resize(K);

        for(uint16_t i = 0; i < total_; i++)
        {
            shards_[i] = (uint8_t *)calloc(1, SHARD_SIZE);
            ASSERT_NE(shards_[i], nullptr);
        }

        /* Fill data shards */
        for(uint16_t i = 0; i < K; i++)
        {
            fill_shard(shards_[i], i);
            backup_[i] = (uint8_t *)malloc(SHARD_SIZE);
            memcpy(backup_[i], shards_[i], SHARD_SIZE);
        }
    }

    void cleanup()
    {
        for(auto *s : shards_) free(s);
        for(auto *b : backup_) free(b);
        shards_.clear();
        backup_.clear();
        present_.clear();
    }

    std::vector<uint8_t *> shards_;
    std::vector<uint8_t *> backup_;
    std::vector<uint8_t>   present_;
    uint16_t               total_ = 0;
};

TEST_F(ReedSolomonTest, CreateFree)
{
    rs_context *ctx = rs_create(4, 2);
    ASSERT_NE(ctx, nullptr);
    rs_free(ctx);
}

TEST_F(ReedSolomonTest, CreateInvalid)
{
    EXPECT_EQ(rs_create(0, 2), nullptr);
    EXPECT_EQ(rs_create(4, 0), nullptr);
    EXPECT_EQ(rs_create(200, 56), nullptr);  /* 200+56 = 256 > 255 */
    EXPECT_NE(rs_create(200, 55), nullptr);  /* 200+55 = 255, OK */
    rs_free(rs_create(200, 55));
}

TEST_F(ReedSolomonTest, EncodeDecodeXOR_M1)
{
    uint16_t K = 4, M = 1;
    rs_context *ctx = rs_create(K, M);
    ASSERT_NE(ctx, nullptr);

    setup_shards(K, M);

    /* Encode: accumulate parity */
    for(uint16_t k = 0; k < K; k++)
    {
        uint8_t coeff = rs_get_coefficient(ctx, 0, k);
        rs_encode_incremental(coeff, shards_[k], shards_[K], SHARD_SIZE);
    }

    /* Erase shard 0 */
    memset(shards_[0], 0, SHARD_SIZE);
    present_[0] = 0;

    EXPECT_EQ(rs_decode(ctx, shards_.data(), present_.data(), SHARD_SIZE), 0);
    EXPECT_EQ(memcmp(shards_[0], backup_[0], SHARD_SIZE), 0);

    cleanup();
    rs_free(ctx);
}

TEST_F(ReedSolomonTest, EncodeDecodeSingleErasure)
{
    uint16_t K = 8, M = 2;
    rs_context *ctx = rs_create(K, M);
    ASSERT_NE(ctx, nullptr);

    setup_shards(K, M);

    /* Encode */
    for(uint16_t m = 0; m < M; m++)
        for(uint16_t k = 0; k < K; k++)
        {
            uint8_t coeff = rs_get_coefficient(ctx, m, k);
            rs_encode_incremental(coeff, shards_[k], shards_[K + m], SHARD_SIZE);
        }

    /* Erase each data shard one at a time and recover */
    for(uint16_t e = 0; e < K; e++)
    {
        /* Restore all shards from backup first */
        for(uint16_t k = 0; k < K; k++)
            memcpy(shards_[k], backup_[k], SHARD_SIZE);
        memset(present_.data(), 1, total_);

        /* Erase shard e */
        memset(shards_[e], 0, SHARD_SIZE);
        present_[e] = 0;

        EXPECT_EQ(rs_decode(ctx, shards_.data(), present_.data(), SHARD_SIZE), 0) << "e=" << e;
        EXPECT_EQ(memcmp(shards_[e], backup_[e], SHARD_SIZE), 0) << "e=" << e;
    }

    cleanup();
    rs_free(ctx);
}

TEST_F(ReedSolomonTest, EncodeDecodeDoubleErasure)
{
    uint16_t K = 8, M = 2;
    rs_context *ctx = rs_create(K, M);
    ASSERT_NE(ctx, nullptr);

    setup_shards(K, M);

    /* Encode */
    for(uint16_t m = 0; m < M; m++)
        for(uint16_t k = 0; k < K; k++)
        {
            uint8_t coeff = rs_get_coefficient(ctx, m, k);
            rs_encode_incremental(coeff, shards_[k], shards_[K + m], SHARD_SIZE);
        }

    /* Erase shards 2 and 5 */
    memset(shards_[2], 0, SHARD_SIZE);
    memset(shards_[5], 0, SHARD_SIZE);
    present_[2] = 0;
    present_[5] = 0;

    EXPECT_EQ(rs_decode(ctx, shards_.data(), present_.data(), SHARD_SIZE), 0);
    EXPECT_EQ(memcmp(shards_[2], backup_[2], SHARD_SIZE), 0);
    EXPECT_EQ(memcmp(shards_[5], backup_[5], SHARD_SIZE), 0);

    cleanup();
    rs_free(ctx);
}

TEST_F(ReedSolomonTest, EncodeDecodeMaxErasure)
{
    uint16_t K = 4, M = 4;
    rs_context *ctx = rs_create(K, M);
    ASSERT_NE(ctx, nullptr);

    setup_shards(K, M);

    /* Encode */
    for(uint16_t m = 0; m < M; m++)
        for(uint16_t k = 0; k < K; k++)
        {
            uint8_t coeff = rs_get_coefficient(ctx, m, k);
            rs_encode_incremental(coeff, shards_[k], shards_[K + m], SHARD_SIZE);
        }

    /* Erase all M=4 data shards (0,1,2,3) — parity can recover all */
    for(uint16_t e = 0; e < M; e++)
    {
        memset(shards_[e], 0, SHARD_SIZE);
        present_[e] = 0;
    }

    EXPECT_EQ(rs_decode(ctx, shards_.data(), present_.data(), SHARD_SIZE), 0);
    for(uint16_t e = 0; e < M; e++)
        EXPECT_EQ(memcmp(shards_[e], backup_[e], SHARD_SIZE), 0) << "e=" << e;

    cleanup();
    rs_free(ctx);
}

TEST_F(ReedSolomonTest, TooManyErasures)
{
    uint16_t K = 4, M = 2;
    rs_context *ctx = rs_create(K, M);
    ASSERT_NE(ctx, nullptr);

    setup_shards(K, M);

    /* Encode */
    for(uint16_t m = 0; m < M; m++)
        for(uint16_t k = 0; k < K; k++)
        {
            uint8_t coeff = rs_get_coefficient(ctx, m, k);
            rs_encode_incremental(coeff, shards_[k], shards_[K + m], SHARD_SIZE);
        }

    /* Erase M+1 = 3 shards — should fail */
    memset(shards_[0], 0, SHARD_SIZE); present_[0] = 0;
    memset(shards_[1], 0, SHARD_SIZE); present_[1] = 0;
    memset(shards_[2], 0, SHARD_SIZE); present_[2] = 0;

    EXPECT_EQ(rs_decode(ctx, shards_.data(), present_.data(), SHARD_SIZE), -1);

    cleanup();
    rs_free(ctx);
}

TEST_F(ReedSolomonTest, ParityErasure)
{
    uint16_t K = 4, M = 2;
    rs_context *ctx = rs_create(K, M);
    ASSERT_NE(ctx, nullptr);

    setup_shards(K, M);

    /* Encode */
    for(uint16_t m = 0; m < M; m++)
        for(uint16_t k = 0; k < K; k++)
        {
            uint8_t coeff = rs_get_coefficient(ctx, m, k);
            rs_encode_incremental(coeff, shards_[k], shards_[K + m], SHARD_SIZE);
        }

    /* Save parity before erasing */
    uint8_t *parity0_backup = (uint8_t *)malloc(SHARD_SIZE);
    memcpy(parity0_backup, shards_[K], SHARD_SIZE);

    /* Erase parity shard 0 and data shard 1 */
    memset(shards_[K], 0, SHARD_SIZE);
    memset(shards_[1], 0, SHARD_SIZE);
    present_[K] = 0;
    present_[1] = 0;

    EXPECT_EQ(rs_decode(ctx, shards_.data(), present_.data(), SHARD_SIZE), 0);
    EXPECT_EQ(memcmp(shards_[1], backup_[1], SHARD_SIZE), 0);
    EXPECT_EQ(memcmp(shards_[K], parity0_backup, SHARD_SIZE), 0);

    free(parity0_backup);
    cleanup();
    rs_free(ctx);
}

TEST_F(ReedSolomonTest, NoErasureIsNoop)
{
    uint16_t K = 4, M = 2;
    rs_context *ctx = rs_create(K, M);
    ASSERT_NE(ctx, nullptr);

    setup_shards(K, M);

    /* Don't erase anything */
    EXPECT_EQ(rs_decode(ctx, shards_.data(), present_.data(), SHARD_SIZE), 0);

    /* Data shards should be unchanged */
    for(uint16_t k = 0; k < K; k++)
        EXPECT_EQ(memcmp(shards_[k], backup_[k], SHARD_SIZE), 0);

    cleanup();
    rs_free(ctx);
}

TEST_F(ReedSolomonTest, BoundaryK1M1)
{
    uint16_t K = 1, M = 1;
    rs_context *ctx = rs_create(K, M);
    ASSERT_NE(ctx, nullptr);

    setup_shards(K, M);

    /* Encode */
    uint8_t coeff = rs_get_coefficient(ctx, 0, 0);
    rs_encode_incremental(coeff, shards_[0], shards_[1], SHARD_SIZE);

    /* Erase data shard */
    memset(shards_[0], 0, SHARD_SIZE);
    present_[0] = 0;

    EXPECT_EQ(rs_decode(ctx, shards_.data(), present_.data(), SHARD_SIZE), 0);
    EXPECT_EQ(memcmp(shards_[0], backup_[0], SHARD_SIZE), 0);

    cleanup();
    rs_free(ctx);
}

TEST_F(ReedSolomonTest, VariousKM)
{
    /* Test a range of K, M combinations */
    struct { uint16_t K, M; } configs[] = {
        {2, 1}, {4, 1}, {8, 1}, {16, 1},
        {2, 2}, {4, 2}, {8, 2}, {16, 2},
        {4, 4}, {8, 4}, {16, 4},
    };

    for(auto &cfg : configs)
    {
        rs_context *ctx = rs_create(cfg.K, cfg.M);
        ASSERT_NE(ctx, nullptr) << "K=" << cfg.K << " M=" << cfg.M;

        setup_shards(cfg.K, cfg.M);

        /* Encode */
        for(uint16_t m = 0; m < cfg.M; m++)
            for(uint16_t k = 0; k < cfg.K; k++)
            {
                uint8_t coeff = rs_get_coefficient(ctx, m, k);
                rs_encode_incremental(coeff, shards_[k], shards_[cfg.K + m], SHARD_SIZE);
            }

        /* Erase first M shards and recover */
        for(uint16_t e = 0; e < cfg.M; e++)
        {
            memset(shards_[e], 0, SHARD_SIZE);
            present_[e] = 0;
        }

        EXPECT_EQ(rs_decode(ctx, shards_.data(), present_.data(), SHARD_SIZE), 0)
            << "K=" << cfg.K << " M=" << cfg.M;

        for(uint16_t e = 0; e < cfg.M; e++)
            EXPECT_EQ(memcmp(shards_[e], backup_[e], SHARD_SIZE), 0)
                << "K=" << cfg.K << " M=" << cfg.M << " e=" << e;

        cleanup();
        rs_free(ctx);
    }
}

TEST_F(ReedSolomonTest, LargeShard)
{
    /* Test with a larger shard size to exercise SIMD paths more thoroughly */
    static constexpr size_t LARGE_SHARD = 65536;
    uint16_t K = 4, M = 2;

    rs_context *ctx = rs_create(K, M);
    ASSERT_NE(ctx, nullptr);

    std::vector<uint8_t *> shards(K + M);
    std::vector<uint8_t *> backup(K);
    std::vector<uint8_t>   present(K + M, 1);

    for(int i = 0; i < K + M; i++)
        shards[i] = (uint8_t *)calloc(1, LARGE_SHARD);
    for(int k = 0; k < K; k++)
    {
        srand(k * 54321);
        for(size_t i = 0; i < LARGE_SHARD; i++)
            shards[k][i] = (uint8_t)(rand() & 0xFF);
        backup[k] = (uint8_t *)malloc(LARGE_SHARD);
        memcpy(backup[k], shards[k], LARGE_SHARD);
    }

    /* Encode */
    for(uint16_t m = 0; m < M; m++)
        for(uint16_t k = 0; k < K; k++)
        {
            uint8_t coeff = rs_get_coefficient(ctx, m, k);
            rs_encode_incremental(coeff, shards[k], shards[K + m], LARGE_SHARD);
        }

    /* Erase shards 1 and 3 */
    memset(shards[1], 0, LARGE_SHARD); present[1] = 0;
    memset(shards[3], 0, LARGE_SHARD); present[3] = 0;

    EXPECT_EQ(rs_decode(ctx, shards.data(), present.data(), LARGE_SHARD), 0);
    EXPECT_EQ(memcmp(shards[1], backup[1], LARGE_SHARD), 0);
    EXPECT_EQ(memcmp(shards[3], backup[3], LARGE_SHARD), 0);

    for(auto *s : shards) free(s);
    for(auto *b : backup) free(b);
    rs_free(ctx);
}
