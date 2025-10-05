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
 * @retval AARUF_ERROR_CANNOT_READ_BLOCK (-6) The geometry block is not present. This occurs when:
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
int32_t aaruf_get_geometry(const void *context, uint32_t *cylinders, uint32_t *heads, uint32_t *sectors_per_track)
{
    TRACE("Entering aaruf_get_geometry(%p, %u, %u, %u)", context, *cylinders, *heads, *sectors_per_track);

    const aaruformatContext *ctx = NULL;

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

    if(ctx->geometryBlock.identifier != GeometryBlock)
    {
        FATAL("No geometry block present");

        TRACE("Exiting aaruf_get_geometry() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    *cylinders         = ctx->geometryBlock.cylinders;
    *heads             = ctx->geometryBlock.heads;
    *sectors_per_track = ctx->geometryBlock.sectorsPerTrack;

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
int32_t aaruf_set_geometry(void *context, const uint32_t cylinders, const uint32_t heads,
                           const uint32_t sectors_per_track)
{
    TRACE("Entering aaruf_set_geometry(%p, %u, %u, %u)", context, cylinders, heads, sectors_per_track);

    aaruformatContext *ctx = NULL;

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
    if(!ctx->isWriting)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_write_sector() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    ctx->geometryBlock.identifier      = GeometryBlock;
    ctx->geometryBlock.cylinders       = cylinders;
    ctx->geometryBlock.heads           = heads;
    ctx->geometryBlock.sectorsPerTrack = sectors_per_track;
    ctx->imageInfo.Cylinders           = cylinders;
    ctx->imageInfo.Heads               = heads;
    ctx->imageInfo.SectorsPerTrack     = sectors_per_track;

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
int32_t aaruf_set_media_sequence(void *context, const int32_t sequence, const int32_t last_sequence)
{
    TRACE("Entering aaruf_set_media_sequence(%p, %d, %d)", context, sequence, last_sequence);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_sequence() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformatContext *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_sequence() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->isWriting)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_media_sequence() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadataBlockHeader.identifier != MetadataBlock) ctx->metadataBlockHeader.identifier = MetadataBlock;

    ctx->metadataBlockHeader.mediaSequence     = sequence;
    ctx->metadataBlockHeader.lastMediaSequence = last_sequence;

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
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-8) Memory allocation failed. This occurs when:
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
int32_t aaruf_set_creator(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_creator(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_creator() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformatContext *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_creator() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->isWriting)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_creator() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadataBlockHeader.identifier != MetadataBlock) ctx->metadataBlockHeader.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for creator");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->imageInfo.Creator != NULL) free(ctx->imageInfo.Creator);
    ctx->imageInfo.Creator                 = copy;
    ctx->metadataBlockHeader.creatorLength = length;

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
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-8) Memory allocation failed. This occurs when:
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
int32_t aaruf_set_comments(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_comments(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_comments() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformatContext *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_comments() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->isWriting)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_comments() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadataBlockHeader.identifier != MetadataBlock) ctx->metadataBlockHeader.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for comments");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->imageInfo.Comments != NULL) free(ctx->imageInfo.Comments);
    ctx->imageInfo.Comments                 = copy;
    ctx->metadataBlockHeader.commentsLength = length;

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
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-8) Memory allocation failed. This occurs when:
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
int32_t aaruf_set_media_title(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_media_title(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_title() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformatContext *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_title() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->isWriting)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_media_title() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadataBlockHeader.identifier != MetadataBlock) ctx->metadataBlockHeader.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for media title");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->imageInfo.MediaTitle != NULL) free(ctx->imageInfo.MediaTitle);
    ctx->imageInfo.MediaTitle                 = copy;
    ctx->metadataBlockHeader.mediaTitleLength = length;

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
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-8) Memory allocation failed. This occurs when:
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
int32_t aaruf_set_media_manufacturer(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_media_manufacturer(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_manufacturer() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformatContext *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_manufacturer() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->isWriting)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_media_manufacturer() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadataBlockHeader.identifier != MetadataBlock) ctx->metadataBlockHeader.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for media manufacturer");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->imageInfo.MediaManufacturer != NULL) free(ctx->imageInfo.MediaManufacturer);
    ctx->imageInfo.MediaManufacturer                 = copy;
    ctx->metadataBlockHeader.mediaManufacturerLength = length;

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
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-8) Memory allocation failed. This occurs when:
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
int32_t aaruf_set_media_model(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_media_model(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_model() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformatContext *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_model() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->isWriting)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_media_model() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadataBlockHeader.identifier != MetadataBlock) ctx->metadataBlockHeader.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for media model");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->imageInfo.MediaModel != NULL) free(ctx->imageInfo.MediaModel);
    ctx->imageInfo.MediaModel                 = copy;
    ctx->metadataBlockHeader.mediaModelLength = length;

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
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-8) Memory allocation failed. This occurs when:
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
int32_t aaruf_set_media_serial_number(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_media_serial_number(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_serial_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformatContext *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_serial_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->isWriting)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_media_serial_number() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadataBlockHeader.identifier != MetadataBlock) ctx->metadataBlockHeader.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for media serial number");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->imageInfo.MediaSerialNumber != NULL) free(ctx->imageInfo.MediaSerialNumber);
    ctx->imageInfo.MediaSerialNumber                 = copy;
    ctx->metadataBlockHeader.mediaSerialNumberLength = length;

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
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-8) Memory allocation failed. This occurs when:
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
int32_t aaruf_set_media_barcode(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_media_barcode(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_barcode() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformatContext *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_barcode() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->isWriting)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_media_barcode() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadataBlockHeader.identifier != MetadataBlock) ctx->metadataBlockHeader.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for media barcode");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->imageInfo.MediaBarcode != NULL) free(ctx->imageInfo.MediaBarcode);
    ctx->imageInfo.MediaBarcode                 = copy;
    ctx->metadataBlockHeader.mediaBarcodeLength = length;

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
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-8) Memory allocation failed. This occurs when:
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
int32_t aaruf_set_media_part_number(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_media_part_number(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_part_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformatContext *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_media_part_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->isWriting)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_media_part_number() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadataBlockHeader.identifier != MetadataBlock) ctx->metadataBlockHeader.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for creator");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->imageInfo.MediaPartNumber != NULL) free(ctx->imageInfo.MediaPartNumber);
    ctx->imageInfo.MediaPartNumber                 = copy;
    ctx->metadataBlockHeader.mediaPartNumberLength = length;

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
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-8) Memory allocation failed. This occurs when:
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
int32_t aaruf_set_drive_manufacturer(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_drive_manufacturer(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_drive_manufacturer() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformatContext *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_drive_manufacturer() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->isWriting)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_drive_manufacturer() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadataBlockHeader.identifier != MetadataBlock) ctx->metadataBlockHeader.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for drive manufacturer");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->imageInfo.DriveManufacturer != NULL) free(ctx->imageInfo.DriveManufacturer);
    ctx->imageInfo.DriveManufacturer                 = copy;
    ctx->metadataBlockHeader.driveManufacturerLength = length;

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
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-8) Memory allocation failed. This occurs when:
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
int32_t aaruf_set_drive_model(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_drive_model(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_drive_model() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformatContext *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_drive_model() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->isWriting)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_drive_model() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadataBlockHeader.identifier != MetadataBlock) ctx->metadataBlockHeader.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for media model");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->imageInfo.DriveModel != NULL) free(ctx->imageInfo.DriveModel);
    ctx->imageInfo.DriveModel                 = copy;
    ctx->metadataBlockHeader.driveModelLength = length;

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
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-8) Memory allocation failed. This occurs when:
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
int32_t aaruf_set_drive_serial_number(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_drive_serial_number(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_drive_serial_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformatContext *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_drive_serial_number() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->isWriting)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_drive_serial_number() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadataBlockHeader.identifier != MetadataBlock) ctx->metadataBlockHeader.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for drive serial number");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->imageInfo.DriveSerialNumber != NULL) free(ctx->imageInfo.DriveSerialNumber);
    ctx->imageInfo.DriveSerialNumber                 = copy;
    ctx->metadataBlockHeader.driveSerialNumberLength = length;

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
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-8) Memory allocation failed. This occurs when:
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
int32_t aaruf_set_drive_firmware_revision(void *context, const uint8_t *data, const int32_t length)
{
    TRACE("Entering aaruf_set_drive_firmware_revision(%p, %p, %d)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_drive_firmware_revision() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformatContext *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_drive_firmware_revision() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->isWriting)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_drive_firmware_revision() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    // Initialize
    if(ctx->metadataBlockHeader.identifier != MetadataBlock) ctx->metadataBlockHeader.identifier = MetadataBlock;

    // Reserve memory
    uint8_t *copy = malloc(length);
    if(copy == NULL)
    {
        FATAL("Could not allocate memory for creator");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy opaque UTF-16LE string
    memcpy(copy, data, length);
    if(ctx->imageInfo.DriveFirmwareRevision != NULL) free(ctx->imageInfo.DriveFirmwareRevision);
    ctx->imageInfo.DriveFirmwareRevision                 = copy;
    ctx->metadataBlockHeader.driveFirmwareRevisionLength = length;

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
 * @retval AARUF_ERROR_CANNOT_READ_BLOCK (-6) The CICM block is not present. This occurs when:
 *         - The image was created without CICM XML metadata
 *         - ctx->cicmBlock is NULL (no data loaded)
 *         - ctx->cicmBlockHeader.length is 0 (empty metadata)
 *         - ctx->cicmBlockHeader.identifier doesn't equal CicmBlock
 *         - The CICM block was not found during image opening
 *         - The *length output parameter is set to 0 to indicate no data available
 *
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-12) The provided buffer is insufficient. This occurs when:
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
int32_t aaruf_get_cicm_metadata(const void *context, uint8_t *buffer, size_t *length)
{
    TRACE("Entering aaruf_get_cicm_metadata(%p, %p, %d)", context, buffer, *length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_cicm_metadata() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformatContext *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_cicm_metadata() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->cicmBlock == NULL || ctx->cicmBlockHeader.length == 0 || ctx->cicmBlockHeader.identifier != CicmBlock)
    {
        TRACE("No CICM XML metadata present");
        *length = 0;

        TRACE("Exiting aaruf_get_cicm_metadata() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    if(*length < ctx->cicmBlockHeader.length)
    {
        TRACE("Buffer too small for CICM XML metadata, required %u bytes", ctx->cicmBlockHeader.length);
        *length = ctx->cicmBlockHeader.length;

        TRACE("Exiting aaruf_get_cicm_metadata() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    *length = ctx->cicmBlockHeader.length;
    memcpy(buffer, ctx->cicmBlock, ctx->cicmBlockHeader.length);

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
 * @retval AARUF_ERROR_CANNOT_READ_BLOCK (-6) The Aaru JSON block is not present. This occurs when:
 *         - The image was created without Aaru metadata JSON
 *         - ctx->jsonBlock is NULL (no data loaded)
 *         - ctx->jsonBlockHeader.length is 0 (empty metadata)
 *         - ctx->jsonBlockHeader.identifier doesn't equal AaruMetadataJsonBlock
 *         - The Aaru JSON block was not found during image opening
 *         - The *length output parameter is set to 0 to indicate no data available
 *
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-12) The provided buffer is insufficient. This occurs when:
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
int32_t aaruf_get_aaru_json_metadata(const void *context, uint8_t *buffer, size_t *length)
{
    TRACE("Entering aaruf_get_aaru_json_metadata(%p, %p, %d)", context, buffer, *length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_aaru_json_metadata() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformatContext *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_aaru_json_metadata() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->jsonBlock == NULL || ctx->jsonBlockHeader.length == 0 ||
       ctx->jsonBlockHeader.identifier != AaruMetadataJsonBlock)
    {
        TRACE("No Aaru metadata JSON present");
        *length = 0;

        TRACE("Exiting aaruf_get_aaru_json_metadata() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    if(*length < ctx->jsonBlockHeader.length)
    {
        TRACE("Buffer too small for Aaru metadata JSON, required %u bytes", ctx->jsonBlockHeader.length);
        *length = ctx->jsonBlockHeader.length;

        TRACE("Exiting aaruf_get_aaru_json_metadata() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    *length = ctx->jsonBlockHeader.length;
    memcpy(buffer, ctx->jsonBlock, ctx->jsonBlockHeader.length);

    TRACE("Aaru metadata JSON read successfully, length %u", *length);
    TRACE("Exiting aaruf_get_aaru_json_metadata(%p, %p, %d) = AARUF_STATUS_OK", context, buffer, *length);
    return AARUF_STATUS_OK;
}