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
 * Nintendo Wii disc encryption: partition key map, group encrypt/decrypt.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <aaruformat.h>

#include "../lib/aes128.h"
#include "wii_crypto.h"

/* ---- Little-endian helpers ---- */

static uint32_t read_le32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static void write_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/* ---- Key lookup ---- */

const uint8_t *wii_get_sector_key(const WiiPartitionRegion *regions, uint32_t region_count, uint64_t logical_sector)
{
    if(regions == NULL || region_count == 0) return NULL;

    /* Convert logical (2048-byte) sector to physical (0x8000-byte) group */
    uint64_t phys_group = logical_sector / WII_LOGICAL_PER_GROUP;

    for(uint32_t i = 0; i < region_count; i++)
    {
        if(phys_group >= regions[i].start_sector && phys_group < regions[i].end_sector)
        {
            /* Partition header group is plaintext */
            if(phys_group == regions[i].start_sector) return NULL;

            return regions[i].key;
        }
    }

    /* Outside any known partition — plaintext */
    return NULL;
}

bool wii_is_sector_encrypted(const WiiPartitionRegion *regions, uint32_t region_count, uint64_t logical_sector)
{ return wii_get_sector_key(regions, region_count, logical_sector) != NULL; }

/* ---- Group encrypt/decrypt ---- */

void wii_encrypt_group(const uint8_t key[16], const uint8_t *hash_block, const uint8_t *data_in, uint8_t *out)
{
    /* Hash block: first 0x400 bytes, IV = all zeros */
    uint8_t iv[16];
    memset(iv, 0, sizeof(iv));
    memcpy(out, hash_block, WII_GROUP_HASH_SIZE);
    aes128_cbc_encrypt(key, iv, out, WII_GROUP_HASH_SIZE);

    /* Data block: next 0x7C00 bytes.
     * IV = bytes 0x3D0..0x3DF of the ENCRYPTED hash output (just written to out). */
    uint8_t data_iv[16];
    memcpy(data_iv, out + 0x3D0, 16);
    memcpy(out + WII_GROUP_HASH_SIZE, data_in, WII_GROUP_DATA_SIZE);
    aes128_cbc_encrypt(key, data_iv, out + WII_GROUP_HASH_SIZE, WII_GROUP_DATA_SIZE);
}

void wii_decrypt_group(const uint8_t key[16], const uint8_t *in, uint8_t *hash_block, uint8_t *data_out)
{
    /* Hash block: first 0x400 bytes, IV = all zeros */
    uint8_t iv[16];
    memset(iv, 0, sizeof(iv));
    memcpy(hash_block, in, WII_GROUP_HASH_SIZE);
    aes128_cbc_decrypt(key, iv, hash_block, WII_GROUP_HASH_SIZE);

    /* Data block: next 0x7C00 bytes.
     * IV = bytes 0x3D0..0x3DF of the ENCRYPTED input (not the decrypted hash block). */
    uint8_t data_iv[16];
    memcpy(data_iv, in + 0x3D0, 16);
    memcpy(data_out, in + WII_GROUP_HASH_SIZE, WII_GROUP_DATA_SIZE);
    aes128_cbc_decrypt(key, data_iv, data_out, WII_GROUP_DATA_SIZE);
}

/* ---- Serialization (same format as Wii U) ---- */

int32_t wii_serialize_partition_key_map(const WiiPartitionRegion *regions, uint32_t count, uint8_t **out_data,
                                        uint32_t *out_len)
{
    if(out_data == NULL || out_len == NULL) return -1;

    if(count > WII_MAX_PARTITIONS) return -3;

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

int32_t wii_deserialize_partition_key_map(const uint8_t *data, uint32_t data_len, WiiPartitionRegion **regions,
                                          uint32_t *count)
{
    if(data == NULL || regions == NULL || count == NULL) return -1;

    if(data_len < 4) return -2;

    uint32_t region_count = read_le32(data);

    if(region_count > WII_MAX_PARTITIONS) return -3;

    if(region_count == 0)
    {
        *regions = NULL;
        *count   = 0;
        return 0;
    }

    uint32_t required = 4 + region_count * 24;

    if(data_len < required) return -2;

    WiiPartitionRegion *r = (WiiPartitionRegion *)malloc(region_count * sizeof(WiiPartitionRegion));

    if(r == NULL) return -4;

    for(uint32_t i = 0; i < region_count; i++)
    {
        uint32_t offset   = 4 + i * 24;
        r[i].start_sector = read_le32(data + offset);
        r[i].end_sector   = read_le32(data + offset + 4);
        memcpy(r[i].key, data + offset + 8, 16);

        if(r[i].start_sector >= r[i].end_sector)
        {
            memset(r, 0, region_count * sizeof(WiiPartitionRegion));
            free(r);
            return -5;
        }
    }

    *regions = r;
    *count   = region_count;
    return 0;
}

/* ---- Lazy initialization ---- */

void wii_lazy_init(aaruformat_context *ctx)
{
    if(ctx == NULL) return;

    /* Read and deserialize partition key map from media tags */
    if(ctx->wii_partition_regions == NULL)
    {
        mediaTagEntry *item = NULL;
        int32_t        tag  = kMediaTagWiiPartitionKeyMap;
        HASH_FIND_INT(ctx->mediaTags, &tag, item);

        if(item != NULL && item->length >= 4)
        {
            WiiPartitionRegion *regions = NULL;
            uint32_t            count   = 0;

            if(wii_deserialize_partition_key_map(item->data, item->length, &regions, &count) == 0)
            {
                ctx->wii_partition_regions      = regions;
                ctx->wii_partition_region_count = count;
            }
        }
    }

    /* Allocate the encrypted group cache if needed */
    if(ctx->wii_encrypted_group_cache == NULL)
    {
        ctx->wii_encrypted_group_cache = (uint8_t *)malloc(WII_GROUP_SIZE);
        ctx->wii_cache_valid           = false;
    }
}
