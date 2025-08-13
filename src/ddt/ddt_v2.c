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

int32_t process_ddt_v2(aaruformatContext *ctx, IndexEntry *entry, bool *foundUserDataDdt)
{
    TRACE("Entering process_ddt_v2(%p, %p, %d)", ctx, entry, *foundUserDataDdt);

    int        pos       = 0;
    size_t     readBytes = 0;
    DdtHeader2 ddtHeader;
    uint8_t   *cmpData = NULL;
    uint8_t    lzmaProperties[LZMA_PROPERTIES_LENGTH];
    size_t     lzmaSize      = 0;
    int        errorNo       = 0;
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
    readBytes = fread(&ddtHeader, 1, sizeof(DdtHeader2), ctx->imageStream);

    if(readBytes != sizeof(DdtHeader2))
    {
        FATAL("Could not read block header at %" PRIu64 "", entry->offset);

        TRACE("Exiting process_ddt_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    *foundUserDataDdt = false;

    ctx->imageInfo.ImageSize += ddtHeader.cmpLength;

    if(entry->dataType == UserData)
    {
        // User area sectors is blocks stored in DDT minus the negative and overflow displacement blocks
        ctx->imageInfo.Sectors = ddtHeader.blocks - ddtHeader.negative - ddtHeader.overflow;
        // We need the header later for the shift calculations
        ctx->userDataDdtHeader = ddtHeader;
        ctx->ddtVersion        = 2;

        // Check for DDT compression
        switch(ddtHeader.compression)
        {
            case Lzma:
                lzmaSize = ddtHeader.cmpLength - LZMA_PROPERTIES_LENGTH;

                cmpData = (uint8_t *)malloc(lzmaSize);
                if(cmpData == NULL)
                {
                    TRACE("Cannot allocate memory for DDT, continuing...");
                    break;
                }

                buffer = malloc(ddtHeader.length);
                if(buffer == NULL)
                {
                    TRACE("Cannot allocate memory for DDT, continuing...");
                    free(cmpData);
                    break;
                }

                readBytes = fread(lzmaProperties, 1, LZMA_PROPERTIES_LENGTH, ctx->imageStream);
                if(readBytes != LZMA_PROPERTIES_LENGTH)
                {
                    TRACE("Could not read LZMA properties, continuing...");
                    free(cmpData);
                    free(buffer);
                    break;
                }

                readBytes = fread(cmpData, 1, lzmaSize, ctx->imageStream);
                if(readBytes != lzmaSize)
                {
                    TRACE("Could not read compressed block, continuing...");
                    free(cmpData);
                    free(buffer);
                    break;
                }

                readBytes = ddtHeader.length;
                TRACE("Decompressing block of size %zu bytes", ddtHeader.length);
                errorNo = aaruf_lzma_decode_buffer(buffer, &readBytes, cmpData, &lzmaSize, lzmaProperties,
                                                   LZMA_PROPERTIES_LENGTH);

                if(errorNo != 0)
                {
                    FATAL("Got error %d from LZMA, stopping...", errorNo);
                    free(cmpData);
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                if(readBytes != ddtHeader.length)
                {
                    FATAL("Error decompressing block, should be {0} bytes but got {1} bytes., stopping...");
                    free(cmpData);
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                free(cmpData);

                crc64_context = aaruf_crc64_init();

                if(crc64_context == NULL)
                {
                    FATAL("Could not initialize CRC64.");
                    free(buffer);

                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                aaruf_crc64_update(crc64_context, buffer, readBytes);
                aaruf_crc64_final(crc64_context, &crc64);

                if(crc64 != ddtHeader.crc64)
                {
                    FATAL("Expected DDT CRC 0x%16lX but got 0x%16lX.", ddtHeader.crc64, crc64);
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_INVALID_BLOCK_CRC");
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                if(ddtHeader.sizeType == SmallDdtSizeType)
                    ctx->userDataDdtMini = (uint16_t *)buffer;
                else if(ddtHeader.sizeType == BigDdtSizeType)
                    ctx->userDataDdtBig = (uint32_t *)buffer;

                ctx->inMemoryDdt  = true;
                *foundUserDataDdt = true;

                break;
            case None:
                buffer = malloc(ddtHeader.length);

                if(buffer == NULL)
                {
                    TRACE("Cannot allocate memory for DDT, continuing...");
                    break;
                }

                TRACE("Reading DDT of length %zu bytes", ddtHeader.length);
                readBytes = fread(buffer, 1, ddtHeader.length, ctx->imageStream);

                if(readBytes != ddtHeader.length)
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

                aaruf_crc64_update(crc64_context, buffer, readBytes);
                aaruf_crc64_final(crc64_context, &crc64);

                if(crc64 != ddtHeader.crc64)
                {
                    FATAL("Expected DDT CRC 0x%16lX but got 0x%16lX.", ddtHeader.crc64, crc64);
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_INVALID_BLOCK_CRC");
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                if(ddtHeader.sizeType == SmallDdtSizeType)
                    ctx->userDataDdtMini = (uint16_t *)buffer;
                else if(ddtHeader.sizeType == BigDdtSizeType)
                    ctx->userDataDdtBig = (uint32_t *)buffer;

                ctx->inMemoryDdt  = true;
                *foundUserDataDdt = true;

                break;
            default:
                TRACE("Found unknown compression type %d, continuing...", ddtHeader.compression);
                *foundUserDataDdt = false;
                break;
        }
    }
    else if(entry->dataType == CdSectorPrefixCorrected || entry->dataType == CdSectorSuffixCorrected)
    {
        switch(ddtHeader.compression)
        {
            case Lzma:
                lzmaSize = ddtHeader.cmpLength - LZMA_PROPERTIES_LENGTH;

                cmpData = (uint8_t *)malloc(lzmaSize);
                if(cmpData == NULL)
                {
                    TRACE("Cannot allocate memory for DDT, continuing...");
                    break;
                }

                buffer = malloc(ddtHeader.length);
                if(buffer == NULL)
                {
                    TRACE("Cannot allocate memory for DDT, continuing...");
                    free(cmpData);
                    break;
                }

                readBytes = fread(lzmaProperties, 1, LZMA_PROPERTIES_LENGTH, ctx->imageStream);
                if(readBytes != LZMA_PROPERTIES_LENGTH)
                {
                    TRACE("Could not read LZMA properties, continuing...");
                    free(cmpData);
                    free(buffer);
                    break;
                }

                readBytes = fread(cmpData, 1, lzmaSize, ctx->imageStream);
                if(readBytes != lzmaSize)
                {
                    TRACE("Could not read compressed block, continuing...");
                    free(cmpData);
                    free(buffer);
                    break;
                }

                readBytes = ddtHeader.length;
                TRACE("Decompressing block of size %zu bytes", ddtHeader.length);
                errorNo = aaruf_lzma_decode_buffer(buffer, &readBytes, cmpData, &lzmaSize, lzmaProperties,
                                                   LZMA_PROPERTIES_LENGTH);

                if(errorNo != 0)
                {
                    FATAL("Got error %d from LZMA, stopping...", errorNo);
                    free(cmpData);
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                if(readBytes != ddtHeader.length)
                {
                    FATAL("Error decompressing block, should be {0} bytes but got {1} bytes., stopping...");
                    free(cmpData);
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

                aaruf_crc64_update(crc64_context, buffer, readBytes);
                aaruf_crc64_final(crc64_context, &crc64);

                if(crc64 != ddtHeader.crc64)
                {
                    FATAL("Expected DDT CRC 0x%16lX but got 0x%16lX.", ddtHeader.crc64, crc64);
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_INVALID_BLOCK_CRC");
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                if(entry->dataType == CdSectorPrefixCorrected)
                {
                    if(ddtHeader.sizeType == SmallDdtSizeType)
                        ctx->sectorPrefixDdtMini = (uint16_t *)buffer;
                    else if(ddtHeader.sizeType == BigDdtSizeType)
                        ctx->sectorPrefixDdt = (uint32_t *)buffer;
                }
                else if(entry->dataType == CdSectorSuffixCorrected)
                {
                    if(ddtHeader.sizeType == SmallDdtSizeType)
                        ctx->sectorSuffixDdtMini = (uint16_t *)buffer;
                    else if(ddtHeader.sizeType == BigDdtSizeType)
                        ctx->sectorSuffixDdt = (uint32_t *)buffer;
                }
                else
                    free(buffer);

                break;

            case None:
                buffer = malloc(ddtHeader.length);

                if(buffer == NULL)
                {
                    TRACE("Cannot allocate memory for deduplication table.");
                    break;
                }

                readBytes = fread(buffer, 1, ddtHeader.length, ctx->imageStream);

                if(readBytes != ddtHeader.length)
                {
                    free(buffer);
                    FATAL("Could not read deduplication table, continuing...");
                    break;
                }

                crc64_context = aaruf_crc64_init();

                if(crc64_context == NULL)
                {
                    FATAL(stderr, "Could not initialize CRC64.");
                    free(buffer);
                    TRACE("Exiting process_ddt_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                aaruf_crc64_update(crc64_context, buffer, readBytes);
                aaruf_crc64_final(crc64_context, &crc64);

                if(crc64 != ddtHeader.crc64)
                {
                    FATAL("Expected DDT CRC 0x%16lX but got 0x%16lX.", ddtHeader.crc64, crc64);
                    free(buffer);
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                if(entry->dataType == CdSectorPrefixCorrected)
                {
                    if(ddtHeader.sizeType == SmallDdtSizeType)
                        ctx->sectorPrefixDdtMini = (uint16_t *)buffer;
                    else if(ddtHeader.sizeType == BigDdtSizeType)
                        ctx->sectorPrefixDdt = (uint32_t *)buffer;
                }
                else if(entry->dataType == CdSectorSuffixCorrected)
                {
                    if(ddtHeader.sizeType == SmallDdtSizeType)
                        ctx->sectorSuffixDdtMini = (uint16_t *)buffer;
                    else if(ddtHeader.sizeType == BigDdtSizeType)
                        ctx->sectorSuffixDdt = (uint32_t *)buffer;
                }
                else
                    free(buffer);

                break;
            default:
                TRACE("Found unknown compression type %d, continuing...", ddtHeader.compression);
                break;
        }
    }

    TRACE("Exiting process_ddt_v2() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

int32_t decode_ddt_entry_v2(aaruformatContext *ctx, uint64_t sectorAddress, uint64_t *offset, uint64_t *blockOffset,
                            uint8_t *sectorStatus)
{
    TRACE("Entering decode_ddt_entry_v2(%p, %" PRIu64 ", %llu, %llu, %d)", ctx, sectorAddress, *offset, *blockOffset,
          *sectorStatus);
    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");

        TRACE("Exiting decode_ddt_entry_v2() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->userDataDdtHeader.tableShift > 0)
        return decode_ddt_multi_level_v2(ctx, sectorAddress, offset, blockOffset, sectorStatus);

    return decode_ddt_single_level_v2(ctx, sectorAddress, offset, blockOffset, sectorStatus);
}

int32_t decode_ddt_single_level_v2(aaruformatContext *ctx, uint64_t sectorAddress, uint64_t *offset,
                                   uint64_t *blockOffset, uint8_t *sectorStatus)
{
    TRACE("Entering decode_ddt_single_level_v2(%p, %" PRIu64 ", %llu, %llu, %d)", ctx, sectorAddress, *offset,
          *blockOffset, *sectorStatus);

    uint64_t ddtEntry = 0;

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
    sectorAddress += ctx->userDataDdtHeader.negative;

    if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
        ddtEntry = ctx->userDataDdtMini[sectorAddress];
    else if(ctx->userDataDdtHeader.sizeType == BigDdtSizeType)
        ddtEntry = ctx->userDataDdtBig[sectorAddress];
    else
    {
        FATAL("Unknown DDT size type %d.", ctx->userDataDdtHeader.sizeType);
        TRACE("Exiting decode_ddt_single_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    if(ddtEntry == 0)
    {
        *sectorStatus = SectorStatusNotDumped;
        *offset       = 0;
        *blockOffset  = 0;
        TRACE("Exiting decode_ddt_single_level_v2(%p, %" PRIu64 ", %llu, %llu, %d) = AARUF_STATUS_OK", ctx,
              sectorAddress, *offset, *blockOffset, *sectorStatus);
        return AARUF_STATUS_OK;
    }

    if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
    {
        *sectorStatus = ddtEntry >> 12;
        ddtEntry &= 0xfff;
    }
    else if(ctx->userDataDdtHeader.sizeType == BigDdtSizeType)
    {
        *sectorStatus = ddtEntry >> 28;
        ddtEntry &= 0x0fffffff;
    }

    const uint64_t offsetMask = (uint64_t)((1 << ctx->userDataDdtHeader.dataShift) - 1);
    *offset                   = ddtEntry & offsetMask;
    *blockOffset = (ddtEntry >> ctx->userDataDdtHeader.dataShift) * (1 << ctx->userDataDdtHeader.blockAlignmentShift);

    TRACE("Exiting decode_ddt_single_level_v2(%p, %" PRIu64 ", %llu, %llu, %d) = AARUF_STATUS_OK", ctx, sectorAddress,
          *offset, *blockOffset, *sectorStatus);
    return AARUF_STATUS_OK;
}

int32_t decode_ddt_multi_level_v2(aaruformatContext *ctx, uint64_t sectorAddress, uint64_t *offset,
                                  uint64_t *blockOffset, uint8_t *sectorStatus)
{
    TRACE("Entering decode_ddt_multi_level_v2(%p, %" PRIu64 ", %llu, %llu, %d)", ctx, sectorAddress, *offset,
          *blockOffset, *sectorStatus);

    uint64_t   ddtEntry = 0;
    uint8_t    lzmaProperties[LZMA_PROPERTIES_LENGTH];
    size_t     lzmaSize           = 0;
    uint8_t   *cmpData            = NULL;
    uint8_t   *buffer             = NULL;
    int32_t    errorNo            = 0;
    crc64_ctx *crc64_context      = NULL;
    uint64_t   crc64              = 0;
    int        itemsPerDdtEntry   = 0;
    uint64_t   ddtPosition        = 0;
    uint64_t   secondaryDdtOffset = 0;

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
    sectorAddress += ctx->userDataDdtHeader.negative;

    itemsPerDdtEntry = 1 << ctx->userDataDdtHeader.tableShift;
    ddtPosition      = sectorAddress / itemsPerDdtEntry;

    if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
        secondaryDdtOffset = ctx->userDataDdtMini[ddtPosition];
    else if(ctx->userDataDdtHeader.sizeType == BigDdtSizeType)
        secondaryDdtOffset = ctx->userDataDdtBig[ddtPosition];
    else
    {
        FATAL("Unknown DDT size type %d.", ctx->userDataDdtHeader.sizeType);
        TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    // Position in file of the child DDT table
    secondaryDdtOffset *= (1 << ctx->userDataDdtHeader.blockAlignmentShift);

    // Is the one we have cached the same as the one we need to read?
    if(ctx->cachedDdtOffset != secondaryDdtOffset)
    {
        fseek(ctx->imageStream, secondaryDdtOffset, SEEK_SET);
        DdtHeader2 ddtHeader;
        size_t     readBytes = fread(&ddtHeader, 1, sizeof(DdtHeader2), ctx->imageStream);

        if(readBytes != sizeof(DdtHeader2))
        {
            FATAL("Could not read block header at %" PRIu64 "", secondaryDdtOffset);
            TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
            return AARUF_ERROR_CANNOT_READ_BLOCK;
        }

        if(ddtHeader.identifier != DeDuplicationTable2 || ddtHeader.type != UserData)
        {
            FATAL("Invalid block header at %" PRIu64 "", secondaryDdtOffset);
            TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
            return AARUF_ERROR_CANNOT_READ_BLOCK;
        }

        // Check for DDT compression
        switch(ddtHeader.compression)
        {
            case Lzma:
                lzmaSize = ddtHeader.cmpLength - LZMA_PROPERTIES_LENGTH;

                cmpData = (uint8_t *)malloc(lzmaSize);
                if(cmpData == NULL)
                {
                    FATAL("Cannot allocate memory for DDT, stopping...");
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                buffer = malloc(ddtHeader.length);
                if(buffer == NULL)
                {
                    FATAL("Cannot allocate memory for DDT, stopping...");
                    free(cmpData);
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                readBytes = fread(lzmaProperties, 1, LZMA_PROPERTIES_LENGTH, ctx->imageStream);
                if(readBytes != LZMA_PROPERTIES_LENGTH)
                {
                    FATAL("Could not read LZMA properties, stopping...");
                    free(cmpData);
                    free(buffer);
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                readBytes = fread(cmpData, 1, lzmaSize, ctx->imageStream);
                if(readBytes != lzmaSize)
                {
                    FATAL("Could not read compressed block, stopping...");
                    free(cmpData);
                    free(buffer);
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                TRACE("Decompressing block of size %zu bytes", ddtHeader.length);
                readBytes = ddtHeader.length;
                errorNo   = aaruf_lzma_decode_buffer(buffer, &readBytes, cmpData, &lzmaSize, lzmaProperties,
                                                     LZMA_PROPERTIES_LENGTH);

                if(errorNo != 0)
                {
                    FATAL("Got error %d from LZMA, stopping...", errorNo);
                    free(cmpData);
                    free(buffer);
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                if(readBytes != ddtHeader.length)
                {
                    FATAL("Error decompressing block, should be {0} bytes but got {1} bytes., stopping...");
                    free(cmpData);
                    free(buffer);
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                free(cmpData);

                crc64_context = aaruf_crc64_init();

                if(crc64_context == NULL)
                {
                    FATAL("Could not initialize CRC64.");
                    free(buffer);
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                aaruf_crc64_update(crc64_context, buffer, readBytes);
                aaruf_crc64_final(crc64_context, &crc64);

                if(crc64 != ddtHeader.crc64)
                {
                    FATAL("Expected DDT CRC 0x%16lX but got 0x%16lX.", ddtHeader.crc64, crc64);
                    free(buffer);
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_INVALID_BLOCK_CRC");
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                if(ddtHeader.sizeType == SmallDdtSizeType)
                    ctx->cachedSecondaryDdtSmall = (uint16_t *)buffer;
                else if(ddtHeader.sizeType == BigDdtSizeType)
                    ctx->cachedSecondaryDdtBig = (uint32_t *)buffer;

                ctx->cachedDdtOffset = secondaryDdtOffset;

                break;
            case None:
                buffer = malloc(ddtHeader.length);

                if(buffer == NULL)
                {
                    FATAL(stderr, "Cannot allocate memory for DDT, stopping...");
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                readBytes = fread(buffer, 1, ddtHeader.length, ctx->imageStream);

                if(readBytes != ddtHeader.length)
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

                aaruf_crc64_update(crc64_context, buffer, readBytes);
                aaruf_crc64_final(crc64_context, &crc64);

                if(crc64 != ddtHeader.crc64)
                {
                    FATAL("Expected DDT CRC 0x%16lX but got 0x%16lX.", ddtHeader.crc64, crc64);
                    free(buffer);
                    TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_INVALID_BLOCK_CRC");
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                if(ddtHeader.sizeType == SmallDdtSizeType)
                    ctx->cachedSecondaryDdtSmall = (uint16_t *)buffer;
                else if(ddtHeader.sizeType == BigDdtSizeType)
                    ctx->cachedSecondaryDdtBig = (uint32_t *)buffer;

                ctx->cachedDdtOffset = secondaryDdtOffset;

                break;
            default:
                FATAL("Found unknown compression type %d, stopping...", ddtHeader.compression);
                TRACE("Exiting decode_ddt_multi_level_v2() = AARUF_ERROR_CANNOT_READ_BLOCK");
                return AARUF_ERROR_CANNOT_READ_BLOCK;
        }
    }

    if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
        ddtEntry = ctx->cachedSecondaryDdtSmall[sectorAddress];
    else if(ctx->userDataDdtHeader.sizeType == BigDdtSizeType)
        ddtEntry = ctx->cachedSecondaryDdtBig[sectorAddress];

    if(ddtEntry == 0)
    {
        *sectorStatus = SectorStatusNotDumped;
        *offset       = 0;
        *blockOffset  = 0;

        TRACE("Exiting decode_ddt_multi_level_v2(%p, %" PRIu64 ", %llu, %llu, %d) = AARUF_STATUS_OK", ctx,
              sectorAddress, *offset, *blockOffset, *sectorStatus);
        return AARUF_STATUS_OK;
    }

    if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
    {
        *sectorStatus = ddtEntry >> 12;
        ddtEntry &= 0xfff;
    }
    else if(ctx->userDataDdtHeader.sizeType == BigDdtSizeType)
    {
        *sectorStatus = ddtEntry >> 28;
        ddtEntry &= 0x0fffffff;
    }

    const uint64_t offsetMask = (uint64_t)((1 << ctx->userDataDdtHeader.dataShift) - 1);
    *offset                   = ddtEntry & offsetMask;
    *blockOffset = (ddtEntry >> ctx->userDataDdtHeader.dataShift) * (1 << ctx->userDataDdtHeader.blockAlignmentShift);

    TRACE("Exiting decode_ddt_multi_level_v2(%p, %" PRIu64 ", %llu, %llu, %d) = AARUF_STATUS_OK", ctx, sectorAddress,
          *offset, *blockOffset, *sectorStatus);
    return AARUF_STATUS_OK;
}