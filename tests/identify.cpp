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

#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../include/aaruformat.h"
#include "gtest/gtest.h"

// Helper: create a tmpfile containing a crafted AaruHeader with the given magic and version.
// Returns the FILE* rewound to position 0. Caller must fclose().
static FILE *make_header_stream(uint64_t magic, uint8_t major_version)
{
    FILE *f = tmpfile();
    EXPECT_NE(f, nullptr);
    if(f == nullptr) return nullptr;

    AaruHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.identifier       = magic;
    hdr.imageMajorVersion = major_version;
    fwrite(&hdr, sizeof(hdr), 1, f);
    rewind(f);
    return f;
}

class IdentifyFixture : public testing::Test
{
public:
    IdentifyFixture() = default;

protected:
    char path[PATH_MAX];
    char filename[PATH_MAX];

    void SetUp() override { getcwd(path, PATH_MAX); }
};

// ---------------------------------------------------------------------------
// aaruf_identify_stream() tests
// ---------------------------------------------------------------------------

TEST_F(IdentifyFixture, StreamValidV2)
{
    snprintf(filename, PATH_MAX, "%s/data/mf2hd.aif", path);
    FILE *f = fopen(filename, "rb");
    ASSERT_NE(f, nullptr) << "Failed to open mf2hd.aif";

    EXPECT_EQ(aaruf_identify_stream(f), 100);
    fclose(f);
}

TEST_F(IdentifyFixture, StreamValidV1)
{
    snprintf(filename, PATH_MAX, "%s/data/mf2hd_v1.aif", path);
    FILE *f = fopen(filename, "rb");
    ASSERT_NE(f, nullptr) << "Failed to open mf2hd_v1.aif";

    EXPECT_EQ(aaruf_identify_stream(f), 100);
    fclose(f);
}

TEST_F(IdentifyFixture, StreamNull)
{
    EXPECT_EQ(aaruf_identify_stream(nullptr), 0);
}

TEST_F(IdentifyFixture, StreamNonAaru)
{
    snprintf(filename, PATH_MAX, "%s/data/random", path);
    FILE *f = fopen(filename, "rb");
    ASSERT_NE(f, nullptr) << "Failed to open random data file";

    EXPECT_EQ(aaruf_identify_stream(f), 0);
    fclose(f);
}

TEST_F(IdentifyFixture, StreamEmptyFile)
{
    FILE *f = tmpfile();
    ASSERT_NE(f, nullptr);

    EXPECT_EQ(aaruf_identify_stream(f), 0);
    fclose(f);
}

TEST_F(IdentifyFixture, StreamTruncatedHeader)
{
    // Write only the 8-byte magic — not enough for a full AaruHeader (104 bytes)
    FILE *f = tmpfile();
    ASSERT_NE(f, nullptr);

    const uint64_t magic = AARU_MAGIC;
    fwrite(&magic, sizeof(magic), 1, f);
    rewind(f);

    EXPECT_EQ(aaruf_identify_stream(f), 0);
    fclose(f);
}

TEST_F(IdentifyFixture, StreamInvalidMagic)
{
    FILE *f = make_header_stream(0xDEADBEEFDEADBEEFULL, 2);
    ASSERT_NE(f, nullptr);

    EXPECT_EQ(aaruf_identify_stream(f), 0);
    fclose(f);
}

TEST_F(IdentifyFixture, StreamFutureVersion)
{
    FILE *f = make_header_stream(AARU_MAGIC, 255);
    ASSERT_NE(f, nullptr);

    EXPECT_EQ(aaruf_identify_stream(f), 0);
    fclose(f);
}

TEST_F(IdentifyFixture, StreamDicMagic)
{
    FILE *f = make_header_stream(DIC_MAGIC, 1);
    ASSERT_NE(f, nullptr);

    EXPECT_EQ(aaruf_identify_stream(f), 100);
    fclose(f);
}

TEST_F(IdentifyFixture, StreamMidPosition)
{
    // Verify identify_stream seeks to 0 even when stream is positioned elsewhere
    snprintf(filename, PATH_MAX, "%s/data/mf2hd.aif", path);
    FILE *f = fopen(filename, "rb");
    ASSERT_NE(f, nullptr) << "Failed to open mf2hd.aif";

    fseek(f, 50, SEEK_SET);
    EXPECT_EQ(aaruf_identify_stream(f), 100);
    fclose(f);
}

// ---------------------------------------------------------------------------
// aaruf_identify() tests
// ---------------------------------------------------------------------------

TEST_F(IdentifyFixture, FileValidV2)
{
    snprintf(filename, PATH_MAX, "%s/data/mf2hd.aif", path);
    EXPECT_EQ(aaruf_identify(filename), 100);
}

TEST_F(IdentifyFixture, FileValidV1)
{
    snprintf(filename, PATH_MAX, "%s/data/mf2hd_v1.aif", path);
    EXPECT_EQ(aaruf_identify(filename), 100);
}

TEST_F(IdentifyFixture, FileNull)
{
    EXPECT_EQ(aaruf_identify(nullptr), EINVAL);
}

TEST_F(IdentifyFixture, FileNonExistent)
{
    EXPECT_EQ(aaruf_identify("/tmp/aaruf_test_nonexistent_identify.aif"), ENOENT);
}

TEST_F(IdentifyFixture, FileNonAaru)
{
    snprintf(filename, PATH_MAX, "%s/data/random", path);
    EXPECT_EQ(aaruf_identify(filename), 0);
}
