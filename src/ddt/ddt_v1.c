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

int32_t process_ddt_v1(aaruformatContext *ctx, IndexEntry *entry, bool *foundUserDataDdt)
{
    int         pos       = 0;
    size_t      readBytes = 0;
    DdtHeader   ddtHeader;
    uint8_t    *cmpData = NULL;
    uint32_t   *cdDdt   = NULL;
    uint8_t     lzmaProperties[LZMA_PROPERTIES_LENGTH];
    size_t      lzmaSize = 0;
    int         errorNo  = 0;
    BlockHeader blockHeader;

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

    readBytes = fread(&ddtHeader, 1, sizeof(DdtHeader), ctx->imageStream);

    if(readBytes != sizeof(DdtHeader))
    {
        fprintf(stderr, "libaaruformat: Could not read block header at %" PRIu64 "\n", entry->offset);

        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    *foundUserDataDdt = true;

    ctx->imageInfo.ImageSize += ddtHeader.cmpLength;

    if(entry->dataType == UserData)
    {
        ctx->imageInfo.Sectors = ddtHeader.entries;
        ctx->shift             = ddtHeader.shift;

        // Check for DDT compression
        switch(ddtHeader.compression)
        {
                // TODO: Check CRC
            case Lzma:
                lzmaSize = ddtHeader.cmpLength - LZMA_PROPERTIES_LENGTH;

                cmpData = (uint8_t *)malloc(lzmaSize);
                if(cmpData == NULL)
                {
                    fprintf(stderr, "Cannot allocate memory for DDT, continuing...\n");
                    break;
                }

                ctx->userDataDdt = (uint64_t *)malloc(ddtHeader.length);
                if(ctx->userDataDdt == NULL)
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
                    free(ctx->userDataDdt);
                    ctx->userDataDdt = NULL;
                    break;
                }

                readBytes = fread(cmpData, 1, lzmaSize, ctx->imageStream);
                if(readBytes != lzmaSize)
                {
                    fprintf(stderr, "Could not read compressed block, continuing...\n");
                    free(cmpData);
                    free(ctx->userDataDdt);
                    ctx->userDataDdt = NULL;
                    break;
                }

                readBytes = ddtHeader.length;
                errorNo   = aaruf_lzma_decode_buffer((uint8_t *)ctx->userDataDdt, &readBytes, cmpData, &lzmaSize,
                                                     lzmaProperties, LZMA_PROPERTIES_LENGTH);

                if(errorNo != 0)
                {
                    fprintf(stderr, "Got error %d from LZMA, stopping...\n", errorNo);
                    free(cmpData);
                    free(ctx->userDataDdt);
                    ctx->userDataDdt = NULL;
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                if(readBytes != ddtHeader.length)
                {
                    fprintf(stderr, "Error decompressing block, should be {0} bytes but got {1} bytes., stopping...\n");
                    free(cmpData);
                    free(ctx->userDataDdt);
                    ctx->userDataDdt = NULL;
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                ctx->inMemoryDdt  = true;
                *foundUserDataDdt = true;

                break;
                // TODO: Check CRC
            case None:
#ifdef __linux__
                ctx->mappedMemoryDdtSize = sizeof(uint64_t) * ddtHeader.entries;
                ctx->userDataDdt = mmap(NULL, ctx->mappedMemoryDdtSize, PROT_READ, MAP_SHARED, fileno(ctx->imageStream),
                                        entry->offset + sizeof(ddtHeader));

                if(ctx->userDataDdt == MAP_FAILED)
                {
                    *foundUserDataDdt = false;
                    fprintf(stderr, "libaaruformat: Could not read map deduplication table.\n");
                    break;
                }

                ctx->inMemoryDdt = false;
                break;
#else  // TODO: Implement
                fprintf(stderr, "libaaruformat: Uncompressed DDT not yet implemented...\n");
                *foundUserDataDdt = false;
                break;
#endif
            default:
                fprintf(stderr, "libaaruformat: Found unknown compression type %d, continuing...\n",
                        blockHeader.compression);
                *foundUserDataDdt = false;
                break;
        }
    }
    else if(entry->dataType == CdSectorPrefixCorrected || entry->dataType == CdSectorSuffixCorrected)
    {
        switch(ddtHeader.compression)
        {
                // TODO: Check CRC
            case Lzma:
                lzmaSize = ddtHeader.cmpLength - LZMA_PROPERTIES_LENGTH;

                cmpData = (uint8_t *)malloc(lzmaSize);
                if(cmpData == NULL)
                {
                    fprintf(stderr, "Cannot allocate memory for DDT, continuing...\n");
                    break;
                }

                cdDdt = (uint32_t *)malloc(ddtHeader.length);
                if(cdDdt == NULL)
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
                    free(cdDdt);
                    ctx->userDataDdt = NULL;
                    break;
                }

                readBytes = fread(cmpData, 1, lzmaSize, ctx->imageStream);
                if(readBytes != lzmaSize)
                {
                    fprintf(stderr, "Could not read compressed block, continuing...\n");
                    free(cmpData);
                    free(cdDdt);
                    ctx->userDataDdt = NULL;
                    break;
                }

                readBytes = ddtHeader.length;
                errorNo   = aaruf_lzma_decode_buffer((uint8_t *)cdDdt, &readBytes, cmpData, &lzmaSize, lzmaProperties,
                                                     LZMA_PROPERTIES_LENGTH);

                if(errorNo != 0)
                {
                    fprintf(stderr, "Got error %d from LZMA, stopping...\n", errorNo);
                    free(cmpData);
                    free(cdDdt);
                    ctx->userDataDdt = NULL;
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                if(readBytes != ddtHeader.length)
                {
                    fprintf(stderr, "Error decompressing block, should be {0} bytes but got {1} bytes., stopping...\n");
                    free(cmpData);
                    free(cdDdt);
                    ctx->userDataDdt = NULL;
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                if(entry->dataType == CdSectorPrefixCorrected)
                    ctx->sectorPrefixDdt = cdDdt;
                else if(entry->dataType == CdSectorSuffixCorrected)
                    ctx->sectorSuffixDdt = cdDdt;
                else
                    free(cdDdt);

                break;

                // TODO: Check CRC
            case None:
                cdDdt = (uint32_t *)malloc(ddtHeader.entries * sizeof(uint32_t));

                if(cdDdt == NULL)
                {
                    fprintf(stderr, "libaaruformat: Cannot allocate memory for deduplication table.\n");
                    break;
                }

                readBytes = fread(cdDdt, 1, ddtHeader.entries * sizeof(uint32_t), ctx->imageStream);

                if(readBytes != ddtHeader.entries * sizeof(uint32_t))
                {
                    free(cdDdt);
                    fprintf(stderr, "libaaruformat: Could not read deduplication table, continuing...\n");
                    break;
                }

                if(entry->dataType == CdSectorPrefixCorrected)
                    ctx->sectorPrefixDdt = cdDdt;
                else if(entry->dataType == CdSectorSuffixCorrected)
                    ctx->sectorSuffixDdt = cdDdt;
                else
                    free(cdDdt);

                break;
            default:
                fprintf(stderr, "libaaruformat: Found unknown compression type %d, continuing...\n",
                        blockHeader.compression);
                break;
        }
    }

    return AARUF_STATUS_OK;
}