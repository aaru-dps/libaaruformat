/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stdint.h>
#include <stdio.h>

// Zstd dictionary context
typedef struct
{
    uint8_t *dict_data;
    size_t   dict_size;
    uint32_t dict_id;
} zstd_dict_context;

// Compression algorithm identifiers
typedef enum
{
    COMP_LZMA   = 0,
    COMP_BZIP3  = 1,
    COMP_ZSTD   = 2,
    COMP_BROTLI = 3
} compression_algorithm;

// Image information structure
typedef struct
{
    FILE    *file;
    uint8_t  major_version;
    uint8_t  minor_version;
    uint64_t index_offset;
    uint64_t block_count;
    uint64_t total_uncompressed_size;
    void    *index_entries;  // Array of IndexEntry
} image_info;

// Benchmark result structure
typedef struct
{
    uint64_t compressed_size;
    uint64_t elapsed_ns;
} benchmark_result;

// Progress tracking structure
typedef struct
{
    uint64_t current;
    uint64_t total;
    char     label[256];
} progress_state;

// Progress callback type
typedef void (*progress_callback)(const progress_state *state);

// Function declarations
int  open_image(const char *path, image_info *info);
void close_image(image_info *info);
int  benchmark_compression(const char *input_path, const char *output_path, compression_algorithm algorithm,
                           image_info *info, benchmark_result *result, progress_state *progress,
                           progress_callback progress_cb, const zstd_dict_context *dict_ctx);

#endif  // BENCHMARK_H
