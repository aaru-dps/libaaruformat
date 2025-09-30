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

#include "aaruformat.h"
#include "internal.h"
#include "log.h"

/**
 * @brief Processes a DDT v2 block from the image stream.
 *
 * Reads and decompresses (if needed) a DDT v2 block, verifies its CRC, and loads it into memory.
 *
 * @param ctx Pointer to the aaruformat context.
 * @param entry Pointer to the index entry describing the DDT block.
 * @param foundUserDataDdt Pointer to a boolean that will be set to true if a user data DDT was found and loaded.
 * @return AARUF_STATUS_OK on success, or an error code on failure.
 */
int32_t process_ddt_v2(aaruformatContext *ctx, IndexEntry *entry, bool *found_user_data_ddt)
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

    *found_user_data_ddt = false;

    ctx->imageInfo.ImageSize += ddt_header.cmpLength;

    if(entry->dataType == UserData)
    {
        // User area sectors is blocks stored in DDT minus the negative and overflow displacement blocks
        ctx->imageInfo.Sectors = ddt_header.blocks - ddt_header.negative - ddt_header.overflow;
        // We need the header later for the shift calculations
        ctx->userDataDdtHeader = ddt_header;
        ctx->ddtVersion        = 2;
        // Store the primary DDT table's file offset for secondary table references
        ctx->primaryDdtOffset  = entry->offset;

        // Check for DDT compression
        switch(ddt_header.compression)
        {
            case Lzma:
                lzma_size = ddt_header.cmpLength - LZMA_PROPERTIES_LENGTH;

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

                if(crc64 != ddt_header.crc64)
                {
                    FATAL("Expected DDT CRC 0x%16lX but got 0x%16lX.", ddt_header.crc64, crc64);
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_INVALID_BLOCK_CRC");
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                if(ddt_header.sizeType == SmallDdtSizeType)
                    ctx->userDataDdtMini = (uint16_t *)buffer;
                else if(ddt_header.sizeType == BigDdtSizeType)
                    ctx->userDataDdtBig = (uint32_t *)buffer;

                ctx->inMemoryDdt     = true;
                *found_user_data_ddt = true;

                break;
            case None:
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

                if(crc64 != ddt_header.crc64)
                {
                    FATAL("Expected DDT CRC 0x%16lX but got 0x%16lX.", ddt_header.crc64, crc64);
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_INVALID_BLOCK_CRC");
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                if(ddt_header.sizeType == SmallDdtSizeType)
                    ctx->userDataDdtMini = (uint16_t *)buffer;
                else if(ddt_header.sizeType == BigDdtSizeType)
                    ctx->userDataDdtBig = (uint32_t *)buffer;

                ctx->inMemoryDdt     = true;
                *found_user_data_ddt = true;

                break;
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
            case Lzma:
                lzma_size = ddt_header.cmpLength - LZMA_PROPERTIES_LENGTH;

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

                if(crc64 != ddt_header.crc64)
                {
                    FATAL("Expected DDT CRC 0x%16lX but got 0x%16lX.", ddt_header.crc64, crc64);
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_INVALID_BLOCK_CRC");
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                if(entry->dataType == CdSectorPrefixCorrected)
                {
                    if(ddt_header.sizeType == SmallDdtSizeType)
                        ctx->sectorPrefixDdtMini = (uint16_t *)buffer;
                    else if(ddt_header.sizeType == BigDdtSizeType)
                        ctx->sectorPrefixDdt = (uint32_t *)buffer;
                }
                else if(entry->dataType == CdSectorSuffixCorrected)
                {
                    if(ddt_header.sizeType == SmallDdtSizeType)
                        ctx->sectorSuffixDdtMini = (uint16_t *)buffer;
                    else if(ddt_header.sizeType == BigDdtSizeType)
                        ctx->sectorSuffixDdt = (uint32_t *)buffer;
                }
                else
                    free(buffer);

                break;

            case None:
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

                if(crc64 != ddt_header.crc64)
                {
                    FATAL("Expected DDT CRC 0x%16lX but got 0x%16lX.", ddt_header.crc64, crc64);
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_INVALID_BLOCK_CRC");
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                if(entry->dataType == CdSectorPrefixCorrected)
                {
                    if(ddt_header.sizeType == SmallDdtSizeType)
                        ctx->sectorPrefixDdtMini = (uint16_t *)buffer;
                    else if(ddt_header.sizeType == BigDdtSizeType)
                        ctx->sectorPrefixDdt = (uint32_t *)buffer;
                }
                else if(entry->dataType == CdSectorSuffixCorrected)
                {
                    if(ddt_header.sizeType == SmallDdtSizeType)
                        ctx->sectorSuffixDdtMini = (uint16_t *)buffer;
                    else if(ddt_header.sizeType == BigDdtSizeType)
                        ctx->sectorSuffixDdt = (uint32_t *)buffer;
                }
                else
                    free(buffer);

                break;
            default:
                TRACE("Found unknown compression type %d, continuing...", ddt_header.compression);
                break;
        }
    }

    TRACE("Exiting process_ddt_v2() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Decodes a DDT v2 entry for a given sector address.
 *
 * Determines the offset and block offset for a sector using the DDT v2 table(s).
 *
 * @param ctx Pointer to the aaruformat context.
 * @param sector_address Logical sector address to decode.
 * @param offset Pointer to store the resulting offset.
 * @param block_offset Pointer to store the resulting block offset.
 * @param sector_status Pointer to store the sector status.
 * @return AARUF_STATUS_OK on success, or an error code on failure.
 */
int32_t decode_ddt_entry_v2(aaruformatContext *ctx, uint64_t sector_address, uint64_t *offset, uint64_t *block_offset,
                            uint8_t *sector_status)
{
    TRACE("Entering decode_ddt_entry_v2(%p, %" PRIu64 ", %llu, %llu, %d)", ctx, sector_address, *offset, *block_offset,
          *sector_status);
    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");

        TRACE("Exiting decode_ddt_entry_v2() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->userDataDdtHeader.tableShift > 0)
        return decode_ddt_multi_level_v2(ctx, sector_address, offset, block_offset, sector_status);

    return decode_ddt_single_level_v2(ctx, sector_address, offset, block_offset, sector_status);
}

/**
 * @brief Decodes a single-level DDT v2 entry for a given sector address.
 *
 * Used when the DDT table does not use multi-level indirection.
 *
 * @param ctx Pointer to the aaruformat context.
 * @param sector_address Logical sector address to decode.
 * @param offset Pointer to store the resulting offset.
 * @param block_offset Pointer to store the resulting block offset.
 * @param sector_status Pointer to store the sector status.
 * @return AARUF_STATUS_OK on success, or an error code on failure.
 */
int32_t decode_ddt_single_level_v2(aaruformatContext *ctx, uint64_t sector_address, uint64_t *offset,
                                   uint64_t *block_offset, uint8_t *sector_status)
{
    TRACE("Entering decode_ddt_single_level_v2(%p, %" PRIu64 ", %llu, %llu, %d)", ctx, sector_address, *offset,
          *block_offset, *sector_status);

    uint64_t ddt_entry = 0;

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");

        TRACE("Exiting decode_ddt_single_level_v2() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Should not really be here
    if(ctx->userDataDdtHeader.tableShift != 0)
    {
        FATAL("DDT table shift is not zero, but we are in single-level DDT decoding.");
        TRACE("Exiting decode_ddt_single_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    // TODO: Take into account the negative and overflow blocks, library-wide
    sector_address += ctx->userDataDdtHeader.negative;

    if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
        ddt_entry = ctx->userDataDdtMini[sector_address];
    else if(ctx->userDataDdtHeader.sizeType == BigDdtSizeType)
        ddt_entry = ctx->userDataDdtBig[sector_address];
    else
    {
        FATAL("Unknown DDT size type %d.", ctx->userDataDdtHeader.sizeType);
        TRACE("Exiting decode_ddt_single_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    if(ddt_entry == 0)
    {
        *sector_status = SectorStatusNotDumped;
        *offset        = 0;
        *block_offset  = 0;
        TRACE("Exiting decode_ddt_single_level_v2(%p, %" PRIu64 ", %llu, %llu, %d) = AARUF_STATUS_OK", ctx,
              sector_address, *offset, *block_offset, *sector_status);
        return AARUF_STATUS_OK;
    }

    if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
    {
        *sector_status = ddt_entry >> 12;
        ddt_entry &= 0xfff;
    }
    else if(ctx->userDataDdtHeader.sizeType == BigDdtSizeType)
    {
        *sector_status = ddt_entry >> 28;
        ddt_entry &= 0x0fffffff;
    }

    const uint64_t offset_mask = (uint64_t)((1 << ctx->userDataDdtHeader.dataShift) - 1);
    *offset                    = ddt_entry & offset_mask;
    *block_offset = (ddt_entry >> ctx->userDataDdtHeader.dataShift) * (1 << ctx->userDataDdtHeader.blockAlignmentShift);

    TRACE("Exiting decode_ddt_single_level_v2(%p, %" PRIu64 ", %llu, %llu, %d) = AARUF_STATUS_OK", ctx, sector_address,
          *offset, *block_offset, *sector_status);
    return AARUF_STATUS_OK;
}

/**
 * @brief Decodes a multi-level DDT v2 entry for a given sector address.
 *
 * Used when the DDT table uses multi-level indirection (tableShift > 0).
 *
 * @param ctx Pointer to the aaruformat context.
 * @param sector_address Logical sector address to decode.
 * @param offset Pointer to store the resulting offset.
 * @param block_offset Pointer to store the resulting block offset.
 * @param sector_status Pointer to store the sector status.
 * @return AARUF_STATUS_OK on success, or an error code on failure.
 */
int32_t decode_ddt_multi_level_v2(aaruformatContext *ctx, uint64_t sector_address, uint64_t *offset,
                                  uint64_t *block_offset, uint8_t *sector_status)
{
    TRACE("Entering decode_ddt_multi_level_v2(%p, %" PRIu64 ", %llu, %llu, %d)", ctx, sector_address, *offset,
          *block_offset, *sector_status);

    uint64_t   ddt_entry = 0;
    uint8_t    lzma_properties[LZMA_PROPERTIES_LENGTH];
    size_t     lzma_size            = 0;
    uint8_t   *cmp_data             = NULL;
    uint8_t   *buffer               = NULL;
    int32_t    error_no             = 0;
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
    if(ctx->userDataDdtHeader.tableShift == 0)
    {
        FATAL("DDT table shift is zero, but we are in multi-level DDT decoding.");
        TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    // TODO: Take into account the negative and overflow blocks, library-wide
    sector_address += ctx->userDataDdtHeader.negative;

    items_per_ddt_entry = 1 << ctx->userDataDdtHeader.tableShift;
    ddt_position        = sector_address / items_per_ddt_entry;

    if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
        secondary_ddt_offset = ctx->userDataDdtMini[ddt_position];
    else if(ctx->userDataDdtHeader.sizeType == BigDdtSizeType)
        secondary_ddt_offset = ctx->userDataDdtBig[ddt_position];
    else
    {
        FATAL("Unknown DDT size type %d.", ctx->userDataDdtHeader.sizeType);
        TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    // Position in file of the child DDT table
    secondary_ddt_offset *= 1 << ctx->userDataDdtHeader.blockAlignmentShift;

    // Is the one we have cached the same as the one we need to read?
    if(ctx->cachedDdtOffset != secondary_ddt_offset)
    {
        fseek(ctx->imageStream, secondary_ddt_offset, SEEK_SET);
        DdtHeader2 ddt_header;
        size_t     read_bytes = fread(&ddt_header, 1, sizeof(DdtHeader2), ctx->imageStream);

        if(read_bytes != sizeof(DdtHeader2))
        {
            FATAL("Could not read block header at %" PRIu64 "", secondaryDdtOffset);
            TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
            return AARUF_ERROR_CANNOT_READ_BLOCK;
        }

        if(ddt_header.identifier != DeDuplicationTable2 || ddt_header.type != UserData)
        {
            FATAL("Invalid block header at %" PRIu64 "", secondaryDdtOffset);
            TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
            return AARUF_ERROR_CANNOT_READ_BLOCK;
        }

        // Check for DDT compression
        switch(ddt_header.compression)
        {
            case Lzma:
                lzma_size = ddt_header.cmpLength - LZMA_PROPERTIES_LENGTH;

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

                if(crc64 != ddt_header.crc64)
                {
                    FATAL("Expected DDT CRC 0x%16lX but got 0x%16lX.", ddtHeader.crc64, crc64);
                    free(buffer);
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_INVALID_BLOCK_CRC");
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                if(ddt_header.sizeType == SmallDdtSizeType)
                    ctx->cachedSecondaryDdtSmall = (uint16_t *)buffer;
                else if(ddt_header.sizeType == BigDdtSizeType)
                    ctx->cachedSecondaryDdtBig = (uint32_t *)buffer;

                ctx->cachedDdtOffset = secondary_ddt_offset;

                break;
            case None:
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

                if(crc64 != ddt_header.crc64)
                {
                    FATAL("Expected DDT CRC 0x%16lX but got 0x%16lX.", ddtHeader.crc64, crc64);
                    free(buffer);
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_INVALID_BLOCK_CRC");
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                if(ddt_header.sizeType == SmallDdtSizeType)
                    ctx->cachedSecondaryDdtSmall = (uint16_t *)buffer;
                else if(ddt_header.sizeType == BigDdtSizeType)
                    ctx->cachedSecondaryDdtBig = (uint32_t *)buffer;

                ctx->cachedDdtOffset = secondary_ddt_offset;

                break;
            default:
                FATAL("Found unknown compression type %d, stopping...", ddtHeader.compression);
                TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
                return AARUF_ERROR_CANNOT_READ_BLOCK;
        }
    }

    if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
        ddt_entry = ctx->cachedSecondaryDdtSmall[sector_address % items_per_ddt_entry];
    else if(ctx->userDataDdtHeader.sizeType == BigDdtSizeType)
        ddt_entry = ctx->cachedSecondaryDdtBig[sector_address % items_per_ddt_entry];

    if(ddt_entry == 0)
    {
        *sector_status = SectorStatusNotDumped;
        *offset        = 0;
        *block_offset  = 0;

        TRACE("Exiting decode_ddt_multi_level_v2(%p, %" PRIu64 ", %llu, %llu, %d) = AARUF_STATUS_OK", ctx,
              sector_address, *offset, *block_offset, *sector_status);
        return AARUF_STATUS_OK;
    }

    if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
    {
        *sector_status = ddt_entry >> 12;
        ddt_entry &= 0xfff;
    }
    else if(ctx->userDataDdtHeader.sizeType == BigDdtSizeType)
    {
        *sector_status = ddt_entry >> 28;
        ddt_entry &= 0x0fffffff;
    }

    const uint64_t offset_mask = (uint64_t)((1 << ctx->userDataDdtHeader.dataShift) - 1);
    *offset                    = ddt_entry & offset_mask;
    *block_offset = (ddt_entry >> ctx->userDataDdtHeader.dataShift) * (1 << ctx->userDataDdtHeader.blockAlignmentShift);

    TRACE("Exiting decode_ddt_multi_level_v2(%p, %" PRIu64 ", %llu, %llu, %d) = AARUF_STATUS_OK", ctx, sector_address,
          *offset, *block_offset, *sector_status);
    return AARUF_STATUS_OK;
}

/**
 * @brief Sets a DDT v2 entry for a given sector address.
 *
 * Updates the DDT v2 table(s) with the specified offset, block offset, and sector status for a sector.
 *
 * @param ctx Pointer to the aaruformat context.
 * @param sector_address Logical sector address to set.
 * @param offset Offset to set for the sector.
 * @param block_offset Block offset to set for the sector.
 * @param sector_status Status to set for the sector.
 */
void set_ddt_entry_v2(aaruformatContext *ctx, uint64_t sector_address, uint64_t offset, uint64_t block_offset,
                      uint8_t sector_status)
{
    TRACE("Entering set_ddt_entry_v2(%p, %" PRIu64 ", %llu, %llu, %d)", ctx, sector_address, offset, block_offset,
          sector_status);

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");
        return;
    }

    if(ctx->userDataDdtHeader.tableShift > 0)
        set_ddt_multi_level_v2(ctx, sector_address, false, offset, block_offset, sector_status);
    else
        set_ddt_single_level_v2(ctx, sector_address, false, offset, block_offset, sector_status);
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
 */
void set_ddt_single_level_v2(aaruformatContext *ctx, uint64_t sector_address, bool negative, uint64_t offset,
                             uint64_t block_offset, uint8_t sector_status)
{
    TRACE("Entering set_ddt_single_level_v2(%p, %" PRIu64 ", %llu, %llu, %d)", ctx, sector_address, offset,
          block_offset, sector_status);

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");
        return;
    }

    // Should not really be here
    if(ctx->userDataDdtHeader.tableShift != 0)
    {
        FATAL("DDT table shift is not zero, but we are in single-level DDT setting.");
        return;
    }

    // Calculate positive or negative sector
    if(negative)
        sector_address -= ctx->userDataDdtHeader.negative;
    else
        sector_address += ctx->userDataDdtHeader.negative;

    uint64_t ddt_entry = 0;

    uint64_t block_index = block_offset >> ctx->userDataDdtHeader.blockAlignmentShift;
    ddt_entry            = offset & ((1ULL << ctx->userDataDdtHeader.dataShift) - 1) | block_index
                                                                                << ctx->userDataDdtHeader.dataShift;

    if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
    {
        // Overflow detection for DDT entry
        if(ddt_entry > 0xFFF)
        {
            FATAL("DDT overflow: media does not fit in small DDT");
            return;
        }

        ddt_entry |= (uint64_t)sector_status << 12;
        ctx->cachedSecondaryDdtSmall[sector_address] = (uint16_t)ddt_entry;
    }
    else if(ctx->userDataDdtHeader.sizeType == BigDdtSizeType)
    {
        // Overflow detection for DDT entry
        if(ddt_entry > 0xFFFFFFF)
        {
            FATAL("DDT overflow: media does not fit in big DDT");
            return;
        }

        ddt_entry |= (uint64_t)sector_status << 28;
        ctx->cachedSecondaryDdtBig[sector_address] = (uint32_t)ddt_entry;
    }
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
 */
void set_ddt_multi_level_v2(aaruformatContext *ctx, uint64_t sector_address, bool negative, uint64_t offset,
                            uint64_t block_offset, uint8_t sector_status)
{
    TRACE("Entering set_ddt_multi_level_v2(%p, %" PRIu64 ", %d, %" PRIu64 ", %" PRIu64 ", %d)", ctx, sector_address,
          negative, offset, block_offset, sector_status);

    uint64_t   items_per_ddt_entry  = 0;
    uint64_t   ddt_position         = 0;
    uint64_t   secondary_ddt_offset = 0;
    uint64_t   ddt_entry            = 0;
    uint64_t   block_index          = 0;
    uint8_t   *buffer               = NULL;
    crc64_ctx *crc64_context        = NULL;
    uint64_t   crc64                = 0;
    DdtHeader2 ddt_header;
    size_t     written_bytes    = 0;
    long       current_pos      = 0;
    long       end_of_file      = 0;
    bool       create_new_table = false;

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");
        return;
    }

    // Should not really be here
    if(ctx->userDataDdtHeader.tableShift == 0)
    {
        FATAL("DDT table shift is zero, but we are in multi-level DDT setting.");
        return;
    }

    // Calculate positive or negative sector
    if(negative)
        sector_address -= ctx->userDataDdtHeader.negative;
    else
        sector_address += ctx->userDataDdtHeader.negative;

    // Step 1: Calculate the corresponding secondary level table
    items_per_ddt_entry = 1 << ctx->userDataDdtHeader.tableShift;
    ddt_position        = sector_address / items_per_ddt_entry;

    if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
        secondary_ddt_offset = ctx->userDataDdtMini[ddt_position];
    else if(ctx->userDataDdtHeader.sizeType == BigDdtSizeType)
        secondary_ddt_offset = ctx->userDataDdtBig[ddt_position];
    else
    {
        FATAL("Unknown DDT size type %d.", ctx->userDataDdtHeader.sizeType);
        return;
    }

    // Position in file of the child DDT table
    secondary_ddt_offset *= 1 << ctx->userDataDdtHeader.blockAlignmentShift;

    // Step 2: Check if it corresponds to the currently in-memory cached secondary level table
    if(ctx->cachedDdtOffset == secondary_ddt_offset && secondary_ddt_offset != 0)
    {
        // Update the corresponding DDT entry directly in the cached table
        block_index = block_offset >> ctx->userDataDdtHeader.blockAlignmentShift;
        ddt_entry   = offset & (1ULL << ctx->userDataDdtHeader.dataShift) - 1 | block_index
                                                                                  << ctx->userDataDdtHeader.dataShift;

        if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
        {
            // Overflow detection for DDT entry
            if(ddt_entry > 0xFFF)
            {
                FATAL("DDT overflow: media does not fit in small DDT");
                return;
            }

            ddt_entry |= (uint64_t)sector_status << 12;
            TRACE("Setting small secondary DDT entry %d to %u", sector_address % items_per_ddt_entry,
                  (uint16_t)ddt_entry);
            ctx->cachedSecondaryDdtSmall[sector_address % items_per_ddt_entry] = (uint16_t)ddt_entry;
        }
        else if(ctx->userDataDdtHeader.sizeType == BigDdtSizeType)
        {
            // Overflow detection for DDT entry
            if(ddt_entry > 0xFFFFFFF)
            {
                FATAL("DDT overflow: media does not fit in big DDT");
                return;
            }

            ddt_entry |= (uint64_t)sector_status << 28;
            TRACE("Setting small secondary DDT entry %d to %u", sector_address % items_per_ddt_entry,
                  (uint16_t)ddt_entry);
            ctx->cachedSecondaryDdtBig[sector_address % items_per_ddt_entry] = (uint32_t)ddt_entry;
        }

        TRACE("Updated cached secondary DDT entry at position %" PRIu64, sector_address % items_per_ddt_entry);
        return;
    }

    // Step 2.5: Handle case where we have a cached secondary DDT that has never been written to disk
    // but does not contain the requested block
    if(ctx->cachedDdtOffset == 0 && (ctx->cachedSecondaryDdtSmall != NULL || ctx->cachedSecondaryDdtBig != NULL))
    {
        // Only write the cached table to disk if the requested block belongs to a different DDT position
        if(ddt_position != ctx->cachedDdtPosition)
        {
            TRACE("Current secondary DDT in memory belongs to position %" PRIu64
                  " but requested block needs position %" PRIu64,
                  ctx->cachedDdtPosition, ddt_position);

            // Write the cached DDT to disk before proceeding with the new one

            // Close the current data block first
            if(ctx->writingBuffer != NULL) aaruf_close_current_block(ctx);

            // Get current position and seek to end of file
            current_pos = ftell(ctx->imageStream);
            fseek(ctx->imageStream, 0, SEEK_END);
            end_of_file = ftell(ctx->imageStream);

            // Align to block boundary
            uint64_t alignment_mask = (1ULL << ctx->userDataDdtHeader.blockAlignmentShift) - 1;
            end_of_file             = (end_of_file + alignment_mask) & ~alignment_mask;
            fseek(ctx->imageStream, end_of_file, SEEK_SET);

            // Prepare DDT header for the never-written cached table
            memset(&ddt_header, 0, sizeof(DdtHeader2));
            ddt_header.identifier          = DeDuplicationTable2;
            ddt_header.type                = UserData;
            ddt_header.compression         = None;  // Use no compression for simplicity
            ddt_header.levels              = ctx->userDataDdtHeader.levels;
            ddt_header.tableLevel          = ctx->userDataDdtHeader.tableLevel + 1;
            ddt_header.previousLevelOffset = ctx->primaryDdtOffset;
            ddt_header.negative            = ctx->userDataDdtHeader.negative;
            ddt_header.blocks              = items_per_ddt_entry;
            ddt_header.overflow            = ctx->userDataDdtHeader.overflow;
            ddt_header.start = ctx->cachedDdtPosition * items_per_ddt_entry;  // Use cached position with table shift
            ddt_header.blockAlignmentShift = ctx->userDataDdtHeader.blockAlignmentShift;
            ddt_header.dataShift           = ctx->userDataDdtHeader.dataShift;
            ddt_header.tableShift          = 0;  // Secondary tables are single level
            ddt_header.sizeType            = ctx->userDataDdtHeader.sizeType;
            ddt_header.entries             = items_per_ddt_entry;

            // Calculate data size
            if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
                ddt_header.length = items_per_ddt_entry * sizeof(uint16_t);
            else
                ddt_header.length = items_per_ddt_entry * sizeof(uint32_t);

            ddt_header.cmpLength = ddt_header.length;

            // Calculate CRC64 of the data
            crc64_context = aaruf_crc64_init();
            if(crc64_context == NULL)
            {
                FATAL("Could not initialize CRC64.");
                return;
            }

            if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
                aaruf_crc64_update(crc64_context, (uint8_t *)ctx->cachedSecondaryDdtSmall, ddt_header.length);
            else
                aaruf_crc64_update(crc64_context, (uint8_t *)ctx->cachedSecondaryDdtBig, ddt_header.length);

            aaruf_crc64_final(crc64_context, &crc64);
            ddt_header.crc64    = crc64;
            ddt_header.cmpCrc64 = crc64;

            // Write header
            written_bytes = fwrite(&ddt_header, sizeof(DdtHeader2), 1, ctx->imageStream);
            if(written_bytes != 1)
            {
                FATAL("Could not write never-written DDT header to file.");
                return;
            }

            // Write data
            if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
                written_bytes = fwrite(ctx->cachedSecondaryDdtSmall, ddt_header.length, 1, ctx->imageStream);
            else
                written_bytes = fwrite(ctx->cachedSecondaryDdtBig, ddt_header.length, 1, ctx->imageStream);

            if(written_bytes != 1)
            {
                FATAL("Could not write never-written DDT data to file.");
                return;
            }

            // Add index entry for the newly written secondary DDT
            IndexEntry new_ddt_entry;
            new_ddt_entry.blockType = DeDuplicationTable2;
            new_ddt_entry.dataType  = UserData;
            new_ddt_entry.offset    = end_of_file;

            utarray_push_back(ctx->indexEntries, &new_ddt_entry);
            TRACE("Added new DDT index entry for never-written table at offset %" PRIu64, end_of_file);

            // Update the primary level table entry to point to the new location of the secondary table
            uint64_t new_secondary_table_block_offset = end_of_file >> ctx->userDataDdtHeader.blockAlignmentShift;

            if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
                ctx->userDataDdtMini[ctx->cachedDdtPosition] = (uint16_t)new_secondary_table_block_offset;
            else
                ctx->userDataDdtBig[ctx->cachedDdtPosition] = (uint32_t)new_secondary_table_block_offset;

            // Write the updated primary table back to its original position in the file
            long saved_pos = ftell(ctx->imageStream);
            fseek(ctx->imageStream, ctx->primaryDdtOffset + sizeof(DdtHeader2), SEEK_SET);

            size_t primary_table_size = ctx->userDataDdtHeader.sizeType == SmallDdtSizeType
                                            ? ctx->userDataDdtHeader.entries * sizeof(uint16_t)
                                            : ctx->userDataDdtHeader.entries * sizeof(uint32_t);

            if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
                written_bytes = fwrite(ctx->userDataDdtMini, primary_table_size, 1, ctx->imageStream);
            else
                written_bytes = fwrite(ctx->userDataDdtBig, primary_table_size, 1, ctx->imageStream);

            if(written_bytes != 1)
            {
                FATAL("Could not flush primary DDT table to file after writing never-written secondary table.");
                return;
            }

            // Update nextBlockPosition to ensure future blocks don't overwrite the DDT
            uint64_t ddt_total_size = sizeof(DdtHeader2) + ddt_header.length;
            ctx->nextBlockPosition  = (end_of_file + ddt_total_size + alignment_mask) & ~alignment_mask;
            block_offset            = ctx->nextBlockPosition;
            offset                  = 0;
            TRACE("Updated nextBlockPosition after never-written DDT write to %" PRIu64, ctx->nextBlockPosition);

            // Free the cached table
            if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType && ctx->cachedSecondaryDdtSmall)
            {
                free(ctx->cachedSecondaryDdtSmall);
                ctx->cachedSecondaryDdtSmall = NULL;
            }
            else if(ctx->userDataDdtHeader.sizeType == BigDdtSizeType && ctx->cachedSecondaryDdtBig)
            {
                free(ctx->cachedSecondaryDdtBig);
                ctx->cachedSecondaryDdtBig = NULL;
            }

            // Reset cached values since we've written and freed the table
            ctx->cachedDdtOffset   = 0;
            ctx->cachedDdtPosition = 0;

            // Restore file position
            fseek(ctx->imageStream, saved_pos, SEEK_SET);

            TRACE("Successfully wrote never-written cached secondary DDT to disk");
        }
        else
        {
            // The cached DDT is actually for the requested block range, so we can use it directly
            TRACE("Cached DDT is for the correct block range, using it directly");
            // No need to write to disk, just continue with the cached table
        }
    }

    // Step 3: Write the currently in-memory cached secondary level table to the end of the file
    if(ctx->cachedDdtOffset != 0)
    {
        // Close the current data block first
        if(ctx->writingBuffer != NULL) aaruf_close_current_block(ctx);

        // Get current position and seek to end of file
        current_pos = ftell(ctx->imageStream);
        fseek(ctx->imageStream, 0, SEEK_END);
        end_of_file = ftell(ctx->imageStream);

        // Align to block boundary
        uint64_t alignment_mask = (1ULL << ctx->userDataDdtHeader.blockAlignmentShift) - 1;
        end_of_file             = (end_of_file + alignment_mask) & ~alignment_mask;
        fseek(ctx->imageStream, end_of_file, SEEK_SET);

        // Prepare DDT header for the cached table
        memset(&ddt_header, 0, sizeof(DdtHeader2));
        ddt_header.identifier          = DeDuplicationTable2;
        ddt_header.type                = UserData;
        ddt_header.compression         = None;  // Use no compression for simplicity
        ddt_header.levels              = ctx->userDataDdtHeader.levels;
        ddt_header.tableLevel          = ctx->userDataDdtHeader.tableLevel + 1;
        ddt_header.previousLevelOffset = ctx->primaryDdtOffset;  // Set to primary DDT table location
        ddt_header.negative            = ctx->userDataDdtHeader.negative;
        ddt_header.blocks              = items_per_ddt_entry;
        ddt_header.overflow            = ctx->userDataDdtHeader.overflow;
        ddt_header.start               = ddt_position * items_per_ddt_entry;  // First block this DDT table references
        ddt_header.blockAlignmentShift = ctx->userDataDdtHeader.blockAlignmentShift;
        ddt_header.dataShift           = ctx->userDataDdtHeader.dataShift;
        ddt_header.tableShift          = 0;  // Secondary tables are single level
        ddt_header.sizeType            = ctx->userDataDdtHeader.sizeType;
        ddt_header.entries             = items_per_ddt_entry;

        // Calculate data size
        if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
            ddt_header.length = items_per_ddt_entry * sizeof(uint16_t);
        else
            ddt_header.length = items_per_ddt_entry * sizeof(uint32_t);

        ddt_header.cmpLength = ddt_header.length;

        // Calculate CRC64 of the data
        crc64_context = aaruf_crc64_init();
        if(crc64_context == NULL)
        {
            FATAL("Could not initialize CRC64.");
            return;
        }

        if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
            aaruf_crc64_update(crc64_context, (uint8_t *)ctx->cachedSecondaryDdtSmall, ddt_header.length);
        else
            aaruf_crc64_update(crc64_context, (uint8_t *)ctx->cachedSecondaryDdtBig, ddt_header.length);

        aaruf_crc64_final(crc64_context, &crc64);
        ddt_header.crc64    = crc64;
        ddt_header.cmpCrc64 = crc64;

        // Write header
        written_bytes = fwrite(&ddt_header, sizeof(DdtHeader2), 1, ctx->imageStream);
        if(written_bytes != 1)
        {
            FATAL("Could not write DDT header to file.");
            return;
        }

        // Write data
        if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
            written_bytes = fwrite(ctx->cachedSecondaryDdtSmall, ddt_header.length, 1, ctx->imageStream);
        else
            written_bytes = fwrite(ctx->cachedSecondaryDdtBig, ddt_header.length, 1, ctx->imageStream);

        if(written_bytes != 1)
        {
            FATAL("Could not write DDT data to file.");
            return;
        }

        // Update index: remove old entry and add new one for the evicted secondary DDT
        TRACE("Updating index for evicted secondary DDT");

        // Remove old index entry for the cached DDT
        if(ctx->cachedDdtOffset != 0)
        {
            TRACE("Removing old index entry for DDT at offset %" PRIu64, ctx->cachedDdtOffset);
            IndexEntry *entry = NULL;

            // Find and remove the old index entry
            for(unsigned int i = 0; i < utarray_len(ctx->indexEntries); i++)
            {
                entry = (IndexEntry *)utarray_eltptr(ctx->indexEntries, i);
                if(entry && entry->offset == ctx->cachedDdtOffset && entry->blockType == DeDuplicationTable2)
                {
                    TRACE("Found old DDT index entry at position %u, removing", i);
                    utarray_erase(ctx->indexEntries, i, 1);
                    break;
                }
            }
        }

        // Add new index entry for the newly written secondary DDT
        IndexEntry new_ddt_entry;
        new_ddt_entry.blockType = DeDuplicationTable2;
        new_ddt_entry.dataType  = UserData;
        new_ddt_entry.offset    = end_of_file;

        utarray_push_back(ctx->indexEntries, &new_ddt_entry);
        TRACE("Added new DDT index entry at offset %" PRIu64, end_of_file);

        // Step 4: Update the primary level table entry and flush it back to file
        uint64_t new_secondary_table_block_offset = end_of_file >> ctx->userDataDdtHeader.blockAlignmentShift;

        // Update the primary table entry to point to the new location of the secondary table
        // Use ddtPosition which was calculated from sectorAddress, not cachedDdtOffset
        if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
            ctx->userDataDdtMini[ddt_position] = (uint16_t)new_secondary_table_block_offset;
        else
            ctx->userDataDdtBig[ddt_position] = (uint32_t)new_secondary_table_block_offset;

        // Write the updated primary table back to its original position in the file
        long saved_pos = ftell(ctx->imageStream);
        fseek(ctx->imageStream, ctx->primaryDdtOffset + sizeof(DdtHeader2), SEEK_SET);

        size_t primary_table_size = ctx->userDataDdtHeader.sizeType == SmallDdtSizeType
                                        ? ctx->userDataDdtHeader.entries * sizeof(uint16_t)
                                        : ctx->userDataDdtHeader.entries * sizeof(uint32_t);

        if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
            written_bytes = fwrite(ctx->userDataDdtMini, primary_table_size, 1, ctx->imageStream);
        else
            written_bytes = fwrite(ctx->userDataDdtBig, primary_table_size, 1, ctx->imageStream);

        if(written_bytes != 1)
        {
            FATAL("Could not flush primary DDT table to file.");
            return;
        }

        // Update nextBlockPosition to ensure future blocks don't overwrite the DDT
        uint64_t ddt_total_size = sizeof(DdtHeader2) + ddt_header.length;
        ctx->nextBlockPosition  = (end_of_file + ddt_total_size + alignment_mask) & ~alignment_mask;
        block_offset            = ctx->nextBlockPosition;
        offset                  = 0;
        TRACE("Updated nextBlockPosition after DDT write to %" PRIu64, ctx->nextBlockPosition);

        fseek(ctx->imageStream, saved_pos, SEEK_SET);

        // Free the cached table
        if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType && ctx->cachedSecondaryDdtSmall)
        {
            free(ctx->cachedSecondaryDdtSmall);
            ctx->cachedSecondaryDdtSmall = NULL;
        }
        else if(ctx->userDataDdtHeader.sizeType == BigDdtSizeType && ctx->cachedSecondaryDdtBig)
        {
            free(ctx->cachedSecondaryDdtBig);
            ctx->cachedSecondaryDdtBig = NULL;
        }

        // Restore file position
        fseek(ctx->imageStream, current_pos, SEEK_SET);
    }

    // Step 5: Check if the specified block already has an existing secondary level table
    create_new_table = ctx->cachedSecondaryDdtSmall == NULL && ctx->cachedSecondaryDdtBig == NULL;

    if(!create_new_table && secondary_ddt_offset != 0)
    {
        // Load existing table
        fseek(ctx->imageStream, secondary_ddt_offset, SEEK_SET);
        size_t read_bytes = fread(&ddt_header, 1, sizeof(DdtHeader2), ctx->imageStream);

        if(read_bytes != sizeof(DdtHeader2) || ddt_header.identifier != DeDuplicationTable2 ||
           ddt_header.type != UserData)
        {
            FATAL("Invalid secondary DDT header at %" PRIu64, secondaryDdtOffset);
            return;
        }

        // Read the table data (assuming no compression for now)
        buffer = malloc(ddt_header.length);
        if(buffer == NULL)
        {
            FATAL("Cannot allocate memory for secondary DDT.");
            return;
        }

        read_bytes = fread(buffer, 1, ddt_header.length, ctx->imageStream);
        if(read_bytes != ddt_header.length)
        {
            FATAL("Could not read secondary DDT data.");
            free(buffer);
            return;
        }

        // Verify CRC
        crc64_context = aaruf_crc64_init();
        if(crc64_context == NULL)
        {
            FATAL("Could not initialize CRC64.");
            free(buffer);
            return;
        }

        aaruf_crc64_update(crc64_context, buffer, read_bytes);
        aaruf_crc64_final(crc64_context, &crc64);

        if(crc64 != ddt_header.crc64)
        {
            FATAL("Secondary DDT CRC mismatch. Expected 0x%16lX but got 0x%16lX.", ddtHeader.crc64, crc64);
            free(buffer);
            return;
        }

        // Cache the loaded table
        if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
            ctx->cachedSecondaryDdtSmall = (uint16_t *)buffer;
        else
            ctx->cachedSecondaryDdtBig = (uint32_t *)buffer;

        ctx->cachedDdtOffset = secondary_ddt_offset;
    }

    if(create_new_table)
    {
        // Create a new empty table
        size_t table_size = ctx->userDataDdtHeader.sizeType == SmallDdtSizeType
                                ? items_per_ddt_entry * sizeof(uint16_t)
                                : items_per_ddt_entry * sizeof(uint32_t);

        buffer = calloc(1, table_size);
        if(buffer == NULL)
        {
            FATAL("Cannot allocate memory for new secondary DDT.");
            return;
        }

        if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
            ctx->cachedSecondaryDdtSmall = (uint16_t *)buffer;
        else
            ctx->cachedSecondaryDdtBig = (uint32_t *)buffer;

        ctx->cachedDdtOffset   = 0;             // Will be set when written to file
        ctx->cachedDdtPosition = ddt_position;  // Track which primary DDT position this new table belongs to
        TRACE("Created new secondary DDT for position %" PRIu64, ddt_position);
    }

    // Step 6: Update the corresponding DDT entry
    block_index = block_offset >> ctx->userDataDdtHeader.blockAlignmentShift;
    ddt_entry   = offset & (1ULL << ctx->userDataDdtHeader.dataShift) - 1 | block_index
                                                                              << ctx->userDataDdtHeader.dataShift;

    if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
    {
        // Overflow detection for DDT entry
        if(ddt_entry > 0xFFF)
        {
            FATAL("DDT overflow: media does not fit in small DDT");
            return;
        }

        ddt_entry |= (uint64_t)sector_status << 12;
        TRACE("Setting small secondary DDT entry %d to %u", sector_address % items_per_ddt_entry, (uint16_t)ddt_entry);
        ctx->cachedSecondaryDdtSmall[sector_address % items_per_ddt_entry] = (uint16_t)ddt_entry;
    }
    else if(ctx->userDataDdtHeader.sizeType == BigDdtSizeType)
    {
        // Overflow detection for DDT entry
        if(ddt_entry > 0xFFFFFFF)
        {
            FATAL("DDT overflow: media does not fit in big DDT");
            return;
        }

        ddt_entry |= (uint64_t)sector_status << 28;
        TRACE("Setting big secondary DDT entry %d to %u", sector_address % items_per_ddt_entry, (uint32_t)ddt_entry);
        ctx->cachedSecondaryDdtBig[sector_address % items_per_ddt_entry] = (uint32_t)ddt_entry;
    }

    TRACE("Updated secondary DDT entry at position %" PRIu64, sector_address % items_per_ddt_entry);
    TRACE("Exiting set_ddt_multi_level_v2()");
}
