/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
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

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "aaruformat.h"
#include "internal.h"
#include "log.h"

/**
 * @brief Processes a DDT v2 block from the image stream.
 *
 * Reads and decompresses (if needed) a DDT v2 block, verifies its CRC, and loads it into memory.
 * This function handles both user data DDT blocks and CD sector prefix/suffix corrected DDT blocks,
 * supporting both LZMA compression and uncompressed formats. It performs CRC64 validation and
 * stores the processed DDT data in the appropriate context fields based on size type (small/big).
 *
 * @param ctx Pointer to the aaruformat context.
 * @param entry Pointer to the index entry describing the DDT block.
 * @param found_user_data_ddt Pointer to a boolean that will be set to true if a user data DDT was found and loaded.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully processed the DDT block. This is returned when:
 *         - The DDT block is successfully read, decompressed (if needed), and loaded into memory
 *         - CRC64 validation passes for the DDT data
 *         - User data DDT blocks are processed and context is properly updated
 *         - CD sector prefix/suffix corrected DDT blocks are processed successfully
 *         - Memory allocation failures occur for non-critical operations (processing continues)
 *         - File reading errors occur for compressed data or LZMA properties (processing continues)
 *         - Unknown compression types are encountered (block is skipped)
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context or image stream is invalid (NULL pointers).
 *
 * @retval AARUF_ERROR_CANNOT_READ_BLOCK (-7) Failed to access the DDT block in the image stream. This occurs when:
 *         - fseek() fails to position at the DDT block offset
 *         - The file position doesn't match the expected offset after seeking
 *         - Failed to read the DDT header from the image stream
 *         - The number of bytes read for the DDT header is insufficient
 *         - CRC64 context initialization fails (internal error)
 *
 * @retval AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK (-17) LZMA decompression failed. This can happen when:
 *         - The LZMA decoder returns a non-zero error code during decompression
 *         - The decompressed data size doesn't match the expected DDT block length
 *         - This error causes immediate function termination and memory cleanup
 *
 * @retval AARUF_ERROR_INVALID_BLOCK_CRC (-18) CRC64 validation failed. This occurs when:
 *         - Calculated CRC64 doesn't match the expected CRC64 in the DDT header
 *         - Data corruption is detected in the DDT block
 *         - This applies to both compressed and uncompressed DDT blocks
 *
 * @note Error Handling Strategy:
 *       - Critical errors (seek failures, header read failures, decompression failures, CRC failures) cause immediate
 * return
 *       - Non-critical errors (memory allocation failures, unknown compression types) allow processing to continue
 *       - The found_user_data_ddt flag is updated to reflect the success of user data DDT loading
 *
 * @note DDT v2 Features:
 *       - Handles multi-level DDT hierarchies with tableShift parameter
 *       - Updates context with sector counts, DDT version, and primary DDT offset
 *       - Stores DDT data in size-appropriate context fields (userDataDdtMini/Big, sectorPrefixDdt, etc.)
 *
 * @note Memory Management:
 *       - Allocated DDT data is stored in the context and becomes part of the context lifecycle
 *       - Memory is automatically cleaned up on decompression or CRC validation errors
 *       - Buffer memory is reused for the final DDT data storage (no double allocation)
 *
 * @note CRC Validation:
 *       - All DDT blocks undergo CRC64 validation regardless of compression type
 *       - CRC is calculated on the final decompressed data
 *       - Uses standard CRC64 calculation (no version-specific endianness conversion like v1)
 *
 * @warning The function modifies context state including sector count, DDT version, and primary DDT offset.
 *          Ensure proper context cleanup when the function completes.
 *
 * @warning Memory allocated for DDT data becomes part of the context and should not be freed separately.
 *          The context cleanup functions will handle DDT memory deallocation.
 */
int32_t process_ddt_v2(aaruformat_context *ctx, IndexEntry *entry, bool *found_user_data_ddt)
{
    TRACE("Entering process_ddt_v2(%p, %p, %d)", ctx, entry, *found_user_data_ddt);

    int        pos        = 0;
    size_t     read_bytes = 0;
    DdtHeader2 ddt_header;
    uint8_t   *cmp_data = NULL;
    uint8_t    lzma_properties[LZMA_PROPERTIES_LENGTH];
    size_t     lzma_size     = 0;
    int        error_no      = 0;
    crc64_ctx *crc64_context = NULL;
    uint64_t   crc64         = 0;
    uint8_t   *buffer        = NULL;

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");

        TRACE("Exiting process_ddt_v2() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Seek to block
    pos = fseek(ctx->imageStream, entry->offset, SEEK_SET);
    if(pos < 0 || ftell(ctx->imageStream) != entry->offset)
    {
        FATAL("Could not seek to %" PRIu64 " as indicated by index entry...", entry->offset);

        TRACE("Exiting process_ddt_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    // Even if those two checks shall have been done before
    TRACE("Reading DDT block header at position %" PRIu64, entry->offset);
    read_bytes = fread(&ddt_header, 1, sizeof(DdtHeader2), ctx->imageStream);

    if(read_bytes != sizeof(DdtHeader2))
    {
        FATAL("Could not read block header at %" PRIu64 "", entry->offset);

        TRACE("Exiting process_ddt_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    ctx->image_info.ImageSize += ddt_header.cmpLength;

    if(entry->dataType == kDataTypeUserData)
    {
        // User area sectors is blocks stored in DDT minus the negative and overflow displacement blocks
        ctx->image_info.Sectors   = ddt_header.blocks - ddt_header.negative - ddt_header.overflow;
        // We need the header later for the shift calculations
        ctx->user_data_ddt_header = ddt_header;
        ctx->ddt_version          = 2;
        // Store the primary DDT table's file offset for secondary table references
        ctx->primary_ddt_offset   = entry->offset;

        // Check for DDT compression
        switch(ddt_header.compression)
        {
            case kCompressionLzma:
                if(ddt_header.cmpLength <= LZMA_PROPERTIES_LENGTH)
                {
                    FATAL("Compressed DDT payload too small (%" PRIu64 ") for LZMA properties.", ddt_header.cmpLength);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                lzma_size = (size_t)(ddt_header.cmpLength - LZMA_PROPERTIES_LENGTH);

                cmp_data = (uint8_t *)malloc(lzma_size);
                if(cmp_data == NULL)
                {
                    TRACE("Cannot allocate memory for DDT, continuing...");
                    break;
                }

                buffer = malloc(ddt_header.length);
                if(buffer == NULL)
                {
                    TRACE("Cannot allocate memory for DDT, continuing...");
                    free(cmp_data);
                    break;
                }

                read_bytes = fread(lzma_properties, 1, LZMA_PROPERTIES_LENGTH, ctx->imageStream);
                if(read_bytes != LZMA_PROPERTIES_LENGTH)
                {
                    TRACE("Could not read LZMA properties, continuing...");
                    free(cmp_data);
                    free(buffer);
                    break;
                }

                read_bytes = fread(cmp_data, 1, lzma_size, ctx->imageStream);
                if(read_bytes != lzma_size)
                {
                    TRACE("Could not read compressed block, continuing...");
                    free(cmp_data);
                    free(buffer);
                    break;
                }

                read_bytes = ddt_header.length;
                TRACE("Decompressing block of size %zu bytes", ddt_header.length);
                error_no = aaruf_lzma_decode_buffer(buffer, &read_bytes, cmp_data, &lzma_size, lzma_properties,
                                                    LZMA_PROPERTIES_LENGTH);

                if(error_no != 0)
                {
                    FATAL("Got error %d from LZMA, stopping...", error_no);
                    free(cmp_data);
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                if(read_bytes != ddt_header.length)
                {
                    FATAL("Error decompressing block, should be {0} bytes but got {1} bytes., stopping...");
                    free(cmp_data);
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                free(cmp_data);

                crc64_context = aaruf_crc64_init();

                if(crc64_context == NULL)
                {
                    FATAL("Could not initialize CRC64.");
                    free(buffer);

                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                aaruf_crc64_update(crc64_context, buffer, read_bytes);
                aaruf_crc64_final(crc64_context, &crc64);
                aaruf_crc64_free(crc64_context);

                if(crc64 != ddt_header.crc64)
                {
                    FATAL("Expected DDT CRC 0x%16lX but got 0x%16lX.", ddt_header.crc64, crc64);
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_INVALID_BLOCK_CRC");
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                ctx->user_data_ddt2 = (uint64_t *)buffer;

                ctx->in_memory_ddt   = true;
                *found_user_data_ddt = true;

                break;
            case kCompressionNone:
                buffer = malloc(ddt_header.length);

                if(buffer == NULL)
                {
                    TRACE("Cannot allocate memory for DDT, continuing...");
                    break;
                }

                TRACE("Reading DDT of length %zu bytes", ddt_header.length);
                read_bytes = fread(buffer, 1, ddt_header.length, ctx->imageStream);

                if(read_bytes != ddt_header.length)
                {
                    free(buffer);
                    FATAL("Could not read deduplication table, continuing...");
                    break;
                }

                crc64_context = aaruf_crc64_init();

                if(crc64_context == NULL)
                {
                    FATAL("Could not initialize CRC64.");
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                aaruf_crc64_update(crc64_context, buffer, read_bytes);
                aaruf_crc64_final(crc64_context, &crc64);
                aaruf_crc64_free(crc64_context);

                if(crc64 != ddt_header.crc64)
                {
                    FATAL("Expected DDT CRC 0x%16lX but got 0x%16lX.", ddt_header.crc64, crc64);
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_INVALID_BLOCK_CRC");
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                ctx->user_data_ddt2 = (uint64_t *)buffer;

                ctx->in_memory_ddt   = true;
                *found_user_data_ddt = true;

                break;
            default:
                TRACE("Found unknown compression type %d, continuing...", ddt_header.compression);
                *found_user_data_ddt = false;
                break;
        }
    }
    else if(entry->dataType == kDataTypeCdSectorPrefix || entry->dataType == kDataTypeCdSectorSuffix)
        switch(ddt_header.compression)
        {
            case kCompressionLzma:
                if(ddt_header.cmpLength <= LZMA_PROPERTIES_LENGTH)
                {
                    FATAL("Compressed DDT payload too small (%" PRIu64 ") for LZMA properties.", ddt_header.cmpLength);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                lzma_size = (size_t)(ddt_header.cmpLength - LZMA_PROPERTIES_LENGTH);

                cmp_data = (uint8_t *)malloc(lzma_size);
                if(cmp_data == NULL)
                {
                    TRACE("Cannot allocate memory for DDT, continuing...");
                    break;
                }

                buffer = malloc(ddt_header.length);
                if(buffer == NULL)
                {
                    TRACE("Cannot allocate memory for DDT, continuing...");
                    free(cmp_data);
                    break;
                }

                read_bytes = fread(lzma_properties, 1, LZMA_PROPERTIES_LENGTH, ctx->imageStream);
                if(read_bytes != LZMA_PROPERTIES_LENGTH)
                {
                    TRACE("Could not read LZMA properties, continuing...");
                    free(cmp_data);
                    free(buffer);
                    break;
                }

                read_bytes = fread(cmp_data, 1, lzma_size, ctx->imageStream);
                if(read_bytes != lzma_size)
                {
                    TRACE("Could not read compressed block, continuing...");
                    free(cmp_data);
                    free(buffer);
                    break;
                }

                read_bytes = ddt_header.length;
                TRACE("Decompressing block of size %zu bytes", ddt_header.length);
                error_no = aaruf_lzma_decode_buffer(buffer, &read_bytes, cmp_data, &lzma_size, lzma_properties,
                                                    LZMA_PROPERTIES_LENGTH);

                if(error_no != 0)
                {
                    FATAL("Got error %d from LZMA, stopping...", error_no);
                    free(cmp_data);
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                if(read_bytes != ddt_header.length)
                {
                    FATAL("Error decompressing block, should be {0} bytes but got {1} bytes., stopping...");
                    free(cmp_data);
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                free(cmp_data);
                cmp_data = NULL;

                crc64_context = aaruf_crc64_init();

                if(crc64_context == NULL)
                {
                    FATAL("Could not initialize CRC64.");
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                aaruf_crc64_update(crc64_context, buffer, read_bytes);
                aaruf_crc64_final(crc64_context, &crc64);
                aaruf_crc64_free(crc64_context);

                if(crc64 != ddt_header.crc64)
                {
                    FATAL("Expected DDT CRC 0x%16lX but got 0x%16lX.", ddt_header.crc64, crc64);
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_INVALID_BLOCK_CRC");
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                if(entry->dataType == kDataTypeCdSectorPrefix)
                    ctx->sector_prefix_ddt2 = (uint64_t *)buffer;
                else if(entry->dataType == kDataTypeCdSectorSuffix)
                    ctx->sector_suffix_ddt2 = (uint64_t *)buffer;
                else
                    free(buffer);

                break;

            case kCompressionNone:
                buffer = malloc(ddt_header.length);

                if(buffer == NULL)
                {
                    TRACE("Cannot allocate memory for deduplication table.");
                    break;
                }

                read_bytes = fread(buffer, 1, ddt_header.length, ctx->imageStream);

                if(read_bytes != ddt_header.length)
                {
                    free(buffer);
                    FATAL("Could not read deduplication table, continuing...");
                    break;
                }

                crc64_context = aaruf_crc64_init();

                if(crc64_context == NULL)
                {
                    FATAL("Could not initialize CRC64.");
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                aaruf_crc64_update(crc64_context, buffer, read_bytes);
                aaruf_crc64_final(crc64_context, &crc64);
                aaruf_crc64_free(crc64_context);

                if(crc64 != ddt_header.crc64)
                {
                    FATAL("Expected DDT CRC 0x%16lX but got 0x%16lX.", ddt_header.crc64, crc64);
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_INVALID_BLOCK_CRC");
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                if(entry->dataType == kDataTypeCdSectorPrefix)
                    ctx->sector_prefix_ddt2 = (uint64_t *)buffer;
                else if(entry->dataType == kDataTypeCdSectorSuffix)
                    ctx->sector_suffix_ddt2 = (uint64_t *)buffer;
                else
                    free(buffer);

                break;
            default:
                TRACE("Found unknown compression type %d, continuing...", ddt_header.compression);
                break;
        }

    TRACE("Exiting process_ddt_v2() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Decodes a DDT v2 entry for a given sector address.
 *
 * Determines the offset and block offset for a sector using the DDT v2 table(s). This function acts
 * as a dispatcher that automatically selects between single-level and multi-level DDT decoding based
 * on the tableShift parameter in the DDT header. It provides a unified interface for DDT v2 entry
 * decoding regardless of the underlying table structure complexity.
 *
 * @param ctx Pointer to the aaruformat context containing the loaded DDT structures.
 * @param sector_address Logical sector address to decode (will be adjusted for negative sectors).
 * @param negative Indicates if the sector address is negative.
 * @param offset Pointer to store the resulting sector offset within the block.
 * @param block_offset Pointer to store the resulting block offset in the image.
 * @param sector_status Pointer to store the sector status (dumped, not dumped, etc.).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully decoded the DDT entry. This is always returned when:
 *         - The context and image stream are valid
 *         - The appropriate decoding function (single-level or multi-level) completes successfully
 *         - All output parameters are properly populated with decoded values
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context or image stream is invalid (NULL pointers).
 *         This is the only error condition that can occur at this dispatcher level.
 *
 * @retval Other error codes may be returned by the underlying decoding functions:
 *         - From decode_ddt_single_level_v2(): AARUF_ERROR_CANNOT_READ_BLOCK (-7)
 *         - From decode_ddt_multi_level_v2(): AARUF_ERROR_CANNOT_READ_BLOCK (-7),
 *           AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK (-17), AARUF_ERROR_INVALID_BLOCK_CRC (-18)
 *
 * @note Function Selection:
 *       - If tableShift > 0: Uses multi-level DDT decoding (decode_ddt_multi_level_v2)
 *       - If tableShift = 0: Uses single-level DDT decoding (decode_ddt_single_level_v2)
 *       - The tableShift parameter is read from ctx->userDataDdtHeader.tableShift
 *
 * @note This function performs minimal validation and primarily acts as a dispatcher.
 *       Most error conditions and complex logic are handled by the underlying functions.
 *
 * @warning The function assumes the DDT has been properly loaded by process_ddt_v2().
 *          Calling this function with an uninitialized or corrupted DDT will result in
 *          undefined behavior from the underlying decoding functions.
 *
 * @warning All output parameters must be valid pointers. No bounds checking is performed
 *          on the sector_address parameter at this level.
 */
int32_t decode_ddt_entry_v2(aaruformat_context *ctx, const uint64_t sector_address, bool negative, uint64_t *offset,
                            uint64_t *block_offset, uint8_t *sector_status)
{
    TRACE("Entering decode_ddt_entry_v2(%p, %" PRIu64 ", %d, %llu, %llu, %d)", ctx, sector_address, negative, *offset,
          *block_offset, *sector_status);
    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");

        TRACE("Exiting decode_ddt_entry_v2() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->user_data_ddt_header.tableShift > 0)
        return decode_ddt_multi_level_v2(ctx, sector_address, negative, offset, block_offset, sector_status);

    return decode_ddt_single_level_v2(ctx, sector_address, negative, offset, block_offset, sector_status);
}

/**
 * @brief Decodes a single-level DDT v2 entry for a given sector address.
 *
 * Used when the DDT table does not use multi-level indirection (tableShift = 0). This function
 * performs direct lookup in the primary DDT table to extract sector offset, block offset, and
 * sector status information. It performs bit manipulation to decode the packed DDT entry values.
 *
 * @param ctx Pointer to the aaruformat context containing the loaded DDT table.
 * @param sector_address Logical sector address to decode (adjusted for negative sectors).
 * @param negative Indicates if the sector address is negative.
 * @param offset Pointer to store the resulting sector offset within the block.
 * @param block_offset Pointer to store the resulting block offset in the image.
 * @param sector_status Pointer to store the sector status (dumped, not dumped, etc.).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully decoded the DDT entry. This is always returned when:
 *         - The context and image stream are valid
 *         - The tableShift validation passes (must be 0)
 *         - The DDT size type is recognized (SmallDdtSizeType or BigDdtSizeType)
 *         - The DDT entry is successfully extracted and decoded
 *         - All output parameters are properly populated with decoded values
 *         - Zero DDT entries are handled (indicates sector not dumped)
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context or image stream is invalid (NULL pointers).
 *
 * @retval AARUF_ERROR_CANNOT_READ_BLOCK (-7) Configuration or validation errors. This occurs when:
 *         - The tableShift is not zero (should use multi-level decoding instead)
 *         - The DDT size type is unknown/unsupported (not SmallDdtSizeType or BigDdtSizeType)
 *         - Internal consistency checks fail
 *
 * @note DDT Entry Decoding
 *       - Bits 63-61: Sector status (4 bits)
 *       - Bits 60-0: Combined offset and block index (60 bits)
 *       - Offset mask: Derived from dataShift parameter
 *       - Block offset: Calculated using blockAlignmentShift parameter
 *
 * @note Negative Sector Handling:
 *       - Sector address is automatically adjusted by adding ctx->userDataDdtHeader.negative
 *       - This allows proper indexing into the DDT table for negative sector addresses
 *
 * @note Zero Entry Handling:
 *       - A zero DDT entry indicates the sector was not dumped
 *       - Sets sector_status to SectorStatusNotDumped and zeros offset/block_offset
 *       - This is a normal condition and not an error
 *
 * @warning The function assumes the DDT table has been properly loaded and is accessible
 *          via ctx->userDataDdtMini or ctx->userDataDdtBig depending on size type.
 *
 * @warning No bounds checking is performed on sector_address. Accessing beyond the DDT
 *          table boundaries will result in undefined behavior.
 *
 * @warning This function should only be called when tableShift is 0. Calling it with
 *          tableShift > 0 will result in AARUF_ERROR_CANNOT_READ_BLOCK.
 */
int32_t decode_ddt_single_level_v2(aaruformat_context *ctx, uint64_t sector_address, bool negative, uint64_t *offset,
                                   uint64_t *block_offset, uint8_t *sector_status)
{
    TRACE("Entering decode_ddt_single_level_v2(%p, %" PRIu64 ", %d, %llu, %llu, %d)", ctx, sector_address, negative,
          *offset, *block_offset, *sector_status);

    uint64_t ddt_entry = 0;

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");

        TRACE("Exiting decode_ddt_single_level_v2() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Should not really be here
    if(ctx->user_data_ddt_header.tableShift != 0)
    {
        FATAL("DDT table shift is not zero, but we are in single-level DDT decoding.");
        TRACE("Exiting decode_ddt_single_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    // Calculate positive or negative sector
    if(negative)
        sector_address = ctx->user_data_ddt_header.negative - sector_address;
    else
        sector_address += ctx->user_data_ddt_header.negative;

    ddt_entry = ctx->user_data_ddt2[sector_address];

    if(ddt_entry == 0)
    {
        *sector_status = SectorStatusNotDumped;
        *offset        = 0;
        *block_offset  = 0;
        TRACE("Exiting decode_ddt_single_level_v2(%p, %" PRIu64 ", %llu, %llu, %d) = AARUF_STATUS_OK", ctx,
              sector_address, *offset, *block_offset, *sector_status);
        return AARUF_STATUS_OK;
    }

    *sector_status = ddt_entry >> 60;
    ddt_entry &= 0xFFFFFFFFFFFFFFF;

    const uint64_t offset_mask = (uint64_t)((1 << ctx->user_data_ddt_header.dataShift) - 1);
    *offset                    = ddt_entry & offset_mask;
    *block_offset =
        (ddt_entry >> ctx->user_data_ddt_header.dataShift) * (1 << ctx->user_data_ddt_header.blockAlignmentShift);

    TRACE("Exiting decode_ddt_single_level_v2(%p, %" PRIu64 ", %d, %llu, %llu, %d) = AARUF_STATUS_OK", ctx,
          sector_address, negative, *offset, *block_offset, *sector_status);
    return AARUF_STATUS_OK;
}

/**
 * @brief Decodes a multi-level DDT v2 entry for a given sector address.
 *
 * Used when the DDT table uses multi-level indirection (tableShift > 0). This function handles
 * the complex process of navigating a hierarchical DDT structure where the primary table points
 * to secondary tables that contain the actual sector mappings. It includes caching mechanisms
 * for secondary tables, supports both compressed and uncompressed secondary tables, and performs
 * comprehensive validation including CRC verification.
 *
 * @param ctx Pointer to the aaruformat context containing the loaded primary DDT table.
 * @param sector_address Logical sector address to decode (adjusted for negative sectors).
 * @param negative Indicates if the sector address is negative.
 * @param offset Pointer to store the resulting sector offset within the block.
 * @param block_offset Pointer to store the resulting block offset in the image.
 * @param sector_status Pointer to store the sector status (dumped, not dumped, etc.).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully decoded the DDT entry. This is returned when:
 *         - The context and image stream are valid
 *         - The tableShift validation passes (must be > 0)
 *         - The DDT size type is recognized (SmallDdtSizeType or BigDdtSizeType)
 *         - Secondary DDT table is successfully loaded (from cache or file)
 *         - Secondary DDT decompression succeeds (if needed)
 *         - Secondary DDT CRC validation passes
 *         - The DDT entry is successfully extracted and decoded
 *         - All output parameters are properly populated with decoded values
 *         - Zero DDT entries are handled (indicates sector not dumped)
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context or image stream is invalid (NULL pointers).
 *
 * @retval AARUF_ERROR_CANNOT_READ_BLOCK (-7) Configuration, validation, or file access errors. This occurs when:
 *         - The tableShift is zero (should use single-level decoding instead)
 *         - The DDT size type is unknown/unsupported (not SmallDdtSizeType or BigDdtSizeType)
 *         - Cannot read the secondary DDT header from the image stream
 *         - Secondary DDT header validation fails (wrong identifier or type)
 *         - Cannot read uncompressed secondary DDT data from the image stream
 *         - CRC64 context initialization fails (internal error)
 *         - Memory allocation fails for secondary DDT data (critical failure)
 *         - Unknown compression type encountered in secondary DDT
 *
 * @retval AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK (-17) LZMA decompression failed for secondary DDT. This occurs when:
 *         - Memory allocation fails for compressed data or decompression buffer
 *         - Cannot read LZMA properties from the image stream
 *         - Cannot read compressed secondary DDT data from the image stream
 *         - The LZMA decoder returns a non-zero error code during decompression
 *         - The decompressed data size doesn't match the expected secondary DDT length
 *
 * @retval AARUF_ERROR_INVALID_BLOCK_CRC (-18) CRC64 validation failed for secondary DDT. This occurs when:
 *         - Calculated CRC64 doesn't match the expected CRC64 in the secondary DDT header
 *         - Data corruption is detected in the secondary DDT data
 *         - This applies to both compressed and uncompressed secondary DDT blocks
 *
 * @note Multi-level DDT Navigation:
 *       - Uses tableShift to calculate items per DDT entry (2^tableShift)
 *       - Calculates DDT position by dividing sector address by items per entry
 *       - Retrieves secondary DDT offset from primary table at calculated position
 *       - Converts block offset to file offset using blockAlignmentShift
 *
 * @note Secondary DDT Caching:
 *       - Maintains a single cached secondary DDT in memory (ctx->cachedSecondaryDdtSmall/Big)
 *       - Compares requested offset with cached offset (ctx->cachedDdtOffset)
 *       - Only loads from disk if the requested secondary DDT is not currently cached
 *       - Caching improves performance for sequential sector access patterns
 *
 * @note Secondary DDT Processing:
 *       - Supports both LZMA compression and uncompressed formats
 *       - Performs full CRC64 validation of secondary DDT data
 *       - Same bit manipulation as single-level DDT for final entry decoding
 *
 * @note Error Handling Strategy:
 *       - Memory allocation failures for secondary DDT loading are treated as critical errors
 *       - File I/O errors and validation failures cause immediate function termination
 *       - Unknown compression types are treated as errors (unlike the processing functions)
 *       - All allocated memory is cleaned up on error conditions
 *
 * @warning This function should only be called when tableShift > 0. Calling it with
 *          tableShift = 0 will result in AARUF_ERROR_CANNOT_READ_BLOCK.
 *
 * @warning The function assumes the primary DDT table has been properly loaded and is accessible
 *          via ctx->userDataDdtMini or ctx->userDataDdtBig depending on size type.
 *
 * @warning Secondary DDT caching means that memory usage can increase during operation.
 *          The cached secondary DDT is replaced when a different secondary table is needed.
 *
 * @warning No bounds checking is performed on sector_address or calculated DDT positions.
 *          Accessing beyond table boundaries will result in undefined behavior.
 */
int32_t decode_ddt_multi_level_v2(aaruformat_context *ctx, uint64_t sector_address, bool negative, uint64_t *offset,
                                  uint64_t *block_offset, uint8_t *sector_status)
{
    TRACE("Entering decode_ddt_multi_level_v2(%p, %" PRIu64 ", %d, %llu, %llu, %d)", ctx, sector_address, negative,
          *offset, *block_offset, *sector_status);

    uint64_t   ddt_entry = 0;
    uint8_t    lzma_properties[LZMA_PROPERTIES_LENGTH];
    size_t     lzma_size            = 0;
    uint8_t   *cmp_data             = NULL;
    uint8_t   *buffer               = NULL;
    crc64_ctx *crc64_context        = NULL;
    uint64_t   crc64                = 0;
    int        items_per_ddt_entry  = 0;
    uint64_t   ddt_position         = 0;
    uint64_t   secondary_ddt_offset = 0;

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");

        TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Should not really be here
    if(ctx->user_data_ddt_header.tableShift == 0)
    {
        FATAL("DDT table shift is zero, but we are in multi-level DDT decoding.");
        TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    // Calculate positive or negative sector
    if(negative)
        sector_address = ctx->user_data_ddt_header.negative - sector_address;
    else
        sector_address += ctx->user_data_ddt_header.negative;

    items_per_ddt_entry  = 1 << ctx->user_data_ddt_header.tableShift;
    ddt_position         = sector_address / items_per_ddt_entry;
    secondary_ddt_offset = ctx->user_data_ddt2[ddt_position];

    // Position in file of the child DDT table
    secondary_ddt_offset *= 1 << ctx->user_data_ddt_header.blockAlignmentShift;

    // Is the one we have cached the same as the one we need to read?
    if(ctx->cached_ddt_offset != secondary_ddt_offset)
    {
        int32_t error_no = 0;
        fseek(ctx->imageStream, secondary_ddt_offset, SEEK_SET);
        DdtHeader2 ddt_header;
        size_t     read_bytes = fread(&ddt_header, 1, sizeof(DdtHeader2), ctx->imageStream);

        if(read_bytes != sizeof(DdtHeader2))
        {
            FATAL("Could not read block header at %" PRIu64 "", secondary_ddt_offset);
            TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
            return AARUF_ERROR_CANNOT_READ_BLOCK;
        }

        if((ddt_header.identifier != DeDuplicationTableSecondary &&
            ddt_header.identifier != DeDuplicationTableSAlpha) ||
           ddt_header.type != kDataTypeUserData)
        {
            FATAL("Invalid block header at %" PRIu64 "", secondary_ddt_offset);
            TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
            return AARUF_ERROR_CANNOT_READ_BLOCK;
        }

        // Check for DDT compression
        switch(ddt_header.compression)
        {
            case kCompressionLzma:
                if(ddt_header.cmpLength <= LZMA_PROPERTIES_LENGTH)
                {
                    FATAL("Compressed DDT payload too small (%" PRIu64 ") for LZMA properties.", ddt_header.cmpLength);
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                lzma_size = (size_t)(ddt_header.cmpLength - LZMA_PROPERTIES_LENGTH);

                cmp_data = (uint8_t *)malloc(lzma_size);
                if(cmp_data == NULL)
                {
                    FATAL("Cannot allocate memory for DDT, stopping...");
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                buffer = malloc(ddt_header.length);
                if(buffer == NULL)
                {
                    FATAL("Cannot allocate memory for DDT, stopping...");
                    free(cmp_data);
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                read_bytes = fread(lzma_properties, 1, LZMA_PROPERTIES_LENGTH, ctx->imageStream);
                if(read_bytes != LZMA_PROPERTIES_LENGTH)
                {
                    FATAL("Could not read LZMA properties, stopping...");
                    free(cmp_data);
                    free(buffer);
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                read_bytes = fread(cmp_data, 1, lzma_size, ctx->imageStream);
                if(read_bytes != lzma_size)
                {
                    FATAL("Could not read compressed block, stopping...");
                    free(cmp_data);
                    free(buffer);
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                TRACE("Decompressing block of size %zu bytes", ddt_header.length);
                read_bytes = ddt_header.length;
                error_no   = aaruf_lzma_decode_buffer(buffer, &read_bytes, cmp_data, &lzma_size, lzma_properties,
                                                      LZMA_PROPERTIES_LENGTH);

                if(error_no != 0)
                {
                    FATAL("Got error %d from LZMA, stopping...", error_no);
                    free(cmp_data);
                    free(buffer);
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                if(read_bytes != ddt_header.length)
                {
                    FATAL("Error decompressing block, should be {0} bytes but got {1} bytes., stopping...");
                    free(cmp_data);
                    free(buffer);
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                free(cmp_data);

                crc64_context = aaruf_crc64_init();

                if(crc64_context == NULL)
                {
                    FATAL("Could not initialize CRC64.");
                    free(buffer);
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                aaruf_crc64_update(crc64_context, buffer, read_bytes);
                aaruf_crc64_final(crc64_context, &crc64);
                aaruf_crc64_free(crc64_context);

                if(crc64 != ddt_header.crc64)
                {
                    FATAL("Expected DDT CRC 0x%16lX but got 0x%16lX.", ddt_header.crc64, crc64);
                    free(buffer);
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_INVALID_BLOCK_CRC");
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                // Free old cached DDT before replacing it
                free(ctx->cached_secondary_ddt2);

                ctx->cached_secondary_ddt2 = (uint64_t *)buffer;

                ctx->cached_ddt_offset = secondary_ddt_offset;

                break;
            case kCompressionNone:
                buffer = malloc(ddt_header.length);

                if(buffer == NULL)
                {
                    FATAL("Cannot allocate memory for DDT, stopping...");
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                read_bytes = fread(buffer, 1, ddt_header.length, ctx->imageStream);

                if(read_bytes != ddt_header.length)
                {
                    free(buffer);
                    FATAL("Could not read deduplication table, stopping...");
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                crc64_context = aaruf_crc64_init();

                if(crc64_context == NULL)
                {
                    FATAL("Could not initialize CRC64.");
                    free(buffer);
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                aaruf_crc64_update(crc64_context, buffer, read_bytes);
                aaruf_crc64_final(crc64_context, &crc64);
                aaruf_crc64_free(crc64_context);

                if(crc64 != ddt_header.crc64)
                {
                    FATAL("Expected DDT CRC 0x%16lX but got 0x%16lX.", ddt_header.crc64, crc64);
                    free(buffer);
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_INVALID_BLOCK_CRC");
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                // Free old cached DDT before replacing it
                free(ctx->cached_secondary_ddt2);

                ctx->cached_secondary_ddt2 = (uint64_t *)buffer;

                ctx->cached_ddt_offset = secondary_ddt_offset;

                break;
            default:
                FATAL("Found unknown compression type %d, stopping...", ddt_header.compression);
                TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
                return AARUF_ERROR_CANNOT_READ_BLOCK;
        }
    }

    ddt_entry = ctx->cached_secondary_ddt2[sector_address % items_per_ddt_entry];

    if(ddt_entry == 0)
    {
        *sector_status = SectorStatusNotDumped;
        *offset        = 0;
        *block_offset  = 0;

        TRACE("Exiting decode_ddt_multi_level_v2(%p, %" PRIu64 ", %llu, %llu, %d) = AARUF_STATUS_OK", ctx,
              sector_address, *offset, *block_offset, *sector_status);
        return AARUF_STATUS_OK;
    }

    *sector_status = ddt_entry >> 60;
    ddt_entry &= 0x0FFFFFFFFFFFFFFF;

    const uint64_t offset_mask = (uint64_t)((1 << ctx->user_data_ddt_header.dataShift) - 1);
    *offset                    = ddt_entry & offset_mask;
    *block_offset =
        (ddt_entry >> ctx->user_data_ddt_header.dataShift) * (1 << ctx->user_data_ddt_header.blockAlignmentShift);

    TRACE("Exiting decode_ddt_multi_level_v2(%p, %" PRIu64 ", %d, %llu, %llu, %d) = AARUF_STATUS_OK", ctx,
          sector_address, negative, *offset, *block_offset, *sector_status);
    return AARUF_STATUS_OK;
}

/**
 * @brief Sets a DDT v2 entry for a given sector address.
 *
 * Updates the DDT v2 table(s) with the specified offset, block offset, and sector status for a sector.
 *
 * @param ctx Pointer to the aaruformat context.
 * @param sector_address Logical sector address to set.
 * @param negative Indicates if the sector address is negative.
 * @param offset Offset to set for the sector.
 * @param block_offset Block offset to set for the sector.
 * @param sector_status Status to set for the sector.
 * @param ddt_entry Existing DDT entry or 0 to create a new one. If 0, a new entry is returned.
 *
 * @return Returns one of the following status codes:
 * @retval true if the entry was set successfully, false otherwise.
 */
bool set_ddt_entry_v2(aaruformat_context *ctx, const uint64_t sector_address, const bool negative,
                      const uint64_t offset, const uint64_t block_offset, const uint8_t sector_status,
                      uint64_t *ddt_entry)
{
    TRACE("Entering set_ddt_entry_v2(%p, %" PRIu64 ", %d, %llu, %llu, %d)", ctx, sector_address, negative, offset,
          block_offset, sector_status);

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");
        return false;
    }

    if(ctx->user_data_ddt_header.tableShift > 0)
        return set_ddt_multi_level_v2(ctx, sector_address, negative, offset, block_offset, sector_status, ddt_entry);

    return set_ddt_single_level_v2(ctx, sector_address, negative, offset, block_offset, sector_status, ddt_entry);
}

/**
 * @brief Sets a single-level DDT v2 entry for a given sector address.
 *
 * Used when the DDT table does not use multi-level indirection.
 *
 * @param ctx Pointer to the aaruformat context.
 * @param sector_address Logical sector address to set.
 * @param negative Indicates if the sector address is negative.
 * @param offset Offset to set for the sector.
 * @param block_offset Block offset to set for the sector.
 * @param sector_status Status to set for the sector.
 * @param ddt_entry Existing DDT entry or 0 to create a new one. If 0, a new entry is returned.
 *
 * @return Returns one of the following status codes:
 * @retval true if the entry was set successfully, false otherwise.
 */
bool set_ddt_single_level_v2(aaruformat_context *ctx, uint64_t sector_address, const bool negative,
                             const uint64_t offset, const uint64_t block_offset, const uint8_t sector_status,
                             uint64_t *ddt_entry)
{
    TRACE("Entering set_ddt_single_level_v2(%p, %" PRIu64 ", %d, %llu, %llu, %d)", ctx, sector_address, negative,
          offset, block_offset, sector_status);

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");
        TRACE("Exiting set_ddt_single_level_v2() = false");
        return false;
    }

    // Should not really be here
    if(ctx->user_data_ddt_header.tableShift != 0)
    {
        FATAL("DDT table shift is not zero, but we are in single-level DDT setting.");
        TRACE("Exiting set_ddt_single_level_v2() = false");
        return false;
    }

    // Calculate positive or negative sector
    if(negative)
        sector_address = ctx->user_data_ddt_header.negative - sector_address;
    else
        sector_address += ctx->user_data_ddt_header.negative;

    if(*ddt_entry == 0)
    {
        const uint64_t block_index = block_offset >> ctx->user_data_ddt_header.blockAlignmentShift;
        *ddt_entry                 = offset & (1ULL << ctx->user_data_ddt_header.dataShift) - 1 |
                     block_index << ctx->user_data_ddt_header.dataShift;

        // Overflow detection for DDT entry
        if(*ddt_entry > 0xFFFFFFFFFFFFFFF)
        {
            FATAL("DDT overflow: media does not fit in big DDT");
            TRACE("Exiting set_ddt_single_level_v2() = false");
            return false;
        }
    }

    // Sector status can be different from previous deduplicated sector
    *ddt_entry &= 0x0FFFFFFFFFFFFFFF;
    *ddt_entry |= (uint64_t)sector_status << 60;

    TRACE("Setting big single-level DDT entry %d to %ull", sector_address, (uint64_t)*ddt_entry);
    ctx->user_data_ddt2[sector_address] = *ddt_entry;
    ctx->dirty_single_level_ddt         = true;  // Mark single-level DDT as dirty

    TRACE("Exiting set_ddt_single_level_v2() = true");
    return true;
}

/**
 * @brief Sets a multi-level DDT v2 entry for a given sector address.
 *
 * Used when the DDT table uses multi-level indirection (tableShift > 0).
 *
 * @param ctx Pointer to the aaruformat context.
 * @param sector_address Logical sector address to set.
 * @param negative Indicates if the sector address is negative.
 * @param offset Offset to set for the sector.
 * @param block_offset Block offset to set for the sector.
 * @param sector_status Status to set for the sector.
 * @param ddt_entry Existing DDT entry or 0 to create a new one. If 0, a new entry is returned.
 *
 * @return Returns one of the following status codes:
 * @retval true if the entry was set successfully, false otherwise.
 */
bool set_ddt_multi_level_v2(aaruformat_context *ctx, uint64_t sector_address, bool negative, uint64_t offset,
                            uint64_t block_offset, uint8_t sector_status, uint64_t *ddt_entry)
{
    TRACE("Entering set_ddt_multi_level_v2(%p, %" PRIu64 ", %d, %" PRIu64 ", %" PRIu64 ", %d)", ctx, sector_address,
          negative, offset, block_offset, sector_status);

    uint64_t   items_per_ddt_entry  = 0;
    uint64_t   ddt_position         = 0;
    uint64_t   secondary_ddt_offset = 0;
    uint64_t   block_index          = 0;
    uint8_t   *buffer               = NULL;
    crc64_ctx *crc64_context        = NULL;
    uint64_t   crc64                = 0;
    DdtHeader2 ddt_header;
    size_t     written_bytes    = 0;
    long       end_of_file      = 0;
    bool       create_new_table = false;

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");
        TRACE("Exiting set_ddt_multi_level_v2() = false");
        return false;
    }

    // Should not really be here
    if(ctx->user_data_ddt_header.tableShift == 0)
    {
        FATAL("DDT table shift is zero, but we are in multi-level DDT setting.");
        TRACE("Exiting set_ddt_multi_level_v2() = false");
        return false;
    }

    // Calculate positive or negative sector
    if(negative)
        sector_address = ctx->user_data_ddt_header.negative - sector_address;
    else
        sector_address += ctx->user_data_ddt_header.negative;

    // Step 1: Calculate the corresponding secondary level table
    items_per_ddt_entry  = 1 << ctx->user_data_ddt_header.tableShift;
    ddt_position         = sector_address / items_per_ddt_entry;
    secondary_ddt_offset = ctx->user_data_ddt2[ddt_position];

    // Position in file of the child DDT table
    secondary_ddt_offset *= 1 << ctx->user_data_ddt_header.blockAlignmentShift;

    // Step 2: Check if it corresponds to the currently in-memory cached secondary level table
    if(ctx->cached_ddt_offset == secondary_ddt_offset && secondary_ddt_offset != 0)
    {
        // Update the corresponding DDT entry directly in the cached table
        if(*ddt_entry == 0)
        {
            block_index = block_offset >> ctx->user_data_ddt_header.blockAlignmentShift;
            *ddt_entry  = offset & (1ULL << ctx->user_data_ddt_header.dataShift) - 1 |
                         block_index << ctx->user_data_ddt_header.dataShift;

            // Overflow detection for DDT entry
            if(*ddt_entry > 0xFFFFFFFFFFFFFFF)
            {
                FATAL("DDT overflow: media does not fit in big DDT");
                TRACE("Exiting set_ddt_multi_level_v2() = false");
                return false;
            }
        }

        // Sector status can be different from previous deduplicated sector
        *ddt_entry &= 0x0FFFFFFFFFFFFFFF;
        *ddt_entry |= (uint64_t)sector_status << 60;

        TRACE("Setting small secondary DDT entry %d to %ull", sector_address % items_per_ddt_entry,
              (uint64_t)*ddt_entry);
        ctx->cached_secondary_ddt2[sector_address % items_per_ddt_entry] = *ddt_entry;
        ctx->dirty_secondary_ddt                                         = true;  // Mark secondary DDT as dirty

        TRACE("Updated cached secondary DDT entry at position %" PRIu64, sector_address % items_per_ddt_entry);
        TRACE("Exiting set_ddt_multi_level_v2() = true");
        return true;
    }

    // Step 2.5: Handle case where we have a cached secondary DDT that has never been written to disk
    // but does not contain the requested block
    if(ctx->cached_ddt_offset == 0 && (ctx->cached_secondary_ddt2 != NULL))
    {
        // Only write the cached table to disk if the requested block belongs to a different DDT position
        if(ddt_position != ctx->cached_ddt_position)
        {
            TRACE("Current secondary DDT in memory belongs to position %" PRIu64
                  " but requested block needs position %" PRIu64,
                  ctx->cached_ddt_position, ddt_position);

            // Write the cached DDT to disk before proceeding with the new one

            // Close the current data block first
            if(ctx->writing_buffer != NULL) aaruf_close_current_block(ctx);

            // Get current position and seek to end of file
            fseek(ctx->imageStream, 0, SEEK_END);
            end_of_file = ftell(ctx->imageStream);

            // Align to block boundary
            uint64_t alignment_mask = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
            end_of_file             = end_of_file + alignment_mask & ~alignment_mask;
            fseek(ctx->imageStream, end_of_file, SEEK_SET);

            // Prepare DDT header for the never-written cached table
            memset(&ddt_header, 0, sizeof(DdtHeader2));
            ddt_header.identifier = DeDuplicationTableSecondary;
            ddt_header.type       = kDataTypeUserData;
            ddt_header.compression =
                ctx->compression_enabled ? kCompressionLzma : kCompressionNone;  // Use no compression for simplicity
            ddt_header.levels              = ctx->user_data_ddt_header.levels;
            ddt_header.tableLevel          = ctx->user_data_ddt_header.tableLevel + 1;
            ddt_header.previousLevelOffset = ctx->primary_ddt_offset;
            ddt_header.negative            = ctx->user_data_ddt_header.negative;
            ddt_header.blocks              = items_per_ddt_entry;
            ddt_header.overflow            = ctx->user_data_ddt_header.overflow;
            ddt_header.start = ctx->cached_ddt_position * items_per_ddt_entry;  // Use cached position with table shift
            ddt_header.blockAlignmentShift = ctx->user_data_ddt_header.blockAlignmentShift;
            ddt_header.dataShift           = ctx->user_data_ddt_header.dataShift;
            ddt_header.tableShift          = 0;  // Secondary tables are single level
            ddt_header.entries             = items_per_ddt_entry;

            // Calculate data size

            ddt_header.length = items_per_ddt_entry * sizeof(uint64_t);

            // Calculate CRC64 of the data
            crc64_context = aaruf_crc64_init();
            if(crc64_context == NULL)
            {
                FATAL("Could not initialize CRC64.");
                TRACE("Exiting set_ddt_multi_level_v2() = false");
                return false;
            }

            aaruf_crc64_update(crc64_context, (uint8_t *)ctx->cached_secondary_ddt2, (uint32_t)ddt_header.length);

            aaruf_crc64_final(crc64_context, &crc64);
            aaruf_crc64_free(crc64_context);
            ddt_header.crc64 = crc64;

            uint8_t *cmp_buffer                              = NULL;
            uint8_t  lzma_properties[LZMA_PROPERTIES_LENGTH] = {0};

            if(ddt_header.compression == kCompressionNone)
            {

                cmp_buffer          = (uint8_t *)ctx->cached_secondary_ddt2;
                ddt_header.cmpCrc64 = ddt_header.crc64;
            }
            else
            {
                cmp_buffer = malloc((size_t)ddt_header.length * 2);  // Allocate double size for compression
                if(cmp_buffer == NULL)
                {
                    TRACE("Failed to allocate memory for secondary DDT v2 compression");
                    return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                }

                size_t dst_size   = (size_t)ddt_header.length * 2 * 2;
                size_t props_size = LZMA_PROPERTIES_LENGTH;
                aaruf_lzma_encode_buffer(cmp_buffer, &dst_size, (uint8_t *)ctx->cached_secondary_ddt2,
                                         ddt_header.length, lzma_properties, &props_size, 9, ctx->lzma_dict_size, 4, 0,
                                         2, 273, LZMA_THREADS(ctx));

                ddt_header.cmpLength = (uint32_t)dst_size;

                if(ddt_header.cmpLength >= ddt_header.length)
                {
                    ddt_header.compression = kCompressionNone;
                    free(cmp_buffer);

                    cmp_buffer = (uint8_t *)ctx->cached_secondary_ddt2;
                }
            }

            if(ddt_header.compression == kCompressionNone)
            {
                ddt_header.cmpLength = ddt_header.length;
                ddt_header.cmpCrc64  = ddt_header.crc64;
            }
            else
                ddt_header.cmpCrc64 = aaruf_crc64_data(cmp_buffer, (uint32_t)ddt_header.cmpLength);

            if(ddt_header.compression == kCompressionLzma) ddt_header.cmpLength += LZMA_PROPERTIES_LENGTH;

            // Write header
            written_bytes = fwrite(&ddt_header, sizeof(DdtHeader2), 1, ctx->imageStream);
            if(written_bytes != 1)
            {
                FATAL("Could not write never-written DDT header to file.");
                TRACE("Exiting set_ddt_multi_level_v2() = false");
                return false;
            }

            // Write data
            if(ddt_header.compression == kCompressionLzma)
                fwrite(lzma_properties, LZMA_PROPERTIES_LENGTH, 1, ctx->imageStream);

            if(fwrite(cmp_buffer, ddt_header.cmpLength, 1, ctx->imageStream) != 1)
            {
                FATAL("Could not write never-written DDT data to file.");
                TRACE("Exiting set_ddt_multi_level_v2() = false");
                return false;
            }

            if(ddt_header.compression == kCompressionLzma) free(cmp_buffer);

            // Add index entry for the newly written secondary DDT
            IndexEntry new_ddt_entry;
            new_ddt_entry.blockType = DeDuplicationTableSecondary;
            new_ddt_entry.dataType  = kDataTypeUserData;
            new_ddt_entry.offset    = end_of_file;

            utarray_push_back(ctx->index_entries, &new_ddt_entry);
            ctx->dirty_index_block = true;
            TRACE("Added new DDT index entry for never-written table at offset %" PRIu64, end_of_file);

            // Update the primary level table entry to point to the new location of the secondary table
            uint64_t new_secondary_table_block_offset = end_of_file >> ctx->user_data_ddt_header.blockAlignmentShift;

            ctx->user_data_ddt2[ctx->cached_ddt_position] = new_secondary_table_block_offset;
            ctx->dirty_primary_ddt                        = true;  // Mark primary DDT as dirty

            // Write the updated primary table back to its original position in the file
            long saved_pos = ftell(ctx->imageStream);
            fseek(ctx->imageStream, ctx->primary_ddt_offset + sizeof(DdtHeader2), SEEK_SET);

            size_t primary_table_size = ctx->user_data_ddt_header.entries * sizeof(uint64_t);

            written_bytes = fwrite(ctx->user_data_ddt2, primary_table_size, 1, ctx->imageStream);

            if(written_bytes != 1)
            {
                FATAL("Could not flush primary DDT table to file after writing never-written secondary table.");
                TRACE("Exiting set_ddt_multi_level_v2() = false");
                return false;
            }

            // Update nextBlockPosition to ensure future blocks don't overwrite the DDT
            uint64_t ddt_total_size  = sizeof(DdtHeader2) + ddt_header.length;
            ctx->next_block_position = end_of_file + ddt_total_size + alignment_mask & ~alignment_mask;
            block_offset             = ctx->next_block_position;
            offset                   = 0;
            TRACE("Updated nextBlockPosition after never-written DDT write to %" PRIu64, ctx->next_block_position);

            // Free the cached table

            free(ctx->cached_secondary_ddt2);
            ctx->cached_secondary_ddt2 = NULL;

            // Reset cached values since we've written and freed the table
            ctx->cached_ddt_offset   = 0;
            ctx->cached_ddt_position = 0;

            // Restore file position
            fseek(ctx->imageStream, saved_pos, SEEK_SET);

            TRACE("Successfully wrote never-written cached secondary DDT to disk");
        }
        else
            // The cached DDT is actually for the requested block range, so we can use it directly
            TRACE("Cached DDT is for the correct block range, using it directly");
        // No need to write to disk, just continue with the cached table
    }

    // Step 3: Write the currently in-memory cached secondary level table to the end of the file
    if(ctx->cached_ddt_offset != 0)
    {
        long current_pos = 0;
        // Close the current data block first
        if(ctx->writing_buffer != NULL) aaruf_close_current_block(ctx);

        // Get current position and seek to end of file
        current_pos = ftell(ctx->imageStream);
        fseek(ctx->imageStream, 0, SEEK_END);
        end_of_file = ftell(ctx->imageStream);

        // Align to block boundary
        uint64_t alignment_mask = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
        end_of_file             = end_of_file + alignment_mask & ~alignment_mask;
        fseek(ctx->imageStream, end_of_file, SEEK_SET);

        // Prepare DDT header for the cached table
        memset(&ddt_header, 0, sizeof(DdtHeader2));
        ddt_header.identifier          = DeDuplicationTableSecondary;
        ddt_header.type                = kDataTypeUserData;
        ddt_header.compression         = ctx->compression_enabled ? kCompressionLzma : kCompressionNone;
        ddt_header.levels              = ctx->user_data_ddt_header.levels;
        ddt_header.tableLevel          = ctx->user_data_ddt_header.tableLevel + 1;
        ddt_header.previousLevelOffset = ctx->primary_ddt_offset;  // Set to primary DDT table location
        ddt_header.negative            = ctx->user_data_ddt_header.negative;
        ddt_header.blocks              = items_per_ddt_entry;
        ddt_header.overflow            = ctx->user_data_ddt_header.overflow;
        ddt_header.start               = ddt_position * items_per_ddt_entry;  // First block this DDT table references
        ddt_header.blockAlignmentShift = ctx->user_data_ddt_header.blockAlignmentShift;
        ddt_header.dataShift           = ctx->user_data_ddt_header.dataShift;
        ddt_header.tableShift          = 0;  // Secondary tables are single level
        ddt_header.entries             = items_per_ddt_entry;

        // Calculate data size

        ddt_header.length = items_per_ddt_entry * sizeof(uint64_t);

        // Calculate CRC64 of the data
        crc64_context = aaruf_crc64_init();
        if(crc64_context == NULL)
        {
            FATAL("Could not initialize CRC64.");
            TRACE("Exiting set_ddt_multi_level_v2() = false");
            return false;
        }

        aaruf_crc64_update(crc64_context, (uint8_t *)ctx->cached_secondary_ddt2, ddt_header.length);

        aaruf_crc64_final(crc64_context, &crc64);
        aaruf_crc64_free(crc64_context);
        ddt_header.crc64 = crc64;

        uint8_t *cmp_buffer                              = NULL;
        uint8_t  lzma_properties[LZMA_PROPERTIES_LENGTH] = {0};

        if(ddt_header.compression == kCompressionNone)
        {

            cmp_buffer          = (uint8_t *)ctx->cached_secondary_ddt2;
            ddt_header.cmpCrc64 = ddt_header.crc64;
        }
        else
        {
            cmp_buffer = malloc((size_t)ddt_header.length * 2);  // Allocate double size for compression
            if(cmp_buffer == NULL)
            {
                TRACE("Failed to allocate memory for secondary DDT v2 compression");
                return AARUF_ERROR_NOT_ENOUGH_MEMORY;
            }

            size_t dst_size   = (size_t)ddt_header.length * 2 * 2;
            size_t props_size = LZMA_PROPERTIES_LENGTH;
            aaruf_lzma_encode_buffer(cmp_buffer, &dst_size, (uint8_t *)ctx->cached_secondary_ddt2, ddt_header.length,
                                     lzma_properties, &props_size, 9, ctx->lzma_dict_size, 4, 0, 2, 273, LZMA_THREADS(ctx));

            ddt_header.cmpLength = (uint32_t)dst_size;

            if(ddt_header.cmpLength >= ddt_header.length)
            {
                ddt_header.compression = kCompressionNone;
                free(cmp_buffer);

                cmp_buffer = (uint8_t *)ctx->cached_secondary_ddt2;
            }
        }

        if(ddt_header.compression == kCompressionNone)
        {
            ddt_header.cmpLength = ddt_header.length;
            ddt_header.cmpCrc64  = ddt_header.crc64;
        }
        else
            ddt_header.cmpCrc64 = aaruf_crc64_data(cmp_buffer, (uint32_t)ddt_header.cmpLength);

        if(ddt_header.compression == kCompressionLzma) ddt_header.cmpLength += LZMA_PROPERTIES_LENGTH;

        // Write header
        if(ddt_header.compression == kCompressionLzma)
            fwrite(lzma_properties, LZMA_PROPERTIES_LENGTH, 1, ctx->imageStream);

        written_bytes = fwrite(&ddt_header, sizeof(DdtHeader2), 1, ctx->imageStream);
        if(written_bytes != 1)
        {
            FATAL("Could not write DDT header to file.");
            TRACE("Exiting set_ddt_multi_level_v2() = false");
            return false;
        }

        // Write data
        written_bytes = fwrite(cmp_buffer, ddt_header.cmpLength, 1, ctx->imageStream);

        if(written_bytes != 1)
        {
            FATAL("Could not write DDT data to file.");
            TRACE("Exiting set_ddt_multi_level_v2() = false");
            return false;
        }

        if(ddt_header.compression == kCompressionLzma) free(cmp_buffer);

        // Update index: remove old entry and add new one for the evicted secondary DDT
        TRACE("Updating index for evicted secondary DDT");

        // Remove old index entry for the cached DDT
        if(ctx->cached_ddt_offset != 0)
        {
            TRACE("Removing old index entry for DDT at offset %" PRIu64, ctx->cached_ddt_offset);
            IndexEntry *entry = NULL;

            // Find and remove the old index entry
            for(unsigned int i = 0; i < utarray_len(ctx->index_entries); i++)
            {
                entry = (IndexEntry *)utarray_eltptr(ctx->index_entries, i);
                if(entry && entry->offset == ctx->cached_ddt_offset &&
                   (entry->blockType == DeDuplicationTableSecondary || entry->blockType == DeDuplicationTableSAlpha))
                {
                    TRACE("Found old DDT index entry at position %u, removing", i);
                    utarray_erase(ctx->index_entries, i, 1);
                    break;
                }
            }
        }

        // Add new index entry for the newly written secondary DDT
        IndexEntry new_ddt_entry;
        new_ddt_entry.blockType = DeDuplicationTableSecondary;
        new_ddt_entry.dataType  = kDataTypeUserData;
        new_ddt_entry.offset    = end_of_file;

        utarray_push_back(ctx->index_entries, &new_ddt_entry);
        ctx->dirty_index_block = true;
        TRACE("Added new DDT index entry at offset %" PRIu64, end_of_file);

        // Step 4: Update the primary level table entry and flush it back to file
        uint64_t new_secondary_table_block_offset = end_of_file >> ctx->user_data_ddt_header.blockAlignmentShift;

        // Update the primary table entry to point to the new location of the secondary table
        // Use ddtPosition which was calculated from sectorAddress, not cachedDdtOffset

        ctx->user_data_ddt2[ddt_position] = new_secondary_table_block_offset;
        ctx->dirty_primary_ddt            = true;  // Mark primary DDT as dirty

        // Write the updated primary table back to its original position in the file
        long saved_pos = ftell(ctx->imageStream);
        fseek(ctx->imageStream, ctx->primary_ddt_offset + sizeof(DdtHeader2), SEEK_SET);

        size_t primary_table_size = ctx->user_data_ddt_header.entries * sizeof(uint64_t);

        written_bytes = fwrite(ctx->user_data_ddt2, primary_table_size, 1, ctx->imageStream);

        if(written_bytes != 1)
        {
            FATAL("Could not flush primary DDT table to file.");
            TRACE("Exiting set_ddt_multi_level_v2() = false");
            return false;
        }

        // Update nextBlockPosition to ensure future blocks don't overwrite the DDT
        uint64_t ddt_total_size  = sizeof(DdtHeader2) + ddt_header.length;
        ctx->next_block_position = end_of_file + ddt_total_size + alignment_mask & ~alignment_mask;
        block_offset             = ctx->next_block_position;
        offset                   = 0;
        TRACE("Updated nextBlockPosition after DDT write to %" PRIu64, ctx->next_block_position);

        fseek(ctx->imageStream, saved_pos, SEEK_SET);

        // Free the cached table

        free(ctx->cached_secondary_ddt2);
        ctx->cached_secondary_ddt2 = NULL;

        // Restore file position
        fseek(ctx->imageStream, current_pos, SEEK_SET);
    }

    // Step 5: Check if the specified block already has an existing secondary level table
    create_new_table = ctx->cached_secondary_ddt2 == NULL;

    if(!create_new_table && secondary_ddt_offset != 0)
    {
        // Load existing table
        fseek(ctx->imageStream, secondary_ddt_offset, SEEK_SET);
        size_t read_bytes = fread(&ddt_header, 1, sizeof(DdtHeader2), ctx->imageStream);

        if(read_bytes != sizeof(DdtHeader2) || ddt_header.identifier != DeDuplicationTable2 ||
           ddt_header.type != kDataTypeUserData)
        {
            FATAL("Invalid secondary DDT header at %" PRIu64, secondary_ddt_offset);
            TRACE("Exiting set_ddt_multi_level_v2() = false");
            return false;
        }

        // Read the table data (assuming no compression for now)
        buffer = malloc(ddt_header.length);
        if(buffer == NULL)
        {
            FATAL("Cannot allocate memory for secondary DDT.");
            TRACE("Exiting set_ddt_multi_level_v2() = false");
            return false;
        }

        read_bytes = fread(buffer, 1, ddt_header.length, ctx->imageStream);
        if(read_bytes != ddt_header.length)
        {
            FATAL("Could not read secondary DDT data.");
            free(buffer);
            TRACE("Exiting set_ddt_multi_level_v2() = false");
            return false;
        }

        // Verify CRC
        crc64_context = aaruf_crc64_init();
        if(crc64_context == NULL)
        {
            FATAL("Could not initialize CRC64.");
            free(buffer);
            TRACE("Exiting set_ddt_multi_level_v2() = false");
            return false;
        }

        aaruf_crc64_update(crc64_context, buffer, read_bytes);
        aaruf_crc64_final(crc64_context, &crc64);
        aaruf_crc64_free(crc64_context);

        if(crc64 != ddt_header.crc64)
        {
            FATAL("Secondary DDT CRC mismatch. Expected 0x%16lX but got 0x%16lX.", ddt_header.crc64, crc64);
            free(buffer);
            TRACE("Exiting set_ddt_multi_level_v2() = false");
            return false;
        }

        // Cache the loaded table

        ctx->cached_secondary_ddt2 = (uint64_t *)buffer;

        ctx->cached_ddt_offset = secondary_ddt_offset;
    }

    if(create_new_table)
    {
        // Create a new empty table
        size_t table_size = items_per_ddt_entry * sizeof(uint64_t);

        buffer = calloc(1, table_size);
        if(buffer == NULL)
        {
            FATAL("Cannot allocate memory for new secondary DDT.");
            TRACE("Exiting set_ddt_multi_level_v2() = false");
            return false;
        }

        ctx->cached_secondary_ddt2 = (uint64_t *)buffer;

        ctx->cached_ddt_offset   = 0;             // Will be set when written to file
        ctx->cached_ddt_position = ddt_position;  // Track which primary DDT position this new table belongs to
        TRACE("Created new secondary DDT for position %" PRIu64, ddt_position);
    }

    // Step 6: Update the corresponding DDT entry
    if(*ddt_entry == 0)
    {
        block_index = block_offset >> ctx->user_data_ddt_header.blockAlignmentShift;
        *ddt_entry  = offset & (1ULL << ctx->user_data_ddt_header.dataShift) - 1 |
                     block_index << ctx->user_data_ddt_header.dataShift;

        // Overflow detection for DDT entry
        if(*ddt_entry > 0xFFFFFFFFFFFFFFF)
        {
            FATAL("DDT overflow: media does not fit in big DDT");
            TRACE("Exiting set_ddt_multi_level_v2() = false");
            return false;
        }
    }

    // Sector status can be different from previous deduplicated sector
    *ddt_entry &= 0x0FFFFFFFFFFFFFFF;
    *ddt_entry |= (uint64_t)sector_status << 60;

    TRACE("Setting big secondary DDT entry %d to %ull", sector_address % items_per_ddt_entry, (uint64_t)*ddt_entry);
    ctx->cached_secondary_ddt2[sector_address % items_per_ddt_entry] = *ddt_entry;
    ctx->dirty_secondary_ddt                                         = true;

    TRACE("Updated secondary DDT entry at position %" PRIu64, sector_address % items_per_ddt_entry);
    TRACE("Exiting set_ddt_multi_level_v2() = true");
    return true;
}

/**
 * @brief Sets a DDT entry for tape media using a hash-based lookup table.
 *
 * This function is specifically designed for tape media images where sectors are accessed
 * non-sequentially and the traditional DDT array structure is inefficient. Instead of using
 * a large contiguous array, it uses a hash table (UTHASH) to store only the sectors that
 * have been written, providing sparse storage for tape media.
 *
 * The function performs the following operations:
 * 1. Validates the context and verifies it's a tape image
 * 2. Constructs a DDT entry encoding offset, block alignment, and sector status
 * 3. Creates a hash table entry with the sector address as the key
 * 4. Inserts or replaces the entry in the tape DDT hash table
 *
 * **DDT Entry Format:**
 * The DDT entry is a 64-bit value with the following bit layout:
 * ```
 * Bits 0-(dataShift-1):    Sector offset within block (masked by dataShift)
 * Bits dataShift-27:       Block index (block_offset >> blockAlignmentShift)
 * Bits 28-31:              Sector status (4 bits for status flags)
 * Bits 32-63:              Unused (reserved for future use)
 * ```
 *
 * **Hash Table Management:**
 * Uses HASH_REPLACE macro from UTHASH library which:
 * - Adds new entries if the key (sector_address) doesn't exist
 * - Replaces existing entries if the key is found (automatically frees old entry)
 * - Maintains O(1) average lookup time for sector address resolution
 *
 * **Overflow Detection:**
 * The function checks if the constructed DDT entry exceeds 28 bits (0xFFFFFFF).
 * This limit ensures the sector status can fit in the upper 4 bits while leaving
 * room for future extensions in the upper 32 bits.
 *
 * @param ctx Pointer to the aaruformat context. Must not be NULL.
 *            The context must have a valid imageStream and is_tape must be true.
 *            The ctx->tapeDdt hash table will be updated with the new entry.
 *            The ctx->userDataDdtHeader contains alignment and shift parameters.
 *
 * @param sector_address Logical sector address on the tape to set. This serves as
 *                       the unique key in the hash table. Multiple calls with the
 *                       same sector_address will replace the previous entry.
 *
 * @param offset Byte offset within the aligned block where the sector data begins.
 *               This value is masked by (1 << dataShift) - 1 to extract only the
 *               lower bits representing the offset within the block.
 *
 * @param block_offset Absolute byte offset in the image file where the data block starts.
 *                     This is right-shifted by blockAlignmentShift to get the block index,
 *                     which is stored in the DDT entry's middle bits.
 *
 * @param sector_status Status flags for the sector (4 bits). Common values include:
 *                      - 0x0 (SectorStatusNotDumped): Sector not yet acquired during image dumping
 *                      - 0x1 (SectorStatusDumped): Sector successfully dumped without error
 *                      - 0x2 (SectorStatusErrored): Error during dumping; data may be incomplete or corrupt
 *                      - 0x3 (SectorStatusMode1Correct): Valid MODE 1 data with regenerable suffix/prefix
 *                      - 0x4 (SectorStatusMode2Form1Ok): Suffix verified/regenerable for MODE 2 Form 1
 *                      - 0x5 (SectorStatusMode2Form2Ok): Suffix matches MODE 2 Form 2 with valid CRC
 *                      - 0x6 (SectorStatusMode2Form2NoCrc): Suffix matches MODE 2 Form 2 but CRC empty/missing
 *                      - 0x7 (SectorStatusTwin): Pointer references a twin sector table
 *                      - 0x8 (SectorStatusUnrecorded): Sector physically unrecorded; repeated reads non-deterministic
 *                      - 0x9 (SectorStatusEncrypted): Content encrypted and stored encrypted in image
 *                      - 0xA (SectorStatusUnencrypted): Content originally encrypted but stored decrypted in image
 *                      See SectorStatus enum for complete list of defined values
 *
 * @param ddt_entry Pointer to a 64-bit value that will receive the constructed DDT entry.
 *                  - If *ddt_entry is 0: A new entry is constructed from the provided parameters
 *                  - If *ddt_entry is non-zero: The existing value is used directly
 *                  The constructed or provided value is stored in the hash table.
 *
 * @return Returns one of the following status codes:
 * @retval true Successfully created and inserted the DDT entry. This occurs when:
 *         - The context and image stream are valid
 *         - The image is confirmed to be a tape image (is_tape == true)
 *         - The DDT entry fits within the 28-bit limit (< 0xFFFFFFF)
 *         - Memory allocation for the hash entry succeeds
 *         - The entry is successfully inserted or replaced in the hash table
 *
 * @retval false Failed to set the DDT entry. This can happen when:
 *         - ctx is NULL or ctx->imageStream is NULL (invalid context)
 *         - ctx->is_tape is false (wrong function called for non-tape media)
 *         - The DDT entry exceeds 0xFFFFFFF (media too large for big DDT)
 *         - Memory allocation fails for the new hash table entry (out of memory)
 *
 * @note This function is only for tape images. For disk images, use set_ddt_single_level_v2()
 *       or set_ddt_multi_level_v2() instead, which use array-based DDT structures.
 *
 * @note Memory Management:
 *       - Allocates a new TapeDdtHashEntry for each sector
 *       - HASH_REPLACE automatically frees replaced entries
 *       - All hash entries remain in context until cleanup
 *       - The tapeDdt hash table must be freed during context destruction
 *
 * @note Tape Media Characteristics:
 *       - Tape sectors are typically accessed sequentially during streaming
 *       - File marks and partition boundaries create sparse address spaces
 *       - Hash table provides efficient storage for sparse sector maps
 *       - Supports variable block sizes common in tape formats
 *
 * @note Error Handling:
 *       - All errors are logged with FATAL level messages
 *       - Function returns false immediately on any error condition
 *       - TRACE logging marks entry/exit points for debugging
 *       - No partial state changes occur on failure
 *
 * @warning The DDT entry overflow check at 0xFFFFFFF (28 bits) is critical. Exceeding
 *          this limit indicates the media is too large to fit in the current DDT format,
 *          and continuing would cause data corruption.
 *
 * @warning This function modifies the shared tapeDdt hash table. In multi-threaded
 *          environments, external synchronization is required to prevent race conditions.
 *
 * @see TapeDdtHashEntry for the hash table entry structure
 * @see set_ddt_entry_v2() for the main DDT entry point that dispatches to this function
 * @see get_ddt_tape() for retrieving tape DDT entries from the hash table
 */
bool set_ddt_tape(aaruformat_context *ctx, uint64_t sector_address, const uint64_t offset, const uint64_t block_offset,
                  const uint8_t sector_status, uint64_t *ddt_entry)
{
    TRACE("Entering set_ddt_tape(%p, %" PRIu64 ", %llu, %llu, %d)", ctx, sector_address, offset, block_offset,
          sector_status);

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");
        TRACE("Exiting set_ddt_tape() = false");
        return false;
    }

    // Should not really be here
    if(!ctx->is_tape)
    {
        FATAL("Image is not tape, wrong function called.");
        TRACE("Exiting set_ddt_tape() = false");
        return false;
    }

    if(*ddt_entry == 0)
    {
        const uint64_t block_index = block_offset >> ctx->user_data_ddt_header.blockAlignmentShift;
        *ddt_entry                 = offset & (1ULL << ctx->user_data_ddt_header.dataShift) - 1 |
                     block_index << ctx->user_data_ddt_header.dataShift;
        // Overflow detection for DDT entry
        if(*ddt_entry > 0xFFFFFFFFFFFFFFF)
        {
            FATAL("DDT overflow: media does not fit in big DDT");
            TRACE("Exiting set_ddt_tape() = false");
            return false;
        }

        *ddt_entry |= (uint64_t)sector_status << 60;
    }

    // Create DDT hash entry
    TapeDdtHashEntry *new_entry = calloc(1, sizeof(TapeDdtHashEntry));
    TapeDdtHashEntry *old_entry = NULL;
    if(new_entry == NULL)
    {
        FATAL("Cannot allocate memory for new tape DDT hash entry.");
        TRACE("Exiting set_ddt_tape() = false");
        return false;
    }

    TRACE("Setting tape DDT entry %d to %u", sector_address, (uint32_t)*ddt_entry);

    new_entry->key   = sector_address;
    new_entry->value = *ddt_entry;

    // Insert entry into tape DDT
    HASH_REPLACE(hh, ctx->tape_ddt, key, sizeof(uint64_t), new_entry, old_entry);
    ctx->dirty_tape_ddt = true;  // Mark tape DDT as dirty
    if(old_entry) free(old_entry);

    TRACE("Exiting set_ddt_tape() = true");
    return true;
}
