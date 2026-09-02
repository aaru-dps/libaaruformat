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

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

#include <aaruformat.h>

#include "erasure_internal.h"
#include "internal.h"
#include "log.h"
#include "utarray.h"

static inline void aaruf_set_open_error(const int error_code)
{
    errno = error_code;
#if defined(_WIN32) || defined(_WIN64)
    SetLastError((DWORD)error_code);
#endif
}

static void cleanup_open_failure(aaruformat_context *ctx)
{
    if(ctx == NULL) return;

    if(ctx->imageStream != NULL)
    {
        fclose(ctx->imageStream);
        ctx->imageStream = NULL;
    }

    free(ctx->readableSectorTags);
    ctx->readableSectorTags = NULL;

    free(ctx);
}

/**
 * @brief Opens an existing AaruFormat image file.
 *
 * Opens the specified image file and returns a pointer to the initialized aaruformat context.
 * This function performs comprehensive validation of the image file format, reads and processes
 * all index entries, initializes data structures for reading (and optionally writing in resume mode),
 * and sets up caches for optimal performance. It supports multiple AaruFormat versions and handles
 * various block types including data blocks, deduplication tables, metadata, and checksums.
 *
 * **Operational Modes:**
 *
 * **Read-Only Mode (resume_mode = false):**
 * - Validates and reads existing image in read-only mode
 * - Loads all index entries, DDT, and metadata structures
 * - Initializes block and header caches for efficient reading
 * - Suitable for accessing, extracting, or verifying completed AaruFormat images
 * - File is opened in binary read mode; no modifications possible
 * - Requires valid user data DDT to be present in the image
 *
 * **Resume/Write Mode (resume_mode = true):**
 * - Validates and opens an existing image for continued writing (resume operations)
 * - Loads all existing index entries, DDT, and metadata
 * - Prepares context for additional write operations to incomplete images
 * - Suitable for resuming interrupted image creation or appending to images
 * - File is opened in binary read/write mode for additional data/blocks
 * - Requires the image to be in a valid resumable state (proper headers, valid index, DDT present)
 * - Requires image to be in AaruFormat version 2.0 or later (version 1.x images cannot be resumed)
 * - Options string is parsed to configure writing parameters
 * - Checksum contexts are reinitialized based on options if checksums are present
 *
 * **Index and Block Processing:**
 * The function processes all indexed blocks from the image:
 * - Data blocks: User and negative/overflow sectors with deduplication references
 * - DDT blocks: Deduplication table entries mapping sectors to physical block locations
 * - Geometry blocks: Cylinder-head-sector information for certain media types
 * - Metadata blocks: CICM XML, Aaru JSON, and other format-specific metadata
 * - Track blocks: CD/DVD track information and sector maps
 * - Dump hardware blocks: Recording device specifications and configuration
 * - Checksum blocks: Media tags and image-level checksums (MD5, SHA-1, SHA-256, BLAKE3, SpamSum)
 *
 * Non-critical block processing errors are logged but don't prevent opening (logging only).
 * Critical errors (especially DDT processing failures) cause the open operation to fail.
 * Unknown block types are logged but silently ignored for forward compatibility.
 *
 * @param filepath Path to the image file to open. Must be a valid readable path.
 *                 When opening in resume mode (resume_mode=true), the file must also be writable.
 *                 The file must contain a valid AaruFormat header and index.
 *
 * @param resume_mode Boolean flag controlling the operational mode:
 *                    - false: Open in read-only mode for accessing completed images.
 *                            File is opened read-only; no modifications can be made.
 *                            Suitable for extraction, analysis, and verification.
 *                            Supports both AaruFormat version 1.x and 2.x images.
 *                    - true: Open in resume/write mode for continuing interrupted image creation.
 *                           File is opened read/write for additional operations.
 *                           Requires existing valid image structure.
 *                           IMPORTANT: Only AaruFormat version 2.x or later images can be opened in resume mode.
 *                                      Version 1.x images cannot be resumed and will fail with
 * AARUF_ERROR_INCOMPATIBLE_VERSION. Write operations are handled as if resuming an incomplete creation. Options string
 * is parsed for write configuration.
 *
 * @param options String with opening/resume options in key=value format, semicolon-separated.
 *                Used primarily in resume mode to configure checksum and compression parameters.
 *                Supported options (resume mode only):
 *                - "deduplicate=true|false": Enable/disable sector deduplication (if supported in image)
 *                - "md5=true|false": Recalculate MD5 checksum during resume operations
 *                - "sha1=true|false": Recalculate SHA-1 checksum during resume operations
 *                - "sha256=true|false": Recalculate SHA-256 checksum during resume operations
 *                - "spamsum=true|false": Recalculate SpamSum fuzzy hash during resume operations
 *                - "blake3=true|false": Recalculate BLAKE3 checksum during resume operations
 *                - "compress=true|false": Use compression for new data blocks in resume operations
 *                Example: "deduplicate=true;md5=true;sha1=true"
 *                In read-only mode (resume_mode=false), this parameter is typically NULL or ignored.
 *
 * @return Returns one of the following:
 * @retval aaruformatContext* Successfully opened and initialized context. The returned pointer contains:
 *         - Validated AaruFormat headers and metadata
 *         - Processed index entries with all discoverable blocks
 *         - Loaded deduplication tables (DDT) for efficient sector access
 *         - Initialized block and header caches for performance
 *         - Open file stream (read-only or read/write depending on mode)
 *         - Populated image information and geometry data
 *         - ECC context initialized for error correction support
 *         - In resume mode: Write context and checksum contexts initialized from options
 *
 * @retval NULL Opening failed. The specific error can be determined by checking errno, which will be set to:
 *         - AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) when memory allocation fails for:
 *           * Context allocation
 *           * Readable sector tags bitmap allocation
 *           * Application version string allocation
 *           * Image version string allocation
 *           * Index entries array allocation
 *           * DDT processing structures
 *         - AARUF_ERROR_FILE_TOO_SMALL (-2) when file reading fails:
 *           * Cannot read the AaruFormat header (file too small or corrupted)
 *           * Cannot read the extended header for version 2+ formats
 *           * Cannot seek to or read the index block
 *         - AARUF_ERROR_NOT_AARUFORMAT (-1) when format validation fails:
 *           * File identifier doesn't match DIC_MAGIC or AARU_MAGIC
 *           * File is not a valid AaruFormat image
 *         - AARUF_ERROR_INCOMPATIBLE_VERSION (-3) when:
 *           * Image major version exceeds the maximum supported version
 *           * Future format versions that cannot be read by this library
 *           * Version 1.x images are opened in resume mode (only version 2.x+ can be resumed)
 *           * Other version incompatibility issues with resume mode requirements
 *         - AARUF_ERROR_CANNOT_READ_INDEX (-4) when index processing fails:
 *           * Cannot seek to the index offset specified in the header
 *           * Cannot read the index signature
 *           * Index signature is not a recognized index block type
 *           * Index processing functions return NULL (corrupted index)
 *         - Other error codes may be propagated from block processing functions:
 *           * Data block processing errors
 *           * DDT processing errors (critical failure)
 *           * Metadata processing errors (non-critical, logging only)
 *           * File I/O errors in resume mode
 *
 * @note Format Support:
 *       - Supports AaruFormat versions 1.x and 2.x
 *       - Automatically detects and handles different index formats (v1, v2, v3)
 *       - Backwards compatible with older DIC format identifiers
 *       - Handles both small and large deduplication tables
 *       - Supports optional user data DDT, necessary for sector access
 *
 * @note Block Processing:
 *       - Processes all indexed blocks including data, DDT, geometry, metadata, tracks, CICM, dump hardware, and
 * checksums
 *       - Non-critical block processing errors are logged but don't prevent opening
 *       - Critical errors (DDT processing failures) cause opening to fail
 *       - Unknown block types are logged but ignored for future compatibility
 *       - Block processing is the same in both read-only and resume modes
 *
 * @note Memory Management:
 *       - Allocates memory for various context structures and caches
 *       - On failure, all previously allocated memory is properly cleaned up
 *       - The returned context must be freed using aaruf_close()
 *       - In resume mode, additional write buffers may be allocated based on options
 *
 * @note Performance Optimization:
 *       - Initializes block and header caches based on sector size and available memory
 *       - Cache sizes are calculated to optimize memory usage and access patterns
 *       - ECC context is pre-initialized for Compact Disc support
 *       - Cache strategies may differ between read-only and resume modes
 *
 * @note Resume Mode Considerations:
 *       - Image must be in a valid state for resume operations
 *       - Image MUST be in AaruFormat version 2.0 or later (version 1.x images cannot be resumed)
 *       - Opening a version 1.x image with resume_mode=true will fail immediately with AARUF_ERROR_INCOMPATIBLE_VERSION
 *       - Partial or corrupted images may fail to open in resume mode
 *       - Options are parsed to reconfigure checksums and compression if needed
 *       - Write position is calculated based on existing data and index entries
 *       - Deduplication hash maps are reconstructed from existing DDT entries
 *       - Some checksums (MD5, SHA-1, SHA-256) may need to be recalculated from scratch if resuming
 *
 * @warning The function requires a valid user data deduplication table to be present.
 *          Images without a DDT will fail to open even if otherwise valid.
 *
 * @warning File access permissions must be appropriate for the selected mode:
 *          - Read-only mode: File must be readable
 *          - Resume mode: File must be readable AND writable
 *          The file must not be locked by other processes in resume mode.
 *
 * @warning Resume mode has additional validation requirements:
 *          - Image must not be finalized (index must not be marked as complete)
 *          - Image MUST be AaruFormat version 2.x or later (version 1.x images are not supported in resume mode)
 *          - Image must have a valid, reconstructible DDT
 *          - File I/O errors in resume mode may leave the image in an inconsistent state
 *
 * @warning Some memory allocations (version strings, checksum contexts) are optional.
 *          Failure in optional allocations doesn't prevent opening but may affect functionality
 *          that depends on checksums or version information.
 *
 * @see aaruf_close() for proper context cleanup and image finalization
 * @see aaruf_read_sector() for reading sectors from opened images
 * @see aaruf_write_sector() for writing sectors in resume mode
 * @see aaruf_identify() for identifying image type before opening
 */
AARU_EXPORT void *AARU_CALL aaruf_open(const char *filepath, const bool resume_mode,
                                       const char *options)  // NOLINT(readability-function-size)
{
    aaruformat_context *ctx           = NULL;
    int                 error_no      = 0;
    size_t              read_bytes    = 0;
    aaru_off_t          pos           = 0;
    int                 i             = 0;
    uint32_t            signature     = 0;
    UT_array           *index_entries = NULL;

#ifdef USE_SLOG
#include "slog.h"

    slog_init("aaruformat.log", SLOG_FLAGS_ALL, 0);
#endif

    TRACE("Logging initialized");

    TRACE("Entering aaruf_open(%s)", filepath);
    aaruf_set_open_error(0);

    TRACE("Allocating memory for context");
    ctx = (aaruformat_context *)malloc(sizeof(aaruformat_context));

    if(ctx == NULL)
    {
        FATAL("Not enough memory to create context");
        aaruf_set_open_error(AARUF_ERROR_NOT_ENOUGH_MEMORY);

        TRACE("Exiting aaruf_open() = NULL");
        return NULL;
    }

    memset(ctx, 0, sizeof(aaruformat_context));

    TRACE("Opening file %s", filepath);
    if(resume_mode)
        ctx->imageStream = fopen(filepath, "r+b");
    else
        ctx->imageStream = fopen(filepath, "rb");

    if(ctx->imageStream == NULL)
    {
        FATAL("Error %d opening file %s for reading", errno, filepath);
        error_no = errno;
        cleanup_open_failure(ctx);
        aaruf_set_open_error(error_no);

        TRACE("Exiting aaruf_open() = NULL");
        return NULL;
    }

    TRACE("Reading header at position 0");
    aaruf_fseek(ctx->imageStream, 0, SEEK_SET);
    read_bytes = fread(&ctx->header, 1, sizeof(AaruHeader), ctx->imageStream);

    if(read_bytes != sizeof(AaruHeader))
    {
        FATAL("Could not read header");
        cleanup_open_failure(ctx);
        aaruf_set_open_error(AARUF_ERROR_FILE_TOO_SMALL);

        TRACE("Exiting aaruf_open() = NULL");
        return NULL;
    }

    if(ctx->header.identifier != DIC_MAGIC && ctx->header.identifier != AARU_MAGIC)
    {
        /* Primary header corrupt — try recovery footer backup header */
        TRACE("Primary header invalid, trying recovery footer backup header");

        aaruf_fseek(ctx->imageStream, 0, SEEK_END);
        int64_t file_size = aaruf_ftell(ctx->imageStream);

        if(file_size >= (int64_t)sizeof(AaruRecoveryFooter))
        {
            aaruf_fseek(ctx->imageStream,
                        (aaru_off_t)(file_size - (int64_t)sizeof(AaruRecoveryFooter)), SEEK_SET);
            AaruRecoveryFooter recovery_footer;
            if(fread(&recovery_footer, sizeof(AaruRecoveryFooter), 1, ctx->imageStream) == 1 &&
               recovery_footer.footerMagic == AARU_RECOVERY_FOOTER_MAGIC)
            {
                /* Verify the backup header's CRC matches what was recorded */
                uint64_t backup_crc = aaruf_crc64_data(
                    (const uint8_t *)&recovery_footer.backupHeader, sizeof(AaruHeaderV2));
                if(backup_crc == recovery_footer.headerCrc64 &&
                   (recovery_footer.backupHeader.identifier == DIC_MAGIC ||
                    recovery_footer.backupHeader.identifier == AARU_MAGIC))
                {
                    TRACE("Recovery footer backup header valid, using it");
                    memcpy(&ctx->header, &recovery_footer.backupHeader, sizeof(AaruHeaderV2));
                    /* Seek past the header for the next read */
                    aaruf_fseek(ctx->imageStream, (aaru_off_t)sizeof(AaruHeaderV2), SEEK_SET);
                }
                else
                {
                    FATAL("Recovery footer backup header also invalid");
                    cleanup_open_failure(ctx);
                    aaruf_set_open_error(AARUF_ERROR_NOT_AARUFORMAT);
                    TRACE("Exiting aaruf_open() = NULL");
                    return NULL;
                }
            }
            else
            {
                FATAL("No recovery footer found, cannot recover from corrupt header");
                cleanup_open_failure(ctx);
                aaruf_set_open_error(AARUF_ERROR_NOT_AARUFORMAT);
                TRACE("Exiting aaruf_open() = NULL");
                return NULL;
            }
        }
        else
        {
            FATAL("Incorrect identifier for AaruFormat file: %8.8s", (char *)&ctx->header.identifier);
            cleanup_open_failure(ctx);
            aaruf_set_open_error(AARUF_ERROR_NOT_AARUFORMAT);
            TRACE("Exiting aaruf_open() = NULL");
            return NULL;
        }
    }

    if(ctx->header.imageMajorVersion < AARUF_VERSION_V2 && resume_mode)
    {
        TRACE("Cannot write to old images");
        cleanup_open_failure(ctx);
        aaruf_set_open_error(AARUF_ERROR_INCOMPATIBLE_VERSION);
        TRACE("Exiting aaruf_open() = NULL");
        return NULL;
    }

    // Read new header version
    if(ctx->header.imageMajorVersion >= AARUF_VERSION_V2)
    {
        TRACE("Reading new header version at position 0");
        aaruf_fseek(ctx->imageStream, 0, SEEK_SET);
        read_bytes = fread(&ctx->header, 1, sizeof(AaruHeaderV2), ctx->imageStream);

        if(read_bytes != sizeof(AaruHeaderV2))
        {
            cleanup_open_failure(ctx);
            aaruf_set_open_error(AARUF_ERROR_FILE_TOO_SMALL);

            return NULL;
        }
    }

    if(ctx->header.imageMajorVersion > AARUF_VERSION)
    {
        FATAL("Incompatible AaruFormat version %d.%d found, maximum supported is %d.%d", ctx->header.imageMajorVersion,
              ctx->header.imageMinorVersion, AARUF_VERSION_V2, 0);
        cleanup_open_failure(ctx);
        aaruf_set_open_error(AARUF_ERROR_INCOMPATIBLE_VERSION);

        TRACE("Exiting aaruf_open() = NULL");
        return NULL;
    }

    // Check feature compatibility for V2+ images
    if(ctx->header.imageMajorVersion >= AARUF_VERSION_V2)
    {
        uint64_t unknown_incompat = ctx->header.featureIncompatible & ~AARUF_KNOWN_INCOMPAT_FEATURES;

        if(unknown_incompat != 0)
        {
            FATAL("Image requires unsupported incompatible features: 0x%016" PRIX64, unknown_incompat);
            cleanup_open_failure(ctx);
            aaruf_set_open_error(AARUF_ERROR_INCOMPATIBLE_FEATURES);

            TRACE("Exiting aaruf_open() = NULL");
            return NULL;
        }

        uint64_t unknown_rocompat = ctx->header.featureCompatibleRo & ~AARUF_KNOWN_ROCOMPAT_FEATURES;

        if(unknown_rocompat != 0)
        {
            if(resume_mode)
            {
                FATAL("Image has unsupported read-only compatible features: 0x%016" PRIX64 ", cannot open for writing",
                      unknown_rocompat);
                cleanup_open_failure(ctx);
                aaruf_set_open_error(AARUF_ERROR_INCOMPATIBLE_FEATURES);

                TRACE("Exiting aaruf_open() = NULL");
                return NULL;
            }

            TRACE("Image has unsupported read-only compatible features: 0x%016" PRIX64 ", forcing read-only",
                  unknown_rocompat);
        }
    }

    TRACE("Opening image version %d.%d", ctx->header.imageMajorVersion, ctx->header.imageMinorVersion);

    TRACE("Allocating memory for readable sector tags bitmap");
    ctx->readableSectorTags = (bool *)malloc(sizeof(bool) * (MaxSectorTag + 1));

    if(ctx->readableSectorTags == NULL)
    {
        FATAL("Could not allocate memory for readable sector tags bitmap");
        cleanup_open_failure(ctx);
        aaruf_set_open_error(AARUF_ERROR_NOT_ENOUGH_MEMORY);

        TRACE("Exiting aaruf_open() = NULL");
        return NULL;
    }

    memset(ctx->readableSectorTags, 0, sizeof(bool) * (MaxSectorTag + 1));

    TRACE("Setting up image info");

    // Handle application name based on image version
    memset(ctx->image_info.Application, 0, 64);

    if(ctx->header.imageMajorVersion >= AARUF_VERSION_V2)
    {
        // Version 2+: application name is UTF-8, direct copy
        TRACE("Converting application name (v2+): UTF-8 direct copy");
        size_t copy_len = AARU_HEADER_APP_NAME_LEN < 63 ? AARU_HEADER_APP_NAME_LEN : 63;
        memcpy(ctx->image_info.Application, ctx->header.application, copy_len);
        ctx->image_info.Application[63] = '\0';
    }
    else
    {
        // Version 1: application name is UTF-16LE, convert by taking every other byte
        TRACE("Converting application name (v1): UTF-16LE to ASCII");
        int dest_idx = 0;
        for(int j = 0; j < AARU_HEADER_APP_NAME_LEN && dest_idx < 63; j += 2)
            // Take the low byte, skip the high byte (assuming it's 0x00 for ASCII)
            if(ctx->header.application[j] != 0)
                ctx->image_info.Application[dest_idx++] = ctx->header.application[j];
            else
                // Stop at null terminator
                break;
        ctx->image_info.Application[dest_idx] = '\0';
    }

    // Set application version string directly in the fixed-size array
    memset(ctx->image_info.ApplicationVersion, 0, 32);
    sprintf(ctx->image_info.ApplicationVersion, "%d.%d", ctx->header.applicationMajorVersion,
            ctx->header.applicationMinorVersion);

    // Set image version string directly in the fixed-size array
    memset(ctx->image_info.Version, 0, 32);
    sprintf(ctx->image_info.Version, "%d.%d", ctx->header.imageMajorVersion, ctx->header.imageMinorVersion);

    ctx->image_info.MediaType = ctx->header.mediaType;

    // Read the index header
    TRACE("Reading index header at position %" PRIu64, ctx->header.indexOffset);
    if(aaruf_fseek(ctx->imageStream, (aaru_off_t)ctx->header.indexOffset, SEEK_SET) != 0)
    {
        cleanup_open_failure(ctx);
        aaruf_set_open_error(AARUF_ERROR_CANNOT_READ_INDEX);

        return NULL;
    }

    pos = aaruf_ftell(ctx->imageStream);
    if(pos != ctx->header.indexOffset)
    {
        cleanup_open_failure(ctx);
        aaruf_set_open_error(AARUF_ERROR_CANNOT_READ_INDEX);

        return NULL;
    }

    read_bytes = fread(&signature, 1, sizeof(uint32_t), ctx->imageStream);

    if(read_bytes != sizeof(uint32_t) ||
       (signature != IndexBlock && signature != IndexBlock2 && signature != IndexBlock3))
    {
        FATAL("Could not read index header or incorrect identifier %4.4s", (char *)&signature);
        cleanup_open_failure(ctx);
        aaruf_set_open_error(AARUF_ERROR_CANNOT_READ_INDEX);

        TRACE("Exiting aaruf_open() = NULL");
        return NULL;
    }

    if(signature == IndexBlock)
        index_entries = process_index_v1(ctx);
    else if(signature == IndexBlock2)
        index_entries = process_index_v2(ctx);
    else if(signature == IndexBlock3)
        index_entries = process_index_v3(ctx);

    if(index_entries == NULL)
    {
        FATAL("Could not process index.");
        utarray_free(index_entries);
        cleanup_open_failure(ctx);
        aaruf_set_open_error(AARUF_ERROR_CANNOT_READ_INDEX);

        TRACE("Exiting aaruf_open() = NULL");
        return NULL;
    }

    TRACE("Index at %" PRIu64 " contains %d entries", ctx->header.indexOffset, utarray_len(index_entries));

    for(i = 0; i < utarray_len(index_entries); i++)
    {
        IndexEntry *entry = utarray_eltptr(index_entries, i);
        TRACE("Block type %4.4s with data type %d is indexed to be at %" PRIu64 "", (char *)&entry->blockType,
              entry->dataType, entry->offset);
    }

    bool found_user_data_ddt  = false;
    bool has_flux_block       = false;
    ctx->image_info.ImageSize = 0;
    for(i = 0; i < utarray_len(index_entries); i++)
    {
        IndexEntry *entry = utarray_eltptr(index_entries, i);

        if(aaruf_fseek(ctx->imageStream, (aaru_off_t)entry->offset, SEEK_SET) != 0 ||
           aaruf_ftell(ctx->imageStream) != (aaru_off_t)entry->offset)
        {
            TRACE("Could not seek to %" PRIu64 " as indicated by index entry %d, continuing...", entry->offset, i);

            continue;
        }

        TRACE("Processing block type %4.4s with data type %d at position %" PRIu64 "", (char *)&entry->blockType,
              entry->dataType, entry->offset);
        switch(entry->blockType)
        {
            case DataBlock:
                if(entry->dataType == kDataTypeUserData && ctx->header.biggestSectorSize > 0) break;

                error_no = process_data_block(ctx, entry);

                if(error_no != AARUF_STATUS_OK)
                {
                    utarray_free(index_entries);
                    cleanup_open_failure(ctx);
                    aaruf_set_open_error(error_no);

                    return NULL;
                }

                break;

            case DeDuplicationTable:
                error_no = process_ddt_v1(ctx, entry, &found_user_data_ddt);

                if(error_no != AARUF_STATUS_OK)
                {
                    utarray_free(index_entries);
                    cleanup_open_failure(ctx);
                    aaruf_set_open_error(error_no);

                    return NULL;
                }

                break;
            case DeDuplicationTable2:
                error_no = process_ddt_v2(ctx, entry, &found_user_data_ddt);

                if(error_no != AARUF_STATUS_OK)
                {
                    utarray_free(index_entries);
                    cleanup_open_failure(ctx);
                    aaruf_set_open_error(error_no);

                    return NULL;
                }

                switch(entry->dataType)
                {
                    case kDataTypeCdSectorPrefix:
                    case kDataTypeCdSectorPrefixCorrected:
                        ctx->readableSectorTags[kSectorTagCdSync]   = true;
                        ctx->readableSectorTags[kSectorTagCdHeader] = true;

                        break;
                    case kDataTypeCdSectorSuffix:
                    case kDataTypeCdSectorSuffixCorrected:
                        ctx->readableSectorTags[kSectorTagCdSubHeader] = true;
                        ctx->readableSectorTags[kSectorTagCdEcc]       = true;
                        ctx->readableSectorTags[kSectorTagCdEccP]      = true;
                        ctx->readableSectorTags[kSectorTagCdEccQ]      = true;
                        ctx->readableSectorTags[kSectorTagCdEdc]       = true;
                        break;
                    default:
                        break;
                }

                break;
            case GeometryBlock:
                process_geometry_block(ctx, entry);

                break;
            case MetadataBlock:
                process_metadata_block(ctx, entry);

                break;
            case TracksBlock:
                process_tracks_block(ctx, entry);

                break;
            case CicmBlock:
                process_cicm_block(ctx, entry);

                break;
            case AaruMetadataJsonBlock:
                process_aaru_metadata_json_block(ctx, entry);

                break;
            // Dump hardware block
            case DumpHardwareBlock:
                process_dumphw_block(ctx, entry);

                break;
            case ChecksumBlock:
                process_checksum_block(ctx, entry);

                break;
            case TapeFileBlock:
                process_tape_files_block(ctx, entry);

                break;
            case TapePartitionBlock:
                process_tape_partitions_block(ctx, entry);

                break;
            case FluxDataBlock:
                // Store the FluxDataBlock offset for lazy loading
                // Don't read flux entries during open - load them on-demand when actually needed
                // This avoids unnecessary I/O if flux data is never accessed
                has_flux_block = true;
                break;
            default:
                TRACE("Unhandled block type %4.4s with data type %d is indexed to be at %" PRIu64 "",
                      (char *)&entry->blockType, entry->dataType, entry->offset);
                break;
        }
    }

    ctx->index_entries = index_entries;

    if(!found_user_data_ddt)
    {
        if(!has_flux_block)
        {
            FATAL("Could not find user data deduplication table or flux data, aborting...");
            aaruf_close(ctx);
            aaruf_set_open_error(AARUF_ERROR_CANNOT_READ_INDEX);

            TRACE("Exiting aaruf_open() = NULL");
            return NULL;
        }

        TRACE("No user data DDT found but flux data present, opening as flux-only image");
        ctx->no_user_data_ddt = true;
        /* image_info.Sectors stays 0; biggestSectorSize stays 0; sector APIs will reject. */
    }

    if(ctx->header.biggestSectorSize != 0)
    {
        TRACE("Setting sector size to %u bytes", ctx->header.biggestSectorSize);
        ctx->image_info.SectorSize = ctx->header.biggestSectorSize;
    }

    ctx->image_info.CreationTime         = ctx->header.creationTime;
    ctx->image_info.LastModificationTime = ctx->header.lastWrittenTime;
    ctx->image_info.MetadataMediaType    = aaruf_get_xml_mediatype(ctx->header.mediaType);

    if(ctx->geometry_block.identifier != GeometryBlock && ctx->image_info.MetadataMediaType == BlockMedia)
    {
        ctx->cylinders         = (uint32_t)(ctx->image_info.Sectors / 16 / 63);
        ctx->heads             = 16;
        ctx->sectors_per_track = 63;
    }

    // Initialize caches
    TRACE("Initializing caches");
    ctx->block_header_cache.cache = NULL;
    ctx->block_cache.cache        = NULL;

    // Set free functions for cached values (both cache malloc'd data)
    ctx->block_header_cache.free_func = free;
    ctx->block_cache.free_func        = free;

    // Both caches are bounded by bytes held, so no block geometry is needed here. Deriving an
    // entry count from a block size got this wrong: the divisor used ctx->shift, which is only
    // ever set for DDT v1 images, so on every DDT v2 image the limit came out ~262144 entries of
    // up to 8 MiB each and the cache grew until the whole decompressed image was resident.
    ctx->block_header_cache.cur_bytes = 0;
    ctx->block_cache.cur_bytes        = 0;
    ctx->block_header_cache.max_bytes = MAX_HEADER_CACHE_SIZE;
    ctx->block_cache.max_bytes        = MAX_CACHE_SIZE;

    // TODO: Cache tracks and sessions?

    // Initialize ECC for Compact Disc
    TRACE("Initializing ECC for Compact Disc");
    ctx->ecc_cd_context = (CdEccContext *)aaruf_ecc_cd_init();

    ctx->magic                 = AARU_MAGIC;
    ctx->library_major_version = LIBAARUFORMAT_MAJOR_VERSION;
    ctx->library_minor_version = LIBAARUFORMAT_MINOR_VERSION;

    /* Try to load erasure coding recovery metadata from EOF footer */
    ec_load_ecmb(ctx);

    if(!resume_mode)
    {
        TRACE("Exiting aaruf_open() = %p", ctx);
        return ctx;
    }

    // Parse the options
    TRACE("Parsing options");
    bool               table_shift_found = false;
    const aaru_options parsed_options    = parse_options(options, &table_shift_found);

    ctx->header.lastWrittenTime          = get_filetime_uint64();
    ctx->image_info.LastModificationTime = ctx->header.lastWrittenTime;

    // Calculate aligned next block position. For flux-only images there is no user-data DDT
    // header, so use the alignment shift recorded in the container header instead.
    const uint8_t resume_alignment_shift =
        ctx->no_user_data_ddt ? ctx->header.blockAlignmentShift : ctx->user_data_ddt_header.blockAlignmentShift;
    aaruf_fseek(ctx->imageStream, 0, SEEK_END);
    const uint64_t alignment_mask = (1ULL << resume_alignment_shift) - 1;
    ctx->next_block_position      = (uint64_t)aaruf_ftell(ctx->imageStream);  // Start just after the header
    ctx->next_block_position      = ctx->next_block_position + alignment_mask & ~alignment_mask;

    TRACE("Data blocks will start at position %" PRIu64, ctx->next_block_position);

    // Position file pointer at the data start position
    if(aaruf_fseek(ctx->imageStream, (aaru_off_t)ctx->next_block_position, SEEK_SET) != 0)
    {
        FATAL("Could not seek to data start position");
        TRACE("Exiting aaruf_open() = NULL");
        cleanup_open_failure(ctx);
        aaruf_set_open_error(AARUF_ERROR_CANNOT_CREATE_FILE);
        return NULL;
    }

    // Apply applicable options (we cannot change table or alignment options resuming)
    ctx->compression_enabled = parsed_options.compress;
    ctx->lzma_dict_size      = parsed_options.dictionary;
    ctx->deduplicate         = parsed_options.deduplicate;
    /* Flux-only images have no DDT and therefore no sector deduplication map. */
    if(ctx->deduplicate && !ctx->no_user_data_ddt)
        ctx->sector_hash_map = create_map(ctx->user_data_ddt_header.blocks * 25 / 100);  // 25% of total sectors

    // Loaded CD prefix/suffix arenas are compact (one slot per custom entry). New customs must append after the
    // existing slots, otherwise they would overwrite them and the DDT indexes would dangle past the rewritten block.
    ctx->sector_prefix_offset = ctx->sector_prefix_length;
    ctx->sector_suffix_offset = ctx->sector_suffix_length;

    // Cannot checksum a resumed file
    ctx->rewinded = true;

    // Is writing
    ctx->is_writing     = true;
    ctx->finalize_write = aaruf_finalize_write;

    TRACE("Exiting aaruf_open() = %p", ctx);
    // Return context
    return ctx;
}
