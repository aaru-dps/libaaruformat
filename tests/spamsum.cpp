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

#define EXPECTED_SPAMSUM "24576:3dvzuAsHTQ16pc7O1Q/gS9qze+Swwn9s6IX:8/TQQpaVqze+JN6IX"
#define EXPECTED_SPAMSUM_15BYTES "3:Ac4E9t:Ac4E9t"
#define EXPECTED_SPAMSUM_31BYTES "3:Ac4E9E5+S09qn:Ac4E9EgSsq"
#define EXPECTED_SPAMSUM_63BYTES "3:Ac4E9E5+S09q2kABV9:Ac4E9EgSs7kW9"
#define EXPECTED_SPAMSUM_2352BYTES "48:pasCLoANDXmjCz1p2OpPm+Gek3xmZfJJ5DD4BacmmlodQMQa/58Z:csK1Nxz7XFGeJS/flHMQu2Z"

static const uint8_t* buffer;
static const uint8_t* buffer_misaligned;

class spamsumFixture : public ::testing::Test
{
  public:
    spamsumFixture()
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

    ~spamsumFixture()
    {
        // resources cleanup, no exceptions allowed
    }

    // shared user data
};

TEST_F(spamsumFixture, spamsum_auto)
{
    spamsum_ctx* ctx     = aaruf_spamsum_init();
    const char*  spamsum = (const char*)malloc(FUZZY_MAX_RESULT);

    EXPECT_NE(ctx, nullptr);
    EXPECT_NE(spamsum, nullptr);

    aaruf_spamsum_update(ctx, buffer, 1048576);
    aaruf_spamsum_final(ctx, (uint8_t*)spamsum);
    aaruf_spamsum_free(ctx);

    EXPECT_STREQ(spamsum, EXPECTED_SPAMSUM);

    free((void*)spamsum);
}

TEST_F(spamsumFixture, spamsum_auto_misaligned)
{
    spamsum_ctx* ctx     = aaruf_spamsum_init();
    const char*  spamsum = (const char*)malloc(FUZZY_MAX_RESULT);

    EXPECT_NE(ctx, nullptr);
    EXPECT_NE(spamsum, nullptr);

    aaruf_spamsum_update(ctx, buffer_misaligned + 1, 1048576);
    aaruf_spamsum_final(ctx, (uint8_t*)spamsum);
    aaruf_spamsum_free(ctx);

    EXPECT_STREQ(spamsum, EXPECTED_SPAMSUM);

    free((void*)spamsum);
}

TEST_F(spamsumFixture, spamsum_auto_15bytes)
{
    spamsum_ctx* ctx     = aaruf_spamsum_init();
    const char*  spamsum = (const char*)malloc(FUZZY_MAX_RESULT);

    EXPECT_NE(ctx, nullptr);
    EXPECT_NE(spamsum, nullptr);

    aaruf_spamsum_update(ctx, buffer, 15);
    aaruf_spamsum_final(ctx, (uint8_t*)spamsum);
    aaruf_spamsum_free(ctx);

    EXPECT_STREQ(spamsum, EXPECTED_SPAMSUM_15BYTES);

    free((void*)spamsum);
}

TEST_F(spamsumFixture, spamsum_auto_31bytes)
{
    spamsum_ctx* ctx     = aaruf_spamsum_init();
    const char*  spamsum = (const char*)malloc(FUZZY_MAX_RESULT);

    EXPECT_NE(ctx, nullptr);
    EXPECT_NE(spamsum, nullptr);

    aaruf_spamsum_update(ctx, buffer, 31);
    aaruf_spamsum_final(ctx, (uint8_t*)spamsum);
    aaruf_spamsum_free(ctx);

    EXPECT_STREQ(spamsum, EXPECTED_SPAMSUM_31BYTES);

    free((void*)spamsum);
}

TEST_F(spamsumFixture, spamsum_auto_63bytes)
{
    spamsum_ctx* ctx     = aaruf_spamsum_init();
    const char*  spamsum = (const char*)malloc(FUZZY_MAX_RESULT);

    EXPECT_NE(ctx, nullptr);
    EXPECT_NE(spamsum, nullptr);

    aaruf_spamsum_update(ctx, buffer, 63);
    aaruf_spamsum_final(ctx, (uint8_t*)spamsum);
    aaruf_spamsum_free(ctx);

    EXPECT_STREQ(spamsum, EXPECTED_SPAMSUM_63BYTES);

    free((void*)spamsum);
}

TEST_F(spamsumFixture, spamsum_auto_2352bytes)
{
    spamsum_ctx* ctx     = aaruf_spamsum_init();
    const char*  spamsum = (const char*)malloc(FUZZY_MAX_RESULT);

    EXPECT_NE(ctx, nullptr);
    EXPECT_NE(spamsum, nullptr);

    aaruf_spamsum_update(ctx, buffer, 2352);
    aaruf_spamsum_final(ctx, (uint8_t*)spamsum);
    aaruf_spamsum_free(ctx);

    EXPECT_STREQ(spamsum, EXPECTED_SPAMSUM_2352BYTES);

    free((void*)spamsum);
}
