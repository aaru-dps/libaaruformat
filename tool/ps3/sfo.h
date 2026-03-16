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
 */

#ifndef LIBAARUFORMAT_TOOL_SFO_H
#define LIBAARUFORMAT_TOOL_SFO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /** A single key-value entry from a PARAM.SFO file. */
    typedef struct SfoEntry
    {
        char    *key;       /**< Parameter name (malloc'd, null-terminated). */
        char    *value;     /**< UTF-8 string value (malloc'd, null-terminated). Only for string-type entries. */
        int32_t  int_value; /**< Integer value (for integer-type entries). */
        uint16_t format;    /**< Data format: 0x0004 = UTF-8 string, 0x0204 = UTF-8 string (special), 0x0404 = int32. */
    } SfoEntry;

    /** Parsed PARAM.SFO file. */
    typedef struct SfoFile
    {
        SfoEntry *entries;     /**< Array of entries (malloc'd). */
        uint32_t  entry_count; /**< Number of entries. */
    } SfoFile;

    /**
     * @brief Parse a PARAM.SFO from an in-memory buffer.
     *
     * @param data   Pointer to the SFO file data.
     * @param length Length of the data buffer.
     * @param sfo    Output: parsed SFO structure. Must be freed with ps3_free_sfo().
     * @return 0 on success, negative on error:
     *   -1: NULL argument
     *   -2: buffer too small or invalid header
     *   -3: invalid magic
     *   -4: memory allocation failure
     */
    int32_t ps3_parse_sfo(const uint8_t *data, uint32_t length, SfoFile *sfo);

    /**
     * @brief Look up a string value by key in a parsed SFO.
     *
     * @param sfo  Parsed SFO structure.
     * @param key  Key to search for.
     * @return Pointer to the value string (owned by the SfoFile), or NULL if not found.
     */
    const char *ps3_sfo_get_string(const SfoFile *sfo, const char *key);

    /**
     * @brief Free all dynamically allocated fields in an SfoFile.
     *
     * @param sfo  Pointer to SfoFile to clean up.
     */
    void ps3_free_sfo(SfoFile *sfo);

#ifdef __cplusplus
}
#endif

#endif /* LIBAARUFORMAT_TOOL_SFO_H */
