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

#ifdef AARU_HAS_SHA256

#include <climits>
#include <cstdint>
#include <openssl/sha.h>

#include "gtest/gtest.h"

static const uint8_t* buffer;

unsigned char expected_sha256[SHA256_DIGEST_LENGTH] = {0x4d, 0x1a, 0x6b, 0x8a, 0x54, 0x67, 0x00, 0xc4, 0x8e, 0xda, 0x70,
                                                       0xd3, 0x39, 0x1c, 0x8f, 0x15, 0x8a, 0x8d, 0x12, 0xb2, 0x38, 0x92,
                                                       0x89, 0x29, 0x50, 0x47, 0x8c, 0x41, 0x8e, 0x25, 0xcc, 0x39};

class sha256Fixture : public ::testing::Test
{
  public:
    sha256Fixture()
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
    }

    void TearDown() { free((void*)buffer); }

    ~sha256Fixture()
    {
        // resources cleanup, no exceptions allowed
    }

    // shared user data
};

#define EXPECT_ARRAY_EQ(reference, actual, element_count)                                                              \
    {                                                                                                                  \
        unsigned char* reference_ = static_cast<unsigned char*>(reference);                                            \
        unsigned char* actual_    = static_cast<unsigned char*>(actual);                                               \
        for(int cmp_i = 0; cmp_i < (element_count); cmp_i++) { EXPECT_EQ(reference_[cmp_i], actual_[cmp_i]); }         \
    }

TEST_F(sha256Fixture, sha256)
{
    SHA256_CTX*   ctx;
    unsigned char hash[SHA256_DIGEST_LENGTH];
    int           ret;

    ctx = static_cast<SHA256_CTX*>(malloc(sizeof(SHA256_CTX)));

    EXPECT_NE(nullptr, ctx);

    ret = SHA256_Init(ctx);

    EXPECT_EQ(ret, 1);

    ret = SHA256_Update(ctx, buffer, 1048576);

    EXPECT_EQ(ret, 1);

    ret = SHA256_Final(hash, ctx);

    EXPECT_EQ(ret, 1);
    EXPECT_ARRAY_EQ(expected_sha256, hash, SHA256_DIGEST_LENGTH);
}

#endif