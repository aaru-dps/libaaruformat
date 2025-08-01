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

#ifndef LIBAARUFORMAT_METADATA_H
#define LIBAARUFORMAT_METADATA_H

#pragma pack(push, 1)

/**Metadata block, contains metadata */
typedef struct MetadataBlockHeader {
    /**Identifier, <see cref="BlockType.MetadataBlock" /> */
    uint32_t identifier;
    /**Size in uint8_ts of this whole metadata block */
    uint32_t blockSize;
    /**Sequence of media set this media belongs to */
    int32_t mediaSequence;
    /**Total number of media on the media set this media belongs to */
    int32_t lastMediaSequence;
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

/**Geometry block, contains physical geometry information */
typedef struct CicmMetadataBlock {
    /**Identifier, <see cref="BlockType.CicmBlock" /> */
    uint32_t identifier;
    uint32_t length;
} CicmMetadataBlock;

#pragma pack(pop)

#endif //LIBAARUFORMAT_METADATA_H
