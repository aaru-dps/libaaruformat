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

#ifndef LIBAARUFORMAT_INDEX_H
#define LIBAARUFORMAT_INDEX_H

#pragma pack(push, 1)

/**Header for the index, followed by entries */
typedef struct IndexHeader
{
    /**Identifier, <see cref="BlockType.Index" /> */
    uint32_t identifier;
    /**How many entries follow this header */
    uint16_t entries;
    /**CRC64-ECMA of the index */
    uint64_t crc64;
} IndexHeader;

/**Header for the index, followed by entries */
typedef struct IndexHeader2
{
    /**Identifier, <see cref="BlockType.Index" /> */
    uint32_t identifier;
    /**How many entries follow this header */
    uint64_t entries;
    /**CRC64-ECMA of the index */
    uint64_t crc64;
} IndexHeader2;

/**Index entry */
typedef struct IndexEntry
{
    /**Type of item pointed by this entry */
    uint32_t blockType;
    /**Type of data contained by the block pointed by this entry */
    uint16_t dataType;
    /**Offset in file where item is stored */
    uint64_t offset;
} IndexEntry;

#pragma pack(pop)

#endif  // LIBAARUFORMAT_INDEX_H
