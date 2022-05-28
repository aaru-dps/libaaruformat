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
//     Calculates CRC64-ECMA checksums using CLMUL.
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

#if defined(__x86_64__) || defined(__amd64) || defined(_M_AMD64) || defined(_M_X64) || defined(__I386__) ||            \
    defined(__i386__) || defined(__THW_INTEL) || defined(_M_IX86)

#include <inttypes.h>
#include <smmintrin.h>
#include <wmmintrin.h>

#ifdef _MSC_VER
#include <intrin.h>
#endif

#include <aaruformat.h>

// Reverses bits
static uint64_t bitReflect(uint64_t v)
{
    v = ((v >> 1) & 0x5555555555555555) | ((v & 0x5555555555555555) << 1);
    v = ((v >> 2) & 0x3333333333333333) | ((v & 0x3333333333333333) << 2);
    v = ((v >> 4) & 0x0F0F0F0F0F0F0F0F) | ((v & 0x0F0F0F0F0F0F0F0F) << 4);
    v = ((v >> 8) & 0x00FF00FF00FF00FF) | ((v & 0x00FF00FF00FF00FF) << 8);
    v = ((v >> 16) & 0x0000FFFF0000FFFF) | ((v & 0x0000FFFF0000FFFF) << 16);
    v = (v >> 32) | (v << 32);
    return v;
}

// Computes r*x^N mod p(x)
static uint64_t expMod65(uint32_t n, uint64_t p, uint64_t r)
{
    return n == 0 ? r : expMod65(n - 1, p, (r << 1) ^ (p & ((int64_t)r >> 63)));
}

// Computes x^129 / p(x); the result has an implicit 65th bit.
static uint64_t div129by65(uint64_t poly)
{
    uint64_t q = 0;
    uint64_t h = poly;
    uint32_t i;
    for(i = 0; i < 64; ++i)
    {
        q |= (h & (1ull << 63)) >> i;
        h = (h << 1) ^ (poly & ((int64_t)h >> 63));
    }
    return q;
}

static const uint8_t shuffleMasks[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x8f, 0x8e, 0x8d, 0x8c, 0x8b, 0x8a, 0x89, 0x88, 0x87, 0x86, 0x85, 0x84, 0x83, 0x82, 0x81, 0x80,
};

CLMUL static void shiftRight128(__m128i in, size_t n, __m128i* outLeft, __m128i* outRight)
{
    const __m128i maskA = _mm_loadu_si128((const __m128i*)(shuffleMasks + (16 - n)));
    const __m128i maskB = _mm_xor_si128(maskA, _mm_cmpeq_epi8(_mm_setzero_si128(), _mm_setzero_si128()));

    *outLeft  = _mm_shuffle_epi8(in, maskB);
    *outRight = _mm_shuffle_epi8(in, maskA);
}

CLMUL static __m128i fold(__m128i in, __m128i foldConstants)
{
    return _mm_xor_si128(_mm_clmulepi64_si128(in, foldConstants, 0x00), _mm_clmulepi64_si128(in, foldConstants, 0x11));
}

AARU_EXPORT CLMUL uint64_t AARU_CALL aaruf_crc64_clmul(uint64_t crc, const uint8_t* data, long length)
{
    const uint64_t k1 = 0xe05dd497ca393ae4; // bitReflect(expMod65(128 + 64, poly, 1)) << 1;
    const uint64_t k2 = 0xdabe95afc7875f40; // bitReflect(expMod65(128, poly, 1)) << 1;
    const uint64_t mu = 0x9c3e466c172963d5; // (bitReflect(div129by65(poly)) << 1) | 1;
    const uint64_t p  = 0x92d8af2baf0e1e85; // (bitReflect(poly) << 1) | 1;

    const __m128i foldConstants1 = _mm_set_epi64x(k2, k1);
    const __m128i foldConstants2 = _mm_set_epi64x(p, mu);

    const uint8_t* end = data + length;

    // Align pointers
    const __m128i* alignedData = (const __m128i*)((uintptr_t)data & ~(uintptr_t)15);
    const __m128i* alignedEnd  = (const __m128i*)(((uintptr_t)end + 15) & ~(uintptr_t)15);

    const size_t leadInSize  = data - (const uint8_t*)alignedData;
    const size_t leadOutSize = (const uint8_t*)alignedEnd - end;

    const size_t alignedLength = alignedEnd - alignedData;

    const __m128i leadInMask = _mm_loadu_si128((const __m128i*)(shuffleMasks + (16 - leadInSize)));
    const __m128i data0      = _mm_blendv_epi8(_mm_setzero_si128(), _mm_load_si128(alignedData), leadInMask);

#if defined(_WIN64)
    const __m128i initialCrc = _mm_cvtsi64x_si128(~crc);
#else
    const __m128i initialCrc = _mm_set_epi64x(0, ~crc);
#endif

    __m128i R;
    if(alignedLength == 1)
    {
        // Single data block, initial CRC possibly bleeds into zero padding
        __m128i crc0, crc1;
        shiftRight128(initialCrc, 16 - length, &crc0, &crc1);

        __m128i A, B;
        shiftRight128(data0, leadOutSize, &A, &B);

        const __m128i P = _mm_xor_si128(A, crc0);
        R               = _mm_xor_si128(_mm_clmulepi64_si128(P, foldConstants1, 0x10),
                          _mm_xor_si128(_mm_srli_si128(P, 8), _mm_slli_si128(crc1, 8)));
    }
    else if(alignedLength == 2)
    {
        const __m128i data1 = _mm_load_si128(alignedData + 1);

        if(length < 8)
        {
            // Initial CRC bleeds into the zero padding
            __m128i crc0, crc1;
            shiftRight128(initialCrc, 16 - length, &crc0, &crc1);

            __m128i A, B, C, D;
            shiftRight128(data0, leadOutSize, &A, &B);
            shiftRight128(data1, leadOutSize, &C, &D);

            const __m128i P = _mm_xor_si128(_mm_xor_si128(B, C), crc0);
            R               = _mm_xor_si128(_mm_clmulepi64_si128(P, foldConstants1, 0x10),
                              _mm_xor_si128(_mm_srli_si128(P, 8), _mm_slli_si128(crc1, 8)));
        }
        else
        {
            // We can fit the initial CRC into the data without bleeding into the zero padding
            __m128i crc0, crc1;
            shiftRight128(initialCrc, leadInSize, &crc0, &crc1);

            __m128i A, B, C, D;
            shiftRight128(_mm_xor_si128(data0, crc0), leadOutSize, &A, &B);
            shiftRight128(_mm_xor_si128(data1, crc1), leadOutSize, &C, &D);

            const __m128i P = _mm_xor_si128(fold(A, foldConstants1), _mm_xor_si128(B, C));
            R               = _mm_xor_si128(_mm_clmulepi64_si128(P, foldConstants1, 0x10), _mm_srli_si128(P, 8));
        }
    }
    else
    {
        alignedData++;
        length -= 16 - leadInSize;

        // Initial CRC can simply be added to data
        __m128i crc0, crc1;
        shiftRight128(initialCrc, leadInSize, &crc0, &crc1);

        __m128i accumulator = _mm_xor_si128(fold(_mm_xor_si128(crc0, data0), foldConstants1), crc1);

        while(length >= 32)
        {
            accumulator = fold(_mm_xor_si128(_mm_load_si128(alignedData), accumulator), foldConstants1);

            length -= 16;
            alignedData++;
        }

        __m128i P;
        if(length == 16) { P = _mm_xor_si128(accumulator, _mm_load_si128(alignedData)); }
        else
        {
            const __m128i end0 = _mm_xor_si128(accumulator, _mm_load_si128(alignedData));
            const __m128i end1 = _mm_load_si128(alignedData + 1);

            __m128i A, B, C, D;
            shiftRight128(end0, leadOutSize, &A, &B);
            shiftRight128(end1, leadOutSize, &C, &D);

            P = _mm_xor_si128(fold(A, foldConstants1), _mm_or_si128(B, C));
        }

        R = _mm_xor_si128(_mm_clmulepi64_si128(P, foldConstants1, 0x10), _mm_srli_si128(P, 8));
    }

    // Final Barrett reduction
    const __m128i T1 = _mm_clmulepi64_si128(R, foldConstants2, 0x00);
    const __m128i T2 =
        _mm_xor_si128(_mm_xor_si128(_mm_clmulepi64_si128(T1, foldConstants2, 0x10), _mm_slli_si128(T1, 8)), R);

#if defined(_WIN64)
    return ~_mm_extract_epi64(T2, 1);
#else
    return ~(((uint64_t)(uint32_t)_mm_extract_epi32(T2, 3) << 32) | (uint64_t)(uint32_t)_mm_extract_epi32(T2, 2));
#endif
}

#endif