/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2025 Natalia Portillo.
 *
 * Internal SHA-256 tests (no OpenSSL dependency).
 * Validates implementation against standard FIPS 180-4 test vectors and the bundled random file.
 */

#include <unistd.h>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "decls.h"
#include "gtest/gtest.h"
#include "sha256.h"

// Test vectors
static const unsigned char sha256_empty[SHA256_DIGEST_LENGTH] = {
    0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
    0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55};

static const unsigned char sha256_abc[SHA256_DIGEST_LENGTH] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};

// Previously validated expected SHA-256 of tests/data/random (1 MiB file)
static const unsigned char sha256_random[SHA256_DIGEST_LENGTH] = {
    0x4d, 0x1a, 0x6b, 0x8a, 0x54, 0x67, 0x00, 0xc4, 0x8e, 0xda, 0x70, 0xd3, 0x39, 0x1c, 0x8f, 0x15,
    0x8a, 0x8d, 0x12, 0xb2, 0x38, 0x92, 0x89, 0x29, 0x50, 0x47, 0x8c, 0x41, 0x8e, 0x25, 0xcc, 0x39};

#define EXPECT_ARRAY_EQ(expected, actual, len)   \
    for(size_t _i = 0; _i < (size_t)(len); ++_i) \
    EXPECT_EQ(((const unsigned char *)(expected))[_i], ((const unsigned char *)(actual))[_i])

TEST(SHA256, EmptyOneShot)
{
    unsigned char out[SHA256_DIGEST_LENGTH];
    aaruf_sha256_buffer("", 0, out);
    EXPECT_ARRAY_EQ(sha256_empty, out, SHA256_DIGEST_LENGTH);
}

TEST(SHA256, ABCOneShot)
{
    unsigned char out[SHA256_DIGEST_LENGTH];
    const char   *msg = "abc";
    aaruf_sha256_buffer(msg, 3, out);
    EXPECT_ARRAY_EQ(sha256_abc, out, SHA256_DIGEST_LENGTH);
}

TEST(SHA256, ABCStreaming)
{
    unsigned char out[SHA256_DIGEST_LENGTH];
    sha256_ctx    ctx;
    aaruf_sha256_init(&ctx);
    const char *msg = "abc";
    for(size_t i = 0; i < 3; i++) aaruf_sha256_update(&ctx, msg + i, 1);
    aaruf_sha256_final(&ctx, out);
    EXPECT_ARRAY_EQ(sha256_abc, out, SHA256_DIGEST_LENGTH);
}

class sha256RandomFixture : public ::testing::Test
{
protected:
    unsigned char *buffer = nullptr;
    size_t         size   = 0;

    void SetUp() override
    {
        char path[PATH_MAX];
        char filename[PATH_MAX];
        getcwd(path, PATH_MAX);
        snprintf(filename, PATH_MAX, "%s/data/random", path);
        FILE *f = fopen(filename, "rb");
        ASSERT_NE(f, nullptr) << "Failed to open random test file";
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        ASSERT_GT(sz, 0);
        fseek(f, 0, SEEK_SET);
        buffer = (unsigned char *)malloc((size_t)sz);
        ASSERT_NE(buffer, nullptr);
        size = fread(buffer, 1, (size_t)sz, f);
        fclose(f);
        ASSERT_EQ(size, (size_t)sz);
    }

    void TearDown() override
    {
        free(buffer);
        buffer = nullptr;
    }
};

TEST_F(sha256RandomFixture, RandomFileOneShot)
{
    unsigned char out[SHA256_DIGEST_LENGTH];
    aaruf_sha256_buffer(buffer, (unsigned long)size, out);
    EXPECT_ARRAY_EQ(sha256_random, out, SHA256_DIGEST_LENGTH);
}

TEST_F(sha256RandomFixture, RandomFileStreaming_Chunk8K)
{
    unsigned char out[SHA256_DIGEST_LENGTH];
    sha256_ctx    ctx;
    aaruf_sha256_init(&ctx);
    const size_t chunk = 8192;
    size_t       pos   = 0;
    while(pos < size)
    {
        size_t to_do = (size - pos) < chunk ? (size - pos) : chunk;
        aaruf_sha256_update(&ctx, buffer + pos, (unsigned long)to_do);
        pos += to_do;
    }
    aaruf_sha256_final(&ctx, out);
    EXPECT_ARRAY_EQ(sha256_random, out, SHA256_DIGEST_LENGTH);
}
