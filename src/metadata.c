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

        TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_read_sector_long() = AARUF_ERROR_NOT_AARUFORMAT");
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

    return AARUF_STATUS_OK;
}