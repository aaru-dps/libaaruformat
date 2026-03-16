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
 * Nintendo GameCube/Wii junk map: serialization, deserialization, regeneration.
 */

#include <stdlib.h>
#include <string.h>

#include <aaruformat.h>

#include "ngcw_junk.h"

/* ---- Little-endian helpers ---- */

static uint16_t read_le16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }

static uint32_t read_le32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static uint64_t read_le64(const uint8_t *p) { return (uint64_t)read_le32(p) | ((uint64_t)read_le32(p + 4) << 32); }

static void write_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void write_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void write_le64(uint8_t *p, uint64_t v)
{
    write_le32(p, (uint32_t)(v & 0xFFFFFFFF));
    write_le32(p + 4, (uint32_t)(v >> 32));
}

/* ---- Serialization ---- */

/*
 * Format:
 *   [2] version     (uint16 LE)
 *   [4] entry_count (uint32 LE)
 *   [2] seed_size   (uint16 LE) — NGC_LFG_SEED_SIZE (17)
 *   For each entry:
 *     [8] offset          (uint64 LE)
 *     [8] length          (uint64 LE)
 *     [2] partition_index (uint16 LE)
 *     [seed_size * 4] seed (raw bytes)
 *
 * Header = 8 bytes. Entry = 18 + seed_size * 4 = 86 bytes (when seed_size=17).
 */

#define JUNK_MAP_HEADER_SIZE 8

int32_t ngcw_serialize_junk_map(const NgcwJunkEntry *entries, uint32_t count, uint8_t **out_data, uint32_t *out_len)
{
    if(out_data == NULL || out_len == NULL) return -1;

    uint32_t entry_size = 18 + NGC_LFG_SEED_SIZE * 4;
    uint32_t size       = JUNK_MAP_HEADER_SIZE + count * entry_size;
    uint8_t *buf        = (uint8_t *)malloc(size);

    if(buf == NULL) return -4;

    write_le16(buf, NGCW_JUNK_MAP_VERSION);
    write_le32(buf + 2, count);
    write_le16(buf + 6, NGC_LFG_SEED_SIZE);

    for(uint32_t i = 0; i < count; i++)
    {
        uint8_t *p = buf + JUNK_MAP_HEADER_SIZE + i * entry_size;

        write_le64(p, entries[i].offset);
        write_le64(p + 8, entries[i].length);
        write_le16(p + 16, entries[i].partition_index);
        memcpy(p + 18, entries[i].seed, NGC_LFG_SEED_SIZE * sizeof(uint32_t));
    }

    *out_data = buf;
    *out_len  = size;
    return 0;
}

int32_t ngcw_deserialize_junk_map(const uint8_t *data, uint32_t data_len, NgcwJunkEntry **entries, uint32_t *count,
                                  uint16_t *seed_size)
{
    if(data == NULL || entries == NULL || count == NULL || seed_size == NULL) return -1;

    if(data_len < JUNK_MAP_HEADER_SIZE) return -2;

    uint16_t version   = read_le16(data);
    uint32_t entry_cnt = read_le32(data + 2);
    uint16_t ss        = read_le16(data + 6);

    if(version != NGCW_JUNK_MAP_VERSION) return -3;

    if(ss != NGC_LFG_SEED_SIZE) return -3;

    if(entry_cnt == 0)
    {
        *entries   = NULL;
        *count     = 0;
        *seed_size = ss;
        return 0;
    }

    uint32_t entry_size = 18 + (uint32_t)ss * 4;
    uint32_t required   = JUNK_MAP_HEADER_SIZE + entry_cnt * entry_size;

    if(data_len < required) return -2;

    NgcwJunkEntry *e = (NgcwJunkEntry *)calloc(entry_cnt, sizeof(NgcwJunkEntry));

    if(e == NULL) return -4;

    for(uint32_t i = 0; i < entry_cnt; i++)
    {
        const uint8_t *p = data + JUNK_MAP_HEADER_SIZE + i * entry_size;

        e[i].offset          = read_le64(p);
        e[i].length          = read_le64(p + 8);
        e[i].partition_index = read_le16(p + 16);
        memcpy(e[i].seed, p + 18, ss * sizeof(uint32_t));
    }

    *entries   = e;
    *count     = entry_cnt;
    *seed_size = ss;
    return 0;
}

/* ---- Junk regeneration ---- */

int ngcw_regenerate_junk_sector(const NgcwJunkEntry *entries, uint32_t entry_count, uint64_t disc_offset,
                                uint8_t *output, uint32_t length)
{
    if(entries == NULL || entry_count == 0 || output == NULL) return -1;

    /* Binary search for the entry containing disc_offset */
    int lo = 0;
    int hi = (int)entry_count - 1;

    while(lo <= hi)
    {
        int      mid       = lo + (hi - lo) / 2;
        uint64_t entry_end = entries[mid].offset + entries[mid].length;

        if(disc_offset >= entry_end)
            lo = mid + 1;
        else if(disc_offset < entries[mid].offset)
            hi = mid - 1;
        else
        {
            /* Found: disc_offset is within entries[mid] */
            uint64_t stream_pos = disc_offset - entries[mid].offset;

            struct ngc_lfg_ctx lfg;
            uint32_t           seed_copy[NGC_LFG_SEED_SIZE];
            memcpy(seed_copy, entries[mid].seed, sizeof(seed_copy));
            ngc_lfg_set_seed(&lfg, seed_copy);

            /* Advance LFG to the correct stream position */
            if(stream_pos > 0)
            {
                uint8_t discard[4096];
                size_t  rem = (size_t)stream_pos;

                while(rem > 0)
                {
                    size_t step = rem > sizeof(discard) ? sizeof(discard) : rem;
                    ngc_lfg_get_bytes(&lfg, discard, step);
                    rem -= step;
                }
            }

            /* Generate the requested bytes */
            ngc_lfg_get_bytes(&lfg, output, length);
            return 0;
        }
    }

    return -1; /* Not found */
}

/* ---- Lazy initialization ---- */

void ngcw_junk_lazy_init(aaruformat_context *ctx)
{
    if(ctx == NULL) return;

    if(ctx->ngcw_junk_entries != NULL) return;

    mediaTagEntry *item = NULL;
    int32_t        tag  = kMediaTagNgcwJunkMap;
    HASH_FIND_INT(ctx->mediaTags, &tag, item);

    if(item == NULL || item->length < JUNK_MAP_HEADER_SIZE) return;

    NgcwJunkEntry *entries   = NULL;
    uint32_t       count     = 0;
    uint16_t       seed_size = 0;

    if(ngcw_deserialize_junk_map(item->data, item->length, &entries, &count, &seed_size) == 0)
    {
        ctx->ngcw_junk_entries     = entries;
        ctx->ngcw_junk_entry_count = count;
        ctx->ngcw_junk_seed_size   = seed_size;
    }
}
