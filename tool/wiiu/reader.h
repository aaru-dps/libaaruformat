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

#ifndef AARUFORMATTOOL_WIIU_READER_H
#define AARUFORMATTOOL_WIIU_READER_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define WIIU_SECTOR_SIZE 0x8000      /**< Wii U physical sector size (32 KiB). */
#define WUX_MAGIC        0x30585557U /**< "WUX0" as little-endian uint32. */

    /** WUX file header (32 bytes). */
    typedef struct WuxHeader
    {
        uint32_t magic;             /**< "WUX0" (0x30585557 LE). */
        uint32_t reserved;          /**< Reserved / version. */
        uint32_t sector_size;       /**< Sector size (must be 0x8000). */
        uint32_t reserved2;         /**< Must be 0. */
        uint64_t uncompressed_size; /**< Original disc size in bytes. */
        uint64_t reserved3;         /**< Must be 0. */
    } WuxHeader;

    /** Reader abstraction that handles both raw WUD and compressed WUX. */
    typedef struct WiiuReader
    {
        FILE     *fp;        /**< File handle. */
        int       is_wux;    /**< 1 if WUX, 0 if raw WUD. */
        uint64_t  disc_size; /**< Uncompressed disc size in bytes. */
        /* WUX-specific */
        uint32_t *wux_index;        /**< Sector index table (NULL if WUD). */
        uint64_t  wux_data_offset;  /**< File offset where WUX data sectors start. */
        uint32_t  wux_sector_count; /**< Number of logical sectors in the disc. */
    } WiiuReader;

    /**
     * @brief Open a WUD or WUX disc image.
     *
     * Detects the format automatically (WUX if magic matches, else raw WUD).
     *
     * @param path     File path to the disc image.
     * @param reader   Output: reader structure to populate.
     * @return 0 on success, -1 on error.
     */
    int wiiu_reader_open(const char *path, WiiuReader *reader);

    /**
     * @brief Close the disc reader and free associated resources.
     *
     * @param reader   Reader to close.
     */
    void wiiu_reader_close(WiiuReader *reader);

    /**
     * @brief Read data from the disc image at the given uncompressed offset.
     *
     * For WUD files this is a direct read. For WUX files this translates
     * through the sector index table.
     *
     * @param reader   Reader to read from.
     * @param buf      Output buffer.
     * @param count    Number of bytes to read.
     * @param offset   Byte offset in the uncompressed disc image.
     * @return Number of bytes read, or -1 on error.
     */
    int64_t wiiu_reader_read_at(WiiuReader *reader, void *buf, size_t count, uint64_t offset);

#ifdef __cplusplus
}
#endif

#endif /* AARUFORMATTOOL_WIIU_READER_H */
