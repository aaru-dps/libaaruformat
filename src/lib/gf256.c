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

/**
 * @file gf256.c
 * @brief GF(2^8) arithmetic with SIMD-accelerated region multiply.
 *
 * Galois Field GF(2^8) with irreducible polynomial x^8 + x^4 + x^3 + x^2 + 1
 * (0x11D). Uses log/antilog tables for scalar operations and 4-bit nibble split
 * tables with SIMD shuffle for vectorized region multiply-accumulate.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "gf256.h"
#include "aaruformat/simd.h"

/* -------------------------------------------------------------------------
 * Log / anti-log tables for GF(2^8) with polynomial 0x11D
 * Generator element: 2
 * ------------------------------------------------------------------------- */

/** Log table: gf256_log[x] = discrete log base 2 of x in GF(2^8). gf256_log[0] is undefined. */
static uint8_t gf256_log_table[256];
/** Anti-log (exp) table: gf256_exp[i] = 2^i mod P. Extended to 512 entries to avoid modular reduction. */
static uint8_t gf256_exp_table[512];

/** Flag to ensure tables are initialized exactly once. */
static int gf256_tables_initialized = 0;

/**
 * @brief Initialize log/antilog tables for GF(2^8) with polynomial 0x11D.
 */
static void gf256_init_tables(void)
{
    if(gf256_tables_initialized) return;

    unsigned x = 1;
    for(int i = 0; i < 255; i++)
    {
        gf256_exp_table[i]     = (uint8_t)x;
        gf256_exp_table[i+255] = (uint8_t)x; /* wrap-around for easy mod 255 */
        gf256_log_table[x]     = (uint8_t)i;

        /* Multiply by generator 2 in GF(2^8) */
        x <<= 1;
        if(x & 0x100) x ^= 0x11D;
    }
    gf256_log_table[0] = 0; /* Convention: log(0) = 0, unused in mul since we short-circuit */
    gf256_exp_table[510] = gf256_exp_table[0];   /* Complete wrap */
    gf256_exp_table[511] = gf256_exp_table[1];

    gf256_tables_initialized = 1;
}

/* -------------------------------------------------------------------------
 * Scalar operations
 * ------------------------------------------------------------------------- */

uint8_t gf256_mul(uint8_t a, uint8_t b)
{
    if(a == 0 || b == 0) return 0;
    gf256_init_tables();
    return gf256_exp_table[gf256_log_table[a] + gf256_log_table[b]];
}

uint8_t gf256_div(uint8_t a, uint8_t b)
{
    if(a == 0) return 0;
    /* b must be non-zero */
    gf256_init_tables();
    return gf256_exp_table[gf256_log_table[a] + 255 - gf256_log_table[b]];
}

uint8_t gf256_inv(uint8_t a)
{
    /* a must be non-zero */
    gf256_init_tables();
    return gf256_exp_table[255 - gf256_log_table[a]];
}

/* -------------------------------------------------------------------------
 * SIMD region multiply-accumulate: dst[i] ^= GF_mul(src[i], coeff)
 *
 * Technique: 4-bit nibble decomposition.
 *   For a given coeff, precompute:
 *     low_tbl[i]  = GF_mul(i,     coeff)   for i = 0..15
 *     hi_tbl[i]   = GF_mul(i<<4,  coeff)   for i = 0..15
 *   Then for each byte b:
 *     GF_mul(b, coeff) = low_tbl[b & 0x0F] ^ hi_tbl[b >> 4]
 *   This maps to SIMD shuffle (pshufb / vpshufb / vqtbl1q_u8).
 * ------------------------------------------------------------------------- */

/**
 * @brief Build the two 16-byte nibble lookup tables for a given coefficient.
 */
static void gf256_build_mul_tables(uint8_t coeff, uint8_t low_tbl[16], uint8_t hi_tbl[16])
{
    gf256_init_tables();
    for(int i = 0; i < 16; i++)
    {
        low_tbl[i] = gf256_mul((uint8_t)i, coeff);
        hi_tbl[i]  = gf256_mul((uint8_t)(i << 4), coeff);
    }
}

/* ---------- Scalar fallback ---------- */

static void gf256_mul_region_scalar(uint8_t *dst, const uint8_t *src, uint8_t coeff, size_t len)
{
    uint8_t low_tbl[16], hi_tbl[16];
    gf256_build_mul_tables(coeff, low_tbl, hi_tbl);

    for(size_t i = 0; i < len; i++)
        dst[i] ^= low_tbl[src[i] & 0x0F] ^ hi_tbl[src[i] >> 4];
}

static void gf256_xor_region_scalar(uint8_t *dst, const uint8_t *src, size_t len)
{
    size_t i = 0;

    /* Process 8 bytes at a time */
    for(; i + 8 <= len; i += 8)
    {
        uint64_t d, s;
        memcpy(&d, dst + i, 8);
        memcpy(&s, src + i, 8);
        d ^= s;
        memcpy(dst + i, &d, 8);
    }

    for(; i < len; i++)
        dst[i] ^= src[i];
}

/* ---------- x86 SSSE3 ---------- */

#if defined(__x86_64__) || defined(__amd64) || defined(_M_AMD64) || defined(_M_X64) || \
    defined(__I386__) || defined(__i386__) || defined(__THW_INTEL) || defined(_M_IX86)

#include <tmmintrin.h> /* SSSE3: _mm_shuffle_epi8 */

SSSE3 static void gf256_mul_region_ssse3(uint8_t *dst, const uint8_t *src, uint8_t coeff, size_t len)
{
    uint8_t low_tbl[16], hi_tbl[16];
    gf256_build_mul_tables(coeff, low_tbl, hi_tbl);

    const __m128i low_v  = _mm_loadu_si128((const __m128i *)low_tbl);
    const __m128i hi_v   = _mm_loadu_si128((const __m128i *)hi_tbl);
    const __m128i mask   = _mm_set1_epi8(0x0F);

    size_t i = 0;
    for(; i + 16 <= len; i += 16)
    {
        __m128i s    = _mm_loadu_si128((const __m128i *)(src + i));
        __m128i d    = _mm_loadu_si128((const __m128i *)(dst + i));
        __m128i s_lo = _mm_and_si128(s, mask);
        __m128i s_hi = _mm_and_si128(_mm_srli_epi16(s, 4), mask);
        __m128i lo   = _mm_shuffle_epi8(low_v, s_lo);
        __m128i hi   = _mm_shuffle_epi8(hi_v,  s_hi);
        __m128i r    = _mm_xor_si128(_mm_xor_si128(lo, hi), d);
        _mm_storeu_si128((__m128i *)(dst + i), r);
    }

    /* Tail */
    for(; i < len; i++)
        dst[i] ^= low_tbl[src[i] & 0x0F] ^ hi_tbl[src[i] >> 4];
}

SSSE3 static void gf256_xor_region_ssse3(uint8_t *dst, const uint8_t *src, size_t len)
{
    size_t i = 0;
    for(; i + 16 <= len; i += 16)
    {
        __m128i d = _mm_loadu_si128((const __m128i *)(dst + i));
        __m128i s = _mm_loadu_si128((const __m128i *)(src + i));
        _mm_storeu_si128((__m128i *)(dst + i), _mm_xor_si128(d, s));
    }
    for(; i < len; i++) dst[i] ^= src[i];
}

/* ---------- x86 AVX2 ---------- */

#include <immintrin.h> /* AVX2: _mm256_shuffle_epi8 */

AVX2 static void gf256_mul_region_avx2(uint8_t *dst, const uint8_t *src, uint8_t coeff, size_t len)
{
    uint8_t low_tbl[16], hi_tbl[16];
    gf256_build_mul_tables(coeff, low_tbl, hi_tbl);

    /* Broadcast 16-byte tables to both 128-bit lanes of 256-bit register */
    const __m128i low_128 = _mm_loadu_si128((const __m128i *)low_tbl);
    const __m128i hi_128  = _mm_loadu_si128((const __m128i *)hi_tbl);
    const __m256i low_v   = _mm256_broadcastsi128_si256(low_128);
    const __m256i hi_v    = _mm256_broadcastsi128_si256(hi_128);
    const __m256i mask    = _mm256_set1_epi8(0x0F);

    size_t i = 0;
    for(; i + 32 <= len; i += 32)
    {
        __m256i s    = _mm256_loadu_si256((const __m256i *)(src + i));
        __m256i d    = _mm256_loadu_si256((const __m256i *)(dst + i));
        __m256i s_lo = _mm256_and_si256(s, mask);
        __m256i s_hi = _mm256_and_si256(_mm256_srli_epi16(s, 4), mask);
        __m256i lo   = _mm256_shuffle_epi8(low_v, s_lo);
        __m256i hi   = _mm256_shuffle_epi8(hi_v,  s_hi);
        __m256i r    = _mm256_xor_si256(_mm256_xor_si256(lo, hi), d);
        _mm256_storeu_si256((__m256i *)(dst + i), r);
    }

    /* Tail: SSSE3 for remaining 16-byte chunks, then scalar */
    const __m128i low_v2 = low_128;
    const __m128i hi_v2  = hi_128;
    const __m128i mask2  = _mm_set1_epi8(0x0F);
    for(; i + 16 <= len; i += 16)
    {
        __m128i s    = _mm_loadu_si128((const __m128i *)(src + i));
        __m128i d    = _mm_loadu_si128((const __m128i *)(dst + i));
        __m128i s_lo = _mm_and_si128(s, mask2);
        __m128i s_hi = _mm_and_si128(_mm_srli_epi16(s, 4), mask2);
        __m128i lo   = _mm_shuffle_epi8(low_v2, s_lo);
        __m128i hi   = _mm_shuffle_epi8(hi_v2,  s_hi);
        __m128i r    = _mm_xor_si128(_mm_xor_si128(lo, hi), d);
        _mm_storeu_si128((__m128i *)(dst + i), r);
    }

    for(; i < len; i++)
        dst[i] ^= low_tbl[src[i] & 0x0F] ^ hi_tbl[src[i] >> 4];
}

AVX2 static void gf256_xor_region_avx2(uint8_t *dst, const uint8_t *src, size_t len)
{
    size_t i = 0;
    for(; i + 32 <= len; i += 32)
    {
        __m256i d = _mm256_loadu_si256((const __m256i *)(dst + i));
        __m256i s = _mm256_loadu_si256((const __m256i *)(src + i));
        _mm256_storeu_si256((__m256i *)(dst + i), _mm256_xor_si256(d, s));
    }
    for(; i + 16 <= len; i += 16)
    {
        __m128i d = _mm_loadu_si128((const __m128i *)(dst + i));
        __m128i s = _mm_loadu_si128((const __m128i *)(src + i));
        _mm_storeu_si128((__m128i *)(dst + i), _mm_xor_si128(d, s));
    }
    for(; i < len; i++) dst[i] ^= src[i];
}

/* Forward declarations for CPUID-based detection (from simd.c) */
int have_ssse3(void);
int have_avx2(void);

#endif /* x86 */

/* ---------- ARM NEON ---------- */

#if defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) || defined(_M_ARM)

#include <arm_neon.h>

TARGET_WITH_SIMD static void gf256_mul_region_neon(uint8_t *dst, const uint8_t *src, uint8_t coeff, size_t len)
{
    uint8_t low_tbl[16], hi_tbl[16];
    gf256_build_mul_tables(coeff, low_tbl, hi_tbl);

    const uint8x16_t low_v = vld1q_u8(low_tbl);
    const uint8x16_t hi_v  = vld1q_u8(hi_tbl);
    const uint8x16_t mask  = vdupq_n_u8(0x0F);

    size_t i = 0;
    for(; i + 16 <= len; i += 16)
    {
        uint8x16_t s    = vld1q_u8(src + i);
        uint8x16_t d    = vld1q_u8(dst + i);
        uint8x16_t s_lo = vandq_u8(s, mask);
        uint8x16_t s_hi = vandq_u8(vshrq_n_u8(s, 4), mask);
        uint8x16_t lo   = vqtbl1q_u8(low_v, s_lo);
        uint8x16_t hi   = vqtbl1q_u8(hi_v,  s_hi);
        uint8x16_t r    = veorq_u8(veorq_u8(lo, hi), d);
        vst1q_u8(dst + i, r);
    }

    for(; i < len; i++)
        dst[i] ^= low_tbl[src[i] & 0x0F] ^ hi_tbl[src[i] >> 4];
}

TARGET_WITH_SIMD static void gf256_xor_region_neon(uint8_t *dst, const uint8_t *src, size_t len)
{
    size_t i = 0;
    for(; i + 16 <= len; i += 16)
    {
        uint8x16_t d = vld1q_u8(dst + i);
        uint8x16_t s = vld1q_u8(src + i);
        vst1q_u8(dst + i, veorq_u8(d, s));
    }
    for(; i < len; i++) dst[i] ^= src[i];
}

int have_neon(void);

#endif /* ARM */

/* -------------------------------------------------------------------------
 * Dispatch functions
 * ------------------------------------------------------------------------- */

void gf256_mul_region(uint8_t *dst, const uint8_t *src, uint8_t coeff, size_t len)
{
    if(coeff == 0) return;
    if(coeff == 1) { gf256_xor_region(dst, src, len); return; }

    gf256_init_tables();

#if defined(__x86_64__) || defined(__amd64) || defined(_M_AMD64) || defined(_M_X64) || \
    defined(__I386__) || defined(__i386__) || defined(__THW_INTEL) || defined(_M_IX86)
    if(have_avx2())  { gf256_mul_region_avx2(dst, src, coeff, len);  return; }
    if(have_ssse3()) { gf256_mul_region_ssse3(dst, src, coeff, len); return; }
#endif

#if defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) || defined(_M_ARM)
    if(have_neon()) { gf256_mul_region_neon(dst, src, coeff, len); return; }
#endif

    gf256_mul_region_scalar(dst, src, coeff, len);
}

void gf256_xor_region(uint8_t *dst, const uint8_t *src, size_t len)
{
#if defined(__x86_64__) || defined(__amd64) || defined(_M_AMD64) || defined(_M_X64) || \
    defined(__I386__) || defined(__i386__) || defined(__THW_INTEL) || defined(_M_IX86)
    if(have_avx2())  { gf256_xor_region_avx2(dst, src, len);  return; }
    if(have_ssse3()) { gf256_xor_region_ssse3(dst, src, len); return; }
#endif

#if defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) || defined(_M_ARM)
    if(have_neon()) { gf256_xor_region_neon(dst, src, len); return; }
#endif

    gf256_xor_region_scalar(dst, src, len);
}
