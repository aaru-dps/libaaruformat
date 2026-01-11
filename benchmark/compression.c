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

#include "compression.h"
#include <aaruformat/consts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_ZSTD
#include <zdict.h>
#include <zstd.h>
#endif

#ifdef HAVE_BZ3
#include <libbz3.h>
#endif

// LZMA compression from library
extern int32_t aaruf_lzma_encode_buffer(uint8_t *dst_buffer, size_t *dst_size, const uint8_t *src_buffer,
                                        size_t src_len, uint8_t *out_props, size_t *out_props_size, int32_t level,
                                        uint32_t dict_size, int32_t lc, int32_t lp, int32_t pb, int32_t fb,
                                        int32_t num_threads);

// Compress data using LZMA
static int compress_lzma(const uint8_t *input, const size_t input_size, uint8_t **output, size_t *output_size)
{
    // Allocate output buffer (input size * 2 + margin)
    const size_t max_output_size = input_size * 2 + 65536;
    uint8_t     *buffer          = malloc(max_output_size);
    if(buffer == NULL) return -1;

    uint8_t props[LZMA_PROPERTIES_LENGTH];
    size_t  props_size = LZMA_PROPERTIES_LENGTH;
    size_t  cmp_size   = max_output_size;

    // Compress (level 9, 1MB dictionary, standard parameters)
    if(aaruf_lzma_encode_buffer(buffer, &cmp_size, input, input_size, props, &props_size, 9, 33554432, 4, 0, 2, 273,
                                8) != 0)
    {
        free(buffer);
        return -1;
    }

    // Allocate final buffer with properties prepended
    *output_size = cmp_size + LZMA_PROPERTIES_LENGTH;
    *output      = malloc(*output_size);
    if(*output == NULL)
    {
        free(buffer);
        return -1;
    }

    memcpy(*output, props, LZMA_PROPERTIES_LENGTH);
    memcpy(*output + LZMA_PROPERTIES_LENGTH, buffer, cmp_size);

    free(buffer);
    return 0;
}

// Compress data using Bzip3
static int compress_bzip3(const uint8_t *input, const size_t input_size, uint8_t **output, size_t *output_size)
{
#ifdef HAVE_BZ3
    // Determine block size (16MiB for best compression)
    const int32_t block_size = 16 * 1024 * 1024;

    // Create bzip3 state
    struct bz3_state *state = bz3_new(block_size);
    if(state == NULL) return -1;

    // Calculate max output size - bz3_encode_block compresses in-place
    // so we need a buffer that can hold the original data initially
    const size_t max_output_size = bz3_bound(input_size);
    *output                      = malloc(max_output_size);
    if(*output == NULL)
    {
        bz3_free(state);
        return -1;
    }

    // Copy input to output buffer (bz3_encode_block works in-place)
    memcpy(*output, input, input_size);

    // Compress in-place (returns compressed size or negative on error)
    const int32_t result = bz3_encode_block(state, *output, input_size);
    bz3_free(state);

    if(result < 0)
    {
        free(*output);
        *output = NULL;
        return -1;
    }

    *output_size = result;
    return 0;
#else
    // Bzip3 not available
    (void)input;
    (void)input_size;
    *output      = NULL;
    *output_size = 0;
    return -1;
#endif
}

// Compress data using Zstd
static int compress_zstd(const uint8_t *input, const size_t input_size, uint8_t **output, size_t *output_size)
{
#ifdef HAVE_ZSTD
    // Calculate max output size
    const size_t max_output_size = ZSTD_compressBound(input_size);
    *output                      = malloc(max_output_size);
    if(*output == NULL) return -1;

    // Compress with level 19 (max compression)
    const size_t result = ZSTD_compress(*output, max_output_size, input, input_size, 19);

    if(ZSTD_isError(result))
    {
        free(*output);
        *output = NULL;
        return -1;
    }

    *output_size = result;
    return 0;
#else
    // Zstd not available
    (void)input;
    (void)input_size;
    *output      = NULL;
    *output_size = 0;
    return -1;
#endif
}

// Main compression function
int compress_data(const compression_algorithm algorithm, const uint8_t *input, const size_t input_size,
                  uint8_t **output, size_t *output_size)
{
    switch(algorithm)
    {
        case COMP_LZMA:
            return compress_lzma(input, input_size, output, output_size);
        case COMP_BZIP3:
            return compress_bzip3(input, input_size, output, output_size);
        case COMP_ZSTD:
            return compress_zstd(input, input_size, output, output_size);
        default:
            return -1;
    }
}

// Get compression type for header
int get_compression_type(const compression_algorithm algorithm)
{
    switch(algorithm)
    {
        case COMP_LZMA:
            return 1;  // LZMA
        case COMP_BZIP3:
            return 100;  // Custom identifier for bzip3
        case COMP_ZSTD:
            return 101;  // Custom identifier for zstd
        default:
            return 0;  // None
    }
}

// Train a Zstd dictionary from samples
zstd_dict_context *train_zstd_dictionary(const uint8_t *sample_data, size_t sample_size, size_t dict_size)
{
#ifdef HAVE_ZSTD
    if(sample_data == NULL || sample_size == 0) return NULL;

    zstd_dict_context *ctx = malloc(sizeof(zstd_dict_context));
    if(ctx == NULL) return NULL;

    // Allocate dictionary buffer
    ctx->dict_data = malloc(dict_size);
    if(ctx->dict_data == NULL)
    {
        free(ctx);
        return NULL;
    }

    // Train dictionary using Zstd's ZDICT_trainFromBuffer
    // This analyzes the sample data and creates an optimized dictionary
    // We need to split the sample into multiple samples for proper training

    // Minimum sample size should be at least 100x the dictionary size for good training
    const size_t min_total_size = dict_size * 100;
    if(sample_size < min_total_size)
    {
        fprintf(stderr,
                "Warning: Sample size %zu too small for optimal dictionary training (recommended at least %zu)\n",
                sample_size, min_total_size);
    }

    // ZDICT has internal constraints on maximum sample size
    // Split large samples into chunks to work around this
    // Use reasonable chunk size (e.g., 2MB per sample)
    const size_t max_sample_size = 2 * 1024 * 1024;  // 2MB chunks
    const size_t num_samples     = (sample_size + max_sample_size - 1) / max_sample_size;

    // Allocate array for sample sizes
    size_t *sample_sizes = malloc(num_samples * sizeof(size_t));
    if(sample_sizes == NULL)
    {
        free(ctx->dict_data);
        free(ctx);
        return NULL;
    }

    // Calculate size for each sample
    size_t remaining = sample_size;
    for(size_t i = 0; i < num_samples; i++)
    {
        sample_sizes[i] = (remaining > max_sample_size) ? max_sample_size : remaining;
        remaining -= sample_sizes[i];
    }

    printf("Training dictionary with %zu samples (total %zu bytes, dict size %zu)\n", num_samples, sample_size,
           dict_size);

    // Use standard ZDICT_trainFromBuffer
    size_t trained_size = ZDICT_trainFromBuffer(ctx->dict_data, dict_size, sample_data, sample_sizes, num_samples);

    free(sample_sizes);

    if(ZDICT_isError(trained_size))
    {
        fprintf(stderr, "Dictionary training failed: %s\n", ZDICT_getErrorName(trained_size));
        free(ctx->dict_data);
        free(ctx);
        return NULL;
    }

    ctx->dict_size = trained_size;

    fprintf(stderr, "Dictionary training SUCCESS: trained_size=%zu (requested=%zu)\n", trained_size, dict_size);

    // Get dictionary ID
    ctx->dict_id = ZSTD_getDictID_fromDict(ctx->dict_data, ctx->dict_size);
    fprintf(stderr, "Dictionary ID: 0x%08X\n", ctx->dict_id);
    if(ctx->dict_id == 0)
    {
        fprintf(stderr, "Warning: Dictionary ID is 0, setting fallback\n");
        ctx->dict_id = 0x12345678;  // Fallback ID
    }

    return ctx;
#else
    (void)sample_data;
    (void)sample_size;
    (void)dict_size;
    return NULL;
#endif
}

// Free dictionary context
void free_zstd_dictionary(zstd_dict_context *dict_ctx)
{
    if(dict_ctx == NULL) return;
    if(dict_ctx->dict_data) free(dict_ctx->dict_data);
    free(dict_ctx);
}

// Compress data using Zstd with custom dictionary
int compress_data_zstd_dict(const uint8_t *input, size_t input_size, uint8_t **output, size_t *output_size,
                            const zstd_dict_context *dict_ctx)
{
#ifdef HAVE_ZSTD
    if(dict_ctx == NULL || dict_ctx->dict_data == NULL) return -1;

    // Calculate max output size
    const size_t max_output_size = ZSTD_compressBound(input_size);
    *output                      = malloc(max_output_size);
    if(*output == NULL) return -1;

    // Create compression context with dictionary
    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    if(cctx == NULL)
    {
        free(*output);
        *output = NULL;
        return -1;
    }

    // Use the simpler ZSTD_compress_usingDict which is optimized for dictionary compression
    // This is more efficient than ZSTD_compress2 with loadDictionary
    size_t result = ZSTD_compress_usingDict(cctx, *output, max_output_size, input, input_size, dict_ctx->dict_data,
                                            dict_ctx->dict_size,
                                            19);  // Compression level 19

    ZSTD_freeCCtx(cctx);

    if(ZSTD_isError(result))
    {
        free(*output);
        *output = NULL;
        return -1;
    }

    *output_size = result;
    return 0;
#else
    (void)input;
    (void)input_size;
    (void)output;
    (void)output_size;
    (void)dict_ctx;
    return -1;
#endif
}
