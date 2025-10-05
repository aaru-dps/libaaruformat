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
 * @brief Retrieves the dump hardware block containing acquisition environment information.
 *
 * Extracts the complete DumpHardwareBlock from the image, which documents the hardware and software
 * environments used to create the image. A dump hardware block records one or more "dump environments" –
 * typically combinations of physical devices (drives, controllers, adapters) and the software stacks
 * that performed the read operations. This metadata is essential for understanding the imaging context,
 * validating acquisition integrity, reproducing imaging conditions, and supporting forensic or archival
 * documentation requirements.
 *
 * Each environment entry includes hardware identification (manufacturer, model, revision, firmware,
 * serial number), software identification (name, version, operating system), and optional extent ranges
 * that specify which logical sectors or units were contributed by that particular environment. This
 * structure supports complex imaging scenarios where multiple devices or software configurations were
 * used to create a composite image.
 *
 * The function reconstructs the complete on-disk binary representation of the dump hardware block,
 * including the DumpHardwareHeader followed by all entries with their variable-length UTF-8 strings
 * and extent arrays. The reconstructed block includes a calculated CRC64 checksum over the payload
 * data for integrity verification.
 *
 * This function supports a two-call pattern for buffer size determination:
 * 1. First call with insufficient buffer (or NULL) returns AARUF_ERROR_BUFFER_TOO_SMALL and sets
 *    *length to the required size (sizeof(DumpHardwareHeader) + total payload length)
 * 2. Second call with properly sized buffer retrieves the complete block data
 *
 * Alternatively, if the caller already knows the buffer is large enough, a single call will succeed
 * and populate the buffer with the complete dump hardware block.
 *
 * @param context Pointer to the aaruformat context (must be a valid, opened image context).
 * @param buffer Pointer to a buffer that will receive the dump hardware block data. Must be large
 *               enough to hold the complete block (at least *length bytes on input). May be NULL
 *               to query the required buffer size. The buffer will contain the DumpHardwareHeader
 *               followed by serialized entries, strings, and extent arrays on success.
 * @param length Pointer to a size_t that serves dual purpose:
 *               - On input: size of the provided buffer in bytes (ignored if buffer is NULL)
 *               - On output: actual size required/used for the dump hardware block in bytes
 *               If the function returns AARUF_ERROR_BUFFER_TOO_SMALL, this will be updated to
 *               contain the required buffer size for a subsequent successful call.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully retrieved dump hardware block. This is returned when:
 *         - The context is valid and properly initialized
 *         - The dump hardware block is present (identifier == DumpHardwareBlock)
 *         - The provided buffer is large enough (>= required length)
 *         - All hardware entries with their strings and extents are copied to the buffer
 *         - The DumpHardwareHeader is written with calculated CRC64 at buffer offset 0
 *         - The *length parameter is set to the actual block size
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *         - The context was not properly initialized by aaruf_open() or aaruf_create()
 *
 * @retval AARUF_ERROR_CANNOT_READ_BLOCK (-6) The dump hardware block is not present. This occurs when:
 *         - The image was created without dump hardware information
 *         - ctx->dumpHardwareEntriesWithData is NULL (no data loaded)
 *         - ctx->dumpHardwareHeader.entries == 0 (no hardware entries)
 *         - ctx->dumpHardwareHeader.identifier doesn't equal DumpHardwareBlock
 *         - The dump hardware block was not found during image opening
 *
 * @retval AARUF_ERROR_BUFFER_TOO_SMALL (-12) The provided buffer is insufficient. This occurs when:
 *         - buffer is NULL (size query mode)
 *         - The input *length is less than sizeof(DumpHardwareHeader) + payload length
 *         - The *length parameter is updated to contain the required buffer size
 *         - No data is copied to the buffer
 *         - The caller should allocate a larger buffer and call again
 *         - Also returned if calculated entry size exceeds buffer during iteration (sanity check)
 *
 * @note Dump Hardware Block Structure:
 *       - DumpHardwareHeader (16 bytes): identifier, entries count, payload length, CRC64
 *       - For each entry (variable size):
 *         * DumpHardwareEntry (36 bytes): length fields for all strings and extent count
 *         * Variable-length UTF-8 strings (in order): manufacturer, model, revision, firmware,
 *           serial, software name, software version, software operating system
 *         * Array of DumpExtent structures (16 bytes each) if extent count > 0
 *       - All strings are UTF-8 encoded and NOT null-terminated in the serialized block
 *       - String lengths are in bytes, not character counts
 *
 * @note Dump Hardware Environments:
 *       - Each entry represents one hardware/software combination used during imaging
 *       - Multiple entries support scenarios where different devices contributed different sectors
 *       - Extent arrays specify which logical sector ranges each environment contributed
 *       - Empty extent arrays (extents == 0) indicate the environment dumped the entire medium
 *       - Overlapping extents between entries may indicate verification passes or redundancy
 *
 * @note Hardware Identification Fields:
 *       - manufacturer: Device manufacturer (e.g., "Plextor", "Sony", "Samsung")
 *       - model: Device model number (e.g., "PX-716A", "DRU-820A")
 *       - revision: Hardware revision identifier
 *       - firmware: Firmware version (e.g., "1.11", "KY08")
 *       - serial: Device serial number for unique identification
 *
 * @note Software Identification Fields:
 *       - softwareName: Dumping software name (e.g., "Aaru", "ddrescue", "IsoBuster")
 *       - softwareVersion: Software version (e.g., "5.3.0", "1.25")
 *       - softwareOperatingSystem: Host OS (e.g., "Linux 5.10.0", "Windows 10", "macOS 12.0")
 *
 * @note CRC64 Calculation:
 *       - The function calculates CRC64-ECMA over the payload (everything after the header)
 *       - The calculated CRC64 is stored in the returned DumpHardwareHeader
 *       - This allows verification of the serialized block integrity
 *       - The CRC64 is computed from buffer data, not from the original context
 *
 * @note Buffer Layout After Successful Call:
 *       - Offset 0: DumpHardwareHeader with calculated CRC64
 *       - Offset 16: First DumpHardwareEntry
 *       - Followed by: First entry's UTF-8 strings (in documented order)
 *       - Followed by: First entry's DumpExtent array (if extents > 0)
 *       - Repeated for all remaining entries
 *
 * @note Use Cases:
 *       - Forensic documentation requiring complete equipment chain of custody
 *       - Archival metadata for long-term preservation requirements
 *       - Reproducing imaging conditions for verification or re-imaging
 *       - Identifying firmware-specific issues or drive-specific behaviors
 *       - Multi-device imaging scenario documentation
 *       - Correlating imaging artifacts with specific hardware/software combinations
 *
 * @warning This function reads from the in-memory dump hardware data loaded during aaruf_open().
 *          It does not perform file I/O operations. The data is reconstructed from the parsed
 *          context structures into the on-disk binary format.
 *
 * @warning The buffer must be valid and large enough to hold the entire dump hardware block.
 *          Passing a buffer smaller than required will result in AARUF_ERROR_BUFFER_TOO_SMALL.
 *
 * @warning String data in the serialized block is NOT null-terminated. Applications parsing
 *          the buffer must use the length fields in DumpHardwareEntry to determine string
 *          boundaries. The library adds null terminators only for in-memory convenience.
 *
 * @warning The function performs bounds checking during serialization. If calculated entry
 *          sizes exceed the buffer length, AARUF_ERROR_BUFFER_TOO_SMALL is returned even
 *          after the initial size check. This should not occur with properly sized buffers
 *          but protects against data corruption.
 *
 * @see DumpHardwareHeader for the block header structure definition.
 * @see DumpHardwareEntry for the per-environment entry structure definition.
 * @see DumpExtent for the extent range structure definition.
 * @see process_dumphw_block() for the loading process during image opening.
 */
int32_t aaruf_get_dumphw(void *context, uint8_t *buffer, size_t *length)
{
    TRACE("Entering aaruf_get_dumphw(%p,  %p, %u)", context, buffer, *length);

    aaruformatContext *ctx = NULL;

    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_dumphw() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_dumphw() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->dumpHardwareEntriesWithData == NULL || ctx->dumpHardwareHeader.entries == 0 ||
       ctx->dumpHardwareHeader.identifier != DumpHardwareBlock)
    {
        FATAL("No dump hardware information present");

        TRACE("Exiting aaruf_get_dumphw() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    size_t required_length = sizeof(DumpHardwareHeader) + ctx->dumpHardwareHeader.length;

    if(buffer == NULL || *length < required_length)
    {
        TRACE("Buffer too small for dump hardware block, required %zu bytes", required_length);
        *length = required_length;

        TRACE("Exiting aaruf_get_dumphw() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    *length = required_length;

    // Start to iterate and copy the data
    size_t offset = 0;
    for(int i = 0; i < ctx->dumpHardwareHeader.entries; i++)
    {
        size_t entry_size = sizeof(DumpHardwareEntry) + ctx->dumpHardwareEntriesWithData[i].entry.manufacturerLength +
                            ctx->dumpHardwareEntriesWithData[i].entry.modelLength +
                            ctx->dumpHardwareEntriesWithData[i].entry.revisionLength +
                            ctx->dumpHardwareEntriesWithData[i].entry.firmwareLength +
                            ctx->dumpHardwareEntriesWithData[i].entry.serialLength +
                            ctx->dumpHardwareEntriesWithData[i].entry.softwareNameLength +
                            ctx->dumpHardwareEntriesWithData[i].entry.softwareVersionLength +
                            ctx->dumpHardwareEntriesWithData[i].entry.softwareOperatingSystemLength +
                            ctx->dumpHardwareEntriesWithData[i].entry.extents * sizeof(DumpExtent);

        if(offset + entry_size > *length)
        {
            FATAL("Calculated size exceeds provided buffer length");
            TRACE("Exiting aaruf_get_dumphw() = AARUF_ERROR_BUFFER_TOO_SMALL");
            return AARUF_ERROR_BUFFER_TOO_SMALL;
        }

        memcpy(buffer + offset, &ctx->dumpHardwareEntriesWithData[i].entry, sizeof(DumpHardwareEntry));
        offset += sizeof(DumpHardwareEntry);
        if(ctx->dumpHardwareEntriesWithData[i].entry.manufacturerLength > 0 &&
           ctx->dumpHardwareEntriesWithData[i].manufacturer != NULL)
        {
            memcpy(buffer + offset, ctx->dumpHardwareEntriesWithData[i].manufacturer,
                   ctx->dumpHardwareEntriesWithData[i].entry.manufacturerLength);
            offset += ctx->dumpHardwareEntriesWithData[i].entry.manufacturerLength;
        }
        if(ctx->dumpHardwareEntriesWithData[i].entry.modelLength > 0 &&
           ctx->dumpHardwareEntriesWithData[i].model != NULL)
        {
            memcpy(buffer + offset, ctx->dumpHardwareEntriesWithData[i].model,
                   ctx->dumpHardwareEntriesWithData[i].entry.modelLength);
            offset += ctx->dumpHardwareEntriesWithData[i].entry.modelLength;
        }
        if(ctx->dumpHardwareEntriesWithData[i].entry.revisionLength > 0 &&
           ctx->dumpHardwareEntriesWithData[i].revision != NULL)
        {
            memcpy(buffer + offset, ctx->dumpHardwareEntriesWithData[i].revision,
                   ctx->dumpHardwareEntriesWithData[i].entry.revisionLength);
            offset += ctx->dumpHardwareEntriesWithData[i].entry.revisionLength;
        }
        if(ctx->dumpHardwareEntriesWithData[i].entry.firmwareLength > 0 &&
           ctx->dumpHardwareEntriesWithData[i].firmware != NULL)
        {
            memcpy(buffer + offset, ctx->dumpHardwareEntriesWithData[i].firmware,
                   ctx->dumpHardwareEntriesWithData[i].entry.firmwareLength);
            offset += ctx->dumpHardwareEntriesWithData[i].entry.firmwareLength;
        }
        if(ctx->dumpHardwareEntriesWithData[i].entry.serialLength > 0 &&
           ctx->dumpHardwareEntriesWithData[i].serial != NULL)
        {
            memcpy(buffer + offset, ctx->dumpHardwareEntriesWithData[i].serial,
                   ctx->dumpHardwareEntriesWithData[i].entry.serialLength);
            offset += ctx->dumpHardwareEntriesWithData[i].entry.serialLength;
        }
        if(ctx->dumpHardwareEntriesWithData[i].entry.softwareNameLength > 0 &&
           ctx->dumpHardwareEntriesWithData[i].softwareName != NULL)
        {
            memcpy(buffer + offset, ctx->dumpHardwareEntriesWithData[i].softwareName,
                   ctx->dumpHardwareEntriesWithData[i].entry.softwareNameLength);
            offset += ctx->dumpHardwareEntriesWithData[i].entry.softwareNameLength;
        }
        if(ctx->dumpHardwareEntriesWithData[i].entry.softwareVersionLength > 0 &&
           ctx->dumpHardwareEntriesWithData[i].softwareVersion != NULL)
        {
            memcpy(buffer + offset, ctx->dumpHardwareEntriesWithData[i].softwareVersion,
                   ctx->dumpHardwareEntriesWithData[i].entry.softwareVersionLength);
            offset += ctx->dumpHardwareEntriesWithData[i].entry.softwareVersionLength;
        }
        if(ctx->dumpHardwareEntriesWithData[i].entry.softwareOperatingSystemLength > 0 &&
           ctx->dumpHardwareEntriesWithData[i].softwareOperatingSystem != NULL)
        {
            memcpy(buffer + offset, ctx->dumpHardwareEntriesWithData[i].softwareOperatingSystem,
                   ctx->dumpHardwareEntriesWithData[i].entry.softwareOperatingSystemLength);
            offset += ctx->dumpHardwareEntriesWithData[i].entry.softwareOperatingSystemLength;
        }
        if(ctx->dumpHardwareEntriesWithData[i].entry.extents > 0 && ctx->dumpHardwareEntriesWithData[i].extents != NULL)
        {
            memcpy(buffer + offset, ctx->dumpHardwareEntriesWithData[i].extents,
                   ctx->dumpHardwareEntriesWithData[i].entry.extents * sizeof(DumpExtent));
            offset += ctx->dumpHardwareEntriesWithData[i].entry.extents * sizeof(DumpExtent);
        }
    }

    // Calculate CRC64
    ctx->dumpHardwareHeader.crc64 =
        aaruf_crc64_data(buffer + sizeof(DumpHardwareHeader), ctx->dumpHardwareHeader.length);

    // Copy header
    memcpy(buffer, &ctx->dumpHardwareHeader, sizeof(DumpHardwareHeader));

    TRACE("Exiting aaruf_get_dumphw() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}