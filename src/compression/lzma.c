/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2025 Natalia Portillo.
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

#include "aaruformat.h"

#include "../../3rdparty/lzma-21.03beta/C/LzmaLib.h"

/**
 * @brief Decodes an LZMA-compressed buffer.
 *
 * Decompresses data from the source buffer into the destination buffer using LZMA.
 *
 * @param dst_buffer Pointer to the destination buffer.
 * @param dst_size Pointer to the size of the destination buffer; updated with the actual size.
 * @param src_buffer Pointer to the source (compressed) buffer.
 * @param src_len Pointer to the size of the source buffer; updated with the actual size read.
 * @param props Pointer to the LZMA properties.
 * @param props_size Size of the LZMA properties.
 * @return 0 on success, or an error code on failure.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_lzma_decode_buffer(uint8_t *dst_buffer, size_t *dst_size, const uint8_t *src_buffer,
                                                       size_t *src_len, const uint8_t *props, const size_t props_size)
{
    return LzmaUncompress(dst_buffer, dst_size, src_buffer, src_len, props, props_size);
}

/**
 * @brief Encodes a buffer using LZMA compression.
 *
 * Compresses data from the source buffer into the destination buffer using LZMA.
 *
 * @param dst_buffer Pointer to the destination buffer.
 * @param dst_size Pointer to the size of the destination buffer; updated with the actual size.
 * @param src_buffer Pointer to the source (uncompressed) buffer.
 * @param src_len Size of the source buffer.
 * @param out_props Pointer to the output LZMA properties.
 * @param out_props_size Pointer to the size of the output LZMA properties.
 * @param level Compression level.
 * @param dict_size Dictionary size.
 * @param lc LZMA literal context bits.
 * @param lp LZMA literal position bits.
 * @param pb LZMA position bits.
 * @param fb Number of fast bytes.
 * @param num_threads Number of threads to use.
 * @return 0 on success, or an error code on failure.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_lzma_encode_buffer(uint8_t *dst_buffer, size_t *dst_size, const uint8_t *src_buffer,
                                                       const size_t src_len, uint8_t *out_props, size_t *out_props_size,
                                                       const int32_t level, const uint32_t dict_size, const int32_t lc,
                                                       const int32_t lp, const int32_t pb, const int32_t fb,
                                                       const int32_t num_threads)
{
    return LzmaCompress(dst_buffer, dst_size, src_buffer, src_len, out_props, out_props_size, level, dict_size, lc, lp,
                        pb, fb, num_threads);
}
