/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 */

#include <cstdio>

#include <gtest/gtest.h>

extern "C"
{
#define AARU_NO_FILEIO_REMAP
#include "../include/internal.h"
#undef AARU_NO_FILEIO_REMAP
}

static FILE *open_temp_test_file()
{
#if defined(_MSC_VER)
    FILE *fp = NULL;
    return tmpfile_s(&fp) == 0 ? fp : NULL;
#else
    return tmpfile();
#endif
}

TEST(LargeFileIO, SeekTellPastTwoGiB)
{
    FILE *fp = open_temp_test_file();

    if(fp == NULL) GTEST_SKIP() << "Temporary file creation is unavailable on this platform";

    const aaru_off_t target_offset = ((aaru_off_t)3 << 30) + 17;

    ASSERT_EQ(0, aaruf_fseek(fp, target_offset, SEEK_SET));
    EXPECT_EQ(target_offset, aaruf_ftell(fp));

    ASSERT_NE(EOF, fputc(0x5A, fp));
    ASSERT_EQ(0, fflush(fp));
    ASSERT_EQ(0, aaruf_fseek(fp, target_offset, SEEK_SET));

    EXPECT_EQ(target_offset, aaruf_ftell(fp));
    EXPECT_EQ(0x5A, fgetc(fp));

    fclose(fp);
}
