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
 * @param dst_buffer Pointer to the destination buffer.
 * @param dst_size Size of the destination buffer.
 * @param src_buffer Pointer to the source (uncompressed) buffer.
 * @param src_size Size of the source buffer.
 * @param level Compression level (1-22).
 * @return Number of compressed bytes, or 0 on error.
 */
AARU_EXPORT size_t AARU_CALL aaruf_zstd_encode_buffer(uint8_t *dst_buffer, size_t dst_size, const uint8_t *src_buffer,
                                                       size_t src_size, int level)
{
    size_t result = ZSTD_compress(dst_buffer, dst_size, src_buffer, src_size, level);

    if(ZSTD_isError(result)) return 0;

    return result;
}
