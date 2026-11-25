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

/**
 * @file twin.h
 * @brief On-disk structures of the twin sector table (TWTB) and the public enumeration record.
 *
 * Some media carry more than one physical sector answering to the same sector address ("twin sectors"),
 * for example floppies with duplicated sector IDs or CDs where the same MSF appears more than once.
 *
 * Twin #0 is stored exactly like any other sector (user-data DDT, CD prefix/suffix DDTs and arenas, per-sector tag
 * arrays). Its user-data DDT entry carries status SectorStatusTwin, but its pointer still references twin #0's data so
 * readers unaware of twins transparently read twin #0.
 *
 * Every further instance (twin #1..count-1) is a TwinInstance record inside the TWTB. A record holds one value per
 * component declared in the table header: user data and CD prefix/suffix are stored as pointers using exactly the
 * same encoding as their respective DDTs (status in the high nibble), small fixed-size tags are stored inline.
 *
 * Payload layout (compressed as a whole):
 * @code
 *   TwinComponentDescriptor descriptors[header.components];
 *   TwinSectorGroup         groups[header.groups];          // sorted by sector, unique
 *   uint8_t                 records[header.instances][header.recordSize];
 * @endcode
 *
 * Record layout:
 * @code
 *   uint32_t present;   // bit i set = component i (descriptor order) holds a value for this instance
 *   uint32_t reserved;
 *   then, in descriptor order, 8 bytes (pointer components) or `size` bytes (inline components)
 *   zero padded to header.recordSize (multiple of 8)
 * @endcode
 */

#ifndef LIBAARUFORMAT_TWIN_H
#define LIBAARUFORMAT_TWIN_H

#include <stdint.h>

#pragma pack(push, 1)

/** How a component value is stored inside a twin instance record. */
typedef enum
{
    kTwinComponentPointer = 0,  ///< 8-byte value encoded as in the component's DDT (status << 60 | pointer).
    kTwinComponentInline  = 1   ///< `size` raw bytes stored inside the record itself.
} TwinComponentKind;

/**
 * @struct TwinComponentDescriptor
 * @brief Describes one component present in every twin instance record.
 */
typedef struct TwinComponentDescriptor
{
    uint16_t dataType;  ///< DataType of the component (kDataTypeUserData, kDataTypeCdSectorPrefix, ...).
    uint8_t  kind;      ///< TwinComponentKind.
    uint8_t  reserved;  ///< Must be 0.
    uint32_t size;      ///< Bytes the component takes in a record (8 for pointers).
} TwinComponentDescriptor;

/**
 * @struct TwinSectorTableHeader
 * @brief Header of the twin sector table block (`TWTB`).
 */
typedef struct TwinSectorTableHeader
{
    uint32_t identifier;           ///< BlockType::TwinSectorTable ("TWTB").
    uint16_t compression;          ///< CompressionType of the payload (None, Lzma or Zstd).
    uint8_t  blockAlignmentShift;  ///< Must equal the user-data DDT's blockAlignmentShift.
    uint8_t  dataShift;            ///< Must equal the user-data DDT's dataShift.
    uint8_t  components;           ///< Number of TwinComponentDescriptor entries (component 0 is always user data).
    uint8_t  reserved[3];          ///< Must be 0.
    uint32_t recordSize;           ///< Bytes per twin instance record (multiple of 8).
    uint32_t groups;               ///< Number of twinned sectors (TwinSectorGroup entries).
    uint64_t instances;            ///< Number of instance records (twins #1..n-1 of all groups).
    uint64_t cmpLength;            ///< Compressed payload length (includes LZMA properties when Lzma).
    uint64_t length;               ///< Uncompressed payload length.
    uint64_t cmpCrc64;             ///< CRC64-ECMA of the compressed payload.
    uint64_t crc64;                ///< CRC64-ECMA of the uncompressed payload.
} TwinSectorTableHeader;

/**
 * @struct TwinSectorGroup
 * @brief One twinned sector.
 */
typedef struct TwinSectorGroup
{
    uint64_t sector;       ///< Internal DDT index of the sector (negative offset applied, same as DDT addressing).
    uint32_t first;        ///< Index of twin #1 in the record array.
    uint16_t count;        ///< Total instances including twin #0 (>= 2). Twins #1..count-1 are records[first..].
    uint8_t  twin0Status;  ///< Real SectorStatus of twin #0's user data (the DDT carries SectorStatusTwin).
    uint8_t  reserved;     ///< Must be 0.
} TwinSectorGroup;

/**
 * @struct TwinSectorInfo
 * @brief One entry returned by aaruf_get_twin_sectors().
 */
typedef struct TwinSectorInfo
{
    uint64_t sector_address;  ///< Sector address as passed to the read/write functions.
    uint32_t count;           ///< Number of physical instances (>= 2).
    uint8_t  negative;        ///< 1 if sector_address is a negative sector.
    uint8_t  reserved[3];     ///< Always 0.
} TwinSectorInfo;

#pragma pack(pop)

#endif  // LIBAARUFORMAT_TWIN_H
