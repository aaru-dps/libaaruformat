// /***************************************************************************
// The Disc Image Chef
// ----------------------------------------------------------------------------
//
// Filename       : structs.h
// Author(s)      : Natalia Portillo <claunia@claunia.com>
//
// Component      : Disk image plugins.
//
// --[ Description ] ----------------------------------------------------------
//
//     Contains structures for DiscImageChef format disk images.
//
// --[ License ] --------------------------------------------------------------
//
//     This library is free software; you can redistribute it and/or modify
//     it under the terms of the GNU Lesser General License as
//     published by the Free Software Foundation; either version 2.1 of the
//     License, or (at your option) any later version.
//
//     This library is distributed in the hope that it will be useful, but
//     WITHOUT ANY WARRANTY; without even the implied warranty of
//     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
//     Lesser General License for more details.
//
//     You should have received a copy of the GNU Lesser General Public
//     License aint64_t with this library; if not, see <http://www.gnu.org/licenses/>.
//
// ----------------------------------------------------------------------------
// Copyright © 2011-2019 Natalia Portillo
// ****************************************************************************/

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
#ifndef LIBDICFORMAT_STRUCTS_H
#define LIBDICFORMAT_STRUCTS_H

#pragma pack(push, 1)

#include <stdint.h>
#include <dic.h>
#include "enums.h"

/**Header, at start of file */
typedef struct DicHeader
{
    /**Header identifier, <see cref="DIC_MAGIC" /> */
    uint64_t      identifier;
    /**UTF-16LE name of the application that created the image */
    unsigned char application[64];
    /**Image format major version. A new major version means a possibly incompatible change of format */
    uint8_t       imageMajorVersion;
    /**Image format minor version. A new minor version indicates a compatible change of format */
    uint8_t       imageMinorVersion;
    /**Major version of the application that created the image */
    uint8_t       applicationMajorVersion;
    /**Minor version of the application that created the image */
    uint8_t       applicationMinorVersion;
    /**Type of media contained on image */
    uint32_t      mediaType;
    /**Offset to index */
    uint64_t      indexOffset;
    /**Windows filetime (100 nanoseconds since 1601/01/01 00:00:00 UTC) of image creation time */
    int64_t       creationTime;
    /**Windows filetime (100 nanoseconds since 1601/01/01 00:00:00 UTC) of image last written time */
    int64_t       lastWrittenTime;
} DicHeader;

/**Header for a deduplication table. Table follows it */
typedef struct DdtHeader
{
    /**Identifier, <see cref="BlockType.DeDuplicationTable" /> */
    uint32_t identifier;
    /**Type of data pointed by this DDT */
    uint16_t type;
    /**Compression algorithm used to compress the DDT */
    uint16_t compression;
    /**Each entry is ((uint8_t offset in file) &lt;&lt; shift) + (sector offset in block) */
    uint8_t  shift;
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

/**Block header, precedes block data */
typedef struct BlockHeader
{
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
typedef struct GeometryBlockHeader
{
    /**Identifier, <see cref="BlockType.GeometryBlock" /> */
    uint32_t identifier;
    uint32_t cylinders;
    uint32_t heads;
    uint32_t sectorsPerTrack;
} GeometryBlockHeader;

/**Metadata block, contains metadata */
typedef struct MetadataBlockHeader
{
    /**Identifier, <see cref="BlockType.MetadataBlock" /> */
    uint32_t identifier;
    /**Size in uint8_ts of this whole metadata block */
    uint32_t blockSize;
    /**Sequence of media set this media beint64_ts to */
    int32_t  mediaSequence;
    /**Total number of media on the media set this media beint64_ts to */
    int32_t  lastMediaSequence;
    /**Offset to start of creator string from start of this block */
    uint32_t creatorOffset;
    /**Length in uint8_ts of the null-terminated UTF-16LE creator string */
    uint32_t creatorLength;
    /**Offset to start of creator string from start of this block */
    uint32_t commentsOffset;
    /**Length in uint8_ts of the null-terminated UTF-16LE creator string */
    uint32_t commentsLength;
    /**Offset to start of creator string from start of this block */
    uint32_t mediaTitleOffset;
    /**Length in uint8_ts of the null-terminated UTF-16LE creator string */
    uint32_t mediaTitleLength;
    /**Offset to start of creator string from start of this block */
    uint32_t mediaManufacturerOffset;
    /**Length in uint8_ts of the null-terminated UTF-16LE creator string */
    uint32_t mediaManufacturerLength;
    /**Offset to start of creator string from start of this block */
    uint32_t mediaModelOffset;
    /**Length in uint8_ts of the null-terminated UTF-16LE creator string */
    uint32_t mediaModelLength;
    /**Offset to start of creator string from start of this block */
    uint32_t mediaSerialNumberOffset;
    /**Length in uint8_ts of the null-terminated UTF-16LE creator string */
    uint32_t mediaSerialNumberLength;
    /**Offset to start of creator string from start of this block */
    uint32_t mediaBarcodeOffset;
    /**Length in uint8_ts of the null-terminated UTF-16LE creator string */
    uint32_t mediaBarcodeLength;
    /**Offset to start of creator string from start of this block */
    uint32_t mediaPartNumberOffset;
    /**Length in uint8_ts of the null-terminated UTF-16LE creator string */
    uint32_t mediaPartNumberLength;
    /**Offset to start of creator string from start of this block */
    uint32_t driveManufacturerOffset;
    /**Length in uint8_ts of the null-terminated UTF-16LE creator string */
    uint32_t driveManufacturerLength;
    /**Offset to start of creator string from start of this block */
    uint32_t driveModelOffset;
    /**Length in uint8_ts of the null-terminated UTF-16LE creator string */
    uint32_t driveModelLength;
    /**Offset to start of creator string from start of this block */
    uint32_t driveSerialNumberOffset;
    /**Length in uint8_ts of the null-terminated UTF-16LE creator string */
    uint32_t driveSerialNumberLength;
    /**Offset to start of creator string from start of this block */
    uint32_t driveFirmwareRevisionOffset;
    /**Length in uint8_ts of the null-terminated UTF-16LE creator string */
    uint32_t driveFirmwareRevisionLength;
} MetadataBlockHeader;

/**Contains list of optical disc tracks */
typedef struct TracksHeader
{
    /**Identifier, <see cref="BlockType.TracksBlock" /> */
    uint32_t identifier;
    /**How many entries follow this header */
    uint16_t entries;
    /**CRC64-ECMA of the block */
    uint64_t crc64;
} TracksHeader;

/**Optical disc track */
typedef struct TrackEntry
{
    /**Track sequence */
    uint8_t       sequence;
    /**Track type */
    uint8_t       type;
    /**Track starting LBA */
    int64_t       start;
    /**Track last LBA */
    int64_t       end;
    /**Track pregap in sectors */
    int64_t       pregap;
    /**Track session */
    uint8_t       session;
    /**Track's ISRC in ASCII */
    unsigned char isrc[13];
    /**Track flags */
    uint8_t       flags;
} TrackEntry;

/**Geometry block, contains physical geometry information */
typedef struct CicmMetadataBlock
{
    /**Identifier, <see cref="BlockType.CicmBlock" /> */
    uint32_t identifier;
    uint32_t length;
} CicmMetadataBlock;

/**Dump hardware block, contains a list of hardware used to dump the media on this image */
typedef struct DumpHardwareHeader
{
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
typedef struct DumpHardwareEntry
{
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

/**
 *     Checksum block, contains a checksum of all user data sectors (except for optical discs that is 2352 uint8_ts raw
 *     sector if available
 *  */
typedef struct ChecksumHeader
{
    /**Identifier, <see cref="BlockType.ChecksumBlock" /> */
    uint32_t identifier;
    /**Length in uint8_ts of the block */
    uint32_t length;
    /**How many checksums follow */
    uint8_t  entries;
} ChecksumHeader;

/**Checksum entry, followed by checksum data itself */
typedef struct ChecksumEntry
{
    /**Checksum algorithm */
    uint8_t  type;
    /**Length in uint8_ts of checksum that follows this structure */
    uint32_t length;
} ChecksumEntry;


#pragma pack(pop)

#endif //LIBDICFORMAT_STRUCTS_H

#pragma clang diagnostic pop