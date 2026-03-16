/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * WUD/WUX disc reader abstraction for aaruformattool.
 */

#include "reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Try to initialize WUX format from an open file.
 * @return 0 on success (file is WUX), -1 if not WUX.
 */
static int wux_init(WiiuReader *reader)
{
    WuxHeader hdr;

    if(fseek(reader->fp, 0, SEEK_SET) != 0) return -1;

    if(fread(&hdr, 1, sizeof(hdr), reader->fp) != sizeof(hdr)) return -1;

    if(hdr.magic != WUX_MAGIC) return -1;

    if(hdr.sector_size == 0 || hdr.sector_size != WIIU_SECTOR_SIZE)
    {
        fprintf(stderr, "Error: WUX sector size 0x%X is not 0x%X\n", hdr.sector_size, WIIU_SECTOR_SIZE);
        return -1;
    }

    reader->is_wux           = 1;
    reader->disc_size        = hdr.uncompressed_size;
    reader->wux_sector_count = (uint32_t)(reader->disc_size / WIIU_SECTOR_SIZE);

    /* Read index table (starts at offset 0x20) */
    size_t idx_bytes  = (size_t)reader->wux_sector_count * sizeof(uint32_t);
    reader->wux_index = (uint32_t *)malloc(idx_bytes);

    if(reader->wux_index == NULL)
    {
        fprintf(stderr, "Error: cannot allocate WUX index (%zu bytes)\n", idx_bytes);
        return -1;
    }

    if(fseek(reader->fp, 0x20, SEEK_SET) != 0)
    {
        free(reader->wux_index);
        reader->wux_index = NULL;
        return -1;
    }

    if(fread(reader->wux_index, 1, idx_bytes, reader->fp) != idx_bytes)
    {
        fprintf(stderr, "Error: cannot read WUX index table\n");
        free(reader->wux_index);
        reader->wux_index = NULL;
        return -1;
    }

    /* Data starts at next sector-aligned offset after header + index table */
    uint64_t raw_data_off   = 0x20 + idx_bytes;
    reader->wux_data_offset = (raw_data_off + WIIU_SECTOR_SIZE - 1) & ~((uint64_t)WIIU_SECTOR_SIZE - 1);

    return 0;
}

int wiiu_reader_open(const char *path, WiiuReader *reader)
{
    memset(reader, 0, sizeof(*reader));

    reader->fp = fopen(path, "rb");

    if(reader->fp == NULL)
    {
        fprintf(stderr, "Error: cannot open %s\n", path);
        return -1;
    }

    /* Try WUX first */
    if(wux_init(reader) == 0) return 0;

    /* Fallback: raw WUD — get file size */
    reader->is_wux = 0;

    if(fseek(reader->fp, 0, SEEK_END) != 0)
    {
        fprintf(stderr, "Error: cannot determine file size for %s\n", path);
        fclose(reader->fp);
        reader->fp = NULL;
        return -1;
    }

    long file_size = ftell(reader->fp);

    if(file_size <= 0)
    {
        fprintf(stderr, "Error: cannot determine file size for %s\n", path);
        fclose(reader->fp);
        reader->fp = NULL;
        return -1;
    }

    reader->disc_size = (uint64_t)file_size;

    return 0;
}

void wiiu_reader_close(WiiuReader *reader)
{
    if(reader->wux_index != NULL)
    {
        free(reader->wux_index);
        reader->wux_index = NULL;
    }

    if(reader->fp != NULL)
    {
        fclose(reader->fp);
        reader->fp = NULL;
    }
}

int64_t wiiu_reader_read_at(WiiuReader *reader, void *buf, size_t count, uint64_t offset)
{
    if(reader == NULL || reader->fp == NULL || buf == NULL) return -1;

    if(!reader->is_wux)
    {
        /* Raw WUD: direct seek + read */
        if(fseek(reader->fp, (long)offset, SEEK_SET) != 0) return -1;

        size_t n = fread(buf, 1, count, reader->fp);

        return n > 0 ? (int64_t)n : -1;
    }

    /* WUX: translate logical sectors through index table */
    uint8_t *out  = (uint8_t *)buf;
    size_t   done = 0;

    while(done < count)
    {
        uint64_t cur_off = offset + done;

        if(cur_off >= reader->disc_size) break;

        uint32_t logical_sector = (uint32_t)(cur_off / WIIU_SECTOR_SIZE);
        uint32_t sector_offset  = (uint32_t)(cur_off % WIIU_SECTOR_SIZE);

        if(logical_sector >= reader->wux_sector_count) break;

        uint32_t physical_sector = reader->wux_index[logical_sector];
        uint64_t file_offset = reader->wux_data_offset + (uint64_t)physical_sector * WIIU_SECTOR_SIZE + sector_offset;

        size_t chunk = WIIU_SECTOR_SIZE - sector_offset;

        if(chunk > count - done) chunk = count - done;

        if(fseek(reader->fp, (long)file_offset, SEEK_SET) != 0) break;

        size_t n = fread(out + done, 1, chunk, reader->fp);

        if(n == 0) break;

        done += n;
    }

    return done > 0 ? (int64_t)done : -1;
}
