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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aaruformat.h"
#include "internal.h"
#include "log.h"

/**
 * @brief Creates a new AaruFormat image file.
 *
 * Allocates and initializes a new aaruformat context and image file with the specified parameters.
 * This function sets up all necessary data structures including headers, DDT (deduplication table),
 * caches, and index entries for writing a new AaruFormat image. It also handles file creation,
 * memory allocation, and proper initialization of the writing context.
 *
 * @param filepath Path to the image file to create.
 * @param media_type Media type identifier.
 * @param sector_size Size of each sector in bytes.
 * @param user_sectors Number of user data sectors.
 * @param negative_sectors Number of negative sectors.
 * @param overflow_sectors Number of overflow sectors.
 * @param options String with creation options (parsed for alignment and shift parameters).
 * @param application_name Pointer to the application name string.
 * @param application_name_length Length of the application name string (must be ≤ AARU_HEADER_APP_NAME_LEN).
 * @param application_major_version Major version of the application.
 * @param application_minor_version Minor version of the application.
 *
 * @return Returns one of the following:
 * @retval aaruformatContext* Successfully created and initialized context. The returned pointer contains:
 *         - Properly initialized AaruFormat headers and metadata
 *         - Allocated and configured DDT structures for deduplication
 *         - Initialized block and header caches for performance
 *         - Open file stream ready for writing operations
 *         - Index entries array ready for block tracking
 *         - ECC context initialized for Compact Disc support
 *
 * @retval NULL Creation failed. The specific error can be determined by checking errno, which will be set to:
 *         - AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) when memory allocation fails for:
 *           * Context allocation
 *           * Readable sector tags array allocation
 *           * Application version string allocation
 *           * Image version string allocation
 *           * DDT table allocation (userDataDdtMini or userDataDdtBig)
 *           * Index entries array allocation
 *         - AARUF_ERROR_CANNOT_CREATE_FILE (-19) when file operations fail:
 *           * Unable to open the specified filepath for writing
 *           * File seek operations fail during initialization
 *           * File system errors or permission issues
 *         - AARUF_ERROR_INVALID_APP_NAME_LENGTH (-20) when:
 *           * application_name_length exceeds AARU_HEADER_APP_NAME_LEN
 *
 * @note Memory Management:
 *       - The function performs extensive memory allocation for various context structures
 *       - On failure, all previously allocated memory is properly cleaned up
 *       - The returned context must be freed using appropriate cleanup functions
 *
 * @note File Operations:
 *       - Creates a new file at the specified path (overwrites existing files)
 *       - Opens the file in binary read/write mode ("wb+")
 *       - Positions the file pointer at the calculated data start position
 *       - File alignment is handled based on parsed options
 *
 * @note DDT Initialization:
 *       - Uses DDT version 2 format with configurable compression and alignment
 *       - Supports both small (16-bit) and big (32-bit) DDT entry sizes
 *       - Calculates optimal table sizes based on sector counts and shift parameters
 *       - All DDT entries are initialized to zero (indicating unallocated sectors)
 *
 * @note Options Parsing:
 *       - The options string is parsed to extract block_alignment, data_shift, and table_shift
 *       - These parameters affect memory usage, performance, and file organization
 *       - Invalid options may result in suboptimal performance but won't cause failure
 *
 * @warning The created context is in writing mode and expects proper finalization
 *          before closing to ensure index and metadata are written correctly.
 *
 * @warning Application name length validation is strict - exceeding the limit will
 *          cause creation failure with AARUF_ERROR_INVALID_APP_NAME_LENGTH.
 */
void *aaruf_create(const char *filepath, const uint32_t media_type, const uint32_t sector_size,
                   const uint64_t user_sectors, const uint64_t negative_sectors, const uint64_t overflow_sectors,
                   const char *options, const uint8_t *application_name, const uint8_t application_name_length,
                   const uint8_t application_major_version, const uint8_t application_minor_version)
{
    TRACE("Entering aaruf_create(%s, %u, %u, %llu, %llu, %llu, %s, %s, %u, %u, %u)", filepath, media_type, sector_size,
          user_sectors, negative_sectors, overflow_sectors, options,
          application_name ? (const char *)application_name : "NULL", application_name_length,
          application_major_version, application_minor_version);

    // Parse the options
    TRACE("Parsing options");
    const aaru_options parsed_options = parse_options(options);

    // Allocate context
    TRACE("Allocating memory for context");
    aaruformatContext *ctx = malloc(sizeof(aaruformatContext));
    if(ctx == NULL)
    {
        FATAL("Not enough memory to create context");
        errno = AARUF_ERROR_NOT_ENOUGH_MEMORY;

        TRACE("Exiting aaruf_create() = NULL");
        return NULL;
    }

    memset(ctx, 0, sizeof(aaruformatContext));

    // Create the image file
    TRACE("Creating image file %s", filepath);
    ctx->imageStream = fopen(filepath, "wb+");
    if(ctx->imageStream == NULL)
    {
        FATAL("Error %d opening file %s for writing", errno, filepath);
        free(ctx);
        errno = AARUF_ERROR_CANNOT_CREATE_FILE;

        TRACE("Exiting aaruf_create() = NULL");

        return NULL;
    }

    if(application_name_length > AARU_HEADER_APP_NAME_LEN)
    {
        FATAL("Application name too long (%u bytes, maximum %u bytes)", application_name_length,
              AARU_HEADER_APP_NAME_LEN);
        free(ctx);
        errno = AARUF_ERROR_INVALID_APP_NAME_LENGTH;

        TRACE("Exiting aaruf_create() = NULL");
        return NULL;
    }

    // Initialize header
    TRACE("Initializing header");
    ctx->header.identifier = AARU_MAGIC;
    memcpy(ctx->header.application, application_name, application_name_length);
    ctx->header.imageMajorVersion       = AARUF_VERSION_V2;
    ctx->header.imageMinorVersion       = 0;
    ctx->header.applicationMajorVersion = application_major_version;
    ctx->header.applicationMinorVersion = application_minor_version;
    ctx->header.mediaType               = media_type;
    ctx->header.indexOffset             = 0;
    ctx->header.creationTime            = get_filetime_uint64();
    ctx->header.lastWrittenTime         = get_filetime_uint64();

    ctx->readableSectorTags = (bool *)malloc(sizeof(bool) * MaxSectorTag);

    if(ctx->readableSectorTags == NULL)
    {
        free(ctx);
        errno = AARUF_ERROR_NOT_ENOUGH_MEMORY;

        return NULL;
    }

    memset(ctx->readableSectorTags, 0, sizeof(bool) * MaxSectorTag);

    // Initialize image info
    TRACE("Initializing image info");
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
    ctx->imageInfo.MediaType            = ctx->header.mediaType;
    ctx->imageInfo.ImageSize            = 0;
    ctx->imageInfo.CreationTime         = ctx->header.creationTime;
    ctx->imageInfo.LastModificationTime = ctx->header.lastWrittenTime;
    ctx->imageInfo.XmlMediaType         = aaruf_get_xml_mediatype(ctx->header.mediaType);
    ctx->imageInfo.SectorSize           = sector_size;

    // Initialize caches
    TRACE("Initializing caches");
    ctx->blockHeaderCache.cache     = NULL;
    ctx->blockHeaderCache.max_items = MAX_CACHE_SIZE / (ctx->imageInfo.SectorSize * (1 << ctx->shift));
    ctx->blockCache.cache           = NULL;
    ctx->blockCache.max_items       = ctx->blockHeaderCache.max_items;

    // TODO: Cache tracks and sessions?

    // Initialize ECC for Compact Disc
    TRACE("Initializing Compact Disc ECC");
    ctx->eccCdContext = (CdEccContext *)aaruf_ecc_cd_init();

    ctx->magic               = AARU_MAGIC;
    ctx->libraryMajorVersion = LIBAARUFORMAT_MAJOR_VERSION;
    ctx->libraryMinorVersion = LIBAARUFORMAT_MINOR_VERSION;

    // Initialize DDT2
    TRACE("Initializing DDT2");
    ctx->inMemoryDdt                           = true;
    ctx->userDataDdtHeader.identifier          = DeDuplicationTable2;
    ctx->userDataDdtHeader.type                = UserData;
    ctx->userDataDdtHeader.compression         = None;
    ctx->userDataDdtHeader.levels              = 2;
    ctx->userDataDdtHeader.tableLevel          = 0;
    ctx->userDataDdtHeader.previousLevelOffset = 0;
    ctx->userDataDdtHeader.negative            = negative_sectors;
    ctx->userDataDdtHeader.blocks              = user_sectors + overflow_sectors + negative_sectors;
    ctx->userDataDdtHeader.overflow            = overflow_sectors;
    ctx->userDataDdtHeader.start               = 0;
    ctx->userDataDdtHeader.blockAlignmentShift = parsed_options.block_alignment;
    ctx->userDataDdtHeader.dataShift           = parsed_options.data_shift;
    ctx->userDataDdtHeader.sizeType            = 1;
    ctx->userDataDdtHeader.entries = ctx->userDataDdtHeader.blocks / (1 << ctx->userDataDdtHeader.tableShift);

    if(parsed_options.table_shift == -1)
    {
        uint64_t total_sectors = user_sectors + overflow_sectors + negative_sectors;

        if(total_sectors < 0x8388608ULL)
            ctx->userDataDdtHeader.tableShift = 0;
        else
            ctx->userDataDdtHeader.tableShift = 22;
    }
    else
        ctx->userDataDdtHeader.tableShift = parsed_options.table_shift;

    if(ctx->userDataDdtHeader.blocks % (1 << ctx->userDataDdtHeader.tableShift) != 0) ctx->userDataDdtHeader.entries++;

    TRACE("Initializing primary/single DDT");
    if(ctx->userDataDdtHeader.sizeType == SmallDdtSizeType)
        ctx->userDataDdtMini =
            (uint16_t *)calloc(ctx->userDataDdtHeader.entries, sizeof(uint16_t));  // All entries to zero
    else if(ctx->userDataDdtHeader.sizeType == BigDdtSizeType)
        ctx->userDataDdtBig =
            (uint32_t *)calloc(ctx->userDataDdtHeader.entries, sizeof(uint32_t));  // All entries to zero

    // Set the primary DDT offset (just after the header, block aligned)
    ctx->primaryDdtOffset        = sizeof(AaruHeaderV2);  // Start just after the header
    const uint64_t alignmentMask = (1ULL << ctx->userDataDdtHeader.blockAlignmentShift) - 1;
    ctx->primaryDdtOffset        = ctx->primaryDdtOffset + alignmentMask & ~alignmentMask;

    TRACE("Primary DDT will be placed at offset %" PRIu64, ctx->primaryDdtOffset);

    // Calculate size of primary DDT table
    const uint64_t primaryTableSize = ctx->userDataDdtHeader.sizeType == SmallDdtSizeType
                                          ? ctx->userDataDdtHeader.entries * sizeof(uint16_t)
                                          : ctx->userDataDdtHeader.entries * sizeof(uint32_t);

    // Calculate where data blocks can start (after primary DDT + header)
    const uint64_t dataStartPosition = ctx->primaryDdtOffset + sizeof(DdtHeader2) + primaryTableSize;
    ctx->nextBlockPosition           = dataStartPosition + alignmentMask & ~alignmentMask;

    TRACE("Data blocks will start at position %" PRIu64, ctx->nextBlockPosition);

    // Position file pointer at the data start position
    if(fseek(ctx->imageStream, (long)ctx->nextBlockPosition, SEEK_SET) != 0)
    {
        FATAL("Could not seek to data start position");
        free(ctx->readableSectorTags);
        if(ctx->userDataDdtMini) free(ctx->userDataDdtMini);
        if(ctx->userDataDdtBig) free(ctx->userDataDdtBig);
        utarray_free(ctx->indexEntries);
        free(ctx);
        errno = AARUF_ERROR_CANNOT_CREATE_FILE;
        return NULL;
    }

    // Initialize index entries array
    TRACE("Initializing index entries array");
    const UT_icd index_entry_icd = {sizeof(IndexEntry), NULL, NULL, NULL};
    utarray_new(ctx->indexEntries, &index_entry_icd);

    if(ctx->indexEntries == NULL)
    {
        FATAL("Not enough memory to create index entries array");
        free(ctx->readableSectorTags);
        free(ctx);
        errno = AARUF_ERROR_NOT_ENOUGH_MEMORY;

        TRACE("Exiting aaruf_create() = NULL");
        return NULL;
    }

    ctx->deduplicate = parsed_options.deduplicate;
    if(ctx->deduplicate)
        ctx->sectorHashMap = create_map(ctx->userDataDdtHeader.blocks * 25 / 100);  // 25% of total sectors

    ctx->rewinded           = false;
    ctx->last_written_block = 0;

    if(parsed_options.md5)
    {
        ctx->calculating_md5 = true;
        aaruf_md5_init(&ctx->md5_context);
    }
    if(parsed_options.sha1)
    {
        ctx->calculating_sha1 = true;
        aaruf_sha1_init(&ctx->sha1_context);
    }

    // Is writing
    ctx->isWriting = true;

    TRACE("Exiting aaruf_create() = %p", ctx);
    // Return context
    return ctx;
}
