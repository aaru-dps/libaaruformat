/*
 * This is an OpenSSL-compatible implementation of the RSA Data Security, Inc.
 * MD5 Message-Digest Algorithm (RFC 1321).
 *
 * Homepage:
 * http://openwall.info/wiki/people/solar/software/public-domain-source-code/md5
 *
 * Author:
 * Alexander Peslyak, better known as Solar Designer <solar at openwall.com>
 *
 * This software was written by Alexander Peslyak in 2001.  No copyright is
 * claimed, and the software is hereby placed in the public domain.
 * In case this attempt to disclaim copyright and place the software in the
 * public domain is deemed null and void, then the software is
 * Copyright (c) 2001 Alexander Peslyak and it is hereby released to the
 * general public under the following terms:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 *
 * (This is a heavily cut-down "BSD license".)
 *
 * This differs from Colin Plumb's older public domain implementation in that
 * no exactly 32-bit integer data type is required (any 32-bit or wider
 * unsigned integer data type will do), there's no compile-time endianness
 * configuration, and the function prototypes match OpenSSL's.  No code from
 * Colin Plumb's implementation has been reused; this comment merely compares
 * the properties of the two independent implementations.
 *
 * The primary goals of this implementation are portability and ease of use.
 * It is meant to be fast, but not as fast as possible.  Some known
 * optimizations are not included to reduce source code size and avoid
 * compile-time configuration.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Performance helper macros */
#ifndef AARU_RESTRICT
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define AARU_RESTRICT restrict
#elif defined(_MSC_VER)
#define AARU_RESTRICT __restrict
#else
#define AARU_RESTRICT
#endif
#endif
#ifndef LIKELY
#if defined(__GNUC__) || defined(__clang__)
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x)   (x)
#define UNLIKELY(x) (x)
#endif
#endif
#ifndef HOT
#if defined(__GNUC__) || defined(__clang__)
#define HOT __attribute__((hot))
#else
#define HOT
#endif
#endif
#ifndef MD5_MAX_UNROLL
#define MD5_MAX_UNROLL 4
#endif
#ifndef MD5_PREFETCH_DISTANCE_BLOCKS
#define MD5_PREFETCH_DISTANCE_BLOCKS 8
#endif
#ifndef MD5_ENABLE_PREFETCH
#define MD5_ENABLE_PREFETCH 1
#endif
#ifndef MD5_UNROLL8_THRESHOLD
#define MD5_UNROLL8_THRESHOLD 8192UL
#endif
#ifndef MD5_UNROLL4_THRESHOLD
#define MD5_UNROLL4_THRESHOLD 2048UL
#endif
#ifndef MD5_UNROLL2_THRESHOLD
#define MD5_UNROLL2_THRESHOLD 512UL
#endif

#include "decls.h"
#include "md5.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif
#if defined(__SSE2__) || defined(__AVX2__) || defined(__SSSE3__) || defined(__SSE4_1__) || defined(__x86_64__) || \
    defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

/*
 * The basic MD5 functions.
 *
 * F and G are optimized compared to their RFC 1321 definitions for
 * architectures that lack an AND-NOT instruction, just like in Colin Plumb's
 * implementation.
 */
#define F(x, y, z)  ((z) ^ ((x) & ((y) ^ (z))))
#define G(x, y, z)  ((y) ^ ((z) & ((x) ^ (y))))
#define H(x, y, z)  (((x) ^ (y)) ^ (z))
#define H2(x, y, z) ((x) ^ ((y) ^ (z)))
#define I(x, y, z)  ((y) ^ ((x) | ~(z)))

/* Rotate-left helper using compiler intrinsics when available */
#if defined(_MSC_VER)
#define ROTL32(x, s) _rotl((x), (s))
#else
#if defined(__has_builtin)
#if __has_builtin(__builtin_rotateleft32)
#define ROTL32(x, s) __builtin_rotateleft32((x), (s))
#endif
#endif
#endif
#ifndef ROTL32
#define ROTL32(x, s) (((uint32_t)(x) << (s)) | ((uint32_t)(x) >> (32 - (s))))
#endif

/*
 * The MD5 transformation for all four rounds.
 */
#define STEP(f, a, b, c, d, x, t, s)               \
    (a) += f((b), (c), (d)) + (x) + (uint32_t)(t); \
    (a) = ROTL32((a), (s));                        \
    (a) += (b);

/*
 * SET reads 4 input bytes in little-endian byte order and stores them in a
 * properly aligned word in host byte order.
 *
 * The check for little-endian architectures that tolerate unaligned memory
 * accesses is just an optimization.  Nothing will break if it fails to detect
 * a suitable architecture.
 *
 * Unfortunately, this optimization may be a C strict aliasing rules violation
 * if the caller's data buffer has effective type that cannot be aliased by
 * uint32_t.  In practice, this problem may occur if these MD5 routines are
 * inlined into a calling function, or with future and dangerously advanced
 * link-time optimizations.  For the time being, keeping these MD5 routines in
 * their own translation unit avoids the problem.
 */
#if defined(__i386__) || defined(__x86_64__) || defined(__vax__)
#define SET(n) (*(uint32_t *)&ptr[(n) * 4])
#define GET(n) SET(n)
#else
#define SET(n)                                                                      \
    (ctx->block[(n)] = (uint32_t)ptr[(n) * 4] | ((uint32_t)ptr[(n) * 4 + 1] << 8) | \
                       ((uint32_t)ptr[(n) * 4 + 2] << 16) | ((uint32_t)ptr[(n) * 4 + 3] << 24))
#define GET(n) (ctx->block[(n)])
#endif

/*
 * This processes one or more 64-byte data blocks, but does NOT update the bit
 * counters.  There are no alignment requirements.
 */
FORCE_INLINE HOT void md5_process_block_loaded(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d,
                                               const unsigned char *AARU_RESTRICT ptr)
{
    const uint32_t *wp = (const uint32_t *)ptr;  // unaligned ok on supported arch (we gate optimized path)
    uint32_t        A = *a, B = *b, C = *c, D = *d;
    uint32_t        w0  = wp[0];
    uint32_t        w1  = wp[1];
    uint32_t        w2  = wp[2];
    uint32_t        w3  = wp[3];
    uint32_t        w4  = wp[4];
    uint32_t        w5  = wp[5];
    uint32_t        w6  = wp[6];
    uint32_t        w7  = wp[7];
    uint32_t        w8  = wp[8];
    uint32_t        w9  = wp[9];
    uint32_t        w10 = wp[10];
    uint32_t        w11 = wp[11];
    uint32_t        w12 = wp[12];
    uint32_t        w13 = wp[13];
    uint32_t        w14 = wp[14];
    uint32_t        w15 = wp[15];

    uint32_t sA = A, sB = B, sC = C, sD = D;

    /* Round 1 */
    STEP(F, A, B, C, D, w0, 0xd76aa478, 7)
    STEP(F, D, A, B, C, w1, 0xe8c7b756, 12)
    STEP(F, C, D, A, B, w2, 0x242070db, 17)
    STEP(F, B, C, D, A, w3, 0xc1bdceee, 22)
    STEP(F, A, B, C, D, w4, 0xf57c0faf, 7)
    STEP(F, D, A, B, C, w5, 0x4787c62a, 12)
    STEP(F, C, D, A, B, w6, 0xa8304613, 17)
    STEP(F, B, C, D, A, w7, 0xfd469501, 22)
    STEP(F, A, B, C, D, w8, 0x698098d8, 7)
    STEP(F, D, A, B, C, w9, 0x8b44f7af, 12)
    STEP(F, C, D, A, B, w10, 0xffff5bb1, 17)
    STEP(F, B, C, D, A, w11, 0x895cd7be, 22)
    STEP(F, A, B, C, D, w12, 0x6b901122, 7)
    STEP(F, D, A, B, C, w13, 0xfd987193, 12)
    STEP(F, C, D, A, B, w14, 0xa679438e, 17)
    STEP(F, B, C, D, A, w15, 0x49b40821, 22)

    /* Round 2 */
    STEP(G, A, B, C, D, w1, 0xf61e2562, 5)
    STEP(G, D, A, B, C, w6, 0xc040b340, 9)
    STEP(G, C, D, A, B, w11, 0x265e5a51, 14)
    STEP(G, B, C, D, A, w0, 0xe9b6c7aa, 20)
    STEP(G, A, B, C, D, w5, 0xd62f105d, 5)
    STEP(G, D, A, B, C, w10, 0x02441453, 9)
    STEP(G, C, D, A, B, w15, 0xd8a1e681, 14)
    STEP(G, B, C, D, A, w4, 0xe7d3fbc8, 20)
    STEP(G, A, B, C, D, w9, 0x21e1cde6, 5)
    STEP(G, D, A, B, C, w14, 0xc33707d6, 9)
    STEP(G, C, D, A, B, w3, 0xf4d50d87, 14)
    STEP(G, B, C, D, A, w8, 0x455a14ed, 20)
    STEP(G, A, B, C, D, w13, 0xa9e3e905, 5)
    STEP(G, D, A, B, C, w2, 0xfcefa3f8, 9)
    STEP(G, C, D, A, B, w7, 0x676f02d9, 14)
    STEP(G, B, C, D, A, w12, 0x8d2a4c8a, 20)

    /* Round 3 */
    STEP(H, A, B, C, D, w5, 0xfffa3942, 4)
    STEP(H2, D, A, B, C, w8, 0x8771f681, 11)
    STEP(H, C, D, A, B, w11, 0x6d9d6122, 16)
    STEP(H2, B, C, D, A, w14, 0xfde5380c, 23)
    STEP(H, A, B, C, D, w1, 0xa4beea44, 4)
    STEP(H2, D, A, B, C, w4, 0x4bdecfa9, 11)
    STEP(H, C, D, A, B, w7, 0xf6bb4b60, 16)
    STEP(H2, B, C, D, A, w10, 0xbebfbc70, 23)
    STEP(H, A, B, C, D, w13, 0x289b7ec6, 4)
    STEP(H2, D, A, B, C, w0, 0xeaa127fa, 11)
    STEP(H, C, D, A, B, w3, 0xd4ef3085, 16)
    STEP(H2, B, C, D, A, w6, 0x04881d05, 23)
    STEP(H, A, B, C, D, w9, 0xd9d4d039, 4)
    STEP(H2, D, A, B, C, w12, 0xe6db99e5, 11)
    STEP(H, C, D, A, B, w15, 0x1fa27cf8, 16)
    STEP(H2, B, C, D, A, w2, 0xc4ac5665, 23)

    /* Round 4 */
    STEP(I, A, B, C, D, w0, 0xf4292244, 6)
    STEP(I, D, A, B, C, w7, 0x432aff97, 10)
    STEP(I, C, D, A, B, w14, 0xab9423a7, 15)
    STEP(I, B, C, D, A, w5, 0xfc93a039, 21)
    STEP(I, A, B, C, D, w12, 0x655b59c3, 6)
    STEP(I, D, A, B, C, w3, 0x8f0ccc92, 10)
    STEP(I, C, D, A, B, w10, 0xffeff47d, 15)
    STEP(I, B, C, D, A, w1, 0x85845dd1, 21)
    STEP(I, A, B, C, D, w8, 0x6fa87e4f, 6)
    STEP(I, D, A, B, C, w15, 0xfe2ce6e0, 10)
    STEP(I, C, D, A, B, w6, 0xa3014314, 15)
    STEP(I, B, C, D, A, w13, 0x4e0811a1, 21)
    STEP(I, A, B, C, D, w4, 0xf7537e82, 6)
    STEP(I, D, A, B, C, w11, 0xbd3af235, 10)
    STEP(I, C, D, A, B, w2, 0x2ad7d2bb, 15)
    STEP(I, B, C, D, A, w9, 0xeb86d391, 21)

    *a = A + sA;
    *b = B + sB;
    *c = C + sC;
    *d = D + sD;
}

/*
 * This processes one or more 64-byte data blocks, but does NOT update the bit
 * counters.  There are no alignment requirements.
 */
static HOT const void *body(md5_ctx *ctx, const void *data, unsigned long size)
{
    const unsigned char *AARU_RESTRICT ptr = (const unsigned char *)data;
    uint32_t                           a = ctx->a, b = ctx->b, c = ctx->c, d = ctx->d;

#if (defined(__x86_64__) || defined(__i386__) || defined(__aarch64__)) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#if MD5_MAX_UNROLL >= 8
    // 8-block unroll only if total remaining size large enough
    while(size >= 512 && size >= MD5_UNROLL8_THRESHOLD)
    {
#if MD5_ENABLE_PREFETCH
        __builtin_prefetch(ptr + 64 * (MD5_PREFETCH_DISTANCE_BLOCKS + 8), 0, 3);
        __builtin_prefetch(ptr + 64 * (MD5_PREFETCH_DISTANCE_BLOCKS + 10), 0, 3);
#endif
        md5_process_block_loaded(&a, &b, &c, &d, ptr + 64 * 0);
        md5_process_block_loaded(&a, &b, &c, &d, ptr + 64 * 1);
        md5_process_block_loaded(&a, &b, &c, &d, ptr + 64 * 2);
        md5_process_block_loaded(&a, &b, &c, &d, ptr + 64 * 3);
        md5_process_block_loaded(&a, &b, &c, &d, ptr + 64 * 4);
        md5_process_block_loaded(&a, &b, &c, &d, ptr + 64 * 5);
        md5_process_block_loaded(&a, &b, &c, &d, ptr + 64 * 6);
        md5_process_block_loaded(&a, &b, &c, &d, ptr + 64 * 7);
        ptr += 512;
        size -= 512;
    }
#endif
    // 4-block unroll
    while(size >= 256 && size >= MD5_UNROLL4_THRESHOLD)
    {
#if MD5_ENABLE_PREFETCH
        __builtin_prefetch(ptr + 64 * (MD5_PREFETCH_DISTANCE_BLOCKS), 0, 3);
        __builtin_prefetch(ptr + 64 * (MD5_PREFETCH_DISTANCE_BLOCKS + 2), 0, 3);
#endif
        md5_process_block_loaded(&a, &b, &c, &d, ptr);
        md5_process_block_loaded(&a, &b, &c, &d, ptr + 64);
        md5_process_block_loaded(&a, &b, &c, &d, ptr + 128);
        md5_process_block_loaded(&a, &b, &c, &d, ptr + 192);
        ptr += 256;
        size -= 256;
    }
    // 2-block unroll
    while(size >= 128 && size >= MD5_UNROLL2_THRESHOLD)
    {
#if MD5_ENABLE_PREFETCH
        __builtin_prefetch(ptr + 64 * (MD5_PREFETCH_DISTANCE_BLOCKS - 2), 0, 3);
#endif
        md5_process_block_loaded(&a, &b, &c, &d, ptr);
        md5_process_block_loaded(&a, &b, &c, &d, ptr + 64);
        ptr += 128;
        size -= 128;
    }
    // Single block
    while(size >= 64)
    {
#if MD5_ENABLE_PREFETCH
        if(size >= 64 * (MD5_PREFETCH_DISTANCE_BLOCKS))
        {
            __builtin_prefetch(ptr + 64 * (MD5_PREFETCH_DISTANCE_BLOCKS / 2), 0, 3);
            __builtin_prefetch(ptr + 64 * (MD5_PREFETCH_DISTANCE_BLOCKS / 2 + 2), 0, 3);
        }
#endif
        md5_process_block_loaded(&a, &b, &c, &d, ptr);
        ptr += 64;
        size -= 64;
    }
#else
    // Fallback original loop
    uint32_t             saved_a, saved_b, saved_c, saved_d;
    const unsigned char *p2 = ptr;
    unsigned long        sz = size;
    while(sz >= 64)
    {
        if(sz >= 64 * 8)
        {
#if defined(__SSE2__) || defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
            __builtin_prefetch(p2 + 64 * 4, 0, 3);
            __builtin_prefetch(p2 + 64 * 6, 0, 3);
#elif defined(__ARM_NEON) || defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) || defined(_M_ARM)
            __builtin_prefetch((const void *)(p2 + 64 * 4));
            __builtin_prefetch((const void *)(p2 + 64 * 6));
#endif
        }
        saved_a = a;
        saved_b = b;
        saved_c = c;
        saved_d = d;
        STEP(F, a, b, c, d, SET(0), 0xd76aa478, 7)
        STEP(F, d, a, b, c, SET(1), 0xe8c7b756, 12)
        STEP(F, c, d, a, b, SET(2), 0x242070db, 17)
        STEP(F, b, c, d, a, SET(3), 0xc1bdceee, 22)
        STEP(F, a, b, c, d, SET(4), 0xf57c0faf, 7)
        STEP(F, d, a, b, c, SET(5), 0x4787c62a, 12)
        STEP(F, c, d, a, b, SET(6), 0xa8304613, 17)
        STEP(F, b, c, d, a, SET(7), 0xfd469501, 22)
        STEP(F, a, b, c, d, SET(8), 0x698098d8, 7)
        STEP(F, d, a, b, c, SET(9), 0x8b44f7af, 12)
        STEP(F, c, d, a, b, SET(10), 0xffff5bb1, 17)
        STEP(F, b, c, d, a, SET(11), 0x895cd7be, 22)
        STEP(F, a, b, c, d, SET(12), 0x6b901122, 7)
        STEP(F, d, a, b, c, SET(13), 0xfd987193, 12)
        STEP(F, c, d, a, b, SET(14), 0xa679438e, 17)
        STEP(F, b, c, d, a, SET(15), 0x49b40821, 22)
        STEP(G, a, b, c, d, GET(1), 0xf61e2562, 5)
        STEP(G, d, a, b, c, GET(6), 0xc040b340, 9)
        STEP(G, c, d, a, b, GET(11), 0x265e5a51, 14)
        STEP(G, b, c, d, a, GET(0), 0xe9b6c7aa, 20)
        STEP(G, a, b, c, d, GET(5), 0xd62f105d, 5)
        STEP(G, d, a, b, c, GET(10), 0x02441453, 9)
        STEP(G, c, d, a, b, GET(15), 0xd8a1e681, 14)
        STEP(G, b, c, d, a, GET(4), 0xe7d3fbc8, 20)
        STEP(G, a, b, c, d, GET(9), 0x21e1cde6, 5)
        STEP(G, d, a, b, c, GET(14), 0xc33707d6, 9)
        STEP(G, c, d, a, b, GET(3), 0xf4d50d87, 14)
        STEP(G, b, c, d, a, GET(8), 0x455a14ed, 20)
        STEP(G, a, b, c, d, GET(13), 0xa9e3e905, 5)
        STEP(G, d, a, b, c, GET(2), 0xfcefa3f8, 9)
        STEP(G, c, d, a, b, GET(7), 0x676f02d9, 14)
        STEP(G, b, c, d, a, GET(12), 0x8d2a4c8a, 20)
        STEP(H, a, b, c, d, GET(5), 0xfffa3942, 4)
        STEP(H2, d, a, b, c, GET(8), 0x8771f681, 11)
        STEP(H, c, d, a, b, GET(11), 0x6d9d6122, 16)
        STEP(H2, b, c, d, a, GET(14), 0xfde5380c, 23)
        STEP(H, a, b, c, d, GET(1), 0xa4beea44, 4)
        STEP(H2, d, a, b, c, GET(4), 0x4bdecfa9, 11)
        STEP(H, c, d, a, b, GET(7), 0xf6bb4b60, 16)
        STEP(H2, b, c, d, a, GET(10), 0xbebfbc70, 23)
        STEP(H, a, b, c, d, GET(13), 0x289b7ec6, 4)
        STEP(H2, d, a, b, c, GET(0), 0xeaa127fa, 11)
        STEP(H, c, d, a, b, GET(3), 0xd4ef3085, 16)
        STEP(H2, b, c, d, a, GET(6), 0x04881d05, 23)
        STEP(H, a, b, c, d, GET(9), 0xd9d4d039, 4)
        STEP(H2, d, a, b, c, GET(12), 0xe6db99e5, 11)
        STEP(H, c, d, a, b, GET(15), 0x1fa27cf8, 16)
        STEP(H2, b, c, d, a, GET(2), 0xc4ac5665, 23)
        STEP(I, a, b, c, d, GET(0), 0xf4292244, 6)
        STEP(I, d, a, b, c, GET(7), 0x432aff97, 10)
        STEP(I, c, d, a, b, GET(14), 0xab9423a7, 15)
        STEP(I, b, c, d, a, GET(5), 0xfc93a039, 21)
        STEP(I, a, b, c, d, GET(12), 0x655b59c3, 6)
        STEP(I, d, a, b, c, GET(3), 0x8f0ccc92, 10)
        STEP(I, c, d, a, b, GET(10), 0xffeff47d, 15)
        STEP(I, b, c, d, a, GET(1), 0x85845dd1, 21)
        STEP(I, a, b, c, d, GET(8), 0x6fa87e4f, 6)
        STEP(I, d, a, b, c, GET(15), 0xfe2ce6e0, 10)
        STEP(I, c, d, a, b, GET(6), 0xa3014314, 15)
        STEP(I, b, c, d, a, GET(13), 0x4e0811a1, 21)
        STEP(I, a, b, c, d, GET(4), 0xf7537e82, 6)
        STEP(I, d, a, b, c, GET(11), 0xbd3af235, 10)
        STEP(I, c, d, a, b, GET(2), 0x2ad7d2bb, 15)
        STEP(I, b, c, d, a, GET(9), 0xeb86d391, 21)
        a += saved_a;
        b += saved_b;
        c += saved_c;
        d += saved_d;
        p2 += 64;
        sz -= 64;
    }
    ptr  = p2;
    size = sz;
#endif

    ctx->a = a;
    ctx->b = b;
    ctx->c = c;
    ctx->d = d;
    return ptr;
}

AARU_EXPORT void AARU_CALL aaruf_md5_init(md5_ctx *ctx)
{
    ctx->a = 0x67452301;
    ctx->b = 0xefcdab89;
    ctx->c = 0x98badcfe;
    ctx->d = 0x10325476;

    ctx->lo = 0;
    ctx->hi = 0;
}

AARU_EXPORT void AARU_CALL aaruf_md5_update(md5_ctx *ctx, const void *AARU_RESTRICT data, unsigned long size)
{

    const uint32_t saved_lo = ctx->lo;
    if((ctx->lo = (saved_lo + size) & 0x1fffffff) < saved_lo) ctx->hi++;
    ctx->hi += size >> 29;

    const unsigned long used = saved_lo & 0x3f;

    if(UNLIKELY(used))
    {
        unsigned long available = 64 - used;
        if(size < available)
        {
            memcpy(&ctx->buffer[used], data, size);
            return;
        }
        memcpy(&ctx->buffer[used], data, available);
        data = (const unsigned char *)data + available;
        size -= available;
        body(ctx, ctx->buffer, 64);
    }

    if(LIKELY(size >= 64))
    {
        data = body(ctx, data, size & ~(unsigned long)0x3f);
        size &= 0x3f;
    }

    if(size) memcpy(ctx->buffer, data, size);
}

#define OUT(dst, src)                        \
    (dst)[0] = (unsigned char)(src);         \
    (dst)[1] = (unsigned char)((src) >> 8);  \
    (dst)[2] = (unsigned char)((src) >> 16); \
    (dst)[3] = (unsigned char)((src) >> 24);

AARU_EXPORT void AARU_CALL aaruf_md5_final(md5_ctx *ctx, unsigned char *result)
{

    unsigned long used = ctx->lo & 0x3f;

    ctx->buffer[used++] = 0x80;

    unsigned long available = 64 - used;

    if(available < 8)
    {
        memset(&ctx->buffer[used], 0, available);
        body(ctx, ctx->buffer, 64);
        used      = 0;
        available = 64;
    }

    memset(&ctx->buffer[used], 0, available - 8);

    ctx->lo <<= 3;
    OUT(&ctx->buffer[56], ctx->lo)
    OUT(&ctx->buffer[60], ctx->hi)

    body(ctx, ctx->buffer, 64);

    OUT(&result[0], ctx->a)
    OUT(&result[4], ctx->b)
    OUT(&result[8], ctx->c)
    OUT(&result[12], ctx->d)

    memset(ctx, 0, sizeof(*ctx));
}

AARU_EXPORT void AARU_CALL aaruf_md5_buffer(const void *data, unsigned long size, unsigned char *result)
{
    md5_ctx ctx;
    aaruf_md5_init(&ctx);
    aaruf_md5_update(&ctx, data, size);
    aaruf_md5_final(&ctx, result);
}
