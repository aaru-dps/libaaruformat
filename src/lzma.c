/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2021 Natalia Portillo.
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

#include "../3rdparty/lzma-21.03beta/C/LzmaLib.h"

AARU_EXPORT int32_t AARU_CALL aaruf_lzma_decode_buffer(uint8_t*       dst_buffer,
                                                       size_t*        dst_size,
                                                       const uint8_t* src_buffer,
                                                       size_t*        srcLen,
                                                       const uint8_t* props,
                                                       size_t         propsSize)
{
    return LzmaUncompress(dst_buffer, dst_size, src_buffer, srcLen, props, propsSize);
}

AARU_EXPORT int32_t AARU_CALL aaruf_lzma_encode_buffer(uint8_t*       dst_buffer,
                                                       size_t*        dst_size,
                                                       const uint8_t* src_buffer,
                                                       size_t         srcLen,
                                                       uint8_t*       outProps,
                                                       size_t*        outPropsSize,
                                                       int32_t        level,
                                                       uint32_t       dictSize,
                                                       int32_t        lc,
                                                       int32_t        lp,
                                                       int32_t        pb,
                                                       int32_t        fb,
                                                       int32_t        numThreads)
{
    return LzmaCompress(
        dst_buffer, dst_size, src_buffer, srcLen, outProps, outPropsSize, level, dictSize, lc, lp, pb, fb, numThreads);
}
