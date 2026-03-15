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
 * Minimal ISO 9660 reader — just enough to find and read a file by path.
 *
 * ISO 9660 on-disc structure (relevant parts):
 *
 *   Sector 16: Primary Volume Descriptor
 *     [1B]  type = 1
 *     [5B]  "CD001"
 *     ...
 *     [156] offset 156: root directory record (34 bytes)
 *
 *   Directory Record (variable length, min 33 bytes):
 *     [1B]  record length
 *     [1B]  extended attribute record length
 *     [8B]  extent location (LBA) — both-endian (LE at +2, BE at +6)
 *     [8B]  data length — both-endian (LE at +10, BE at +14)
 *     [7B]  recording date/time
 *     [1B]  file flags (bit 1 = directory)
 *     [1B]  file unit size
 *     [1B]  interleave gap size
 *     [4B]  volume sequence number — both-endian
 *     [1B]  file identifier length
 *     [nB]  file identifier
 */

#include "iso9660_mini.h"

#include <stdlib.h>
#include <string.h>

#define PVD_SECTOR          16
#define PVD_TYPE_PRIMARY    1
#define PVD_MAGIC           "CD001"
#define PVD_ROOT_DIR_OFFSET 156
#define DIR_FLAG_DIRECTORY  0x02

static inline uint32_t read_le32_iso(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

/**
 * @brief Search a directory extent for a named entry.
 *
 * @param read_sector  Sector read callback.
 * @param user_data    Opaque pointer for callback.
 * @param dir_lba      Starting LBA of the directory extent.
 * @param dir_size     Size of the directory extent in bytes.
 * @param name         Name to search for (case-insensitive, without version ";1").
 * @param name_len     Length of name.
 * @param out_lba      Output: LBA of found entry.
 * @param out_size     Output: data length of found entry.
 * @param out_is_dir   Output: true if entry is a directory.
 * @return 0 if found, -4 if not found, other negative on error.
 */
static int32_t iso9660_find_entry(iso9660_read_sector_fn read_sector, void *user_data, uint32_t dir_lba,
                                  uint32_t dir_size, const char *name, size_t name_len, uint32_t *out_lba,
                                  uint32_t *out_size, int *out_is_dir)
{
    uint8_t  sector[ISO9660_SECTOR_SIZE];
    uint32_t bytes_remaining = dir_size;
    uint32_t current_lba     = dir_lba;

    while(bytes_remaining > 0)
    {
        if(read_sector(user_data, current_lba, sector) != 0) return -2;

        uint32_t sector_bytes = bytes_remaining < ISO9660_SECTOR_SIZE ? bytes_remaining : ISO9660_SECTOR_SIZE;
        uint32_t pos          = 0;

        while(pos < sector_bytes)
        {
            uint8_t rec_len = sector[pos];

            /* A zero record length means we've hit padding — skip to next sector */
            if(rec_len == 0) break;

            if(rec_len < 33 || pos + rec_len > sector_bytes) break;

            const uint8_t *rec = sector + pos;

            uint32_t    entry_lba  = read_le32_iso(rec + 2);
            uint32_t    entry_size = read_le32_iso(rec + 10);
            uint8_t     file_flags = rec[25];
            uint8_t     id_len     = rec[32];
            const char *id         = (const char *)(rec + 33);

            /* Skip "." (id_len==1, id[0]==0) and ".." (id_len==1, id[0]==1) */
            if(id_len == 1 && (id[0] == 0 || id[0] == 1))
            {
                pos += rec_len;
                continue;
            }

            /* ISO 9660 filenames may have ";1" version suffix — strip it for comparison */
            size_t cmp_len = id_len;

            if(cmp_len >= 2 && id[cmp_len - 2] == ';') cmp_len -= 2;

            /* Also strip trailing "." if present (directory entries sometimes have it) */
            if(cmp_len > 0 && id[cmp_len - 1] == '.') cmp_len--;

            /* Case-insensitive comparison */
            if(cmp_len == name_len)
            {
                int match = 1;

                for(size_t i = 0; i < cmp_len; i++)
                {
                    char a = id[i];
                    char b = name[i];

                    if(a >= 'a' && a <= 'z') a -= 32;

                    if(b >= 'a' && b <= 'z') b -= 32;

                    if(a != b)
                    {
                        match = 0;
                        break;
                    }
                }

                if(match)
                {
                    *out_lba    = entry_lba;
                    *out_size   = entry_size;
                    *out_is_dir = (file_flags & DIR_FLAG_DIRECTORY) != 0;
                    return 0;
                }
            }

            pos += rec_len;
        }

        current_lba++;
        bytes_remaining -= sector_bytes;
    }

    return -4; /* not found */
}

int32_t iso9660_read_file(iso9660_read_sector_fn read_sector, void *user_data, const char *path, uint8_t **out_data,
                          uint32_t *out_length)
{
    if(read_sector == NULL || path == NULL || out_data == NULL || out_length == NULL) return -1;

    *out_data   = NULL;
    *out_length = 0;

    /* Read Primary Volume Descriptor at sector 16 */
    uint8_t pvd[ISO9660_SECTOR_SIZE];

    if(read_sector(user_data, PVD_SECTOR, pvd) != 0) return -2;

    /* Validate PVD */
    if(pvd[0] != PVD_TYPE_PRIMARY || memcmp(pvd + 1, PVD_MAGIC, 5) != 0) return -3;

    /* Extract root directory record from PVD offset 156 */
    const uint8_t *root_rec  = pvd + PVD_ROOT_DIR_OFFSET;
    uint32_t       root_lba  = read_le32_iso(root_rec + 2);
    uint32_t       root_size = read_le32_iso(root_rec + 10);

    /* Parse path components */
    const char *p = path;

    /* Skip leading '/' */
    while(*p == '/') p++;

    uint32_t cur_lba  = root_lba;
    uint32_t cur_size = root_size;

    while(*p != '\0')
    {
        /* Find next path separator or end */
        const char *slash = strchr(p, '/');
        size_t      comp_len;

        if(slash != NULL)
            comp_len = (size_t)(slash - p);
        else
            comp_len = strlen(p);

        if(comp_len == 0)
        {
            p++;
            continue;
        }

        uint32_t found_lba;
        uint32_t found_size;
        int      is_dir;
        int32_t  ret = iso9660_find_entry(read_sector, user_data, cur_lba, cur_size, p, comp_len, &found_lba,
                                          &found_size, &is_dir);

        if(ret != 0) return ret;

        cur_lba  = found_lba;
        cur_size = found_size;

        /* Advance past this component */
        p += comp_len;

        while(*p == '/') p++;

        /* If there are more components, this must be a directory */
        if(*p != '\0' && !is_dir) return -4;
    }

    /* cur_lba / cur_size now point to the target file — read it */
    if(cur_size == 0)
    {
        *out_data   = NULL;
        *out_length = 0;
        return 0;
    }

    uint8_t *file_data = malloc(cur_size);

    if(file_data == NULL) return -5;

    uint32_t bytes_read = 0;
    uint32_t lba        = cur_lba;

    while(bytes_read < cur_size)
    {
        uint8_t  sector[ISO9660_SECTOR_SIZE];
        uint32_t to_copy = cur_size - bytes_read;

        if(to_copy > ISO9660_SECTOR_SIZE) to_copy = ISO9660_SECTOR_SIZE;

        if(read_sector(user_data, lba, sector) != 0)
        {
            free(file_data);
            return -2;
        }

        memcpy(file_data + bytes_read, sector, to_copy);
        bytes_read += to_copy;
        lba++;
    }

    *out_data   = file_data;
    *out_length = cur_size;
    return 0;
}
