//
// Created by claunia on 5/10/21.
//

#include "../include/aaruformat.h"

#include "gtest/gtest.h"
#include <climits>
#include <cstdint>
#include <cstring>

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
    void*    ctx = aaruf_crc64_init_ecma();
    uint64_t crc;

    EXPECT_NE(ctx, nullptr);

    aaruf_crc64_update(ctx, buffer, 1048576);
    crc = aaruf_crc64_final(ctx);

    EXPECT_EQ(crc, EXPECTED_CRC64);
}

TEST_F(crc64Fixture, crc64_auto_misaligned)
{
    void*    ctx = aaruf_crc64_init_ecma();
    uint64_t crc;

    EXPECT_NE(ctx, nullptr);

    aaruf_crc64_update(ctx, buffer_misaligned + 1, 1048576);
    crc = aaruf_crc64_final(ctx);

    EXPECT_EQ(crc, EXPECTED_CRC64);
}

TEST_F(crc64Fixture, crc64_auto_15bytes)
{
    void*    ctx = aaruf_crc64_init_ecma();
    uint64_t crc;

    EXPECT_NE(ctx, nullptr);

    aaruf_crc64_update(ctx, buffer, 15);
    crc = aaruf_crc64_final(ctx);

    EXPECT_EQ(crc, EXPECTED_CRC64_15BYTES);
}

TEST_F(crc64Fixture, crc64_auto_31bytes)
{
    void*    ctx = aaruf_crc64_init_ecma();
    uint64_t crc;

    EXPECT_NE(ctx, nullptr);

    aaruf_crc64_update(ctx, buffer, 31);
    crc = aaruf_crc64_final(ctx);

    EXPECT_EQ(crc, EXPECTED_CRC64_31BYTES);
}

TEST_F(crc64Fixture, crc64_auto_63bytes)
{
    void*    ctx = aaruf_crc64_init_ecma();
    uint64_t crc;

    EXPECT_NE(ctx, nullptr);

    aaruf_crc64_update(ctx, buffer, 63);
    crc = aaruf_crc64_final(ctx);

    EXPECT_EQ(crc, EXPECTED_CRC64_63BYTES);
}

TEST_F(crc64Fixture, crc64_auto_2352bytes)
{
    void*    ctx = aaruf_crc64_init_ecma();
    uint64_t crc;

    EXPECT_NE(ctx, nullptr);

    aaruf_crc64_update(ctx, buffer, 2352);
    crc = aaruf_crc64_final(ctx);

    EXPECT_EQ(crc, EXPECTED_CRC64_2352BYTES);
}