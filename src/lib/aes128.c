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
 * AES-128 CBC encrypt/decrypt implementation.
 * Derived from public domain tiny-AES-c by kokke.
 */

#include <stdint.h>
#include <string.h>

#include "aes128.h"

#define AES_BLOCK_SIZE   16
#define AES_KEY_SIZE     16
#define AES_NUM_ROUNDS   10
#define AES_KEY_EXP_SIZE 176 /* 16 * (10 + 1) */

/* S-box */
static const uint8_t sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9,
    0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f,
    0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07,
    0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3,
    0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58,
    0xcf, 0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3,
    0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec, 0x5f,
    0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73, 0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
    0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac,
    0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a,
    0xae, 0x08, 0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a, 0x70,
    0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11,
    0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf, 0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42,
    0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

/* Inverse S-box */
static const uint8_t rsbox[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb, 0x7c, 0xe3, 0x39,
    0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb, 0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2,
    0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e, 0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76,
    0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25, 0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc,
    0x5d, 0x65, 0xb6, 0x92, 0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d,
    0x84, 0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06, 0xd0, 0x2c,
    0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b, 0x3a, 0x91, 0x11, 0x41, 0x4f,
    0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73, 0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85,
    0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e, 0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62,
    0x0e, 0xaa, 0x18, 0xbe, 0x1b, 0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd,
    0x5a, 0xf4, 0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f, 0x60,
    0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef, 0xa0, 0xe0, 0x3b, 0x4d,
    0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61, 0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6,
    0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d};

/* Round constant */
static const uint8_t rcon[11] = {0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};

typedef struct
{
    uint8_t round_key[AES_KEY_EXP_SIZE];
} aes128_ctx;

static uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ (((x >> 7) & 1) * 0x1b)); }

static void key_expansion(aes128_ctx *ctx, const uint8_t key[16])
{
    uint8_t tempa[4];

    memcpy(ctx->round_key, key, AES_KEY_SIZE);

    for(int i = 4; i < 4 * (AES_NUM_ROUNDS + 1); i++)
    {
        int k = (i - 1) * 4;

        tempa[0] = ctx->round_key[k + 0];
        tempa[1] = ctx->round_key[k + 1];
        tempa[2] = ctx->round_key[k + 2];
        tempa[3] = ctx->round_key[k + 3];

        if(i % 4 == 0)
        {
            /* RotWord */
            uint8_t u8tmp = tempa[0];
            tempa[0]      = tempa[1];
            tempa[1]      = tempa[2];
            tempa[2]      = tempa[3];
            tempa[3]      = u8tmp;

            /* SubWord */
            tempa[0] = sbox[tempa[0]];
            tempa[1] = sbox[tempa[1]];
            tempa[2] = sbox[tempa[2]];
            tempa[3] = sbox[tempa[3]];

            tempa[0] ^= rcon[i / 4];
        }

        int j                 = i * 4;
        int m                 = (i - 4) * 4;
        ctx->round_key[j + 0] = ctx->round_key[m + 0] ^ tempa[0];
        ctx->round_key[j + 1] = ctx->round_key[m + 1] ^ tempa[1];
        ctx->round_key[j + 2] = ctx->round_key[m + 2] ^ tempa[2];
        ctx->round_key[j + 3] = ctx->round_key[m + 3] ^ tempa[3];
    }
}

static void add_round_key(uint8_t round, uint8_t state[4][4], const uint8_t *round_key)
{
    for(int i = 0; i < 4; i++)
        for(int j = 0; j < 4; j++) state[i][j] ^= round_key[(round * 16) + (i * 4) + j];
}

static void sub_bytes(uint8_t state[4][4])
{
    for(int i = 0; i < 4; i++)
        for(int j = 0; j < 4; j++) state[j][i] = sbox[state[j][i]];
}

static void inv_sub_bytes(uint8_t state[4][4])
{
    for(int i = 0; i < 4; i++)
        for(int j = 0; j < 4; j++) state[j][i] = rsbox[state[j][i]];
}

static void shift_rows(uint8_t state[4][4])
{
    uint8_t temp;

    /* Row 1: shift left by 1 */
    temp        = state[0][1];
    state[0][1] = state[1][1];
    state[1][1] = state[2][1];
    state[2][1] = state[3][1];
    state[3][1] = temp;

    /* Row 2: shift left by 2 */
    temp        = state[0][2];
    state[0][2] = state[2][2];
    state[2][2] = temp;
    temp        = state[1][2];
    state[1][2] = state[3][2];
    state[3][2] = temp;

    /* Row 3: shift left by 3 */
    temp        = state[0][3];
    state[0][3] = state[3][3];
    state[3][3] = state[2][3];
    state[2][3] = state[1][3];
    state[1][3] = temp;
}

static void inv_shift_rows(uint8_t state[4][4])
{
    uint8_t temp;

    /* Row 1: shift right by 1 */
    temp        = state[3][1];
    state[3][1] = state[2][1];
    state[2][1] = state[1][1];
    state[1][1] = state[0][1];
    state[0][1] = temp;

    /* Row 2: shift right by 2 */
    temp        = state[0][2];
    state[0][2] = state[2][2];
    state[2][2] = temp;
    temp        = state[1][2];
    state[1][2] = state[3][2];
    state[3][2] = temp;

    /* Row 3: shift right by 3 */
    temp        = state[0][3];
    state[0][3] = state[1][3];
    state[1][3] = state[2][3];
    state[2][3] = state[3][3];
    state[3][3] = temp;
}

static void mix_columns(uint8_t state[4][4])
{
    for(int i = 0; i < 4; i++)
    {
        uint8_t t   = state[i][0];
        uint8_t tmp = state[i][0] ^ state[i][1] ^ state[i][2] ^ state[i][3];

        uint8_t tm = state[i][0] ^ state[i][1];
        tm         = xtime(tm);
        state[i][0] ^= tm ^ tmp;

        tm = state[i][1] ^ state[i][2];
        tm = xtime(tm);
        state[i][1] ^= tm ^ tmp;

        tm = state[i][2] ^ state[i][3];
        tm = xtime(tm);
        state[i][2] ^= tm ^ tmp;

        tm = state[i][3] ^ t;
        tm = xtime(tm);
        state[i][3] ^= tm ^ tmp;
    }
}

#define MULTIPLY(x, y)                                                              \
    (((y & 1) * x) ^ ((y >> 1 & 1) * xtime(x)) ^ ((y >> 2 & 1) * xtime(xtime(x))) ^ \
     ((y >> 3 & 1) * xtime(xtime(xtime(x)))) ^ ((y >> 4 & 1) * xtime(xtime(xtime(xtime(x))))))

static void inv_mix_columns(uint8_t state[4][4])
{
    for(int i = 0; i < 4; i++)
    {
        uint8_t a = state[i][0];
        uint8_t b = state[i][1];
        uint8_t c = state[i][2];
        uint8_t d = state[i][3];

        state[i][0] = MULTIPLY(a, 14) ^ MULTIPLY(b, 11) ^ MULTIPLY(c, 13) ^ MULTIPLY(d, 9);
        state[i][1] = MULTIPLY(a, 9) ^ MULTIPLY(b, 14) ^ MULTIPLY(c, 11) ^ MULTIPLY(d, 13);
        state[i][2] = MULTIPLY(a, 13) ^ MULTIPLY(b, 9) ^ MULTIPLY(c, 14) ^ MULTIPLY(d, 11);
        state[i][3] = MULTIPLY(a, 11) ^ MULTIPLY(b, 13) ^ MULTIPLY(c, 9) ^ MULTIPLY(d, 14);
    }
}

static void cipher(uint8_t state[4][4], const uint8_t *round_key)
{
    add_round_key(0, state, round_key);

    for(uint8_t round = 1; round < AES_NUM_ROUNDS; round++)
    {
        sub_bytes(state);
        shift_rows(state);
        mix_columns(state);
        add_round_key(round, state, round_key);
    }

    sub_bytes(state);
    shift_rows(state);
    add_round_key(AES_NUM_ROUNDS, state, round_key);
}

static void inv_cipher(uint8_t state[4][4], const uint8_t *round_key)
{
    add_round_key(AES_NUM_ROUNDS, state, round_key);

    for(uint8_t round = AES_NUM_ROUNDS - 1; round > 0; round--)
    {
        inv_shift_rows(state);
        inv_sub_bytes(state);
        add_round_key(round, state, round_key);
        inv_mix_columns(state);
    }

    inv_shift_rows(state);
    inv_sub_bytes(state);
    add_round_key(0, state, round_key);
}

static void xor_block(uint8_t *buf, const uint8_t *xor_val)
{
    for(int i = 0; i < AES_BLOCK_SIZE; i++) buf[i] ^= xor_val[i];
}

void aes128_cbc_encrypt(const uint8_t key[16], const uint8_t iv[16], uint8_t *data, uint32_t length)
{
    aes128_ctx ctx;
    uint8_t    iv_buf[AES_BLOCK_SIZE];

    key_expansion(&ctx, key);
    memcpy(iv_buf, iv, AES_BLOCK_SIZE);

    for(uint32_t i = 0; i < length; i += AES_BLOCK_SIZE)
    {
        xor_block(data + i, iv_buf);
        cipher((uint8_t (*)[4])(data + i), ctx.round_key);
        memcpy(iv_buf, data + i, AES_BLOCK_SIZE);
    }
}

void aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16], uint8_t *data, uint32_t length)
{
    aes128_ctx ctx;
    uint8_t    iv_buf[AES_BLOCK_SIZE];
    uint8_t    next_iv[AES_BLOCK_SIZE];

    key_expansion(&ctx, key);
    memcpy(iv_buf, iv, AES_BLOCK_SIZE);

    for(uint32_t i = 0; i < length; i += AES_BLOCK_SIZE)
    {
        memcpy(next_iv, data + i, AES_BLOCK_SIZE);
        inv_cipher((uint8_t (*)[4])(data + i), ctx.round_key);
        xor_block(data + i, iv_buf);
        memcpy(iv_buf, next_iv, AES_BLOCK_SIZE);
    }
}
