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

#ifndef LIBAARUFORMAT_DUMP_H
#define LIBAARUFORMAT_DUMP_H

#pragma pack(push, 1)

/**Dump hardware block, contains a list of hardware used to dump the media on this image */
typedef struct DumpHardwareHeader {
    /**Identifier, <see cref="BlockType.DumpHardwareBlock" /> */
    uint32_t identifier;
    /**How many entries follow this header */
    uint16_t entries;
    /**Size of the whole block, not including this header, in uint8_ts */
    uint32_t length;
    /**CRC64-ECMA of the block */
    uint64_t crc64;
} DumpHardwareHeader;

/**Dump hardware entry, contains length of strings that follow, in the same order as the length, this structure */
typedef struct DumpHardwareEntry {
    /**Length of UTF-8 manufacturer string */
    uint32_t manufacturerLength;
    /**Length of UTF-8 model string */
    uint32_t modelLength;
    /**Length of UTF-8 revision string */
    uint32_t revisionLength;
    /**Length of UTF-8 firmware version string */
    uint32_t firmwareLength;
    /**Length of UTF-8 serial string */
    uint32_t serialLength;
    /**Length of UTF-8 software name string */
    uint32_t softwareNameLength;
    /**Length of UTF-8 software version string */
    uint32_t softwareVersionLength;
    /**Length of UTF-8 software operating system string */
    uint32_t softwareOperatingSystemLength;
    /**How many extents are after the strings */
    uint32_t extents;
} DumpHardwareEntry;

#pragma pack(pop)

#endif //LIBAARUFORMAT_DUMP_H
