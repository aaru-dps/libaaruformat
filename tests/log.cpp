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

/**
 * @file log.cpp
 * @brief Tests for the internal TRACE/FATAL logger: file output, line format, level filtering and long lines.
 */

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "../include/log.h"
#include "gtest/gtest.h"

namespace
{

const char *kLogFile = "log_test_output.txt";

void set_env(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if(value)
        setenv(name, value, 1);
    else
        unsetenv(name);
#endif
}

std::string read_log()
{
    std::ifstream     in(kLogFile, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

class LogTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        std::remove(kLogFile);
        aaru_log_reset();
        set_env("AARUFORMAT_LOG_FILE", kLogFile);
        set_env("AARUFORMAT_LOG_LEVEL", nullptr);
    }

    void TearDown() override
    {
        aaru_log_reset();
        set_env("AARUFORMAT_LOG_FILE", nullptr);
        set_env("AARUFORMAT_LOG_LEVEL", nullptr);
        std::remove(kLogFile);
    }
};

}  // namespace

TEST_F(LogTest, WritesTraceAndFatalLines)
{
    aaru_log_write(AARU_LOG_TRACE, "/build/src/blocks/data.c", 42, "some_func", "value=%d name=%s", 7, "abc");
    aaru_log_write(AARU_LOG_FATAL, "C:\\build\\src\\open.c", 9, "other_func", "boom");
    aaru_log_reset();

    const std::string log = read_log();

    EXPECT_NE(log.find("[TRACE] blocks/data.c:42 some_func(): value=7 name=abc\n"), std::string::npos) << log;
    EXPECT_NE(log.find("[FATAL] open.c:9 other_func(): boom\n"), std::string::npos) << log;
}

TEST_F(LogTest, FatalLevelFiltersTrace)
{
    set_env("AARUFORMAT_LOG_LEVEL", "fatal");

    aaru_log_write(AARU_LOG_TRACE, "trace.c", 1, "f", "hidden");
    aaru_log_write(AARU_LOG_FATAL, "fatal.c", 2, "g", "shown");
    aaru_log_reset();

    const std::string log = read_log();

    EXPECT_EQ(log.find("hidden"), std::string::npos) << log;
    EXPECT_NE(log.find("[FATAL] fatal.c:2 g(): shown\n"), std::string::npos) << log;
}

TEST_F(LogTest, NoneLevelWritesNothing)
{
    set_env("AARUFORMAT_LOG_LEVEL", "none");

    aaru_log_write(AARU_LOG_FATAL, "fatal.c", 2, "g", "shown");
    aaru_log_reset();

    EXPECT_TRUE(read_log().empty());
}

TEST_F(LogTest, LongLinesAreNotTruncated)
{
    const std::string payload(5000, 'x');

    aaru_log_write(AARU_LOG_TRACE, "long.c", 3, "h", "%s|end", payload.c_str());
    aaru_log_reset();

    const std::string log = read_log();

    EXPECT_NE(log.find(payload + "|end\n"), std::string::npos);
}
