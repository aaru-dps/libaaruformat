// /***************************************************************************
// Aaru Data Preservation Suite
// ----------------------------------------------------------------------------
//
// Filename       : crc64.c
// Author(s)      : Natalia Portillo <claunia@claunia.com>
//
// Component      : libaaruformat.
//
// --[ Description ] ----------------------------------------------------------
//
//     Calculates CRC64-ECMA checksums using VMULL.
//
// --[ License ] --------------------------------------------------------------
//
//     This library is free software; you can redistribute it and/or modify
//     it under the terms of the GNU Lesser General Public License as
//     published by the Free Software Foundation; either version 2.1 of the
//     License, or (at your option) any later version.
//
//     This library is distributed in the hope that it will be useful, but
//     WITHOUT ANY WARRANTY; without even the implied warranty of
//     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
//     Lesser General Public License for more details.
//
//     You should have received a copy of the GNU Lesser General Public
//     License along with this library; if not, see <http://www.gnu.org/licenses/>.
//
// ----------------------------------------------------------------------------
// Copyright © 2011-2022 Natalia Portillo
// ****************************************************************************/

#if defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) || defined(_M_ARM)

#include <arm_neon.h>
#include <stddef.h>
#include <stdint.h>

#include <aaruformat.h>

#include "arm_vmull.h"

static const uint8_t shuffleMasks[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x8f, 0x8e, 0x8d, 0x8c, 0x8b, 0x8a, 0x89, 0x88, 0x87, 0x86, 0x85, 0x84, 0x83, 0x82, 0x81, 0x80,
};

TARGET_WITH_SIMD FORCE_INLINE void shiftRight128(uint64x2_t in, size_t n, uint64x2_t* outLeft, uint64x2_t* outRight)
{
    const uint64x2_t maskA =
        vreinterpretq_u64_u32(vld1q_u32((const uint32_t*)(const uint64x2_t*)(shuffleMasks + (16 - n))));
    uint64x2_t       b     = vreinterpretq_u64_u8(vceqq_u8(vreinterpretq_u8_u64(vreinterpretq_u64_u32(vdupq_n_u32(0))),
                                                 vreinterpretq_u8_u64(vreinterpretq_u64_u32(vdupq_n_u32(0)))));
    const uint64x2_t maskB = vreinterpretq_u64_u32(veorq_u32(vreinterpretq_u32_u64(maskA), vreinterpretq_u32_u64(b)));

    *outLeft  = mm_shuffle_epi8(in, maskB);
    *outRight = mm_shuffle_epi8(in, maskA);
}

TARGET_WITH_SIMD FORCE_INLINE uint64x2_t fold(uint64x2_t in, uint64x2_t foldConstants)
{
    return veorq_u64(sse2neon_vmull_p64(vget_low_u64(in), vget_low_u64(foldConstants)),
                     sse2neon_vmull_p64(vget_high_u64(in), vget_high_u64(foldConstants)));
}

AARU_EXPORT TARGET_WITH_SIMD uint64_t AARU_CALL aaruf_crc64_vmull(uint64_t previous_crc, const uint8_t* data, long len)
{
    const uint64_t k1 = 0xe05dd497ca393ae4; // bitReflect(expMod65(128 + 64, poly, 1)) << 1;
    const uint64_t k2 = 0xdabe95afc7875f40; // bitReflect(expMod65(128, poly, 1)) << 1;
    const uint64_t mu = 0x9c3e466c172963d5; // (bitReflect(div129by65(poly)) << 1) | 1;
    const uint64_t p  = 0x92d8af2baf0e1e85; // (bitReflect(poly) << 1) | 1;

    const uint64x2_t foldConstants1 = vcombine_u64(vcreate_u64(k1), vcreate_u64(k2));
    const uint64x2_t foldConstants2 = vcombine_u64(vcreate_u64(mu), vcreate_u64(p));

    const uint8_t* end = data + len;

    // Align pointers
    const uint64x2_t* alignedData = (const uint64x2_t*)((uintptr_t)data & ~(uintptr_t)15);
    const uint64x2_t* alignedEnd  = (const uint64x2_t*)(((uintptr_t)end + 15) & ~(uintptr_t)15);

    const size_t leadInSize  = data - (const uint8_t*)alignedData;
    const size_t leadOutSize = (const uint8_t*)alignedEnd - end;

    const size_t alignedLength = alignedEnd - alignedData;

    const uint64x2_t leadInMask =
        vreinterpretq_u64_u32(vld1q_u32((const uint32_t*)(const uint64x2_t*)(shuffleMasks + (16 - leadInSize))));
    uint64x2_t a = vreinterpretq_u64_u32(vdupq_n_u32(0));
    uint64x2_t b = vreinterpretq_u64_u32(
        vld1q_u32((const uint32_t*)alignedData)); // Use a signed shift right to create a mask with the sign bit
    const uint64x2_t data0 =
        vreinterpretq_u64_u8(vbslq_u8(vreinterpretq_u8_s8(vshrq_n_s8(vreinterpretq_s8_u64(leadInMask), 7)),
                                      vreinterpretq_u8_u64(b),
                                      vreinterpretq_u8_u64(a)));

    const uint64x2_t initialCrc = vsetq_lane_u64(~previous_crc, vdupq_n_u64(0), 0);

    uint64x2_t R;
    if(alignedLength == 1)
    {
        // Single data block, initial CRC possibly bleeds into zero padding
        uint64x2_t crc0, crc1;
        shiftRight128(initialCrc, 16 - len, &crc0, &crc1);

        uint64x2_t A, B;
        shiftRight128(data0, leadOutSize, &A, &B);

        const uint64x2_t P = veorq_u64(A, crc0);
        R                  = veorq_u64(sse2neon_vmull_p64(vget_low_u64(P), vget_high_u64(foldConstants1)),
                      veorq_u64(mm_srli_si128(P, 8), mm_slli_si128(crc1, 8)));
    }
    else if(alignedLength == 2)
    {
        const uint64x2_t data1 = vreinterpretq_u64_u32(vld1q_u32((const uint32_t*)(alignedData + 1)));

        if(len < 8)
        {
            // Initial CRC bleeds into the zero padding
            uint64x2_t crc0, crc1;
            shiftRight128(initialCrc, 16 - len, &crc0, &crc1);

            uint64x2_t A, B, C, D;
            shiftRight128(data0, leadOutSize, &A, &B);
            shiftRight128(data1, leadOutSize, &C, &D);

            const uint64x2_t P = veorq_u64(veorq_u64(B, C), crc0);
            R                  = veorq_u64(sse2neon_vmull_p64(vget_low_u64(P), vget_high_u64(foldConstants1)),
                          veorq_u64(mm_srli_si128(P, 8), mm_slli_si128(crc1, 8)));
        }
        else
        {
            // We can fit the initial CRC into the data without bleeding into the zero padding
            uint64x2_t crc0, crc1;
            shiftRight128(initialCrc, leadInSize, &crc0, &crc1);

            uint64x2_t A, B, C, D;
            shiftRight128(veorq_u64(data0, crc0), leadOutSize, &A, &B);
            shiftRight128(veorq_u64(data1, crc1), leadOutSize, &C, &D);

            const uint64x2_t P = veorq_u64(fold(A, foldConstants1), veorq_u64(B, C));
            R = veorq_u64(sse2neon_vmull_p64(vget_low_u64(P), vget_high_u64(foldConstants1)), mm_srli_si128(P, 8));
        }
    }
    else
    {
        alignedData++;
        len -= 16 - leadInSize;

        // Initial CRC can simply be added to data
        uint64x2_t crc0, crc1;
        shiftRight128(initialCrc, leadInSize, &crc0, &crc1);

        uint64x2_t accumulator = veorq_u64(fold(veorq_u64(crc0, data0), foldConstants1), crc1);

        while(len >= 32)
        {
            accumulator = fold(veorq_u64(vreinterpretq_u64_u32(vld1q_u32((const uint32_t*)alignedData)), accumulator),
                               foldConstants1);

            len -= 16;
            alignedData++;
        }

        uint64x2_t P;
        if(len == 16) P = veorq_u64(accumulator, vreinterpretq_u64_u32(vld1q_u32((const uint32_t*)alignedData)));
        else
        {
            const uint64x2_t end0 =
                veorq_u64(accumulator, vreinterpretq_u64_u32(vld1q_u32((const uint32_t*)alignedData)));
            const uint64x2_t end1 = vreinterpretq_u64_u32(vld1q_u32((const uint32_t*)(alignedData + 1)));

            uint64x2_t A, B, C, D;
            shiftRight128(end0, leadOutSize, &A, &B);
            shiftRight128(end1, leadOutSize, &C, &D);

            P = veorq_u64(fold(A, foldConstants1),
                          vreinterpretq_u64_u32(vorrq_u32(vreinterpretq_u32_u64(B), vreinterpretq_u32_u64(C))));
        }

        R = veorq_u64(sse2neon_vmull_p64(vget_low_u64(P), vget_high_u64(foldConstants1)), mm_srli_si128(P, 8));
    }

    // Final Barrett reduction
    const uint64x2_t T1 = sse2neon_vmull_p64(vget_low_u64(R), vget_low_u64(foldConstants2));
    const uint64x2_t T2 = veorq_u64(
        veorq_u64(sse2neon_vmull_p64(vget_low_u64(T1), vget_high_u64(foldConstants2)), mm_slli_si128(T1, 8)), R);

    return ~vgetq_lane_u64(T2, 1);
}

#endif