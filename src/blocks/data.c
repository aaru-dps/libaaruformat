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
#include "log.h"
#include "uthash.h"

// Process data blocks found while opening an AaruFormat file
int32_t process_data_block(aaruformatContext *ctx, IndexEntry *entry)
{
    TRACE("Entering process_data_block(%p, %p)", ctx, entry);
    BlockHeader    blockHeader;
    int            pos         = 0;
    size_t         readBytes   = 0;
    size_t         lzmaSize    = 0;
    uint8_t       *cmpData     = NULL;
    uint8_t       *cstData     = NULL;
    mediaTagEntry *oldMediaTag = NULL;
    mediaTagEntry *mediaTag    = NULL;
    uint8_t       *data        = NULL;
    int            errorNo     = 0;
    uint8_t        lzmaProperties[LZMA_PROPERTIES_LENGTH];
    uint64_t       crc64 = 0;

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Seek to block
    pos = fseek(ctx->imageStream, entry->offset, SEEK_SET);
    if(pos < 0 || ftell(ctx->imageStream) != entry->offset)
    {
        FATAL("Could not seek to %" PRIu64 " as indicated by index entry...", entry->offset);

        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    // Even if those two checks shall have been done before

    // NOP block, skip
    TRACE("NoData block found, exiting");
    TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
    if(entry->dataType == NoData) return AARUF_STATUS_OK;

    TRACE("Reading block header at position %" PRIu64, entry->offset);
    readBytes = fread(&blockHeader, 1, sizeof(BlockHeader), ctx->imageStream);

    if(readBytes != sizeof(BlockHeader))
    {
        FATAL("Could not read block header at %" PRIu64, entry->offset);

        TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    ctx->imageInfo.ImageSize += blockHeader.cmpLength;

    // Unused, skip
    if(entry->dataType == UserData)
    {
        if(blockHeader.sectorSize > ctx->imageInfo.SectorSize)
        {
            TRACE("Setting sector size to %" PRIu64 " bytes", blockHeader.sectorSize);
            ctx->imageInfo.SectorSize = blockHeader.sectorSize;
        }

        TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    if(blockHeader.identifier != entry->blockType)
    {
        TRACE("Incorrect identifier for data block at position %" PRIu64, entry->offset);

        TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    if(blockHeader.type != entry->dataType)
    {
        TRACE("Expected block with data type %4.4s at position %" PRIu64 " but found data type %4.4s",
              (char *)&entry->blockType, entry->offset, (char *)&blockHeader.type);

        TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    TRACE("Found data block with type %4.4s at position %" PRIu64, (char *)&entry->blockType, entry->offset);

    if(blockHeader.compression == Lzma || blockHeader.compression == LzmaClauniaSubchannelTransform)
    {
        if(blockHeader.compression == LzmaClauniaSubchannelTransform && blockHeader.type != CdSectorSubchannel)
        {
            TRACE("Invalid compression type %d for block with data type %d, continuing...", blockHeader.compression,
                  blockHeader.type);

            TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        }

        lzmaSize = blockHeader.cmpLength - LZMA_PROPERTIES_LENGTH;

        cmpData = (uint8_t *)malloc(lzmaSize);
        if(cmpData == NULL)
        {
            TRACE("Cannot allocate memory for block, continuing...");

            TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        }

        data = (uint8_t *)malloc(blockHeader.length);
        if(data == NULL)
        {
            TRACE("Cannot allocate memory for block, continuing...");
            free(cmpData);

            TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        }

        readBytes = fread(lzmaProperties, 1, LZMA_PROPERTIES_LENGTH, ctx->imageStream);
        if(readBytes != LZMA_PROPERTIES_LENGTH)
        {
            TRACE("Could not read LZMA properties, continuing...");
            free(cmpData);
            free(data);

            TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        }

        readBytes = fread(cmpData, 1, lzmaSize, ctx->imageStream);
        if(readBytes != lzmaSize)
        {
            TRACE("Could not read compressed block, continuing...");
            free(cmpData);
            free(data);

            TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        }

        readBytes = blockHeader.length;
        errorNo =
            aaruf_lzma_decode_buffer(data, &readBytes, cmpData, &lzmaSize, lzmaProperties, LZMA_PROPERTIES_LENGTH);

        if(errorNo != 0)
        {
            TRACE("Got error %d from LZMA, continuing...", errorNo);
            free(cmpData);
            free(data);

            TRACE("Exiting process_data_block() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
            return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
        }

        if(readBytes != blockHeader.length)
        {
            TRACE("Error decompressing block, should be {0} bytes but got {1} bytes., continuing...");
            free(cmpData);
            free(data);

            TRACE("Exiting process_data_block() = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK");
            return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
        }

        if(blockHeader.compression == LzmaClauniaSubchannelTransform)
        {
            cstData = malloc(blockHeader.length);
            if(cstData == NULL)
            {
                TRACE("Cannot allocate memory for block, continuing...");
                free(cmpData);
                free(data);

                TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
                return AARUF_STATUS_OK;
            }

            aaruf_cst_untransform(data, cstData, blockHeader.length);
            free(data);
            data    = cstData;
            cstData = NULL;
        }

        free(cmpData);
    }
    else if(blockHeader.compression == None)
    {
        data = (uint8_t *)malloc(blockHeader.length);
        if(data == NULL)
        {
            fprintf(stderr, "Cannot allocate memory for block, continuing...");

            TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        }

        readBytes = fread(data, 1, blockHeader.length, ctx->imageStream);

        if(readBytes != blockHeader.length)
        {
            free(data);
            fprintf(stderr, "Could not read block, continuing...");

            TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        }
    }
    else
    {
        TRACE("Found unknown compression type %d, continuing...", blockHeader.compression);

        TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    if(blockHeader.length > 0)
    {
        crc64 = aaruf_crc64_data(data, blockHeader.length);

        // Due to how C# wrote it, it is effectively reversed
        if(ctx->header.imageMajorVersion <= AARUF_VERSION_V1) crc64 = bswap_64(crc64);

        if(crc64 != blockHeader.crc64)
        {
            TRACE("Incorrect CRC found: 0x%" PRIx64 " found, expected 0x%" PRIx64 ", continuing...", crc64,
                  blockHeader.crc64);

            TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        }
    }

    // Check if it's not a media tag, but a sector tag, and fill the appropriate table then
    switch(entry->dataType)
    {
        case CdSectorPrefix:
        case CdSectorPrefixCorrected:
            if(entry->dataType == CdSectorPrefixCorrected) { ctx->sectorPrefixCorrected = data; }
            else
                ctx->sectorPrefix = data;

            ctx->readableSectorTags[CdSectorSync]   = true;
            ctx->readableSectorTags[CdSectorHeader] = true;

            break;
        case CdSectorSuffix:
        case CdSectorSuffixCorrected:
            if(entry->dataType == CdSectorSuffixCorrected)
                ctx->sectorSuffixCorrected = data;
            else
                ctx->sectorSuffix = data;

            ctx->readableSectorTags[CdSectorSubHeader] = true;
            ctx->readableSectorTags[CdSectorEcc]       = true;
            ctx->readableSectorTags[CdSectorEccP]      = true;
            ctx->readableSectorTags[CdSectorEccQ]      = true;
            ctx->readableSectorTags[CdSectorEdc]       = true;
            break;
        case CdSectorSubchannel:
            ctx->sectorSubchannel                       = data;
            ctx->readableSectorTags[CdSectorSubchannel] = true;
            break;
        case AppleProfileTag:
        case AppleSonyTag:
        case PriamDataTowerTag:
            ctx->sectorSubchannel                   = data;
            ctx->readableSectorTags[AppleSectorTag] = true;
            break;
        case CompactDiscMode2Subheader:
            ctx->mode2Subheaders = data;
            break;
        default:
            mediaTag = (mediaTagEntry *)malloc(sizeof(mediaTagEntry));

            if(mediaTag == NULL)
            {
                TRACE("Cannot allocate memory for media tag entry.");
                break;
            }
            memset(mediaTag, 0, sizeof(mediaTagEntry));

            mediaTag->type   = aaruf_get_media_tag_type_for_datatype(blockHeader.type);
            mediaTag->data   = data;
            mediaTag->length = blockHeader.length;

            HASH_REPLACE_INT(ctx->mediaTags, type, mediaTag, oldMediaTag);

            if(oldMediaTag != NULL)
            {
                TRACE("Replaced media tag with type %d", oldMediaTag->type);
                free(oldMediaTag->data);
                free(oldMediaTag);
                oldMediaTag = NULL;
            }

            break;
    }

    TRACE("Exiting process_data_block() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}