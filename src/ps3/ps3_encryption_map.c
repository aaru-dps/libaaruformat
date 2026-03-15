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
 * PS3 encryption map: plaintext region parsing, serialization, and lookup.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ps3_encryption_map.h"

/* Read a big-endian uint32 from a byte buffer. */
static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Read a little-endian uint32 from a byte buffer. */
static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Write a little-endian uint32 to a byte buffer. */
static void write_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

int32_t ps3_parse_encryption_map(const uint8_t *sector0, uint32_t length, Ps3PlaintextRegion **regions,
                                 uint32_t *count)
{
    if(sector0 == NULL || regions == NULL || count == NULL) return -1;

    /* Minimum required: 4 (region_count) + 4 (unknown) = 8 bytes */
    if(length < 8) return -2;

    uint32_t region_count = read_be32(sector0);

    if(region_count > PS3_MAX_PLAINTEXT_REGIONS) return -3;

    if(region_count == 0)
    {
        *regions = NULL;
        *count   = 0;
        return 0;
    }

    /* Need 8 + region_count * 8 bytes */
    uint32_t required = 8 + region_count * 8;
    if(length < required) return -2;

    Ps3PlaintextRegion *r = (Ps3PlaintextRegion *)malloc(region_count * sizeof(Ps3PlaintextRegion));
    if(r == NULL) return -4;

    const uint8_t *ptr = sector0 + 8; /* skip region_count (4) + unknown (4) */

    for(uint32_t i = 0; i < region_count; i++)
    {
        r[i].start_sector = read_be32(ptr);
        r[i].end_sector   = read_be32(ptr + 4);
        ptr += 8;

        if(r[i].start_sector > r[i].end_sector)
        {
            free(r);
            return -5;
        }
    }

    *regions = r;
    *count   = region_count;
    return 0;
}

int32_t ps3_serialize_encryption_map(const Ps3PlaintextRegion *regions, uint32_t count, uint8_t **out_data,
                                     uint32_t *out_length)
{
    if(out_data == NULL || out_length == NULL) return -1;
    if(count > PS3_MAX_PLAINTEXT_REGIONS) return -3;

    uint32_t size = 4 + count * 8;
    uint8_t *buf  = (uint8_t *)malloc(size);
    if(buf == NULL) return -4;

    write_le32(buf, count);

    for(uint32_t i = 0; i < count; i++)
    {
        write_le32(buf + 4 + i * 8, regions[i].start_sector);
        write_le32(buf + 4 + i * 8 + 4, regions[i].end_sector);
    }

    *out_data   = buf;
    *out_length = size;
    return 0;
}

int32_t ps3_deserialize_encryption_map(const uint8_t *data, uint32_t length, Ps3PlaintextRegion **regions,
                                       uint32_t *count)
{
    if(data == NULL || regions == NULL || count == NULL) return -1;
    if(length < 4) return -2;

    uint32_t region_count = read_le32(data);

    if(region_count > PS3_MAX_PLAINTEXT_REGIONS) return -3;

    if(region_count == 0)
    {
        *regions = NULL;
        *count   = 0;
        return 0;
    }

    uint32_t required = 4 + region_count * 8;
    if(length < required) return -2;

    Ps3PlaintextRegion *r = (Ps3PlaintextRegion *)malloc(region_count * sizeof(Ps3PlaintextRegion));
    if(r == NULL) return -4;

    for(uint32_t i = 0; i < region_count; i++)
    {
        r[i].start_sector = read_le32(data + 4 + i * 8);
        r[i].end_sector   = read_le32(data + 4 + i * 8 + 4);
    }

    *regions = r;
    *count   = region_count;
    return 0;
}

bool ps3_is_sector_encrypted(const Ps3PlaintextRegion *plaintext_regions, uint32_t region_count,
                             uint64_t sector_address)
{
    if(plaintext_regions == NULL || region_count == 0) return true;

    for(uint32_t i = 0; i < region_count; i++)
        if(sector_address >= plaintext_regions[i].start_sector && sector_address <= plaintext_regions[i].end_sector)
            return false;

    return true;
}
