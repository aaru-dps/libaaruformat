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

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "errors.h"

#ifdef __linux__
#include <sys/mman.h>
#endif

#include "aaruformat.h"
#include "log.h"

/**
 * @brief Processes a DDT v1 block from the image stream.
 *
 * Reads and decompresses (if needed) a DDT v1 block, verifies its integrity, and loads it into memory or maps it.
 * This function handles both user data DDT blocks and CD sector prefix/suffix corrected DDT blocks, supporting
 * both LZMA compression and uncompressed formats. On Linux, uncompressed blocks can be memory-mapped for
 * improved performance.
 *
 * @param ctx Pointer to the aaruformat context.
 * @param entry Pointer to the index entry describing the DDT block.
 * @param found_user_data_ddt Pointer to a boolean that will be set to true if a user data DDT was found and loaded.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully processed the DDT block. This is returned when:
 *         - The DDT block is successfully read, decompressed (if needed), and loaded into memory
 *         - Memory mapping of uncompressed DDT succeeds on Linux systems
 *         - CD sector prefix/suffix corrected DDT blocks are processed successfully
 *         - Memory allocation failures occur for non-critical operations (processing continues)
 *         - File reading errors occur for compressed data or LZMA properties (processing continues)
 *         - Unknown compression types are encountered (block is skipped)
 *         - Memory mapping fails on Linux (sets found_user_data_ddt to false but continues)
 *         - Uncompressed DDT is encountered on non-Linux systems (not yet implemented, continues)
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context or image stream is invalid (NULL pointers).
 *
 * @retval AARUF_ERROR_CANNOT_READ_BLOCK (-7) Failed to access the DDT block in the image stream. This occurs when:
 *         - fseek() fails to position at the DDT block offset
 *         - The file position doesn't match the expected offset after seeking
 *         - Failed to read the DDT header from the image stream
 *         - The number of bytes read for the DDT header is insufficient
 *
 * @retval AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK (-17) LZMA decompression failed. This can happen when:
 *         - The LZMA decoder returns a non-zero error code during decompression
 *         - The decompressed data size doesn't match the expected DDT block length
 *         - This error causes immediate function termination and memory cleanup
 *
 * @note The function exhibits different error handling strategies depending on the operation:
 *       - Critical errors (seek failures, header read failures, decompression failures) cause immediate return
 *       - Non-critical errors (memory allocation failures, unknown compression types) allow processing to continue
 *       - The found_user_data_ddt flag is updated to reflect the success of user data DDT loading
 *
 * @note Memory Management:
 *       - Allocated DDT data is stored in the context (ctx->userDataDdt, ctx->sectorPrefixDdt, ctx->sectorSuffixDdt)
 *       - On Linux, uncompressed DDTs may be memory-mapped instead of allocated
 *       - Memory is automatically cleaned up on decompression errors
 *
 * @note Platform-specific behavior:
 *       - Linux: Supports memory mapping of uncompressed DDT blocks for better performance
 *       - Non-Linux: Uncompressed DDT processing is not yet implemented
 *
 * @warning The function modifies context state including sector count, shift value, and DDT version.
 *          Ensure proper context cleanup when the function completes.
 */
int32_t process_ddt_v1(aaruformatContext *ctx, IndexEntry *entry, bool *found_user_data_ddt)
{
    TRACE("Entering process_ddt_v1(%p, %p, %d)", ctx, entry, *found_user_data_ddt);

    int       pos        = 0;
    size_t    read_bytes = 0;
    DdtHeader ddt_header;
    uint8_t  *cmp_data = NULL;
    uint32_t *cd_ddt   = NULL;
    uint8_t   lzma_properties[LZMA_PROPERTIES_LENGTH];
    size_t    lzma_size = 0;
    int       error_no  = 0;

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");

        TRACE("Exiting process_ddt_v1() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Seek to block
    TRACE("Seeking to DDT block at position %" PRIu64, entry->offset);
    pos = fseek(ctx->imageStream, entry->offset, SEEK_SET);
    if(pos < 0 || ftell(ctx->imageStream) != entry->offset)
    {
        FATAL("Could not seek to %" PRIu64 " as indicated by index entry...", entry->offset);

        TRACE("Exiting process_ddt_v1() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    // Even if those two checks shall have been done before
    TRACE("Reading DDT block header at position %" PRIu64, entry->offset);
    read_bytes = fread(&ddt_header, 1, sizeof(DdtHeader), ctx->imageStream);

    if(read_bytes != sizeof(DdtHeader))
    {
        FATAL("Could not read block header at %" PRIu64 "", entry->offset);

        TRACE("Exiting process_ddt_v1() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    *found_user_data_ddt = true;

    ctx->imageInfo.ImageSize += ddt_header.cmpLength;

    if(entry->dataType == UserData)
    {
        ctx->imageInfo.Sectors = ddt_header.entries;
        ctx->shift             = ddt_header.shift;
        ctx->ddtVersion        = 1;

        // Check for DDT compression
        switch(ddt_header.compression)
        {
            // TODO: Check CRC
            case Lzma:
                if(ddt_header.cmpLength <= LZMA_PROPERTIES_LENGTH)
                {
                    FATAL("Compressed DDT payload too small (%" PRIu64 ") for LZMA properties.", ddt_header.cmpLength);
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                lzma_size = ddt_header.cmpLength - LZMA_PROPERTIES_LENGTH;

                cmp_data = (uint8_t *)malloc(lzma_size);
                if(cmp_data == NULL)
                {
                    TRACE("Cannot allocate memory for DDT, continuing...");
                    break;
                }

                ctx->userDataDdt = (uint64_t *)malloc(ddt_header.length);
                if(ctx->userDataDdt == NULL)
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
                    free(ctx->userDataDdt);
                    ctx->userDataDdt = NULL;
                    break;
                }

                read_bytes = fread(cmp_data, 1, lzma_size, ctx->imageStream);
                if(read_bytes != lzma_size)
                {
                    TRACE("Could not read compressed block, continuing...");
                    free(cmp_data);
                    free(ctx->userDataDdt);
                    ctx->userDataDdt = NULL;
                    break;
                }

                read_bytes = ddt_header.length;
                TRACE("Decompressing block of size %zu bytes", ddt_header.length);
                error_no = aaruf_lzma_decode_buffer((uint8_t *)ctx->userDataDdt, &read_bytes, cmp_data, &lzma_size,
                                                    lzma_properties, LZMA_PROPERTIES_LENGTH);

                if(error_no != 0)
                {
                    FATAL("Got error %d from LZMA, stopping...", error_no);
                    free(cmp_data);
                    free(ctx->userDataDdt);
                    ctx->userDataDdt = NULL;
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                if(read_bytes != ddt_header.length)
                {
                    FATAL("Error decompressing block, should be {0} bytes but got {1} bytes., stopping...");
                    free(cmp_data);
                    free(ctx->userDataDdt);
                    ctx->userDataDdt = NULL;
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                free(cmp_data);
                cmp_data = NULL;

                ctx->inMemoryDdt     = true;
                *found_user_data_ddt = true;

                break;
            // TODO: Check CRC
            case None:
#ifdef __linux__
                TRACE("Memory mapping deduplication table at position %" PRIu64, entry->offset + sizeof(ddt_header));
                ctx->mappedMemoryDdtSize = sizeof(uint64_t) * ddt_header.entries;
                ctx->userDataDdt = mmap(NULL, ctx->mappedMemoryDdtSize, PROT_READ, MAP_SHARED, fileno(ctx->imageStream),
                                        entry->offset + sizeof(ddt_header));

                if(ctx->userDataDdt == MAP_FAILED)
                {
                    *found_user_data_ddt = false;
                    FATAL("Could not read map deduplication table.");
                    break;
                }

                ctx->inMemoryDdt = false;
                break;
#else  // TODO: Implement
                TRACE("Uncompressed DDT not yet implemented...");
                *found_user_data_ddt = false;
                break;
#endif
            default:
                TRACE("Found unknown compression type %d, continuing...", ddt_header.compression);
                *found_user_data_ddt = false;
                break;
        }
    }
    else if(entry->dataType == CdSectorPrefixCorrected || entry->dataType == CdSectorSuffixCorrected)
    {
        switch(ddt_header.compression)
        {
            // TODO: Check CRC
            case Lzma:
                if(ddt_header.cmpLength <= LZMA_PROPERTIES_LENGTH)
                {
                    FATAL("Compressed DDT payload too small (%" PRIu64 ") for LZMA properties.", ddt_header.cmpLength);
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                lzma_size = ddt_header.cmpLength - LZMA_PROPERTIES_LENGTH;

                cmp_data = (uint8_t *)malloc(lzma_size);
                if(cmp_data == NULL)
                {
                    TRACE("Cannot allocate memory for DDT, continuing...");
                    break;
                }

                cd_ddt = (uint32_t *)malloc(ddt_header.length);
                if(cd_ddt == NULL)
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
                    free(cd_ddt);
                    break;
                }

                read_bytes = fread(cmp_data, 1, lzma_size, ctx->imageStream);
                if(read_bytes != lzma_size)
                {
                    TRACE("Could not read compressed block, continuing...");
                    free(cmp_data);
                    free(cd_ddt);
                    break;
                }

                read_bytes = ddt_header.length;
                TRACE("Decompressing block of size %zu bytes", ddt_header.length);
                error_no = aaruf_lzma_decode_buffer((uint8_t *)cd_ddt, &read_bytes, cmp_data, &lzma_size,
                                                    lzma_properties, LZMA_PROPERTIES_LENGTH);

                if(error_no != 0)
                {
                    FATAL("Got error %d from LZMA, stopping...", error_no);
                    free(cmp_data);
                    free(cd_ddt);
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                if(read_bytes != ddt_header.length)
                {
                    FATAL("Error decompressing block, should be {0} bytes but got {1} bytes., stopping...");
                    free(cmp_data);
                    free(cd_ddt);
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                free(cmp_data);
                cmp_data = NULL;

                if(entry->dataType == CdSectorPrefixCorrected)
                    ctx->sectorPrefixDdt = cd_ddt;
                else if(entry->dataType == CdSectorSuffixCorrected)
                    ctx->sectorSuffixDdt = cd_ddt;
                else
                    free(cd_ddt);

                break;

            // TODO: Check CRC
            case None:
                cd_ddt = (uint32_t *)malloc(ddt_header.entries * sizeof(uint32_t));

                if(cd_ddt == NULL)
                {
                    TRACE("Cannot allocate memory for deduplication table.");
                    break;
                }

                read_bytes = fread(cd_ddt, 1, ddt_header.entries * sizeof(uint32_t), ctx->imageStream);

                if(read_bytes != ddt_header.entries * sizeof(uint32_t))
                {
                    free(cd_ddt);
                    TRACE("Could not read deduplication table, continuing...");
                    break;
                }

                if(entry->dataType == CdSectorPrefixCorrected)
                    ctx->sectorPrefixDdt = cd_ddt;
                else if(entry->dataType == CdSectorSuffixCorrected)
                    ctx->sectorSuffixDdt = cd_ddt;
                else
                    free(cd_ddt);

                break;
            default:
                TRACE("Found unknown compression type %d, continuing...", ddt_header.compression);
                break;
        }
    }

    TRACE("Exiting process_ddt_v1() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Decodes a DDT v1 entry for a given sector address.
 *
 * Determines the offset and block offset for a sector using the DDT v1 table. This function performs
 * bit manipulation on the DDT entry to extract the sector offset within a block and the block offset,
 * and determines whether the sector was dumped or not based on the DDT entry value.
 *
 * @param ctx Pointer to the aaruformat context containing the loaded DDT table.
 * @param sector_address Logical sector address to decode (must be within valid range).
 * @param offset Pointer to store the resulting sector offset within the block.
 * @param block_offset Pointer to store the resulting block offset in the image.
 * @param sector_status Pointer to store the sector status (dumped or not dumped).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully decoded the DDT entry. This is always returned when:
 *         - The context and image stream are valid
 *         - The DDT entry is successfully extracted and decoded
 *         - The offset, block_offset, and sector_status are successfully populated
 *         - A zero DDT entry is encountered (indicates sector not dumped)
 *         - A non-zero DDT entry is encountered (indicates sector was dumped)
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context or image stream is invalid (NULL pointers).
 *         This is the only error condition that can occur in this function.
 *
 * @note This is a lightweight function that performs only basic validation and bit manipulation.
 *       It does not perform bounds checking on the sector_address parameter.
 *
 * @note DDT Entry Decoding:
 *       - Uses a bit mask derived from ctx->shift to extract the offset within block
 *       - Right-shifts the DDT entry by ctx->shift bits to get the block offset
 *       - A zero DDT entry indicates the sector was not dumped (SectorStatusNotDumped)
 *       - A non-zero DDT entry indicates the sector was dumped (SectorStatusDumped)
 *
 * @warning The function assumes:
 *         - The DDT table (ctx->userDataDdt) has been properly loaded by process_ddt_v1()
 *         - The sector_address is within the valid range of the DDT table
 *         - The shift value (ctx->shift) has been properly initialized
 *         - All output parameters (offset, block_offset, sector_status) are valid pointers
 *
 * @warning No bounds checking is performed on sector_address. Accessing beyond the DDT table
 *          boundaries will result in undefined behavior.
 */
int32_t decode_ddt_entry_v1(aaruformatContext *ctx, const uint64_t sector_address, uint64_t *offset,
                            uint64_t *block_offset, uint8_t *sector_status)
{
    TRACE("Entering decode_ddt_entry_v1(%p, %" PRIu64 ", %p, %p, %p)", ctx, sector_address, offset, block_offset,
          sector_status);

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");
        TRACE("Exiting decode_ddt_entry_v1() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->userDataDdt == NULL)
    {
        FATAL("User data DDT not loaded.");
        TRACE("Exiting decode_ddt_entry_v1() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->shift >= 64)
    {
        FATAL("Invalid DDT shift value %u", ctx->shift);
        TRACE("Exiting decode_ddt_entry_v1() = AARUF_ERROR_INCORRECT_DATA_SIZE");
        return AARUF_ERROR_INCORRECT_DATA_SIZE;
    }

    const uint64_t ddt_entry     = ctx->userDataDdt[sector_address];
    const uint64_t offset_mask64 = (UINT64_C(1) << ctx->shift) - UINT64_C(1);
    *offset                      = ddt_entry & offset_mask64;
    *block_offset                = ddt_entry >> ctx->shift;

    // Partially written image... as we can't know the real sector size just assume it's common :/
    if(ddt_entry == 0)
        *sector_status = SectorStatusNotDumped;
    else
        *sector_status = SectorStatusDumped;

    TRACE("Exiting decode_ddt_entry_v1(%p, %" PRIu64 ", %llu, %llu, %d) = AARUF_STATUS_OK", ctx, sector_address,
          *offset, *block_offset, *sector_status);
    return AARUF_STATUS_OK;
}