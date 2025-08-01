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

#ifndef LIBAARUFORMAT_HEADER_H
#define LIBAARUFORMAT_HEADER_H

#define AARU_HEADER_APP_NAME_LEN 64
#define GUID_SIZE 16

#pragma pack(push, 1)

/**Header, at start of file */
typedef struct AaruHeader {
    /**Header identifier, <see cref="AARU_MAGIC" /> */
    uint64_t identifier;
    /**UTF-16LE name of the application that created the image */
    uint8_t application[AARU_HEADER_APP_NAME_LEN];
    /**Image format major version. A new major version means a possibly incompatible change of format */
    uint8_t imageMajorVersion;
    /**Image format minor version. A new minor version indicates a compatible change of format */
    uint8_t imageMinorVersion;
    /**Major version of the application that created the image */
    uint8_t applicationMajorVersion;
    /**Minor version of the application that created the image */
    uint8_t applicationMinorVersion;
    /**Type of media contained on image */
    uint32_t mediaType;
    /**Offset to index */
    uint64_t indexOffset;
    /**Windows filetime (100 nanoseconds since 1601/01/01 00:00:00 UTC) of image creation time */
    int64_t creationTime;
    /**Windows filetime (100 nanoseconds since 1601/01/01 00:00:00 UTC) of image last written time */
    int64_t lastWrittenTime;
} AaruHeader;

/**Header, at start of file */
typedef struct AaruHeaderV2 {
    /**Header identifier, see AARU_MAGIC */
    uint64_t identifier;
    /**UTF-16LE name of the application that created the image */
    uint8_t application[AARU_HEADER_APP_NAME_LEN];
    /**Image format major version. A new major version means a possibly incompatible change of format */
    uint8_t imageMajorVersion;
    /**Image format minor version. A new minor version indicates a compatible change of format */
    uint8_t imageMinorVersion;
    /**Major version of the application that created the image */
    uint8_t applicationMajorVersion;
    /**Minor version of the application that created the image */
    uint8_t applicationMinorVersion;
    /**Type of media contained on image */
    uint32_t mediaType;
    /**Offset to index */
    uint64_t indexOffset;
    /**Windows filetime (100 nanoseconds since 1601/01/01 00:00:00 UTC) of image creation time */
    int64_t creationTime;
    /**Windows filetime (100 nanoseconds since 1601/01/01 00:00:00 UTC) of image last written time */
    int64_t lastWrittenTime;
    /**Unique identifier that allows children images to recognize and find this image.*/
    uint8_t guid[GUID_SIZE];
    /**Block alignment shift. All blocks in the image are aligned at 2 << blockAlignmentShift bytes */
    uint8_t blockAlignmentShift;
    /**Data shift. All data blocks in the image contain 2 << dataShift items at most */
    uint8_t dataShift;
    /**Table shift. All deduplication tables in the image use this shift to calculate the position of an item */
    uint8_t tableShift;
    /**Features used in this image that if unsupported are still compatible for reading and writing implementations */
    uint64_t featureCompatible;
    /**Features used in this image that if unsupported are still compatible for reading implementations but not for writing */
    uint64_t featureCompatibleRo;
    /**Featured used in this image that if unsupported prevent reading or writing the image*/
    uint64_t featureIncompatible;
} AaruHeaderV2;

#pragma pack(pop)

#endif //LIBAARUFORMAT_HEADER_H
