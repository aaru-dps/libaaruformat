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

int32_t process_ddt_v2(aaruformatContext *ctx, IndexEntry *entry, bool *foundUserDataDdt)
{
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
        fprintf(stderr, "Invalid context or image stream.\n");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Seek to block
    pos = fseek(ctx->imageStream, entry->offset, SEEK_SET);
    if(pos < 0 || ftell(ctx->imageStream) != entry->offset)
    {
        fprintf(stderr, "libaaruformat: Could not seek to %" PRIu64 " as indicated by index entry...\n", entry->offset);

        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    // Even if those two checks shall have been done before

    readBytes = fread(&ddtHeader, 1, sizeof(DdtHeader2), ctx->imageStream);

    if(readBytes != sizeof(DdtHeader2))
    {
        fprintf(stderr, "libaaruformat: Could not read block header at %" PRIu64 "\n", entry->offset);

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
                    fprintf(stderr, "Cannot allocate memory for DDT, continuing...\n");
                    break;
                }

                buffer = malloc(ddtHeader.length);
                if(buffer == NULL)
                {
                    fprintf(stderr, "Cannot allocate memory for DDT, continuing...\n");
                    free(cmpData);
                    break;
                }

                readBytes = fread(lzmaProperties, 1, LZMA_PROPERTIES_LENGTH, ctx->imageStream);
                if(readBytes != LZMA_PROPERTIES_LENGTH)
                {
                    fprintf(stderr, "Could not read LZMA properties, continuing...\n");
                    free(cmpData);
                    free(buffer);
                    break;
                }

                readBytes = fread(cmpData, 1, lzmaSize, ctx->imageStream);
                if(readBytes != lzmaSize)
                {
                    fprintf(stderr, "Could not read compressed block, continuing...\n");
                    free(cmpData);
                    free(buffer);
                    break;
                }

                readBytes = ddtHeader.length;
                errorNo   = aaruf_lzma_decode_buffer(buffer, &readBytes, cmpData, &lzmaSize, lzmaProperties,
                                                     LZMA_PROPERTIES_LENGTH);

                if(errorNo != 0)
                {
                    fprintf(stderr, "Got error %d from LZMA, stopping...\n", errorNo);
                    free(cmpData);
                    free(buffer);
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                if(readBytes != ddtHeader.length)
                {
                    fprintf(stderr, "Error decompressing block, should be {0} bytes but got {1} bytes., stopping...\n");
                    free(cmpData);
                    free(ctx->userDataDdt);
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                free(cmpData);

                crc64_context = aaruf_crc64_init();

                if(crc64_context == NULL)
                {
                    fprintf(stderr, "Could not initialize CRC64.\n");
                    free(buffer);
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                aaruf_crc64_update(crc64_context, buffer, readBytes);
                aaruf_crc64_final(crc64_context, &crc64);

                if(crc64 != ddtHeader.crc64)
                {
                    fprintf(stderr, "Expected DDT CRC 0x%16lX but got 0x%16lX.\n", ddtHeader.crc64, crc64);
                    free(buffer);
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
                    fprintf(stderr, "Cannot allocate memory for DDT, continuing...\n");
                    free(cmpData);
                    break;
                }

                readBytes = fread(buffer, 1, ddtHeader.length, ctx->imageStream);

                if(readBytes != ddtHeader.length)
                {
                    free(buffer);
                    fprintf(stderr, "libaaruformat: Could not read deduplication table, continuing...\n");
                    break;
                }

                crc64_context = aaruf_crc64_init();

                if(crc64_context == NULL)
                {
                    fprintf(stderr, "Could not initialize CRC64.\n");
                    free(buffer);
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                aaruf_crc64_update(crc64_context, buffer, readBytes);
                aaruf_crc64_final(crc64_context, &crc64);

                if(crc64 != ddtHeader.crc64)
                {
                    fprintf(stderr, "Expected DDT CRC 0x%16lX but got 0x%16lX.\n", ddtHeader.crc64, crc64);
                    free(buffer);
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
                fprintf(stderr, "libaaruformat: Found unknown compression type %d, continuing...\n",
                        ddtHeader.compression);
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
                    fprintf(stderr, "Cannot allocate memory for DDT, continuing...\n");
                    break;
                }

                buffer = malloc(ddtHeader.length);
                if(buffer == NULL)
                {
                    fprintf(stderr, "Cannot allocate memory for DDT, continuing...\n");
                    free(cmpData);
                    break;
                }

                readBytes = fread(lzmaProperties, 1, LZMA_PROPERTIES_LENGTH, ctx->imageStream);
                if(readBytes != LZMA_PROPERTIES_LENGTH)
                {
                    fprintf(stderr, "Could not read LZMA properties, continuing...\n");
                    free(cmpData);
                    free(buffer);
                    break;
                }

                readBytes = fread(cmpData, 1, lzmaSize, ctx->imageStream);
                if(readBytes != lzmaSize)
                {
                    fprintf(stderr, "Could not read compressed block, continuing...\n");
                    free(cmpData);
                    break;
                }

                readBytes = ddtHeader.length;
                errorNo   = aaruf_lzma_decode_buffer(buffer, &readBytes, cmpData, &lzmaSize, lzmaProperties,
                                                     LZMA_PROPERTIES_LENGTH);

                if(errorNo != 0)
                {
                    fprintf(stderr, "Got error %d from LZMA, stopping...\n", errorNo);
                    free(cmpData);
                    free(buffer);
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                if(readBytes != ddtHeader.length)
                {
                    fprintf(stderr, "Error decompressing block, should be {0} bytes but got {1} bytes., stopping...\n");
                    free(cmpData);
                    free(buffer);
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                crc64_context = aaruf_crc64_init();

                if(crc64_context == NULL)
                {
                    fprintf(stderr, "Could not initialize CRC64.\n");
                    free(buffer);
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                aaruf_crc64_update(crc64_context, buffer, readBytes);
                aaruf_crc64_final(crc64_context, &crc64);

                if(crc64 != ddtHeader.crc64)
                {
                    fprintf(stderr, "Expected DDT CRC 0x%16lX but got 0x%16lX.\n", ddtHeader.crc64, crc64);
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

            case None:
                buffer = malloc(ddtHeader.length);

                if(buffer == NULL)
                {
                    fprintf(stderr, "libaaruformat: Cannot allocate memory for deduplication table.\n");
                    break;
                }

                readBytes = fread(buffer, 1, ddtHeader.length, ctx->imageStream);

                if(readBytes != ddtHeader.length)
                {
                    free(buffer);
                    fprintf(stderr, "libaaruformat: Could not read deduplication table, continuing...\n");
                    break;
                }

                crc64_context = aaruf_crc64_init();

                if(crc64_context == NULL)
                {
                    fprintf(stderr, "Could not initialize CRC64.\n");
                    free(ctx->userDataDdt);
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                aaruf_crc64_update(crc64_context, buffer, readBytes);
                aaruf_crc64_final(crc64_context, &crc64);

                if(crc64 != ddtHeader.crc64)
                {
                    fprintf(stderr, "Expected DDT CRC 0x%16lX but got 0x%16lX.\n", ddtHeader.crc64, crc64);
                    free(ctx->userDataDdt);
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
                fprintf(stderr, "libaaruformat: Found unknown compression type %d, continuing...\n",
                        ddtHeader.compression);
                break;
        }
    }

    return AARUF_STATUS_OK;
}

int32_t decode_ddt_entry_v2(aaruformatContext *ctx, uint64_t sectorAddress, uint64_t *offset, uint64_t *blockOffset,
                            uint8_t *sectorStatus)
{
    uint64_t ddtEntry = 0;

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        fprintf(stderr, "Invalid context or image stream.\n");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // TODO: Implement multi-level tables
    if(ctx->userDataDdtHeader.tableShift != 0) return AARUF_ERROR_CANNOT_READ_BLOCK;

    // TODO: Take into account the negative and overflow blocks, library-wide
    sectorAddress += ctx->userDataDdtHeader.negative;

    if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
        ddtEntry = ctx->userDataDdtMini[sectorAddress];
    else if(ctx->userDataDdtHeader.sizeType == BigDdtSizeType)
        ddtEntry = ctx->userDataDdtBig[sectorAddress];
    else
    {
        fprintf(stderr, "libaaruformat: Unknown DDT size type %d.\n", ctx->userDataDdtHeader.sizeType);
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    if(ddtEntry == 0)
    {
        *sectorStatus = SectorStatusNotDumped;
        *offset       = 0;
        *blockOffset  = 0;
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
    *offset                   = (ddtEntry & offsetMask) * (1 << ctx->userDataDdtHeader.blockAlignmentShift);
    *blockOffset              = ddtEntry >> ctx->userDataDdtHeader.dataShift;

    return AARUF_STATUS_OK;
}