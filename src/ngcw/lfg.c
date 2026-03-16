/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <https://www.gnu.org/licenses/>.
 *
 * Lagged Fibonacci Generator for Nintendo GameCube/Wii junk fill.
 * Based on Dolphin emulator's LaggedFibonacciGenerator (CC0 licensed).
 */

#include "lfg.h"

#include <string.h>

static inline uint32_t swap32(uint32_t x)
{ return ((x >> 24) & 0xFF) | ((x >> 8) & 0xFF00) | ((x << 8) & 0xFF0000) | ((x << 24) & 0xFF000000); }

static void lfg_forward(struct ngc_lfg_ctx *ctx)
{
    for(size_t i = 0; i < NGC_LFG_J; i++) ctx->buffer[i] ^= ctx->buffer[i + NGC_LFG_K - NGC_LFG_J];

    for(size_t i = NGC_LFG_J; i < NGC_LFG_K; i++) ctx->buffer[i] ^= ctx->buffer[i - NGC_LFG_J];
}

static void lfg_backward(struct ngc_lfg_ctx *ctx, size_t start_word, size_t end_word)
{
    size_t loop_end = NGC_LFG_J > start_word ? NGC_LFG_J : start_word;
    size_t upper    = end_word < NGC_LFG_K ? end_word : NGC_LFG_K;

    for(size_t i = upper; i > loop_end; --i) ctx->buffer[i - 1] ^= ctx->buffer[i - 1 - NGC_LFG_J];

    size_t upper2 = end_word < NGC_LFG_J ? end_word : NGC_LFG_J;

    for(size_t i = upper2; i > start_word; --i) ctx->buffer[i - 1] ^= ctx->buffer[i - 1 + NGC_LFG_K - NGC_LFG_J];
}

static bool lfg_initialize(struct ngc_lfg_ctx *ctx, bool check_existing)
{
    for(size_t i = NGC_LFG_SEED_SIZE; i < NGC_LFG_K; i++)
    {
        uint32_t calculated = (ctx->buffer[i - 17] << 23) ^ (ctx->buffer[i - 16] >> 9) ^ ctx->buffer[i - 1];

        if(check_existing)
        {
            uint32_t actual = (ctx->buffer[i] & 0xFF00FFFF) | (ctx->buffer[i] << 2 & 0x00FC0000);

            if((calculated & 0xFFFCFFFF) != actual) return false;
        }

        ctx->buffer[i] = calculated;
    }

    /* Apply the shift-by-18-instead-of-16 quirk + byteswap */
    for(size_t i = 0; i < NGC_LFG_K; i++)
        ctx->buffer[i] = swap32((ctx->buffer[i] & 0xFF00FFFF) | ((ctx->buffer[i] >> 2) & 0x00FF0000));

    for(int i = 0; i < 4; i++) lfg_forward(ctx);

    return true;
}

void ngc_lfg_set_seed(struct ngc_lfg_ctx *ctx, const uint32_t seed[NGC_LFG_SEED_SIZE])
{
    ctx->position_bytes = 0;

    for(size_t i = 0; i < NGC_LFG_SEED_SIZE; i++) ctx->buffer[i] = swap32(seed[i]);

    lfg_initialize(ctx, false);
}

void ngc_lfg_get_bytes(struct ngc_lfg_ctx *ctx, uint8_t *out, size_t count)
{
    while(count > 0)
    {
        size_t avail = NGC_LFG_K * sizeof(uint32_t) - ctx->position_bytes;
        size_t chunk = count < avail ? count : avail;

        memcpy(out, (uint8_t *)ctx->buffer + ctx->position_bytes, chunk);

        ctx->position_bytes += chunk;
        count -= chunk;
        out += chunk;

        if(ctx->position_bytes == NGC_LFG_K * sizeof(uint32_t))
        {
            lfg_forward(ctx);
            ctx->position_bytes = 0;
        }
    }
}

static bool lfg_reinitialize(struct ngc_lfg_ctx *ctx, uint32_t seed_out[NGC_LFG_SEED_SIZE])
{
    for(int i = 0; i < 4; i++) lfg_backward(ctx, 0, NGC_LFG_K);

    for(size_t i = 0; i < NGC_LFG_K; i++) ctx->buffer[i] = swap32(ctx->buffer[i]);

    /* Reconstruct bits lost by the shift-by-18-instead-of-16 quirk */
    for(size_t i = 0; i < NGC_LFG_SEED_SIZE; i++)
    {
        ctx->buffer[i] = (ctx->buffer[i] & 0xFF00FFFF) | (ctx->buffer[i] << 2 & 0x00FC0000) |
                         ((ctx->buffer[i + 16] ^ ctx->buffer[i + 15]) << 9 & 0x00030000);
    }

    for(size_t i = 0; i < NGC_LFG_SEED_SIZE; i++) seed_out[i] = swap32(ctx->buffer[i]);

    return lfg_initialize(ctx, true);
}

size_t ngc_lfg_get_seed(const uint8_t *data, size_t size, size_t data_offset, uint32_t seed_out[NGC_LFG_SEED_SIZE])
{
    /* Alignment: data - data_offset must be 4-byte aligned */
    if(((uintptr_t)data - data_offset) % sizeof(uint32_t) != 0) return 0;

    /* Work on whole u32 words */
    size_t bytes_to_skip = ((data_offset + 3) & ~(size_t)3) - data_offset;

    if(bytes_to_skip > size) return 0;

    const uint32_t *u32_data        = (const uint32_t *)(data + bytes_to_skip);
    size_t          u32_size        = (size - bytes_to_skip) / sizeof(uint32_t);
    size_t          u32_data_offset = (data_offset + bytes_to_skip) / sizeof(uint32_t);

    if(u32_size < NGC_LFG_K) return 0;

    /* Quick sanity check: the top bits have a specific pattern from the shift quirk */
    for(size_t i = 0; i < NGC_LFG_K; i++)
    {
        uint32_t x = swap32(u32_data[i]);

        if((x & 0x00C00000) != (x >> 2 & 0x00C00000)) return 0;
    }

    struct ngc_lfg_ctx lfg;
    size_t             data_offset_mod_k = u32_data_offset % NGC_LFG_K;
    size_t             data_offset_div_k = u32_data_offset / NGC_LFG_K;

    /* Place the data into the buffer at the correct position.
     * Copy raw native-endian u32 values — NO byte-swapping here.
     * The swap happens later inside lfg_reinitialize/lfg_initialize. */
    size_t first_part = NGC_LFG_K - data_offset_mod_k;

    if(first_part > NGC_LFG_K) first_part = NGC_LFG_K;

    for(size_t i = 0; i < first_part && i < NGC_LFG_K; i++) lfg.buffer[data_offset_mod_k + i] = u32_data[i];

    for(size_t i = 0; i < data_offset_mod_k; i++) lfg.buffer[i] = u32_data[first_part + i];

    lfg_backward(&lfg, 0, data_offset_mod_k);

    for(size_t i = 0; i < data_offset_div_k; i++) lfg_backward(&lfg, 0, NGC_LFG_K);

    if(!lfg_reinitialize(&lfg, seed_out)) return 0;

    lfg.position_bytes = data_offset % (NGC_LFG_K * sizeof(uint32_t));

    /* Advance the LFG forward to match the data_offset position. */
    for(size_t i = 0; i < data_offset_div_k; i++) lfg_forward(&lfg);

    /* Count how many bytes from data match the LFG output */
    size_t         result = 0;
    const uint8_t *p      = data;
    const uint8_t *end    = data + size;

    while(p < end)
    {
        uint8_t expected = ((uint8_t *)lfg.buffer)[lfg.position_bytes];

        if(*p != expected) break;

        result++;
        p++;
        lfg.position_bytes++;

        if(lfg.position_bytes == NGC_LFG_K * sizeof(uint32_t))
        {
            lfg_forward(&lfg);
            lfg.position_bytes = 0;
        }
    }

    return result;
}
