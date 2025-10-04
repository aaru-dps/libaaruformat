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