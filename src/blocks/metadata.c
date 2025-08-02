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

// Process the metadata block found while opening an AaruFormat file
void process_metadata_block(aaruformatContext *ctx, const IndexEntry *entry)
{
    int    pos       = 0;
    size_t readBytes = 0;

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        fprintf(stderr, "Invalid context or image stream.\n");
        return;
    }

    // Seek to block
    pos = fseek(ctx->imageStream, entry->offset, SEEK_SET);
    if(pos < 0 || ftell(ctx->imageStream) != entry->offset)
    {
        fprintf(stderr, "libaaruformat: Could not seek to %" PRIu64 " as indicated by index entry...\n", entry->offset);

        return;
    }

    // Even if those two checks shall have been done before

    readBytes = fread(&ctx->metadataBlockHeader, 1, sizeof(MetadataBlockHeader), ctx->imageStream);

    if(readBytes != sizeof(MetadataBlockHeader))
    {
        memset(&ctx->metadataBlockHeader, 0, sizeof(MetadataBlockHeader));
        fprintf(stderr, "libaaruformat: Could not read metadata block header, continuing...\n");
        return;
    }

    if(ctx->metadataBlockHeader.identifier != entry->blockType)
    {
        memset(&ctx->metadataBlockHeader, 0, sizeof(MetadataBlockHeader));
        fprintf(stderr, "libaaruformat: Incorrect identifier for data block at position %" PRIu64 "\n", entry->offset);
        return;
    }

    ctx->imageInfo.ImageSize += ctx->metadataBlockHeader.blockSize;

    ctx->metadataBlock = (uint8_t *)malloc(ctx->metadataBlockHeader.blockSize);

    if(ctx->metadataBlock == NULL)
    {
        memset(&ctx->metadataBlockHeader, 0, sizeof(MetadataBlockHeader));
        fprintf(stderr, "libaaruformat: Could not allocate memory for metadata block, continuing...\n");
        return;
    }

    readBytes = fread(ctx->metadataBlock, 1, ctx->metadataBlockHeader.blockSize, ctx->imageStream);

    if(readBytes != ctx->metadataBlockHeader.blockSize)
    {
        memset(&ctx->metadataBlockHeader, 0, sizeof(MetadataBlockHeader));
        free(ctx->metadataBlock);
        fprintf(stderr, "libaaruformat: Could not read metadata block, continuing...\n");
    }

    if(ctx->metadataBlockHeader.mediaSequence > 0 && ctx->metadataBlockHeader.lastMediaSequence > 0)
    {
        ctx->imageInfo.MediaSequence     = ctx->metadataBlockHeader.mediaSequence;
        ctx->imageInfo.LastMediaSequence = ctx->metadataBlockHeader.lastMediaSequence;
        fprintf(stderr, "libaaruformat: Setting media sequence as %d of %d\n", ctx->imageInfo.MediaSequence,
                ctx->imageInfo.LastMediaSequence);
    }

    if(ctx->metadataBlockHeader.creatorLength > 0 &&
       ctx->metadataBlockHeader.creatorOffset + ctx->metadataBlockHeader.creatorLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.Creator = (uint8_t *)malloc(ctx->metadataBlockHeader.creatorLength);
        if(ctx->imageInfo.Creator != NULL)
        {
            memcpy(ctx->imageInfo.Creator, ctx->metadataBlock + ctx->metadataBlockHeader.creatorOffset,
                   ctx->metadataBlockHeader.creatorLength);
        }
    }

    if(ctx->metadataBlockHeader.commentsLength > 0 &&
       ctx->metadataBlockHeader.commentsOffset + ctx->metadataBlockHeader.commentsLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.Comments = (uint8_t *)malloc(ctx->metadataBlockHeader.commentsLength);
        if(ctx->imageInfo.Comments != NULL)
        {
            memcpy(ctx->imageInfo.Comments, ctx->metadataBlock + ctx->metadataBlockHeader.commentsOffset,
                   ctx->metadataBlockHeader.commentsLength);
        }
    }

    if(ctx->metadataBlockHeader.mediaTitleLength > 0 &&
       ctx->metadataBlockHeader.mediaTitleOffset + ctx->metadataBlockHeader.mediaTitleLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.MediaTitle = (uint8_t *)malloc(ctx->metadataBlockHeader.mediaTitleLength);
        if(ctx->imageInfo.MediaTitle != NULL)
        {
            memcpy(ctx->imageInfo.MediaTitle, ctx->metadataBlock + ctx->metadataBlockHeader.mediaTitleOffset,
                   ctx->metadataBlockHeader.mediaTitleLength);
        }
    }

    if(ctx->metadataBlockHeader.mediaManufacturerLength > 0 &&
       ctx->metadataBlockHeader.mediaManufacturerOffset + ctx->metadataBlockHeader.mediaManufacturerLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.MediaManufacturer = (uint8_t *)malloc(ctx->metadataBlockHeader.mediaManufacturerLength);
        if(ctx->imageInfo.MediaManufacturer != NULL)
        {
            memcpy(ctx->imageInfo.MediaManufacturer,
                   ctx->metadataBlock + ctx->metadataBlockHeader.mediaManufacturerOffset,
                   ctx->metadataBlockHeader.mediaManufacturerLength);
        }
    }

    if(ctx->metadataBlockHeader.mediaModelLength > 0 &&
       ctx->metadataBlockHeader.mediaModelOffset + ctx->metadataBlockHeader.mediaModelLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.MediaModel = (uint8_t *)malloc(ctx->metadataBlockHeader.mediaModelOffset);
        if(ctx->imageInfo.MediaModel != NULL)
        {
            memcpy(ctx->imageInfo.MediaModel, ctx->metadataBlock + ctx->metadataBlockHeader.mediaModelOffset,
                   ctx->metadataBlockHeader.mediaModelLength);
        }
    }

    if(ctx->metadataBlockHeader.mediaSerialNumberLength > 0 &&
       ctx->metadataBlockHeader.mediaSerialNumberOffset + ctx->metadataBlockHeader.mediaSerialNumberLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.MediaSerialNumber = (uint8_t *)malloc(ctx->metadataBlockHeader.mediaSerialNumberLength);
        if(ctx->imageInfo.MediaSerialNumber != NULL)
        {
            memcpy(ctx->imageInfo.MediaSerialNumber,
                   ctx->metadataBlock + ctx->metadataBlockHeader.mediaSerialNumberOffset,
                   ctx->metadataBlockHeader.mediaManufacturerLength);
        }
    }

    if(ctx->metadataBlockHeader.mediaBarcodeLength > 0 &&
       ctx->metadataBlockHeader.mediaBarcodeOffset + ctx->metadataBlockHeader.mediaBarcodeLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.MediaBarcode = (uint8_t *)malloc(ctx->metadataBlockHeader.mediaBarcodeLength);
        if(ctx->imageInfo.MediaBarcode != NULL)
        {
            memcpy(ctx->imageInfo.MediaBarcode, ctx->metadataBlock + ctx->metadataBlockHeader.mediaBarcodeOffset,
                   ctx->metadataBlockHeader.mediaBarcodeLength);
        }
    }

    if(ctx->metadataBlockHeader.mediaPartNumberLength > 0 &&
       ctx->metadataBlockHeader.mediaPartNumberOffset + ctx->metadataBlockHeader.mediaPartNumberLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.MediaPartNumber = (uint8_t *)malloc(ctx->metadataBlockHeader.mediaPartNumberLength);
        if(ctx->imageInfo.MediaPartNumber != NULL)
        {
            memcpy(ctx->imageInfo.MediaPartNumber, ctx->metadataBlock + ctx->metadataBlockHeader.mediaPartNumberOffset,
                   ctx->metadataBlockHeader.mediaPartNumberLength);
        }
    }

    if(ctx->metadataBlockHeader.driveManufacturerLength > 0 &&
       ctx->metadataBlockHeader.driveManufacturerOffset + ctx->metadataBlockHeader.driveManufacturerLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.DriveManufacturer = (uint8_t *)malloc(ctx->metadataBlockHeader.driveManufacturerLength);
        if(ctx->imageInfo.DriveManufacturer != NULL)
        {
            memcpy(ctx->imageInfo.DriveManufacturer,
                   ctx->metadataBlock + ctx->metadataBlockHeader.driveManufacturerOffset,
                   ctx->metadataBlockHeader.driveManufacturerLength);
        }
    }

    if(ctx->metadataBlockHeader.driveModelLength > 0 &&
       ctx->metadataBlockHeader.driveModelOffset + ctx->metadataBlockHeader.driveModelLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.DriveModel = (uint8_t *)malloc(ctx->metadataBlockHeader.driveModelLength);
        if(ctx->imageInfo.DriveModel != NULL)
        {
            memcpy(ctx->imageInfo.DriveModel, ctx->metadataBlock + ctx->metadataBlockHeader.driveModelOffset,
                   ctx->metadataBlockHeader.driveModelLength);
        }
    }

    if(ctx->metadataBlockHeader.driveSerialNumberLength > 0 &&
       ctx->metadataBlockHeader.driveSerialNumberOffset + ctx->metadataBlockHeader.driveSerialNumberLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.DriveSerialNumber = (uint8_t *)malloc(ctx->metadataBlockHeader.driveSerialNumberLength);
        if(ctx->imageInfo.DriveSerialNumber != NULL)
        {
            memcpy(ctx->imageInfo.DriveSerialNumber,
                   ctx->metadataBlock + ctx->metadataBlockHeader.driveSerialNumberOffset,
                   ctx->metadataBlockHeader.driveSerialNumberLength);
        }
    }

    if(ctx->metadataBlockHeader.driveManufacturerLength > 0 &&
       ctx->metadataBlockHeader.driveFirmwareRevisionOffset + ctx->metadataBlockHeader.driveManufacturerLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.DriveFirmwareRevision = (uint8_t *)malloc(ctx->metadataBlockHeader.driveFirmwareRevisionLength);
        if(ctx->imageInfo.DriveFirmwareRevision != NULL)
        {
            memcpy(ctx->imageInfo.DriveFirmwareRevision,
                   ctx->metadataBlock + ctx->metadataBlockHeader.driveFirmwareRevisionLength,
                   ctx->metadataBlockHeader.driveFirmwareRevisionLength);
        }
    }
}

// Logical geometry block. It doesn't have a CRC coz, well, it's not so important
void process_geometry_block(aaruformatContext *ctx, const IndexEntry *entry)
{
    size_t readBytes = 0;

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        fprintf(stderr, "Invalid context or image stream.\n");
        return;
    }

    // Seek to block
    if(fseek(ctx->imageStream, entry->offset, SEEK_SET) != 0)
    {
        fprintf(stderr, "libaaruformat: Could not seek to %" PRIu64 " as indicated by index entry...\n", entry->offset);
        return;
    }

    readBytes = fread(&ctx->geometryBlock, 1, sizeof(GeometryBlockHeader), ctx->imageStream);

    if(readBytes != sizeof(GeometryBlockHeader))
    {
        memset(&ctx->geometryBlock, 0, sizeof(GeometryBlockHeader));
        fprintf(stderr, "libaaruformat: Could not read geometry block header, continuing...\n");
        return;
    }

    if(ctx->geometryBlock.identifier != GeometryBlock)
    {
        memset(&ctx->geometryBlock, 0, sizeof(GeometryBlockHeader));
        fprintf(stderr, "libaaruformat: Incorrect identifier for geometry block at position %" PRIu64 "\n",
                entry->offset);
        return;
    }

    ctx->imageInfo.ImageSize += sizeof(GeometryBlockHeader);

    fprintf(stderr, "libaaruformat: Geometry set to %d cylinders %d heads %d sectors per track\n",
            ctx->geometryBlock.cylinders, ctx->geometryBlock.heads, ctx->geometryBlock.sectorsPerTrack);

    ctx->imageInfo.Cylinders       = ctx->geometryBlock.cylinders;
    ctx->imageInfo.Heads           = ctx->geometryBlock.heads;
    ctx->imageInfo.SectorsPerTrack = ctx->geometryBlock.sectorsPerTrack;
}

// CICM XML metadata block
void process_cicm_block(aaruformatContext *ctx, const IndexEntry *entry)
{
    int    pos       = 0;
    size_t readBytes = 0;

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        fprintf(stderr, "Invalid context or image stream.\n");
        return;
    }

    // Seek to block
    pos = fseek(ctx->imageStream, entry->offset, SEEK_SET);
    if(pos < 0 || ftell(ctx->imageStream) != entry->offset)
    {
        fprintf(stderr, "libaaruformat: Could not seek to %" PRIu64 " as indicated by index entry...\n", entry->offset);

        return;
    }

    // Even if those two checks shall have been done before

    readBytes = fread(&ctx->cicmBlockHeader, 1, sizeof(CicmMetadataBlock), ctx->imageStream);

    if(readBytes != sizeof(CicmMetadataBlock))
    {
        memset(&ctx->cicmBlockHeader, 0, sizeof(CicmMetadataBlock));
        fprintf(stderr, "libaaruformat: Could not read CICM XML metadata header, continuing...\n");
        return;
    }

    if(ctx->cicmBlockHeader.identifier != CicmBlock)
    {
        memset(&ctx->cicmBlockHeader, 0, sizeof(CicmMetadataBlock));
        fprintf(stderr, "libaaruformat: Incorrect identifier for data block at position %" PRIu64 "\n", entry->offset);
    }

    ctx->imageInfo.ImageSize += ctx->cicmBlockHeader.length;

    ctx->cicmBlock = (uint8_t *)malloc(ctx->cicmBlockHeader.length);

    if(ctx->cicmBlock == NULL)
    {
        memset(&ctx->cicmBlockHeader, 0, sizeof(CicmMetadataBlock));
        fprintf(stderr, "libaaruformat: Could not allocate memory for CICM XML metadata block, continuing...\n");
        return;
    }

    readBytes = fread(ctx->cicmBlock, 1, ctx->cicmBlockHeader.length, ctx->imageStream);

    if(readBytes != ctx->metadataBlockHeader.blockSize)
    {
        memset(&ctx->cicmBlockHeader, 0, sizeof(CicmMetadataBlock));
        free(ctx->cicmBlock);
        fprintf(stderr, "libaaruformat: Could not read CICM XML metadata block, continuing...\n");
    }

    fprintf(stderr, "libaaruformat: Found CICM XML metadata block %" PRIu64 ".\n", entry->offset);
}