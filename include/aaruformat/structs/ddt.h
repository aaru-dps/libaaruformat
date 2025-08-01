/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2025 Natalia Portillo.
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBAARUFORMAT_DDT_H
#define LIBAARUFORMAT_DDT_H

#pragma pack(push, 1)

/**Header for a deduplication table. Table follows it */
typedef struct DdtHeader {
    /**Identifier, <see cref="BlockType.DeDuplicationTable" /> */
    uint32_t identifier;
    /**Type of data pointed by this DDT */
    uint16_t type;
    /**Compression algorithm used to compress the DDT */
    uint16_t compression;
    /**Each entry is ((uint8_t offset in file) &lt;&lt; shift) + (sector offset in block) */
    uint8_t shift;
    /**How many entries are in the table */
    uint64_t entries;
    /**Compressed length for the DDT */
    uint64_t cmpLength;
    /**Uncompressed length for the DDT */
    uint64_t length;
    /**CRC64-ECMA of the compressed DDT */
    uint64_t cmpCrc64;
    /**CRC64-ECMA of the uncompressed DDT */
    uint64_t crc64;
} DdtHeader;

#pragma pack(pop)

#endif //LIBAARUFORMAT_DDT_H
