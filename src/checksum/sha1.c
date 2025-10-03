/*
 * Public domain / MIT SHA-1 implementation for libaaruformat.
 *
 * This implementation is derived from the FIPS PUB 180-1 specification and
 * other public domain reference implementations; released as public domain.
 * If public domain dedication is not recognized, you may use it under the
 * MIT license terms (see header in sha1.h).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "decls.h"
#include "sha1.h"

#ifndef AARU_LOCAL
#define AARU_LOCAL static
#endif

/* Rotate-left 32-bit */
#if defined(_MSC_VER)
#define ROTL32(v, n) _rotl(v, n)
#else
#define ROTL32(v, n) (((v) << (n)) | ((v) >> (32 - (n))))
#endif

/* SHA-1 logical functions */
#define F0(b, c, d) (((b) & (c)) | (~(b) & (d)))
#define F1(b, c, d) ((b) ^ (c) ^ (d))
#define F2(b, c, d) (((b) & (c)) | ((b) & (d)) | ((c) & (d)))
#define F3(b, c, d) ((b) ^ (c) ^ (d))

AARU_EXPORT void AARU_CALL aaruf_sha1_init(sha1_ctx *ctx)
{
    ctx->state[0] = 0x67452301UL;
    ctx->state[1] = 0xEFCDAB89UL;
    ctx->state[2] = 0x98BADCFEUL;
    ctx->state[3] = 0x10325476UL;
    ctx->state[4] = 0xC3D2E1F0UL;
    ctx->count    = 0ULL;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

AARU_LOCAL void sha1_transform(uint32_t state[5], const uint8_t block[64])
{
    uint32_t W[80];

    /* Prepare message schedule */
    for(int t = 0; t < 16; t++)
    {
        W[t] = ((uint32_t)block[t * 4] << 24) | ((uint32_t)block[t * 4 + 1] << 16) | ((uint32_t)block[t * 4 + 2] << 8) |
               ((uint32_t)block[t * 4 + 3]);
    }
    for(int t = 16; t < 80; t++) { W[t] = ROTL32(W[t - 3] ^ W[t - 8] ^ W[t - 14] ^ W[t - 16], 1); }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];

    for(int t = 0; t < 80; t++)
    {
        uint32_t temp;
        if(t < 20)
            temp = ROTL32(a, 5) + F0(b, c, d) + e + W[t] + 0x5A827999UL;
        else if(t < 40)
            temp = ROTL32(a, 5) + F1(b, c, d) + e + W[t] + 0x6ED9EBA1UL;
        else if(t < 60)
            temp = ROTL32(a, 5) + F2(b, c, d) + e + W[t] + 0x8F1BBCDCUL;
        else
            temp = ROTL32(a, 5) + F3(b, c, d) + e + W[t] + 0xCA62C1D6UL;

        e = d;
        d = c;
        c = ROTL32(b, 30);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

AARU_EXPORT void AARU_CALL aaruf_sha1_update(sha1_ctx *ctx, const void *data_ptr, unsigned long len)
{
    const uint8_t *data = (const uint8_t *)data_ptr;
    if(len == 0) return;

    /* Number of bytes mod 64 currently in buffer */
    uint32_t buffer_bytes = (uint32_t)((ctx->count >> 3) & 0x3F);

    /* Update bit count */
    ctx->count += (uint64_t)len << 3;

    uint32_t free_bytes = 64 - buffer_bytes;

    if(len >= free_bytes)
    {
        /* Fill buffer and compress */
        memcpy(ctx->buffer + buffer_bytes, data, free_bytes);
        sha1_transform(ctx->state, ctx->buffer);
        data += free_bytes;
        len -= free_bytes;

        /* Process direct blocks */
        while(len >= 64)
        {
            sha1_transform(ctx->state, data);
            data += 64;
            len -= 64;
        }
        buffer_bytes = 0;
    }

    /* Buffer the remaining */
    if(len > 0) memcpy(ctx->buffer + buffer_bytes, data, len);
}

AARU_EXPORT void AARU_CALL aaruf_sha1_final(sha1_ctx *ctx, unsigned char *digest)
{
    uint8_t pad[64];
    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;

    uint8_t  length_be[8];
    uint64_t bits = ctx->count;
    for(int i = 0; i < 8; i++) { length_be[7 - i] = (uint8_t)(bits >> (i * 8)); }

    /* Current bytes mod 64 */
    uint32_t buffer_bytes = (uint32_t)((ctx->count >> 3) & 0x3F);

    uint32_t pad_len = (buffer_bytes < 56) ? (56 - buffer_bytes) : (120 - buffer_bytes);

    aaruf_sha1_update(ctx, pad, pad_len);
    aaruf_sha1_update(ctx, length_be, 8);

    /* Output digest big-endian */
    for(int i = 0; i < 5; i++)
    {
        digest[i * 4]     = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }

    /* Wipe context (except state in case reused) */
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

AARU_EXPORT void AARU_CALL aaruf_sha1_buffer(const void *data, unsigned long size, unsigned char *result)
{
    sha1_ctx ctx;
    aaruf_sha1_init(&ctx);
    aaruf_sha1_update(&ctx, data, size);
    aaruf_sha1_final(&ctx, result);
}
