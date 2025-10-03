/*
 * Public domain / MIT SHA-256 implementation for libaaruformat.
 *
 * Reference: FIPS PUB 180-4.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "decls.h"
#include "sha256.h"

#ifndef AARU_LOCAL
#define AARU_LOCAL static
#endif

#if defined(_MSC_VER)
#define ROTR32(x, n) _rotr((x), (n))
#else
#define ROTR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#endif

#define Ch(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define Maj(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SIGMA0(x)    (ROTR32((x), 2) ^ ROTR32((x), 13) ^ ROTR32((x), 22))
#define SIGMA1(x)    (ROTR32((x), 6) ^ ROTR32((x), 11) ^ ROTR32((x), 25))
#define sigma0(x)    (ROTR32((x), 7) ^ ROTR32((x), 18) ^ ((x) >> 3))
#define sigma1(x)    (ROTR32((x), 17) ^ ROTR32((x), 19) ^ ((x) >> 10))

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

AARU_LOCAL void sha256_transform(uint32_t state[8], const uint8_t block[64])
{
    uint32_t W[64];
    for(int i = 0; i < 16; i++)
        W[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) | ((uint32_t)block[i * 4 + 2] << 8) |
               (uint32_t)block[i * 4 + 3];
    for(int i = 16; i < 64; i++) W[i] = sigma1(W[i - 2]) + W[i - 7] + sigma0(W[i - 15]) + W[i - 16];

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5], g = state[6],
             h = state[7];

    for(int i = 0; i < 64; i++)
    {
        uint32_t T1 = h + SIGMA1(e) + Ch(e, f, g) + K[i] + W[i];
        uint32_t T2 = SIGMA0(a) + Maj(a, b, c);
        h           = g;
        g           = f;
        f           = e;
        e           = d + T1;
        d           = c;
        c           = b;
        b           = a;
        a           = T1 + T2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

AARU_EXPORT void AARU_CALL aaruf_sha256_init(sha256_ctx *ctx)
{
    ctx->state[0] = 0x6a09e667U;
    ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U;
    ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU;
    ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU;
    ctx->state[7] = 0x5be0cd19U;
    ctx->bitcount = 0ULL;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

AARU_EXPORT void AARU_CALL aaruf_sha256_update(sha256_ctx *ctx, const void *data_ptr, unsigned long len)
{
    if(len == 0) return;
    const uint8_t *data = (const uint8_t *)data_ptr;
    uint32_t       idx  = (uint32_t)((ctx->bitcount >> 3) & 0x3F);
    ctx->bitcount += ((uint64_t)len) << 3;
    uint32_t space = 64 - idx;

    if(len >= space)
    {
        memcpy(ctx->buffer + idx, data, space);
        sha256_transform(ctx->state, ctx->buffer);
        data += space;
        len -= space;
        while(len >= 64)
        {
            sha256_transform(ctx->state, data);
            data += 64;
            len -= 64;
        }
        idx = 0;
    }
    if(len > 0) memcpy(ctx->buffer + idx, data, len);
}

AARU_EXPORT void AARU_CALL aaruf_sha256_final(sha256_ctx *ctx, unsigned char *out)
{
    uint8_t pad[64];
    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;

    uint8_t  len_be[8];
    uint64_t bits = ctx->bitcount;
    for(int i = 0; i < 8; i++) len_be[7 - i] = (uint8_t)(bits >> (i * 8));

    uint32_t idx     = (uint32_t)((ctx->bitcount >> 3) & 0x3F);
    uint32_t pad_len = (idx < 56) ? (56 - idx) : (120 - idx);

    aaruf_sha256_update(ctx, pad, pad_len);
    aaruf_sha256_update(ctx, len_be, 8);

    for(int i = 0; i < 8; i++)
    {
        out[i * 4]     = (uint8_t)(ctx->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

AARU_EXPORT void AARU_CALL aaruf_sha256_buffer(const void *data, unsigned long size, unsigned char *result)
{
    sha256_ctx ctx;
    aaruf_sha256_init(&ctx);
    aaruf_sha256_update(&ctx, data, size);
    aaruf_sha256_final(&ctx, result);
}
