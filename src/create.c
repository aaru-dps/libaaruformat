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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aaruformat.h"
#include "enums.h"
#include "internal.h"
#include "log.h"

static void cleanup_failed_create(aaruformat_context *ctx)
{
    if(ctx == NULL) return;

    if(ctx->sector_hash_map != NULL)
    {
        free_map(ctx->sector_hash_map);
        ctx->sector_hash_map = NULL;
    }

    if(ctx->index_entries != NULL)
    {
        utarray_free(ctx->index_entries);
        ctx->index_entries = NULL;
    }

    if(ctx->user_data_ddt2 != NULL)
    {
        free(ctx->user_data_ddt2);
        ctx->user_data_ddt2 = NULL;
    }

    if(ctx->spamsum_context != NULL)
    {
        aaruf_spamsum_free(ctx->spamsum_context);
        ctx->spamsum_context = NULL;
    }

    if(ctx->blake3_context != NULL)
    {
        free(ctx->blake3_context);
        ctx->blake3_context = NULL;
    }

    if(ctx->ecc_cd_context != NULL)
    {
        free(ctx->ecc_cd_context);
        ctx->ecc_cd_context = NULL;
    }

    if(ctx->readableSectorTags != NULL)
    {
        free(ctx->readableSectorTags);
        ctx->readableSectorTags = NULL;
    }

    // ApplicationVersion and Version are fixed-size arrays, not pointers - no need to free

    if(ctx->imageStream != NULL)
    {
        fclose(ctx->imageStream);
        ctx->imageStream = NULL;
    }

    free(ctx);
}

/**
 * @brief Creates a new AaruFormat image file.
 *
 * Allocates and initializes a new aaruformat context and image file with the specified parameters.
 * This function sets up all necessary data structures including headers, DDT (deduplication table),
 * caches, and index entries for writing a new AaruFormat image. It also handles file creation,
 * memory allocation, and proper initialization of the writing context. The function supports both
 * block-based media (disks, optical media) and sequential tape media with different initialization
 * strategies optimized for each media type.
 *
 * **Media Type Handling:**
 * The function creates different internal structures based on the `is_tape` parameter:
 *
 * **Block Media (is_tape = false):**
 * - Initializes full DDT (Deduplication Table) version 2 for sector-level deduplication
 * - Allocates primary DDT table (userDataDdtMini or userDataDdtBig) as a preallocated array
 * - Configures multi-level DDT support for large images (> 138,412,552 sectors)
 * - Enables optional deduplication hash map for detecting duplicate sectors
 * - Reserves space for DDT at the beginning of the file (after header, block-aligned)
 * - Data blocks start after DDT table to maintain sequential layout
 * - DDT size is fixed and known upfront based on sector count
 *
 * **Tape Media (is_tape = true):**
 * - Initializes DDT for sector-level deduplication using a different strategy
 * - Uses a growing hash table (tapeDdt) instead of a preallocated array
 * - Sets ctx->is_tape flag and initializes ctx->tapeDdt to NULL (populated on first write)
 * - Data blocks start immediately after the header (block-aligned)
 * - Hash table grows dynamically as blocks are written
 * - Optimized for sequential write patterns typical of tape media
 * - Tape file/partition metadata is managed separately via additional hash tables
 * - More memory-efficient for tapes with unknown final size
 *
 * **Initialization Flow:**
 * 1. Parse creation options (compression, alignment, deduplication, checksums)
 * 2. Allocate and zero-initialize context structure
 * 3. Create/open image file in binary write mode
 * 4. Initialize AaruFormat header with application and version information
 * 5. Set up image metadata and sector size information
 * 6. Initialize block and header caches for performance
 * 7. Initialize ECC context for Compact Disc support
 * 8. Branch based on media type:
 *    - Block media: Configure DDT structures and calculate offsets with preallocated array
 *    - Tape media: Set tape flags and initialize for dynamic hash table DDT
 * 9. Initialize index entries array for tracking all blocks
 * 10. Configure compression, checksums, and deduplication based on options
 * 11. Position file pointer at calculated data start position
 *
 * **DDT Configuration (Block Media Only):**
 * The function automatically selects optimal DDT parameters:
 * - Single-level DDT (tableShift=0): For images < 138,412,552 sectors
 * - Multi-level DDT (tableShift=22): For images ≥ 138,412,552 sectors
 *
 * The DDT offset calculation ensures proper alignment:
 * - Primary DDT placed immediately after header (block-aligned)
 * - Data blocks positioned after DDT table (block-aligned)
 * - Alignment controlled by blockAlignmentShift from options
 *
 * @param filepath Path to the image file to create. The file will be created if it doesn't exist,
 *                 or overwritten if it does. Must be a valid writable path.
 *
 * @param media_type Media type identifier (e.g., CompactDisc, DVD, HardDisk, Tape formats).
 *                   This affects how the image is structured and which features are enabled.
 *
 * @param sector_size Size of each sector/block in bytes. Common values:
 *                    - 512 bytes: Hard disks, floppy disks
 *                    - 2048 bytes: CD-ROM, DVD
 *                    - Variable: Tape media (block size varies by format)
 *
 * @param user_sectors Number of user data sectors/blocks in the image. This is the main
 *                     data area excluding negative (lead-in) and overflow (lead-out) regions.
 *                     For tape media, this may be an estimate as the final size is often unknown.
 *
 * @param negative_sectors Number of negative sectors (typically lead-in area for optical media).
 *                         Set to 0 for media types without lead-in areas. Not used for tape media.
 *
 * @param overflow_sectors Number of overflow sectors (typically lead-out area for optical media).
 *                         Set to 0 for media types without lead-out areas. Not used for tape media.
 *
 * @param options String with creation options in key=value format, semicolon-separated.
 *                Supported options:
 *                - "compress=true|false": Enable/disable LZMA compression
 *                - "deduplicate=true|false": Enable/disable sector deduplication (all media types)
 *                - "md5=true|false": Calculate MD5 checksum during write
 *                - "sha1=true|false": Calculate SHA-1 checksum during write
 *                - "sha256=true|false": Calculate SHA-256 checksum during write
 *                - "spamsum=true|false": Calculate SpamSum fuzzy hash during write
 *                - "blake3=true|false": Calculate BLAKE3 checksum during write
 *                - "block_alignment=N": Block alignment shift value (default varies)
 *                - "data_shift=N": Data shift value for DDT granularity
 *                - "table_shift=N": Table shift for multi-level DDT (-1 for auto, block media only)
 *                - "dictionary=N": LZMA dictionary size in bytes
 *                Example: "compress=true;deduplicate=true;md5=true;sha1=true"
 *
 * @param application_name Pointer to the application name string (UTF-8 encoded).
 *                        This identifies the software that created the image.
 *                        The string will be copied directly to the image header.
 *
 * @param application_name_length Length of the application name string in bytes.
 *                                Must be ≤ AARU_HEADER_APP_NAME_LEN (64 bytes).
 *
 * @param application_major_version Major version of the creating application (0-255).
 *
 * @param application_minor_version Minor version of the creating application (0-255).
 *
 * @param is_tape Boolean flag indicating tape media type:
 *                - true: Initialize for tape media (sequential, dynamic hash table DDT, file/partition metadata)
 *                - false: Initialize for block media (random access, preallocated array DDT)
 *
 * @return Returns one of the following:
 * @retval aaruformatContext* Successfully created and initialized context. The returned pointer contains:
 *         - Properly initialized AaruFormat headers and metadata
 *         - For block media: Allocated and configured DDT structures with preallocated arrays
 *         - For tape media: Tape flags set, DDT initialized as NULL (grows on demand)
 *         - Initialized block and header caches for performance
 *         - Open file stream ready for writing operations
 *         - Index entries array ready for block tracking
 *         - ECC context initialized for Compact Disc support
 *         - Checksum contexts initialized based on options
 *
 * @retval NULL Creation failed. The specific error can be determined by checking errno, which will be set to:
 *         - AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) when memory allocation fails for:
 *           * Context allocation
 *           * Readable sector tags array allocation
 *           * Application version string allocation
 *           * Image version string allocation
 *           * DDT table allocation (userDataDdtMini or userDataDdtBig, block media only)
 *           * Index entries array allocation
 *         - AARUF_ERROR_CANNOT_CREATE_FILE (-19) when file operations fail:
 *           * Unable to open the specified filepath for writing
 *           * File seek operations fail during initialization
 *           * File system errors or permission issues
 *         - AARUF_ERROR_INVALID_APP_NAME_LENGTH (-20) when:
 *           * application_name_length exceeds AARU_HEADER_APP_NAME_LEN (64 bytes)
 *
 * @note Memory Management:
 *       - The function performs extensive memory allocation for various context structures
 *       - On failure, all previously allocated memory is properly cleaned up
 *       - The returned context must be freed using aaruf_close() when finished
 *
 * @note File Operations:
 *       - Creates a new file at the specified path (overwrites existing files)
 *       - Opens the file in binary read/write mode ("wb+")
 *       - Positions the file pointer at the calculated data start position
 *       - File alignment is handled based on parsed options
 *
 * @note DDT Initialization (Block Media Only):
 *       - Uses DDT version 2 format with configurable compression and alignment
 *       - Calculates optimal table sizes based on sector counts and shift parameters
 *       - All DDT entries are initialized to zero (indicating unallocated sectors)
 *       - Multi-level DDT is used for images with ≥ 138,412,552 total sectors
 *       - Single-level DDT is used for smaller images for efficiency
 *       - DDT is a fixed-size preallocated array written to file at known offset
 *
 * @note Tape Media Initialization:
 *       - Tape images use a dynamic hash table DDT for sector-level deduplication
 *       - File and partition metadata is managed via separate hash tables
 *       - ctx->is_tape is set to 1 to indicate tape mode throughout the library
 *       - ctx->tapeDdt is initialized to NULL and grows dynamically as blocks are written
 *       - Data blocks can start immediately after header for optimal sequential access
 *       - The hash table DDT allows for efficient deduplication without knowing final size
 *       - More memory-efficient for tapes with unpredictable or very large sizes
 *       - Deduplication hash map may still be used alongside tapeDdt if enabled in options
 *
 * @note Options Parsing:
 *       - The options string is parsed to extract block_alignment, data_shift, and table_shift
 *       - These parameters affect memory usage, performance, and file organization
 *       - Invalid options may result in suboptimal performance but won't cause failure
 *       - Compression and checksums can be enabled independently via options
 *
 * @note Checksum Initialization:
 *       - MD5, SHA-1, SHA-256, SpamSum, and BLAKE3 can be calculated during write
 *       - Checksum contexts are initialized only if requested in options
 *       - Checksums are computed incrementally as sectors/blocks are written
 *       - Final checksums are stored in checksum block during image finalization
 *
 * @warning The created context is in writing mode and expects proper finalization
 *          before closing to ensure index and metadata are written correctly.
 *
 * @warning Application name length validation is strict - exceeding the limit will
 *          cause creation failure with AARUF_ERROR_INVALID_APP_NAME_LENGTH.
 *
 * @warning For tape media, the DDT structure is fundamentally different (hash table vs array).
 *          The is_tape flag must accurately reflect the media type being created.
 *
 * @warning The negative_sectors and overflow_sectors parameters are used only for
 *          block media. For tape media, these parameters are ignored.
 *
 * @see aaruf_close() for proper context cleanup and image finalization
 * @see aaruf_write_sector() for writing sectors to block media images
 * @see aaruf_set_tape_file() for defining tape file metadata
 * @see aaruf_set_tape_partition() for defining tape partition metadata
 */
AARU_EXPORT void AARU_CALL *aaruf_create(const char *filepath, const uint32_t media_type, const uint32_t sector_size,
                                         const uint64_t user_sectors, const uint64_t negative_sectors,
                                         const uint64_t overflow_sectors, const char *options,
                                         const uint8_t *application_name, const uint8_t application_name_length,
                                         const uint8_t application_major_version,
                                         const uint8_t application_minor_version, const bool is_tape)
{
    TRACE("Entering aaruf_create(%s, %u, %u, %llu, %llu, %llu, %s, %s, %u, %u, %u, %d)", filepath, media_type,
          sector_size, user_sectors, negative_sectors, overflow_sectors, options,
          application_name ? (const char *)application_name : "NULL", application_name_length,
          application_major_version, application_minor_version, is_tape);

    // Parse the options
    TRACE("Parsing options");
    bool               table_shift_found = false;
    const aaru_options parsed_options    = parse_options(options, &table_shift_found);

    // Allocate context
    TRACE("Allocating memory for context");
    aaruformat_context *ctx = malloc(sizeof(aaruformat_context));
    if(ctx == NULL)
    {
        FATAL("Not enough memory to create context");
        errno = AARUF_ERROR_NOT_ENOUGH_MEMORY;

        TRACE("Exiting aaruf_create() = NULL");
        return NULL;
    }

    memset(ctx, 0, sizeof(aaruformat_context));

    // Create the image file
    TRACE("Creating image file %s", filepath);
    ctx->imageStream = fopen(filepath, "wb+");
    if(ctx->imageStream == NULL)
    {
        FATAL("Error %d opening file %s for writing", errno, filepath);
        errno = AARUF_ERROR_CANNOT_CREATE_FILE;

        TRACE("Exiting aaruf_create() = NULL");
        cleanup_failed_create(ctx);
        return NULL;
    }

    if(application_name_length > AARU_HEADER_APP_NAME_LEN)
    {
        FATAL("Application name too long (%u bytes, maximum %u bytes)", application_name_length,
              AARU_HEADER_APP_NAME_LEN);
        errno = AARUF_ERROR_INVALID_APP_NAME_LENGTH;

        TRACE("Exiting aaruf_create() = NULL");
        cleanup_failed_create(ctx);
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

    // Generate random GUID for the image
    TRACE("Generating random GUID");
    generate_random_bytes(ctx->header.guid, GUID_SIZE);

    ctx->readableSectorTags = (bool *)malloc(sizeof(bool) * MaxSectorTag);

    if(ctx->readableSectorTags == NULL)
    {
        errno = AARUF_ERROR_NOT_ENOUGH_MEMORY;

        TRACE("Exiting aaruf_create() = NULL");
        cleanup_failed_create(ctx);
        return NULL;
    }

    memset(ctx->readableSectorTags, 0, sizeof(bool) * MaxSectorTag);

    // Initialize image info
    TRACE("Initializing image info");

    // Copy application name (UTF-8) to image_info
    memset(ctx->image_info.Application, 0, 64);
    size_t copy_len = application_name_length < 63 ? application_name_length : 63;
    memcpy(ctx->image_info.Application, application_name, copy_len);
    ctx->image_info.Application[63] = '\0';

    // Set application version string directly in the fixed-size array
    memset(ctx->image_info.ApplicationVersion, 0, 32);
    sprintf(ctx->image_info.ApplicationVersion, "%d.%d", ctx->header.applicationMajorVersion,
            ctx->header.applicationMinorVersion);

    // Set image version string directly in the fixed-size array
    memset(ctx->image_info.Version, 0, 32);
    sprintf(ctx->image_info.Version, "%d.%d", ctx->header.imageMajorVersion, ctx->header.imageMinorVersion);

    ctx->image_info.MediaType            = ctx->header.mediaType;
    ctx->image_info.ImageSize            = 0;
    ctx->image_info.CreationTime         = ctx->header.creationTime;
    ctx->image_info.LastModificationTime = ctx->header.lastWrittenTime;
    ctx->image_info.MetadataMediaType    = aaruf_get_xml_mediatype(ctx->header.mediaType);
    ctx->image_info.SectorSize           = sector_size;
    ctx->image_info.Sectors              = user_sectors;

    // Initialize caches
    TRACE("Initializing caches");
    ctx->block_header_cache.cache     = NULL;
    const uint64_t cache_divisor      = (uint64_t)ctx->image_info.SectorSize * (1ULL << ctx->shift);
    ctx->block_header_cache.max_items = cache_divisor == 0 ? 0 : MAX_CACHE_SIZE / cache_divisor;
    ctx->block_cache.cache            = NULL;
    ctx->block_cache.max_items        = ctx->block_header_cache.max_items;

    // TODO: Cache tracks and sessions?

    // Initialize ECC for Compact Disc
    TRACE("Initializing Compact Disc ECC");
    ctx->ecc_cd_context = (CdEccContext *)aaruf_ecc_cd_init();

    ctx->magic                 = AARU_MAGIC;
    ctx->library_major_version = LIBAARUFORMAT_MAJOR_VERSION;
    ctx->library_minor_version = LIBAARUFORMAT_MINOR_VERSION;

    if(!is_tape)
    {  // Initialize DDT2
        TRACE("Initializing DDT2");
        ctx->in_memory_ddt                            = true;
        ctx->user_data_ddt_header.identifier          = DeDuplicationTable2;
        ctx->user_data_ddt_header.type                = kDataTypeUserData;
        ctx->user_data_ddt_header.compression         = kCompressionNone;
        ctx->user_data_ddt_header.tableLevel          = 0;
        ctx->user_data_ddt_header.previousLevelOffset = 0;
        ctx->user_data_ddt_header.negative            = negative_sectors;
        ctx->user_data_ddt_header.blocks              = user_sectors + overflow_sectors + negative_sectors;
        ctx->user_data_ddt_header.overflow            = overflow_sectors;
        ctx->user_data_ddt_header.start               = 0;
        ctx->user_data_ddt_header.blockAlignmentShift = parsed_options.block_alignment;
        ctx->header.blockAlignmentShift               = parsed_options.block_alignment;
        ctx->user_data_ddt_header.dataShift           = parsed_options.data_shift;

        if(parsed_options.table_shift == -1 || !table_shift_found)
        {
            const uint64_t total_sectors = user_sectors + overflow_sectors + negative_sectors;

            if(total_sectors < 8388608ULL)
                ctx->user_data_ddt_header.tableShift = 0;
            else
                ctx->user_data_ddt_header.tableShift = 22;
        }
        else
            ctx->user_data_ddt_header.tableShift =
                parsed_options.table_shift > 0 ? (uint8_t)parsed_options.table_shift : 0;

        ctx->user_data_ddt_header.levels = ctx->user_data_ddt_header.tableShift > 0 ? 2 : 1;

        uint8_t effective_table_shift = ctx->user_data_ddt_header.tableShift;
        if(effective_table_shift >= 63)
        {
            TRACE("Clamping table shift from %u to 62 to avoid overflow", effective_table_shift);
            effective_table_shift                = 62;
            ctx->user_data_ddt_header.tableShift = effective_table_shift;
        }

        const uint64_t sectors_per_entry  = 1ULL << effective_table_shift;
        ctx->user_data_ddt_header.entries = ctx->user_data_ddt_header.blocks / sectors_per_entry;
        if(ctx->user_data_ddt_header.blocks % sectors_per_entry != 0 || ctx->user_data_ddt_header.entries == 0)
            ctx->user_data_ddt_header.entries++;

        TRACE("Initializing primary/single DDT");
        ctx->user_data_ddt2 =
            (uint64_t *)calloc(ctx->user_data_ddt_header.entries, sizeof(uint64_t));  // All entries to zero
        if(ctx->user_data_ddt2 == NULL)
        {
            FATAL("Not enough memory to allocate primary DDT (big)");
            errno = AARUF_ERROR_NOT_ENOUGH_MEMORY;
            TRACE("Exiting aaruf_create() = NULL");
            cleanup_failed_create(ctx);
            return NULL;
        }

        // Set the primary DDT offset (just after the header, block aligned)
        ctx->primary_ddt_offset       = sizeof(AaruHeaderV2);  // Start just after the header
        const uint64_t alignment_mask = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
        ctx->primary_ddt_offset       = ctx->primary_ddt_offset + alignment_mask & ~alignment_mask;

        TRACE("Primary DDT will be placed at offset %" PRIu64, ctx->primary_ddt_offset);

        // Calculate size of primary DDT table
        const uint64_t primary_table_size = ctx->user_data_ddt_header.entries * sizeof(uint64_t);

        // Calculate where data blocks can start (after primary DDT + header)
        if(ctx->user_data_ddt_header.tableShift > 0)
        {
            const uint64_t data_start_position = ctx->primary_ddt_offset + sizeof(DdtHeader2) + primary_table_size;
            ctx->next_block_position           = data_start_position + alignment_mask & ~alignment_mask;
        }
        else
            ctx->next_block_position = ctx->primary_ddt_offset;  // Single-level DDT can start anywhere
    }
    else
    {
        // Fill needed values
        ctx->user_data_ddt_header.blockAlignmentShift = parsed_options.block_alignment;
        ctx->header.blockAlignmentShift               = parsed_options.block_alignment;
        ctx->user_data_ddt_header.dataShift           = parsed_options.data_shift;

        // Calculate aligned next block position
        const uint64_t alignment_mask = (1ULL << parsed_options.block_alignment) - 1;
        ctx->next_block_position      = sizeof(AaruHeaderV2);  // Start just after the header
        ctx->next_block_position      = ctx->next_block_position + alignment_mask & ~alignment_mask;
        ctx->is_tape                  = 1;
        ctx->tape_ddt                 = NULL;
    }

    TRACE("Data blocks will start at position %" PRIu64, ctx->next_block_position);

    // Position file pointer at the data start position
    if(fseek(ctx->imageStream, ctx->next_block_position, SEEK_SET) != 0)
    {
        FATAL("Could not seek to data start position");
        errno = AARUF_ERROR_CANNOT_CREATE_FILE;
        TRACE("Exiting aaruf_create() = NULL");
        cleanup_failed_create(ctx);
        return NULL;
    }

    // Initialize index entries array
    TRACE("Initializing index entries array");
    const UT_icd index_entry_icd = {sizeof(IndexEntry), NULL, NULL, NULL};
    utarray_new(ctx->index_entries, &index_entry_icd);

    if(ctx->index_entries == NULL)
    {
        FATAL("Not enough memory to create index entries array");
        errno = AARUF_ERROR_NOT_ENOUGH_MEMORY;

        TRACE("Exiting aaruf_create() = NULL");
        cleanup_failed_create(ctx);
        return NULL;
    }

    ctx->compression_enabled = parsed_options.compress;
    ctx->lzma_dict_size      = parsed_options.dictionary;
    ctx->deduplicate         = parsed_options.deduplicate;
    ctx->use_zstd            = parsed_options.zstd;
    ctx->zstd_level          = parsed_options.zstd_level;
    if(ctx->deduplicate)
        ctx->sector_hash_map = create_map(ctx->user_data_ddt_header.blocks * 25 / 100);  // 25% of total sectors

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
    if(parsed_options.sha256)
    {
        ctx->calculating_sha256 = true;
        aaruf_sha256_init(&ctx->sha256_context);
    }
    if(parsed_options.spamsum)
    {
        ctx->calculating_spamsum = true;
        ctx->spamsum_context     = aaruf_spamsum_init();
    }
    if(parsed_options.blake3)
    {
        ctx->blake3_context = calloc(1, sizeof(blake3_hasher));
        if(ctx->blake3_context != NULL)
        {
            ctx->calculating_blake3 = true;
            blake3_hasher_init(ctx->blake3_context);
        }
    }

    // Is writing
    ctx->is_writing = true;

    // Initialize dirty flags - all true by default for new images
    ctx->dirty_secondary_ddt                 = true;
    ctx->dirty_primary_ddt                   = true;
    ctx->dirty_single_level_ddt              = true;
    ctx->dirty_checksum_block                = true;
    ctx->dirty_tracks_block                  = true;
    ctx->dirty_mode2_subheaders_block        = true;
    ctx->dirty_sector_prefix_block           = true;
    ctx->dirty_sector_prefix_ddt             = true;
    ctx->dirty_sector_suffix_block           = true;
    ctx->dirty_sector_suffix_ddt             = true;
    ctx->dirty_sector_subchannel_block       = true;
    ctx->dirty_dvd_long_sector_blocks        = true;
    ctx->dirty_dvd_title_key_decrypted_block = true;
    ctx->dirty_media_tags                    = true;
    ctx->dirty_tape_ddt                      = true;
    ctx->dirty_tape_file_block               = true;
    ctx->dirty_tape_partition_block          = true;
    ctx->dirty_geometry_block                = true;
    ctx->dirty_metadata_block                = true;
    ctx->dirty_dumphw_block                  = true;
    ctx->dirty_cicm_block                    = true;
    ctx->dirty_json_block                    = true;
    ctx->dirty_flux_block                    = true;
    ctx->dirty_index_block                   = true;

    TRACE("Exiting aaruf_create() = %p", ctx);
    // Return context
    return ctx;
}
