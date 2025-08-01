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

#ifndef LIBAARUFORMAT_DATA_H
#define LIBAARUFORMAT_DATA_H

#pragma pack(push, 1)

/**Block header, precedes block data */
typedef struct BlockHeader {
    /**Identifier, <see cref="BlockType.DataBlock" /> */
    uint32_t identifier;
    /**Type of data contained by this block */
    uint16_t type;
    /**Compression algorithm used to compress the block */
    uint16_t compression;
    /**Size in uint8_ts of each sector contained in this block */
    uint32_t sectorSize;
    /**Compressed length for the block */
    uint32_t cmpLength;
    /**Uncompressed length for the block */
    uint32_t length;
    /**CRC64-ECMA of the compressed block */
    uint64_t cmpCrc64;
    /**CRC64-ECMA of the uncompressed block */
    uint64_t crc64;
} BlockHeader;

/**Geometry block, contains physical geometry information */
typedef struct GeometryBlockHeader {
    /**Identifier, <see cref="BlockType.GeometryBlock" /> */
    uint32_t identifier;
    uint32_t cylinders;
    uint32_t heads;
    uint32_t sectorsPerTrack;
} GeometryBlockHeader;

#pragma pack(pop)

#endif //LIBAARUFORMAT_DATA_H
