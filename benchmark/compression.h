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

#ifndef COMPRESSION_H
#define COMPRESSION_H

#include <stddef.h>
#include <stdint.h>
#include "benchmark.h"

// Compress data using specified algorithm
// Returns 0 on success, -1 on failure
// Caller must free output buffer
int compress_data(compression_algorithm algorithm, const uint8_t *input, size_t input_size, uint8_t **output,
                  size_t *output_size);

// Compress data using Zstd with custom dictionary
// Returns 0 on success, -1 on failure
int compress_data_zstd_dict(const uint8_t *input, size_t input_size, uint8_t **output, size_t *output_size,
                            const zstd_dict_context *dict_ctx);

// Train a Zstd dictionary from samples
// sample_data: concatenated uncompressed data from multiple blocks
// sample_size: total size of sample data
// dict_size: desired dictionary size (typically 16KB)
// Returns dictionary context on success, NULL on failure
zstd_dict_context *train_zstd_dictionary(const uint8_t *sample_data, size_t sample_size, size_t dict_size);

// Free dictionary context
void free_zstd_dictionary(zstd_dict_context *dict_ctx);

// Get compression type identifier for block header
int get_compression_type(compression_algorithm algorithm);

#endif  // COMPRESSION_H
