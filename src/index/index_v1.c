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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "aaruformat.h"
#include "log.h"
#include "utarray.h"

/**
 * @brief Processes an index block (version 1) from the image stream.
 *
 * Reads and parses an index block (version 1) from the image, returning an array of index entries.
 * This function handles the legacy index format used in early AaruFormat versions, providing
 * compatibility with older image files. It reads the IndexHeader structure followed by a
 * sequential list of IndexEntry structures, validating the index identifier for format correctness.
 *
 * @param ctx Pointer to the aaruformat context containing the image stream and header information.
 *
 * @return Returns one of the following values:
 * @retval UT_array* Successfully processed the index block. This is returned when:
 *         - The context and image stream are valid
 *         - The index header is successfully read from the position specified in ctx->header.indexOffset
 *         - The index identifier matches IndexBlock (legacy format identifier)
 *         - All index entries are successfully read and stored in the UT_array
 *         - Memory allocation for the index entries array succeeds
 *         - The returned array contains all index entries from the version 1 index block
 *
 * @retval NULL Index processing failed. This occurs when:
 *         - The context parameter is NULL
 *         - The image stream (ctx->imageStream) is NULL or invalid
 *         - Cannot read the IndexHeader structure from the image stream
 *         - The index identifier doesn't match IndexBlock (incorrect format or corruption)
 *         - Memory allocation fails for the UT_array structure
 *         - File I/O errors occur while reading index entries
 *
 * @note Index Structure (Version 1):
 *       - IndexHeader: Contains identifier (IndexBlock), entry count, and metadata
 *       - IndexEntry array: Sequential list of entries describing block locations and types
 *       - No CRC validation is performed during processing (use verify_index_v1 for validation)
 *       - No compression support in version 1 index format
 *
 * @note Memory Management:
 *       - Returns a newly allocated UT_array that must be freed by the caller using utarray_free()
 *       - On error, any partially allocated memory is cleaned up before returning NULL
 *       - Each IndexEntry is copied into the array (no reference to original stream data)
 *
 * @note Version Compatibility:
 *       - Supports only legacy IndexBlock format (not IndexBlock2 or IndexBlock3)
 *       - Compatible with early AaruFormat and DiscImageChef image files
 *       - Does not handle subindex or hierarchical index structures
 *
 * @warning The caller is responsible for freeing the returned UT_array using utarray_free().
 *          Failure to free the array will result in memory leaks.
 *
 * @warning This function does not validate the CRC integrity of the index data.
 *          Use verify_index_v1() to ensure index integrity before processing.
 *
 * @warning The function assumes ctx->header.indexOffset points to a valid index block.
 *          Invalid offsets may cause file access errors or reading incorrect data.
 */
UT_array *process_index_v1(aaruformatContext *ctx)
{
    TRACE("Entering process_index_v1(%p)", ctx);

    UT_array  *index_entries = NULL;
    IndexEntry entry;

    if(ctx == NULL || ctx->imageStream == NULL) return NULL;

    // Initialize the index entries array
    UT_icd index_entry_icd = {sizeof(IndexEntry), NULL, NULL, NULL};

    utarray_new(index_entries, &index_entry_icd);

    // Read the index header
    TRACE("Reading index header at position %llu", ctx->header.indexOffset);
    fseek(ctx->imageStream, ctx->header.indexOffset, SEEK_SET);
    IndexHeader idx_header;
    fread(&idx_header, sizeof(IndexHeader), 1, ctx->imageStream);

    // Check if the index header is valid
    if(idx_header.identifier != IndexBlock)
    {
        FATAL("Incorrect index identifier.");
        utarray_free(index_entries);

        TRACE("Exiting process_index_v1() = NULL");
        return NULL;
    }

    for(int i = 0; i < idx_header.entries; i++)
    {
        fread(&entry, sizeof(IndexEntry), 1, ctx->imageStream);
        utarray_push_back(index_entries, &entry);
    }

    TRACE("Read %d index entries from index block at position %llu", idx_header.entries, ctx->header.indexOffset);
    return index_entries;
}

/**
 * @brief Verifies the integrity of an index block (version 1) in the image stream.
 *
 * Checks the CRC64 of the index block without decompressing it. This function performs
 * comprehensive validation of the index structure including header validation, data
 * integrity verification, and version-specific CRC calculation. It ensures the index
 * block is valid and uncorrupted before the image can be safely used for data access.
 *
 * @param ctx Pointer to the aaruformat context containing image stream and header information.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully verified index integrity. This is returned when:
 *         - The context and image stream are valid
 *         - The index header is successfully read from ctx->header.indexOffset
 *         - The index identifier matches IndexBlock (version 1 format)
 *         - Memory allocation for index entries succeeds
 *         - All index entries are successfully read from the image stream
 *         - CRC64 calculation completes successfully with version-specific endianness handling
 *         - The calculated CRC64 matches the expected CRC64 in the index header
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) Invalid context or stream. This occurs when:
 *         - The context parameter is NULL
 *         - The image stream (ctx->imageStream) is NULL or invalid
 *
 * @retval AARUF_ERROR_CANNOT_READ_HEADER (-6) Index header reading failed. This occurs when:
 *         - Cannot read the complete IndexHeader structure from the image stream
 *         - File I/O errors prevent accessing the header at ctx->header.indexOffset
 *         - Insufficient data available at the specified index offset
 *
 * @retval AARUF_ERROR_CANNOT_READ_INDEX (-19) Index format or data access errors. This occurs when:
 *         - The index identifier doesn't match IndexBlock (wrong format or corruption)
 *         - Cannot read all index entries from the image stream
 *         - File I/O errors during index entry reading
 *         - Index structure is corrupted or truncated
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. This occurs when:
 *         - Cannot allocate memory for the index entries array
 *         - System memory exhaustion prevents loading index data for verification
 *
 * @retval AARUF_ERROR_INVALID_BLOCK_CRC (-18) CRC64 validation failed. This occurs when:
 *         - The calculated CRC64 doesn't match the expected CRC64 in the index header
 *         - Index data corruption is detected
 *         - Data integrity verification fails indicating potential file damage
 *
 * @note CRC64 Validation Process:
 *       - Reads all index entries into memory for CRC calculation
 *       - Calculates CRC64 over the complete index entries array
 *       - Applies version-specific endianness conversion for compatibility
 *       - For imageMajorVersion <= AARUF_VERSION_V1: Uses bswap_64() for byte order correction
 *       - Compares calculated CRC64 with the value stored in the index header
 *
 * @note Version Compatibility:
 *       - Handles legacy byte order differences from original C# implementation
 *       - Supports IndexBlock format (version 1 only)
 *       - Does not support IndexBlock2 or IndexBlock3 formats
 *
 * @note Memory Management:
 *       - Allocates temporary memory for index entries during verification
 *       - Automatically frees allocated memory on both success and error conditions
 *       - Memory usage is proportional to the number of index entries
 *
 * @note Verification Scope:
 *       - Validates index header structure and identifier
 *       - Verifies data integrity through CRC64 calculation
 *       - Does not validate individual index entry contents or block references
 *       - Does not check for logical consistency of referenced blocks
 *
 * @warning This function reads the entire index into memory for CRC calculation.
 *          Large indexes may require significant memory allocation.
 *
 * @warning The function assumes ctx->header.indexOffset points to a valid index location.
 *          Invalid offsets will cause file access errors or incorrect validation.
 *
 * @warning CRC validation failure indicates potential data corruption and may suggest
 *          the image file is damaged or has been modified outside of library control.
 */
int32_t verify_index_v1(aaruformatContext *ctx)
{
    TRACE("Entering verify_index_v1(%p)", ctx);

    size_t      read_bytes = 0;
    IndexHeader index_header;
    uint64_t    crc64         = 0;
    IndexEntry *index_entries = NULL;

    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");

        TRACE("Exiting verify_index_v1() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // This will traverse all blocks and check their CRC64 without uncompressing them
    TRACE("Checking index integrity at %llu.", ctx->header.indexOffset);
    fseek(ctx->imageStream, ctx->header.indexOffset, SEEK_SET);

    // Read the index header
    TRACE("Reading index header at position %llu", ctx->header.indexOffset);
    read_bytes = fread(&index_header, 1, sizeof(IndexHeader), ctx->imageStream);

    if(read_bytes != sizeof(IndexHeader))
    {
        FATAL("Could not read index header.");

        TRACE("Exiting verify_index_v1() = AARUF_ERROR_CANNOT_READ_HEADER");
        return AARUF_ERROR_CANNOT_READ_HEADER;
    }

    if(index_header.identifier != IndexBlock)
    {
        FATAL("Incorrect index identifier.");

        TRACE("Exiting verify_index_v1() = AARUF_ERROR_CANNOT_READ_INDEX");
        return AARUF_ERROR_CANNOT_READ_INDEX;
    }

    TRACE("Index at %llu contains %d entries.", ctx->header.indexOffset, index_header.entries);
    fprintf(stderr, "Index at %llu contains %d entries.", ctx->header.indexOffset, index_header.entries);

    index_entries = malloc(sizeof(IndexEntry) * index_header.entries);

    if(index_entries == NULL)
    {
        FATAL("Cannot allocate memory for index entries.");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    read_bytes = fread(index_entries, 1, sizeof(IndexEntry) * index_header.entries, ctx->imageStream);

    if(read_bytes != sizeof(IndexEntry) * index_header.entries)
    {
        FATAL("Could not read index entries.");
        free(index_entries);

        TRACE("Exiting verify_index_v1() = AARUF_ERROR_CANNOT_READ_INDEX");
        return AARUF_ERROR_CANNOT_READ_INDEX;
    }

    crc64 = aaruf_crc64_data((const uint8_t *)index_entries, sizeof(IndexEntry) * index_header.entries);

    // Due to how C# wrote it, it is effectively reversed
    if(ctx->header.imageMajorVersion <= AARUF_VERSION_V1) crc64 = bswap_64(crc64);

    if(crc64 != index_header.crc64)
    {
        FATAL("Expected index CRC 0x%16llX but got 0x%16llX.", index_header.crc64, crc64);
        free(index_entries);

        TRACE("Exiting verify_index_v1() = AARUF_ERROR_INVALID_BLOCK_CRC");
        return AARUF_ERROR_INVALID_BLOCK_CRC;
    }

    TRACE("Exiting verify_index_v1() = AARUF_OK");
    return AARUF_STATUS_OK;
}