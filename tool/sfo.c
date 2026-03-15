/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * PARAM.SFO parser for PlayStation 3 game metadata.
 *
 * SFO binary format (all fields little-endian):
 *
 *   Header (20 bytes):
 *     [4B] magic:             0x00505346 ("\0PSF")
 *     [4B] version:           e.g. 0x00000101
 *     [4B] key_table_offset:  offset to key string table
 *     [4B] data_table_offset: offset to data value table
 *     [4B] entry_count:       number of index entries
 *
 *   Index table (entry_count × 16 bytes each):
 *     [2B] key_offset:   offset into key table for this entry's name
 *     [2B] data_format:  0x0004=UTF-8, 0x0404=UTF-8(special), 0x0204=int32
 *     [4B] data_len:     actual data length
 *     [4B] data_maxlen:  maximum data length (padded)
 *     [4B] data_offset:  offset into data table for this entry's value
 *
 *   Key table: null-terminated ASCII strings back-to-back
 *   Data table: values packed per index entries
 */

#include "sfo.h"

#include <stdlib.h>
#include <string.h>

#define SFO_MAGIC 0x46535000 /* \"\\0PSF\" as little-endian uint32 (bytes: 00 50 53 46) */

#define SFO_FORMAT_UTF8         0x0004
#define SFO_FORMAT_UTF8_SPECIAL 0x0204
#define SFO_FORMAT_INT32        0x0404

static inline uint16_t read_le16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

static inline uint32_t read_le32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

int32_t ps3_parse_sfo(const uint8_t *data, uint32_t length, SfoFile *sfo)
{
    if(data == NULL || sfo == NULL) return -1;

    memset(sfo, 0, sizeof(*sfo));

    /* Need at least the 20-byte header */
    if(length < 20) return -2;

    uint32_t magic = read_le32(data);

    if(magic != SFO_MAGIC) return -3;

    /* uint32_t version           = read_le32(data + 4); — not needed */
    uint32_t key_table_offset  = read_le32(data + 8);
    uint32_t data_table_offset = read_le32(data + 12);
    uint32_t entry_count       = read_le32(data + 16);

    if(entry_count == 0) return 0;

    /* Validate that the index table fits */
    uint32_t index_end = 20 + entry_count * 16;

    if(index_end > length || key_table_offset > length || data_table_offset > length) return -2;

    sfo->entries = calloc(entry_count, sizeof(SfoEntry));

    if(sfo->entries == NULL) return -4;

    sfo->entry_count = entry_count;

    for(uint32_t i = 0; i < entry_count; i++)
    {
        const uint8_t *idx = data + 20 + i * 16;

        uint16_t key_offset  = read_le16(idx);
        uint16_t data_format = read_le16(idx + 2);
        uint32_t data_len    = read_le32(idx + 4);
        /* uint32_t data_maxlen = read_le32(idx + 8); — not needed */
        uint32_t data_offset = read_le32(idx + 12);

        sfo->entries[i].format = data_format;

        /* Read key from key table (null-terminated) */
        uint32_t abs_key = key_table_offset + key_offset;

        if(abs_key < length)
        {
            /* Find null terminator, bounded by end of buffer */
            const char *key_start = (const char *)(data + abs_key);
            size_t      max_len   = length - abs_key;
            size_t      key_len   = strnlen(key_start, max_len);

            sfo->entries[i].key = malloc(key_len + 1);

            if(sfo->entries[i].key != NULL)
            {
                memcpy(sfo->entries[i].key, key_start, key_len);
                sfo->entries[i].key[key_len] = '\0';
            }
        }

        /* Read value from data table */
        uint32_t abs_data = data_table_offset + data_offset;

        if(abs_data < length && data_len > 0)
        {
            uint32_t avail = length - abs_data;

            if(data_len > avail) data_len = avail;

            if(data_format == SFO_FORMAT_UTF8 || data_format == SFO_FORMAT_UTF8_SPECIAL)
            {
                /* String value: copy ensuring null termination */
                sfo->entries[i].value = malloc(data_len + 1);

                if(sfo->entries[i].value != NULL)
                {
                    memcpy(sfo->entries[i].value, data + abs_data, data_len);
                    sfo->entries[i].value[data_len] = '\0';

                    /* Trim trailing nulls for clean string */
                    size_t slen = strlen(sfo->entries[i].value);

                    if(slen < data_len) sfo->entries[i].value[slen] = '\0';
                }
            }
            else if(data_format == SFO_FORMAT_INT32 && data_len >= 4)
            {
                sfo->entries[i].int_value = (int32_t)read_le32(data + abs_data);
            }
        }
    }

    return 0;
}

const char *ps3_sfo_get_string(const SfoFile *sfo, const char *key)
{
    if(sfo == NULL || key == NULL || sfo->entries == NULL) return NULL;

    for(uint32_t i = 0; i < sfo->entry_count; i++)
        if(sfo->entries[i].key != NULL && strcmp(sfo->entries[i].key, key) == 0) return sfo->entries[i].value;

    return NULL;
}

void ps3_free_sfo(SfoFile *sfo)
{
    if(sfo == NULL) return;

    if(sfo->entries != NULL)
    {
        for(uint32_t i = 0; i < sfo->entry_count; i++)
        {
            free(sfo->entries[i].key);
            free(sfo->entries[i].value);
        }

        free(sfo->entries);
        sfo->entries = NULL;
    }

    sfo->entry_count = 0;
}
