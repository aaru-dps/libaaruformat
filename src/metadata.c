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
/**
 * @file metadata.c
 * @brief Read-only metadata accessors for libaaruformat.
 *
 * Contains all aaruf_get_* functions that retrieve metadata from an opened
 * AaruFormat image context. Writer-side functions (aaruf_set_*, aaruf_clear_*)
 * are in metadata_write.c.
 *
 * @see metadata_write.c
 */

#include <stddef.h>
#include <stdint.h>

#include "aaruformat.h"
#include "log.h"

/**
 * @brief Retrieves the logical CHS geometry from the AaruFormat image.
 *
 * Reads the Cylinder-Head-Sector (CHS) geometry information from the image's geometry
 * block and returns the values through output parameters. The geometry block contains
 * legacy-style logical addressing parameters that describe how the storage medium was
 * originally organized in terms of cylinders, heads (tracks per cylinder), and sectors
 * per track. This information is essential for software that requires CHS addressing
 * or for accurately representing the original medium's logical structure.
 *
 * @param context Pointer to the aaruformat context (must be a valid, opened image context).
 * @param cylinders Pointer to store the number of cylinders. Updated on success.
 * @param heads Pointer to store the number of heads (tracks per cylinder). Updated on success.
 * @param sectors_per_track Pointer to store the number of sectors per track. Updated on success.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully retrieved geometry information. This is returned when:
 *         - The context is valid and properly initialized
 *         - The geometry block is present in the image (identifier == GeometryBlock)
 *         - All three output parameters are successfully populated with geometry values
 *         - The cylinders parameter contains the total number of cylinders
 *         - The heads parameter contains the number of heads per cylinder
 *         - The sectors_per_track parameter contains the number of sectors per track
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_open() or aaruf_create()
 *
 * @retval AARUF_ERROR_CANNOT_READ_BLOCK (-7) The geometry block is not present. This occurs when:
 *         - The image was created without geometry information
 *         - The geometryBlock.identifier field doesn't equal GeometryBlock
 *         - The geometry block was not found during image opening
 *         - The image format doesn't support or require CHS geometry
 *
 * @note Geometry Interpretation:
 *       - Total logical sectors = cylinders × heads × sectors_per_track
 *       - Sector size is not included in the geometry block and must be obtained separately
 *         (typically 512 bytes for most block devices, but can vary)
 *       - The geometry represents logical addressing, not necessarily physical medium geometry
 *       - Modern storage devices often report translated or synthetic geometry values
 *
 * @note CHS Addressing Context:
 *       - CHS addressing was historically used for hard disk drives and floppy disks
 *       - Legacy BIOS and older operating systems relied on CHS parameters
 *       - LBA (Logical Block Addressing) has largely replaced CHS for modern devices
 *       - Some disk image formats and emulators still require CHS information
 *
 * @note Geometry Block Availability:
 *       - Not all image types contain geometry blocks
 *       - Optical media (CDs, DVDs) typically don't have CHS geometry
 *       - Modern large-capacity drives may not have meaningful CHS values
 *       - Check the return value to determine if geometry is available
 *
 * @note Parameter Validation:
 *       - All output parameters must be non-NULL valid pointers
 *       - The function does not validate the geometry values themselves
 *       - Geometry values of zero or unusually large values may indicate issues
 *
 * @warning The output parameters are only modified on success (AARUF_STATUS_OK).
 *          On error, their values remain unchanged. Initialize them before calling
 *          if default values are needed on failure.
 *
 * @warning This function reads from the in-memory geometry block loaded during
 *          aaruf_open(). It does not perform file I/O operations.
 *
 * @warning Geometry values may not accurately represent physical device geometry,
 *          especially for modern drives with zone-based recording or flash storage.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_geometry(const void *context, uint32_t *cylinders, uint32_t *heads,
                                                 uint32_t *sectors_per_track)
{
    TRACE("Entering aaruf_get_geometry(%p, %p, %p, %p)", context, cylinders, heads, sectors_per_track);

    const aaruformat_context *ctx = NULL;

    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_geometry() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_geometry() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->geometry_block.identifier != GeometryBlock)
    {
        FATAL("No geometry block present");

        TRACE("Exiting aaruf_get_geometry() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    *cylinders         = ctx->geometry_block.cylinders;
    *heads             = ctx->geometry_block.heads;
    *sectors_per_track = ctx->geometry_block.sectorsPerTrack;

    TRACE("Exiting aaruf_get_geometry(%p, %u, %u, %u) = AARUF_STATUS_OK", context, *cylinders, *heads,
          *sectors_per_track);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves the embedded CICM XML metadata sidecar from the image.
 *
 * CICM (Canary Islands Computer Museum) XML is a standardized metadata format used for documenting
 * preservation and archival information about media and disk images. This function extracts the
 * raw CICM XML payload that was embedded in the AaruFormat image during creation. The XML data is
 * preserved in its original form without parsing, interpretation, or validation by the library.
 * The metadata typically includes detailed information about the physical media, imaging process,
 * checksums, device information, and preservation metadata following the CICM schema.
 *
 * This function supports a two-call pattern for buffer size determination:
 * 1. First call with a buffer that may be too small returns AARUF_ERROR_BUFFER_TOO_SMALL
 *    and sets *length to the required size
 * 2. Second call with a properly sized buffer retrieves the actual data
 *
 * Alternatively, if the caller already knows the buffer is large enough, a single call
 * will succeed and populate the buffer with the CICM XML data.
 *
 * @param context Pointer to the aaruformat context (must be a valid, opened image context).
 * @param buffer Pointer to a buffer that will receive the CICM XML metadata. Must be large
 *               enough to hold the entire XML payload (at least *length bytes on input).
 *               The buffer will contain raw UTF-8 encoded XML data on success.
 * @param length Pointer to a size_t that serves dual purpose:
 *               - On input: size of the provided buffer in bytes
 *               - On output: actual size of the CICM XML metadata in bytes
 *               If the function returns AARUF_ERROR_BUFFER_TOO_SMALL, this will be updated
 *               to contain the required buffer size for a subsequent successful call.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully retrieved CICM XML metadata. This is returned when:
 *         - The context is valid and properly initialized
 *         - The CICM block is present in the image (identifier == CicmBlock)
 *         - The provided buffer is large enough (>= required length)
 *         - The CICM XML data is successfully copied to the buffer
 *         - The *length parameter is set to the actual size of the XML data
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_open() or aaruf_create()
 *
 * @retval AARUF_ERROR_CANNOT_READ_BLOCK (-7) The CICM block is not present. This occurs when:
 *         - The image was created without CICM XML metadata
 *         - ctx->cicmBlock is NULL (no data loaded)
 *         - ctx->cicmBlockHeader.length is 0 (empty metadata)
 *         - ctx->cicmBlockHeader.identifier doesn't equal CicmBlock
 *         - The CICM block was not found during image opening
 *         - The *length output parameter is set to 0 to indicate no data available
 *
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-10) The provided buffer is insufficient. This occurs when:
 *         - The input *length is less than ctx->cicmBlockHeader.length
 *         - The *length parameter is updated to contain the required buffer size
 *         - No data is copied to the buffer
 *         - The caller should allocate a larger buffer and call again
 *
 * @note CICM XML Format:
 *       - The XML is stored in UTF-8 encoding
 *       - The payload may or may not be null-terminated
 *       - The library treats the XML as opaque binary data
 *       - No XML parsing, interpretation, or validation is performed by libaaruformat
 *       - Schema validation and XML processing are the caller's responsibility
 *
 * @note CICM Metadata Purpose:
 *       - Developed by the Canary Islands Computer Museum for digital preservation
 *       - Documents comprehensive preservation metadata
 *       - Includes checksums for data integrity verification
 *       - Records detailed device and media information
 *       - Supports archival and long-term preservation requirements
 *       - Provides standardized metadata for digital preservation workflows
 *       - Used by cultural heritage institutions and archives
 *
 * @note Buffer Size Handling:
 *       - First call with insufficient buffer returns required size in *length
 *       - Caller allocates properly sized buffer based on returned length
 *       - Second call with adequate buffer retrieves the actual XML data
 *       - Single call succeeds if buffer is already large enough
 *
 * @note Data Availability:
 *       - CICM blocks are optional in AaruFormat images
 *       - Not all images will contain CICM metadata
 *       - The presence of CICM data depends on how the image was created
 *       - Check return value to handle missing metadata gracefully
 *
 * @warning The XML data may contain sensitive information about the imaging environment,
 *          personnel, locations, or media content. Handle appropriately for your use case.
 *
 * @warning This function reads from the in-memory CICM block loaded during aaruf_open().
 *          It does not perform file I/O operations. The entire CICM XML is kept in memory
 *          for the lifetime of the context.
 *
 * @warning The buffer parameter must be valid and large enough to hold the XML data.
 *          Passing a buffer smaller than the required size will result in
 *          AARUF_ERROR_BUFFER_TOO_SMALL with no partial data copied.
 *
 * @see CicmMetadataBlock for the on-disk structure definition.
 * @see aaruf_set_cicm_metadata() for embedding CICM XML during image creation.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_cicm_metadata(const void *context, uint8_t *buffer, size_t *length)
{
    TRACE("Entering aaruf_get_cicm_metadata(%p, %p, %p)", context, buffer, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_cicm_metadata() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_cicm_metadata() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->cicm_block == NULL || ctx->cicm_block_header.length == 0 || ctx->cicm_block_header.identifier != CicmBlock)
    {
        TRACE("No CICM XML metadata present");
        *length = 0;

        TRACE("Exiting aaruf_get_cicm_metadata() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    if(*length < ctx->cicm_block_header.length)
    {
        TRACE("Buffer too small for CICM XML metadata, required %u bytes", ctx->cicm_block_header.length);
        *length = ctx->cicm_block_header.length;

        TRACE("Exiting aaruf_get_cicm_metadata() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    *length = ctx->cicm_block_header.length;
    memcpy(buffer, ctx->cicm_block, ctx->cicm_block_header.length);

    TRACE("CICM XML metadata read successfully, length %u", *length);
    TRACE("Exiting aaruf_get_cicm_metadata(%p, %p, %d) = AARUF_STATUS_OK", context, buffer, *length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves the embedded Aaru metadata JSON from the image.
 *
 * Aaru metadata JSON is a structured metadata format that provides machine-readable, comprehensive
 * information about the image, media, imaging session details, hardware configuration, optical disc
 * tracks and sessions, checksums, and preservation metadata. This function extracts the raw JSON
 * payload that was embedded in the AaruFormat image during creation. The JSON data is preserved in
 * its original form without parsing or interpretation by the library, allowing callers to process
 * the structured metadata using standard JSON parsing libraries.
 *
 * This function supports a two-call pattern for buffer size determination:
 * 1. First call with a buffer that may be too small returns AARUF_ERROR_BUFFER_TOO_SMALL
 *    and sets *length to the required size
 * 2. Second call with a properly sized buffer retrieves the actual data
 *
 * Alternatively, if the caller already knows the buffer is large enough, a single call
 * will succeed and populate the buffer with the Aaru JSON data.
 *
 * @param context Pointer to the aaruformat context (must be a valid, opened image context).
 * @param buffer Pointer to a buffer that will receive the Aaru metadata JSON. Must be large
 *               enough to hold the entire JSON payload (at least *length bytes on input).
 *               The buffer will contain raw UTF-8 encoded JSON data on success.
 * @param length Pointer to a size_t that serves dual purpose:
 *               - On input: size of the provided buffer in bytes
 *               - On output: actual size of the Aaru metadata JSON in bytes
 *               If the function returns AARUF_ERROR_BUFFER_TOO_SMALL, this will be updated
 *               to contain the required buffer size for a subsequent successful call.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully retrieved Aaru metadata JSON. This is returned when:
 *         - The context is valid and properly initialized
 *         - The Aaru JSON block is present in the image (identifier == AaruMetadataJsonBlock)
 *         - The provided buffer is large enough (>= required length)
 *         - The Aaru JSON data is successfully copied to the buffer
 *         - The *length parameter is set to the actual size of the JSON data
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_open() or aaruf_create()
 *
 * @retval AARUF_ERROR_CANNOT_READ_BLOCK (-7) The Aaru JSON block is not present. This occurs when:
 *         - The image was created without Aaru metadata JSON
 *         - ctx->jsonBlock is NULL (no data loaded)
 *         - ctx->jsonBlockHeader.length is 0 (empty metadata)
 *         - ctx->jsonBlockHeader.identifier doesn't equal AaruMetadataJsonBlock
 *         - The Aaru JSON block was not found during image opening
 *         - The *length output parameter is set to 0 to indicate no data available
 *
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-10) The provided buffer is insufficient. This occurs when:
 *         - The input *length is less than ctx->jsonBlockHeader.length
 *         - The *length parameter is updated to contain the required buffer size
 *         - No data is copied to the buffer
 *         - The caller should allocate a larger buffer and call again
 *
 * @note Aaru JSON Format and Encoding:
 *       - The JSON payload is stored in UTF-8 encoding
 *       - The payload may or may not be null-terminated
 *       - The library treats the JSON as opaque binary data
 *       - No JSON parsing, interpretation, or validation is performed by libaaruformat
 *       - JSON schema validation and parsing are the caller's responsibility
 *
 * @note Aaru Metadata JSON Purpose:
 *       - Provides machine-readable structured metadata using modern JSON format
 *       - Includes comprehensive information about media, sessions, tracks, and checksums
 *       - Enables programmatic access to metadata without XML parsing overhead
 *       - Documents imaging session details, hardware configuration, and preservation data
 *       - Used by Aaru and compatible tools for metadata exchange and analysis
 *       - Complements or serves as alternative to CICM XML metadata
 *
 * @note Buffer Size Handling:
 *       - First call with insufficient buffer returns required size in *length
 *       - Caller allocates properly sized buffer based on returned length
 *       - Second call with adequate buffer retrieves the actual JSON data
 *       - Single call succeeds if buffer is already large enough
 *
 * @note Data Availability:
 *       - Aaru JSON blocks are optional in AaruFormat images
 *       - Not all images will contain Aaru metadata JSON
 *       - The presence of JSON data depends on how the image was created
 *       - Check return value to handle missing metadata gracefully
 *       - Images may contain CICM XML, Aaru JSON, both, or neither
 *
 * @note Distinction from CICM XML:
 *       - CICM XML follows the Canary Islands Computer Museum schema (older format)
 *       - Aaru JSON follows the Aaru-specific metadata schema (newer format)
 *       - Both can coexist in the same image file
 *       - Aaru JSON may provide more detailed or different metadata than CICM XML
 *       - Different tools and workflows may prefer one format over the other
 *
 * @warning This function reads from the in-memory Aaru JSON block loaded during aaruf_open().
 *          It does not perform file I/O operations. The entire JSON is kept in memory
 *          for the lifetime of the context.
 *
 * @warning The buffer parameter must be valid and large enough to hold the JSON data.
 *          Passing a buffer smaller than the required size will result in
 *          AARUF_ERROR_BUFFER_TOO_SMALL with no partial data copied.
 *
 * @warning This function does not validate JSON syntax or schema. Corrupted JSON data will
 *          be retrieved successfully and errors will only be detected when attempting to parse.
 *
 * @see AaruMetadataJsonBlockHeader for the on-disk structure definition.
 * @see aaruf_get_cicm_metadata() for retrieving CICM XML metadata.
 * @see process_aaru_metadata_json_block() for the loading process during image opening.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_aaru_json_metadata(const void *context, uint8_t *buffer, size_t *length)
{
    TRACE("Entering aaruf_get_aaru_json_metadata(%p, %p, %p)", context, buffer, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_aaru_json_metadata() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_aaru_json_metadata() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->json_block == NULL || ctx->json_block_header.length == 0 ||
       ctx->json_block_header.identifier != AaruMetadataJsonBlock)
    {
        TRACE("No Aaru metadata JSON present");
        *length = 0;

        TRACE("Exiting aaruf_get_aaru_json_metadata() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    if(*length < ctx->json_block_header.length)
    {
        TRACE("Buffer too small for Aaru metadata JSON, required %u bytes", ctx->json_block_header.length);
        *length = ctx->json_block_header.length;

        TRACE("Exiting aaruf_get_aaru_json_metadata() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    *length = ctx->json_block_header.length;
    memcpy(buffer, ctx->json_block, ctx->json_block_header.length);

    TRACE("Aaru metadata JSON read successfully, length %u", *length);
    TRACE("Exiting aaruf_get_aaru_json_metadata(%p, %p, %d) = AARUF_STATUS_OK", context, buffer, *length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves the media sequence metadata for multi-volume image sets.
 *
 * Reads the media sequence fields stored in the MetadataBlock header and returns the current media
 * number together with the final media number for the complete set. This information indicates the
 * position of the imaged medium within a multi-volume collection (for example, "disc 2 of 5"). The
 * function operates entirely on in-memory structures populated during aaruf_open(); no additional
 * disk I/O is performed.
 *
 * @param context Pointer to an initialized aaruformat context opened for reading or writing.
 * @param sequence Pointer that receives the current media sequence number (typically 1-based).
 * @param last_sequence Pointer that receives the total number of media in the set.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Metadata was present and the output parameters were populated.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The provided context pointer is NULL or not an
 *         aaruformat context (magic mismatch).
 * @retval AARUF_ERROR_METADATA_NOT_PRESENT (-30) The MetadataBlock was not present in the image,
 *         making sequence data unavailable.
 *
 * @note For standalone media, both sequence and last_sequence are commonly set to 1. Some creators
 *       may also set them to 0 to indicate the absence of sequence semantics; callers should handle
 *       either pattern gracefully.
 *
 * @note The function does not validate logical consistency (e.g., whether sequence <= last_sequence);
 *       it simply returns the values stored in the image header.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_media_sequence(const void *context, int32_t *sequence, int32_t *last_sequence)
{
    TRACE("Entering aaruf_get_media_sequence(%p, %p, %p)", context, sequence, last_sequence);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_sequence() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_sequence() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock)
    {
        FATAL("No metadata block present");

        TRACE("Exiting aaruf_set_media_sequence() = AARUF_ERROR_METADATA_NOT_PRESENT");
        return AARUF_ERROR_METADATA_NOT_PRESENT;
    }

    *sequence      = ctx->metadata_block_header.mediaSequence;
    *last_sequence = ctx->metadata_block_header.lastMediaSequence;

    TRACE("Exiting aaruf_set_media_sequence(%p, %d, %d) = AARUF_STATUS_OK", context, *sequence, *last_sequence);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves the recorded creator (operator) name from the MetadataBlock.
 *
 * Copies the UTF-16LE encoded creator string that identifies the person or operator who created the
 * image. The function supports the common two-call pattern: the caller first determines the required
 * buffer size by passing a buffer that may be NULL or too small, then allocates sufficient memory and
 * calls again to obtain the actual data. On success the buffer contains an opaque UTF-16LE string of
 * length *length bytes (not null-terminated).
 *
 * @param context Pointer to a valid aaruformat context opened for reading or writing.
 * @param buffer Pointer to the destination buffer that will receive the creator string. May be NULL
 *               to query the required size.
 * @param length Pointer to an int32_t that on input specifies the size of @p buffer in bytes and on
 *               output receives the actual length of the creator metadata.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) The creator string was copied successfully.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is NULL or not an aaruformat context.
 * @retval AARUF_ERROR_METADATA_NOT_PRESENT (-30) Creator metadata has not been recorded in the image.
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-10) The provided buffer was insufficient; *length contains
 *         the required size and no data was copied.
 *
 * @note The returned data is UTF-16LE encoded and may contain embedded null characters. Callers
 *       should treat it as an opaque byte array unless they explicitly handle UTF-16LE strings.
 *
 * @note The function does not allocate memory. Callers are responsible for ensuring @p buffer is
 *       large enough before requesting the data.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_creator(const void *context, uint8_t *buffer, int32_t *length)
{
    TRACE("Entering aaruf_get_creator(%p, %p, %p)", context, buffer, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_creator() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_creator() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock || ctx->creator == NULL ||
       ctx->metadata_block_header.creatorLength == 0)
    {
        FATAL("No metadata block present");

        TRACE("Exiting aaruf_get_creator() = AARUF_ERROR_METADATA_NOT_PRESENT");
        return AARUF_ERROR_METADATA_NOT_PRESENT;
    }

    if(buffer == NULL || *length < ctx->metadata_block_header.creatorLength)
    {
        *length = ctx->metadata_block_header.creatorLength;

        TRACE("Exiting aaruf_get_creator() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    // Copy opaque UTF-16LE string
    memcpy(buffer, ctx->creator, ctx->metadata_block_header.creatorLength);
    *length = ctx->metadata_block_header.creatorLength;

    TRACE("Exiting aaruf_get_creator(%p, %p, %d) = AARUF_STATUS_OK", context, buffer, *length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves the user comments or notes stored in the MetadataBlock.
 *
 * Provides access to the UTF-16LE encoded comments associated with the image. Comments are often
 * used for provenance notes, imaging details, or curator remarks. The function follows the same
 * two-call buffer sizing pattern used by other metadata retrieval APIs: the caller may probe the
 * required size before allocating memory.
 *
 * @param context Pointer to a valid aaruformat context opened with aaruf_open() or aaruf_create().
 * @param buffer Destination buffer that receives the comments data. May be NULL when probing size.
 * @param length Pointer to an int32_t. On input it contains the size of @p buffer in bytes; on output
 *               it is updated with the actual comments length.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Comments were available and copied successfully.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context pointer is invalid or not a libaaruformat context.
 * @retval AARUF_ERROR_METADATA_NOT_PRESENT (-30) No comments metadata exists in the image.
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-10) The supplied buffer was too small. *length is updated with
 *         the required size and no data is copied.
 *
 * @note Comments are stored exactly as provided during image creation and may include multi-line text
 *       or other control characters. No validation or normalization is applied by the library.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_comments(const void *context, uint8_t *buffer, int32_t *length)
{
    TRACE("Entering aaruf_get_comments(%p, %p, %p)", context, buffer, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_comments() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_comments() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock || ctx->comments == NULL ||
       ctx->metadata_block_header.commentsLength == 0)
    {
        FATAL("No metadata block present");

        TRACE("Exiting aaruf_get_comments() = AARUF_ERROR_METADATA_NOT_PRESENT");
        return AARUF_ERROR_METADATA_NOT_PRESENT;
    }

    if(buffer == NULL || *length < ctx->metadata_block_header.commentsLength)
    {
        *length = ctx->metadata_block_header.commentsLength;

        TRACE("Exiting aaruf_get_comments() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    // Copy opaque UTF-16LE string
    memcpy(buffer, ctx->comments, ctx->metadata_block_header.commentsLength);
    *length = ctx->metadata_block_header.commentsLength;

    TRACE("Exiting aaruf_get_comments(%p, %p, %d) = AARUF_STATUS_OK", context, buffer, *length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves the media title or label captured during image creation.
 *
 * Returns the UTF-16LE encoded media title string, representing markings or labels present on the
 * original physical media. This function allows applications to present or archive the media title
 * alongside the image, preserving contextual information from the physical artifact.
 *
 * @param context Pointer to a valid aaruformat context.
 * @param buffer Destination buffer for the UTF-16LE title string. May be NULL when querying size.
 * @param length Pointer to an int32_t that on input holds the buffer capacity in bytes and on output
 *               receives the actual title length.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) The media title was available and copied to @p buffer.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context pointer is invalid.
 * @retval AARUF_ERROR_METADATA_NOT_PRESENT (-30) No media title metadata exists.
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-10) The supplied buffer was insufficient.
 *
 * @note Titles may contain international characters, control codes, or mixed casing. The library does
 *       not attempt to sanitize or interpret the string.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_media_title(const void *context, uint8_t *buffer, int32_t *length)
{
    TRACE("Entering aaruf_get_media_title(%p, %p, %p)", context, buffer, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_media_title() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_media_title() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock || ctx->media_title == NULL ||
       ctx->metadata_block_header.mediaTitleLength == 0)
    {
        FATAL("No metadata block present");

        TRACE("Exiting aaruf_get_media_title() = AARUF_ERROR_METADATA_NOT_PRESENT");
        return AARUF_ERROR_METADATA_NOT_PRESENT;
    }

    if(buffer == NULL || *length < ctx->metadata_block_header.mediaTitleLength)
    {
        *length = ctx->metadata_block_header.mediaTitleLength;

        TRACE("Exiting aaruf_get_media_title() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    // Copy opaque UTF-16LE string
    memcpy(buffer, ctx->media_title, ctx->metadata_block_header.mediaTitleLength);
    *length = ctx->metadata_block_header.mediaTitleLength;

    TRACE("Exiting aaruf_get_media_title(%p, %p, %d) = AARUF_STATUS_OK", context, buffer, *length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves the recorded media manufacturer name.
 *
 * Provides access to the UTF-16LE encoded manufacturer metadata that identifies the company which
 * produced the physical medium. This information is taken from the MetadataBlock and mirrors the
 * value previously stored via aaruf_set_media_manufacturer().
 *
 * @param context Pointer to a valid aaruformat context.
 * @param buffer Buffer that receives the manufacturer string. May be NULL when probing size.
 * @param length Pointer to an int32_t specifying the buffer size on input and receiving the actual
 *               manufacturer string length on output.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Manufacturer metadata was available and copied.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context pointer is invalid.
 * @retval AARUF_ERROR_METADATA_NOT_PRESENT (-30) The image lacks manufacturer metadata.
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-10) The provided buffer was too small; *length indicates size.
 *
 * @note Values may include trailing spaces or vendor-specific capitalization. Treat the returned data
 *       as authoritative and avoid trimming unless required by the consuming application.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_media_manufacturer(const void *context, uint8_t *buffer, int32_t *length)
{
    TRACE("Entering aaruf_get_media_manufacturer(%p, %p, %p)", context, buffer, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_media_manufacturer() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_media_manufacturer() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock || ctx->media_manufacturer == NULL ||
       ctx->metadata_block_header.mediaManufacturerLength == 0)
    {
        FATAL("No metadata block present");

        TRACE("Exiting aaruf_get_media_manufacturer() = AARUF_ERROR_METADATA_NOT_PRESENT");
        return AARUF_ERROR_METADATA_NOT_PRESENT;
    }

    if(buffer == NULL || *length < ctx->metadata_block_header.mediaManufacturerLength)
    {
        *length = ctx->metadata_block_header.mediaManufacturerLength;

        TRACE("Exiting aaruf_get_media_manufacturer() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    // Copy opaque UTF-16LE string
    memcpy(buffer, ctx->media_manufacturer, ctx->metadata_block_header.mediaManufacturerLength);
    *length = ctx->metadata_block_header.mediaManufacturerLength;

    TRACE("Exiting aaruf_get_media_manufacturer(%p, %p, %d) = AARUF_STATUS_OK", context, buffer, *length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves the media model or product designation metadata.
 *
 * Returns the UTF-16LE encoded model name that specifies the exact product variant of the physical
 * medium. The function mirrors the set counterpart and is useful for accurately documenting media
 * specifications during preservation workflows.
 *
 * @param context Pointer to a valid aaruformat context.
 * @param buffer Destination buffer for the model string; may be NULL when querying required size.
 * @param length Pointer to an int32_t that on input contains the buffer capacity in bytes and on
 *               output is updated with the actual metadata length.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Model metadata was successfully copied.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context pointer is invalid.
 * @retval AARUF_ERROR_METADATA_NOT_PRESENT (-30) No media model metadata is present in the image.
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-10) The caller-provided buffer is too small.
 *
 * @note Model strings often contain performance ratings (e.g., "16x", "LTO-7"). The data is opaque and
 *       should be handled without modification unless necessary.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_media_model(const void *context, uint8_t *buffer, int32_t *length)
{
    TRACE("Entering aaruf_get_media_model(%p, %p, %p)", context, buffer, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_media_model() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_media_model() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock || ctx->media_model == NULL ||
       ctx->metadata_block_header.mediaModelLength == 0)
    {
        FATAL("No metadata block present");

        TRACE("Exiting aaruf_get_media_model() = AARUF_ERROR_METADATA_NOT_PRESENT");
        return AARUF_ERROR_METADATA_NOT_PRESENT;
    }

    if(buffer == NULL || *length < ctx->metadata_block_header.mediaModelLength)
    {
        *length = ctx->metadata_block_header.mediaModelLength;

        TRACE("Exiting aaruf_get_media_model() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    // Copy opaque UTF-16LE string
    memcpy(buffer, ctx->media_model, ctx->metadata_block_header.mediaModelLength);
    *length = ctx->metadata_block_header.mediaModelLength;

    TRACE("Exiting aaruf_get_media_model(%p, %p, %d) = AARUF_STATUS_OK", context, buffer, *length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves the media serial number recorded in the image metadata.
 *
 * Copies the UTF-16LE encoded serial number identifying the specific physical medium. Serial numbers
 * are particularly important for forensic tracking and archival provenance, enabling correlation
 * between the physical item and its digital representation.
 *
 * @param context Pointer to a valid aaruformat context.
 * @param buffer Destination buffer for the serial number. May be NULL to determine required size.
 * @param length Pointer to an int32_t that on input holds the buffer size and on output receives the
 *               actual serial number length.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Serial number metadata was copied into @p buffer.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context pointer is invalid.
 * @retval AARUF_ERROR_METADATA_NOT_PRESENT (-30) No serial number metadata was stored in the image.
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-10) The provided buffer was too small.
 *
 * @note Serial numbers may contain spaces, hyphens, or alphanumeric characters. The library does not
 *       normalize or validate these strings.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_media_serial_number(const void *context, uint8_t *buffer, int32_t *length)
{
    TRACE("Entering aaruf_get_media_serial_number(%p, %p, %p)", context, buffer, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_media_serial_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_media_serial_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock || ctx->media_serial_number == NULL ||
       ctx->metadata_block_header.mediaSerialNumberLength == 0)
    {
        FATAL("No metadata block present");

        TRACE("Exiting aaruf_get_media_serial_number() = AARUF_ERROR_METADATA_NOT_PRESENT");
        return AARUF_ERROR_METADATA_NOT_PRESENT;
    }

    if(buffer == NULL || *length < ctx->metadata_block_header.mediaSerialNumberLength)
    {
        *length = ctx->metadata_block_header.mediaSerialNumberLength;

        TRACE("Exiting aaruf_get_media_serial_number() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    // Copy opaque UTF-16LE string
    memcpy(buffer, ctx->media_serial_number, ctx->metadata_block_header.mediaSerialNumberLength);
    *length = ctx->metadata_block_header.mediaSerialNumberLength;

    TRACE("Exiting aaruf_get_media_serial_number(%p, %p, %d) = AARUF_STATUS_OK", context, buffer, *length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves the barcode assigned to the physical media or its packaging.
 *
 * Returns the UTF-16LE encoded barcode string that was captured when the image was created. Barcodes
 * are commonly used in institutional workflows for inventory tracking and automated retrieval.
 *
 * @param context Pointer to a valid aaruformat context.
 * @param buffer Buffer that receives the barcode string; may be NULL when probing size requirements.
 * @param length Pointer to an int32_t specifying the buffer size on input and receiving the actual
 *               barcode length on output.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Barcode metadata was present and copied successfully.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context pointer is invalid.
 * @retval AARUF_ERROR_METADATA_NOT_PRESENT (-30) No barcode metadata exists in the image.
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-10) The supplied buffer was too small.
 *
 * @note Barcode values can be strict alphanumeric codes (e.g., LTO cartridge IDs) or full strings from
 *       custom labeling systems. Preserve the returned string exactly for catalog interoperability.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_media_barcode(const void *context, uint8_t *buffer, int32_t *length)
{
    TRACE("Entering aaruf_get_media_barcode(%p, %p, %p)", context, buffer, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_media_barcode() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_media_barcode() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock || ctx->media_barcode == NULL ||
       ctx->metadata_block_header.mediaBarcodeLength == 0)
    {
        FATAL("No metadata block present");

        TRACE("Exiting aaruf_get_media_barcode() = AARUF_ERROR_METADATA_NOT_PRESENT");
        return AARUF_ERROR_METADATA_NOT_PRESENT;
    }

    if(buffer == NULL || *length < ctx->metadata_block_header.mediaBarcodeLength)
    {
        *length = ctx->metadata_block_header.mediaBarcodeLength;

        TRACE("Exiting aaruf_get_media_barcode() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    // Copy opaque UTF-16LE string
    memcpy(buffer, ctx->media_barcode, ctx->metadata_block_header.mediaBarcodeLength);
    *length = ctx->metadata_block_header.mediaBarcodeLength;

    TRACE("Exiting aaruf_get_media_barcode(%p, %p, %d) = AARUF_STATUS_OK", context, buffer, *length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves the media part number recorded in the MetadataBlock.
 *
 * Provides access to the UTF-16LE encoded part number identifying the precise catalog or ordering
 * code for the physical medium. Part numbers help archivists procure exact replacements and document
 * specific media variants used during acquisition.
 *
 * @param context Pointer to a valid aaruformat context.
 * @param buffer Destination buffer for the part number string. May be NULL while querying size.
 * @param length Pointer to an int32_t that supplies the buffer size on input and is updated with the
 *               actual part number length on output.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Part number metadata was returned successfully.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context pointer is invalid.
 * @retval AARUF_ERROR_METADATA_NOT_PRESENT (-30) No part number metadata exists.
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-10) The provided buffer was insufficient; *length contains the
 *         required size.
 *
 * @note Part numbers may include manufacturer-specific formatting such as hyphens or suffix letters.
 *       The library stores and returns the data verbatim.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_media_part_number(const void *context, uint8_t *buffer, int32_t *length)
{
    TRACE("Entering aaruf_get_media_part_number(%p, %p, %p)", context, buffer, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_media_part_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_media_part_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock || ctx->media_part_number == NULL ||
       ctx->metadata_block_header.mediaPartNumberLength == 0)
    {
        FATAL("No metadata block present");

        TRACE("Exiting aaruf_get_media_part_number() = AARUF_ERROR_METADATA_NOT_PRESENT");
        return AARUF_ERROR_METADATA_NOT_PRESENT;
    }

    if(buffer == NULL || *length < ctx->metadata_block_header.mediaPartNumberLength)
    {
        *length = ctx->metadata_block_header.mediaPartNumberLength;

        TRACE("Exiting aaruf_get_media_part_number() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    // Copy opaque UTF-16LE string
    memcpy(buffer, ctx->media_part_number, ctx->metadata_block_header.mediaPartNumberLength);
    *length = ctx->metadata_block_header.mediaPartNumberLength;

    TRACE("Exiting aaruf_get_media_part_number(%p, %p, %d) = AARUF_STATUS_OK", context, buffer, *length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves the drive manufacturer metadata captured during imaging.
 *
 * Copies the UTF-16LE encoded manufacturer name of the device used to read or write the medium. This
 * information documents the hardware involved in the imaging process, which is crucial for forensic
 * reporting and reproducibility studies.
 *
 * @param context Pointer to a valid aaruformat context.
 * @param buffer Destination buffer for the manufacturer string. May be NULL when querying required
 *               length.
 * @param length Pointer to an int32_t specifying the buffer size on input and receiving the actual
 *               metadata length on output.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Drive manufacturer metadata was copied successfully.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context pointer is invalid.
 * @retval AARUF_ERROR_METADATA_NOT_PRESENT (-30) The image lacks drive manufacturer metadata.
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-10) The provided buffer is too small; *length holds the
 *         required size for a subsequent call.
 *
 * @note The returned manufacturer string corresponds to the value recorded by aaruf_set_drive_manufacturer()
 *       and may include branding or OEM designations.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_drive_manufacturer(const void *context, uint8_t *buffer, int32_t *length)
{
    TRACE("Entering aaruf_get_drive_manufacturer(%p, %p, %p)", context, buffer, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_drive_manufacturer() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_drive_manufacturer() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock || ctx->drive_manufacturer == NULL ||
       ctx->metadata_block_header.driveManufacturerLength == 0)
    {
        FATAL("No metadata block present");

        TRACE("Exiting aaruf_get_drive_manufacturer() = AARUF_ERROR_METADATA_NOT_PRESENT");
        return AARUF_ERROR_METADATA_NOT_PRESENT;
    }

    if(buffer == NULL || *length < ctx->metadata_block_header.driveManufacturerLength)
    {
        *length = ctx->metadata_block_header.driveManufacturerLength;

        TRACE("Exiting aaruf_get_drive_manufacturer() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    // Copy opaque UTF-16LE string
    memcpy(buffer, ctx->drive_manufacturer, ctx->metadata_block_header.driveManufacturerLength);
    *length = ctx->metadata_block_header.driveManufacturerLength;

    TRACE("Exiting aaruf_get_drive_manufacturer(%p, %p, %d) = AARUF_STATUS_OK", context, buffer, *length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves the device model information for the imaging drive.
 *
 * Returns the UTF-16LE encoded model identifier for the drive used during acquisition. The model
 * metadata provides finer granularity than the manufacturer name, enabling detailed documentation of
 * imaging hardware capabilities and behavior.
 *
 * @param context Pointer to a valid aaruformat context.
 * @param buffer Buffer that receives the model string; may be NULL while probing required capacity.
 * @param length Pointer to an int32_t indicating buffer size on input and receiving the metadata length
 *               on output.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Drive model metadata was available and copied.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context pointer is invalid.
 * @retval AARUF_ERROR_METADATA_NOT_PRESENT (-30) No drive model metadata exists in the image.
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-10) The supplied buffer was insufficient; *length is updated.
 *
 * @note Model strings can include firmware suffixes, interface hints, or OEM variations. Consume the
 *       data verbatim to maintain accurate provenance records.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_drive_model(const void *context, uint8_t *buffer, int32_t *length)
{
    TRACE("Entering aaruf_get_drive_model(%p, %p, %p)", context, buffer, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_drive_model() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_drive_model() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock || ctx->drive_model == NULL ||
       ctx->metadata_block_header.driveModelLength == 0)
    {
        FATAL("No metadata block present");

        TRACE("Exiting aaruf_get_drive_model() = AARUF_ERROR_METADATA_NOT_PRESENT");
        return AARUF_ERROR_METADATA_NOT_PRESENT;
    }

    if(buffer == NULL || *length < ctx->metadata_block_header.driveModelLength)
    {
        *length = ctx->metadata_block_header.driveModelLength;

        TRACE("Exiting aaruf_get_drive_model() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    // Copy opaque UTF-16LE string
    memcpy(buffer, ctx->drive_model, ctx->metadata_block_header.driveModelLength);
    *length = ctx->metadata_block_header.driveModelLength;

    TRACE("Exiting aaruf_get_drive_model(%p, %p, %d) = AARUF_STATUS_OK", context, buffer, *length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves the imaging drive's serial number metadata.
 *
 * Copies the UTF-16LE encoded serial number reported for the drive used during the imaging session.
 * This metadata enables correlation between images and specific hardware units for forensic chain of
 * custody or quality assurance workflows.
 *
 * @param context Pointer to a valid aaruformat context.
 * @param buffer Destination buffer for the serial number; may be NULL when querying size.
 * @param length Pointer to an int32_t carrying the buffer size on input and receiving the actual
 *               serial number length on output.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Drive serial number metadata was copied to @p buffer.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context pointer is invalid.
 * @retval AARUF_ERROR_METADATA_NOT_PRESENT (-30) No drive serial number metadata is available.
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-10) The provided buffer was insufficient.
 *
 * @note Serial numbers are stored exactly as returned by the imaging hardware and may include leading
 *       zeros or spacing that should be preserved.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_drive_serial_number(const void *context, uint8_t *buffer, int32_t *length)
{
    TRACE("Entering aaruf_get_drive_serial_number(%p, %p, %p)", context, buffer, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_drive_serial_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_drive_serial_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock || ctx->drive_serial_number == NULL ||
       ctx->metadata_block_header.driveSerialNumberLength == 0)
    {
        FATAL("No metadata block present");

        TRACE("Exiting aaruf_get_drive_serial_number() = AARUF_ERROR_METADATA_NOT_PRESENT");
        return AARUF_ERROR_METADATA_NOT_PRESENT;
    }

    if(buffer == NULL || *length < ctx->metadata_block_header.driveSerialNumberLength)
    {
        *length = ctx->metadata_block_header.driveSerialNumberLength;

        TRACE("Exiting aaruf_get_drive_serial_number() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    // Copy opaque UTF-16LE string
    memcpy(buffer, ctx->drive_serial_number, ctx->metadata_block_header.driveSerialNumberLength);
    *length = ctx->metadata_block_header.driveSerialNumberLength;

    TRACE("Exiting aaruf_get_drive_serial_number(%p, %p, %d) = AARUF_STATUS_OK", context, buffer, *length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves the firmware revision metadata for the imaging drive.
 *
 * Returns the UTF-16LE encoded firmware revision string that was captured when the image was created.
 * Firmware information is critical for reproducing imaging environments and diagnosing drive-specific
 * behavior or bugs.
 *
 * @param context Pointer to a valid aaruformat context.
 * @param buffer Destination buffer for the firmware revision string. May be NULL when probing size.
 * @param length Pointer to an int32_t that specifies the buffer capacity in bytes on input and is
 *               updated with the actual metadata length on output.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Firmware revision metadata was present and copied successfully.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context pointer is invalid.
 * @retval AARUF_ERROR_METADATA_NOT_PRESENT (-30) No firmware metadata exists in the image.
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-10) The supplied buffer was too small; *length is updated.
 *
 * @note Firmware revision formats vary between manufacturers (e.g., numeric, alphanumeric, dot-separated).
 *       The library stores the data verbatim without attempting normalization.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_drive_firmware_revision(const void *context, uint8_t *buffer, int32_t *length)
{
    TRACE("Entering aaruf_get_drive_firmware_revision(%p, %p, %p)", context, buffer, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_drive_firmware_revision() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_drive_firmware_revision() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock || ctx->drive_firmware_revision == NULL ||
       ctx->metadata_block_header.driveFirmwareRevisionLength == 0)
    {
        FATAL("No metadata block present");

        TRACE("Exiting aaruf_get_drive_firmware_revision() = AARUF_ERROR_METADATA_NOT_PRESENT");
        return AARUF_ERROR_METADATA_NOT_PRESENT;
    }

    if(buffer == NULL || *length < ctx->metadata_block_header.driveFirmwareRevisionLength)
    {
        *length = ctx->metadata_block_header.driveFirmwareRevisionLength;

        TRACE("Exiting aaruf_get_drive_firmware_revision() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    // Copy opaque UTF-16LE string
    memcpy(buffer, ctx->drive_firmware_revision, ctx->metadata_block_header.driveFirmwareRevisionLength);
    *length = ctx->metadata_block_header.driveFirmwareRevisionLength;

    TRACE("Exiting aaruf_get_drive_firmware_revision(%p, %p, %d) = AARUF_STATUS_OK", context, buffer, *length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves the total number of user-accessible sectors in the AaruFormat image.
 *
 * Returns the count of standard user data sectors in the image, excluding any negative (pre-gap)
 * or overflow (post-gap) sectors. This represents the primary addressable sector range that contains
 * the main user data, typically corresponding to the logical capacity of the storage medium as it
 * would be seen by an operating system or file system. For optical media, this excludes lead-in and
 * lead-out areas. For hard disks, this represents the standard LBA-addressable range.
 *
 * @param context Pointer to a valid aaruformat context (must be properly initialized).
 * @param sectors Pointer to a uint64_t that receives the total user sector count on success.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully retrieved the user sector count. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context magic number matches AARU_MAGIC
 *         - The sectors parameter is successfully populated with the user sector count
 *         - The value is taken from ctx->user_data_ddt_header.blocks
 *         - For block devices: sectors typically equals the total capacity in sectors
 *         - For optical media: sectors represents the user data area excluding lead-in/lead-out
 *         - For tape media: sectors may represent the total block count across all files
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_open() or aaruf_create()
 *
 * @note Sector Range Context:
 *       - User sectors represent the standard addressable range: 0 to (user_sectors - 1)
 *       - Total addressable sectors = negative_sectors + user_sectors + overflow_sectors
 *       - Negative sectors precede user sectors (pre-gap, lead-in)
 *       - Overflow sectors follow user sectors (post-gap, lead-out)
 *       - Use aaruf_get_negative_sectors() to get the negative sector count
 *       - Use aaruf_get_overflow_sectors() to get the overflow sector count
 *
 * @note Media Type Considerations:
 *       - **Optical Media (CD/DVD/BD)**: User sectors exclude lead-in and lead-out areas.
 *         Negative sectors may contain TOC and pre-gap data. Overflow sectors may contain
 *         post-gap and lead-out data.
 *       - **Hard Disk Drives**: User sectors represent the full LBA range. Negative and
 *         overflow sectors are typically zero unless capturing special areas.
 *       - **Floppy Disks**: User sectors represent the standard formatted capacity.
 *       - **Tape Media**: User sectors may represent the total block count. Negative and
 *         overflow sectors are typically not used for tape.
 *
 * @note DDT Header Source:
 *       - The value is retrieved from ctx->user_data_ddt_header.blocks
 *       - The DDT (Deduplication and Data Table) header tracks sector allocation
 *       - This field is populated during image creation with aaruf_create()
 *       - The value is fixed for read-only images opened with aaruf_open()
 *       - For write-enabled images, this represents the allocated capacity
 *
 * @note Addressing and I/O Operations:
 *       - When reading/writing sectors with aaruf_read_sector() or aaruf_write_sector():
 *         - Set negative=false and use sector_address in range [0, user_sectors - 1]
 *         - For negative sectors: set negative=true, sector_address in [0, negative_sectors - 1]
 *         - For overflow sectors: set negative=false, sector_address in [user_sectors, user_sectors + overflow_sectors
 * - 1]
 *
 * @note Relationship to Image Creation:
 *       - The user_sectors value is specified when calling aaruf_create()
 *       - It should match the logical capacity of the medium being imaged
 *       - For forensic images, ensure it matches the source medium exactly
 *       - For virtual disks, set it to the desired capacity
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_user_sectors(const void *context, uint64_t *sectors)
{
    TRACE("Entering aaruf_get_user_sectors(%p, %p)", context, sectors);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_user_sectors() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_user_sectors() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    *sectors = ctx->user_data_ddt_header.blocks;

    TRACE("Exiting aaruf_get_user_sectors(%p, %llu) = AARUF_STATUS_OK", context, *sectors);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves the number of negative (pre-gap) sectors in the AaruFormat image.
 *
 * Returns the count of negative sectors that precede the standard user data area. Negative sectors
 * are used to capture pre-gap data, lead-in areas, and other metadata that exists before the main
 * user-accessible storage region. This is particularly important for optical media (CD, DVD, BD)
 * where the lead-in contains the Table of Contents (TOC) and other essential disc structures, and
 * for audio CDs where pre-gap sectors contain silence or hidden tracks. For most hard disk and
 * floppy disk images, this value is typically zero.
 *
 * @param context Pointer to a valid aaruformat context (must be properly initialized).
 * @param sectors Pointer to a uint16_t that receives the negative sector count on success.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully retrieved the negative sector count. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context magic number matches AARU_MAGIC
 *         - The sectors parameter is successfully populated with the negative sector count
 *         - The value is taken from ctx->user_data_ddt_header.negative
 *         - For optical media with lead-in data: sectors may be non-zero
 *         - For standard hard disk/floppy images: sectors is typically 0
 *         - Maximum value is 65,535 (uint16_t limit)
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_open() or aaruf_create()
 *
 * @note Negative Sector Addressing:
 *       - Negative sectors are addressed with the 'negative' flag set to true
 *       - Sector addresses range from 0 to (negative_sectors - 1)
 *       - When calling aaruf_read_sector() or aaruf_write_sector():
 *         - Use negative=true
 *         - Use sector_address in range [0, negative_sectors - 1]
 *         - The actual logical position is before the user data area
 *
 * @note Optical Media Context:
 *       - **CD-ROM/CD-DA**: Negative sectors contain lead-in area with TOC (Table of Contents).
 *         The lead-in typically spans LBA -450000 to -1, though only a portion may be captured.
 *         Pre-gap sectors (usually 150 sectors/2 seconds before each track) may also be stored
 *         as negative sectors for the first track.
 *       - **DVD**: May contain lead-in with disc structure information, copyright data, and
 *         region codes. The lead-in area varies by format (DVD-ROM, DVD-R, DVD+R, etc.).
 *       - **Blu-ray**: Lead-in contains disc information, burst cutting area (BCA), and other
 *         metadata. The structure differs between BD-ROM, BD-R, and BD-RE.
 *
 * @note Hard Disk and Floppy Context:
 *       - Hard disk drives: Negative sectors are typically zero unless capturing special
 *         manufacturer reserved areas (HPA, DCO) that precede the standard user area.
 *       - Floppy disks: Negative sectors are typically zero as floppies have a simple
 *         linear sector layout without lead-in areas.
 *
 * @note Audio CD Hidden Tracks:
 *       - Some audio CDs contain hidden tracks in the pre-gap of the first track
 *       - These pre-gap sectors can extend up to several minutes before track 1
 *       - Negative sectors can capture this "hidden" audio data
 *       - The pre-gap for track 1 typically starts at LBA -150 (2 seconds)
 *
 * @note DDT Header Source:
 *       - The value is retrieved from ctx->user_data_ddt_header.negative
 *       - The DDT (Deduplication and Data Table) header tracks all sector allocation
 *       - This field is populated during image creation with aaruf_create()
 *       - The value is fixed for read-only images opened with aaruf_open()
 *       - Maximum representable value is 65,535 (uint16_t)
 *
 * @note Total Addressable Space:
 *       - Total sectors = negative_sectors + user_sectors + overflow_sectors
 *       - The negative region comes first in logical order
 *       - Followed by the user region [0, user_sectors - 1]
 *       - Followed by the overflow region if present
 *
 * @note Relationship to Image Creation:
 *       - The negative_sectors value is specified when calling aaruf_create()
 *       - It should be set based on the medium type and imaging requirements:
 *         - Optical discs: Set to the number of lead-in sectors captured
 *         - Hard disks: Typically 0, unless capturing HPA/DCO areas
 *         - Floppy disks: Typically 0
 *         - Audio CDs: May be non-zero to capture pre-gap hidden tracks
 *
 * @warning The sectors parameter is only modified on success (AARUF_STATUS_OK).
 *          On error, its value remains unchanged. Initialize it before calling
 *          if a default value is needed on failure.
 *
 * @warning This function reads from the in-memory DDT header loaded during
 *          aaruf_open() or set during aaruf_create(). It does not perform file
 *          I/O operations and executes quickly.
 *
 * @warning The maximum negative sector count is 65,535 due to the uint16_t storage type.
 *          If imaging optical media with larger lead-in areas, some data may not be
 *          representable. This limit is generally sufficient for most practical cases.
 *
 * @warning Negative sector data may contain copy-protected or encrypted content
 *          (e.g., CSS on DVDs, AACS on Blu-rays). Handle this data according to
 *          applicable laws and licensing agreements.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_negative_sectors(const void *context, uint32_t *sectors)
{
    TRACE("Entering aaruf_get_negative_sectors(%p, %p)", context, sectors);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_negative_sectors() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_negative_sectors() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    *sectors = ctx->user_data_ddt_header.negative;

    TRACE("Exiting aaruf_get_negative_sectors(%p, %u) = AARUF_STATUS_OK", context, *sectors);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves the number of overflow (post-gap) sectors in the AaruFormat image.
 *
 * Returns the count of overflow sectors that follow the standard user data area. Overflow sectors
 * are used to capture post-gap data, lead-out areas, and other metadata that exists after the main
 * user-accessible storage region. This is particularly important for optical media (CD, DVD, BD)
 * where the lead-out marks the physical end of the recorded data and contains disc finalization
 * information, and for multi-session discs where gaps between sessions need to be preserved. For
 * most hard disk and floppy disk images, this value is typically zero.
 *
 * @param context Pointer to a valid aaruformat context (must be properly initialized).
 * @param sectors Pointer to a uint16_t that receives the overflow sector count on success.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully retrieved the overflow sector count. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context magic number matches AARU_MAGIC
 *         - The sectors parameter is successfully populated with the overflow sector count
 *         - The value is taken from ctx->user_data_ddt_header.overflow
 *         - For optical media with lead-out data: sectors may be non-zero
 *         - For standard hard disk/floppy images: sectors is typically 0
 *         - Maximum value is 65,535 (uint16_t limit)
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_open() or aaruf_create()
 *
 * @note Overflow Sector Addressing:
 *       - Overflow sectors are addressed with the 'negative' flag set to false
 *       - Sector addresses range from user_sectors to (user_sectors + overflow_sectors - 1)
 *       - When calling aaruf_read_sector() or aaruf_write_sector():
 *         - Use negative=false
 *         - Use sector_address in range [user_sectors, user_sectors + overflow_sectors - 1]
 *         - The actual logical position is after the user data area
 *
 * @note Optical Media Context:
 *       - **CD-ROM/CD-DA**: Overflow sectors contain the lead-out area, which marks the physical
 *         end of the disc's recorded data. The lead-out consists of unreadable sectors filled
 *         with specific patterns.
 *       - **DVD**: May contain lead-out with disc finalization data, middle area (for dual-layer),
 *         and outer zone. DVD+R/RW discs may have substantial lead-out areas.
 *       - **Blu-ray**: Lead-out contains disc finalization markers and padding. For multi-layer
 *         discs, may include middle zones and outer areas.
 *
 * @note Multi-Session and Track Context:
 *       - Multi-session optical discs have gaps between sessions
 *       - Audio CDs with post-gap after the last track may use overflow sectors
 *       - Track post-gaps (silence after audio tracks) typically 2 seconds/150 sectors
 *
 * @note Hard Disk and Floppy Context:
 *       - Hard disk drives: Overflow sectors are typically zero unless capturing special
 *         manufacturer reserved areas (like DCO or HPA) that follow the standard user area.
 *       - Floppy disks: Overflow sectors are typically zero as floppies have a simple
 *         linear sector layout without lead-out areas. They may contain mastering information.
 *       - Some proprietary copy protection schemes may place data beyond the normal
 *         capacity, which could be captured as overflow sectors.
 *
 * @note DDT Header Source:
 *       - The value is retrieved from ctx->user_data_ddt_header.overflow
 *       - The DDT (Deduplication and Data Table) header tracks all sector allocation
 *       - This field is populated during image creation with aaruf_create()
 *       - The value is fixed for read-only images opened with aaruf_open()
 *       - Maximum representable value is 65,535 (uint16_t)
 *
 * @note Total Addressable Space:
 *       - Total sectors = negative_sectors + user_sectors + overflow_sectors
 *       - The negative region comes first in logical order
 *       - Followed by the user region [0, user_sectors - 1]
 *       - Followed by the overflow region at the end
 *       - Overflow represents the final addressable range in the image
 *
 * @note Relationship to Image Creation:
 *       - The overflow_sectors value is specified when calling aaruf_create()
 *       - It should be set based on the medium type and imaging requirements:
 *         - Optical discs: Set to the number of lead-out sectors captured
 *         - Multi-session discs: May include inter-session gaps
 *         - Hard disks: Typically 0, unless capturing post-user reserved areas
 *         - Floppy disks: Typically 0
 *         - Copy-protected media: May be non-zero to capture protection schemes
 *
 * @note Forensic Imaging Considerations:
 *       - Some copy protection schemes intentionally place data in overflow regions
 *       - These "overburn" areas extend beyond the disc's rated capacity
 *       - Overflow sectors ensure complete forensic capture of all readable data
 *       - Important for authenticity verification and copy protection analysis
 *
 * @warning The sectors parameter is only modified on success (AARUF_STATUS_OK).
 *          On error, its value remains unchanged. Initialize it before calling
 *          if a default value is needed on failure.
 *
 * @warning This function reads from the in-memory DDT header loaded during
 *          aaruf_open() or set during aaruf_create(). It does not perform file
 *          I/O operations and executes quickly.
 *
 * @warning The maximum overflow sector count is 65,535 due to the uint16_t storage type.
 *          If imaging optical media with larger lead-out areas or extensive overburn
 *          regions, some data may not be representable. This limit is generally
 *          sufficient for most practical cases.
 *
 * @warning Overflow sector data may be difficult or impossible to read on some drives,
 *          as it often resides in lead-out areas or beyond rated capacity. The presence
 *          of overflow sectors in an image indicates the imaging drive was capable of
 *          reading these extended areas, but other drives may not be able to access them.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_overflow_sectors(const void *context, uint32_t *sectors)
{
    TRACE("Entering aaruf_get_overflow_sectors(%p, %p)", context, sectors);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_overflow_sectors() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_overflow_sectors() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    *sectors = ctx->user_data_ddt_header.overflow;

    TRACE("Exiting aaruf_get_overflow_sectors(%p, %u) = AARUF_STATUS_OK", context, *sectors);
    return AARUF_STATUS_OK;
}

/**
 * @brief Retrieves a deep copy of the ImageInfo structure from the AaruFormat image.
 *
 * Returns a complete copy of the high-level image information summary containing
 * metadata such as image size, sector count, sector size, version information,
 * creation timestamps, and media type. This function performs a deep copy of all
 * fields including string buffers, ensuring the caller receives a complete,
 * independent copy of the image information.
 *
 * @param context Pointer to the aaruformat context (must be a valid, opened image context).
 * @param image_info Pointer to an ImageInfo structure to receive the copied data.
 *                   Must be a valid pointer to allocated memory.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully copied image info. The image_info parameter
 *         contains a complete copy of all fields including:
 *         - HasPartitions: Whether image contains partitions/tracks
 *         - HasSessions: Whether image contains multiple sessions
 *         - ImageSize: Size of image payload in bytes
 *         - Sectors: Total count of addressable sectors/blocks
 *         - SectorSize: Size of each logical sector in bytes
 *         - Version: Image format version string (NUL-terminated)
 *         - Application: Creating application name (NUL-terminated)
 *         - ApplicationVersion: Application version string (NUL-terminated)
 *         - CreationTime: Image creation timestamp (Windows FILETIME)
 *         - LastModificationTime: Last modification timestamp (Windows FILETIME)
 *         - MediaType: Media type identifier
 *         - MetadataMediaType: Media type for sidecar generation
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC
 *         - The context was not properly initialized
 *
 * @note The ImageInfo structure contains fixed-size character arrays that are
 *       properly NUL-terminated, making it safe to use as C strings.
 *
 * @note This function performs a complete deep copy using memcpy, copying all
 *       fields including strings, integers, and timestamps.
 *
 * @note The caller is responsible for allocating the ImageInfo structure before
 *       calling this function. The structure is not dynamically allocated by
 *       this function.
 *
 * @warning The image_info parameter must point to valid, allocated memory of
 *          at least sizeof(ImageInfo) bytes. Passing NULL or invalid pointers
 *          will result in undefined behavior.
 *
 * @warning This function reads from the in-memory image_info loaded during
 *          aaruf_open() or populated during aaruf_create(). It does not perform
 *          file I/O operations.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_image_info(const void *context, ImageInfo *image_info)
{
    TRACE("Entering aaruf_get_image_info(%p, %p)", context, image_info);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_image_info() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_image_info() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(image_info == NULL)
    {
        FATAL("image_info parameter is NULL");

        TRACE("Exiting aaruf_get_image_info() = AARUF_ERROR_INCORRECT_DATA_SIZE");
        return AARUF_ERROR_INCORRECT_DATA_SIZE;
    }

    // Perform deep copy of the image_info structure
    memcpy(image_info, &ctx->image_info, sizeof(ImageInfo));

    TRACE("Exiting aaruf_get_image_info() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

