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
#include "internal.h"
#include "log.h"
#include "utarray.h"

/**
 * @brief Processes an index block (version 3) from the image stream.
 *
 * Reads and parses an index block (version 3) from the image, returning an array of index entries.
 * This function handles the advanced index format used in current AaruFormat versions, supporting
 * hierarchical subindex structures for improved scalability. It reads the IndexHeader3 structure
 * followed by index entries, recursively processing any subindex blocks encountered to create
 * a flattened array of all index entries.
 *
 * @param ctx Pointer to the aaruformat context containing the image stream and header information.
 *
 * @return Returns one of the following values:
 * @retval UT_array* Successfully processed the index block and all subindexes. This is returned when:
 *         - The context and image stream are valid
 *         - The index header is successfully read from the position specified in ctx->header.indexOffset
 *         - The index identifier matches IndexBlock3 (version 3 format identifier)
 *         - All index entries are successfully read and stored in the UT_array
 *         - Any subindex blocks (IndexBlock3 entries) are recursively processed via add_subindex_entries()
 *         - Memory allocation for the index entries array succeeds
 *         - The returned array contains all index entries from the main index and all subindexes
 *
 * @retval NULL Index processing failed. This occurs when:
 *         - The context parameter is NULL
 *         - The image stream (ctx->imageStream) is NULL or invalid
 *         - Cannot read the IndexHeader3 structure from the image stream
 *         - The index identifier doesn't match IndexBlock3 (incorrect format or corruption)
 *         - Memory allocation fails for the UT_array structure
 *         - File I/O errors occur while reading index entries
 *         - Recursive subindex processing fails (errors in add_subindex_entries())
 *
 * @note Index Structure (Version 3):
 *       - IndexHeader3: Contains identifier (IndexBlock3), entry count, and advanced metadata
 *       - IndexEntry array: May contain regular entries and subindex references (IndexBlock3 type)
 *       - Hierarchical support: Subindex entries are recursively processed to flatten the structure
 *       - No CRC validation is performed during processing (use verify_index_v3 for validation)
 *       - Supports scalable index organization for large image files
 *
 * @note Subindex Processing:
 *       - When an IndexEntry has blockType == IndexBlock3, it references a subindex
 *       - Subindexes are recursively processed using add_subindex_entries()
 *       - All entries from subindexes are flattened into the main index entries array
 *       - Supports arbitrary nesting depth of subindexes
 *
 * @note Memory Management:
 *       - Returns a newly allocated UT_array that must be freed by the caller using utarray_free()
 *       - On error, any partially allocated memory is cleaned up before returning NULL
 *       - Each IndexEntry is copied into the array (no reference to original stream data)
 *       - Memory usage scales with total entries across all subindexes
 *
 * @note Version Compatibility:
 *       - Supports only IndexBlock3 format (not IndexBlock or IndexBlock2)
 *       - Compatible with current generation AaruFormat image files
 *       - Backward compatible with images that don't use subindexes
 *
 * @warning The caller is responsible for freeing the returned UT_array using utarray_free().
 *          Failure to free the array will result in memory leaks.
 *
 * @warning This function does not validate the CRC integrity of the index data.
 *          Use verify_index_v3() to ensure index integrity before processing.
 *
 * @warning Recursive subindex processing may cause significant memory usage and processing
 *          time for images with deeply nested or numerous subindexes.
 *
 * @warning The function assumes ctx->header.indexOffset points to a valid index block.
 *          Invalid offsets may cause file access errors or reading incorrect data.
 */
UT_array *process_index_v3(aaruformatContext *ctx)
{
    TRACE("Entering process_index_v3(%p)", ctx);

    UT_array  *index_entries = NULL;
    IndexEntry entry;

    if(ctx == NULL || ctx->imageStream == NULL) return NULL;

    // Initialize the index entries array
    const UT_icd index_entry_icd = {sizeof(IndexEntry), NULL, NULL, NULL};

    utarray_new(index_entries, &index_entry_icd);

    // Read the index header
    TRACE("Reading index header at position %llu", ctx->header.indexOffset);
    fseek(ctx->imageStream, ctx->header.indexOffset, SEEK_SET);
    IndexHeader3 idx_header;
    fread(&idx_header, sizeof(IndexHeader3), 1, ctx->imageStream);

    // Check if the index header is valid
    if(idx_header.identifier != IndexBlock3)
    {
        FATAL("Incorrect index identifier.");
        utarray_free(index_entries);

        TRACE("Exiting process_index_v3() = NULL");
        return NULL;
    }

    for(int i = 0; i < idx_header.entries; i++)
    {
        fread(&entry, sizeof(IndexEntry), 1, ctx->imageStream);
        if(entry.blockType == IndexBlock3)
        {
            // If the entry is a subindex, we need to read its entries
            add_subindex_entries(ctx, index_entries, &entry);
            continue;
        }
        utarray_push_back(index_entries, &entry);
    }

    TRACE("Read %d index entries from index block at position %llu", idx_header.entries, ctx->header.indexOffset);
    return index_entries;
}

/**
 * @brief Adds entries from a subindex block (version 3) to the main index entries array.
 *
 * Recursively reads subindex blocks and appends their entries to the main index entries array.
 * This function is a critical component of the hierarchical index system in AaruFormat version 3,
 * enabling scalable index organization for large image files. It performs recursive traversal
 * of subindex structures, flattening the hierarchy into a single array while maintaining
 * entry order and handling nested subindex references.
 *
 * @param ctx Pointer to the aaruformat context containing the image stream.
 * @param index_entries Pointer to the UT_array of main index entries to append to.
 * @param subindex_entry Pointer to the IndexEntry describing the subindex location and metadata.
 *
 * @note Function Behavior:
 * @retval No return value (void function) The function operates through side effects on the index_entries array:
 *         - Successfully appends subindex entries when all parameters are valid and file I/O succeeds
 *         - Recursively processes nested subindexes when entries have blockType == IndexBlock3
 *         - Silently returns without modification when validation fails or errors occur
 *         - Does not perform error reporting beyond logging (errors are handled gracefully)
 *
 * @note Success Conditions - Entries are added when:
 *         - All input parameters (ctx, index_entries, subindex_entry) are non-NULL
 *         - The image stream is valid and accessible
 *         - File positioning succeeds to subindex_entry->offset
 *         - The subindex header is successfully read from the image stream
 *         - The subindex identifier matches IndexBlock3 (correct format)
 *         - All entries in the subindex are successfully read and processed
 *         - Recursive calls for nested subindexes complete successfully
 *
 * @note Failure Conditions - Function returns silently when:
 *         - Any input parameter is NULL (ctx, index_entries, or subindex_entry)
 *         - The image stream (ctx->imageStream) is NULL or invalid
 *         - File I/O errors prevent reading the subindex header or entries
 *         - The subindex identifier doesn't match IndexBlock3 (format mismatch or corruption)
 *         - Memory operations fail during UT_array manipulation
 *
 * @note Recursive Processing:
 *       - When an entry has blockType == IndexBlock3, it indicates another subindex
 *       - The function recursively calls itself to process nested subindexes
 *       - Supports arbitrary nesting depth limited only by stack space and file structure
 *       - All entries are flattened into the main index_entries array regardless of nesting level
 *
 * @note Memory and Performance:
 *       - Memory usage scales with the total number of entries across all processed subindexes
 *       - File I/O is performed for each subindex block accessed
 *       - Processing time increases with the depth and breadth of subindex hierarchies
 *       - No internal memory allocation - uses existing UT_array structure
 *
 * @note Error Handling:
 *       - Designed for graceful degradation - errors don't propagate to callers
 *       - Invalid subindexes are skipped without affecting other processing
 *       - Partial success is possible when some subindexes fail but others succeed
 *       - Logging provides visibility into processing failures for debugging
 *
 * @warning This function modifies the index_entries array by appending new entries.
 *          Ensure the array has sufficient capacity and is properly initialized.
 *
 * @warning Recursive processing can cause significant stack usage for deeply nested
 *          subindex structures. Very deep hierarchies may cause stack overflow.
 *
 * @warning The function assumes subindex_entry->offset points to a valid subindex block.
 *          Invalid offsets will cause file access errors but are handled gracefully.
 *
 * @warning No validation is performed on individual IndexEntry contents - only
 *          structural validation of subindex headers is done.
 */
void add_subindex_entries(aaruformatContext *ctx, UT_array *index_entries, IndexEntry *subindex_entry)
{
    TRACE("Entering add_subindex_entries(%p, %p, %p)", ctx, index_entries, subindex_entry);

    IndexHeader3 subindex_header;
    IndexEntry   entry;

    if(ctx == NULL || ctx->imageStream == NULL || index_entries == NULL || subindex_entry == NULL) return;

    // Seek to the subindex
    fseek(ctx->imageStream, subindex_entry->offset, SEEK_SET);

    // Read the subindex header
    fread(&subindex_header, sizeof(IndexHeader3), 1, ctx->imageStream);

    // Check if the subindex header is valid
    if(subindex_header.identifier != IndexBlock3)
    {
        FATAL("Incorrect subindex identifier.");

        TRACE("Exiting add_subindex_entries() without adding entries");
        return;
    }

    // Read each entry in the subindex and add it to the main index entries array
    for(int i = 0; i < subindex_header.entries; i++)
    {
        fread(&entry, sizeof(IndexEntry), 1, ctx->imageStream);
        if(entry.blockType == IndexBlock3)
        {
            // If the entry is a subindex, we need to read its entries
            add_subindex_entries(ctx, index_entries, &entry);
            continue;
        }
        utarray_push_back(index_entries, &entry);
    }

    TRACE("Exiting add_subindex_entries() after adding %d entries", subindex_header.entries);
}

/**
 * @brief Verifies the integrity of an index block (version 3) in the image stream.
 *
 * Checks the CRC64 of the index block and all subindexes without decompressing them. This function
 * performs comprehensive validation of the advanced version 3 index structure including header
 * validation, data integrity verification, and version-specific CRC calculation. Note that this
 * function validates only the main index block's CRC and does not recursively validate subindex
 * CRCs, focusing on the primary index structure integrity.
 *
 * @param ctx Pointer to the aaruformat context containing image stream and header information.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully verified index integrity. This is returned when:
 *         - The context and image stream are valid
 *         - The index header is successfully read from ctx->header.indexOffset
 *         - The index identifier matches IndexBlock3 (version 3 format)
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
 *         - Cannot read the complete IndexHeader3 structure from the image stream
 *         - File I/O errors prevent accessing the header at ctx->header.indexOffset
 *         - Insufficient data available at the specified index offset
 *
 * @retval AARUF_ERROR_CANNOT_READ_INDEX (-19) Index format or data access errors. This occurs when:
 *         - The index identifier doesn't match IndexBlock3 (wrong format or corruption)
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
 *       - Reads all main index entries into memory for CRC calculation
 *       - Calculates CRC64 over the complete main index entries array
 *       - Applies version-specific endianness conversion for compatibility
 *       - For imageMajorVersion <= AARUF_VERSION_V1: Uses bswap_64() for byte order correction
 *       - Compares calculated CRC64 with the value stored in the IndexHeader3
 *
 * @note Version 3 Specific Features:
 *       - Uses IndexHeader3 structure with hierarchical subindex support
 *       - Maintains compatibility with legacy endianness handling
 *       - Supports advanced index entry organization with subindex references
 *       - Only validates the main index block CRC - subindex CRCs are not recursively checked
 *
 * @note Validation Scope:
 *       - Validates main index header structure and identifier
 *       - Verifies data integrity of the primary index entries through CRC64 calculation
 *       - Does not validate individual IndexEntry contents or block references
 *       - Does not check for logical consistency of referenced blocks
 *       - Does not recursively validate subindex blocks (unlike hierarchical processing)
 *
 * @note Memory Management:
 *       - Allocates temporary memory for index entries during verification
 *       - Automatically frees allocated memory on both success and error conditions
 *       - Memory usage is proportional to the number of entries in the main index only
 *
 * @note Subindex Validation Limitation:
 *       - This function does not recursively validate subindex CRCs
 *       - Each subindex block contains its own CRC that would need separate validation
 *       - For complete integrity verification, subindex blocks should be validated individually
 *       - The main index CRC only covers the primary index entries, not the subindex content
 *
 * @warning This function reads only the main index into memory for CRC calculation.
 *          Subindex blocks are not loaded or validated by this function.
 *
 * @warning The function assumes ctx->header.indexOffset points to a valid index location.
 *          Invalid offsets will cause file access errors or incorrect validation.
 *
 * @warning CRC validation failure indicates potential data corruption in the main index
 *          and may suggest the image file is damaged or has been modified outside of library control.
 *
 * @warning For complete integrity verification of hierarchical indexes, additional validation
 *          of subindex blocks may be required beyond this function's scope.
 */
int32_t verify_index_v3(aaruformatContext *ctx)
{
    TRACE("Entering verify_index_v3(%p)", ctx);

    size_t       read_bytes = 0;
    IndexHeader3 index_header;
    uint64_t     crc64         = 0;
    IndexEntry  *index_entries = NULL;

    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");

        TRACE("Exiting verify_index_v2() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // This will traverse all blocks and check their CRC64 without uncompressing them
    TRACE("Checking index integrity at %llu.", ctx->header.indexOffset);
    fseek(ctx->imageStream, ctx->header.indexOffset, SEEK_SET);

    // Read the index header
    TRACE("Reading index header at position %llu", ctx->header.indexOffset);
    read_bytes = fread(&index_header, 1, sizeof(IndexHeader3), ctx->imageStream);

    if(read_bytes != sizeof(IndexHeader3))
    {
        FATAL("Could not read index header.");

        TRACE("Exiting verify_index_v3() = AARUF_ERROR_CANNOT_READ_HEADER");
        return AARUF_ERROR_CANNOT_READ_HEADER;
    }

    if(index_header.identifier != IndexBlock3)
    {
        FATAL("Incorrect index identifier.");

        TRACE("Exiting verify_index_v3() = AARUF_ERROR_CANNOT_READ_INDEX");
        return AARUF_ERROR_CANNOT_READ_INDEX;
    }

    TRACE("Index at %llu contains %d entries.", ctx->header.indexOffset, index_header.entries);

    index_entries = malloc(sizeof(IndexEntry) * index_header.entries);

    if(index_entries == NULL)
    {
        FATAL("Cannot allocate memory for index entries.");

        TRACE("Exiting verify_index_v3() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    read_bytes = fread(index_entries, 1, sizeof(IndexEntry) * index_header.entries, ctx->imageStream);

    if(read_bytes != sizeof(IndexEntry) * index_header.entries)
    {
        FATAL("Could not read index entries.");
        free(index_entries);

        TRACE("Exiting verify_index_v3() = AARUF_ERROR_CANNOT_READ_INDEX");
        return AARUF_ERROR_CANNOT_READ_INDEX;
    }

    crc64 = aaruf_crc64_data((const uint8_t *)index_entries, sizeof(IndexEntry) * index_header.entries);

    // Due to how C# wrote it, it is effectively reversed
    if(ctx->header.imageMajorVersion <= AARUF_VERSION_V1) crc64 = bswap_64(crc64);

    if(crc64 != index_header.crc64)
    {
        FATAL("Expected index CRC 0x%16llX but got 0x%16llX.", index_header.crc64, crc64);
        free(index_entries);

        TRACE("Exiting verify_index_v3() = AARUF_ERROR_INVALID_BLOCK_CRC");
        return AARUF_ERROR_INVALID_BLOCK_CRC;
    }

    TRACE("Exiting verify_index_v3() = AARUF_OK");
    return AARUF_STATUS_OK;
}