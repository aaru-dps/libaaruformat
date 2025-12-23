/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * Standalone MD5 test for the file 'random'.
 * Expected MD5: d78f0eec417be386219b21b700044b95
 */

#include <climits>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#include "md5.h"

#include "decls.h"
#include "gtest/gtest.h"

static uint8_t *buffer;

unsigned char expected_md5[16] = {
    0xd7, 0x8f, 0x0e, 0xec, 0x41, 0x7b, 0xe3, 0x86,
    0x21, 0x9b, 0x21, 0xb7, 0x00, 0x04, 0x4b, 0x95
};

class md5Fixture : public ::testing::Test
{
public:
    md5Fixture() = default;

protected:
    void SetUp() override
    {
        char path[PATH_MAX];
        char filename[PATH_MAX];

        getcwd(path, PATH_MAX);
        snprintf(filename, PATH_MAX, "%s/data/random", path);

        FILE *file = fopen(filename, "rb");
        buffer     = static_cast<uint8_t *>(malloc(1048576));
        fread(buffer, 1, 1048576, file);
        fclose(file);
    }

    void TearDown() override { free(buffer); }
    ~md5Fixture() override = default;
};

TEST_F(md5Fixture, MD5OfRandomFile)
{
    uint8_t md5_result[16];

    aaruf_md5_buffer(buffer, 1048576, md5_result);
    EXPECT_EQ(memcmp(md5_result, expected_md5, 16), 0);
}

