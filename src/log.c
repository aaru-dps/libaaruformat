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

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L  // localtime_r
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

#include "log.h"

#define LOG_STATE_UNINITIALIZED 0
#define LOG_STATE_INITIALIZING  1
#define LOG_STATE_READY         2

#define LOG_LEVEL_NONE (-1)

#define LOG_STACK_BUFFER_SIZE 1024

static volatile long log_state  = LOG_STATE_UNINITIALIZED;
static FILE         *log_stream = NULL;
static int           log_level  = AARU_LOG_TRACE;

/* Minimal portable atomics so the one-time initialization is race-free without pthreads. */
#if defined(_MSC_VER)
static int  log_state_claim(void) { return InterlockedCompareExchange(&log_state, LOG_STATE_INITIALIZING, LOG_STATE_UNINITIALIZED) == LOG_STATE_UNINITIALIZED; }
static long log_state_load(void) { return InterlockedCompareExchange(&log_state, 0, 0); }
static void log_state_store(long value) { InterlockedExchange(&log_state, value); }
#elif defined(__GNUC__) || defined(__clang__)
static int log_state_claim(void)
{
    long expected = LOG_STATE_UNINITIALIZED;
    return __atomic_compare_exchange_n(&log_state, &expected, LOG_STATE_INITIALIZING, 0, __ATOMIC_ACQ_REL,
                                       __ATOMIC_ACQUIRE);
}
static long log_state_load(void) { return __atomic_load_n(&log_state, __ATOMIC_ACQUIRE); }
static void log_state_store(long value) { __atomic_store_n(&log_state, value, __ATOMIC_RELEASE); }
#else
static int log_state_claim(void)
{
    if(log_state != LOG_STATE_UNINITIALIZED) return 0;
    log_state = LOG_STATE_INITIALIZING;
    return 1;
}
static long log_state_load(void) { return log_state; }
static void log_state_store(long value) { log_state = value; }
#endif

static int log_strcasecmp(const char *a, const char *b)
{
    while(*a && *b)
    {
        int ca = (unsigned char)*a++;
        int cb = (unsigned char)*b++;
        if(ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if(cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if(ca != cb) return ca - cb;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/**
 * @brief Resolves the logging configuration from the environment, exactly once per process.
 *
 * AARUFORMAT_LOG_LEVEL selects "trace" (default), "fatal" or "none".
 * AARUFORMAT_LOG_FILE names a file to append to; if unset or unopenable, stderr is used.
 */
static void log_init(void)
{
    const char *level_env;
    const char *file_env;

    if(log_state_load() == LOG_STATE_READY) return;

    if(!log_state_claim())
    {
        // Another thread is initializing; the window is a getenv() and an fopen().
        while(log_state_load() != LOG_STATE_READY);
        return;
    }

    level_env = getenv("AARUFORMAT_LOG_LEVEL");
    if(level_env != NULL)
    {
        if(log_strcasecmp(level_env, "none") == 0 || log_strcasecmp(level_env, "off") == 0)
            log_level = LOG_LEVEL_NONE;
        else if(log_strcasecmp(level_env, "fatal") == 0)
            log_level = AARU_LOG_FATAL;
        else
            log_level = AARU_LOG_TRACE;
    }

    log_stream = stderr;
    file_env   = getenv("AARUFORMAT_LOG_FILE");
    if(file_env != NULL && *file_env != '\0' && log_level != LOG_LEVEL_NONE)
    {
        FILE *file = fopen(file_env, "a");
        if(file != NULL) log_stream = file;
    }

    log_state_store(LOG_STATE_READY);
}

/** @brief Strips the build-machine directory from __FILE__, keeping the path below src/ if present. */
static const char *log_short_path(const char *file)
{
    const char *p;
    const char *base = file;

    for(p = file; *p; p++)
    {
        if((*p == '/' || *p == '\\') && strncmp(p + 1, "src", 3) == 0 && (p[4] == '/' || p[4] == '\\'))
            base = p + 5;
    }

    if(base != file) return base;

    for(p = file; *p; p++)
        if(*p == '/' || *p == '\\') base = p + 1;

    return base;
}

static int log_timestamp(char *buf, size_t size)
{
#ifdef _WIN32
    SYSTEMTIME st;
    GetLocalTime(&st);
    return snprintf(buf, size, "%02u:%02u:%02u.%03u", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
#else
    struct timeval tv;
    struct tm      tm;
    time_t         secs;

    gettimeofday(&tv, NULL);
    secs = tv.tv_sec;
    if(localtime_r(&secs, &tm) == NULL) return snprintf(buf, size, "??:??:??.???");
    return snprintf(buf, size, "%02d:%02d:%02d.%03ld", tm.tm_hour, tm.tm_min, tm.tm_sec, (long)(tv.tv_usec / 1000));
#endif
}

/**
 * @brief Writes one log line. Called through the TRACE and FATAL macros in log.h.
 *
 * The whole line is formatted into a buffer and emitted with a single fwrite(), which stdio
 * locks per call, so lines from concurrent threads do not interleave.
 */
void aaru_log_write(int level, const char *file, int line, const char *func, const char *fmt, ...)
{
    char        stack_buf[LOG_STACK_BUFFER_SIZE];
    char       *buf = stack_buf;
    char        timestamp[16];
    const char *tag = level == AARU_LOG_FATAL ? "FATAL" : "TRACE";
    int         prefix_len;
    int         msg_len;
    size_t      total;
    va_list     args;
    va_list     args_copy;

    log_init();

    if(level > log_level) return;

    log_timestamp(timestamp, sizeof(timestamp));

    prefix_len = snprintf(NULL, 0, "%s [%s] %s:%d %s(): ", timestamp, tag, log_short_path(file), line, func);

    va_start(args, fmt);
    va_copy(args_copy, args);
    msg_len = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);

    if(prefix_len < 0 || msg_len < 0)
    {
        va_end(args);
        return;
    }

    total = (size_t)prefix_len + (size_t)msg_len + 2;  // newline + terminator

    if(total > sizeof(stack_buf))
    {
        buf = malloc(total);
        if(buf == NULL)
        {
            va_end(args);
            return;
        }
    }

    snprintf(buf, total, "%s [%s] %s:%d %s(): ", timestamp, tag, log_short_path(file), line, func);
    vsnprintf(buf + prefix_len, total - (size_t)prefix_len, fmt, args);
    va_end(args);

    buf[total - 2] = '\n';
    buf[total - 1] = '\0';

    fwrite(buf, 1, total - 1, log_stream);
    fflush(log_stream);

    if(buf != stack_buf) free(buf);
}

void aaru_log_reset(void)
{
    if(log_state_load() == LOG_STATE_READY && log_stream != NULL && log_stream != stderr) fclose(log_stream);

    log_stream = NULL;
    log_level  = AARU_LOG_TRACE;
    log_state_store(LOG_STATE_UNINITIALIZED);
}
