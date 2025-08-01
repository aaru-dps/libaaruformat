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

#ifndef LIBAARUFORMAT_CHECKSUM_H
#define LIBAARUFORMAT_CHECKSUM_H

#pragma pack(push, 1)

/**
 *     Checksum block, contains a checksum of all user data sectors (except for optical discs that is 2352 uint8_ts raw
 *     sector if available
 *  */
typedef struct ChecksumHeader {
    /**Identifier, <see cref="BlockType.ChecksumBlock" /> */
    uint32_t identifier;
    /**Length in uint8_ts of the block */
    uint32_t length;
    /**How many checksums follow */
    uint8_t entries;
} ChecksumHeader;

/**Checksum entry, followed by checksum data itself */
typedef struct ChecksumEntry {
    /**Checksum algorithm */
    uint8_t type;
    /**Length in uint8_ts of checksum that follows this structure */
    uint32_t length;
} ChecksumEntry;

#pragma pack(pop)

#endif //LIBAARUFORMAT_CHECKSUM_H
