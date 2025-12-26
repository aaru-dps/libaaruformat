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
 */

#ifndef AARUFORMATTOOL_DDT_ALPHA20_H
#define AARUFORMATTOOL_DDT_ALPHA20_H

#include <stdint.h>

#pragma pack(push, 1)

/**
 * \struct ddt_v2_header_alpha20
 * \brief DDT v2 header structure as it was in alpha20, before modifications.
 *
 * This is a snapshot of the DdtHeader2 structure from alpha20 for use in the
 * upgrade-ddt-to-alpha21 command. This allows the tool to read old format
 * DDT headers while the library uses the newer structure.
 *
 * Original documentation from alpha20:
 * Header preceding a version 2 hierarchical deduplication table.
 *
 * Version 2 introduces multi-level tables to efficiently address very large images by subdividing
 * the logical address space. Tables at higher levels partition regions; leaves contain direct
 * (block, sector) entry mappings. Navigation uses tableLevel (0 = root) and levels (total depth).
 */
typedef struct ddt_v2_header_alpha20
{
    uint32_t identifier;           ///< Block identifier, must be BlockType::DeDuplicationTable2.
    uint16_t type;                 ///< Data classification (DataType) for sectors referenced by this table.
    uint16_t compression;          ///< Compression algorithm for this table body (CompressionType).
    uint8_t  levels;               ///< Total number of hierarchy levels (root depth); > 0.
    uint8_t  tableLevel;           ///< Zero-based level index of this table (0 = root, increases downward).
    uint64_t previousLevelOffset;  ///< Absolute byte offset of the parent (previous) level table; 0 if root.
    uint16_t negative;             ///< Leading negative LBA count; added to external L to build internal index.
    uint64_t blocks;               ///< Total internal span (negative + usable + overflow) in logical sectors.
    uint16_t overflow;  ///< Trailing dumped sectors beyond user area (overflow range), still mapped with entries.
    uint64_t
            start;  ///< Base internal index covered by this table (used for secondary tables; currently informational).
    uint8_t blockAlignmentShift;  ///< 2^blockAlignmentShift = block alignment boundary in bytes.
    uint8_t dataShift;            ///< 2^dataShift = sectors represented per increment in blockIndex field.
    uint8_t tableShift;  ///< 2^tableShift = number of logical sectors per primary entry (multi-level only; 0 for
                         ///< single-level or secondary tables).
    uint64_t entries;    ///< Number of entries contained in (uncompressed) table payload.
    uint64_t cmpLength;  ///< Compressed payload size in bytes.
    uint64_t length;     ///< Uncompressed payload size in bytes.
    uint64_t cmpCrc64;   ///< CRC64-ECMA of compressed table payload.
    uint64_t crc64;      ///< CRC64-ECMA of uncompressed table payload.
} ddt_v2_header_alpha20;

#pragma pack(pop)

#endif  // AARUFORMATTOOL_DDT_ALPHA20_H
