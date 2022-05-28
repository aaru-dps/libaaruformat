/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2022 Natalia Portillo.
 * Copyright (C) 2002 Andrew Tridgell <tridge@samba.org>
 * Copyright (C) 2006 ManTech International Corporation
 * Copyright (C) 2013 Helmut Grohne <helmut@subdivi.de>
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

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <aaruformat.h>

#include "spamsum.h"

static uint8_t b64[] = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50,
                        0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
                        0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76,
                        0x77, 0x78, 0x79, 0x7A, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x2B, 0x2F};

AARU_EXPORT spamsum_ctx* AARU_CALL aaruf_spamsum_init(void)
{
    spamsum_ctx* ctx = (spamsum_ctx*)malloc(sizeof(spamsum_ctx));
    if(!ctx) return NULL;

    memset(ctx, 0, sizeof(spamsum_ctx));

    ctx->bh_end       = 1;
    ctx->bh[0].h      = HASH_INIT;
    ctx->bh[0].half_h = HASH_INIT;

    return ctx;
}

AARU_EXPORT int AARU_CALL aaruf_spamsum_update(spamsum_ctx* ctx, const uint8_t* data, uint32_t len)
{
    int i;
    if(!ctx || !data) return -1;

    for(i = 0; i < len; i++) fuzzy_engine_step(ctx, data[i]);

    ctx->total_size += len;

    return 0;
}

AARU_EXPORT void AARU_CALL aaruf_spamsum_free(spamsum_ctx* ctx)
{
    if(ctx) free(ctx);
}

#define ROLL_SUM(ctx) ((ctx)->roll.h1 + (ctx)->roll.h2 + (ctx)->roll.h3)
#define SUM_HASH(c, h) (((h)*HASH_PRIME) ^ (c));
#define SSDEEP_BS(index) (MIN_BLOCKSIZE << (index))

AARU_LOCAL inline void fuzzy_engine_step(spamsum_ctx* ctx, uint8_t c)
{
    uint32_t i;
    /* At each character we update the rolling hash and the normal hashes.
     * When the rolling hash hits a reset value then we emit a normal hash
     * as a element of the signature and reset the normal hash. */
    roll_hash(ctx, c);
    uint64_t h = ROLL_SUM(ctx);

    for(i = ctx->bh_start; i < ctx->bh_end; ++i)
    {
        ctx->bh[i].h      = SUM_HASH(c, ctx->bh[i].h);
        ctx->bh[i].half_h = SUM_HASH(c, ctx->bh[i].half_h);
    }

    for(i = ctx->bh_start; i < ctx->bh_end; ++i)
    {
        /* With growing blocksize almost no runs fail the next test. */
        if(h % SSDEEP_BS(i) != SSDEEP_BS(i) - 1)
            /* Once this condition is false for one bs, it is
             * automatically false for all further bs. I.e. if
             * h === -1 (mod 2*bs) then h === -1 (mod bs). */
            break;

        /* We have hit a reset point. We now emit hashes which are
         * based on all characters in the piece of the message between
         * the last reset point and this one */
        if(0 == ctx->bh[i].d_len) fuzzy_try_fork_blockhash(ctx);

        ctx->bh[i].digest[ctx->bh[i].d_len] = b64[ctx->bh[i].h % 64];
        ctx->bh[i].half_digest              = b64[ctx->bh[i].half_h % 64];

        if(ctx->bh[i].d_len < SPAMSUM_LENGTH - 1)
        {
            /* We can have a problem with the tail overflowing. The
             * easiest way to cope with this is to only reset the
             * normal hash if we have room for more characters in
             * our signature. This has the effect of combining the
             * last few pieces of the message into a single piece
             * */
            ctx->bh[i].digest[++ctx->bh[i].d_len] = 0;
            ctx->bh[i].h                          = HASH_INIT;

            if(ctx->bh[i].d_len >= SPAMSUM_LENGTH / 2) continue;

            ctx->bh[i].half_h      = HASH_INIT;
            ctx->bh[i].half_digest = 0;
        }
        else
            fuzzy_try_reduce_blockhash(ctx);
    }
}

AARU_LOCAL inline void roll_hash(spamsum_ctx* ctx, uint8_t c)
{
    ctx->roll.h2 -= ctx->roll.h1;
    ctx->roll.h2 += ROLLING_WINDOW * c;

    ctx->roll.h1 += c;
    ctx->roll.h1 -= ctx->roll.window[ctx->roll.n % ROLLING_WINDOW];

    ctx->roll.window[ctx->roll.n % ROLLING_WINDOW] = c;
    ctx->roll.n++;

    /* The original spamsum AND'ed this value with 0xFFFFFFFF which
     * in theory should have no effect. This AND has been removed
     * for performance (jk) */
    ctx->roll.h3 <<= 5;
    ctx->roll.h3 ^= c;
}

AARU_LOCAL inline void fuzzy_try_reduce_blockhash(spamsum_ctx* ctx)
{
    // assert(ctx->bh_start < ctx->bh_end);

    if(ctx->bh_end - ctx->bh_start < 2) /* Need at least two working hashes. */
        return;

    if((uint64_t)SSDEEP_BS(ctx->bh_start) * SPAMSUM_LENGTH >= ctx->total_size)
        /* Initial blocksize estimate would select this or a smaller
         * blocksize. */
        return;

    if(ctx->bh[ctx->bh_start + 1].d_len < SPAMSUM_LENGTH / 2) /* Estimate adjustment would select this blocksize. */
        return;

    /* At this point we are clearly no longer interested in the
     * start_blocksize. Get rid of it. */
    ++ctx->bh_start;
}

AARU_LOCAL inline void fuzzy_try_fork_blockhash(spamsum_ctx* ctx)
{
    if(ctx->bh_end >= NUM_BLOCKHASHES) return;

    // assert(ctx->bh_end != 0);

    uint32_t obh             = ctx->bh_end - 1;
    uint32_t nbh             = ctx->bh_end;
    ctx->bh[nbh].h           = ctx->bh[obh].h;
    ctx->bh[nbh].half_h      = ctx->bh[obh].half_h;
    ctx->bh[nbh].digest[0]   = 0;
    ctx->bh[nbh].half_digest = 0;
    ctx->bh[nbh].d_len       = 0;
    ++ctx->bh_end;
}

AARU_EXPORT int AARU_CALL aaruf_spamsum_final(spamsum_ctx* ctx, uint8_t* result)
{
    uint32_t bi     = ctx->bh_start;
    uint32_t h      = ROLL_SUM(ctx);
    int      remain = (int)(FUZZY_MAX_RESULT - 1); /* Exclude terminating '\0'. */

    if(!result) return -1;

    /* Verify that our elimination was not overeager. */
    // assert(bi == 0 || (uint64_t)SSDEEP_BS(bi) / 2 * SPAMSUM_LENGTH < ctx->total_size);

    /* Initial blocksize guess. */
    while((uint64_t)SSDEEP_BS(bi) * SPAMSUM_LENGTH < ctx->total_size)
    {
        ++bi;

        if(bi >= NUM_BLOCKHASHES)
        {
            errno = EOVERFLOW;
            return -1;
        }
    }

    /* Adapt blocksize guess to actual digest length. */
    while(bi >= ctx->bh_end) --bi;

    while(bi > ctx->bh_start && ctx->bh[bi].d_len < SPAMSUM_LENGTH / 2) --bi;

    // assert(!(bi > 0 && ctx->bh[bi].d_len < SPAMSUM_LENGTH / 2));

    int i = snprintf((char*)result, (size_t)remain, "%lu:", (unsigned long)SSDEEP_BS(bi));

    if(i <= 0) /* Maybe snprintf has set errno here? */
        return -1;

    // assert(i < remain);

    remain -= i;
    result += i;

    i = (int)ctx->bh[bi].d_len;

    // assert(i <= remain);

    memcpy(result, ctx->bh[bi].digest, (size_t)i);
    result += i;
    remain -= i;

    if(h != 0)
    {
        // assert(remain > 0);

        *result = b64[ctx->bh[bi].h % 64];

        if(i < 3 || *result != result[-1] || *result != result[-2] || *result != result[-3])
        {
            ++result;
            --remain;
        }
    }
    else if(ctx->bh[bi].digest[i] != 0)
    {
        // assert(remain > 0);

        *result = ctx->bh[bi].digest[i];

        if(i < 3 || *result != result[-1] || *result != result[-2] || *result != result[-3])
        {
            ++result;
            --remain;
        }
    }

    // assert(remain > 0);

    *result++ = ':';
    --remain;

    if(bi < ctx->bh_end - 1)
    {
        ++bi;
        i = (int)ctx->bh[bi].d_len;

        if(i <= remain)
            ;

        memcpy(result, ctx->bh[bi].digest, (size_t)i);
        result += i;
        remain -= i;

        if(h != 0)
        {
            // assert(remain > 0);

            h       = ctx->bh[bi].half_h;
            *result = b64[h % 64];

            if(i < 3 || *result != result[-1] || *result != result[-2] || *result != result[-3])
            {
                ++result;
                --remain;
            }
        }
        else
        {
            i = ctx->bh[bi].half_digest;

            if(i != 0)
            {
                // assert(remain > 0);

                *result = (uint8_t)i;

                if(i < 3 || *result != result[-1] || *result != result[-2] || *result != result[-3])
                {
                    ++result;
                    --remain;
                }
            }
        }
    }
    else if(h != 0)
    {
        // assert(ctx->bh[bi].d_len == 0);

        // assert(remain > 0);

        *result++ = b64[ctx->bh[bi].h % 64];
        /* No need to bother with FUZZY_FLAG_ELIMSEQ, because this
         * digest has length 1. */
        --remain;
    }

    *result = 0;

    return 0;
}
