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
 * */

#ifndef LIBAARUFORMAT_OPTICAL_H
#define LIBAARUFORMAT_OPTICAL_H

#pragma pack(push, 1)

/**Contains list of optical disc tracks */
typedef struct TracksHeader {
    /**Identifier, <see cref="BlockType.TracksBlock" /> */
    uint32_t identifier;
    /**How many entries follow this header */
    uint16_t entries;
    /**CRC64-ECMA of the block */
    uint64_t crc64;
} TracksHeader;

/**Optical disc track */
typedef struct TrackEntry {
    /**Track sequence */
    uint8_t sequence;
    /**Track type */
    uint8_t type;
    /**Track starting LBA */
    int64_t start;
    /**Track last LBA */
    int64_t end;
    /**Track pregap in sectors */
    int64_t pregap;
    /**Track session */
    uint8_t session;
    /**Track's ISRC in ASCII */
    uint8_t isrc[13];
    /**Track flags */
    uint8_t flags;
} TrackEntry;

#pragma pack(pop)

#endif //LIBAARUFORMAT_OPTICAL_H
