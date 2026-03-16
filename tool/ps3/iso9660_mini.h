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
 */

#ifndef LIBAARUFORMAT_TOOL_ISO9660_MINI_H
#define LIBAARUFORMAT_TOOL_ISO9660_MINI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define ISO9660_SECTOR_SIZE 2048

    /**
     * @brief Callback type for reading a sector.
     *
     * @param user_data  Opaque pointer passed through from the caller.
     * @param sector     Sector number to read (0-based LBA).
     * @param buffer     Output buffer (must hold ISO9660_SECTOR_SIZE bytes).
     * @return 0 on success, negative on error.
     */
    typedef int32_t (*iso9660_read_sector_fn)(void *user_data, uint64_t sector, uint8_t *buffer);

    /**
     * @brief Read a file from an ISO 9660 filesystem by path.
     *
     * Navigates the ISO 9660 directory tree from the primary volume descriptor
     * (sector 16) to locate and read the specified file. Only supports plain
     * ISO 9660 — no Joliet, no Rock Ridge, no UDF.
     *
     * @param read_sector  Function to read a single 2048-byte sector.
     * @param user_data    Opaque pointer passed to read_sector.
     * @param path         Absolute path to the file (e.g., "/PS3_GAME/PARAM.SFO").
     *                     Path components are separated by '/'. Leading '/' is optional.
     * @param out_data     Output: malloc'd buffer containing the file data. Caller must free().
     * @param out_length   Output: length of the file data in bytes.
     * @return 0 on success, negative on error:
     *   -1: NULL argument
     *   -2: cannot read sector
     *   -3: not an ISO 9660 volume (bad magic)
     *   -4: file/directory not found
     *   -5: memory allocation failure
     */
    int32_t iso9660_read_file(iso9660_read_sector_fn read_sector, void *user_data, const char *path, uint8_t **out_data,
                              uint32_t *out_length);

#ifdef __cplusplus
}
#endif

#endif /* LIBAARUFORMAT_TOOL_ISO9660_MINI_H */
