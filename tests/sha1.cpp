// SHA-1 tests (public domain / MIT implementation integration)
// Uses standard FIPS 180-1 test vectors.

#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "decls.h"
#include "gtest/gtest.h"
#include "sha1.h"

static const unsigned char sha1_empty[SHA1_DIGEST_LENGTH] = {0xDA, 0x39, 0xA3, 0xEE, 0x5E, 0x6B, 0x4B,
                                                             0x0D, 0x32, 0x55, 0xBF, 0xEF, 0x95, 0x60,
                                                             0x18, 0x90, 0xAF, 0xD8, 0x07, 0x09};

static const unsigned char sha1_abc[SHA1_DIGEST_LENGTH] = {0xA9, 0x99, 0x3E, 0x36, 0x47, 0x06, 0x81, 0x6A, 0xBA, 0x3E,
                                                           0x25, 0x71, 0x78, 0x50, 0xC2, 0x6C, 0x9C, 0xD0, 0xD8, 0x9D};

static const unsigned char sha1_random[SHA1_DIGEST_LENGTH] = {0x72, 0x0d, 0x3b, 0x71, 0x7d, 0xe0, 0xc7,
                                                              0x4c, 0x77, 0xdd, 0x9c, 0xaa, 0x9e, 0xba,
                                                              0x50, 0x60, 0xdc, 0xbd, 0x28, 0x8d};

#define EXPECT_ARRAY_EQ(expected, actual, len) \
    for(size_t i_ = 0; i_ < (len); ++i_)       \
    EXPECT_EQ(((const unsigned char *)(expected))[i_], ((const unsigned char *)(actual))[i_])

TEST(SHA1, EmptyBuffer)
{
    unsigned char out[SHA1_DIGEST_LENGTH];
    aaruf_sha1_buffer("", 0, out);
    EXPECT_ARRAY_EQ(sha1_empty, out, SHA1_DIGEST_LENGTH);
}

TEST(SHA1, ABCSingleBuffer)
{
    unsigned char out[SHA1_DIGEST_LENGTH];
    const char   *msg = "abc";
    aaruf_sha1_buffer(msg, 3, out);
    EXPECT_ARRAY_EQ(sha1_abc, out, SHA1_DIGEST_LENGTH);
}

TEST(SHA1, ABCStreaming)
{
    unsigned char out[SHA1_DIGEST_LENGTH];
    sha1_ctx      ctx;
    aaruf_sha1_init(&ctx);
    const char *msg = "abc";
    for(size_t i = 0; i < 3; i++) aaruf_sha1_update(&ctx, msg + i, 1);
    aaruf_sha1_final(&ctx, out);
    EXPECT_ARRAY_EQ(sha1_abc, out, SHA1_DIGEST_LENGTH);
}

TEST(SHA1, RandomFileOneShot)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];
    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/random", path);
    FILE *file = fopen(filename, "rb");
    ASSERT_NE(file, nullptr);
    fseek(file, 0, SEEK_END);
    long sz = ftell(file);
    ASSERT_GT(sz, 0);
    fseek(file, 0, SEEK_SET);
    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    ASSERT_NE(buf, nullptr);
    size_t rd = fread(buf, 1, (size_t)sz, file);
    fclose(file);
    ASSERT_EQ(rd, (size_t)sz);
    unsigned char out[SHA1_DIGEST_LENGTH];
    aaruf_sha1_buffer(buf, (unsigned long)sz, out);
    free(buf);
    EXPECT_ARRAY_EQ(sha1_random, out, SHA1_DIGEST_LENGTH);
}

TEST(SHA1, RandomFileStreaming)
{
    char path[PATH_MAX];
    char filename[PATH_MAX];
    getcwd(path, PATH_MAX);
    snprintf(filename, PATH_MAX, "%s/data/random", path);
    FILE *file = fopen(filename, "rb");
    ASSERT_NE(file, nullptr);
    sha1_ctx ctx;
    aaruf_sha1_init(&ctx);
    unsigned char chunk[8192];
    size_t        rd;
    while((rd = fread(chunk, 1, sizeof(chunk), file)) > 0) aaruf_sha1_update(&ctx, chunk, (unsigned long)rd);
    fclose(file);
    unsigned char out[SHA1_DIGEST_LENGTH];
    aaruf_sha1_final(&ctx, out);
    EXPECT_ARRAY_EQ(sha1_random, out, SHA1_DIGEST_LENGTH);
}
