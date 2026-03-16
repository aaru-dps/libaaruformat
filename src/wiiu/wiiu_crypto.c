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
 * Wii U disc encryption: partition key map, encrypt/decrypt, serialization.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <aaruformat.h>

#include "../lib/aes128.h"
#include "wiiu_crypto.h"

/* Read a little-endian uint32 from a byte buffer. */
static uint32_t read_le32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

/* Write a little-endian uint32 to a byte buffer. */
static void write_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

const uint8_t *wiiu_get_sector_key(const WiiuPartitionRegion *regions, uint32_t region_count, uint64_t logical_sector)
{
    if(regions == NULL || region_count == 0) return NULL;

    /* Convert logical (2048-byte) sector to physical (0x8000-byte) sector */
    uint64_t phys_sector = logical_sector / WIIU_LOGICAL_PER_PHYSICAL;

    /* Disc header sectors (0-2) are always plaintext */
    if(phys_sector < WIIU_HEADER_PHYSICAL_SECTORS) return NULL;

    for(uint32_t i = 0; i < region_count; i++)
    {
        if(phys_sector >= regions[i].start_sector && phys_sector < regions[i].end_sector)
        {
            /* Partition header sector is plaintext */
            if(phys_sector == regions[i].start_sector) return NULL;

            /* Encrypted with this partition's key */
            return regions[i].key;
        }
    }

    /* Outside any known partition — treat as plaintext */
    return NULL;
}

bool wiiu_is_sector_encrypted(const WiiuPartitionRegion *regions, uint32_t region_count, uint64_t logical_sector)
{ return wiiu_get_sector_key(regions, region_count, logical_sector) != NULL; }

void wiiu_encrypt_physical_sector(const uint8_t key[16], uint8_t *data, uint32_t length)
{
    uint8_t iv[16];
    memset(iv, 0, sizeof(iv));
    aes128_cbc_encrypt(key, iv, data, length);
}

void wiiu_decrypt_physical_sector(const uint8_t key[16], uint8_t *data, uint32_t length)
{
    uint8_t iv[16];
    memset(iv, 0, sizeof(iv));
    aes128_cbc_decrypt(key, iv, data, length);
}

int32_t wiiu_serialize_partition_key_map(const WiiuPartitionRegion *regions, uint32_t count, uint8_t **out_data,
                                         uint32_t *out_len)
{
    if(out_data == NULL || out_len == NULL) return -1;
    if(count > WIIU_MAX_PARTITIONS) return -3;

    /* 4 bytes for count + 24 bytes per entry (4 start + 4 end + 16 key) */
    uint32_t size = 4 + count * 24;
    uint8_t *buf  = (uint8_t *)malloc(size);
    if(buf == NULL) return -4;

    write_le32(buf, count);

    for(uint32_t i = 0; i < count; i++)
    {
        uint32_t offset = 4 + i * 24;
        write_le32(buf + offset, regions[i].start_sector);
        write_le32(buf + offset + 4, regions[i].end_sector);
        memcpy(buf + offset + 8, regions[i].key, 16);
    }

    *out_data = buf;
    *out_len  = size;
    return 0;
}

int32_t wiiu_deserialize_partition_key_map(const uint8_t *data, uint32_t data_len, WiiuPartitionRegion **regions,
                                           uint32_t *count)
{
    if(data == NULL || regions == NULL || count == NULL) return -1;
    if(data_len < 4) return -2;

    uint32_t region_count = read_le32(data);

    if(region_count > WIIU_MAX_PARTITIONS) return -3;

    if(region_count == 0)
    {
        *regions = NULL;
        *count   = 0;
        return 0;
    }

    uint32_t required = 4 + region_count * 24;
    if(data_len < required) return -2;

    WiiuPartitionRegion *r = (WiiuPartitionRegion *)malloc(region_count * sizeof(WiiuPartitionRegion));
    if(r == NULL) return -4;

    for(uint32_t i = 0; i < region_count; i++)
    {
        uint32_t offset   = 4 + i * 24;
        r[i].start_sector = read_le32(data + offset);
        r[i].end_sector   = read_le32(data + offset + 4);
        memcpy(r[i].key, data + offset + 8, 16);

        if(r[i].start_sector >= r[i].end_sector)
        {
            memset(r, 0, region_count * sizeof(WiiuPartitionRegion));
            free(r);
            return -5;
        }
    }

    *regions = r;
    *count   = region_count;
    return 0;
}

void wiiu_lazy_init(aaruformat_context *ctx)
{
    if(ctx == NULL) return;

    /* Read disc key from media tags */
    if(ctx->wiiu_disc_key == NULL)
    {
        mediaTagEntry *item = NULL;
        int32_t        tag  = kMediaTagWiiUDiscKey;
        HASH_FIND_INT(ctx->mediaTags, &tag, item);

        if(item != NULL && item->length == 16)
        {
            ctx->wiiu_disc_key = (uint8_t *)malloc(16);

            if(ctx->wiiu_disc_key != NULL) memcpy(ctx->wiiu_disc_key, item->data, 16);
        }
    }

    /* Read and deserialize partition key map from media tags */
    if(ctx->wiiu_partition_regions == NULL)
    {
        mediaTagEntry *item = NULL;
        int32_t        tag  = kMediaTagWiiUPartitionKeyMap;
        HASH_FIND_INT(ctx->mediaTags, &tag, item);

        if(item != NULL && item->length >= 4)
        {
            WiiuPartitionRegion *regions = NULL;
            uint32_t             count   = 0;

            if(wiiu_deserialize_partition_key_map(item->data, item->length, &regions, &count) == 0)
            {
                ctx->wiiu_partition_regions      = regions;
                ctx->wiiu_partition_region_count = count;
            }
        }
    }

    /* Allocate the encrypted block cache if needed */
    if(ctx->wiiu_encrypted_block_cache == NULL)
    {
        ctx->wiiu_encrypted_block_cache = (uint8_t *)malloc(WIIU_CRYPTO_SECTOR_SIZE);
        ctx->wiiu_cache_valid           = false;
    }
}
