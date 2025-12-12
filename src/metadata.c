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
 * @brief Sets the logical CHS geometry for the AaruFormat image.
 *
 * Configures the Cylinder-Head-Sector (CHS) geometry information for the image being
 * created or modified. This function populates both the geometry block (used for storage
 * in the image file) and the image information structure (used for runtime calculations).
 * The geometry block contains legacy-style logical addressing parameters that describe
 * how the storage medium should be logically organized in terms of cylinders, heads
 * (tracks per cylinder), and sectors per track. This information is crucial for creating
 * images that will be used with software requiring CHS addressing or for accurately
 * preserving the original medium's logical structure.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 * @param cylinders The number of cylinders to set for the geometry.
 * @param heads The number of heads (tracks per cylinder) to set for the geometry.
 * @param sectors_per_track The number of sectors per track to set for the geometry.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully set geometry information. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context is opened in write mode (ctx->isWriting is true)
 *         - The geometry block identifier is set to GeometryBlock
 *         - The geometry block fields (cylinders, heads, sectorsPerTrack) are updated
 *         - The image info fields (Cylinders, Heads, SectorsPerTrack) are synchronized
 *         - All parameters are stored for subsequent write operations
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_create()
 *
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing. This occurs when:
 *         - The image was opened with aaruf_open() instead of aaruf_create()
 *         - The context's isWriting flag is false
 *         - Attempting to modify a read-only image
 *
 * @note Dual Storage:
 *       - Geometry is stored in two locations within the context:
 *         1. ctx->geometryBlock: Written to the image file as a GeometryBlock during close
 *         2. ctx->imageInfo: Used for runtime calculations and metadata queries
 *       - Both locations are kept synchronized by this function
 *
 * @note Geometry Calculation:
 *       - Total logical sectors = cylinders × heads × sectors_per_track
 *       - Ensure the product matches the actual sector count in the image
 *       - Mismatched geometry may cause issues with legacy software or emulators
 *       - Sector size is separate and should be set via other API calls
 *
 * @note CHS Addressing Requirements:
 *       - Required for images intended for legacy BIOS or MBR partition schemes
 *       - Essential for floppy disk images and older hard disk images
 *       - May be optional or synthetic for modern large-capacity drives
 *       - Some virtualization platforms require valid CHS geometry
 *
 * @note Parameter Constraints:
 *       - No validation is performed on the geometry values
 *       - Zero values are technically accepted but may cause issues
 *       - Extremely large values may overflow in calculations (cylinders × heads × sectors_per_track)
 *       - Common constraints for legacy systems:
 *         * Cylinders: typically 1-1024 for BIOS, up to 65535 for modern systems
 *         * Heads: typically 1-255 for most systems
 *         * Sectors per track: typically 1-63 for BIOS, up to 255 for modern systems
 *
 * @note Write Mode Requirement:
 *       - This function is intended for use during image creation
 *       - Should be called after aaruf_create() and before writing sector data
 *       - The geometry block is serialized during aaruf_close()
 *       - Must be used with a write-enabled context
 *
 * @note Historical Context:
 *       - CHS geometry was the original addressing scheme for disk drives
 *       - Physical CHS reflected actual disk platters, heads, and sector layout
 *       - Logical CHS often differs from physical due to zone-bit recording and translation
 *       - Modern drives use LBA (Logical Block Addressing) internally
 *
 * @warning This function does not validate geometry consistency:
 *          - Does not check if cylinders × heads × sectors_per_track equals image sector count
 *          - Does not prevent overflow in the multiplication
 *          - Caller must ensure geometry values are appropriate for the medium type
 *          - Invalid geometry may cause boot failures or data access issues
 *
 * @warning The geometry block is only written to the image file during aaruf_close().
 *          Changes made by this function are not immediately persisted.
 *
 * @warning Changing geometry after writing sector data may create inconsistencies.
 *          Set geometry before beginning sector write operations for best results.
 *
 * @warning Some image formats and use cases don't require CHS geometry:
 *          - Optical media (CD/DVD/BD) use different addressing schemes
 *          - Modern GPT-partitioned disks don't rely on CHS
 *          - Flash-based storage typically doesn't have meaningful CHS geometry
 *          - Setting geometry for such media types is harmless but unnecessary
 */
AARU_EXPORT int32_t AARU_CALL aaruf_set_geometry(void *context, const uint32_t cylinders, const uint32_t heads,
                                                 const uint32_t sectors_per_track)
{
    TRACE("Entering aaruf_set_geometry(%p, %u, %u, %u)", context, cylinders, heads, sectors_per_track);

    aaruformat_context *ctx = NULL;

    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_geometry() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_geometry() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_write_sector() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    ctx->geometry_block.identifier      = GeometryBlock;
    ctx->geometry_block.cylinders       = cylinders;
    ctx->geometry_block.heads           = heads;
    ctx->geometry_block.sectorsPerTrack = sectors_per_track;
    ctx->cylinders                      = cylinders;
    ctx->heads                          = heads;
    ctx->sectors_per_track              = sectors_per_track;
    ctx->dirty_geometry_block           = true;  // Mark geometry block as dirty

    TRACE("Exiting aaruf_set_geometry(%p, %u, %u, %u) = AARUF_STATUS_OK", context, cylinders, heads, sectors_per_track);
    return AARUF_STATUS_OK;
}

/**
 * @brief Sets the media sequence information for multi-volume media sets.
 *
 * Configures the sequence numbering for media that is part of a larger set, such as
 * multi-disk software distributions, backup sets spanning multiple tapes, or optical
 * disc sets. This function records both the current media's position in the sequence
 * and the total number of media in the complete set. This metadata is essential for
 * proper ordering and completeness verification when working with multi-volume archives.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 * @param sequence The sequence number of this media (1-based index indicating position in the set).
 * @param last_sequence The total number of media in the complete set (indicates the final sequence number).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully set media sequence information. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context is opened in write mode (ctx->isWriting is true)
 *         - The metadata block header is initialized (identifier set to MetadataBlock)
 *         - The mediaSequence field is set to the provided sequence value
 *         - The lastMediaSequence field is set to the provided last_sequence value
 *         - Both values are stored for serialization during image close
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_create()
 *
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing. This occurs when:
 *         - The image was opened with aaruf_open() instead of aaruf_create()
 *         - The context's isWriting flag is false
 *         - Attempting to modify a read-only image
 *
 * @note Sequence Numbering:
 *       - Sequence numbers are typically 1-based (first disc is 1, not 0)
 *       - The sequence parameter should be in the range [1, last_sequence]
 *       - last_sequence indicates the total count of media in the set
 *       - Example: For a 3-disc set, disc 2 would have sequence=2, last_sequence=3
 *
 * @note Common Use Cases:
 *       - Multi-CD/DVD software installations (e.g., "Disc 2 of 4")
 *       - Backup tape sets spanning multiple volumes
 *       - Split archive formats requiring sequential processing
 *       - Multi-floppy disk software distributions
 *       - Large data sets divided across multiple optical discs
 *
 * @note Metadata Block Initialization:
 *       - If the metadata block header is not yet initialized, this function initializes it
 *       - The metadataBlockHeader.identifier is set to MetadataBlock automatically
 *       - Multiple metadata setter functions can be called to build complete metadata
 *       - All metadata is written to the image file during aaruf_close()
 *
 * @note Single Media Images:
 *       - For standalone media not part of a set, use sequence=1, last_sequence=1
 *       - Setting both values to 0 may be used to indicate "not part of a sequence"
 *       - The function does not validate that sequence <= last_sequence
 *
 * @note Parameter Validation:
 *       - No validation is performed on sequence numbers
 *       - Negative values are accepted but may be semantically incorrect
 *       - sequence > last_sequence is not prevented but indicates an error condition
 *       - Callers should ensure sequence and last_sequence are logically consistent
 *
 * @note Archive Integrity:
 *       - Proper sequence metadata is crucial for multi-volume restoration
 *       - Archival software may refuse to extract if sequence information is incorrect
 *       - Missing volumes can be detected by checking sequence completeness
 *       - Helps prevent data loss from incomplete multi-volume sets
 *
 * @note Historical Context:
 *       - Multi-volume sets were common in the floppy disk era due to size constraints
 *       - CD/DVD sets were used for large software distributions (operating systems, games)
 *       - Tape backup systems still use multi-volume sets for large archives
 *       - Modern optical media (BD-R DL) reduced the need for multi-disc sets
 *
 * @warning This function does not validate the logical consistency of sequence numbers:
 *          - Does not check if sequence <= last_sequence
 *          - Does not prevent negative or zero values if semantically inappropriate
 *          - Caller must ensure values represent a valid sequence relationship
 *
 * @warning The metadata block is only written to the image file during aaruf_close().
 *          Changes made by this function are not immediately persisted.
 *
 * @warning Incorrect sequence information may prevent proper reconstruction:
 *          - Software relying on sequence numbers may fail to recognize media order
 *          - Archival systems may incorrectly report missing volumes
 *          - Restoration processes may fail if sequence is inconsistent
 */
AARU_EXPORT int32_t AARU_CALL aaruf_set_media_sequence(void *context, const int32_t sequence,
                                                       const int32_t last_sequence)
{
    TRACE("Entering aaruf_set_media_sequence(%p, %d, %d)", context, sequence, last_sequence);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_sequence() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_sequence() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_media_sequence() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadata_block_header.identifier != MetadataBlock) ctx->metadata_block_header.identifier = MetadataBlock;

    ctx->metadata_block_header.mediaSequence     = sequence;
    ctx->metadata_block_header.lastMediaSequence = last_sequence;

    TRACE("Exiting aaruf_set_media_sequence(%p, %d, %d) = AARUF_STATUS_OK", context, sequence, last_sequence);
    return AARUF_STATUS_OK;
}

/**
 * @brief Sets the creator (person/operator) information for the image.
 *
 * Records the name of the person or operator who created the AaruFormat image. This
 * metadata identifies the individual responsible for the imaging process, which is
 * valuable for provenance tracking, accountability, and understanding the human context
 * in which the image was created. The creator name is stored in UTF-16LE encoding and
 * preserved exactly as provided by the caller.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 * @param data Pointer to the creator name string data in UTF-16LE encoding (opaque byte array).
 * @param length Length of the creator data in bytes (must include full UTF-16LE character sequences).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully set creator information. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context is opened in write mode (ctx->isWriting is true)
 *         - Memory allocation for the creator string succeeded
 *         - The metadata block header is initialized (identifier set to MetadataBlock)
 *         - The creator data is copied to ctx->imageInfo.Creator
 *         - The creatorLength field is set in the metadata block header
 *         - Any previous creator string is properly freed before replacement
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_create()
 *
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing. This occurs when:
 *         - The image was opened with aaruf_open() instead of aaruf_create()
 *         - The context's isWriting flag is false
 *         - Attempting to modify a read-only image
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. This occurs when:
 *         - malloc() failed to allocate the required memory for the creator string
 *         - System is out of memory or memory is severely fragmented
 *         - The requested allocation size is too large
 *
 * @note UTF-16LE Encoding:
 *       - The data parameter must contain a valid UTF-16LE encoded string
 *       - Length must be in bytes, not character count (UTF-16LE uses 2 or 4 bytes per character)
 *       - The library treats the data as opaque and does not validate UTF-16LE encoding
 *       - Null termination is not required; length specifies the exact byte count
 *       - Ensure even byte lengths to maintain UTF-16LE character alignment
 *
 * @note Creator Name Format:
 *       - Typically contains the full name of the person who created the image
 *       - May include titles, credentials, or institutional affiliations
 *       - Common formats: "John Smith", "Dr. Jane Doe", "Smith, John (Archivist)"
 *       - Should identify the individual operator, not the software used
 *       - May include contact information or employee/operator ID in institutional settings
 *
 * @note Provenance and Accountability:
 *       - Identifies who performed the imaging operation
 *       - Important for establishing chain of custody in forensic contexts
 *       - Provides contact point for questions about the imaging process
 *       - Supports institutional recordkeeping and quality assurance
 *       - May be required for compliance with archival standards
 *
 * @note Privacy Considerations:
 *       - Consider privacy implications when recording personal names
 *       - Some organizations may use operator IDs instead of full names
 *       - Institutional policies may govern what personal information is recorded
 *       - GDPR and similar regulations may apply in some jurisdictions
 *
 * @note Memory Management:
 *       - The function allocates a new buffer and copies the data
 *       - If a previous creator string exists, it is freed before replacement
 *       - The caller retains ownership of the input data buffer
 *       - The allocated memory is freed when the context is closed or destroyed
 *
 * @note Metadata Block Initialization:
 *       - If the metadata block header is not yet initialized, this function initializes it
 *       - The metadataBlockHeader.identifier is set to MetadataBlock automatically
 *       - Multiple metadata setter functions can be called to build complete metadata
 *
 * @warning The data buffer must remain valid for the duration of this function call.
 *          After the function returns, the caller may free or reuse the data buffer.
 *
 * @warning Invalid UTF-16LE encoding may cause issues when reading the metadata:
 *          - The library does not validate UTF-16LE correctness
 *          - Malformed strings may display incorrectly or cause decoding errors
 *          - Ensure the data is properly encoded before calling this function
 *
 * @warning The metadata block is only written to the image file during aaruf_close().
 *          Changes made by this function are not immediately persisted.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_set_creator(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_creator(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_creator() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_creator() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_creator() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadata_block_header.identifier != MetadataBlock) ctx->metadata_block_header.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for creator");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->creator != NULL) free(ctx->creator);
    ctx->creator                             = copy;
    ctx->metadata_block_header.creatorLength = length;

    TRACE("Exiting aaruf_set_creator(%p, %p, %d) = AARUF_STATUS_OK", context, data, length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Sets user comments or notes for the image.
 *
 * Records arbitrary user-provided comments, notes, or annotations associated with the
 * AaruFormat image. This metadata field allows users to document the image's purpose,
 * provenance, condition, or any other relevant information. Comments are stored in
 * UTF-16LE encoding and can contain multi-line text, special characters, and detailed
 * descriptions.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 * @param data Pointer to the comments string data in UTF-16LE encoding (opaque byte array).
 * @param length Length of the comments data in bytes (must include full UTF-16LE character sequences).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully set comments. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context is opened in write mode (ctx->isWriting is true)
 *         - Memory allocation for the comments string succeeded
 *         - The metadata block header is initialized (identifier set to MetadataBlock)
 *         - The comments data is copied to ctx->imageInfo.Comments
 *         - The commentsLength field is set in the metadata block header
 *         - Any previous comments string is properly freed before replacement
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_create()
 *
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing. This occurs when:
 *         - The image was opened with aaruf_open() instead of aaruf_create()
 *         - The context's isWriting flag is false
 *         - Attempting to modify a read-only image
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. This occurs when:
 *         - malloc() failed to allocate the required memory for the comments string
 *         - System is out of memory or memory is severely fragmented
 *         - The requested allocation size is too large
 *
 * @note UTF-16LE Encoding:
 *       - The data parameter must contain a valid UTF-16LE encoded string
 *       - Length must be in bytes, not character count
 *       - The library treats the data as opaque and does not validate encoding
 *       - Null termination is not required; length specifies the exact byte count
 *
 * @note Common Uses for Comments:
 *       - Documentation of image source and creation date
 *       - Notes about media condition or read errors encountered
 *       - Preservation metadata and archival notes
 *       - User annotations for organizing image collections
 *       - Workflow status or processing history
 *
 * @note Memory Management:
 *       - The function allocates a new buffer and copies the data
 *       - If previous comments exist, they are freed before replacement
 *       - The caller retains ownership of the input data buffer
 *       - The allocated memory is freed when the context is closed or destroyed
 *
 * @warning The metadata block is only written to the image file during aaruf_close().
 *          Changes made by this function are not immediately persisted.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_set_comments(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_comments(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_comments() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_comments() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_comments() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadata_block_header.identifier != MetadataBlock) ctx->metadata_block_header.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for comments");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->comments != NULL) free(ctx->comments);
    ctx->comments                             = copy;
    ctx->metadata_block_header.commentsLength = length;

    TRACE("Exiting aaruf_set_comments(%p, %p, %d) = AARUF_STATUS_OK", context, data, length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Sets the media title or label for the image.
 *
 * Records the title, label, or name printed or written on the physical storage media.
 * This metadata is particularly useful for identifying discs, tapes, or other media
 * that have user-applied labels or manufacturer-printed titles. The title is stored
 * in UTF-16LE encoding to support international characters and special symbols.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 * @param data Pointer to the media title string data in UTF-16LE encoding (opaque byte array).
 * @param length Length of the media title data in bytes (must include full UTF-16LE character sequences).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully set media title. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context is opened in write mode (ctx->isWriting is true)
 *         - Memory allocation for the media title string succeeded
 *         - The metadata block header is initialized (identifier set to MetadataBlock)
 *         - The media title data is copied to ctx->imageInfo.MediaTitle
 *         - The mediaTitleLength field is set in the metadata block header
 *         - Any previous media title string is properly freed before replacement
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_create()
 *
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing. This occurs when:
 *         - The image was opened with aaruf_open() instead of aaruf_create()
 *         - The context's isWriting flag is false
 *         - Attempting to modify a read-only image
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. This occurs when:
 *         - malloc() failed to allocate the required memory for the media title string
 *         - System is out of memory or memory is severely fragmented
 *         - The requested allocation size is too large
 *
 * @note Common Media Title Examples:
 *       - Handwritten labels on optical discs or tapes
 *       - Pre-printed software names on distribution media (e.g., "Windows 95 Setup Disk 1")
 *       - Volume labels or disc names (e.g., "BACKUP_2024", "INSTALL_CD")
 *       - Game titles printed on cartridges or discs
 *       - Album or movie titles on multimedia discs
 *
 * @note Preservation Context:
 *       - Important for archival purposes to record exactly what appears on the media
 *       - Helps identify media in large collections
 *       - May differ from filesystem volume labels
 *       - Useful for matching physical media to digital images
 *
 * @note UTF-16LE Encoding:
 *       - The data parameter must contain a valid UTF-16LE encoded string
 *       - Length must be in bytes, not character count
 *       - Supports international characters, emoji, and special symbols
 *       - Null termination is not required; length specifies the exact byte count
 *
 * @warning The metadata block is only written to the image file during aaruf_close().
 *          Changes made by this function are not immediately persisted.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_set_media_title(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_media_title(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_title() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_title() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_media_title() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadata_block_header.identifier != MetadataBlock) ctx->metadata_block_header.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for media title");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->media_title != NULL) free(ctx->media_title);
    ctx->media_title                            = copy;
    ctx->metadata_block_header.mediaTitleLength = length;
    ctx->dirty_metadata_block                   = true;  // Mark metadata block as dirty

    TRACE("Exiting aaruf_set_media_title(%p, %p, %d) = AARUF_STATUS_OK", context, data, length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Sets the media manufacturer information for the image.
 *
 * Records the name of the company that manufactured the physical storage media.
 * This metadata is valuable for preservation and forensic purposes, as it can help
 * identify the media type, quality characteristics, and manufacturing period. The
 * manufacturer name is stored in UTF-16LE encoding.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 * @param data Pointer to the media manufacturer string data in UTF-16LE encoding (opaque byte array).
 * @param length Length of the media manufacturer data in bytes (must include full UTF-16LE character sequences).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully set media manufacturer. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context is opened in write mode (ctx->isWriting is true)
 *         - Memory allocation for the media manufacturer string succeeded
 *         - The metadata block header is initialized (identifier set to MetadataBlock)
 *         - The media manufacturer data is copied to ctx->imageInfo.MediaManufacturer
 *         - The mediaManufacturerLength field is set in the metadata block header
 *         - Any previous media manufacturer string is properly freed before replacement
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_create()
 *
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing. This occurs when:
 *         - The image was opened with aaruf_open() instead of aaruf_create()
 *         - The context's isWriting flag is false
 *         - Attempting to modify a read-only image
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. This occurs when:
 *         - malloc() failed to allocate the required memory for the media manufacturer string
 *         - System is out of memory or memory is severely fragmented
 *         - The requested allocation size is too large
 *
 * @note Common Media Manufacturers:
 *       - Optical discs: Verbatim, Sony, Maxell, TDK, Taiyo Yuden
 *       - Magnetic tapes: Fujifilm, Maxell, Sony, IBM
 *       - Floppy disks: 3M, Maxell, Sony, Verbatim
 *       - Flash media: SanDisk, Kingston, Samsung
 *
 * @note Identification Methods:
 *       - May be printed on the media surface or packaging
 *       - Can sometimes be detected from media ID codes (e.g., ATIP for CD-R)
 *       - Historical records or catalogs may provide this information
 *       - Important for understanding media quality and longevity characteristics
 *
 * @note Preservation Value:
 *       - Helps assess expected media lifespan and degradation patterns
 *       - Useful for identifying counterfeit or remarked media
 *       - Aids in research about media quality and failure modes
 *       - Provides context for archival planning and migration strategies
 *
 * @warning The metadata block is only written to the image file during aaruf_close().
 *          Changes made by this function are not immediately persisted.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_set_media_manufacturer(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_media_manufacturer(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_manufacturer() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_manufacturer() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_media_manufacturer() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadata_block_header.identifier != MetadataBlock) ctx->metadata_block_header.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for media manufacturer");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->media_manufacturer != NULL) free(ctx->media_manufacturer);
    ctx->media_manufacturer                            = copy;
    ctx->metadata_block_header.mediaManufacturerLength = length;

    TRACE("Exiting aaruf_set_media_manufacturer(%p, %p, %d) = AARUF_STATUS_OK", context, data, length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Sets the media model or product designation for the image.
 *
 * Records the specific model, product line, or type designation of the physical storage
 * media. This is more specific than the manufacturer and identifies the exact product
 * variant. This metadata helps in identifying specific media characteristics, performance
 * specifications, and compatibility information. The model information is stored in
 * UTF-16LE encoding.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 * @param data Pointer to the media model string data in UTF-16LE encoding (opaque byte array).
 * @param length Length of the media model data in bytes (must include full UTF-16LE character sequences).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully set media model. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context is opened in write mode (ctx->isWriting is true)
 *         - Memory allocation for the media model string succeeded
 *         - The metadata block header is initialized (identifier set to MetadataBlock)
 *         - The media model data is copied to ctx->imageInfo.MediaModel
 *         - The mediaModelLength field is set in the metadata block header
 *         - Any previous media model string is properly freed before replacement
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_create()
 *
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing. This occurs when:
 *         - The image was opened with aaruf_open() instead of aaruf_create()
 *         - The context's isWriting flag is false
 *         - Attempting to modify a read-only image
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. This occurs when:
 *         - malloc() failed to allocate the required memory for the media model string
 *         - System is out of memory or memory is severely fragmented
 *         - The requested allocation size is too large
 *
 * @note Model Designation Examples:
 *       - Optical discs: "DVD+R 16x", "CD-R 80min", "BD-R DL 50GB"
 *       - Tapes: "LTO-7", "DLT-IV", "AIT-3"
 *       - Floppy disks: "HD 1.44MB", "DD 720KB"
 *       - Flash cards: "SDHC Class 10", "CompactFlash 1000x"
 *
 * @note Model Information Usage:
 *       - Identifies specific capacity and speed ratings
 *       - Indicates format compatibility (e.g., DVD-R vs DVD+R)
 *       - Helps determine recording characteristics and quality tier
 *       - Useful for matching replacement media or troubleshooting issues
 *
 * @warning The metadata block is only written to the image file during aaruf_close().
 *          Changes made by this function are not immediately persisted.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_set_media_model(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_media_model(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_model() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_model() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_media_model() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadata_block_header.identifier != MetadataBlock) ctx->metadata_block_header.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for media model");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->media_model != NULL) free(ctx->media_model);
    ctx->media_model                            = copy;
    ctx->metadata_block_header.mediaModelLength = length;

    TRACE("Exiting aaruf_set_media_model(%p, %p, %d) = AARUF_STATUS_OK", context, data, length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Sets the media serial number for the image.
 *
 * Records the unique serial number assigned to the physical storage media by the
 * manufacturer. This metadata provides a unique identifier for the specific piece of
 * media, which is invaluable for tracking, authentication, and forensic analysis. Not
 * all media types have serial numbers; this is most common with professional-grade
 * media and some consumer optical discs. The serial number is stored in UTF-16LE encoding.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 * @param data Pointer to the media serial number string data in UTF-16LE encoding (opaque byte array).
 * @param length Length of the media serial number data in bytes (must include full UTF-16LE character sequences).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully set media serial number. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context is opened in write mode (ctx->isWriting is true)
 *         - Memory allocation for the media serial number string succeeded
 *         - The metadata block header is initialized (identifier set to MetadataBlock)
 *         - The media serial number data is copied to ctx->imageInfo.MediaSerialNumber
 *         - The mediaSerialNumberLength field is set in the metadata block header
 *         - Any previous media serial number string is properly freed before replacement
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_create()
 *
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing. This occurs when:
 *         - The image was opened with aaruf_open() instead of aaruf_create()
 *         - The context's isWriting flag is false
 *         - Attempting to modify a read-only image
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. This occurs when:
 *         - malloc() failed to allocate the required memory for the media serial number string
 *         - System is out of memory or memory is severely fragmented
 *         - The requested allocation size is too large
 *
 * @note Serial Number Availability:
 *       - Professional tape media (LTO, DLT, etc.) typically have printed serial numbers
 *       - Some optical discs have embedded media IDs that can serve as serial numbers
 *       - Hard drives and SSDs have electronically-readable serial numbers
 *       - Consumer recordable media (CD-R, DVD-R) rarely have unique serial numbers
 *       - May be printed on labels, hubs, or cartridge shells
 *
 * @note Forensic and Archival Importance:
 *       - Uniquely identifies the specific physical media instance
 *       - Critical for chain of custody in forensic investigations
 *       - Enables tracking of media throughout its lifecycle
 *       - Helps prevent mix-ups in large media collections
 *       - Can verify authenticity and detect counterfeits
 *
 * @note Format Considerations:
 *       - Serial numbers may be alphanumeric, numeric, or contain special characters
 *       - Format varies widely between manufacturers and media types
 *       - May include check digits or formatting separators
 *       - Should be recorded exactly as it appears on the media
 *
 * @warning The metadata block is only written to the image file during aaruf_close().
 *          Changes made by this function are not immediately persisted.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_set_media_serial_number(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_media_serial_number(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_serial_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_serial_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_media_serial_number() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadata_block_header.identifier != MetadataBlock) ctx->metadata_block_header.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for media serial number");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->media_serial_number != NULL) free(ctx->media_serial_number);
    ctx->media_serial_number                           = copy;
    ctx->metadata_block_header.mediaSerialNumberLength = length;

    TRACE("Exiting aaruf_set_media_serial_number(%p, %p, %d) = AARUF_STATUS_OK", context, data, length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Sets the media barcode information for the image.
 *
 * Records the barcode affixed to the physical storage media or its packaging. Barcodes
 * are commonly used in professional archival and library environments for automated
 * inventory management, tracking, and retrieval systems. This metadata enables correlation
 * between physical media location systems and digital image files. The barcode is stored
 * in UTF-16LE encoding.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 * @param data Pointer to the media barcode string data in UTF-16LE encoding (opaque byte array).
 * @param length Length of the media barcode data in bytes (must include full UTF-16LE character sequences).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully set media barcode. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context is opened in write mode (ctx->isWriting is true)
 *         - Memory allocation for the media barcode string succeeded
 *         - The metadata block header is initialized (identifier set to MetadataBlock)
 *         - The media barcode data is copied to ctx->imageInfo.MediaBarcode
 *         - The mediaBarcodeLength field is set in the metadata block header
 *         - Any previous media barcode string is properly freed before replacement
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_create()
 *
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing. This occurs when:
 *         - The image was opened with aaruf_open() instead of aaruf_create()
 *         - The context's isWriting flag is false
 *         - Attempting to modify a read-only image
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. This occurs when:
 *         - malloc() failed to allocate the required memory for the media barcode string
 *         - System is out of memory or memory is severely fragmented
 *         - The requested allocation size is too large
 *
 * @note Common Barcode Uses:
 *       - Library and archival tape management systems
 *       - Automated tape library robotics (e.g., LTO tape cartridges)
 *       - Inventory tracking in large media collections
 *       - Asset management in corporate or institutional settings
 *       - Chain of custody tracking in forensic environments
 *
 * @note Barcode Types and Formats:
 *       - Code 39 and Code 128 are common for media labeling
 *       - LTO tapes use specific 6 or 8-character barcode formats
 *       - May include library-specific prefixes or location codes
 *       - Some systems use 2D barcodes (QR codes, Data Matrix)
 *       - Barcode should be recorded exactly as it appears
 *
 * @note Integration with Physical Systems:
 *       - Enables automated correlation between physical and digital catalogs
 *       - Supports robotic tape library operations
 *       - Facilitates retrieval from off-site storage facilities
 *       - Links to broader asset management databases
 *       - Critical for large-scale archival operations
 *
 * @note Preservation Context:
 *       - Important for long-term archival planning
 *       - Helps maintain inventory accuracy over time
 *       - Supports audit and compliance requirements
 *       - Enables efficient physical media location and retrieval
 *
 * @warning The metadata block is only written to the image file during aaruf_close().
 *          Changes made by this function are not immediately persisted.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_set_media_barcode(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_media_barcode(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_barcode() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_barcode() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_media_barcode() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadata_block_header.identifier != MetadataBlock) ctx->metadata_block_header.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for media barcode");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->media_barcode != NULL) free(ctx->media_barcode);
    ctx->media_barcode                            = copy;
    ctx->metadata_block_header.mediaBarcodeLength = length;

    TRACE("Exiting aaruf_set_media_barcode(%p, %p, %d) = AARUF_STATUS_OK", context, data, length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Sets the media part number or model designation for the image.
 *
 * Records the manufacturer's part number or catalog designation for the specific type
 * of physical storage media. This is distinct from the media model in that it represents
 * the exact ordering or catalog number used for procurement and inventory purposes. Part
 * numbers are particularly important for sourcing compatible replacement media and for
 * precise identification in technical documentation. The part number is stored in UTF-16LE
 * encoding.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 * @param data Pointer to the media part number string data in UTF-16LE encoding (opaque byte array).
 * @param length Length of the media part number data in bytes (must include full UTF-16LE character sequences).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully set media part number. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context is opened in write mode (ctx->isWriting is true)
 *         - Memory allocation for the media part number string succeeded
 *         - The metadata block header is initialized (identifier set to MetadataBlock)
 *         - The media part number data is copied to ctx->imageInfo.MediaPartNumber
 *         - The mediaPartNumberLength field is set in the metadata block header
 *         - Any previous media part number string is properly freed before replacement
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_create()
 *
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing. This occurs when:
 *         - The image was opened with aaruf_open() instead of aaruf_create()
 *         - The context's isWriting flag is false
 *         - Attempting to modify a read-only image
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. This occurs when:
 *         - malloc() failed to allocate the required memory for the media part number string
 *         - System is out of memory or memory is severely fragmented
 *         - The requested allocation size is too large
 *
 * @note Part Number Examples:
 *       - Optical media: "MR-25332" (Verbatim DVD+R), "CDR80JC" (Sony CD-R)
 *       - Tape media: "C7973A" (HP LTO-3), "TK88" (Maxell DLT-IV)
 *       - Floppy disks: "MF2HD" (3.5" high-density), "2D" (5.25" double-density)
 *       - May include regional variations or packaging size indicators
 *
 * @note Practical Applications:
 *       - Procurement and ordering of compatible replacement media
 *       - Cross-referencing with manufacturer specifications and datasheets
 *       - Identifying specific product revisions or manufacturing batches
 *       - Ensuring compatibility for archival media migration projects
 *       - Tracking costs and suppliers in institutional settings
 *
 * @note Relationship to Model:
 *       - Part number is more specific than model designation
 *       - Model might be "DVD+R 16x", part number "MR-25332"
 *       - Part numbers may vary by region, packaging quantity, or color
 *       - Critical for exact product identification in catalogs and databases
 *
 * @note Documentation and Compliance:
 *       - Useful for creating detailed preservation documentation
 *       - Supports compliance with archival standards and best practices
 *       - Enables precise replication of archival environments
 *       - Facilitates research on media types and failure modes
 *
 * @warning The metadata block is only written to the image file during aaruf_close().
 *          Changes made by this function are not immediately persisted.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_set_media_part_number(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_media_part_number(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_part_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_part_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_media_part_number() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadata_block_header.identifier != MetadataBlock) ctx->metadata_block_header.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for creator");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->media_part_number != NULL) free(ctx->media_part_number);
    ctx->media_part_number                           = copy;
    ctx->metadata_block_header.mediaPartNumberLength = length;

    TRACE("Exiting aaruf_set_media_part_number(%p, %p, %d) = AARUF_STATUS_OK", context, data, length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Sets the drive manufacturer information for the image.
 *
 * Records the name of the company that manufactured the drive or device used to read
 * or write the physical storage media. This metadata provides valuable context about
 * the imaging process, as different drives may have different capabilities, error
 * handling characteristics, and compatibility with specific media types. The manufacturer
 * name is stored in UTF-16LE encoding.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 * @param data Pointer to the drive manufacturer string data in UTF-16LE encoding (opaque byte array).
 * @param length Length of the drive manufacturer data in bytes (must include full UTF-16LE character sequences).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully set drive manufacturer. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context is opened in write mode (ctx->isWriting is true)
 *         - Memory allocation for the drive manufacturer string succeeded
 *         - The metadata block header is initialized (identifier set to MetadataBlock)
 *         - The drive manufacturer data is copied to ctx->imageInfo.DriveManufacturer
 *         - The driveManufacturerLength field is set in the metadata block header
 *         - Any previous drive manufacturer string is properly freed before replacement
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_create()
 *
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing. This occurs when:
 *         - The image was opened with aaruf_open() instead of aaruf_create()
 *         - The context's isWriting flag is false
 *         - Attempting to modify a read-only image
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. This occurs when:
 *         - malloc() failed to allocate the required memory for the drive manufacturer string
 *         - System is out of memory or memory is severely fragmented
 *         - The requested allocation size is too large
 *
 * @note Common Drive Manufacturers:
 *       - Optical drives: Pioneer, Plextor, LG, ASUS, Sony, Samsung
 *       - Tape drives: HP, IBM, Quantum, Tandberg, Exabyte
 *       - Hard drives: Seagate, Western Digital, Hitachi, Toshiba
 *       - Floppy drives: Teac, Panasonic, Mitsumi, Sony
 *
 * @note Imaging Context and Quality:
 *       - Different manufacturers have varying error recovery capabilities
 *       - Some drives are better suited for archival-quality imaging
 *       - Plextor drives were historically preferred for optical disc preservation
 *       - Professional-grade drives often have better read accuracy
 *       - Important for understanding potential limitations in the imaging process
 *
 * @note Forensic and Provenance Value:
 *       - Documents the complete imaging environment
 *       - Helps assess reliability and trustworthiness of the image
 *       - Useful for troubleshooting or reproducing imaging results
 *       - May be required for forensic chain of custody
 *       - Supports quality assurance and validation processes
 *
 * @warning The metadata block is only written to the image file during aaruf_close().
 *          Changes made by this function are not immediately persisted.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_set_drive_manufacturer(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_drive_manufacturer(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_drive_manufacturer() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_drive_manufacturer() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_drive_manufacturer() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadata_block_header.identifier != MetadataBlock) ctx->metadata_block_header.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for drive manufacturer");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->drive_manufacturer != NULL) free(ctx->drive_manufacturer);
    ctx->drive_manufacturer                            = copy;
    ctx->metadata_block_header.driveManufacturerLength = length;

    TRACE("Exiting aaruf_set_drive_manufacturer(%p, %p, %d) = AARUF_STATUS_OK", context, data, length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Sets the drive model information for the image.
 *
 * Records the specific model or product designation of the drive or device used to read
 * or write the physical storage media. This metadata provides detailed information about
 * the imaging equipment, which can be important for understanding the capabilities,
 * limitations, and characteristics of the imaging process. Different drive models within
 * the same manufacturer's product line may have significantly different features and
 * performance characteristics. The model information is stored in UTF-16LE encoding.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 * @param data Pointer to the drive model string data in UTF-16LE encoding (opaque byte array).
 * @param length Length of the drive model data in bytes (must include full UTF-16LE character sequences).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully set drive model. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context is opened in write mode (ctx->isWriting is true)
 *         - Memory allocation for the drive model string succeeded
 *         - The metadata block header is initialized (identifier set to MetadataBlock)
 *         - The drive model data is copied to ctx->imageInfo.DriveModel
 *         - The driveModelLength field is set in the metadata block header
 *         - Any previous drive model string is properly freed before replacement
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_create()
 *
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing. This occurs when:
 *         - The image was opened with aaruf_open() instead of aaruf_create()
 *         - The context's isWriting flag is false
 *         - Attempting to modify a read-only image
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. This occurs when:
 *         - malloc() failed to allocate the required memory for the drive model string
 *         - System is out of memory or memory is severely fragmented
 *         - The requested allocation size is too large
 *
 * @note Drive Model Examples:
 *       - Optical drives: "DVR-111D" (Pioneer), "PX-716A" (Plextor), "GH24NSB0" (LG)
 *       - Tape drives: "Ultrium 7-SCSI" (HP), "TS1140" (IBM), "SDLT600" (Quantum)
 *       - Hard drives: "ST2000DM008" (Seagate), "WD40EZRZ" (Western Digital)
 *       - Floppy drives: "FD-235HF" (Teac), "JU-257A633P" (Panasonic)
 *
 * @note Model-Specific Characteristics:
 *       - Different models may have different error correction algorithms
 *       - Some models are known for superior read quality (e.g., Plextor Premium)
 *       - Certain models may have bugs or quirks in specific firmware versions
 *       - Professional models often have features not available in consumer versions
 *       - Important for reproducing imaging conditions or troubleshooting issues
 *
 * @note Forensic Documentation:
 *       - Complete drive identification aids in forensic reporting
 *       - Helps establish the reliability of the imaging process
 *       - May be required for compliance with forensic standards
 *       - Supports validation and verification procedures
 *       - Enables assessment of tool suitability for the imaging task
 *
 * @note Historical and Research Value:
 *       - Documents evolution of imaging technology over time
 *       - Helps identify best practices for specific media types
 *       - Useful for academic research on digital preservation
 *       - Aids in understanding drive availability and obsolescence
 *
 * @warning The metadata block is only written to the image file during aaruf_close().
 *          Changes made by this function are not immediately persisted.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_set_drive_model(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_drive_model(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_drive_model() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_drive_model() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_drive_model() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadata_block_header.identifier != MetadataBlock) ctx->metadata_block_header.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for media model");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->drive_model != NULL) free(ctx->drive_model);
    ctx->drive_model                            = copy;
    ctx->metadata_block_header.driveModelLength = length;

    TRACE("Exiting aaruf_set_drive_model(%p, %p, %d) = AARUF_STATUS_OK", context, data, length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Sets the drive serial number for the image.
 *
 * Records the unique serial number of the drive or device used to read or write the
 * physical storage media. This metadata provides a unique identifier for the specific
 * piece of hardware used in the imaging process, which is particularly important for
 * forensic work, equipment tracking, and quality assurance. The serial number enables
 * correlation between multiple images created with the same drive and can help identify
 * drive-specific issues or characteristics. The serial number is stored in UTF-16LE encoding.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 * @param data Pointer to the drive serial number string data in UTF-16LE encoding (opaque byte array).
 * @param length Length of the drive serial number data in bytes (must include full UTF-16LE character sequences).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully set drive serial number. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context is opened in write mode (ctx->isWriting is true)
 *         - Memory allocation for the drive serial number string succeeded
 *         - The metadata block header is initialized (identifier set to MetadataBlock)
 *         - The drive serial number data is copied to ctx->imageInfo.DriveSerialNumber
 *         - The driveSerialNumberLength field is set in the metadata block header
 *         - Any previous drive serial number string is properly freed before replacement
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_create()
 *
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing. This occurs when:
 *         - The image was opened with aaruf_open() instead of aaruf_create()
 *         - The context's isWriting flag is false
 *         - Attempting to modify a read-only image
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. This occurs when:
 *         - malloc() failed to allocate the required memory for the drive serial number string
 *         - System is out of memory or memory is severely fragmented
 *         - The requested allocation size is too large
 *
 * @note Serial Number Sources:
 *       - Hard drives and SSDs: Retrieved via ATA/SCSI IDENTIFY commands
 *       - Optical drives: Available through SCSI inquiry or ATA identify
 *       - Tape drives: Typically reported via SCSI inquiry data
 *       - USB devices: May have USB serial numbers or internal device serials
 *       - Some older or consumer devices may not report serial numbers
 *
 * @note Forensic Applications:
 *       - Critical for forensic chain of custody documentation
 *       - Uniquely identifies the specific hardware used for imaging
 *       - Enables tracking of drive calibration and maintenance history
 *       - Supports correlation of images created with the same equipment
 *       - May be required by forensic standards and best practices
 *
 * @note Equipment Management:
 *       - Helps track drive usage and workload for maintenance planning
 *       - Enables identification of drives requiring replacement or service
 *       - Supports equipment inventory and asset management systems
 *       - Useful for identifying drives with known issues or recalls
 *       - Facilitates warranty and support claim processing
 *
 * @note Quality Assurance:
 *       - Enables analysis of drive-specific error patterns
 *       - Helps identify if multiple imaging issues stem from the same drive
 *       - Supports statistical quality control processes
 *       - Aids in evaluating drive reliability over time
 *       - Can reveal degradation or calibration drift in aging hardware
 *
 * @warning The metadata block is only written to the image file during aaruf_close().
 *          Changes made by this function are not immediately persisted.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_set_drive_serial_number(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_drive_serial_number(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_drive_serial_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_drive_serial_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_drive_serial_number() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadata_block_header.identifier != MetadataBlock) ctx->metadata_block_header.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for drive serial number");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->drive_serial_number != NULL) free(ctx->drive_serial_number);
    ctx->drive_serial_number                           = copy;
    ctx->metadata_block_header.driveSerialNumberLength = length;

    TRACE("Exiting aaruf_set_drive_serial_number(%p, %p, %d) = AARUF_STATUS_OK", context, data, length);
    return AARUF_STATUS_OK;
}

/**
 * @brief Sets the drive firmware revision for the image.
 *
 * Records the firmware version or revision of the drive or device used to read or write
 * the physical storage media. Firmware revisions can significantly affect drive behavior,
 * error handling, performance, and compatibility. This metadata is crucial for understanding
 * the imaging environment, troubleshooting issues, and ensuring reproducibility. Different
 * firmware versions of the same drive model can behave quite differently, making this
 * information essential for comprehensive documentation. The firmware revision is stored
 * in UTF-16LE encoding.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 * @param data Pointer to the drive firmware revision string data in UTF-16LE encoding (opaque byte array).
 * @param length Length of the drive firmware revision data in bytes (must include full UTF-16LE character sequences).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully set drive firmware revision. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context is opened in write mode (ctx->isWriting is true)
 *         - Memory allocation for the drive firmware revision string succeeded
 *         - The metadata block header is initialized (identifier set to MetadataBlock)
 *         - The drive firmware revision data is copied to ctx->imageInfo.DriveFirmwareRevision
 *         - The driveFirmwareRevisionLength field is set in the metadata block header
 *         - Any previous drive firmware revision string is properly freed before replacement
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_create()
 *
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing. This occurs when:
 *         - The image was opened with aaruf_open() instead of aaruf_create()
 *         - The context's isWriting flag is false
 *         - Attempting to modify a read-only image
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. This occurs when:
 *         - malloc() failed to allocate the required memory for the drive firmware revision string
 *         - System is out of memory or memory is severely fragmented
 *         - The requested allocation size is too large
 *
 * @note Firmware Revision Examples:
 *       - Optical drives: "1.07", "1.00a", "VER A302"
 *       - Tape drives: "B8S1", "7760", "V4.0"
 *       - Hard drives: "CC45", "80.00A80", "HPG9"
 *       - Format varies by manufacturer and device type
 *
 * @note Firmware Impact on Imaging:
 *       - Different firmware versions may have different error recovery strategies
 *       - Bug fixes in newer firmware can improve read reliability
 *       - Some firmware versions have known issues with specific media types
 *       - Performance characteristics may vary between firmware revisions
 *       - Firmware can affect features like C2 error reporting on optical drives
 *
 * @note Troubleshooting and Support:
 *       - Essential for diagnosing drive-specific problems
 *       - Manufacturer support often requires firmware version information
 *       - Helps identify if issues are resolved in newer firmware
 *       - Enables correlation of problems with known firmware bugs
 *       - Supports decision-making about firmware updates
 *
 * @note Reproducibility and Documentation:
 *       - Complete environment documentation for scientific reproducibility
 *       - Important for forensic work requiring detailed equipment records
 *       - Helps explain variations in imaging results over time
 *       - Supports compliance with archival and preservation standards
 *       - Enables future researchers to understand imaging conditions
 *
 * @note Historical Tracking:
 *       - Documents firmware changes over the life of imaging equipment
 *       - Helps assess impact of firmware updates on imaging quality
 *       - Useful for long-term archival projects spanning years
 *       - Aids in understanding evolution of drive technology
 *
 * @warning The metadata block is only written to the image file during aaruf_close().
 *          Changes made by this function are not immediately persisted.
 *
 * @warning Firmware revisions are device-specific and format varies widely:
 *          - No standard format exists across manufacturers
 *          - May include letters, numbers, dots, or other characters
 *          - Should be recorded exactly as reported by the device
 */
AARU_EXPORT int32_t AARU_CALL aaruf_set_drive_firmware_revision(void *context, const uint8_t *data,
                                                                const int32_t length)
{
    TRACE("Entering aaruf_set_drive_firmware_revision(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_drive_firmware_revision() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_drive_firmware_revision() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_drive_firmware_revision() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadata_block_header.identifier != MetadataBlock) ctx->metadata_block_header.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for creator");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->drive_firmware_revision != NULL) free(ctx->drive_firmware_revision);
    ctx->drive_firmware_revision                           = copy;
    ctx->metadata_block_header.driveFirmwareRevisionLength = length;

    TRACE("Exiting aaruf_set_drive_firmware_revision(%p, %p, %d) = AARUF_STATUS_OK", context, data, length);
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
 * @brief Sets the Aaru metadata JSON for the image during creation.
 *
 * Embeds Aaru metadata JSON into the image being created. The Aaru metadata JSON format is a
 * structured, machine-readable representation of comprehensive image metadata including media
 * information, imaging session details, hardware configuration, optical disc tracks and sessions,
 * checksums, and preservation metadata. This function stores the JSON payload in its original form
 * without parsing or validation, allowing callers to provide pre-generated JSON conforming to the
 * Aaru metadata schema. The JSON data will be written to the image file during aaruf_close() as
 * an AaruMetadataJsonBlock.
 *
 * The function accepts raw UTF-8 encoded JSON data as an opaque byte array and creates an internal
 * copy that persists for the lifetime of the context. If Aaru JSON metadata was previously set on
 * this context, the old data is freed and replaced with the new JSON. The JSON is treated as opaque
 * binary data by the library; no parsing, interpretation, or schema validation is performed during
 * the set operation.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 * @param data Pointer to the Aaru metadata JSON data in UTF-8 encoding (opaque byte array).
 *             The JSON should conform to the Aaru metadata schema, though this is not validated.
 *             Must not be NULL.
 * @param length Length of the JSON data in bytes. The payload may or may not be null-terminated;
 *               the length should reflect the actual JSON data size.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully set Aaru metadata JSON. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context is opened in write mode (ctx->isWriting is true)
 *         - Memory allocation for the JSON data succeeded
 *         - The JSON data is copied to ctx->jsonBlock
 *         - The jsonBlockHeader is initialized with identifier AaruMetadataJsonBlock
 *         - The jsonBlockHeader.length field is set to the provided length
 *         - Any previous Aaru JSON data is properly freed before replacement
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_create()
 *
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing. This occurs when:
 *         - The image was opened with aaruf_open() instead of aaruf_create()
 *         - The context's isWriting flag is false
 *         - Attempting to modify a read-only image
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. This occurs when:
 *         - malloc() failed to allocate the required memory for the JSON data
 *         - System is out of memory or memory is severely fragmented
 *         - The requested allocation size is too large
 *
 * @note Aaru JSON Format and Encoding:
 *       - The JSON payload must be valid UTF-8 encoded data
 *       - The payload may or may not be null-terminated
 *       - The library treats the JSON as opaque binary data
 *       - No JSON parsing, interpretation, or validation is performed during set
 *       - Schema compliance is the caller's responsibility
 *       - Ensure the JSON conforms to the Aaru metadata schema for compatibility
 *
 * @note Aaru Metadata JSON Purpose:
 *       - Provides machine-readable structured metadata using modern JSON format
 *       - Includes comprehensive information about media, sessions, tracks, and checksums
 *       - Enables programmatic access to metadata without XML parsing overhead
 *       - Documents imaging session details, hardware configuration, and preservation data
 *       - Used by Aaru and compatible tools for metadata exchange and analysis
 *       - Complements or serves as alternative to CICM XML metadata
 *
 * @note JSON Content Examples:
 *       - Media type and physical characteristics
 *       - Track and session information for optical media
 *       - Partition and filesystem metadata
 *       - Checksums (MD5, SHA-1, SHA-256, etc.) for integrity verification
 *       - Hardware information (drive manufacturer, model, firmware)
 *       - Imaging software version and configuration
 *       - Timestamps for image creation and modification
 *
 * @note Memory Management:
 *       - The function creates an internal copy of the JSON data
 *       - The caller retains ownership of the input data buffer
 *       - The caller may free the input data immediately after this function returns
 *       - The internal copy is freed automatically during aaruf_close()
 *       - Calling this function multiple times replaces previous JSON data
 *
 * @note Relationship to CICM XML:
 *       - Both CICM XML and Aaru JSON can be set on the same image
 *       - CICM XML follows the Canary Islands Computer Museum schema (older format)
 *       - Aaru JSON follows the Aaru-specific metadata schema (newer format)
 *       - Setting one does not affect the other
 *       - Different tools may prefer one format over the other
 *       - Consider including both for maximum compatibility
 *
 * @warning The Aaru JSON block is only written to the image file during aaruf_close().
 *          Changes made by this function are not immediately persisted to disk.
 *
 * @warning No validation is performed on the JSON data:
 *          - Invalid JSON syntax will be stored and only detected during parsing
 *          - Schema violations will not be caught by this function
 *          - Ensure JSON is valid and schema-compliant before calling
 *          - Use a JSON validation library before embedding if correctness is critical
 *
 * @warning The function accepts any length value:
 *          - Ensure the length accurately reflects the JSON data size
 *          - Incorrect length values may cause truncation or include garbage data
 *          - For null-terminated JSON, length should not include the null terminator
 *            unless it is intended to be part of the stored data
 *
 * @see AaruMetadataJsonBlockHeader for the on-disk structure definition.
 * @see aaruf_get_aaru_json_metadata() for retrieving Aaru JSON from opened images.
 * @see write_aaru_json_block() for the serialization process during image closing.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_set_aaru_json_metadata(void *context, uint8_t *data, size_t length)
{
    TRACE("Entering aaruf_set_aaru_json_metadata(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_aaru_json_metadata() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_aaru_json_metadata() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_aaru_json_metadata() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for Aaru metadata JSON");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-8 string
    memcpy(copy, data, length);
    if(ctx->json_block != NULL) free(ctx->json_block);
    ctx->json_block                   = copy;
    ctx->json_block_header.identifier = AaruMetadataJsonBlock;
    ctx->json_block_header.length     = (uint32_t)length;
    ctx->dirty_json_block             = true;  // Mark JSON block as dirty

    TRACE("Exiting aaruf_set_aaru_json_metadata(%p, %p, %d) = AARUF_STATUS_OK", context, data, length);
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
AARU_EXPORT int32_t AARU_CALL aaruf_get_negative_sectors(const void *context, uint16_t *sectors)
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
AARU_EXPORT int32_t AARU_CALL aaruf_get_overflow_sectors(const void *context, uint16_t *sectors)
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

/**
 * @brief Clears the media sequence information from the image metadata.
 *
 * Removes the media sequence and last media sequence fields from the AaruFormat image's
 * metadata block, effectively removing any indication that this media is part of a
 * multi-volume set. Both the current sequence number and the total sequence count are
 * reset to zero. If this operation results in all metadata fields being empty, the
 * entire metadata block is deinitialized (identifier set to 0) to avoid writing an
 * empty metadata block to the image file.
 *
 * This function is useful when repurposing an image that was originally part of a
 * multi-disc set but is now being treated as standalone media, or when correcting
 * metadata that was set incorrectly.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully cleared media sequence information. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context is opened in write mode (ctx->isWriting is true)
 *         - Either the metadata block was not initialized (early exit, nothing to clear)
 *         - Or the mediaSequence and lastMediaSequence fields are set to 0
 *         - If all metadata fields are now empty, the metadata block identifier is cleared
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_create()
 *
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing. This occurs when:
 *         - The image was opened with aaruf_open() instead of aaruf_create()
 *         - The context's isWriting flag is false
 *         - Attempting to modify a read-only image
 *
 * @note Metadata Block State Management:
 *       - If the metadata block header is not initialized (identifier != MetadataBlock),
 *         the function returns successfully without any action (nothing to clear)
 *       - After clearing, if all metadata fields are empty, the metadata block header
 *         identifier is set to 0, preventing an empty block from being written
 *       - This automatic cleanup ensures efficient storage and avoids unnecessary blocks
 *
 * @note Sequence Field Reset:
 *       - Both mediaSequence and lastMediaSequence are set to 0
 *       - Zero values typically indicate "not part of a sequence" or "unknown sequence"
 *       - The function does not differentiate between never-set and explicitly-cleared
 *
 * @note Use Cases:
 *       - Correcting incorrectly set sequence metadata
 *       - Converting multi-volume images to standalone format
 *       - Removing obsolete sequence information during image repurposing
 *       - Cleaning up test or development images
 *
 * @note Relationship to Other Metadata:
 *       - This function only affects sequence fields; other metadata is preserved
 *       - Use aaruf_set_media_sequence() to set new sequence values after clearing
 *       - If reconstructing multi-volume metadata, set sequence before other fields
 *
 * @warning The metadata changes are only written to the image file during aaruf_close().
 *          Changes made by this function are not immediately persisted to disk.
 *
 * @warning After clearing, the sequence information is permanently lost unless:
 *          - The image file is not closed (context remains in memory)
 *          - A backup of the original image exists
 *          - The information is reconstructed from external sources
 *
 * @see aaruf_set_media_sequence() for setting sequence information.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_clear_media_sequence(void *context)
{
    TRACE("Entering aaruf_clear_media_sequence(%p)", context);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_media_sequence() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_media_sequence() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_clear_media_sequence() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock)
    {
        TRACE("Exiting aaruf_clear_media_sequence() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    ctx->metadata_block_header.mediaSequence     = 0;
    ctx->metadata_block_header.lastMediaSequence = 0;

    // Check if all metadata is clear
    if(ctx->metadata_block_header.creatorLength == 0 && ctx->metadata_block_header.commentsLength == 0 &&
       ctx->metadata_block_header.mediaTitleLength == 0 && ctx->metadata_block_header.mediaManufacturerLength == 0 &&
       ctx->metadata_block_header.mediaModelLength == 0 && ctx->metadata_block_header.mediaSerialNumberLength == 0 &&
       ctx->metadata_block_header.mediaBarcodeLength == 0 && ctx->metadata_block_header.mediaPartNumberLength == 0 &&
       ctx->metadata_block_header.driveModelLength == 0 && ctx->metadata_block_header.driveManufacturerLength == 0 &&
       ctx->metadata_block_header.driveSerialNumberLength == 0 &&
       ctx->metadata_block_header.driveFirmwareRevisionLength == 0)
        ctx->metadata_block_header.identifier = 0;

    TRACE("Exiting aaruf_clear_media_sequence() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Clears the creator (person/operator) information from the image metadata.
 *
 * Removes the creator name string from the AaruFormat image's metadata block, freeing
 * the associated memory and resetting the length field to zero. This effectively removes
 * any record of who created the image. If this operation results in all metadata fields
 * being empty, the entire metadata block is deinitialized to avoid writing an empty
 * metadata block to the image file.
 *
 * This function is useful when anonymizing images for distribution, removing personally
 * identifiable information for privacy compliance (e.g., GDPR), or correcting metadata
 * that was set incorrectly during image creation.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully cleared creator information. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context is opened in write mode (ctx->isWriting is true)
 *         - Either the metadata block was not initialized (early exit, nothing to clear)
 *         - Or the creator string is freed (if it existed) and the pointer set to NULL
 *         - The creatorLength field in the metadata block header is set to 0
 *         - If all metadata fields are now empty, the metadata block identifier is cleared
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_create()
 *
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing. This occurs when:
 *         - The image was opened with aaruf_open() instead of aaruf_create()
 *         - The context's isWriting flag is false
 *         - Attempting to modify a read-only image
 *
 * @note Memory Management:
 *       - If ctx->creator is not NULL, the allocated memory is freed before clearing
 *       - The ctx->creator pointer is set to NULL after freeing
 *       - Safe to call even if creator was never set (NULL check protects against errors)
 *       - No memory leaks occur when clearing previously set creator information
 *
 * @note Privacy and Anonymization:
 *       - Removes personally identifiable information from image metadata
 *       - Important for GDPR compliance and privacy protection
 *       - Consider clearing creator before distributing images publicly
 *       - May be required by institutional privacy policies
 *
 * @note Metadata Block State Management:
 *       - If the metadata block header is not initialized, function returns success immediately
 *       - After clearing, checks if all metadata fields are empty
 *       - If all fields empty, sets metadata block identifier to 0 (deinitialized)
 *       - Prevents writing empty metadata blocks, saving storage space
 *
 * @note Use Cases:
 *       - Anonymizing images for public distribution or research datasets
 *       - Compliance with data protection regulations (GDPR, CCPA, etc.)
 *       - Removing incorrect or outdated operator information
 *       - Preparing images for archival with institutional rather than personal attribution
 *       - Sanitizing test or development images before production use
 *
 * @warning The metadata changes are only written to the image file during aaruf_close().
 *          Changes made by this function are not immediately persisted to disk.
 *
 * @warning After clearing, the creator information is permanently lost unless:
 *          - The image file is not closed (context remains in memory)
 *          - A backup of the original image exists
 *          - The information is reconstructed from external documentation
 *
 * @warning Clearing creator may affect:
 *          - Forensic chain of custody requirements
 *          - Provenance documentation for archival purposes
 *          - Accountability and quality assurance workflows
 *          - Consider legal and institutional requirements before clearing
 *
 * @see aaruf_set_creator() for setting creator information.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_clear_creator(void *context)
{
    TRACE("Entering aaruf_clear_creator(%p)", context);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_creator() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_creator() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_clear_creator() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock)
    {
        TRACE("Exiting aaruf_clear_creator() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    if(ctx->creator != NULL) free(ctx->creator);

    ctx->creator                             = NULL;
    ctx->metadata_block_header.creatorLength = 0;

    // Check if all metadata is clear
    if(ctx->metadata_block_header.mediaSequence == 0 && ctx->metadata_block_header.lastMediaSequence == 0 &&
       ctx->metadata_block_header.commentsLength == 0 && ctx->metadata_block_header.mediaTitleLength == 0 &&
       ctx->metadata_block_header.mediaManufacturerLength == 0 && ctx->metadata_block_header.mediaModelLength == 0 &&
       ctx->metadata_block_header.mediaSerialNumberLength == 0 && ctx->metadata_block_header.mediaBarcodeLength == 0 &&
       ctx->metadata_block_header.mediaPartNumberLength == 0 && ctx->metadata_block_header.driveModelLength == 0 &&
       ctx->metadata_block_header.driveManufacturerLength == 0 &&
       ctx->metadata_block_header.driveSerialNumberLength == 0 &&
       ctx->metadata_block_header.driveFirmwareRevisionLength == 0)
        ctx->metadata_block_header.identifier = 0;

    TRACE("Exiting aaruf_clear_creator() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Clears user comments or notes from the image metadata.
 *
 * Removes the comments string from the AaruFormat image's metadata block, freeing the
 * associated memory and resetting the length field to zero. This effectively removes
 * any user-provided annotations, notes, or descriptions associated with the image.
 * If this operation results in all metadata fields being empty, the entire metadata
 * block is deinitialized to avoid writing an empty metadata block to the image file.
 *
 * This function is useful when removing outdated notes, clearing test comments before
 * production deployment, sanitizing images for distribution, or resetting an image to
 * a clean metadata state for repurposing.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully cleared comments. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context is opened in write mode (ctx->isWriting is true)
 *         - Either the metadata block was not initialized (early exit, nothing to clear)
 *         - Or the comments string is freed (if it existed) and the pointer set to NULL
 *         - The commentsLength field in the metadata block header is set to 0
 *         - If all metadata fields are now empty, the metadata block identifier is cleared
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_create()
 *
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing. This occurs when:
 *         - The image was opened with aaruf_open() instead of aaruf_create()
 *         - The context's isWriting flag is false
 *         - Attempting to modify a read-only image
 *
 * @note Memory Management:
 *       - If ctx->comments is not NULL, the allocated memory is freed before clearing
 *       - The ctx->comments pointer is set to NULL after freeing
 *       - Safe to call even if comments were never set (NULL check protects against errors)
 *       - No memory leaks occur when clearing previously set comments
 *
 * @note Use Cases:
 *       - Removing temporary or development notes before production deployment
 *       - Clearing outdated comments that no longer apply
 *       - Sanitizing images for distribution by removing internal notes
 *       - Resetting images for repurposing with new documentation
 *       - Removing potentially sensitive information from comments
 *       - Preparing images for archival with standardized, minimal metadata
 *
 * @note Metadata Block State Management:
 *       - If the metadata block header is not initialized, function returns success immediately
 *       - After clearing, checks if all metadata fields are empty
 *       - If all fields empty, sets metadata block identifier to 0 (deinitialized)
 *       - Prevents writing empty metadata blocks, optimizing storage efficiency
 *
 * @note Information Loss Considerations:
 *       - Comments may contain valuable provenance or preservation information
 *       - Consider archiving comments externally before clearing if they contain important data
 *       - Comments might document imaging conditions, errors encountered, or quality notes
 *       - Workflow history and processing information may be lost
 *
 * @warning The metadata changes are only written to the image file during aaruf_close().
 *          Changes made by this function are not immediately persisted to disk.
 *
 * @warning After clearing, the comments are permanently lost unless:
 *          - The image file is not closed (context remains in memory)
 *          - A backup of the original image exists
 *          - The comments are documented in external metadata systems
 *
 * @see aaruf_set_comments() for setting comment information.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_clear_comments(void *context)
{
    TRACE("Entering aaruf_clear_comments(%p)", context);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_comments() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_comments() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_clear_comments() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock)
    {
        TRACE("Exiting aaruf_clear_comments() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    if(ctx->comments != NULL) free(ctx->comments);

    ctx->comments                             = NULL;
    ctx->metadata_block_header.commentsLength = 0;

    // Check if all metadata is clear
    if(ctx->metadata_block_header.mediaSequence == 0 && ctx->metadata_block_header.lastMediaSequence == 0 &&
       ctx->metadata_block_header.creatorLength == 0 && ctx->metadata_block_header.mediaTitleLength == 0 &&
       ctx->metadata_block_header.mediaManufacturerLength == 0 && ctx->metadata_block_header.mediaModelLength == 0 &&
       ctx->metadata_block_header.mediaSerialNumberLength == 0 && ctx->metadata_block_header.mediaBarcodeLength == 0 &&
       ctx->metadata_block_header.mediaPartNumberLength == 0 && ctx->metadata_block_header.driveModelLength == 0 &&
       ctx->metadata_block_header.driveManufacturerLength == 0 &&
       ctx->metadata_block_header.driveSerialNumberLength == 0 &&
       ctx->metadata_block_header.driveFirmwareRevisionLength == 0)
        ctx->metadata_block_header.identifier = 0;

    TRACE("Exiting aaruf_clear_comments() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Clears the media title or label from the image metadata.
 *
 * Removes the media title string from the AaruFormat image's metadata block, freeing
 * the associated memory and resetting the length field to zero. This effectively removes
 * any record of the title, label, or name that was printed or written on the physical
 * storage media. If this operation results in all metadata fields being empty, the
 * entire metadata block is deinitialized.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully cleared media title.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid.
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing.
 *
 * @note Memory Management: Frees ctx->media_title if not NULL and sets pointer to NULL.
 * @note Metadata Block: If all fields become empty, the metadata block is deinitialized.
 * @note Use Cases: Removing physical label information, anonymizing media, or correcting errors.
 *
 * @warning Changes are only persisted during aaruf_close(). Lost unless backed up or not closed.
 *
 * @see aaruf_set_media_title() for setting media title information.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_clear_media_title(void *context)
{
    TRACE("Entering aaruf_clear_media_title(%p)", context);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_media_title() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_media_title() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_clear_media_title() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock)
    {
        TRACE("Exiting aaruf_clear_media_title() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    if(ctx->media_title != NULL) free(ctx->media_title);

    ctx->media_title                            = NULL;
    ctx->metadata_block_header.mediaTitleLength = 0;

    // Check if all metadata is clear
    if(ctx->metadata_block_header.mediaSequence == 0 && ctx->metadata_block_header.lastMediaSequence == 0 &&
       ctx->metadata_block_header.creatorLength == 0 && ctx->metadata_block_header.commentsLength == 0 &&
       ctx->metadata_block_header.mediaManufacturerLength == 0 && ctx->metadata_block_header.mediaModelLength == 0 &&
       ctx->metadata_block_header.mediaSerialNumberLength == 0 && ctx->metadata_block_header.mediaBarcodeLength == 0 &&
       ctx->metadata_block_header.mediaPartNumberLength == 0 && ctx->metadata_block_header.driveModelLength == 0 &&
       ctx->metadata_block_header.driveManufacturerLength == 0 &&
       ctx->metadata_block_header.driveSerialNumberLength == 0 &&
       ctx->metadata_block_header.driveFirmwareRevisionLength == 0)
        ctx->metadata_block_header.identifier = 0;

    TRACE("Exiting aaruf_clear_media_title() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Clears the media manufacturer information from the image metadata.
 *
 * Removes the media manufacturer name string from the AaruFormat image's metadata block,
 * freeing the associated memory and resetting the length field to zero. This removes
 * any record of the company that manufactured the physical storage media. If this
 * operation results in all metadata fields being empty, the entire metadata block is
 * deinitialized.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully cleared media manufacturer.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid.
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing.
 *
 * @note Memory Management: Frees ctx->media_manufacturer if not NULL and sets pointer to NULL.
 * @note Preservation Impact: Removes valuable information about media quality and lifespan characteristics.
 * @note Use Cases: Anonymizing media source, removing commercial information, or correcting errors.
 *
 * @warning Loss of manufacturer information may affect preservation planning and media assessment.
 * @warning Changes are only persisted during aaruf_close().
 *
 * @see aaruf_set_media_manufacturer() for setting media manufacturer information.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_clear_media_manufacturer(void *context)
{
    TRACE("Entering aaruf_clear_media_manufacturer(%p)", context);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_media_manufacturer() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_media_manufacturer() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_clear_media_manufacturer() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock)
    {
        TRACE("Exiting aaruf_clear_media_manufacturer() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    if(ctx->media_manufacturer != NULL) free(ctx->media_manufacturer);

    ctx->media_manufacturer                            = NULL;
    ctx->metadata_block_header.mediaManufacturerLength = 0;

    // Check if all metadata is clear
    if(ctx->metadata_block_header.mediaSequence == 0 && ctx->metadata_block_header.lastMediaSequence == 0 &&
       ctx->metadata_block_header.creatorLength == 0 && ctx->metadata_block_header.commentsLength == 0 &&
       ctx->metadata_block_header.mediaTitleLength == 0 && ctx->metadata_block_header.mediaModelLength == 0 &&
       ctx->metadata_block_header.mediaSerialNumberLength == 0 && ctx->metadata_block_header.mediaBarcodeLength == 0 &&
       ctx->metadata_block_header.mediaPartNumberLength == 0 && ctx->metadata_block_header.driveModelLength == 0 &&
       ctx->metadata_block_header.driveManufacturerLength == 0 &&
       ctx->metadata_block_header.driveSerialNumberLength == 0 &&
       ctx->metadata_block_header.driveFirmwareRevisionLength == 0)
        ctx->metadata_block_header.identifier = 0;

    TRACE("Exiting aaruf_clear_media_manufacturer() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Clears the media model or product designation from the image metadata.
 *
 * Removes the media model string from the AaruFormat image's metadata block, freeing
 * the associated memory and resetting the length field to zero. This removes any record
 * of the specific model, product line, or type designation of the physical storage media.
 * If this operation results in all metadata fields being empty, the entire metadata
 * block is deinitialized.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully cleared media model.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid.
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing.
 *
 * @note Memory Management: Frees ctx->media_model if not NULL and sets pointer to NULL.
 * @note Information Loss: Removes specific capacity, speed, and compatibility information.
 * @note Use Cases: Removing product-specific details, generalizing metadata, or correcting errors.
 *
 * @warning Loss of model information may affect media compatibility assessments.
 * @warning Changes are only persisted during aaruf_close().
 *
 * @see aaruf_set_media_model() for setting media model information.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_clear_media_model(void *context)
{
    TRACE("Entering aaruf_clear_media_model(%p)", context);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_media_model() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_media_model() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_clear_media_model() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock)
    {
        TRACE("Exiting aaruf_clear_media_model() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    if(ctx->media_model != NULL) free(ctx->media_model);

    ctx->media_model                            = NULL;
    ctx->metadata_block_header.mediaModelLength = 0;

    // Check if all metadata is clear
    if(ctx->metadata_block_header.mediaSequence == 0 && ctx->metadata_block_header.lastMediaSequence == 0 &&
       ctx->metadata_block_header.creatorLength == 0 && ctx->metadata_block_header.commentsLength == 0 &&
       ctx->metadata_block_header.mediaTitleLength == 0 && ctx->metadata_block_header.mediaManufacturerLength == 0 &&
       ctx->metadata_block_header.mediaSerialNumberLength == 0 && ctx->metadata_block_header.mediaBarcodeLength == 0 &&
       ctx->metadata_block_header.mediaPartNumberLength == 0 && ctx->metadata_block_header.driveModelLength == 0 &&
       ctx->metadata_block_header.driveManufacturerLength == 0 &&
       ctx->metadata_block_header.driveSerialNumberLength == 0 &&
       ctx->metadata_block_header.driveFirmwareRevisionLength == 0)
        ctx->metadata_block_header.identifier = 0;

    TRACE("Exiting aaruf_clear_media_model() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Clears the media serial number from the image metadata.
 *
 * Removes the media serial number string from the AaruFormat image's metadata block,
 * freeing the associated memory and resetting the length field to zero. This removes
 * the unique identifier assigned to the specific piece of physical storage media by
 * the manufacturer. If this operation results in all metadata fields being empty,
 * the entire metadata block is deinitialized.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully cleared media serial number.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid.
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing.
 *
 * @note Memory Management: Frees ctx->media_serial_number if not NULL and sets pointer to NULL.
 * @note Forensic Impact: Removes unique identification critical for chain of custody.
 * @note Privacy: May be necessary for anonymizing specific media instances.
 * @note Use Cases: Privacy compliance, removing tracking identifiers, or correcting errors.
 *
 * @warning Loss of serial number eliminates unique media instance identification.
 * @warning May affect forensic chain of custody and authentication capabilities.
 * @warning Changes are only persisted during aaruf_close().
 *
 * @see aaruf_set_media_serial_number() for setting media serial number information.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_clear_media_serial_number(void *context)
{
    TRACE("Entering aaruf_clear_media_serial_number(%p)", context);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_media_serial_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_media_serial_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_clear_media_serial_number() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock)
    {
        TRACE("Exiting aaruf_clear_media_serial_number() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    if(ctx->media_serial_number != NULL) free(ctx->media_serial_number);

    ctx->media_serial_number                           = NULL;
    ctx->metadata_block_header.mediaSerialNumberLength = 0;

    // Check if all metadata is clear
    if(ctx->metadata_block_header.mediaSequence == 0 && ctx->metadata_block_header.lastMediaSequence == 0 &&
       ctx->metadata_block_header.creatorLength == 0 && ctx->metadata_block_header.commentsLength == 0 &&
       ctx->metadata_block_header.mediaTitleLength == 0 && ctx->metadata_block_header.mediaManufacturerLength == 0 &&
       ctx->metadata_block_header.mediaModelLength == 0 && ctx->metadata_block_header.mediaBarcodeLength == 0 &&
       ctx->metadata_block_header.mediaPartNumberLength == 0 && ctx->metadata_block_header.driveModelLength == 0 &&
       ctx->metadata_block_header.driveManufacturerLength == 0 &&
       ctx->metadata_block_header.driveSerialNumberLength == 0 &&
       ctx->metadata_block_header.driveFirmwareRevisionLength == 0)
        ctx->metadata_block_header.identifier = 0;

    TRACE("Exiting aaruf_clear_media_serial_number() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Clears the media barcode information from the image metadata.
 *
 * Removes the media barcode string from the AaruFormat image's metadata block, freeing
 * the associated memory and resetting the length field to zero. This removes any record
 * of the barcode affixed to the physical storage media or its packaging, typically used
 * in professional archival and library inventory systems. If this operation results in
 * all metadata fields being empty, the entire metadata block is deinitialized.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully cleared media barcode.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid.
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing.
 *
 * @note Memory Management: Frees ctx->media_barcode if not NULL and sets pointer to NULL.
 * @note Inventory Impact: Removes correlation with physical media location systems.
 * @note Use Cases: Anonymizing media, removing institutional tracking, or correcting errors.
 * @note Archive Systems: May affect automated retrieval and inventory management.
 *
 * @warning Loss of barcode breaks links to physical inventory and asset management systems.
 * @warning May affect robotic tape library operations and automated retrieval.
 * @warning Changes are only persisted during aaruf_close().
 *
 * @see aaruf_set_media_barcode() for setting media barcode information.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_clear_media_barcode(void *context)
{
    TRACE("Entering aaruf_clear_media_barcode(%p)", context);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_media_barcode() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_media_barcode() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_clear_media_barcode() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock)
    {
        TRACE("Exiting aaruf_clear_media_barcode() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    if(ctx->media_barcode != NULL) free(ctx->media_barcode);

    ctx->media_barcode                            = NULL;
    ctx->metadata_block_header.mediaBarcodeLength = 0;

    // Check if all metadata is clear
    if(ctx->metadata_block_header.mediaSequence == 0 && ctx->metadata_block_header.lastMediaSequence == 0 &&
       ctx->metadata_block_header.creatorLength == 0 && ctx->metadata_block_header.commentsLength == 0 &&
       ctx->metadata_block_header.mediaTitleLength == 0 && ctx->metadata_block_header.mediaManufacturerLength == 0 &&
       ctx->metadata_block_header.mediaModelLength == 0 && ctx->metadata_block_header.mediaSerialNumberLength == 0 &&
       ctx->metadata_block_header.mediaPartNumberLength == 0 && ctx->metadata_block_header.driveModelLength == 0 &&
       ctx->metadata_block_header.driveManufacturerLength == 0 &&
       ctx->metadata_block_header.driveSerialNumberLength == 0 &&
       ctx->metadata_block_header.driveFirmwareRevisionLength == 0)
        ctx->metadata_block_header.identifier = 0;

    TRACE("Exiting aaruf_clear_media_barcode() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Clears the media part number or model designation from the image metadata.
 *
 * Removes the media part number string from the AaruFormat image's metadata block,
 * freeing the associated memory and resetting the length field to zero. This removes
 * the manufacturer's part number or catalog designation used for procurement and exact
 * product identification. If this operation results in all metadata fields being empty,
 * the entire metadata block is deinitialized.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully cleared media part number.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid.
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing.
 *
 * @note Memory Management: Frees ctx->media_part_number if not NULL and sets pointer to NULL.
 * @note Procurement Impact: Removes exact ordering and catalog reference information.
 * @note Use Cases: Removing commercial details, generalizing metadata, or correcting errors.
 * @note Documentation: May affect ability to source compatible replacement media.
 *
 * @warning Loss of part number affects precise product identification and procurement.
 * @warning May impact ability to cross-reference with manufacturer specifications.
 * @warning Changes are only persisted during aaruf_close().
 *
 * @see aaruf_set_media_part_number() for setting media part number information.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_clear_media_part_number(void *context)
{
    TRACE("Entering aaruf_clear_media_part_number(%p)", context);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_media_part_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_media_part_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_clear_media_part_number() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock)
    {
        TRACE("Exiting aaruf_clear_media_part_number() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    if(ctx->media_part_number != NULL) free(ctx->media_part_number);

    ctx->media_part_number                           = NULL;
    ctx->metadata_block_header.mediaPartNumberLength = 0;

    // Check if all metadata is clear
    if(ctx->metadata_block_header.mediaSequence == 0 && ctx->metadata_block_header.lastMediaSequence == 0 &&
       ctx->metadata_block_header.creatorLength == 0 && ctx->metadata_block_header.commentsLength == 0 &&
       ctx->metadata_block_header.mediaTitleLength == 0 && ctx->metadata_block_header.mediaManufacturerLength == 0 &&
       ctx->metadata_block_header.mediaModelLength == 0 && ctx->metadata_block_header.mediaSerialNumberLength == 0 &&
       ctx->metadata_block_header.mediaBarcodeLength == 0 && ctx->metadata_block_header.driveModelLength == 0 &&
       ctx->metadata_block_header.driveManufacturerLength == 0 &&
       ctx->metadata_block_header.driveSerialNumberLength == 0 &&
       ctx->metadata_block_header.driveFirmwareRevisionLength == 0)
        ctx->metadata_block_header.identifier = 0;

    TRACE("Exiting aaruf_clear_media_part_number() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Clears the drive manufacturer information from the image metadata.
 *
 * Removes the drive manufacturer name string from the AaruFormat image's metadata block,
 * freeing the associated memory and resetting the length field to zero. This removes
 * any record of the company that manufactured the drive or device used to read or write
 * the physical storage media during the imaging process. If this operation results in
 * all metadata fields being empty, the entire metadata block is deinitialized.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully cleared drive manufacturer.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid.
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing.
 *
 * @note Memory Management: Frees ctx->drive_manufacturer if not NULL and sets pointer to NULL.
 * @note Provenance Impact: Removes imaging equipment context from preservation metadata.
 * @note Use Cases: Anonymizing imaging environment, removing commercial info, or correcting errors.
 * @note Quality Assessment: May affect ability to evaluate imaging tool quality and reliability.
 *
 * @warning Loss of drive manufacturer information reduces imaging process documentation.
 * @warning May affect forensic provenance and imaging environment reconstruction.
 * @warning Changes are only persisted during aaruf_close().
 *
 * @see aaruf_set_drive_manufacturer() for setting drive manufacturer information.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_clear_drive_manufacturer(void *context)
{
    TRACE("Entering aaruf_clear_drive_manufacturer(%p)", context);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_drive_manufacturer() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_drive_manufacturer() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_clear_drive_manufacturer() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock)
    {
        TRACE("Exiting aaruf_clear_drive_manufacturer() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    if(ctx->drive_manufacturer != NULL) free(ctx->drive_manufacturer);

    ctx->drive_manufacturer                            = NULL;
    ctx->metadata_block_header.driveManufacturerLength = 0;

    // Check if all metadata is clear
    if(ctx->metadata_block_header.mediaSequence == 0 && ctx->metadata_block_header.lastMediaSequence == 0 &&
       ctx->metadata_block_header.creatorLength == 0 && ctx->metadata_block_header.commentsLength == 0 &&
       ctx->metadata_block_header.mediaTitleLength == 0 && ctx->metadata_block_header.mediaManufacturerLength == 0 &&
       ctx->metadata_block_header.mediaModelLength == 0 && ctx->metadata_block_header.mediaSerialNumberLength == 0 &&
       ctx->metadata_block_header.mediaBarcodeLength == 0 && ctx->metadata_block_header.mediaPartNumberLength == 0 &&
       ctx->metadata_block_header.driveModelLength == 0 && ctx->metadata_block_header.driveSerialNumberLength == 0 &&
       ctx->metadata_block_header.driveFirmwareRevisionLength == 0)
        ctx->metadata_block_header.identifier = 0;

    TRACE("Exiting aaruf_clear_drive_manufacturer() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Clears the drive model information from the image metadata.
 *
 * Removes the drive model string from the AaruFormat image's metadata block, freeing
 * the associated memory and resetting the length field to zero. This removes any record
 * of the specific model or product designation of the drive or device used to read or
 * write the physical storage media during the imaging process. If this operation results
 * in all metadata fields being empty, the entire metadata block is deinitialized.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully cleared drive model.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid.
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing.
 *
 * @note Memory Management: Frees ctx->drive_model if not NULL and sets pointer to NULL.
 * @note Technical Context: Removes specific drive capability and feature information.
 * @note Use Cases: Anonymizing imaging equipment, simplifying metadata, or correcting errors.
 * @note Troubleshooting: May affect ability to diagnose model-specific imaging issues.
 *
 * @warning Loss of drive model information reduces imaging tool documentation completeness.
 * @warning May affect understanding of drive-specific capabilities or limitations.
 * @warning Important for reproducing imaging conditions or troubleshooting issues.
 * @warning Changes are only persisted during aaruf_close().
 *
 * @see aaruf_set_drive_model() for setting drive model information.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_clear_drive_model(void *context)
{
    TRACE("Entering aaruf_clear_drive_model(%p)", context);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_drive_model() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_drive_model() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_clear_drive_model() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock)
    {
        TRACE("Exiting aaruf_clear_drive_model() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    if(ctx->drive_model != NULL) free(ctx->drive_model);

    ctx->drive_model                            = NULL;
    ctx->metadata_block_header.driveModelLength = 0;

    // Check if all metadata is clear
    if(ctx->metadata_block_header.mediaSequence == 0 && ctx->metadata_block_header.lastMediaSequence == 0 &&
       ctx->metadata_block_header.creatorLength == 0 && ctx->metadata_block_header.commentsLength == 0 &&
       ctx->metadata_block_header.mediaTitleLength == 0 && ctx->metadata_block_header.mediaManufacturerLength == 0 &&
       ctx->metadata_block_header.mediaModelLength == 0 && ctx->metadata_block_header.mediaSerialNumberLength == 0 &&
       ctx->metadata_block_header.mediaBarcodeLength == 0 && ctx->metadata_block_header.mediaPartNumberLength == 0 &&
       ctx->metadata_block_header.driveManufacturerLength == 0 &&
       ctx->metadata_block_header.driveSerialNumberLength == 0 &&
       ctx->metadata_block_header.driveFirmwareRevisionLength == 0)
        ctx->metadata_block_header.identifier = 0;

    TRACE("Exiting aaruf_clear_drive_model() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Clears the drive serial number from the image metadata.
 *
 * Removes the drive serial number string from the AaruFormat image's metadata block,
 * freeing the associated memory and resetting the length field to zero. This removes
 * the unique identifier of the specific drive or device used to read or write the
 * physical storage media during the imaging process. If this operation results in
 * all metadata fields being empty, the entire metadata block is deinitialized.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully cleared drive serial number.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid.
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing.
 *
 * @note Memory Management: Frees ctx->drive_serial_number if not NULL and sets pointer to NULL.
 * @note Forensic Impact: Removes unique hardware identification from imaging provenance.
 * @note Privacy: May be necessary for anonymizing specific equipment instances.
 * @note Use Cases: Privacy compliance, equipment anonymization, or correcting errors.
 * @note Equipment Tracking: Breaks correlation with equipment maintenance and calibration records.
 *
 * @warning Loss of drive serial number eliminates unique hardware instance identification.
 * @warning May affect forensic chain of custody documentation requirements.
 * @warning Impacts ability to correlate images with specific equipment for quality analysis.
 * @warning Removes equipment tracking capability for maintenance and workload management.
 * @warning Changes are only persisted during aaruf_close().
 *
 * @see aaruf_set_drive_serial_number() for setting drive serial number information.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_clear_drive_serial_number(void *context)
{
    TRACE("Entering aaruf_clear_drive_serial_number(%p)", context);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_drive_serial_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_drive_serial_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_clear_drive_serial_number() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock)
    {
        TRACE("Exiting aaruf_clear_drive_serial_number() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    if(ctx->drive_serial_number != NULL) free(ctx->drive_serial_number);

    ctx->drive_serial_number                           = NULL;
    ctx->metadata_block_header.driveSerialNumberLength = 0;

    // Check if all metadata is clear
    if(ctx->metadata_block_header.mediaSequence == 0 && ctx->metadata_block_header.lastMediaSequence == 0 &&
       ctx->metadata_block_header.creatorLength == 0 && ctx->metadata_block_header.commentsLength == 0 &&
       ctx->metadata_block_header.mediaTitleLength == 0 && ctx->metadata_block_header.mediaManufacturerLength == 0 &&
       ctx->metadata_block_header.mediaModelLength == 0 && ctx->metadata_block_header.mediaSerialNumberLength == 0 &&
       ctx->metadata_block_header.mediaBarcodeLength == 0 && ctx->metadata_block_header.mediaPartNumberLength == 0 &&
       ctx->metadata_block_header.driveModelLength == 0 && ctx->metadata_block_header.driveManufacturerLength == 0 &&
       ctx->metadata_block_header.driveFirmwareRevisionLength == 0)
        ctx->metadata_block_header.identifier = 0;

    TRACE("Exiting aaruf_clear_drive_serial_number() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Clears the drive firmware revision from the image metadata.
 *
 * Removes the drive firmware revision string from the AaruFormat image's metadata block,
 * freeing the associated memory and resetting the length field to zero. This removes
 * any record of the firmware version or revision of the drive or device used to read
 * or write the physical storage media during the imaging process. If this operation
 * results in all metadata fields being empty, the entire metadata block is deinitialized.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully cleared drive firmware revision.
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid.
 * @retval AARUF_READ_ONLY (-13) The context is not opened for writing.
 *
 * @note Memory Management: Frees ctx->drive_firmware_revision if not NULL and sets pointer to NULL.
 * @note Technical Impact: Removes specific firmware behavior and capability documentation.
 * @note Use Cases: Simplifying metadata, removing technical details, or correcting errors.
 * @note Troubleshooting: May affect ability to diagnose firmware-specific issues.
 * @note Reproducibility: Reduces ability to reproduce exact imaging conditions.
 *
 * @warning Loss of firmware revision information reduces imaging environment documentation.
 * @warning Firmware versions significantly affect drive behavior and error handling.
 * @warning May affect ability to identify firmware-related imaging issues or quirks.
 * @warning Impacts scientific reproducibility and technical troubleshooting capabilities.
 * @warning Changes are only persisted during aaruf_close().
 *
 * @see aaruf_set_drive_firmware_revision() for setting drive firmware revision information.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_clear_drive_firmware_revision(void *context)
{
    TRACE("Entering aaruf_clear_drive_firmware_revision(%p)", context);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_drive_firmware_revision() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_drive_firmware_revision() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_clear_drive_firmware_revision() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    if(ctx->metadata_block_header.identifier != MetadataBlock)
    {
        TRACE("Exiting aaruf_clear_drive_firmware_revision() = AARUF_STATUS_OK");
        return AARUF_STATUS_OK;
    }

    if(ctx->drive_firmware_revision != NULL) free(ctx->drive_firmware_revision);

    ctx->drive_firmware_revision                           = NULL;
    ctx->metadata_block_header.driveFirmwareRevisionLength = 0;

    // Check if all metadata is clear
    if(ctx->metadata_block_header.mediaSequence == 0 && ctx->metadata_block_header.lastMediaSequence == 0 &&
       ctx->metadata_block_header.creatorLength == 0 && ctx->metadata_block_header.commentsLength == 0 &&
       ctx->metadata_block_header.mediaTitleLength == 0 && ctx->metadata_block_header.mediaManufacturerLength == 0 &&
       ctx->metadata_block_header.mediaModelLength == 0 && ctx->metadata_block_header.mediaSerialNumberLength == 0 &&
       ctx->metadata_block_header.mediaBarcodeLength == 0 && ctx->metadata_block_header.mediaPartNumberLength == 0 &&
       ctx->metadata_block_header.driveModelLength == 0 && ctx->metadata_block_header.driveManufacturerLength == 0 &&
       ctx->metadata_block_header.driveSerialNumberLength == 0)
        ctx->metadata_block_header.identifier = 0;

    TRACE("Exiting aaruf_clear_drive_firmware_revision() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}
