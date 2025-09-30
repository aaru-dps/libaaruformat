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

#ifdef __linux__
#include <sys/mman.h>
#endif

#include "aaruformat.h"
#include "log.h"

/**
 * @brief Processes a DDT v1 block from the image stream.
 *
 * Reads and decompresses (if needed) a DDT v1 block, verifies its integrity, and loads it into memory or maps it.
 *
 * @param ctx Pointer to the aaruformat context.
 * @param entry Pointer to the index entry describing the DDT block.
 * @param foundUserDataDdt Pointer to a boolean that will be set to true if a user data DDT was found and loaded.
 * @return AARUF_STATUS_OK on success, or an error code on failure.
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
 * Determines the offset and block offset for a sector using the DDT v1 table.
 *
 * @param ctx Pointer to the aaruformat context.
 * @param sector_address Logical sector address to decode.
 * @param offset Pointer to store the resulting offset.
 * @param block_offset Pointer to store the resulting block offset.
 * @param sector_status Pointer to store the sector status.
 * @return AARUF_STATUS_OK on success, or an error code on failure.
 */
int32_t decode_ddt_entry_v1(aaruformatContext *ctx, uint64_t sector_address, uint64_t *offset, uint64_t *block_offset,
                            uint8_t *sector_status)
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

    const uint64_t ddt_entry   = ctx->userDataDdt[sector_address];
    const uint32_t offset_mask = (uint32_t)((1 << ctx->shift) - 1);
    *offset                    = ddt_entry & offset_mask;
    *block_offset              = ddt_entry >> ctx->shift;

    // Partially written image... as we can't know the real sector size just assume it's common :/
    if(ddt_entry == 0)
        *sector_status = SectorStatusNotDumped;
    else
        *sector_status = SectorStatusDumped;

    TRACE("Exiting decode_ddt_entry_v1(%p, %" PRIu64 ", %llu, %llu, %d) = AARUF_STATUS_OK", ctx, sector_address,
          *offset, *block_offset, *sector_status);
    return AARUF_STATUS_OK;
}