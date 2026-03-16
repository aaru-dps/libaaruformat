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
 * IRD (ISO Rebuild Data) file parser for PlayStation 3 disc images.
 * Supports versions 5-9 as documented by ps3dev.
 * Handles both raw and gzip-compressed IRD files.
 */

#ifndef LIBAARUFORMAT_TOOL_IRD_H
#define LIBAARUFORMAT_TOOL_IRD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define IRD_MAGIC 0x33495244 /* "3IRD" */

    /** Parsed IRD file structure. */
    typedef struct IrdFile
    {
        uint8_t  version;        /**< IRD format version (6-9). */
        char     game_id[10];    /**< 9-char game ID + null terminator (e.g., "BLES00905"). */
        char     game_name[256]; /**< Game name (null-terminated). */
        char     update_ver[5];  /**< PS3 system/update version (4 chars + null). */
        char     game_ver[6];    /**< Game version (5 chars + null). */
        char     app_ver[6];     /**< App version (5 chars + null). */
        uint8_t  d1[16];         /**< Data1 key (16 bytes). */
        uint8_t  d2[16];         /**< Data2 key (16 bytes). */
        uint8_t  pic[115];       /**< PIC data (115 bytes, only valid if has_pic is true). */
        bool     has_pic;        /**< True if PIC data was present in the IRD. */
        uint8_t *header_gz;      /**< Gzip-compressed ISO header blob (malloc'd, may be NULL). */
        uint32_t header_gz_len;  /**< Length of header_gz data. */
        uint8_t *footer_gz;      /**< Gzip-compressed ISO footer blob (malloc'd, may be NULL). */
        uint32_t footer_gz_len;  /**< Length of footer_gz data. */
        bool     valid;          /**< True if parsing succeeded (at least basic fields). */
    } IrdFile;

    /**
     * @brief Parse an IRD file from disk (handles gzip-compressed files).
     *
     * @param path  Path to the IRD file.
     * @param ird   Output: parsed IRD structure. game_title is malloc'd and must be freed via ps3_free_ird().
     * @return 0 on success, negative on error:
     *   -1: NULL argument
     *   -2: cannot open file
     *   -3: read/decompression error
     *   -4: invalid magic
     *   -5: unsupported version
     *   -6: memory allocation error
     *   -7: truncated file
     */
    int32_t ps3_parse_ird(const char *path, IrdFile *ird);

    /**
     * @brief Free dynamically allocated fields in an IrdFile.
     *
     * @param ird  Pointer to IrdFile to clean up. Safe to call on a zeroed or partially-parsed struct.
     */
    void ps3_free_ird(IrdFile *ird);

#ifdef __cplusplus
}
#endif

#endif /* LIBAARUFORMAT_TOOL_IRD_H */
