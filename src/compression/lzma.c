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
 * @param srcLen Pointer to the size of the source buffer; updated with the actual size read.
 * @param props Pointer to the LZMA properties.
 * @param propsSize Size of the LZMA properties.
 * @return 0 on success, or an error code on failure.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_lzma_decode_buffer(uint8_t *dst_buffer, size_t *dst_size, const uint8_t *src_buffer,
                                                       size_t *srcLen, const uint8_t *props, size_t propsSize)
{
    return LzmaUncompress(dst_buffer, dst_size, src_buffer, srcLen, props, propsSize);
}

/**
 * @brief Encodes a buffer using LZMA compression.
 *
 * Compresses data from the source buffer into the destination buffer using LZMA.
 *
 * @param dst_buffer Pointer to the destination buffer.
 * @param dst_size Pointer to the size of the destination buffer; updated with the actual size.
 * @param src_buffer Pointer to the source (uncompressed) buffer.
 * @param srcLen Size of the source buffer.
 * @param outProps Pointer to the output LZMA properties.
 * @param outPropsSize Pointer to the size of the output LZMA properties.
 * @param level Compression level.
 * @param dictSize Dictionary size.
 * @param lc LZMA literal context bits.
 * @param lp LZMA literal position bits.
 * @param pb LZMA position bits.
 * @param fb Number of fast bytes.
 * @param numThreads Number of threads to use.
 * @return 0 on success, or an error code on failure.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_lzma_encode_buffer(uint8_t *dst_buffer, size_t *dst_size, const uint8_t *src_buffer,
                                                       size_t srcLen, uint8_t *outProps, size_t *outPropsSize,
                                                       int32_t level, uint32_t dictSize, int32_t lc, int32_t lp,
                                                       int32_t pb, int32_t fb, int32_t numThreads)
{
    return LzmaCompress(dst_buffer, dst_size, src_buffer, srcLen, outProps, outPropsSize, level, dictSize, lc, lp, pb,
                        fb, numThreads);
}
