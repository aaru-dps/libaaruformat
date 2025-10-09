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
        ctx->MediaSequence     = ctx->metadataBlockHeader.mediaSequence;
        ctx->LastMediaSequence = ctx->metadataBlockHeader.lastMediaSequence;
        TRACE("Setting media sequence as %d of %d", ctx->MediaSequence, ctx->LastMediaSequence);
    }

    if(ctx->metadataBlockHeader.creatorLength > 0 &&
       ctx->metadataBlockHeader.creatorOffset + ctx->metadataBlockHeader.creatorLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->Creator = (uint8_t *)malloc(ctx->metadataBlockHeader.creatorLength);
        if(ctx->Creator != NULL)
            memcpy(ctx->Creator, ctx->metadataBlock + ctx->metadataBlockHeader.creatorOffset,
                   ctx->metadataBlockHeader.creatorLength);
    }

    if(ctx->metadataBlockHeader.commentsLength > 0 &&
       ctx->metadataBlockHeader.commentsOffset + ctx->metadataBlockHeader.commentsLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->Comments = (uint8_t *)malloc(ctx->metadataBlockHeader.commentsLength);
        if(ctx->Comments != NULL)
            memcpy(ctx->Comments, ctx->metadataBlock + ctx->metadataBlockHeader.commentsOffset,
                   ctx->metadataBlockHeader.commentsLength);
    }

    if(ctx->metadataBlockHeader.mediaTitleLength > 0 &&
       ctx->metadataBlockHeader.mediaTitleOffset + ctx->metadataBlockHeader.mediaTitleLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->MediaTitle = (uint8_t *)malloc(ctx->metadataBlockHeader.mediaTitleLength);
        if(ctx->MediaTitle != NULL)
            memcpy(ctx->MediaTitle, ctx->metadataBlock + ctx->metadataBlockHeader.mediaTitleOffset,
                   ctx->metadataBlockHeader.mediaTitleLength);
    }

    if(ctx->metadataBlockHeader.mediaManufacturerLength > 0 &&
       ctx->metadataBlockHeader.mediaManufacturerOffset + ctx->metadataBlockHeader.mediaManufacturerLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->MediaManufacturer = (uint8_t *)malloc(ctx->metadataBlockHeader.mediaManufacturerLength);
        if(ctx->MediaManufacturer != NULL)
            memcpy(ctx->MediaManufacturer, ctx->metadataBlock + ctx->metadataBlockHeader.mediaManufacturerOffset,
                   ctx->metadataBlockHeader.mediaManufacturerLength);
    }

    if(ctx->metadataBlockHeader.mediaModelLength > 0 &&
       ctx->metadataBlockHeader.mediaModelOffset + ctx->metadataBlockHeader.mediaModelLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->MediaModel = (uint8_t *)malloc(ctx->metadataBlockHeader.mediaModelLength);
        if(ctx->MediaModel != NULL)
            memcpy(ctx->MediaModel, ctx->metadataBlock + ctx->metadataBlockHeader.mediaModelOffset,
                   ctx->metadataBlockHeader.mediaModelLength);
    }

    if(ctx->metadataBlockHeader.mediaSerialNumberLength > 0 &&
       ctx->metadataBlockHeader.mediaSerialNumberOffset + ctx->metadataBlockHeader.mediaSerialNumberLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->MediaSerialNumber = (uint8_t *)malloc(ctx->metadataBlockHeader.mediaSerialNumberLength);
        if(ctx->MediaSerialNumber != NULL)
            memcpy(ctx->MediaSerialNumber, ctx->metadataBlock + ctx->metadataBlockHeader.mediaSerialNumberOffset,
                   ctx->metadataBlockHeader.mediaSerialNumberLength);
    }

    if(ctx->metadataBlockHeader.mediaBarcodeLength > 0 &&
       ctx->metadataBlockHeader.mediaBarcodeOffset + ctx->metadataBlockHeader.mediaBarcodeLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->MediaBarcode = (uint8_t *)malloc(ctx->metadataBlockHeader.mediaBarcodeLength);
        if(ctx->MediaBarcode != NULL)
            memcpy(ctx->MediaBarcode, ctx->metadataBlock + ctx->metadataBlockHeader.mediaBarcodeOffset,
                   ctx->metadataBlockHeader.mediaBarcodeLength);
    }

    if(ctx->metadataBlockHeader.mediaPartNumberLength > 0 &&
       ctx->metadataBlockHeader.mediaPartNumberOffset + ctx->metadataBlockHeader.mediaPartNumberLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->MediaPartNumber = (uint8_t *)malloc(ctx->metadataBlockHeader.mediaPartNumberLength);
        if(ctx->MediaPartNumber != NULL)
            memcpy(ctx->MediaPartNumber, ctx->metadataBlock + ctx->metadataBlockHeader.mediaPartNumberOffset,
                   ctx->metadataBlockHeader.mediaPartNumberLength);
    }

    if(ctx->metadataBlockHeader.driveManufacturerLength > 0 &&
       ctx->metadataBlockHeader.driveManufacturerOffset + ctx->metadataBlockHeader.driveManufacturerLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->DriveManufacturer = (uint8_t *)malloc(ctx->metadataBlockHeader.driveManufacturerLength);
        if(ctx->DriveManufacturer != NULL)
            memcpy(ctx->DriveManufacturer, ctx->metadataBlock + ctx->metadataBlockHeader.driveManufacturerOffset,
                   ctx->metadataBlockHeader.driveManufacturerLength);
    }

    if(ctx->metadataBlockHeader.driveModelLength > 0 &&
       ctx->metadataBlockHeader.driveModelOffset + ctx->metadataBlockHeader.driveModelLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->DriveModel = (uint8_t *)malloc(ctx->metadataBlockHeader.driveModelLength);
        if(ctx->DriveModel != NULL)
            memcpy(ctx->DriveModel, ctx->metadataBlock + ctx->metadataBlockHeader.driveModelOffset,
                   ctx->metadataBlockHeader.driveModelLength);
    }

    if(ctx->metadataBlockHeader.driveSerialNumberLength > 0 &&
       ctx->metadataBlockHeader.driveSerialNumberOffset + ctx->metadataBlockHeader.driveSerialNumberLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->DriveSerialNumber = (uint8_t *)malloc(ctx->metadataBlockHeader.driveSerialNumberLength);
        if(ctx->DriveSerialNumber != NULL)
            memcpy(ctx->DriveSerialNumber, ctx->metadataBlock + ctx->metadataBlockHeader.driveSerialNumberOffset,
                   ctx->metadataBlockHeader.driveSerialNumberLength);
    }

    if(ctx->metadataBlockHeader.driveFirmwareRevisionLength > 0 &&
       ctx->metadataBlockHeader.driveFirmwareRevisionOffset + ctx->metadataBlockHeader.driveFirmwareRevisionLength <=
           ctx->metadataBlockHeader.blockSize)
    {
        ctx->DriveFirmwareRevision = (uint8_t *)malloc(ctx->metadataBlockHeader.driveFirmwareRevisionLength);
        if(ctx->DriveFirmwareRevision != NULL)
            memcpy(ctx->DriveFirmwareRevision,
                   ctx->metadataBlock + ctx->metadataBlockHeader.driveFirmwareRevisionOffset,
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

    ctx->Cylinders       = ctx->geometryBlock.cylinders;
    ctx->Heads           = ctx->geometryBlock.heads;
    ctx->SectorsPerTrack = ctx->geometryBlock.sectorsPerTrack;

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

    if(read_bytes != ctx->cicmBlockHeader.length)
    {
        memset(&ctx->cicmBlockHeader, 0, sizeof(CicmMetadataBlock));
        free(ctx->cicmBlock);
        TRACE("Could not read CICM XML metadata block, continuing...");
    }

    TRACE("Found CICM XML metadata block %" PRIu64 ".", entry->offset);

    TRACE("Exiting process_cicm_block()");
}

/**
 * @brief Processes an Aaru metadata JSON block from the image stream during image opening.
 *
 * Reads an Aaru metadata JSON block from the image file and loads its contents into the context
 * for subsequent retrieval. The Aaru metadata JSON format is a structured representation of
 * comprehensive image metadata including media information, imaging session details, hardware
 * configuration, optical disc tracks and sessions, checksums, and preservation metadata. The
 * JSON payload is stored in its original form without parsing or interpretation by this function,
 * allowing higher-level code to process the structured data as needed.
 *
 * This function is called during the image opening process (aaruf_open) when an index entry
 * indicates the presence of an AaruMetadataJsonBlock. The function is non-critical; if reading
 * fails or memory allocation fails, the error is logged but the image opening continues. This
 * allows images without JSON metadata or with corrupted JSON blocks to still be opened for
 * data access.
 *
 * **Processing sequence:**
 * 1. Validate context and image stream
 * 2. Seek to the block offset specified by the index entry
 * 3. Read the AaruMetadataJsonBlockHeader (8 bytes: identifier + length)
 * 4. Validate the block identifier matches AaruMetadataJsonBlock
 * 5. Allocate memory for the JSON payload
 * 6. Read the JSON data from the file stream
 * 7. Store header and data pointer in the context for later retrieval
 *
 * **Memory allocation:**
 * The function allocates memory (via malloc) sized to hold the entire JSON payload as specified
 * by ctx->jsonBlockHeader.length. This memory remains allocated for the lifetime of the context
 * and is freed during aaruf_close(). If allocation fails, the function returns gracefully without
 * the JSON metadata, allowing the image to still be opened.
 *
 * **Image size tracking:**
 * The function increments ctx->imageInfo.ImageSize by the length of the JSON payload to track
 * the total size of metadata and structural blocks in the image.
 *
 * **Error handling:**
 * All errors are non-fatal and handled gracefully:
 * - Seek failures: logged and function returns early
 * - Header read failures: header zeroed, function returns
 * - Identifier mismatches: header zeroed, processing continues but data is not loaded
 * - Memory allocation failures: header zeroed, function returns
 * - Data read failures: header zeroed, allocated memory freed, function returns
 *
 * In all error cases, the ctx->jsonBlockHeader is zeroed (memset to 0) to indicate that no
 * valid JSON metadata is available, and any allocated memory is properly freed.
 *
 * @param ctx Pointer to an initialized aaruformatContext being populated during image opening.
 *            Must not be NULL. ctx->imageStream must be open and readable. On success,
 *            ctx->jsonBlockHeader will contain the block header and ctx->jsonBlock will
 *            point to the allocated JSON data.
 * @param entry Pointer to the IndexEntry that specifies the file offset where the
 *              AaruMetadataJsonBlock begins. Must not be NULL. entry->offset indicates
 *              the position of the block header in the file.
 *
 * @note JSON Format and Encoding:
 *       - The JSON payload is stored in UTF-8 encoding
 *       - The payload may or may not be null-terminated
 *       - This function treats the JSON as opaque binary data
 *       - No JSON parsing, interpretation, or validation is performed during loading
 *       - JSON schema validation is the responsibility of code that retrieves the metadata
 *
 * @note Aaru Metadata JSON Purpose:
 *       - Provides machine-readable structured metadata about the image
 *       - Includes comprehensive information about media, sessions, tracks, and checksums
 *       - Enables programmatic access to metadata without XML parsing overhead
 *       - Complements CICM XML with a more modern, structured format
 *       - Used by Aaru and compatible tools for metadata exchange
 *
 * @note Non-Critical Nature:
 *       - JSON metadata is optional and supplementary to core image data
 *       - Failures reading this block do not prevent image opening
 *       - The image remains fully functional for sector data access without JSON metadata
 *       - Higher-level code should check if ctx->jsonBlock is non-NULL before use
 *
 * @note Distinction from CICM XML:
 *       - Both CICM and Aaru JSON blocks can coexist in the same image
 *       - CICM XML follows the Canary Islands Computer Museum schema
 *       - Aaru JSON follows the Aaru-specific metadata schema
 *       - Different tools may prefer one format over the other
 *
 * @warning Memory allocated for ctx->jsonBlock persists for the context lifetime and must be
 *          freed during context cleanup (aaruf_close).
 *
 * @warning This function does not validate JSON syntax or schema. Corrupted JSON data will
 *          be loaded successfully and errors will only be detected when attempting to parse.
 *
 * @see AaruMetadataJsonBlockHeader for the on-disk structure definition.
 * @see process_cicm_block() for processing CICM XML metadata blocks.
 * @see aaruf_open() for the overall image opening sequence.
 *
 * @internal
 */
void process_aaru_metadata_json_block(aaruformatContext *ctx, const IndexEntry *entry)
{
    TRACE("Entering process_aaru_metadata_json_block(%p, %p)", ctx, entry);
    int    pos        = 0;
    size_t read_bytes = 0;

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");

        TRACE("Exiting process_aaru_metadata_json_block()");
        return;
    }

    // Seek to block
    TRACE("Seeking to Aaru metadata JSON block at position %" PRIu64, entry->offset);
    pos = fseek(ctx->imageStream, entry->offset, SEEK_SET);
    if(pos < 0 || ftell(ctx->imageStream) != entry->offset)
    {
        FATAL("Could not seek to %" PRIu64 " as indicated by index entry...", entry->offset);

        TRACE("Exiting process_aaru_metadata_json_block()");
        return;
    }

    // Even if those two checks shall have been done before
    TRACE("Reading Aaru metadata JSON block header at position %" PRIu64, entry->offset);
    read_bytes = fread(&ctx->jsonBlockHeader, 1, sizeof(AaruMetadataJsonBlockHeader), ctx->imageStream);

    if(read_bytes != sizeof(AaruMetadataJsonBlockHeader))
    {
        memset(&ctx->jsonBlockHeader, 0, sizeof(AaruMetadataJsonBlockHeader));
        TRACE("Could not read Aaru metadata JSON header, continuing...");
        return;
    }

    if(ctx->jsonBlockHeader.identifier != AaruMetadataJsonBlock)
    {
        memset(&ctx->jsonBlockHeader, 0, sizeof(AaruMetadataJsonBlockHeader));
        TRACE("Incorrect identifier for data block at position %" PRIu64 "", entry->offset);
    }

    ctx->imageInfo.ImageSize += ctx->jsonBlockHeader.length;

    ctx->jsonBlock = (uint8_t *)malloc(ctx->jsonBlockHeader.length);

    if(ctx->jsonBlock == NULL)
    {
        memset(&ctx->jsonBlockHeader, 0, sizeof(AaruMetadataJsonBlockHeader));
        TRACE("Could not allocate memory for Aaru metadata JSON block, continuing...");

        TRACE("Exiting process_aaru_metadata_json_block()");
        return;
    }

    TRACE("Reading Aaru metadata JSON block of size %u at position %" PRIu64, ctx->jsonBlockHeader.length,
          entry->offset + sizeof(AaruMetadataJsonBlockHeader));
    read_bytes = fread(ctx->jsonBlock, 1, ctx->jsonBlockHeader.length, ctx->imageStream);

    if(read_bytes != ctx->jsonBlockHeader.length)
    {
        memset(&ctx->jsonBlockHeader, 0, sizeof(AaruMetadataJsonBlockHeader));
        free(ctx->jsonBlock);
        TRACE("Could not read Aaru metadata JSON block, continuing...");
    }

    TRACE("Found Aaru metadata JSON block %" PRIu64 ".", entry->offset);

    TRACE("Exiting process_aaru_metadata_json_block()");
}