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

/**
 * @brief Processes a metadata block from the image stream.
 *
 * Reads a metadata block from the image and updates the context with its contents.
 *
 * @param ctx Pointer to the aaruformat context.
 * @param entry Pointer to the index entry describing the metadata block.
 */
void process_metadata_block(aaruformatContext *ctx, const IndexEntry *entry)
{
    TRACE("Entering process_metadata_block(%p, %p)", ctx, entry);
    int    pos        = 0;
    size_t read_bytes = 0;

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");
        TRACE("Exiting process_metadata_block()");
        return;
    }

    // Seek to block
    TRACE("Seeking to metadata block at position %" PRIu64, entry->offset);
    pos = fseek(ctx->imageStream, entry->offset, SEEK_SET);
    if(pos < 0 || ftell(ctx->imageStream) != entry->offset)
    {
        FATAL("Could not seek to %" PRIu64 " as indicated by index entry...", entry->offset);

        TRACE("Exiting process_metadata_block()");
        return;
    }

    // Even if those two checks shall have been done before
    TRACE("Reading metadata block header at position %" PRIu64, entry->offset);
    read_bytes = fread(&ctx->metadataBlockHeader, 1, sizeof(MetadataBlockHeader), ctx->imageStream);

    if(read_bytes != sizeof(MetadataBlockHeader))
    {
        memset(&ctx->metadataBlockHeader, 0, sizeof(MetadataBlockHeader));
        FATAL("Could not read metadata block header, continuing...");

        TRACE("Exiting process_metadata_block()");
        return;
    }

    if(ctx->metadataBlockHeader.identifier != entry->blockType)
    {
        memset(&ctx->metadataBlockHeader, 0, sizeof(MetadataBlockHeader));
        TRACE("Incorrect identifier for data block at position %" PRIu64 "", entry->offset);

        TRACE("Exiting process_metadata_block()");
        return;
    }

    ctx->imageInfo.ImageSize += ctx->metadataBlockHeader.blockSize;

    ctx->metadataBlock = (uint8_t *)malloc(ctx->metadataBlockHeader.blockSize);

    if(ctx->metadataBlock == NULL)
    {
        memset(&ctx->metadataBlockHeader, 0, sizeof(MetadataBlockHeader));
        FATAL("Could not allocate memory for metadata block, continuing...");

        TRACE("Exiting process_metadata_block()");
        return;
    }

    TRACE("Reading metadata block of size %u at position %" PRIu64, ctx->metadataBlockHeader.blockSize,
          entry->offset + sizeof(MetadataBlockHeader));
    read_bytes = fread(ctx->metadataBlock, 1, ctx->metadataBlockHeader.blockSize, ctx->imageStream);

    if(read_bytes != ctx->metadataBlockHeader.blockSize)
    {
        memset(&ctx->metadataBlockHeader, 0, sizeof(MetadataBlockHeader));
        free(ctx->metadataBlock);
        FATAL("Could not read metadata block, continuing...");
    }

    if(ctx->metadataBlockHeader.mediaSequence > 0 && ctx->metadataBlockHeader.lastMediaSequence > 0)
    {
        ctx->imageInfo.MediaSequence     = ctx->metadataBlockHeader.mediaSequence;
        ctx->imageInfo.LastMediaSequence = ctx->metadataBlockHeader.lastMediaSequence;
        TRACE("Setting media sequence as %d of %d", ctx->imageInfo.MediaSequence, ctx->imageInfo.LastMediaSequence);
    }

    if(ctx->metadataBlockHeader.creatorLength > 0 &&
       ctx->metadataBlockHeader.creatorOffset + ctx->metadataBlockHeader.creatorLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.Creator = (uint8_t *)malloc(ctx->metadataBlockHeader.creatorLength);
        if(ctx->imageInfo.Creator != NULL)
            memcpy(ctx->imageInfo.Creator, ctx->metadataBlock + ctx->metadataBlockHeader.creatorOffset,
                   ctx->metadataBlockHeader.creatorLength);
    }

    if(ctx->metadataBlockHeader.commentsLength > 0 &&
       ctx->metadataBlockHeader.commentsOffset + ctx->metadataBlockHeader.commentsLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.Comments = (uint8_t *)malloc(ctx->metadataBlockHeader.commentsLength);
        if(ctx->imageInfo.Comments != NULL)
            memcpy(ctx->imageInfo.Comments, ctx->metadataBlock + ctx->metadataBlockHeader.commentsOffset,
                   ctx->metadataBlockHeader.commentsLength);
    }

    if(ctx->metadataBlockHeader.mediaTitleLength > 0 &&
       ctx->metadataBlockHeader.mediaTitleOffset + ctx->metadataBlockHeader.mediaTitleLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.MediaTitle = (uint8_t *)malloc(ctx->metadataBlockHeader.mediaTitleLength);
        if(ctx->imageInfo.MediaTitle != NULL)
            memcpy(ctx->imageInfo.MediaTitle, ctx->metadataBlock + ctx->metadataBlockHeader.mediaTitleOffset,
                   ctx->metadataBlockHeader.mediaTitleLength);
    }

    if(ctx->metadataBlockHeader.mediaManufacturerLength > 0 &&
       ctx->metadataBlockHeader.mediaManufacturerOffset + ctx->metadataBlockHeader.mediaManufacturerLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.MediaManufacturer = (uint8_t *)malloc(ctx->metadataBlockHeader.mediaManufacturerLength);
        if(ctx->imageInfo.MediaManufacturer != NULL)
            memcpy(ctx->imageInfo.MediaManufacturer,
                   ctx->metadataBlock + ctx->metadataBlockHeader.mediaManufacturerOffset,
                   ctx->metadataBlockHeader.mediaManufacturerLength);
    }

    if(ctx->metadataBlockHeader.mediaModelLength > 0 &&
       ctx->metadataBlockHeader.mediaModelOffset + ctx->metadataBlockHeader.mediaModelLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.MediaModel = (uint8_t *)malloc(ctx->metadataBlockHeader.mediaModelOffset);
        if(ctx->imageInfo.MediaModel != NULL)
            memcpy(ctx->imageInfo.MediaModel, ctx->metadataBlock + ctx->metadataBlockHeader.mediaModelOffset,
                   ctx->metadataBlockHeader.mediaModelLength);
    }

    if(ctx->metadataBlockHeader.mediaSerialNumberLength > 0 &&
       ctx->metadataBlockHeader.mediaSerialNumberOffset + ctx->metadataBlockHeader.mediaSerialNumberLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.MediaSerialNumber = (uint8_t *)malloc(ctx->metadataBlockHeader.mediaSerialNumberLength);
        if(ctx->imageInfo.MediaSerialNumber != NULL)
            memcpy(ctx->imageInfo.MediaSerialNumber,
                   ctx->metadataBlock + ctx->metadataBlockHeader.mediaSerialNumberOffset,
                   ctx->metadataBlockHeader.mediaManufacturerLength);
    }

    if(ctx->metadataBlockHeader.mediaBarcodeLength > 0 &&
       ctx->metadataBlockHeader.mediaBarcodeOffset + ctx->metadataBlockHeader.mediaBarcodeLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.MediaBarcode = (uint8_t *)malloc(ctx->metadataBlockHeader.mediaBarcodeLength);
        if(ctx->imageInfo.MediaBarcode != NULL)
            memcpy(ctx->imageInfo.MediaBarcode, ctx->metadataBlock + ctx->metadataBlockHeader.mediaBarcodeOffset,
                   ctx->metadataBlockHeader.mediaBarcodeLength);
    }

    if(ctx->metadataBlockHeader.mediaPartNumberLength > 0 &&
       ctx->metadataBlockHeader.mediaPartNumberOffset + ctx->metadataBlockHeader.mediaPartNumberLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.MediaPartNumber = (uint8_t *)malloc(ctx->metadataBlockHeader.mediaPartNumberLength);
        if(ctx->imageInfo.MediaPartNumber != NULL)
            memcpy(ctx->imageInfo.MediaPartNumber, ctx->metadataBlock + ctx->metadataBlockHeader.mediaPartNumberOffset,
                   ctx->metadataBlockHeader.mediaPartNumberLength);
    }

    if(ctx->metadataBlockHeader.driveManufacturerLength > 0 &&
       ctx->metadataBlockHeader.driveManufacturerOffset + ctx->metadataBlockHeader.driveManufacturerLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.DriveManufacturer = (uint8_t *)malloc(ctx->metadataBlockHeader.driveManufacturerLength);
        if(ctx->imageInfo.DriveManufacturer != NULL)
            memcpy(ctx->imageInfo.DriveManufacturer,
                   ctx->metadataBlock + ctx->metadataBlockHeader.driveManufacturerOffset,
                   ctx->metadataBlockHeader.driveManufacturerLength);
    }

    if(ctx->metadataBlockHeader.driveModelLength > 0 &&
       ctx->metadataBlockHeader.driveModelOffset + ctx->metadataBlockHeader.driveModelLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.DriveModel = (uint8_t *)malloc(ctx->metadataBlockHeader.driveModelLength);
        if(ctx->imageInfo.DriveModel != NULL)
            memcpy(ctx->imageInfo.DriveModel, ctx->metadataBlock + ctx->metadataBlockHeader.driveModelOffset,
                   ctx->metadataBlockHeader.driveModelLength);
    }

    if(ctx->metadataBlockHeader.driveSerialNumberLength > 0 &&
       ctx->metadataBlockHeader.driveSerialNumberOffset + ctx->metadataBlockHeader.driveSerialNumberLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.DriveSerialNumber = (uint8_t *)malloc(ctx->metadataBlockHeader.driveSerialNumberLength);
        if(ctx->imageInfo.DriveSerialNumber != NULL)
            memcpy(ctx->imageInfo.DriveSerialNumber,
                   ctx->metadataBlock + ctx->metadataBlockHeader.driveSerialNumberOffset,
                   ctx->metadataBlockHeader.driveSerialNumberLength);
    }

    if(ctx->metadataBlockHeader.driveFirmwareRevisionLength > 0 &&
       ctx->metadataBlockHeader.driveFirmwareRevisionOffset + ctx->metadataBlockHeader.driveFirmwareRevisionLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->imageInfo.DriveFirmwareRevision = (uint8_t *)malloc(ctx->metadataBlockHeader.driveFirmwareRevisionLength);
        if(ctx->imageInfo.DriveFirmwareRevision != NULL)
            memcpy(ctx->imageInfo.DriveFirmwareRevision,
                   ctx->metadataBlock + ctx->metadataBlockHeader.driveFirmwareRevisionLength,
                   ctx->metadataBlockHeader.driveFirmwareRevisionLength);
    }

    TRACE("Exiting process_metadata_block()");
}

/**
 * @brief Processes a logical geometry block from the image stream.
 *
 * Reads a logical geometry block from the image and updates the context with its contents.
 *
 * @param ctx Pointer to the aaruformat context.
 * @param entry Pointer to the index entry describing the geometry block.
 */
void process_geometry_block(aaruformatContext *ctx, const IndexEntry *entry)
{
    TRACE("Entering process_geometry_block(%p, %p)", ctx, entry);
    size_t read_bytes = 0;

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");

        TRACE("Exiting process_geometry_block()");
        return;
    }

    // Seek to block
    if(fseek(ctx->imageStream, entry->offset, SEEK_SET) != 0)
    {
        FATAL("Could not seek to %" PRIu64 " as indicated by index entry...", entry->offset);

        TRACE("Exiting process_geometry_block()");
        return;
    }

    TRACE("Reading geometry block header at position %" PRIu64, entry->offset);
    read_bytes = fread(&ctx->geometryBlock, 1, sizeof(GeometryBlockHeader), ctx->imageStream);

    if(read_bytes != sizeof(GeometryBlockHeader))
    {
        memset(&ctx->geometryBlock, 0, sizeof(GeometryBlockHeader));
        TRACE("Could not read geometry block header, continuing...");
        return;
    }

    if(ctx->geometryBlock.identifier != GeometryBlock)
    {
        memset(&ctx->geometryBlock, 0, sizeof(GeometryBlockHeader));
        TRACE("Incorrect identifier for geometry block at position %" PRIu64 "", entry->offset);
        return;
    }

    ctx->imageInfo.ImageSize += sizeof(GeometryBlockHeader);

    TRACE("Geometry set to %d cylinders %d heads %d sectors per track", ctx->geometryBlock.cylinders,
          ctx->geometryBlock.heads, ctx->geometryBlock.sectorsPerTrack);

    ctx->imageInfo.Cylinders       = ctx->geometryBlock.cylinders;
    ctx->imageInfo.Heads           = ctx->geometryBlock.heads;
    ctx->imageInfo.SectorsPerTrack = ctx->geometryBlock.sectorsPerTrack;

    TRACE("Exiting process_geometry_block()");
}

/**
 * @brief Processes a CICM XML metadata block from the image stream.
 *
 * Reads a CICM XML metadata block from the image and updates the context with its contents.
 *
 * @param ctx Pointer to the aaruformat context.
 * @param entry Pointer to the index entry describing the CICM block.
 */
void process_cicm_block(aaruformatContext *ctx, const IndexEntry *entry)
{
    TRACE("Entering process_cicm_block(%p, %p)", ctx, entry);
    int    pos        = 0;
    size_t read_bytes = 0;

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");

        TRACE("Exiting process_cicm_block()");
        return;
    }

    // Seek to block
    TRACE("Seeking to CICM XML metadata block at position %" PRIu64, entry->offset);
    pos = fseek(ctx->imageStream, entry->offset, SEEK_SET);
    if(pos < 0 || ftell(ctx->imageStream) != entry->offset)
    {
        FATAL("Could not seek to %" PRIu64 " as indicated by index entry...", entry->offset);

        TRACE("Exiting process_cicm_block()");
        return;
    }

    // Even if those two checks shall have been done before
    TRACE("Reading CICM XML metadata block header at position %" PRIu64, entry->offset);
    read_bytes = fread(&ctx->cicmBlockHeader, 1, sizeof(CicmMetadataBlock), ctx->imageStream);

    if(read_bytes != sizeof(CicmMetadataBlock))
    {
        memset(&ctx->cicmBlockHeader, 0, sizeof(CicmMetadataBlock));
        TRACE("Could not read CICM XML metadata header, continuing...");
        return;
    }

    if(ctx->cicmBlockHeader.identifier != CicmBlock)
    {
        memset(&ctx->cicmBlockHeader, 0, sizeof(CicmMetadataBlock));
        TRACE("Incorrect identifier for data block at position %" PRIu64 "", entry->offset);
    }

    ctx->imageInfo.ImageSize += ctx->cicmBlockHeader.length;

    ctx->cicmBlock = (uint8_t *)malloc(ctx->cicmBlockHeader.length);

    if(ctx->cicmBlock == NULL)
    {
        memset(&ctx->cicmBlockHeader, 0, sizeof(CicmMetadataBlock));
        TRACE("Could not allocate memory for CICM XML metadata block, continuing...");

        TRACE("Exiting process_cicm_block()");
        return;
    }

    TRACE("Reading CICM XML metadata block of size %u at position %" PRIu64, ctx->cicmBlockHeader.length,
          entry->offset + sizeof(CicmMetadataBlock));
    read_bytes = fread(ctx->cicmBlock, 1, ctx->cicmBlockHeader.length, ctx->imageStream);

    if(read_bytes != ctx->metadataBlockHeader.blockSize)
    {
        memset(&ctx->cicmBlockHeader, 0, sizeof(CicmMetadataBlock));
        free(ctx->cicmBlock);
        TRACE("Could not read CICM XML metadata block, continuing...");
    }

    TRACE("Found CICM XML metadata block %" PRIu64 ".", entry->offset);

    TRACE("Exiting process_cicm_block()");
}