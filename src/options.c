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
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "structs/options.h"

#include "log.h"

static char *aaru_strtok_reentrant(char *str, const char *delim, char **saveptr)
{
#if defined(_MSC_VER)
    return strtok_s(str, delim, saveptr);
#else
    return strtok_r(str, delim, saveptr);
#endif
}

/**
 * @brief Parses the options string for AaruFormat image creation/opening.
 *
 * Parses the options string and returns a struct with the parsed options for image creation or opening.
 *
 * @param options String with options to parse (may be NULL).
 * @return Parsed options as an aaru_options struct.
 */
aaru_options parse_options(const char *options, bool *table_shift_found)
{
    const char *options_str = options != NULL ? options : "(null)";
    TRACE("Entering parse_options(%s)", options_str);

    aaru_options parsed = {.compress        = true,
                           .deduplicate     = true,
                           .dictionary      = 33554432,
                           .table_shift     = -1,
                           .data_shift      = 12,
                           .block_alignment = 9,
                           .md5             = false,
                           .sha1            = false,
                           .sha256          = false,
                           .blake3          = false,
                           .spamsum         = false,
                           .zstd            = false,
                           .zstd_level      = 19,
                           .num_threads     = 1};

    if(options == NULL)
    {
        TRACE("Exiting parse_options() = {compress: %d, deduplicate: %d, dictionary: %u, table_shift: %d, "
              "data_shift: %u, block_alignment: %u, md5: %d, sha1: %d, sha256: %d, blake3: %d, spamsum: %d, "
              "zstd: %d, zstd_level: %d, num_threads: %d}",
              parsed.compress, parsed.deduplicate, parsed.dictionary, parsed.table_shift, parsed.data_shift,
              parsed.block_alignment, parsed.md5, parsed.sha1, parsed.sha256, parsed.blake3, parsed.spamsum,
              parsed.zstd, parsed.zstd_level, parsed.num_threads);
        return parsed;
    }

    char buffer[1024];
    strncpy(buffer, options, sizeof(buffer));
    buffer[sizeof(buffer) - 1] = '\0';

    char *saveptr = NULL;
    char *token   = aaru_strtok_reentrant(buffer, ";", &saveptr);
    while(token != NULL)
    {
        char *equal = strchr(token, '=');
        if(equal)
        {
            *equal            = '\0';
            const char *key   = token;
            const char *value = equal + 1;

            const bool bval = strncmp(value, "true", 4) == 0;

            if(strncmp(key, "compress", 8) == 0)
                parsed.compress = bval;
            else if(strncmp(key, "deduplicate", 11) == 0)
                parsed.deduplicate = bval;
            else if(strncmp(key, "dictionary", 10) == 0)
            {
                parsed.dictionary = (uint32_t)strtoul(value, NULL, 10);
                if(parsed.dictionary == 0) parsed.dictionary = 33554432;
            }
            else if(strncmp(key, "table_shift", 11) == 0)
            {
                errno              = 0;
                char *endptr       = NULL;
                long  parsed_value = strtol(value, &endptr, 10);
                if(errno == 0 && endptr != value)
                {
                    if(parsed_value < INT8_MIN) parsed_value = INT8_MIN;
                    if(parsed_value > INT8_MAX) parsed_value = INT8_MAX;
                    parsed.table_shift = (int8_t)parsed_value;
                    *table_shift_found = true;
                }
            }
            else if(strncmp(key, "data_shift", 10) == 0)
            {
                errno              = 0;
                char *endptr       = NULL;
                long  parsed_value = strtol(value, &endptr, 10);
                if(errno == 0 && endptr != value && parsed_value >= 0)
                {
                    if(parsed_value > UINT8_MAX) parsed_value = UINT8_MAX;
                    parsed.data_shift = (uint8_t)parsed_value;
                    if(parsed.data_shift == 0) parsed.data_shift = 12;
                }
            }
            else if(strncmp(key, "block_alignment", 15) == 0)
            {
                errno              = 0;
                char *endptr       = NULL;
                long  parsed_value = strtol(value, &endptr, 10);
                if(errno == 0 && endptr != value && parsed_value >= 0)
                {
                    if(parsed_value > UINT8_MAX) parsed_value = UINT8_MAX;
                    parsed.block_alignment = (uint8_t)parsed_value;
                    if(parsed.block_alignment == 0) parsed.block_alignment = 9;
                }
            }
            else if(strncmp(key, "md5", 3) == 0)
                parsed.md5 = bval;
            else if(strncmp(key, "sha1", 4) == 0)
                parsed.sha1 = bval;
            else if(strncmp(key, "sha256", 6) == 0)
                parsed.sha256 = bval;
            else if(strncmp(key, "blake3", 6) == 0)
                parsed.blake3 = bval;
            else if(strncmp(key, "spamsum", 7) == 0)
                parsed.spamsum = bval;
            else if(strncmp(key, "zstd_level", 10) == 0)
            {
                parsed.zstd_level = (int)strtol(value, NULL, 10);
                if(parsed.zstd_level < 1) parsed.zstd_level = 1;
                if(parsed.zstd_level > 22) parsed.zstd_level = 22;
            }
            else if(strncmp(key, "zstd", 4) == 0)
                parsed.zstd = bval;
            else if(strncmp(key, "threads", 7) == 0)
            {
                parsed.num_threads = (int)strtol(value, NULL, 10);
                if(parsed.num_threads < 1) parsed.num_threads = 1;
            }
        }
        token = aaru_strtok_reentrant(NULL, ";", &saveptr);
    }

    TRACE("Exiting parse_options() = {compress: %d, deduplicate: %d, dictionary: %u, table_shift: %d, "
          "data_shift: %u, block_alignment: %u, md5: %d, sha1: %d, sha256: %d, blake3: %d, spamsum: %d, "
          "zstd: %d, zstd_level: %d, num_threads: %d}",
          parsed.compress, parsed.deduplicate, parsed.dictionary, parsed.table_shift, parsed.data_shift,
          parsed.block_alignment, parsed.md5, parsed.sha1, parsed.sha256, parsed.blake3, parsed.spamsum,
          parsed.zstd, parsed.zstd_level, parsed.num_threads);
    return parsed;
}