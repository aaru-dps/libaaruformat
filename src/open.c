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

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <aaruformat.h>

#include "internal.h"
#include "log.h"
#include "utarray.h"

static void cleanup_open_failure(aaruformatContext *ctx)
{
    if(ctx == NULL) return;

    if(ctx->imageStream != NULL)
    {
        fclose(ctx->imageStream);
        ctx->imageStream = NULL;
    }

    free(ctx->readableSectorTags);
    ctx->readableSectorTags = NULL;

    free(ctx->imageInfo.ApplicationVersion);
    ctx->imageInfo.ApplicationVersion = NULL;

    free(ctx->imageInfo.Version);
    ctx->imageInfo.Version = NULL;

    free(ctx);
}

/**
 * @brief Opens an existing AaruFormat image file.
 *
 * Opens the specified image file and returns a pointer to the initialized aaruformat context.
 * This function performs comprehensive validation of the image file format, reads and processes
 * all index entries, initializes data structures for reading operations, and sets up caches
 * for optimal performance. It supports multiple AaruFormat versions and handles various block
 * types including data blocks, deduplication tables, metadata, and checksums.
 *
 * @param filepath Path to the image file to open.
 *
 * @return Returns one of the following:
 * @retval aaruformatContext* Successfully opened and initialized context. The returned pointer contains:
 *         - Validated AaruFormat headers and metadata
 *         - Processed index entries with all discoverable blocks
 *         - Loaded deduplication tables (DDT) for efficient sector access
 *         - Initialized block and header caches for performance
 *         - Open file stream ready for reading operations
 *         - Populated image information and geometry data
 *         - ECC context initialized for error correction support
 *
 * @retval NULL Opening failed. The specific error can be determined by checking errno, which will be set to:
 *         - AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) when memory allocation fails for:
 *           * Context allocation
 *           * Readable sector tags bitmap allocation
 *           * Application version string allocation
 *           * Image version string allocation
 *         - AARUF_ERROR_FILE_TOO_SMALL (-2) when file reading fails:
 *           * Cannot read the AaruFormat header (file too small or corrupted)
 *           * Cannot read the extended header for version 2+ formats
 *         - AARUF_ERROR_NOT_AARUFORMAT (-1) when format validation fails:
 *           * File identifier doesn't match DIC_MAGIC or AARU_MAGIC
 *           * File is not a valid AaruFormat image
 *         - AARUF_ERROR_INCOMPATIBLE_VERSION (-3) when:
 *           * Image major version exceeds the maximum supported version
 *           * Future format versions that cannot be read by this library
 *         - AARUF_ERROR_CANNOT_READ_INDEX (-4) when index processing fails:
 *           * Cannot seek to the index offset specified in the header
 *           * Cannot read the index signature
 *           * Index signature is not a recognized index block type
 *           * Index processing functions return NULL (corrupted index)
 *         - Other error codes may be propagated from block processing functions:
 *           * Data block processing errors
 *           * DDT processing errors
 *           * Metadata processing errors
 *
 * @note Format Support:
 *       - Supports AaruFormat versions 1.x and 2.x
 *       - Automatically detects and handles different index formats (v1, v2, v3)
 *       - Backwards compatible with older DIC format identifiers
 *       - Handles both small and large deduplication tables
 *
 * @note Block Processing:
 *       - Processes all indexed blocks including data, DDT, geometry, metadata, tracks, CICM, dump hardware, and
 * checksums
 *       - Non-critical block processing errors are logged but don't prevent opening
 *       - Critical errors (DDT processing failures) cause opening to fail
 *       - Unknown block types are logged but ignored
 *
 * @note Memory Management:
 *       - Allocates memory for various context structures and caches
 *       - On failure, all previously allocated memory is properly cleaned up
 *       - The returned context must be freed using aaruf_close()
 *
 * @note Performance Optimization:
 *       - Initializes block and header caches based on sector size and available memory
 *       - Cache sizes are calculated to optimize memory usage and access patterns
 *       - ECC context is pre-initialized for Compact Disc support
 *
 * @warning The function requires a valid user data deduplication table to be present.
 *          Images without a DDT will fail to open even if otherwise valid.
 *
 * @warning File access is performed in binary read mode. The file must be accessible
 *          and not locked by other processes.
 *
 * @warning Some memory allocations (version strings) are optional and failure doesn't
 *          prevent opening, but may affect functionality that depends on version information.
 */
void *aaruf_open(const char *filepath)  // NOLINT(readability-function-size)
{
    aaruformatContext *ctx           = NULL;
    int                error_no      = 0;
    size_t             read_bytes    = 0;
    long               pos           = 0;
    int                i             = 0;
    uint32_t           signature     = 0;
    UT_array          *index_entries = NULL;

#ifdef USE_SLOG
#include "slog.h"

    slog_init("aaruformat.log", SLOG_FLAGS_ALL, 0);
#endif

    TRACE("Logging initialized");

    TRACE("Entering aaruf_open(%s)", filepath);

    TRACE("Allocating memory for context");
    ctx = (aaruformatContext *)malloc(sizeof(aaruformatContext));

    if(ctx == NULL)
    {
        FATAL("Not enough memory to create context");
        errno = AARUF_ERROR_NOT_ENOUGH_MEMORY;

        TRACE("Exiting aaruf_open() = NULL");
        return NULL;
    }

    memset(ctx, 0, sizeof(aaruformatContext));

    TRACE("Opening file %s", filepath);
    ctx->imageStream = fopen(filepath, "rb");

    if(ctx->imageStream == NULL)
    {
        FATAL("Error %d opening file %s for reading", errno, filepath);
        error_no = errno;
        cleanup_open_failure(ctx);
        errno = error_no;

        TRACE("Exiting aaruf_open() = NULL");
        return NULL;
    }

    TRACE("Reading header at position 0");
    fseek(ctx->imageStream, 0, SEEK_SET);
    read_bytes = fread(&ctx->header, 1, sizeof(AaruHeader), ctx->imageStream);

    if(read_bytes != sizeof(AaruHeader))
    {
        FATAL("Could not read header");
        cleanup_open_failure(ctx);
        errno = AARUF_ERROR_FILE_TOO_SMALL;

        TRACE("Exiting aaruf_open() = NULL");
        return NULL;
    }

    if(ctx->header.identifier != DIC_MAGIC && ctx->header.identifier != AARU_MAGIC)
    {
        FATAL("Incorrect identifier for AaruFormat file: %8.8s", (char *)&ctx->header.identifier);
        cleanup_open_failure(ctx);
        errno = AARUF_ERROR_NOT_AARUFORMAT;

        TRACE("Exiting aaruf_open() = NULL");
        return NULL;
    }

    // Read new header version
    if(ctx->header.imageMajorVersion >= AARUF_VERSION_V2)
    {
        TRACE("Reading new header version at position 0");
        fseek(ctx->imageStream, 0, SEEK_SET);
        read_bytes = fread(&ctx->header, 1, sizeof(AaruHeaderV2), ctx->imageStream);

        if(read_bytes != sizeof(AaruHeaderV2))
        {
            cleanup_open_failure(ctx);
            errno = AARUF_ERROR_FILE_TOO_SMALL;

            return NULL;
        }
    }

    if(ctx->header.imageMajorVersion > AARUF_VERSION)
    {
        FATAL("Incompatible AaruFormat version %d.%d found, maximum supported is %d.%d", ctx->header.imageMajorVersion,
              ctx->header.imageMinorVersion, AARUF_VERSION_V2, 0);
        cleanup_open_failure(ctx);
        errno = AARUF_ERROR_INCOMPATIBLE_VERSION;

        TRACE("Exiting aaruf_open() = NULL");
        return NULL;
    }

    TRACE("Opening image version %d.%d", ctx->header.imageMajorVersion, ctx->header.imageMinorVersion);

    TRACE("Allocating memory for readable sector tags bitmap");
    ctx->readableSectorTags = (bool *)malloc(sizeof(bool) * MaxSectorTag);

    if(ctx->readableSectorTags == NULL)
    {
        FATAL("Could not allocate memory for readable sector tags bitmap");
        cleanup_open_failure(ctx);
        errno = AARUF_ERROR_NOT_ENOUGH_MEMORY;

        TRACE("Exiting aaruf_open() = NULL");
        return NULL;
    }

    memset(ctx->readableSectorTags, 0, sizeof(bool) * MaxSectorTag);

    TRACE("Setting up image info");
    ctx->imageInfo.Application        = ctx->header.application;
    ctx->imageInfo.ApplicationVersion = (uint8_t *)malloc(32);
    if(ctx->imageInfo.ApplicationVersion != NULL)
    {
        memset(ctx->imageInfo.ApplicationVersion, 0, 32);
        sprintf((char *)ctx->imageInfo.ApplicationVersion, "%d.%d", ctx->header.applicationMajorVersion,
                ctx->header.applicationMinorVersion);
    }
    ctx->imageInfo.Version = (uint8_t *)malloc(32);
    if(ctx->imageInfo.Version != NULL)
    {
        memset(ctx->imageInfo.Version, 0, 32);
        sprintf((char *)ctx->imageInfo.Version, "%d.%d", ctx->header.imageMajorVersion, ctx->header.imageMinorVersion);
    }
    ctx->imageInfo.MediaType = ctx->header.mediaType;

    // Read the index header
    TRACE("Reading index header at position %" PRIu64, ctx->header.indexOffset);
    pos = fseek(ctx->imageStream, ctx->header.indexOffset, SEEK_SET);
    if(pos < 0)
    {
        cleanup_open_failure(ctx);
        errno = AARUF_ERROR_CANNOT_READ_INDEX;

        return NULL;
    }

    pos = ftell(ctx->imageStream);
    if(pos != ctx->header.indexOffset)
    {
        cleanup_open_failure(ctx);
        errno = AARUF_ERROR_CANNOT_READ_INDEX;

        return NULL;
    }

    read_bytes = fread(&signature, 1, sizeof(uint32_t), ctx->imageStream);

    if(read_bytes != sizeof(uint32_t) ||
       (signature != IndexBlock && signature != IndexBlock2 && signature != IndexBlock3))
    {
        FATAL("Could not read index header or incorrect identifier %4.4s", (char *)&signature);
        cleanup_open_failure(ctx);
        errno = AARUF_ERROR_CANNOT_READ_INDEX;

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
        errno = AARUF_ERROR_CANNOT_READ_INDEX;

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

    bool found_user_data_ddt = false;
    ctx->imageInfo.ImageSize = 0;
    for(i = 0; i < utarray_len(index_entries); i++)
    {
        IndexEntry *entry = utarray_eltptr(index_entries, i);
        pos               = fseek(ctx->imageStream, entry->offset, SEEK_SET);

        if(pos < 0 || ftell(ctx->imageStream) != entry->offset)
        {
            TRACE("Could not seek to %" PRIu64 " as indicated by index entry %d, continuing...", entry->offset, i);

            continue;
        }

        TRACE("Processing block type %4.4s with data type %d at position %" PRIu64 "", (char *)&entry->blockType,
              entry->dataType, entry->offset);
        switch(entry->blockType)
        {
            case DataBlock:
                error_no = process_data_block(ctx, entry);

                if(error_no != AARUF_STATUS_OK)
                {
                    utarray_free(index_entries);
                    cleanup_open_failure(ctx);
                    errno = error_no;

                    return NULL;
                }

                break;

            case DeDuplicationTable:
                error_no = process_ddt_v1(ctx, entry, &found_user_data_ddt);

                if(error_no != AARUF_STATUS_OK)
                {
                    utarray_free(index_entries);
                    cleanup_open_failure(ctx);
                    errno = error_no;

                    return NULL;
                }

                break;
            case DeDuplicationTable2:
                error_no = process_ddt_v2(ctx, entry, &found_user_data_ddt);

                if(error_no != AARUF_STATUS_OK)
                {
                    utarray_free(index_entries);
                    cleanup_open_failure(ctx);
                    errno = error_no;

                    return NULL;
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
            default:
                TRACE("Unhandled block type %4.4s with data type %d is indexed to be at %" PRIu64 "",
                      (char *)&entry->blockType, entry->dataType, entry->offset);
                break;
        }
    }

    utarray_free(index_entries);

    if(!found_user_data_ddt)
    {
        FATAL("Could not find user data deduplication table, aborting...");
        aaruf_close(ctx);

        TRACE("Exiting aaruf_open() = NULL");
        return NULL;
    }

    ctx->imageInfo.CreationTime         = ctx->header.creationTime;
    ctx->imageInfo.LastModificationTime = ctx->header.lastWrittenTime;
    ctx->imageInfo.XmlMediaType         = aaruf_get_xml_mediatype(ctx->header.mediaType);

    if(ctx->geometryBlock.identifier != GeometryBlock && ctx->imageInfo.XmlMediaType == BlockMedia)
    {
        ctx->Cylinders       = (uint32_t)(ctx->imageInfo.Sectors / 16 / 63);
        ctx->Heads           = 16;
        ctx->SectorsPerTrack = 63;
    }

    // Initialize caches
    TRACE("Initializing caches");
    ctx->blockHeaderCache.cache = NULL;
    ctx->blockCache.cache       = NULL;

    const uint64_t cache_divisor = (uint64_t)ctx->imageInfo.SectorSize * (1ULL << ctx->shift);
    if(cache_divisor == 0)
    {
        ctx->blockHeaderCache.max_items = 0;
        ctx->blockCache.max_items       = 0;
    }
    else
    {
        ctx->blockHeaderCache.max_items = MAX_CACHE_SIZE / cache_divisor;
        ctx->blockCache.max_items       = ctx->blockHeaderCache.max_items;
    }

    // TODO: Cache tracks and sessions?

    // Initialize ECC for Compact Disc
    TRACE("Initializing ECC for Compact Disc");
    ctx->eccCdContext = (CdEccContext *)aaruf_ecc_cd_init();

    ctx->magic               = AARU_MAGIC;
    ctx->libraryMajorVersion = LIBAARUFORMAT_MAJOR_VERSION;
    ctx->libraryMinorVersion = LIBAARUFORMAT_MINOR_VERSION;

    TRACE("Exiting aaruf_open() = %p", ctx);
    return ctx;
}