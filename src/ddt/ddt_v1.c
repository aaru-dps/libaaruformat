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

int32_t process_ddt_v1(aaruformatContext *ctx, IndexEntry *entry, bool *foundUserDataDdt)
{
    TRACE("Entering process_ddt_v1(%p, %p, %d)", ctx, entry, *foundUserDataDdt);

    int       pos       = 0;
    size_t    readBytes = 0;
    DdtHeader ddtHeader;
    uint8_t  *cmpData = NULL;
    uint32_t *cdDdt   = NULL;
    uint8_t   lzmaProperties[LZMA_PROPERTIES_LENGTH];
    size_t    lzmaSize = 0;
    int       errorNo  = 0;

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
    readBytes = fread(&ddtHeader, 1, sizeof(DdtHeader), ctx->imageStream);

    if(readBytes != sizeof(DdtHeader))
    {
        FATAL("Could not read block header at %" PRIu64 "", entry->offset);

        TRACE("Exiting process_ddt_v1() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    *foundUserDataDdt = true;

    ctx->imageInfo.ImageSize += ddtHeader.cmpLength;

    if(entry->dataType == UserData)
    {
        ctx->imageInfo.Sectors = ddtHeader.entries;
        ctx->shift             = ddtHeader.shift;
        ctx->ddtVersion        = 1;

        // Check for DDT compression
        switch(ddtHeader.compression)
        {
            // TODO: Check CRC
            case Lzma:
                lzmaSize = ddtHeader.cmpLength - LZMA_PROPERTIES_LENGTH;

                cmpData = (uint8_t *)malloc(lzmaSize);
                if(cmpData == NULL)
                {
                    TRACE("Cannot allocate memory for DDT, continuing...");
                    break;
                }

                ctx->userDataDdt = (uint64_t *)malloc(ddtHeader.length);
                if(ctx->userDataDdt == NULL)
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
                    free(ctx->userDataDdt);
                    ctx->userDataDdt = NULL;
                    break;
                }

                readBytes = fread(cmpData, 1, lzmaSize, ctx->imageStream);
                if(readBytes != lzmaSize)
                {
                    TRACE("Could not read compressed block, continuing...");
                    free(cmpData);
                    free(ctx->userDataDdt);
                    ctx->userDataDdt = NULL;
                    break;
                }

                readBytes = ddtHeader.length;
                TRACE("Decompressing block of size %zu bytes", ddtHeader.length);
                errorNo = aaruf_lzma_decode_buffer((uint8_t *)ctx->userDataDdt, &readBytes, cmpData, &lzmaSize,
                                                   lzmaProperties, LZMA_PROPERTIES_LENGTH);

                if(errorNo != 0)
                {
                    FATAL("Got error %d from LZMA, stopping...", errorNo);
                    free(cmpData);
                    free(ctx->userDataDdt);
                    ctx->userDataDdt = NULL;
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                if(readBytes != ddtHeader.length)
                {
                    FATAL("Error decompressing block, should be {0} bytes but got {1} bytes., stopping...");
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
                TRACE("Memory mapping deduplication table at position %" PRIu64, entry->offset + sizeof(ddtHeader));
                ctx->mappedMemoryDdtSize = sizeof(uint64_t) * ddtHeader.entries;
                ctx->userDataDdt = mmap(NULL, ctx->mappedMemoryDdtSize, PROT_READ, MAP_SHARED, fileno(ctx->imageStream),
                                        entry->offset + sizeof(ddtHeader));

                if(ctx->userDataDdt == MAP_FAILED)
                {
                    *foundUserDataDdt = false;
                    FATAL("Could not read map deduplication table.");
                    break;
                }

                ctx->inMemoryDdt = false;
                break;
#else  // TODO: Implement
                TRACE("Uncompressed DDT not yet implemented...");
                *foundUserDataDdt = false;
                break;
#endif
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
            // TODO: Check CRC
            case Lzma:
                lzmaSize = ddtHeader.cmpLength - LZMA_PROPERTIES_LENGTH;

                cmpData = (uint8_t *)malloc(lzmaSize);
                if(cmpData == NULL)
                {
                    TRACE("Cannot allocate memory for DDT, continuing...");
                    break;
                }

                cdDdt = (uint32_t *)malloc(ddtHeader.length);
                if(cdDdt == NULL)
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
                    free(cdDdt);
                    break;
                }

                readBytes = fread(cmpData, 1, lzmaSize, ctx->imageStream);
                if(readBytes != lzmaSize)
                {
                    TRACE("Could not read compressed block, continuing...");
                    free(cmpData);
                    free(cdDdt);
                    break;
                }

                readBytes = ddtHeader.length;
                TRACE("Decompressing block of size %zu bytes", ddtHeader.length);
                errorNo = aaruf_lzma_decode_buffer((uint8_t *)cdDdt, &readBytes, cmpData, &lzmaSize, lzmaProperties,
                                                   LZMA_PROPERTIES_LENGTH);

                if(errorNo != 0)
                {
                    FATAL("Got error %d from LZMA, stopping...", errorNo);
                    free(cmpData);
                    free(cdDdt);
                    return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
                }

                if(readBytes != ddtHeader.length)
                {
                    FATAL("Error decompressing block, should be {0} bytes but got {1} bytes., stopping...");
                    free(cmpData);
                    free(cdDdt);
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
                    TRACE("Cannot allocate memory for deduplication table.");
                    break;
                }

                readBytes = fread(cdDdt, 1, ddtHeader.entries * sizeof(uint32_t), ctx->imageStream);

                if(readBytes != ddtHeader.entries * sizeof(uint32_t))
                {
                    free(cdDdt);
                    TRACE("Could not read deduplication table, continuing...");
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
                TRACE("Found unknown compression type %d, continuing...", ddtHeader.compression);
                break;
        }
    }

    TRACE("Exiting process_ddt_v1() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

int32_t decode_ddt_entry_v1(aaruformatContext *ctx, uint64_t sectorAddress, uint64_t *offset, uint64_t *blockOffset,
                            uint8_t *sectorStatus)
{
    TRACE("Entering decode_ddt_entry_v1(%p, %" PRIu64 ", %llu, %llu, %d)", ctx, sectorAddress, *offset, *blockOffset,
          *sectorStatus);

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const uint64_t ddtEntry   = ctx->userDataDdt[sectorAddress];
    const uint32_t offsetMask = (uint32_t)((1 << ctx->shift) - 1);
    *offset                   = ddtEntry & offsetMask;
    *blockOffset              = ddtEntry >> ctx->shift;

    // Partially written image... as we can't know the real sector size just assume it's common :/
    if(ddtEntry == 0)
        *sectorStatus = SectorStatusNotDumped;
    else
        *sectorStatus = SectorStatusDumped;

    TRACE("Exiting decode_ddt_entry_v1(%p, %" PRIu64 ", %llu, %llu, %d) = AARUF_STATUS_OK", ctx, sectorAddress, *offset,
          *blockOffset, *sectorStatus);

    return AARUF_STATUS_OK;
}