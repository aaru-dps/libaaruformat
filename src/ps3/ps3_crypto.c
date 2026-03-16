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
 * PS3 disc encryption: key derivation, sector encrypt/decrypt, IV derivation.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <aaruformat.h>

#include "../lib/aes128.h"
#include "ps3_crypto.h"
#include "ps3_encryption_map.h"

/* PS3 Encryption Round Key — publicly known constant */
static const uint8_t PS3_ERK[16] = {0x38, 0x0B, 0xCF, 0x0B, 0x53, 0x45, 0x5B, 0x3C,
                                    0x78, 0x17, 0xAB, 0x4F, 0xA3, 0xBA, 0x90, 0xED};

/* PS3 ERK IV — publicly known constant */
static const uint8_t PS3_ERK_IV[16] = {0x69, 0x47, 0x47, 0x72, 0xAF, 0x6F, 0xDA, 0xB3,
                                       0x42, 0x74, 0x3A, 0xEF, 0xAA, 0x18, 0x62, 0x87};

void ps3_derive_disc_key(const uint8_t data1[16], uint8_t disc_key[16])
{
    memcpy(disc_key, data1, 16);
    aes128_cbc_encrypt(PS3_ERK, PS3_ERK_IV, disc_key, 16);
}

void ps3_derive_iv(uint64_t sector_num, uint8_t iv[16])
{
    memset(iv, 0, 16);

    for(int i = 15; i >= 0 && sector_num > 0; i--)
    {
        iv[i] = (uint8_t)(sector_num & 0xFF);
        sector_num >>= 8;
    }
}

void ps3_encrypt_sector(const uint8_t disc_key[16], uint64_t sector_num, uint8_t *data, uint32_t length)
{
    uint8_t iv[16];
    ps3_derive_iv(sector_num, iv);
    aes128_cbc_encrypt(disc_key, iv, data, length);
}

void ps3_decrypt_sector(const uint8_t disc_key[16], uint64_t sector_num, uint8_t *data, uint32_t length)
{
    uint8_t iv[16];
    ps3_derive_iv(sector_num, iv);
    aes128_cbc_decrypt(disc_key, iv, data, length);
}

void ps3_lazy_init(aaruformat_context *ctx)
{
    if(ctx == NULL) return;

    /* Read disc key from media tags */
    if(ctx->ps3_disc_key == NULL)
    {
        mediaTagEntry *item = NULL;
        int32_t        tag  = kMediaTagPs3DiscKey;
        HASH_FIND_INT(ctx->mediaTags, &tag, item);

        if(item != NULL && item->length == 16)
        {
            ctx->ps3_disc_key = (uint8_t *)malloc(16);

            if(ctx->ps3_disc_key != NULL) memcpy(ctx->ps3_disc_key, item->data, 16);
        }
        else
        {
            /* Try deriving from data1 */
            tag = kMediaTagPs3Data1;
            HASH_FIND_INT(ctx->mediaTags, &tag, item);

            if(item != NULL && item->length == 16)
            {
                ctx->ps3_disc_key = (uint8_t *)malloc(16);

                if(ctx->ps3_disc_key != NULL) ps3_derive_disc_key(item->data, ctx->ps3_disc_key);
            }
        }
    }

    /* Read and deserialize encryption map from media tags */
    if(ctx->ps3_plaintext_regions == NULL)
    {
        mediaTagEntry *item = NULL;
        int32_t        tag  = kMediaTagPs3EncryptionMap;
        HASH_FIND_INT(ctx->mediaTags, &tag, item);

        if(item != NULL && item->length >= 4)
        {
            Ps3PlaintextRegion *regions = NULL;
            uint32_t            count   = 0;

            if(ps3_deserialize_encryption_map(item->data, item->length, &regions, &count) == 0)
            {
                ctx->ps3_plaintext_regions      = regions;
                ctx->ps3_plaintext_region_count = count;
            }
        }
    }
}
