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
#include <stdlib.h>

#include "aaruformat.h"
#include "internal.h"
#include "log.h"

static void free_dump_hardware_entries(DumpHardwareEntriesWithData *entries, uint32_t count)
{
    if(entries == NULL) return;

    for(uint32_t e = 0; e < count; e++)
    {
        free(entries[e].manufacturer);
        free(entries[e].model);
        free(entries[e].revision);
        free(entries[e].firmware);
        free(entries[e].serial);
        free(entries[e].softwareName);
        free(entries[e].softwareVersion);
        free(entries[e].softwareOperatingSystem);
        free(entries[e].extents);
    }

    free(entries);
}

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
AARU_EXPORT int32_t AARU_CALL aaruf_get_dumphw(void *context, uint8_t *buffer, size_t *length)
{
    size_t length_value = 0;
    if(length != NULL) length_value = *length;

    TRACE("Entering aaruf_get_dumphw(%p, %p, %zu)", context, buffer, length_value);

    aaruformat_context *ctx = NULL;

    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_dumphw() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(length == NULL)
    {
        FATAL("Invalid length pointer");

        TRACE("Exiting aaruf_get_dumphw() = AARUF_ERROR_INCORRECT_DATA_SIZE");
        return AARUF_ERROR_INCORRECT_DATA_SIZE;
    }

    ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_dumphw() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->dump_hardware_entries_with_data == NULL || ctx->dump_hardware_header.entries == 0 ||
       ctx->dump_hardware_header.identifier != DumpHardwareBlock)
    {
        FATAL("No dump hardware information present");

        TRACE("Exiting aaruf_get_dumphw() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    size_t required_length = sizeof(DumpHardwareHeader) + (size_t)ctx->dump_hardware_header.length;
    if(required_length < sizeof(DumpHardwareHeader))
    {
        FATAL("Dump hardware payload length overflow");

        TRACE("Exiting aaruf_get_dumphw() = AARUF_ERROR_INCORRECT_DATA_SIZE");
        return AARUF_ERROR_INCORRECT_DATA_SIZE;
    }

    if(buffer == NULL || *length < required_length)
    {
        TRACE("Buffer too small for dump hardware block, required %zu bytes", required_length);
        *length = required_length;

        TRACE("Exiting aaruf_get_dumphw() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    *length = required_length;

    // Start to iterate and copy the data
    size_t offset = sizeof(DumpHardwareHeader);
    for(uint32_t i = 0; i < ctx->dump_hardware_header.entries; i++)
    {
        size_t entry_size = sizeof(DumpHardwareEntry);

        const DumpHardwareEntry *entry = &ctx->dump_hardware_entries_with_data[i].entry;

        const size_t extent_bytes = (size_t)entry->extents * sizeof(DumpExtent);
        if(entry->extents != 0 && extent_bytes / sizeof(DumpExtent) != entry->extents)
        {
            FATAL("Dump hardware extent size overflow");
            TRACE("Exiting aaruf_get_dumphw() = AARUF_ERROR_INCORRECT_DATA_SIZE");
            return AARUF_ERROR_INCORRECT_DATA_SIZE;
        }

        const size_t additive_lengths[] = {entry->manufacturerLength,
                                           entry->modelLength,
                                           entry->revisionLength,
                                           entry->firmwareLength,
                                           entry->serialLength,
                                           entry->softwareNameLength,
                                           entry->softwareVersionLength,
                                           entry->softwareOperatingSystemLength,
                                           extent_bytes};

        for(size_t j = 0; j < sizeof(additive_lengths) / sizeof(additive_lengths[0]); j++)
        {
            if(additive_lengths[j] > SIZE_MAX - entry_size)
            {
                FATAL("Dump hardware entry size overflow");
                TRACE("Exiting aaruf_get_dumphw() = AARUF_ERROR_INCORRECT_DATA_SIZE");
                return AARUF_ERROR_INCORRECT_DATA_SIZE;
            }
            entry_size += additive_lengths[j];
        }

        if(offset + entry_size > *length)
        {
            FATAL("Calculated size exceeds provided buffer length");
            TRACE("Exiting aaruf_get_dumphw() = AARUF_ERROR_BUFFER_TOO_SMALL");
            return AARUF_ERROR_BUFFER_TOO_SMALL;
        }

        memcpy(buffer + offset, &ctx->dump_hardware_entries_with_data[i].entry, sizeof(DumpHardwareEntry));
        offset += sizeof(DumpHardwareEntry);
        if(ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength > 0 &&
           ctx->dump_hardware_entries_with_data[i].manufacturer != NULL)
        {
            memcpy(buffer + offset, ctx->dump_hardware_entries_with_data[i].manufacturer,
                   ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength);
            offset += ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength;
        }
        if(ctx->dump_hardware_entries_with_data[i].entry.modelLength > 0 &&
           ctx->dump_hardware_entries_with_data[i].model != NULL)
        {
            memcpy(buffer + offset, ctx->dump_hardware_entries_with_data[i].model,
                   ctx->dump_hardware_entries_with_data[i].entry.modelLength);
            offset += ctx->dump_hardware_entries_with_data[i].entry.modelLength;
        }
        if(ctx->dump_hardware_entries_with_data[i].entry.revisionLength > 0 &&
           ctx->dump_hardware_entries_with_data[i].revision != NULL)
        {
            memcpy(buffer + offset, ctx->dump_hardware_entries_with_data[i].revision,
                   ctx->dump_hardware_entries_with_data[i].entry.revisionLength);
            offset += ctx->dump_hardware_entries_with_data[i].entry.revisionLength;
        }
        if(ctx->dump_hardware_entries_with_data[i].entry.firmwareLength > 0 &&
           ctx->dump_hardware_entries_with_data[i].firmware != NULL)
        {
            memcpy(buffer + offset, ctx->dump_hardware_entries_with_data[i].firmware,
                   ctx->dump_hardware_entries_with_data[i].entry.firmwareLength);
            offset += ctx->dump_hardware_entries_with_data[i].entry.firmwareLength;
        }
        if(ctx->dump_hardware_entries_with_data[i].entry.serialLength > 0 &&
           ctx->dump_hardware_entries_with_data[i].serial != NULL)
        {
            memcpy(buffer + offset, ctx->dump_hardware_entries_with_data[i].serial,
                   ctx->dump_hardware_entries_with_data[i].entry.serialLength);
            offset += ctx->dump_hardware_entries_with_data[i].entry.serialLength;
        }
        if(ctx->dump_hardware_entries_with_data[i].entry.softwareNameLength > 0 &&
           ctx->dump_hardware_entries_with_data[i].softwareName != NULL)
        {
            memcpy(buffer + offset, ctx->dump_hardware_entries_with_data[i].softwareName,
                   ctx->dump_hardware_entries_with_data[i].entry.softwareNameLength);
            offset += ctx->dump_hardware_entries_with_data[i].entry.softwareNameLength;
        }
        if(ctx->dump_hardware_entries_with_data[i].entry.softwareVersionLength > 0 &&
           ctx->dump_hardware_entries_with_data[i].softwareVersion != NULL)
        {
            memcpy(buffer + offset, ctx->dump_hardware_entries_with_data[i].softwareVersion,
                   ctx->dump_hardware_entries_with_data[i].entry.softwareVersionLength);
            offset += ctx->dump_hardware_entries_with_data[i].entry.softwareVersionLength;
        }
        if(ctx->dump_hardware_entries_with_data[i].entry.softwareOperatingSystemLength > 0 &&
           ctx->dump_hardware_entries_with_data[i].softwareOperatingSystem != NULL)
        {
            memcpy(buffer + offset, ctx->dump_hardware_entries_with_data[i].softwareOperatingSystem,
                   ctx->dump_hardware_entries_with_data[i].entry.softwareOperatingSystemLength);
            offset += ctx->dump_hardware_entries_with_data[i].entry.softwareOperatingSystemLength;
        }
        if(entry->extents > 0 && ctx->dump_hardware_entries_with_data[i].extents != NULL)
        {
            memcpy(buffer + offset, ctx->dump_hardware_entries_with_data[i].extents, extent_bytes);
            offset += extent_bytes;
        }
    }

    // Calculate CRC64
    ctx->dump_hardware_header.crc64 =
        aaruf_crc64_data(buffer + sizeof(DumpHardwareHeader), ctx->dump_hardware_header.length);

    // Copy header
    memcpy(buffer, &ctx->dump_hardware_header, sizeof(DumpHardwareHeader));

    TRACE("Exiting aaruf_get_dumphw() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Sets the dump hardware block for the image during creation.
 *
 * Embeds dump hardware information into the image being created. The dump hardware block documents
 * the hardware and software environments used to create the image, recording one or more "dump
 * environments" – typically combinations of physical devices (drives, controllers, adapters) and
 * the software stacks that performed the read operations. This metadata is essential for understanding
 * the imaging context, validating acquisition integrity, reproducing imaging conditions, and supporting
 * forensic or archival documentation requirements.
 *
 * Each environment entry includes hardware identification (manufacturer, model, revision, firmware,
 * serial number), software identification (name, version, operating system), and optional extent ranges
 * that specify which logical sectors or units were contributed by that particular environment. This
 * structure supports complex imaging scenarios where multiple devices or software configurations were
 * used to create a composite image.
 *
 * The function accepts a complete, pre-serialized DumpHardwareBlock in the on-disk binary format
 * (as returned by aaruf_get_dumphw() or manually constructed). The block is validated for correct
 * identifier, length consistency, and CRC64 integrity before being parsed and stored in the context.
 * The function deserializes the binary block, extracts all entries with their variable-length UTF-8
 * strings and extent arrays, and creates null-terminated in-memory copies for internal use.
 *
 * **Validation performed:**
 * 1. Context validation (non-NULL, correct magic, write mode)
 * 2. Data buffer validation (non-NULL, non-zero length)
 * 3. Block identifier validation (must be DumpHardwareBlock)
 * 4. Length consistency (buffer length must equal sizeof(DumpHardwareHeader) + header.length)
 * 5. CRC64 integrity verification (calculated CRC64 must match header.crc64)
 *
 * **Parsing process:**
 * 1. Read and validate the DumpHardwareHeader from the buffer
 * 2. Allocate array for all dump hardware entries
 * 3. For each entry:
 *    - Read the DumpHardwareEntry structure (36 bytes)
 *    - Allocate and copy each non-empty UTF-8 string with +1 byte for null terminator
 *    - Allocate and copy the DumpExtent array if extents > 0
 * 4. Free any previously set dump hardware data
 * 5. Store the new parsed data in ctx->dumpHardwareEntriesWithData
 * 6. Store the header in ctx->dumpHardwareHeader
 *
 * **Memory management:**
 * If any memory allocation fails during parsing, all previously allocated memory for the new
 * data is freed via the free_copy_and_error label, and AARUF_ERROR_NOT_ENOUGH_MEMORY is returned.
 * Any existing dump hardware data in the context is freed before storing new data, ensuring no
 * memory leaks when replacing dump hardware information.
 *
 * @param context Pointer to the aaruformat context (must be a valid, write-enabled image context).
 * @param data Pointer to the dump hardware block data in on-disk binary format. Must contain a
 *             complete DumpHardwareBlock starting with DumpHardwareHeader followed by all entries,
 *             strings, and extent arrays. Must not be NULL.
 * @param length Length of the dump hardware block data in bytes. Must equal
 *               sizeof(DumpHardwareHeader) + header.length for validation to succeed.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully set dump hardware block. This is returned when:
 *         - The context is valid and properly initialized
 *         - The context is opened in write mode (ctx->isWriting is true)
 *         - The data buffer contains a valid DumpHardwareBlock
 *         - The block identifier is DumpHardwareBlock
 *         - The length is consistent (buffer length == header size + payload length)
 *         - The CRC64 checksum is valid
 *         - All memory allocations succeeded
 *         - All entries with strings and extents are parsed and stored
 *         - Any previous dump hardware data is freed
 *         - ctx->dumpHardwareEntriesWithData is populated with parsed entries
 *         - ctx->dumpHardwareHeader is updated with the new header
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
 * @retval AARUF_ERROR_CANNOT_READ_BLOCK (-6) Invalid block identifier. This occurs when:
 *         - The identifier field in the DumpHardwareHeader doesn't equal DumpHardwareBlock
 *         - The data buffer doesn't contain a valid dump hardware block
 *         - The block type is incorrect or corrupted
 *
 * @retval AARUF_ERROR_INCORRECT_DATA_SIZE (-11) Invalid data or length. This occurs when:
 *         - The data parameter is NULL
 *         - The length parameter is 0 (empty block)
 *         - The buffer length doesn't match sizeof(DumpHardwareHeader) + header.length
 *         - Length inconsistency indicates corrupted or incomplete block data
 *
 * @retval AARUF_ERROR_INVALID_BLOCK_CRC (-10) CRC64 checksum mismatch. This occurs when:
 *         - The calculated CRC64 over the payload doesn't match header.crc64
 *         - Block data is corrupted or tampered with
 *         - Block was not properly constructed or serialized
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-8) Memory allocation failed. This occurs when:
 *         - calloc() or malloc() failed to allocate memory for entries array
 *         - Failed to allocate memory for any string field (manufacturer, model, etc.)
 *         - Failed to allocate memory for extent arrays
 *         - System is out of memory or memory is severely fragmented
 *         - All partially allocated memory is freed before returning
 *
 * @note Dump Hardware Block Format:
 *       - The data buffer must contain a complete serialized DumpHardwareBlock
 *       - Format: DumpHardwareHeader + repeated entries with strings and extents
 *       - All strings are UTF-8 encoded and NOT null-terminated in the buffer
 *       - The function adds null terminators when copying strings to internal storage
 *       - String lengths are in bytes, not character counts
 *
 * @note Creating Block Data:
 *       - Use aaruf_get_dumphw() to retrieve a block from an existing image
 *       - Manually construct by serializing DumpHardwareHeader, entries, strings, and extents
 *       - Calculate CRC64-ECMA over the payload (everything after the header)
 *       - Ensure all length fields accurately reflect the data sizes
 *       - Ensure total buffer size equals sizeof(DumpHardwareHeader) + payload length
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
 * @note Extent Arrays:
 *       - Each DumpExtent specifies a [start, end] logical sector range
 *       - Extents indicate which sectors this environment contributed
 *       - Empty extent arrays (extents == 0) mean the environment dumped entire medium
 *       - Extents are stored in the order provided in the input buffer
 *
 * @note Memory Ownership:
 *       - The function creates internal copies of all data
 *       - The caller retains ownership of the input data buffer
 *       - The caller may free the input buffer immediately after this function returns
 *       - Internal copies are freed during aaruf_close() or when replaced by another call
 *
 * @note Replacing Existing Data:
 *       - Calling this function multiple times replaces previous dump hardware data
 *       - All previous entries, strings, and extents are freed before storing new data
 *       - No memory leaks occur when updating dump hardware information
 *
 * @warning The dump hardware block is only written to the image file during aaruf_close().
 *          Changes made by this function are not immediately persisted to disk.
 *
 * @warning CRC64 validation protects against corrupted blocks, but construction errors in the
 *          input buffer (incorrect lengths, misaligned data) may cause parsing to fail or
 *          produce incorrect results even with a valid checksum.
 *
 * @warning The function assumes the input buffer is properly formatted and packed according
 *          to the DumpHardwareBlock specification. Malformed input may cause crashes or
 *          memory corruption.
 *
 * @see DumpHardwareHeader for the block header structure definition.
 * @see DumpHardwareEntry for the per-environment entry structure definition.
 * @see DumpExtent for the extent range structure definition.
 * @see aaruf_get_dumphw() for retrieving dump hardware from opened images.
 * @see write_dumphw_block() for the serialization process during image closing.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_set_dumphw(void *context, uint8_t *data, size_t length)
{
    TRACE("Entering aaruf_set_dumphw(%p, %p, %zu)", context, data, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_dumphw() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_dumphw() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_dumphw() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    if(data == NULL || length == 0)
    {
        FATAL("Invalid data or length");

        TRACE("Exiting aaruf_set_dumphw() = AARUF_ERROR_INCORRECT_DATA_SIZE");
        return AARUF_ERROR_INCORRECT_DATA_SIZE;
    }

    if(length < sizeof(DumpHardwareHeader))
    {
        FATAL("Dump hardware block shorter than header");

        TRACE("Exiting aaruf_set_dumphw() = AARUF_ERROR_INCORRECT_DATA_SIZE");
        return AARUF_ERROR_INCORRECT_DATA_SIZE;
    }

    DumpHardwareHeader header;
    memcpy(&header, data, sizeof(DumpHardwareHeader));
    if(header.identifier != DumpHardwareBlock)
    {
        FATAL("Invalid dump hardware block identifier");

        TRACE("Exiting aaruf_set_dumphw() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    if(length != sizeof(DumpHardwareHeader) + header.length)
    {
        FATAL("Invalid dump hardware block length");

        TRACE("Exiting aaruf_set_dumphw() = AARUF_ERROR_INCORRECT_DATA_SIZE");
        return AARUF_ERROR_INCORRECT_DATA_SIZE;
    }

    uint64_t crc64 = aaruf_crc64_data(data + sizeof(DumpHardwareHeader), header.length);
    if(header.crc64 != crc64)
    {
        FATAL("Invalid dump hardware block CRC64");

        TRACE("Exiting aaruf_set_dumphw() = AARUF_ERROR_INVALID_BLOCK_CRC");
        return AARUF_ERROR_INVALID_BLOCK_CRC;
    }

    DumpHardwareEntriesWithData *copy = NULL;
    if(header.entries > 0)
    {
        copy = calloc(header.entries, sizeof(DumpHardwareEntriesWithData));
        if(copy == NULL)
        {
            TRACE("Could not allocate memory for dump hardware block entries");
            TRACE("Exiting aaruf_set_dumphw() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
        }
    }

    TRACE("Processing %u dump hardware block entries", header.entries);

    size_t pos = sizeof(DumpHardwareHeader);

#define COPY_STRING_FIELD(field)                                                    \
    do                                                                              \
    {                                                                               \
        const size_t field##_length = copy[e].entry.field##Length;                  \
        if(field##_length > 0)                                                      \
        {                                                                           \
            if(field##_length > length - pos) goto invalid_data;                    \
            /* Allocate only field##_length bytes, since input is NUL-terminated */ \
            copy[e].field = (uint8_t *)calloc(1, field##_length);                   \
            if(copy[e].field == NULL) goto free_copy_and_error;                     \
            memcpy(copy[e].field, data + pos, field##_length);                      \
            /* Ensure NUL-termination in case input is malformed */                 \
            copy[e].field[field##_length - 1] = '\0';                               \
            pos += field##_length;                                                  \
        }                                                                           \
    } while(0)

    for(uint32_t e = 0; e < header.entries; e++)
    {
        if(length - pos < sizeof(DumpHardwareEntry)) goto invalid_data;

        memcpy(&copy[e].entry, data + pos, sizeof(DumpHardwareEntry));
        pos += sizeof(DumpHardwareEntry);

        COPY_STRING_FIELD(manufacturer);
        COPY_STRING_FIELD(model);
        COPY_STRING_FIELD(revision);
        COPY_STRING_FIELD(firmware);
        COPY_STRING_FIELD(serial);
        COPY_STRING_FIELD(softwareName);
        COPY_STRING_FIELD(softwareVersion);
        COPY_STRING_FIELD(softwareOperatingSystem);

        const uint32_t extent_count = copy[e].entry.extents;
        if(extent_count > 0)
        {
            const size_t extent_bytes = (size_t)extent_count * sizeof(DumpExtent);
            if(extent_bytes / sizeof(DumpExtent) != extent_count || extent_bytes > length - pos) goto invalid_data;

            copy[e].extents = (DumpExtent *)malloc(extent_bytes);
            if(copy[e].extents == NULL) goto free_copy_and_error;

            memcpy(copy[e].extents, data + pos, extent_bytes);
            pos += extent_bytes;

            qsort(copy[e].extents, extent_count, sizeof(DumpExtent), compare_extents);
            TRACE("Sorted %u extents for entry %u", extent_count, e);
        }
    }

#undef COPY_STRING_FIELD

    if(pos != length)
    {
        FATAL("Dump hardware block contains trailing data");
        goto invalid_data;
    }

    free_dump_hardware_entries(ctx->dump_hardware_entries_with_data, ctx->dump_hardware_header.entries);
    ctx->dump_hardware_entries_with_data = copy;
    ctx->dump_hardware_header            = header;
    ctx->dirty_dumphw_block              = true;  // Mark dump hardware block as dirty

    TRACE("Exiting aaruf_set_dumphw() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;

invalid_data:
    TRACE("Dump hardware block truncated or malformed");
    free_dump_hardware_entries(copy, header.entries);
    TRACE("Exiting aaruf_set_dumphw() = AARUF_ERROR_INCORRECT_DATA_SIZE");
    return AARUF_ERROR_INCORRECT_DATA_SIZE;

free_copy_and_error:
    free_dump_hardware_entries(copy, header.entries);
    TRACE("Exiting aaruf_set_dumphw() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
    return AARUF_ERROR_NOT_ENOUGH_MEMORY;
}
