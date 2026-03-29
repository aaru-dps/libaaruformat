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

#include <stddef.h>
#include <stdint.h>

#include <aaruformat.h>
#include <zstd.h>

/**
 * @brief Decodes a Zstandard-compressed buffer.
 *
 * @param dst_buffer Pointer to the destination buffer.
 * @param dst_size Size of the destination buffer.
 * @param src_buffer Pointer to the source (compressed) buffer.
 * @param src_size Size of the source buffer.
 * @return Number of decompressed bytes, or 0 on error.
 */
AARU_EXPORT size_t AARU_CALL aaruf_zstd_decode_buffer(uint8_t *dst_buffer, size_t dst_size, const uint8_t *src_buffer,
                                                       size_t src_size)
{
    size_t result = ZSTD_decompress(dst_buffer, dst_size, src_buffer, src_size);

    if(ZSTD_isError(result)) return 0;

    return result;
}

/**
 * @brief Encodes a buffer using Zstandard compression.
 *
 * Uses the advanced API (ZSTD_CCtx) to support multi-threaded compression
 * when num_threads > 1. With num_threads <= 1 the output is bit-identical
 * to the simple ZSTD_compress() API.
 *
 * @param dst_buffer Pointer to the destination buffer.
 * @param dst_size Size of the destination buffer.
 * @param src_buffer Pointer to the source (uncompressed) buffer.
 * @param src_size Size of the source buffer.
 * @param level Compression level (1-22).
 * @param num_threads Number of worker threads (1 = single-threaded).
 * @return Number of compressed bytes, or 0 on error.
 */
AARU_EXPORT size_t AARU_CALL aaruf_zstd_encode_buffer(uint8_t *dst_buffer, size_t dst_size, const uint8_t *src_buffer,
                                                       size_t src_size, int level, int num_threads)
{
    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    if(cctx == NULL) return 0;

    ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, level);

    if(num_threads > 1) ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, num_threads);

    size_t result = ZSTD_compress2(cctx, dst_buffer, dst_size, src_buffer, src_size);

    ZSTD_freeCCtx(cctx);

    if(ZSTD_isError(result)) return 0;

    return result;
}
