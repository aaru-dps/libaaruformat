/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
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
 * @brief Processes a metadata block from the image stream.
 *
 * Reads a metadata block from the image and updates the context with its contents.
 *
 * @param ctx Pointer to the aaruformat context.
 * @param entry Pointer to the index entry describing the metadata block.
 */
void process_metadata_block(aaruformat_context *ctx, const IndexEntry *entry)
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
    pos = aaruf_fseek(ctx->imageStream, (aaru_off_t)entry->offset, SEEK_SET);
    if(pos < 0 || aaruf_ftell(ctx->imageStream) != (aaru_off_t)entry->offset)
    {
        FATAL("Could not seek to %" PRIu64 " as indicated by index entry...", entry->offset);

        TRACE("Exiting process_metadata_block()");
        return;
    }

    // Even if those two checks shall have been done before
    TRACE("Reading metadata block header at position %" PRIu64, entry->offset);
    read_bytes = fread(&ctx->metadata_block_header, 1, sizeof(MetadataBlockHeader), ctx->imageStream);

    if(read_bytes != sizeof(MetadataBlockHeader))
    {
        memset(&ctx->metadata_block_header, 0, sizeof(MetadataBlockHeader));
        FATAL("Could not read metadata block header, continuing...");

        TRACE("Exiting process_metadata_block()");
        return;
    }

    if(ctx->metadata_block_header.identifier != entry->blockType)
    {
        memset(&ctx->metadata_block_header, 0, sizeof(MetadataBlockHeader));
        TRACE("Incorrect identifier for data block at position %" PRIu64 "", entry->offset);

        TRACE("Exiting process_metadata_block()");
        return;
    }

    ctx->image_info.ImageSize += ctx->metadata_block_header.blockSize;

    ctx->metadata_block = (uint8_t *)malloc(ctx->metadata_block_header.blockSize + sizeof(MetadataBlockHeader));

    if(ctx->metadata_block == NULL)
    {
        memset(&ctx->metadata_block_header, 0, sizeof(MetadataBlockHeader));
        FATAL("Could not allocate memory for metadata block, continuing...");

        TRACE("Exiting process_metadata_block()");
        return;
    }

    TRACE("Reading metadata block of size %u at position %" PRIu64,
          ctx->metadata_block_header.blockSize + sizeof(MetadataBlockHeader), entry->offset);

    aaruf_fseek(ctx->imageStream, (aaru_off_t)entry->offset, SEEK_SET);
    read_bytes = fread(ctx->metadata_block, 1, ctx->metadata_block_header.blockSize + sizeof(MetadataBlockHeader),
                       ctx->imageStream);

    if(read_bytes != ctx->metadata_block_header.blockSize + sizeof(MetadataBlockHeader))
    {
        memset(&ctx->metadata_block_header, 0, sizeof(MetadataBlockHeader));
        free(ctx->metadata_block);
        ctx->metadata_block = NULL;
        FATAL("Could not read metadata block, continuing...");

        return;
    }

    if(ctx->metadata_block_header.mediaSequence > 0 && ctx->metadata_block_header.lastMediaSequence > 0)
    {
        ctx->media_sequence      = ctx->metadata_block_header.mediaSequence;
        ctx->last_media_sequence = ctx->metadata_block_header.lastMediaSequence;
        TRACE("Setting media sequence as %d of %d", ctx->media_sequence, ctx->last_media_sequence);
    }

    if(ctx->metadata_block_header.creatorLength > 0 &&
       ctx->metadata_block_header.creatorOffset + ctx->metadata_block_header.creatorLength <=
           ctx->metadata_block_header.blockSize)
    {
        ctx->creator = (uint8_t *)malloc(ctx->metadata_block_header.creatorLength);
        if(ctx->creator != NULL)
            memcpy(ctx->creator, ctx->metadata_block + ctx->metadata_block_header.creatorOffset,
                   ctx->metadata_block_header.creatorLength);
    }

    if(ctx->metadata_block_header.commentsLength > 0 &&
       ctx->metadata_block_header.commentsOffset + ctx->metadata_block_header.commentsLength <=
           ctx->metadata_block_header.blockSize)
    {
        ctx->comments = (uint8_t *)malloc(ctx->metadata_block_header.commentsLength);
        if(ctx->comments != NULL)
            memcpy(ctx->comments, ctx->metadata_block + ctx->metadata_block_header.commentsOffset,
                   ctx->metadata_block_header.commentsLength);
    }

    if(ctx->metadata_block_header.mediaTitleLength > 0 &&
       ctx->metadata_block_header.mediaTitleOffset + ctx->metadata_block_header.mediaTitleLength <=
           ctx->metadata_block_header.blockSize)
    {
        ctx->media_title = (uint8_t *)malloc(ctx->metadata_block_header.mediaTitleLength);
        if(ctx->media_title != NULL)
            memcpy(ctx->media_title, ctx->metadata_block + ctx->metadata_block_header.mediaTitleOffset,
                   ctx->metadata_block_header.mediaTitleLength);
    }

    if(ctx->metadata_block_header.mediaManufacturerLength > 0 &&
       ctx->metadata_block_header.mediaManufacturerOffset + ctx->metadata_block_header.mediaManufacturerLength <=
           ctx->metadata_block_header.blockSize)
    {
        ctx->media_manufacturer = (uint8_t *)malloc(ctx->metadata_block_header.mediaManufacturerLength);
        if(ctx->media_manufacturer != NULL)
            memcpy(ctx->media_manufacturer, ctx->metadata_block + ctx->metadata_block_header.mediaManufacturerOffset,
                   ctx->metadata_block_header.mediaManufacturerLength);
    }

    if(ctx->metadata_block_header.mediaModelLength > 0 &&
       ctx->metadata_block_header.mediaModelOffset + ctx->metadata_block_header.mediaModelLength <=
           ctx->metadata_block_header.blockSize)
    {
        ctx->media_model = (uint8_t *)malloc(ctx->metadata_block_header.mediaModelLength);
        if(ctx->media_model != NULL)
            memcpy(ctx->media_model, ctx->metadata_block + ctx->metadata_block_header.mediaModelOffset,
                   ctx->metadata_block_header.mediaModelLength);
    }

    if(ctx->metadata_block_header.mediaSerialNumberLength > 0 &&
       ctx->metadata_block_header.mediaSerialNumberOffset + ctx->metadata_block_header.mediaSerialNumberLength <=
           ctx->metadata_block_header.blockSize)
    {
        ctx->media_serial_number = (uint8_t *)malloc(ctx->metadata_block_header.mediaSerialNumberLength);
        if(ctx->media_serial_number != NULL)
            memcpy(ctx->media_serial_number, ctx->metadata_block + ctx->metadata_block_header.mediaSerialNumberOffset,
                   ctx->metadata_block_header.mediaSerialNumberLength);
    }

    if(ctx->metadata_block_header.mediaBarcodeLength > 0 &&
       ctx->metadata_block_header.mediaBarcodeOffset + ctx->metadata_block_header.mediaBarcodeLength <=
           ctx->metadata_block_header.blockSize)
    {
        ctx->media_barcode = (uint8_t *)malloc(ctx->metadata_block_header.mediaBarcodeLength);
        if(ctx->media_barcode != NULL)
            memcpy(ctx->media_barcode, ctx->metadata_block + ctx->metadata_block_header.mediaBarcodeOffset,
                   ctx->metadata_block_header.mediaBarcodeLength);
    }

    if(ctx->metadata_block_header.mediaPartNumberLength > 0 &&
       ctx->metadata_block_header.mediaPartNumberOffset + ctx->metadata_block_header.mediaPartNumberLength <=
           ctx->metadata_block_header.blockSize)
    {
        ctx->media_part_number = (uint8_t *)malloc(ctx->metadata_block_header.mediaPartNumberLength);
        if(ctx->media_part_number != NULL)
            memcpy(ctx->media_part_number, ctx->metadata_block + ctx->metadata_block_header.mediaPartNumberOffset,
                   ctx->metadata_block_header.mediaPartNumberLength);
    }

    if(ctx->metadata_block_header.driveManufacturerLength > 0 &&
       ctx->metadata_block_header.driveManufacturerOffset + ctx->metadata_block_header.driveManufacturerLength <=
           ctx->metadata_block_header.blockSize)
    {
        ctx->drive_manufacturer = (uint8_t *)malloc(ctx->metadata_block_header.driveManufacturerLength);
        if(ctx->drive_manufacturer != NULL)
            memcpy(ctx->drive_manufacturer, ctx->metadata_block + ctx->metadata_block_header.driveManufacturerOffset,
                   ctx->metadata_block_header.driveManufacturerLength);
    }

    if(ctx->metadata_block_header.driveModelLength > 0 &&
       ctx->metadata_block_header.driveModelOffset + ctx->metadata_block_header.driveModelLength <=
           ctx->metadata_block_header.blockSize)
    {
        ctx->drive_model = (uint8_t *)malloc(ctx->metadata_block_header.driveModelLength);
        if(ctx->drive_model != NULL)
            memcpy(ctx->drive_model, ctx->metadata_block + ctx->metadata_block_header.driveModelOffset,
                   ctx->metadata_block_header.driveModelLength);
    }

    if(ctx->metadata_block_header.driveSerialNumberLength > 0 &&
       ctx->metadata_block_header.driveSerialNumberOffset + ctx->metadata_block_header.driveSerialNumberLength <=
           ctx->metadata_block_header.blockSize)
    {
        ctx->drive_serial_number = (uint8_t *)malloc(ctx->metadata_block_header.driveSerialNumberLength);
        if(ctx->drive_serial_number != NULL)
            memcpy(ctx->drive_serial_number, ctx->metadata_block + ctx->metadata_block_header.driveSerialNumberOffset,
                   ctx->metadata_block_header.driveSerialNumberLength);
    }

    if(ctx->metadata_block_header.driveFirmwareRevisionLength > 0 &&
       ctx->metadata_block_header.driveFirmwareRevisionOffset +
               ctx->metadata_block_header.driveFirmwareRevisionLength <=
           ctx->metadata_block_header.blockSize)
    {
        ctx->drive_firmware_revision = (uint8_t *)malloc(ctx->metadata_block_header.driveFirmwareRevisionLength);
        if(ctx->drive_firmware_revision != NULL)
            memcpy(ctx->drive_firmware_revision,
                   ctx->metadata_block + ctx->metadata_block_header.driveFirmwareRevisionOffset,
                   ctx->metadata_block_header.driveFirmwareRevisionLength);
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
void process_geometry_block(aaruformat_context *ctx, const IndexEntry *entry)
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
    if(aaruf_fseek(ctx->imageStream, (aaru_off_t)entry->offset, SEEK_SET) != 0)
    {
        FATAL("Could not seek to %" PRIu64 " as indicated by index entry...", entry->offset);

        TRACE("Exiting process_geometry_block()");
        return;
    }

    TRACE("Reading geometry block header at position %" PRIu64, entry->offset);
    read_bytes = fread(&ctx->geometry_block, 1, sizeof(GeometryBlockHeader), ctx->imageStream);

    if(read_bytes != sizeof(GeometryBlockHeader))
    {
        memset(&ctx->geometry_block, 0, sizeof(GeometryBlockHeader));
        TRACE("Could not read geometry block header, continuing...");
        return;
    }

    if(ctx->geometry_block.identifier != GeometryBlock)
    {
        memset(&ctx->geometry_block, 0, sizeof(GeometryBlockHeader));
        TRACE("Incorrect identifier for geometry block at position %" PRIu64 "", entry->offset);
        return;
    }

    ctx->image_info.ImageSize += sizeof(GeometryBlockHeader);

    TRACE("Geometry set to %d cylinders %d heads %d sectors per track", ctx->geometry_block.cylinders,
          ctx->geometry_block.heads, ctx->geometry_block.sectorsPerTrack);

    ctx->cylinders         = ctx->geometry_block.cylinders;
    ctx->heads             = ctx->geometry_block.heads;
    ctx->sectors_per_track = ctx->geometry_block.sectorsPerTrack;

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
void process_cicm_block(aaruformat_context *ctx, const IndexEntry *entry)
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
    pos = aaruf_fseek(ctx->imageStream, (aaru_off_t)entry->offset, SEEK_SET);
    if(pos < 0 || aaruf_ftell(ctx->imageStream) != (aaru_off_t)entry->offset)
    {
        FATAL("Could not seek to %" PRIu64 " as indicated by index entry...", entry->offset);

        TRACE("Exiting process_cicm_block()");
        return;
    }

    // Even if those two checks shall have been done before
    TRACE("Reading CICM XML metadata block header at position %" PRIu64, entry->offset);
    read_bytes = fread(&ctx->cicm_block_header, 1, sizeof(CicmMetadataBlock), ctx->imageStream);

    if(read_bytes != sizeof(CicmMetadataBlock))
    {
        memset(&ctx->cicm_block_header, 0, sizeof(CicmMetadataBlock));
        TRACE("Could not read CICM XML metadata header, continuing...");
        return;
    }

    if(ctx->cicm_block_header.identifier != CicmBlock)
    {
        memset(&ctx->cicm_block_header, 0, sizeof(CicmMetadataBlock));
        TRACE("Incorrect identifier for data block at position %" PRIu64 "", entry->offset);
    }

    ctx->image_info.ImageSize += ctx->cicm_block_header.length;

    ctx->cicm_block = (uint8_t *)malloc(ctx->cicm_block_header.length);

    if(ctx->cicm_block == NULL)
    {
        memset(&ctx->cicm_block_header, 0, sizeof(CicmMetadataBlock));
        TRACE("Could not allocate memory for CICM XML metadata block, continuing...");

        TRACE("Exiting process_cicm_block()");
        return;
    }

    TRACE("Reading CICM XML metadata block of size %u at position %" PRIu64, ctx->cicm_block_header.length,
          entry->offset + sizeof(CicmMetadataBlock));
    read_bytes = fread(ctx->cicm_block, 1, ctx->cicm_block_header.length, ctx->imageStream);

    if(read_bytes != ctx->cicm_block_header.length)
    {
        memset(&ctx->cicm_block_header, 0, sizeof(CicmMetadataBlock));
        free(ctx->cicm_block);
        ctx->cicm_block = NULL;
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
void process_aaru_metadata_json_block(aaruformat_context *ctx, const IndexEntry *entry)
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
    pos = aaruf_fseek(ctx->imageStream, (aaru_off_t)entry->offset, SEEK_SET);
    if(pos < 0 || aaruf_ftell(ctx->imageStream) != (aaru_off_t)entry->offset)
    {
        FATAL("Could not seek to %" PRIu64 " as indicated by index entry...", entry->offset);

        TRACE("Exiting process_aaru_metadata_json_block()");
        return;
    }

    // Even if those two checks shall have been done before
    TRACE("Reading Aaru metadata JSON block header at position %" PRIu64, entry->offset);
    read_bytes = fread(&ctx->json_block_header, 1, sizeof(AaruMetadataJsonBlockHeader), ctx->imageStream);

    if(read_bytes != sizeof(AaruMetadataJsonBlockHeader))
    {
        memset(&ctx->json_block_header, 0, sizeof(AaruMetadataJsonBlockHeader));
        TRACE("Could not read Aaru metadata JSON header, continuing...");
        return;
    }

    if(ctx->json_block_header.identifier != AaruMetadataJsonBlock)
    {
        memset(&ctx->json_block_header, 0, sizeof(AaruMetadataJsonBlockHeader));
        TRACE("Incorrect identifier for data block at position %" PRIu64 "", entry->offset);
    }

    ctx->image_info.ImageSize += ctx->json_block_header.length;

    ctx->json_block = (uint8_t *)malloc(ctx->json_block_header.length);

    if(ctx->json_block == NULL)
    {
        memset(&ctx->json_block_header, 0, sizeof(AaruMetadataJsonBlockHeader));
        TRACE("Could not allocate memory for Aaru metadata JSON block, continuing...");

        TRACE("Exiting process_aaru_metadata_json_block()");
        return;
    }

    TRACE("Reading Aaru metadata JSON block of size %u at position %" PRIu64, ctx->json_block_header.length,
          entry->offset + sizeof(AaruMetadataJsonBlockHeader));
    read_bytes = fread(ctx->json_block, 1, ctx->json_block_header.length, ctx->imageStream);

    if(read_bytes != ctx->json_block_header.length)
    {
        memset(&ctx->json_block_header, 0, sizeof(AaruMetadataJsonBlockHeader));
        free(ctx->json_block);
        ctx->json_block = NULL;
        TRACE("Could not read Aaru metadata JSON block, continuing...");
    }

    TRACE("Found Aaru metadata JSON block %" PRIu64 ".", entry->offset);

    TRACE("Exiting process_aaru_metadata_json_block()");
}

/**
 * @brief Retrieves which sector tags are readable in the AaruFormat image.
 *
 * Returns an array of booleans indicating whether each SectorTagType was successfully read
 * from the image. The array is indexed by SectorTagType enum values (0 to MaxSectorTag),
 * where each boolean is true if the corresponding sector tag data is present and readable
 * in the image. Sector tags contain per-sector metadata such as sync headers, ECC/EDC codes,
 * subchannels, and other sector-level information essential for exact media reconstruction.
 *
 * @param context Pointer to the aaruformat context (must be a valid, opened image context).
 * @param buffer Pointer to a buffer to store the boolean array. Can be NULL to query required length.
 * @param length Pointer to the buffer length. On input, contains buffer size; on output, contains required size.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully retrieved readable sector tags. This is returned when:
 *         - The context is valid and properly initialized
 *         - The readableSectorTags array is populated in the context
 *         - The buffer is large enough to hold all SectorTagType values (0 to MaxSectorTag)
 *         - The output array has been successfully copied from the internal readableSectorTags
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_open() or aaruf_create()
 *
 * @retval AARUF_ERROR_METADATA_NOT_PRESENT (-30) The sector tags array is not available. This occurs when:
 *         - The readableSectorTags array is NULL (not initialized in the context)
 *         - The image was created without sector tag support
 *         - The image format does not support or require sector tags
 *
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-10) The buffer is too small. This occurs when:
 *         - The buffer parameter is NULL
 *         - The length parameter is NULL
 *         - The provided buffer is smaller than required (size = (MaxSectorTag + 1) * sizeof(bool))
 *         - The length output will contain the required buffer size in bytes
 *
 * @note Buffer Size:
 *       The buffer must be at least (MaxSectorTag + 1) bytes, where each byte represents
 *       whether the corresponding SectorTagType data was successfully read in the image.
 *       On most systems, sizeof(bool) == 1, so the required size is (MaxSectorTag + 1) bytes.
 *
 * @note Sector Tag Types:
 *       Sector tags preserve on-disk structures not part of main user data. Examples include:
 *       - CD sector sync/header information (CdSectorSync, CdSectorHeader)
 *       - CD subheaders and ECC/EDC codes (CdSectorSubHeader, CdSectorEcc, CdSectorEdc)
 *       - DVD sector numbers and IED (DvdSectorNumber)
 *       - Media-specific proprietary tags (AppleProfileTagAaru, PriamDataTowerTagAaru)
 *
 * @note Query Mode:
 *       To query the required buffer size, pass buffer == NULL or *length < required.
 *       The function will return AARUF_ERROR_BUFFER_TOO_SMALL and set *length to required size.
 *       This allows allocation of properly sized buffers without prior knowledge of MaxSectorTag.
 *
 * @note Data Interpretation:
 *       A true value (non-zero) at buffer[i] indicates sector tag type i was readable.
 *       A false value (zero) at buffer[i] indicates that sector tag type i is either not present
 *       or was not successfully read from the image during opening.
 *       The readableSectorTags array is populated during image opening/processing in data blocks.
 *
 * @note Image Opening Context:
 *       The readableSectorTags array is initialized during aaruf_open() or aaruf_create()
 *       and populated as data blocks are processed. It reflects what was actually present
 *       and readable in the image file, not what theoretically could be present for the media type.
 *
 * @warning The output parameters are only modified on success (AARUF_STATUS_OK).
 *          On error, their values remain unchanged. Initialize them before calling
 *          if default values are needed on failure.
 *
 * @warning If readableSectorTags is NULL in the context, AARUF_ERROR_NOT_FOUND is returned.
 *          This typically indicates the image format does not support sector-level tags,
 *          rather than indicating an error state. Check return value to distinguish.
 *
 * @see SectorTagType for enumeration of all available sector tag types
 * @see aaruf_read_sector_tag() for reading individual sector tag data
 * @see aaruf_write_sector_tag() for writing sector tags during image creation
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_readable_sector_tags(const void *context, uint8_t *buffer, size_t *length)
{
    TRACE("Entering aaruf_get_readable_sector_tags(%p, %p, %zu)", context, buffer, (length ? *length : 0));

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_readable_sector_tags() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_readable_sector_tags() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->readableSectorTags == NULL)
    {
        FATAL("Image contains no readable sector tags");

        TRACE("Exiting aaruf_get_readable_sector_tags() = AARUF_ERROR_METADATA_NOT_PRESENT");
        return AARUF_ERROR_METADATA_NOT_PRESENT;
    }

    size_t required_length = sizeof(bool) * (MaxSectorTag + 1);

    if(buffer == NULL || length == NULL || *length < required_length)
    {
        if(length) *length = required_length;
        TRACE("Buffer too small for readable sector tags, required %zu bytes", required_length);

        TRACE("Exiting aaruf_get_readable_sector_tags() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    memcpy(buffer, ctx->readableSectorTags, required_length);
    *length = required_length;

    TRACE("Exiting aaruf_get_readable_sector_tags(%p, %p, %zu) = AARUF_STATUS_OK", context, buffer, *length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves which media tags are present in the AaruFormat image.
 *
 * Returns an array of booleans indicating the presence of each MediaTagType in the image.
 * The array is indexed by MediaTagType enum values (0 to MaxMediaTag), where each boolean
 * is true if the corresponding media tag exists in the image's mediaTags hash table.
 *
 * @param context Pointer to the aaruformat context (must be a valid, opened image context).
 * @param buffer Pointer to a buffer to store the boolean array. Can be NULL to query required length.
 * @param length Pointer to the buffer length. On input, contains buffer size; on output, contains required size.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully retrieved readable media tags. This is returned when:
 *         - The context is valid and properly initialized
 *         - The buffer is large enough to hold all MediaTagType values (0 to MaxMediaTag)
 *         - The output array has been populated with true/false for each MediaTagType
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-10) The buffer is too small. This occurs when:
 *         - The buffer parameter is NULL
 *         - The length parameter is NULL
 *         - The provided buffer is smaller than required (size = MaxMediaTag + 1 bytes)
 *         - The length output will contain the required buffer size
 *
 * @note Buffer Size:
 *       The buffer must be at least (MaxMediaTag + 1) bytes, where each byte represents
 *       whether the corresponding MediaTagType is present in the image.
 *
 * @note Query Mode:
 *       To query the required buffer size, pass buffer == NULL or *length < required.
 *       The function will return AARUF_ERROR_BUFFER_TOO_SMALL and set *length to required size.
 *
 * @warning The output parameters are only modified on success (AARUF_STATUS_OK).
 *          On error, their values remain unchanged.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_readable_media_tags(const void *context, uint8_t *buffer, size_t *length)
{
    TRACE("Entering aaruf_get_readable_media_tags(%p, %p, %zu)", context, buffer, (length ? *length : 0));

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_readable_media_tags() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_readable_media_tags() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Required size: one byte for each MediaTagType (0 to MaxMediaTag)
    size_t required_length = MaxMediaTag + 1;

    if(buffer == NULL || length == NULL || *length < required_length)
    {
        if(length) *length = required_length;
        TRACE("Buffer too small for readable media tags, required %zu bytes", required_length);

        TRACE("Exiting aaruf_get_readable_media_tags() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    // Initialize all bytes to false (0)
    memset(buffer, 0, required_length);

    // Iterate through all media tag types and mark present ones as true
    for(int32_t tag_type = 0; tag_type <= MaxMediaTag; tag_type++)
    {
        mediaTagEntry *item = NULL;
        HASH_FIND_INT(ctx->mediaTags, &tag_type, item);

        if(item != NULL)
        {
            buffer[tag_type] = 1;
            TRACE("Media tag type %d is present", tag_type);
        }
    }

    *length = required_length;

    TRACE("Exiting aaruf_get_readable_media_tags(%p, %p, %zu) = AARUF_STATUS_OK", context, buffer, *length);
    return AARUF_STATUS_OK;
}
