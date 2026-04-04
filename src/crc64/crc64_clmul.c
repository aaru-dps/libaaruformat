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

#if defined(__x86_64__) || defined(__amd64) || defined(_M_AMD64) || defined(_M_X64) || defined(__I386__) || \
    defined(__i386__) || defined(__THW_INTEL) || defined(_M_IX86)

#include <inttypes.h>
#include <smmintrin.h>
#include <string.h>
#include <wmmintrin.h>

#include "log.h"

#ifdef _MSC_VER
#include <intrin.h>
#define AARU_ALIGN16 __declspec(align(16))
#else
#define AARU_ALIGN16 __attribute__((aligned(16)))
#endif

#include <aaruformat.h>

// Reverses bits
static uint64_t bitReflect(uint64_t v)
{
    v = v >> 1 & 0x5555555555555555 | (v & 0x5555555555555555) << 1;
    v = v >> 2 & 0x3333333333333333 | (v & 0x3333333333333333) << 2;
    v = v >> 4 & 0x0F0F0F0F0F0F0F0F | (v & 0x0F0F0F0F0F0F0F0F) << 4;
    v = v >> 8 & 0x00FF00FF00FF00FF | (v & 0x00FF00FF00FF00FF) << 8;
    v = v >> 16 & 0x0000FFFF0000FFFF | (v & 0x0000FFFF0000FFFF) << 16;
    v = v >> 32 | v << 32;
    return v;
}

// Computes r*x^N mod p(x)
static uint64_t expMod65(uint32_t n, uint64_t p, uint64_t r)
{
    return n == 0 ? r : expMod65(n - 1, p, r << 1 ^ p & (int64_t)r >> 63);
}

// Computes x^129 / p(x); the result has an implicit 65th bit.
static uint64_t div129by65(uint64_t poly)
{
    uint64_t q = 0;
    uint64_t h = poly;
    for(uint32_t i = 0; i < 64; ++i)
    {
        q |= (h & 1ull << 63) >> i;
        h = h << 1 ^ poly & (int64_t)h >> 63;
    }
    return q;
}

static const uint8_t shuffleMasks[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x8f, 0x8e, 0x8d, 0x8c, 0x8b, 0x8a, 0x89, 0x88, 0x87, 0x86, 0x85, 0x84, 0x83, 0x82, 0x81, 0x80,
};

CLMUL static void shiftRight128(__m128i in, size_t n, __m128i *out_left, __m128i *out_right)
{
    const __m128i mask_a = _mm_loadu_si128((const __m128i *)(shuffleMasks + (16 - n)));
    const __m128i mask_b = _mm_xor_si128(mask_a, _mm_cmpeq_epi8(_mm_setzero_si128(), _mm_setzero_si128()));

    *out_left  = _mm_shuffle_epi8(in, mask_b);
    *out_right = _mm_shuffle_epi8(in, mask_a);
}

CLMUL static __m128i fold(__m128i in, __m128i fold_constants)
{
    return _mm_xor_si128(_mm_clmulepi64_si128(in, fold_constants, 0x00),
                         _mm_clmulepi64_si128(in, fold_constants, 0x11));
}

/**
 * @brief Computes CRC64 using the CLMUL (carry-less multiplication) instruction set.
 *
 * @param crc Initial CRC64 value.
 * @param data Pointer to the data buffer.
 * @param length Length of the data buffer in bytes.
 * @return Computed CRC64 value.
 */
AARU_EXPORT CLMUL uint64_t AARU_CALL aaruf_crc64_clmul(const uint64_t crc, const uint8_t *data, long length)
{
    TRACE("Entering aaruf_crc64_clmul(%" PRIu64 ", %p, %ld)", crc, data, length);

    const uint64_t k1 = 0xe05dd497ca393ae4;  // bitReflect(expMod65(128 + 64, poly, 1)) << 1;
    const uint64_t k2 = 0xdabe95afc7875f40;  // bitReflect(expMod65(128, poly, 1)) << 1;
    const uint64_t mu = 0x9c3e466c172963d5;  // (bitReflect(div129by65(poly)) << 1) | 1;
    const uint64_t p  = 0x92d8af2baf0e1e85;  // (bitReflect(poly) << 1) | 1;

    const __m128i fold_constants_1 = _mm_set_epi64x(k2, k1);
    const __m128i fold_constants_2 = _mm_set_epi64x(p, mu);

    const uint8_t *end = data + length;

    // Align pointers
    const __m128i *aligned_data = (const __m128i *)((uintptr_t)data & ~(uintptr_t)15);
    const __m128i *aligned_end  = (const __m128i *)((uintptr_t)end + 15 & ~(uintptr_t)15);

    const size_t lead_in_size  = data - (const uint8_t *)aligned_data;
    const size_t lead_out_size = (const uint8_t *)aligned_end - end;

    const size_t aligned_length = aligned_end - aligned_data;

    const __m128i lead_in_mask = _mm_loadu_si128((const __m128i *)(shuffleMasks + (16 - lead_in_size)));
    const __m128i data0        = _mm_blendv_epi8(_mm_setzero_si128(), _mm_load_si128(aligned_data), lead_in_mask);

#if defined(_WIN64)
    const __m128i initial_crc = _mm_cvtsi64x_si128(~crc);
#else
    const __m128i initial_crc = _mm_set_epi64x(0, ~crc);
#endif

    __m128i r_reg;
    if(aligned_length == 1)
    {
        // Single data block, initial CRC possibly bleeds into zero padding
        __m128i crc0, crc1;
        shiftRight128(initial_crc, 16 - length, &crc0, &crc1);

        __m128i a_reg, b_reg;
        shiftRight128(data0, lead_out_size, &a_reg, &b_reg);

        const __m128i p_reg = _mm_xor_si128(a_reg, crc0);
        r_reg               = _mm_xor_si128(_mm_clmulepi64_si128(p_reg, fold_constants_1, 0x10),
                                            _mm_xor_si128(_mm_srli_si128(p_reg, 8), _mm_slli_si128(crc1, 8)));
    }
    else if(aligned_length == 2)
    {
        const __m128i data1 = _mm_load_si128(aligned_data + 1);

        if(length < 8)
        {
            // Initial CRC bleeds into the zero padding
            __m128i crc0, crc1;
            shiftRight128(initial_crc, 16 - length, &crc0, &crc1);

            __m128i a_reg, b_reg, c_reg, d_reg;
            shiftRight128(data0, lead_out_size, &a_reg, &b_reg);
            shiftRight128(data1, lead_out_size, &c_reg, &d_reg);

            const __m128i p_reg = _mm_xor_si128(_mm_xor_si128(b_reg, c_reg), crc0);
            r_reg               = _mm_xor_si128(_mm_clmulepi64_si128(p_reg, fold_constants_1, 0x10),
                                                _mm_xor_si128(_mm_srli_si128(p_reg, 8), _mm_slli_si128(crc1, 8)));
        }
        else
        {
            // We can fit the initial CRC into the data without bleeding into the zero padding
            __m128i crc0, crc1;
            shiftRight128(initial_crc, lead_in_size, &crc0, &crc1);

            __m128i a_reg, b_reg, c_reg, d_reg;
            shiftRight128(_mm_xor_si128(data0, crc0), lead_out_size, &a_reg, &b_reg);
            shiftRight128(_mm_xor_si128(data1, crc1), lead_out_size, &c_reg, &d_reg);

            const __m128i p_reg = _mm_xor_si128(fold(a_reg, fold_constants_1), _mm_xor_si128(b_reg, c_reg));
            r_reg = _mm_xor_si128(_mm_clmulepi64_si128(p_reg, fold_constants_1, 0x10), _mm_srli_si128(p_reg, 8));
        }
    }
    else
    {
        aligned_data++;
        length -= 16 - lead_in_size;

        // Initial CRC can simply be added to data
        __m128i crc0, crc1;
        shiftRight128(initial_crc, lead_in_size, &crc0, &crc1);

        __m128i accumulator = _mm_xor_si128(fold(_mm_xor_si128(crc0, data0), fold_constants_1), crc1);

        while(length >= 32)
        {
            accumulator = fold(_mm_xor_si128(_mm_load_si128(aligned_data), accumulator), fold_constants_1);

            length -= 16;
            aligned_data++;
        }

        __m128i p_reg;
        if(length == 16) { p_reg = _mm_xor_si128(accumulator, _mm_load_si128(aligned_data)); }
        else
        {
            const __m128i end0 = _mm_xor_si128(accumulator, _mm_load_si128(aligned_data));

            // For the second block, safely handle the case where it extends past the actual data
            // Always use safe copy approach to avoid ASan buffer overflow detection
            AARU_ALIGN16 uint8_t temp[16] = {0};
            const uint8_t *next_block_addr = (const uint8_t *)(aligned_data + 1);

            // Only copy bytes that are actually within the original buffer
            if(next_block_addr < end)
            {
                size_t available = (size_t)(end - next_block_addr);
                if(available > 16) available = 16;
                memcpy(temp, next_block_addr, available);
            }

            const __m128i end1 = _mm_load_si128((const __m128i *)temp);

            __m128i a_reg, b_reg, c_reg, d_reg;
            shiftRight128(end0, lead_out_size, &a_reg, &b_reg);
            shiftRight128(end1, lead_out_size, &c_reg, &d_reg);

            p_reg = _mm_xor_si128(fold(a_reg, fold_constants_1), _mm_or_si128(b_reg, c_reg));
        }

        r_reg = _mm_xor_si128(_mm_clmulepi64_si128(p_reg, fold_constants_1, 0x10), _mm_srli_si128(p_reg, 8));
    }

    // Final Barrett reduction
    const __m128i t1_reg = _mm_clmulepi64_si128(r_reg, fold_constants_2, 0x00);
    const __m128i t2_reg = _mm_xor_si128(
        _mm_xor_si128(_mm_clmulepi64_si128(t1_reg, fold_constants_2, 0x10), _mm_slli_si128(t1_reg, 8)), r_reg);

    TRACE("Exiting aaruf_crc64_clmul()");

#if defined(_WIN64)
    return ~_mm_extract_epi64(t2_reg, 1);
#else
    return ~((uint64_t)(uint32_t)_mm_extract_epi32(t2_reg, 3) << 32 | (uint64_t)(uint32_t)_mm_extract_epi32(t2_reg, 2));
#endif
}

#endif
