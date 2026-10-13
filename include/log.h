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

#ifndef LIBAARUFORMAT_LOG_H
#define LIBAARUFORMAT_LOG_H

/*
 * Internal debug logging.
 *
 * TRACE and FATAL compile to nothing unless ENABLE_TRACE / ENABLE_FATAL are defined
 * (CMake: -DAARU_ENABLE_LOGGING=ON). When enabled, output goes to stderr unless the
 * AARUFORMAT_LOG_FILE environment variable names a file to append to, and the
 * AARUFORMAT_LOG_LEVEL environment variable (trace, fatal, none) filters it at runtime.
 *
 * Never log key material.
 */

#include <inttypes.h>  // PRIu64 and friends, used throughout log format strings
#include <stdarg.h>
#include <stdio.h>

#define AARU_LOG_FATAL 0
#define AARU_LOG_TRACE 1

#if defined(__GNUC__) || defined(__clang__)
#define AARU_LOG_PRINTF_ATTR    __attribute__((format(printf, 5, 6)))
#if !defined(_WIN32) && !defined(__CYGWIN__)
#define AARU_LOG_VISIBILITY_ATTR __attribute__((visibility("hidden")))
#else
#define AARU_LOG_VISIBILITY_ATTR
#endif
#else
#define AARU_LOG_PRINTF_ATTR
#define AARU_LOG_VISIBILITY_ATTR
#endif

#ifdef __cplusplus
extern "C" {
#endif

void aaru_log_write(int level, const char *file, int line, const char *func, const char *fmt,
                    ...) AARU_LOG_PRINTF_ATTR AARU_LOG_VISIBILITY_ATTR;

/* Closes any log file and forgets the configuration so the environment is read again. Used by tests. */
void aaru_log_reset(void) AARU_LOG_VISIBILITY_ATTR;

#ifdef __cplusplus
}
#endif

#ifdef ENABLE_TRACE
#define TRACE(fmt, ...) aaru_log_write(AARU_LOG_TRACE, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#else
#define TRACE(fmt, ...) ((void)0)
#endif

#ifdef ENABLE_FATAL
#define FATAL(fmt, ...) aaru_log_write(AARU_LOG_FATAL, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#else
#define FATAL(fmt, ...) ((void)0)
#endif

#endif  // LIBAARUFORMAT_LOG_H
