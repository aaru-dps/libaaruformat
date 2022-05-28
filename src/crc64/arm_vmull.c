/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2022 Natalia Portillo.
 * Copyright sse2neon.h contributors
 *
 * sse2neon is freely redistributable under the MIT License.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#if defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) || defined(_M_ARM)

#include <arm_neon.h>

#include <aaruformat.h>

#include "arm_vmull.h"

#if !defined(__MINGW32__) && (!defined(__ANDROID__) || !defined(__arm__))
TARGET_WITH_CRYPTO static uint64x2_t sse2neon_vmull_p64_crypto(uint64x1_t _a, uint64x1_t _b)
{
    poly64_t a = vget_lane_p64(vreinterpret_p64_u64(_a), 0);
    poly64_t b = vget_lane_p64(vreinterpret_p64_u64(_b), 0);
    return vreinterpretq_u64_p128(vmull_p64(a, b));
}
#endif

TARGET_WITH_SIMD uint64x2_t sse2neon_vmull_p64(uint64x1_t _a, uint64x1_t _b)
{
#if !defined(__MINGW32__) && (!defined(__ANDROID__) || !defined(__arm__))
    // Wraps vmull_p64
    if(have_arm_crypto()) return sse2neon_vmull_p64_crypto(_a, _b);
#endif

    // ARMv7 polyfill
    // ARMv7/some A64 lacks vmull_p64, but it has vmull_p8.
    //
    // vmull_p8 calculates 8 8-bit->16-bit polynomial multiplies, but we need a
    // 64-bit->128-bit polynomial multiply.
    //
    // It needs some work and is somewhat slow, but it is still faster than all
    // known scalar methods.
    //
    // Algorithm adapted to C from
    // https://www.workofard.com/2017/07/ghash-for-low-end-cores/, which is adapted
    // from "Fast Software Polynomial Multiplication on ARM Processors Using the
    // NEON Engine" by Danilo Camara, Conrado Gouvea, Julio Lopez and Ricardo Dahab
    // (https://hal.inria.fr/hal-01506572)

    poly8x8_t a = vreinterpret_p8_u64(_a);
    poly8x8_t b = vreinterpret_p8_u64(_b);

    // Masks
    uint8x16_t k48_32 = vcombine_u8(vcreate_u8(0x0000ffffffffffff), vcreate_u8(0x00000000ffffffff));
    uint8x16_t k16_00 = vcombine_u8(vcreate_u8(0x000000000000ffff), vcreate_u8(0x0000000000000000));

    // Do the multiplies, rotating with vext to get all combinations
    uint8x16_t d = vreinterpretq_u8_p16(vmull_p8(a, b));                // D = A0 * B0
    uint8x16_t e = vreinterpretq_u8_p16(vmull_p8(a, vext_p8(b, b, 1))); // E = A0 * B1
    uint8x16_t f = vreinterpretq_u8_p16(vmull_p8(vext_p8(a, a, 1), b)); // F = A1 * B0
    uint8x16_t g = vreinterpretq_u8_p16(vmull_p8(a, vext_p8(b, b, 2))); // G = A0 * B2
    uint8x16_t h = vreinterpretq_u8_p16(vmull_p8(vext_p8(a, a, 2), b)); // H = A2 * B0
    uint8x16_t i = vreinterpretq_u8_p16(vmull_p8(a, vext_p8(b, b, 3))); // I = A0 * B3
    uint8x16_t j = vreinterpretq_u8_p16(vmull_p8(vext_p8(a, a, 3), b)); // J = A3 * B0
    uint8x16_t k = vreinterpretq_u8_p16(vmull_p8(a, vext_p8(b, b, 4))); // L = A0 * B4

    // Add cross products
    uint8x16_t l = veorq_u8(e, f); // L = E + F
    uint8x16_t m = veorq_u8(g, h); // M = G + H
    uint8x16_t n = veorq_u8(i, j); // N = I + J

    // Interleave. Using vzip1 and vzip2 prevents Clang from emitting TBL
    // instructions.
#if defined(__aarch64__)
    uint8x16_t lm_p0 = vreinterpretq_u8_u64(vzip1q_u64(vreinterpretq_u64_u8(l), vreinterpretq_u64_u8(m)));
    uint8x16_t lm_p1 = vreinterpretq_u8_u64(vzip2q_u64(vreinterpretq_u64_u8(l), vreinterpretq_u64_u8(m)));
    uint8x16_t nk_p0 = vreinterpretq_u8_u64(vzip1q_u64(vreinterpretq_u64_u8(n), vreinterpretq_u64_u8(k)));
    uint8x16_t nk_p1 = vreinterpretq_u8_u64(vzip2q_u64(vreinterpretq_u64_u8(n), vreinterpretq_u64_u8(k)));
#else
    uint8x16_t lm_p0 = vcombine_u8(vget_low_u8(l), vget_low_u8(m));
    uint8x16_t lm_p1 = vcombine_u8(vget_high_u8(l), vget_high_u8(m));
    uint8x16_t nk_p0 = vcombine_u8(vget_low_u8(n), vget_low_u8(k));
    uint8x16_t nk_p1 = vcombine_u8(vget_high_u8(n), vget_high_u8(k));
#endif
    // t0 = (L) (P0 + P1) << 8
    // t1 = (M) (P2 + P3) << 16
    uint8x16_t t0t1_tmp = veorq_u8(lm_p0, lm_p1);
    uint8x16_t t0t1_h   = vandq_u8(lm_p1, k48_32);
    uint8x16_t t0t1_l   = veorq_u8(t0t1_tmp, t0t1_h);

    // t2 = (N) (P4 + P5) << 24
    // t3 = (K) (P6 + P7) << 32
    uint8x16_t t2t3_tmp = veorq_u8(nk_p0, nk_p1);
    uint8x16_t t2t3_h   = vandq_u8(nk_p1, k16_00);
    uint8x16_t t2t3_l   = veorq_u8(t2t3_tmp, t2t3_h);

    // De-interleave
#if defined(__aarch64__)
    uint8x16_t t0 = vreinterpretq_u8_u64(vuzp1q_u64(vreinterpretq_u64_u8(t0t1_l), vreinterpretq_u64_u8(t0t1_h)));
    uint8x16_t t1 = vreinterpretq_u8_u64(vuzp2q_u64(vreinterpretq_u64_u8(t0t1_l), vreinterpretq_u64_u8(t0t1_h)));
    uint8x16_t t2 = vreinterpretq_u8_u64(vuzp1q_u64(vreinterpretq_u64_u8(t2t3_l), vreinterpretq_u64_u8(t2t3_h)));
    uint8x16_t t3 = vreinterpretq_u8_u64(vuzp2q_u64(vreinterpretq_u64_u8(t2t3_l), vreinterpretq_u64_u8(t2t3_h)));
#else
    uint8x16_t t1    = vcombine_u8(vget_high_u8(t0t1_l), vget_high_u8(t0t1_h));
    uint8x16_t t0    = vcombine_u8(vget_low_u8(t0t1_l), vget_low_u8(t0t1_h));
    uint8x16_t t3    = vcombine_u8(vget_high_u8(t2t3_l), vget_high_u8(t2t3_h));
    uint8x16_t t2    = vcombine_u8(vget_low_u8(t2t3_l), vget_low_u8(t2t3_h));
#endif
    // Shift the cross products
    uint8x16_t t0_shift = vextq_u8(t0, t0, 15); // t0 << 8
    uint8x16_t t1_shift = vextq_u8(t1, t1, 14); // t1 << 16
    uint8x16_t t2_shift = vextq_u8(t2, t2, 13); // t2 << 24
    uint8x16_t t3_shift = vextq_u8(t3, t3, 12); // t3 << 32

    // Accumulate the products
    uint8x16_t cross1 = veorq_u8(t0_shift, t1_shift);
    uint8x16_t cross2 = veorq_u8(t2_shift, t3_shift);
    uint8x16_t mix    = veorq_u8(d, cross1);
    uint8x16_t r      = veorq_u8(mix, cross2);
    return vreinterpretq_u64_u8(r);
}

TARGET_WITH_SIMD uint64x2_t mm_shuffle_epi8(uint64x2_t a, uint64x2_t b)
{
    uint8x16_t tbl        = vreinterpretq_u8_u64(a);         // input a
    uint8x16_t idx        = vreinterpretq_u8_u64(b);         // input b
    uint8x16_t idx_masked = vandq_u8(idx, vdupq_n_u8(0x8F)); // avoid using meaningless bits
#if defined(__aarch64__)
    return vreinterpretq_u64_u8(vqtbl1q_u8(tbl, idx_masked));
#else
    // use this line if testing on aarch64
    uint8x8x2_t a_split = {vget_low_u8(tbl), vget_high_u8(tbl)};
    return vreinterpretq_u64_u8(
        vcombine_u8(vtbl2_u8(a_split, vget_low_u8(idx_masked)), vtbl2_u8(a_split, vget_high_u8(idx_masked))));
#endif
}

TARGET_WITH_SIMD uint64x2_t mm_srli_si128(uint64x2_t a, int imm)
{
    uint8x16_t tmp[2] = {vreinterpretq_u8_u64(a), vdupq_n_u8(0)};
    return vreinterpretq_u64_u8(vld1q_u8(((uint8_t const*)tmp) + imm));
}

TARGET_WITH_SIMD uint64x2_t mm_slli_si128(uint64x2_t a, int imm)
{
    uint8x16_t tmp[2] = {vdupq_n_u8(0), vreinterpretq_u8_u64(a)};
    return vreinterpretq_u64_u8(vld1q_u8(((uint8_t const*)tmp) + (16 - imm)));
}

#endif
