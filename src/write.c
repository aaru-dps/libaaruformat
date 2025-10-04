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
#include "structs/lisa_tag.h"
#include "xxhash.h"

/**
 * @brief Writes a sector to the AaruFormat image.
 *
 * Writes the given data to the specified sector address in the image, with the given status and length.
 * This function handles buffering data into blocks, automatically closing blocks when necessary (sector
 * size changes or block size limits are reached), and managing the deduplication table (DDT) entries.
 *
 * @param context Pointer to the aaruformat context.
 * @param sector_address Logical sector address to write.
 * @param negative Indicates if the sector address is negative.
 * @param data Pointer to the data buffer to write.
 * @param sector_status Status of the sector to write.
 * @param length Length of the data buffer.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully wrote the sector data. This is returned when:
 *         - The sector data is successfully copied to the writing buffer
 *         - The DDT entry is successfully updated for the sector address
 *         - Block management operations complete successfully
 *         - Buffer positions and offsets are properly updated
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *
 * @retval AARUF_READ_ONLY (-22) Attempting to write to a read-only image. This occurs when:
 *         - The context's isWriting flag is false
 *         - The image was opened in read-only mode
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. This occurs when:
 *         - Failed to allocate memory for the writing buffer when creating a new block
 *         - The system is out of available memory for buffer allocation
 *
 * @retval AARUF_ERROR_CANNOT_WRITE_BLOCK_HEADER (-23) Failed to write block header to the image file.
 *         This can occur during automatic block closure when:
 *         - The fwrite() call for the block header fails
 *         - Disk space is insufficient or file system errors occur
 *
 * @retval AARUF_ERROR_CANNOT_WRITE_BLOCK_DATA (-24) Failed to write block data to the image file.
 *         This can occur during automatic block closure when:
 *         - The fwrite() call for the block data fails
 *         - Disk space is insufficient or file system errors occur
 *
 * @retval AARUF_ERROR_CANNOT_SET_DDT_ENTRY (-25) Failed to update the deduplication table (DDT) entry.
 *         This occurs when:
 *         - The DDT entry for the specified sector address could not be set or updated
 *         - Internal DDT management functions return failure
 *         - DDT table corruption or memory issues prevent entry updates
 *
 * @note Block Management:
 *       - The function automatically closes the current block when sector size changes
 *       - Blocks are also closed when they reach the maximum size (determined by dataShift)
 *       - New blocks are created automatically when needed with appropriate headers
 *
 * @note Memory Management:
 *       - Writing buffers are allocated on-demand when creating new blocks
 *       - Buffer memory is freed when blocks are closed
 *       - Buffer size is calculated based on sector size and data shift parameters
 *
 * @note DDT Updates:
 *       - Each written sector updates the corresponding DDT entry
 *       - DDT entries track block offset, position, and sector status
 *       - Uses DDT version 2 format for entries
 *
 * @warning The function may trigger automatic block closure, which can result in disk I/O
 *          operations and potential write errors even for seemingly simple sector writes.
 */
int32_t aaruf_write_sector(void *context, uint64_t sector_address, bool negative, const uint8_t *data,
                           uint8_t sector_status, uint32_t length)
{
    TRACE("Entering aaruf_write_sector(%p, %" PRIu64 ", %d, %p, %u, %u)", context, sector_address, negative, data,
          sector_status, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformatContext *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->isWriting)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_write_sector() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    if(negative && sector_address > ctx->userDataDdtHeader.negative - 1)
    {
        FATAL("Sector address out of bounds");

        TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_SECTOR_OUT_OF_BOUNDS");
        return AARUF_ERROR_SECTOR_OUT_OF_BOUNDS;
    }

    if(!negative && sector_address > ctx->imageInfo.Sectors + ctx->userDataDdtHeader.overflow - 1)
    {
        FATAL("Sector address out of bounds");

        TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_SECTOR_OUT_OF_BOUNDS");
        return AARUF_ERROR_SECTOR_OUT_OF_BOUNDS;
    }

    if(!ctx->rewinded)
    {
        if(sector_address <= ctx->last_written_block)
        {
            TRACE("Rewinded");
            ctx->rewinded = true;

            // Disable MD5 calculation
            if(ctx->calculating_md5) ctx->calculating_md5 = false;
            // Disable SHA1 calculation
            if(ctx->calculating_sha1) ctx->calculating_sha1 = false;
            // Disable SHA256 calculation
            if(ctx->calculating_sha256) ctx->calculating_sha256 = false;
            // Disable SpamSum calculation
            if(ctx->calculating_spamsum) ctx->calculating_spamsum = false;
            // Disable BLAKE3 calculation
            if(ctx->calculating_blake3) ctx->calculating_blake3 = false;
        }
        else
            ctx->last_written_block = sector_address;
    }

    // Calculate MD5 on-the-fly if requested and sector is within user sectors (not negative or overflow)
    if(ctx->calculating_md5 && !negative && sector_address <= ctx->imageInfo.Sectors && !ctx->writingLong)
        aaruf_md5_update(&ctx->md5_context, data, length);
    // Calculate SHA1 on-the-fly if requested and sector is within user sectors (not negative or overflow)
    if(ctx->calculating_sha1 && !negative && sector_address <= ctx->imageInfo.Sectors && !ctx->writingLong)
        aaruf_sha1_update(&ctx->sha1_context, data, length);
    // Calculate SHA256 on-the-fly if requested and sector is within user sectors (not negative or overflow)
    if(ctx->calculating_sha256 && !negative && sector_address <= ctx->imageInfo.Sectors && !ctx->writingLong)
        aaruf_sha256_update(&ctx->sha256_context, data, length);
    // Calculate SpamSum on-the-fly if requested and sector is within user sectors (not negative or overflow)
    if(ctx->calculating_sha256 && !negative && sector_address <= ctx->imageInfo.Sectors && !ctx->writingLong)
        aaruf_spamsum_update(ctx->spamsum_context, data, length);
    // Calculate BLAKE3 on-the-fly if requested and sector is within user sectors (not negative or overflow)
    if(ctx->calculating_blake3 && !negative && sector_address <= ctx->imageInfo.Sectors && !ctx->writingLong)
        blake3_hasher_update(ctx->blake3_context, data, length);

    // Close current block first
    if(ctx->writingBuffer != NULL &&
       // When sector size changes
       (ctx->currentBlockHeader.sectorSize != length || ctx->currentBlockOffset == 1 << ctx->userDataDdtHeader.dataShift
        // TODO: Implement compression
        ))
    {
        TRACE("Closing current block before writing new data");
        int error = aaruf_close_current_block(ctx);

        if(error != AARUF_STATUS_OK)
        {
            FATAL("Error closing current block: %d", error);

            TRACE("Exiting aaruf_write_sector() = %d", error);
            return error;
        }
    }

    uint64_t ddt_entry = 0;
    bool     ddt_ok;

    if(ctx->deduplicate)
    {
        // Calculate 64-bit XXH3 hash of the sector
        TRACE("Hashing sector data for deduplication");
        uint64_t hash = XXH3_64bits(data, length);

        // Check if the hash is already in the map
        bool existing = lookup_map(ctx->sectorHashMap, hash, &ddt_entry);
        TRACE("Block does %s exist in deduplication map", existing ? "already" : "not yet");

        ddt_ok = set_ddt_entry_v2(ctx, sector_address, negative, ctx->currentBlockOffset, ctx->nextBlockPosition,
                                  sector_status, &ddt_entry);
        if(!ddt_ok)
        {
            TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_CANNOT_SET_DDT_ENTRY");
            return AARUF_ERROR_CANNOT_SET_DDT_ENTRY;
        }

        if(existing)
        {
            TRACE("Sector exists, so not writing to image");
            TRACE("Exiting aaruf_write_sector() = AARUF_STATUS_OK");
            return AARUF_STATUS_OK;
        }

        TRACE("Inserting sector hash into deduplication map, proceeding to write into image as normal");
        insert_map(ctx->sectorHashMap, hash, ddt_entry);
    }
    else
        ddt_ok = set_ddt_entry_v2(ctx, sector_address, negative, ctx->currentBlockOffset, ctx->nextBlockPosition,
                                  sector_status, &ddt_entry);

    if(!ddt_ok)
    {
        TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_CANNOT_SET_DDT_ENTRY");
        return AARUF_ERROR_CANNOT_SET_DDT_ENTRY;
    }

    // No block set
    if(ctx->writingBufferPosition == 0)
    {
        TRACE("Creating new writing block");
        ctx->currentBlockHeader.identifier  = DataBlock;
        ctx->currentBlockHeader.type        = UserData;
        ctx->currentBlockHeader.compression = None;  // TODO: Compression
        ctx->currentBlockHeader.sectorSize  = length;

        // We need to save the track type for later compression
        if(ctx->imageInfo.XmlMediaType == OpticalDisc && ctx->trackEntries != NULL)
        {
            const TrackEntry *track = NULL;
            for(int i = 0; i < ctx->tracksHeader.entries; i++)
                if(sector_address >= ctx->trackEntries[i].start && sector_address <= ctx->trackEntries[i].end)
                {
                    track = &ctx->trackEntries[i];
                    break;
                }

            if(track != NULL)
            {
                ctx->currentTrackType = track->type;

                if(track->sequence == 0 && track->start == 0 && track->end == 0) ctx->currentTrackType = Data;
            }
            else
                ctx->currentTrackType = Data;

            if(ctx->currentTrackType == Audio &&
               // JaguarCD stores data in audio tracks. FLAC is too inefficient, we need to use LZMA as data.
               (ctx->imageInfo.MediaType == JaguarCD && track->session > 1 ||
                // VideoNow stores video in audio tracks, and LZMA works better too.
                ctx->imageInfo.MediaType == VideoNow || ctx->imageInfo.MediaType == VideoNowColor ||
                ctx->imageInfo.MediaType == VideoNowXp))
                ctx->currentTrackType = Data;
        }
        else
            ctx->currentTrackType = Data;

        uint32_t max_buffer_size = (1 << ctx->userDataDdtHeader.dataShift) * ctx->currentBlockHeader.sectorSize;
        TRACE("Setting max buffer size to %u bytes", max_buffer_size);

        TRACE("Allocating memory for writing buffer");
        ctx->writingBuffer = (uint8_t *)calloc(1, max_buffer_size);
        if(ctx->writingBuffer == NULL)
        {
            FATAL("Could not allocate memory");

            TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
        }
    }

    TRACE("Copying data to writing buffer at position %zu", ctx->writingBufferPosition);
    memcpy(ctx->writingBuffer + ctx->writingBufferPosition, data, length);
    TRACE("Advancing writing buffer position to %zu", ctx->writingBufferPosition + length);
    ctx->writingBufferPosition += length;
    TRACE("Advancing current block offset to %zu", ctx->currentBlockOffset + 1);
    ctx->currentBlockOffset++;

    TRACE("Exiting aaruf_write_sector() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Writes a full ("long") raw sector from optical or block media, parsing structure and validating content.
 *
 * This function processes complete raw sectors including structural metadata, error correction codes, and
 * subchannel information. It is the primary entry point for ingesting raw sector data where the caller
 * provides the complete sector including synchronization patterns, headers, user data, and error correction
 * information. The function intelligently parses the sector structure based on media type and track
 * information, validates correctness, and delegates user data writing to aaruf_write_sector().
 *
 * Supported Media Types and Sector Formats:
 *
 * **Optical Disc Media (2352-byte sectors):**
 *   - **Audio tracks**: Raw PCM audio data (2352 bytes) passed directly to aaruf_write_sector()
 *   - **Data tracks**: Raw data sectors passed directly to aaruf_write_sector()
 *   - **CD Mode 1**: Sync(12) + Header(4) + UserData(2048) + EDC(4) + Reserved(8) + ECC_P(172) + ECC_Q(104)
 *     * Validates sync pattern (00 FF FF FF FF FF FF FF FF FF FF 00), mode byte (01), and MSF timing
 *     * Checks EDC/ECC correctness using aaruf_ecc_cd_is_suffix_correct()
 *     * Stores anomalous prefixes/suffixes in separate buffers with mini-DDT indexing
 *   - **CD Mode 2 Form 1**: Sync(12) + Header(4) + Subheader(8) + UserData(2048) + EDC(4) + ECC_P(172) + ECC_Q(104)
 *     * Validates sync pattern, mode byte (02), and Form 1 identification in subheader
 *     * Checks both EDC and ECC correctness for Form 1 sectors
 *     * Extracts and stores 8-byte subheader separately in mode2_subheaders buffer
 *   - **CD Mode 2 Form 2**: Sync(12) + Header(4) + Subheader(8) + UserData(2324) + EDC(4)
 *     * Validates sync pattern, mode byte (02), and Form 2 identification in subheader
 *     * Checks EDC correctness, handles missing EDC (zero) as valid state
 *     * No ECC validation for Form 2 sectors (not present in format)
 *   - **CD Mode 2 Formless**: Similar to Form 2 but without form determination from subheader
 *
 * **Block Media (512+ byte sectors with tags):**
 *   - **Apple Profile/FileWare**: 512-byte sectors + 20-byte Profile tags
 *   - **Apple Sony SS/DS**: 512-byte sectors + 12-byte Sony tags
 *   - **Apple Widget**: 512-byte sectors with tag conversion support
 *   - **Priam DataTower**: 512-byte sectors + 24-byte Priam tags
 *   - Supports automatic tag format conversion between Sony (12), Profile (20), and Priam (24) byte formats
 *   - Tag data stored in sector_subchannel buffer for preservation
 *
 * **Data Processing Pipeline:**
 * 1. **Context and Parameter Validation**: Verifies context magic, write permissions, and sector bounds
 * 2. **Track Resolution**: Locates track entry covering the sector address to determine track type
 * 3. **Rewind Detection**: Detects out-of-order writing and disables hash calculations to maintain integrity
 * 4. **Hash Updates**: Updates MD5, SHA1, SHA256, SpamSum, and BLAKE3 contexts for user-range sectors
 * 5. **Structure Parsing**: Splits raw sector into prefix, user data, and suffix components
 * 6. **Validation**: Checks sync patterns, timing fields, EDC/ECC correctness, and format compliance
 * 7. **Metadata Storage**: Stores anomalous or non-standard components in dedicated buffers
 * 8. **User Data Delegation**: Calls aaruf_write_sector() with extracted user data and derived status
 *
 * **Memory Management Strategy:**
 * - **Mini-DDT Arrays**: Lazily allocated 16-bit arrays sized for total addressable space (negative + user + overflow)
 *   * sectorPrefixDdtMini: Tracks prefix status and buffer offsets (high 4 bits = status, low 12 bits = offset/16)
 *   * sectorSuffixDdtMini: Tracks suffix status and buffer offsets (high 4 bits = status, low 12 bits = offset/288)
 * - **Prefix Buffer**: Dynamically growing buffer storing non-standard 16-byte CD prefixes
 * - **Suffix Buffer**: Dynamically growing buffer storing non-standard CD suffixes (288 bytes for Mode 1, 4 bytes for
 * Mode 2 Form 2, 280 bytes for Mode 2 Form 1)
 * - **Subheader Buffer**: Fixed-size buffer (8 bytes per sector) for Mode 2 subheaders
 * - **Subchannel Buffer**: Fixed-size buffer for block media tag data
 * - All buffers use doubling reallocation strategy when capacity exceeded
 *
 * **Address Space Management:**
 * The function handles three logical address regions:
 * - **Negative Region**: Pre-gap sectors (sector_address < negative region size, negative=true)
 * - **User Region**: Main data sectors (0 ≤ sector_address < Sectors, negative=false)
 * - **Overflow Region**: Post-data sectors (sector_address ≥ Sectors, negative=false)
 * Internal corrected_sector_address provides linear indexing: corrected = address ± negative_size
 *
 * **Sector Status Classification:**
 * Status codes stored in high nibble of mini-DDT entries:
 * - **SectorStatusMode1Correct**: Valid Mode 1 sector with correct sync, timing, EDC, and ECC
 * - **SectorStatusMode2Form1Ok**: Valid Mode 2 Form 1 with correct subheader, EDC, and ECC
 * - **SectorStatusMode2Form2Ok**: Valid Mode 2 Form 2 with correct subheader and EDC
 * - **SectorStatusMode2Form2NoCrc**: Mode 2 Form 2 with zero EDC (acceptable state)
 * - **SectorStatusErrored**: Sector with validation errors, anomalous data stored in buffers
 * - **SectorStatusNotDumped**: All-zero sectors treated as not dumped
 *
 * **Deduplication Integration:**
 * Long sector processing does not directly perform deduplication - this occurs in the delegated
 * aaruf_write_sector() call for the extracted user data portion. The prefix/suffix/subheader
 * metadata is stored separately and not subject to deduplication.
 *
 * **Hash Calculation Behavior:**
 * - Hashes computed on complete raw sector data (all 2352 bytes for optical, full tag+data for block)
 * - Only performed for sectors in user address range (not negative or overflow regions)
 * - Permanently disabled upon rewind detection to prevent corrupted streaming digests
 * - Supports MD5, SHA1, SHA256, SpamSum, and BLAKE3 simultaneously when enabled
 *
 * **Error Recovery and Validation:**
 * - Sync pattern validation for optical sectors (CD standard 12-byte sync)
 * - MSF timing validation (Minutes:Seconds:Frames converted to LBA must match sector_address)
 * - Mode byte validation (01 for Mode 1, 02 for Mode 2)
 * - EDC validation using aaruf_edc_cd_compute() for computed vs stored comparison
 * - ECC validation using aaruf_ecc_cd_is_suffix_correct() and aaruf_ecc_cd_is_suffix_correct_mode2()
 * - Form determination from subheader flags (bit 5 of bytes 18 and 22)
 * - Tag format validation and conversion for block media
 *
 * **Thread Safety and Concurrency:**
 * This function is NOT thread-safe. The context contains mutable shared state including:
 * - Buffer pointers and offsets
 * - Hash computation contexts
 * - Rewind detection state
 * - DDT modification operations
 * External synchronization required for concurrent access.
 *
 * **Performance Considerations:**
 * - Buffer allocation occurs lazily on first use
 * - Buffer growth uses doubling strategy to amortize allocation cost
 * - Validation operations are optimized for common cases (correct sectors)
 * - Memory copying minimized for standard compliant sectors
 * - Hash updates operate on full sector to maintain streaming performance
 *
 * @param context Pointer to a valid aaruformatContext with magic == AARU_MAGIC opened for writing.
 * @param sector_address Logical Block Address (LBA) for the sector. For negative regions, this is
 *        the negative-space address; for user/overflow regions, this is the standard 0-based LBA.
 * @param negative true if sector_address refers to the negative (pre-gap) region;
 *        false for user or overflow regions.
 * @param data Pointer to the complete raw sector buffer. Must contain:
 *        - For optical: exactly 2352 bytes of raw sector data
 *        - For block media: 512 bytes + tag data (12, 20, or 24 bytes depending on format)
 * @param sector_status Initial sector status hint from caller. May be overridden based on validation
 *        results when delegating to aaruf_write_sector().
 * @param length Length in bytes of the data buffer. Must be exactly 2352 for optical discs.
 *        For block media: 512 (no tags), 524 (Sony), 532 (Profile), or 536 (Priam).
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Sector successfully processed and user data written. This occurs when:
 *         - Raw sector structure parsed and validated successfully
 *         - Prefix/suffix/subheader metadata stored appropriately
 *         - User data portion successfully delegated to aaruf_write_sector()
 *         - All buffer allocations and DDT updates completed successfully
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) Invalid context provided. This occurs when:
 *         - context parameter is NULL
 *         - Context magic number != AARU_MAGIC (wrong context type or corruption)
 *
 * @retval AARUF_READ_ONLY (-22) Attempting to write to read-only image. This occurs when:
 *         - Context isWriting flag is false
 *         - Image was opened without write permissions
 *
 * @retval AARUF_ERROR_SECTOR_OUT_OF_BOUNDS (-7) Sector address outside valid ranges. This occurs when:
 *         - negative=true and sector_address >= negative region size
 *         - negative=false and sector_address >= (Sectors + overflow region size)
 *
 * @retval AARUF_ERROR_INCORRECT_DATA_SIZE (-8) Invalid sector size for media type. This occurs when:
 *         - length != 2352 for optical disc media
 *         - length not in {512, 524, 532, 536} for supported block media types
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. This occurs when:
 *         - Failed to allocate mini-DDT arrays (sectorPrefixDdtMini, sectorSuffixDdtMini)
 *         - Failed to allocate or grow prefix buffer (sector_prefix)
 *         - Failed to allocate or grow suffix buffer (sector_suffix)
 *         - Failed to allocate subheader buffer (mode2_subheaders)
 *         - Failed to allocate subchannel buffer (sector_subchannel)
 *         - System out of memory during buffer reallocation
 *
 * @retval AARUF_ERROR_INCORRECT_MEDIA_TYPE (-26) Unsupported media type for long sectors. This occurs when:
 *         - Media type is not OpticalDisc or supported BlockMedia variant
 *         - Block media type does not support the provided tag format
 *
 * @retval AARUF_ERROR_CANNOT_SET_DDT_ENTRY (-25) DDT update failed. Propagated from aaruf_write_sector() when:
 *         - User data DDT entry could not be updated
 *         - DDT table corruption prevents entry modification
 *
 * @retval AARUF_ERROR_CANNOT_WRITE_BLOCK_HEADER (-23) Block header write failed. Propagated from aaruf_write_sector()
 * when:
 *         - Automatic block closure triggered by user data write fails
 *         - File system error prevents header write
 *
 * @retval AARUF_ERROR_CANNOT_WRITE_BLOCK_DATA (-24) Block data write failed. Propagated from aaruf_write_sector() when:
 *         - User data portion write fails during block flush
 *         - Insufficient disk space or I/O error occurs
 *
 * @note **Cross-References**: This function is the primary companion to aaruf_write_sector() for
 *       raw sector ingestion. See also:
 *       - aaruf_write_sector(): Handles user data portion writing and deduplication
 *       - aaruf_ecc_cd_is_suffix_correct(): CD Mode 1 ECC validation
 *       - aaruf_ecc_cd_is_suffix_correct_mode2(): CD Mode 2 Form 1 ECC validation
 *       - aaruf_edc_cd_compute(): EDC calculation and validation for all CD modes
 *       - aaruf_close(): Serializes prefix/suffix/subheader metadata to image file
 *
 * @note **Buffer Management**: All dynamically allocated buffers (prefix, suffix, subheader, subchannel)
 *       are automatically freed during aaruf_close(). Applications should not attempt to access
 *       these buffers directly or free them manually.
 *
 * @note **Metadata Persistence**: Prefix, suffix, and subheader data captured by this function
 *       is serialized to the image file during aaruf_close() as separate metadata blocks with
 *       corresponding mini-DDT tables for efficient access during reading.
 *
 * @note **Tag Format Conversion**: For block media, automatic conversion between Sony, Profile,
 *       and Priam tag formats ensures compatibility regardless of source format. Conversion
 *       preserves all semantic information while adapting to target media type requirements.
 *
 * @warning **Rewind Detection**: Writing sectors out of strictly increasing order triggers rewind
 *          detection, permanently disabling hash calculations for the session. This prevents
 *          corrupted streaming digests but means hash values will be unavailable if non-sequential
 *          writing occurs. Plan sector writing order carefully if digest calculation is required.
 *
 * @warning **Memory Growth**: Prefix and suffix buffers grow dynamically and can consume significant
 *          memory for images with many non-standard sectors. Monitor memory usage when processing
 *          damaged or non-compliant optical media.
 *
 * @warning **Media Type Constraints**: This function only supports OpticalDisc and specific BlockMedia
 *          types. Other media types will return AARUF_ERROR_INCORRECT_MEDIA_TYPE. Use aaruf_write_sector()
 *          directly for unsupported media types.
 *
 * @see aaruf_write_sector() for user data writing and deduplication
 * @see aaruf_read_sector_long() for corresponding long sector reading functionality
 * @see aaruf_close() for metadata serialization and cleanup
 */
int32_t aaruf_write_sector_long(void *context, uint64_t sector_address, bool negative, const uint8_t *data,
                                uint8_t sector_status, uint32_t length)
{
    TRACE("Entering aaruf_write_sector_long(%p, %" PRIu64 ", %d, %p, %u, %u)", context, sector_address, negative, data,
          sector_status, length);

    // Check context is correct AaruFormat context
    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformatContext *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // Check we are writing
    if(!ctx->isWriting)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_write_sector() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    if(negative && sector_address > ctx->userDataDdtHeader.negative - 1)
    {
        FATAL("Sector address out of bounds");

        TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_SECTOR_OUT_OF_BOUNDS");
        return AARUF_ERROR_SECTOR_OUT_OF_BOUNDS;
    }

    if(!negative && sector_address > ctx->imageInfo.Sectors + ctx->userDataDdtHeader.overflow - 1)
    {
        FATAL("Sector address out of bounds");

        TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_SECTOR_OUT_OF_BOUNDS");
        return AARUF_ERROR_SECTOR_OUT_OF_BOUNDS;
    }

    switch(ctx->imageInfo.XmlMediaType)
    {
        case OpticalDisc:
            TrackEntry track = {0};

            for(int i = 0; i < ctx->tracksHeader.entries; i++)
                if(sector_address >= ctx->trackEntries[i].start && sector_address <= ctx->trackEntries[i].end)
                {
                    track = ctx->trackEntries[i];
                    break;
                }

            if(track.sequence == 0 && track.start == 0 && track.end == 0) track.type = Data;

            if(length != 2352)
            {
                FATAL("Incorrect sector size");
                TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_INCORRECT_DATA_SIZE");
                return AARUF_ERROR_INCORRECT_DATA_SIZE;
            }

            ctx->writingLong = true;

            if(!ctx->rewinded)
            {
                if(sector_address <= ctx->last_written_block)
                {
                    TRACE("Rewinded");
                    ctx->rewinded = true;

                    // Disable MD5 calculation
                    if(ctx->calculating_md5) ctx->calculating_md5 = false;
                    // Disable SHA1 calculation
                    if(ctx->calculating_sha1) ctx->calculating_sha1 = false;
                    // Disable SHA256 calculation
                    if(ctx->calculating_sha256) ctx->calculating_sha256 = false;
                    // Disable SpamSum calculation
                    if(ctx->calculating_spamsum) ctx->calculating_spamsum = false;
                    // Disable BLAKE3 calculation
                    if(ctx->calculating_blake3) ctx->calculating_blake3 = false;
                }
                else
                    ctx->last_written_block = sector_address;
            }

            // Calculate MD5 on-the-fly if requested and sector is within user sectors (not negative or overflow)
            if(ctx->calculating_md5 && !negative && sector_address <= ctx->imageInfo.Sectors)
                aaruf_md5_update(&ctx->md5_context, data, length);
            // Calculate SHA1 on-the-fly if requested and sector is within user sectors (not negative or overflow)
            if(ctx->calculating_sha1 && !negative && sector_address <= ctx->imageInfo.Sectors)
                aaruf_sha1_update(&ctx->sha1_context, data, length);
            // Calculate SHA256 on-the-fly if requested and sector is within user sectors (not negative or overflow)
            if(ctx->calculating_sha256 && !negative && sector_address <= ctx->imageInfo.Sectors)
                aaruf_sha256_update(&ctx->sha256_context, data, length);
            // Calculate SpamSum on-the-fly if requested and sector is within user sectors (not negative or overflow)
            if(ctx->calculating_sha256 && !negative && sector_address <= ctx->imageInfo.Sectors)
                aaruf_spamsum_update(ctx->spamsum_context, data, length);
            // Calculate BLAKE3 on-the-fly if requested and sector is within user sectors (not negative or overflow)
            if(ctx->calculating_blake3 && !negative && sector_address <= ctx->imageInfo.Sectors)
                blake3_hasher_update(ctx->blake3_context, data, length);

            bool prefix_correct;

            uint64_t corrected_sector_address = sector_address;

            // Calculate positive or negative sector
            if(negative)
                corrected_sector_address -= ctx->userDataDdtHeader.negative;
            else
                corrected_sector_address += ctx->userDataDdtHeader.negative;

            // Split raw cd sector data in prefix (sync, header), user data and suffix (edc, ecc p, ecc q)
            switch(track.type)
            {
                case Audio:
                case Data:
                    return aaruf_write_sector(context, sector_address, negative, data, sector_status, length);
                case CdMode1:

                    // If we do not have a DDT V2 for sector prefix, create one
                    if(ctx->sectorPrefixDdtMini == NULL)
                    {
                        ctx->sectorPrefixDdtMini =
                            calloc(1, sizeof(uint16_t) * (ctx->userDataDdtHeader.negative + ctx->imageInfo.Sectors +
                                                          ctx->userDataDdtHeader.overflow));

                        if(ctx->sectorPrefixDdtMini == NULL)
                        {
                            FATAL("Could not allocate memory for CD sector prefix DDT");

                            TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                        }
                    }

                    // If we do not have a DDT V2 for sector suffix, create one
                    if(ctx->sectorSuffixDdtMini == NULL)
                    {
                        ctx->sectorSuffixDdtMini =
                            calloc(1, sizeof(uint16_t) * (ctx->userDataDdtHeader.negative + ctx->imageInfo.Sectors +
                                                          ctx->userDataDdtHeader.overflow));

                        if(ctx->sectorSuffixDdtMini == NULL)
                        {
                            FATAL("Could not allocate memory for CD sector prefix DDT");

                            TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                        }
                    }

                    if(ctx->sector_prefix == NULL)
                    {
                        ctx->sector_prefix_length = 16 * (ctx->userDataDdtHeader.negative + ctx->imageInfo.Sectors +
                                                          ctx->userDataDdtHeader.overflow);
                        ctx->sector_prefix        = malloc(ctx->sector_prefix_length);

                        if(ctx->sector_prefix == NULL)
                        {
                            FATAL("Could not allocate memory for CD sector prefix buffer");

                            TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                        }
                    }

                    if(ctx->sector_suffix == NULL)
                    {
                        ctx->sector_suffix_length = 288 * (ctx->userDataDdtHeader.negative + ctx->imageInfo.Sectors +
                                                           ctx->userDataDdtHeader.overflow);
                        ctx->sector_suffix        = malloc(ctx->sector_suffix_length);

                        if(ctx->sector_suffix == NULL)
                        {
                            FATAL("Could not allocate memory for CD sector suffix buffer");

                            TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                        }
                    }

                    bool empty = true;

                    for(int i = 0; i < length; i++)
                        if(data[i] != 0)
                        {
                            empty = false;
                            break;
                        }

                    if(empty)
                    {
                        ctx->sectorPrefixDdtMini[corrected_sector_address] = SectorStatusNotDumped;
                        ctx->sectorSuffixDdtMini[corrected_sector_address] = SectorStatusNotDumped;
                        return aaruf_write_sector(context, sector_address, negative, data + 16, SectorStatusNotDumped,
                                                  2048);
                    }

                    prefix_correct = true;

                    if(data[0x00] != 0x00 || data[0x01] != 0xFF || data[0x02] != 0xFF || data[0x03] != 0xFF ||
                       data[0x04] != 0xFF || data[0x05] != 0xFF || data[0x06] != 0xFF || data[0x07] != 0xFF ||
                       data[0x08] != 0xFF || data[0x09] != 0xFF || data[0x0A] != 0xFF || data[0x0B] != 0x00 ||
                       data[0x0F] != 0x01)
                        prefix_correct = false;

                    if(prefix_correct)
                    {
                        const int minute     = (data[0x0C] >> 4) * 10 + (data[0x0C] & 0x0F);
                        const int second     = (data[0x0D] >> 4) * 10 + (data[0x0D] & 0x0F);
                        const int frame      = (data[0x0E] >> 4) * 10 + (data[0x0E] & 0x0F);
                        const int stored_lba = minute * 60 * 75 + second * 75 + frame - 150;
                        prefix_correct       = stored_lba == sector_address;
                    }

                    if(prefix_correct)
                        ctx->sectorPrefixDdtMini[corrected_sector_address] = SectorStatusMode1Correct << 12;
                    else
                    {
                        // Copy CD prefix from data buffer to prefix buffer
                        memcpy(ctx->sector_prefix + ctx->sector_prefix_offset, data, 16);
                        ctx->sectorPrefixDdtMini[corrected_sector_address] = (uint16_t)(ctx->sector_prefix_offset / 16);
                        ctx->sectorPrefixDdtMini[corrected_sector_address] |= SectorStatusErrored << 12;
                        ctx->sector_prefix_offset += 16;

                        // Grow prefix buffer if needed
                        if(ctx->sector_prefix_offset >= ctx->sector_prefix_length)
                        {
                            ctx->sector_prefix_length *= 2;
                            ctx->sector_prefix = realloc(ctx->sector_prefix, ctx->sector_prefix_length);

                            if(ctx->sector_prefix == NULL)
                            {
                                FATAL("Could not allocate memory for CD sector prefix buffer");

                                TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                                return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                            }
                        }
                    }

                    const bool suffix_correct = aaruf_ecc_cd_is_suffix_correct(context, data);

                    if(suffix_correct)
                        ctx->sectorSuffixDdtMini[corrected_sector_address] = SectorStatusMode1Correct << 12;
                    else
                    {
                        // Copy CD suffix from data buffer to suffix buffer
                        memcpy(ctx->sector_suffix + ctx->sector_suffix_offset, data + 2064, 288);
                        ctx->sectorSuffixDdtMini[corrected_sector_address] =
                            (uint16_t)(ctx->sector_suffix_offset / 288);
                        ctx->sectorSuffixDdtMini[corrected_sector_address] |= SectorStatusErrored << 12;
                        ctx->sector_suffix_offset += 288;

                        // Grow suffix buffer if needed
                        if(ctx->sector_suffix_offset >= ctx->sector_suffix_length)
                        {
                            ctx->sector_suffix_length *= 2;
                            ctx->sector_suffix = realloc(ctx->sector_suffix, ctx->sector_suffix_length);

                            if(ctx->sector_suffix == NULL)
                            {
                                FATAL("Could not allocate memory for CD sector suffix buffer");

                                TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                                return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                            }
                        }
                    }

                    return aaruf_write_sector(context, sector_address, negative, data + 16, SectorStatusMode1Correct,
                                              2048);
                case CdMode2Form1:
                case CdMode2Form2:
                case CdMode2Formless:
                    // If we do not have a DDT V2 for sector prefix, create one
                    if(ctx->sectorPrefixDdtMini == NULL)
                    {
                        ctx->sectorPrefixDdtMini =
                            calloc(1, sizeof(uint16_t) * (ctx->userDataDdtHeader.negative + ctx->imageInfo.Sectors +
                                                          ctx->userDataDdtHeader.overflow));

                        if(ctx->sectorPrefixDdtMini == NULL)
                        {
                            FATAL("Could not allocate memory for CD sector prefix DDT");

                            TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                        }
                    }

                    // If we do not have a DDT V2 for sector suffix, create one
                    if(ctx->sectorSuffixDdtMini == NULL)
                    {
                        ctx->sectorSuffixDdtMini =
                            calloc(1, sizeof(uint16_t) * (ctx->userDataDdtHeader.negative + ctx->imageInfo.Sectors +
                                                          ctx->userDataDdtHeader.overflow));

                        if(ctx->sectorSuffixDdtMini == NULL)
                        {
                            FATAL("Could not allocate memory for CD sector prefix DDT");

                            TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                        }
                    }

                    if(ctx->sector_prefix == NULL)
                    {
                        ctx->sector_prefix_length = 16 * (ctx->userDataDdtHeader.negative + ctx->imageInfo.Sectors +
                                                          ctx->userDataDdtHeader.overflow);
                        ctx->sector_prefix        = malloc(ctx->sector_prefix_length);

                        if(ctx->sector_prefix == NULL)
                        {
                            FATAL("Could not allocate memory for CD sector prefix buffer");

                            TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                        }
                    }

                    if(ctx->sector_suffix == NULL)
                    {
                        ctx->sector_suffix_length = 288 * (ctx->userDataDdtHeader.negative + ctx->imageInfo.Sectors +
                                                           ctx->userDataDdtHeader.overflow);
                        ctx->sector_suffix        = malloc(ctx->sector_suffix_length);

                        if(ctx->sector_suffix == NULL)
                        {
                            FATAL("Could not allocate memory for CD sector suffix buffer");

                            TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                        }
                    }

                    empty = true;

                    for(int i = 0; i < length; i++)
                        if(data[i] != 0)
                        {
                            empty = false;
                            break;
                        }

                    if(empty)
                    {
                        ctx->sectorPrefixDdtMini[corrected_sector_address] = SectorStatusNotDumped;
                        ctx->sectorSuffixDdtMini[corrected_sector_address] = SectorStatusNotDumped;
                        return aaruf_write_sector(context, sector_address, negative, data + 16, SectorStatusNotDumped,
                                                  2328);
                    }

                    const bool form2 = (data[18] & 0x20) == 0x20 || (data[22] & 0x20) == 0x20;

                    prefix_correct = true;

                    if(data[0x00] != 0x00 || data[0x01] != 0xFF || data[0x02] != 0xFF || data[0x03] != 0xFF ||
                       data[0x04] != 0xFF || data[0x05] != 0xFF || data[0x06] != 0xFF || data[0x07] != 0xFF ||
                       data[0x08] != 0xFF || data[0x09] != 0xFF || data[0x0A] != 0xFF || data[0x0B] != 0x00 ||
                       data[0x0F] != 0x02)
                        prefix_correct = false;

                    if(prefix_correct)
                    {
                        const int minute     = (data[0x0C] >> 4) * 10 + (data[0x0C] & 0x0F);
                        const int second     = (data[0x0D] >> 4) * 10 + (data[0x0D] & 0x0F);
                        const int frame      = (data[0x0E] >> 4) * 10 + (data[0x0E] & 0x0F);
                        const int stored_lba = minute * 60 * 75 + second * 75 + frame - 150;
                        prefix_correct       = stored_lba == sector_address;
                    }

                    if(prefix_correct)
                        ctx->sectorPrefixDdtMini[corrected_sector_address] =
                            (form2 ? SectorStatusMode2Form2Ok : SectorStatusMode2Form1Ok) << 12;
                    else
                    {
                        // Copy CD prefix from data buffer to prefix buffer
                        memcpy(ctx->sector_prefix + ctx->sector_prefix_offset, data, 16);
                        ctx->sectorPrefixDdtMini[corrected_sector_address] = (uint16_t)(ctx->sector_prefix_offset / 16);
                        ctx->sectorPrefixDdtMini[corrected_sector_address] |= SectorStatusErrored << 12;
                        ctx->sector_prefix_offset += 16;

                        // Grow prefix buffer if needed
                        if(ctx->sector_prefix_offset >= ctx->sector_prefix_length)
                        {
                            ctx->sector_prefix_length *= 2;
                            ctx->sector_prefix = realloc(ctx->sector_prefix, ctx->sector_prefix_length);

                            if(ctx->sector_prefix == NULL)
                            {
                                FATAL("Could not allocate memory for CD sector prefix buffer");

                                TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                                return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                            }
                        }
                    }

                    if(ctx->mode2_subheaders == NULL)
                    {
                        ctx->mode2_subheaders =
                            calloc(1, 8 * (ctx->userDataDdtHeader.negative + ctx->imageInfo.Sectors +
                                           ctx->userDataDdtHeader.overflow));

                        if(ctx->mode2_subheaders == NULL)
                        {
                            FATAL("Could not allocate memory for CD mode 2 subheader buffer");

                            TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                        }
                    }

                    if(form2)
                    {
                        const uint32_t computed_edc = aaruf_edc_cd_compute(context, 0, data, 0x91C, 0x10);
                        const uint32_t edc          = *(data + 0x92C);
                        const bool     correct_edc  = computed_edc == edc;

                        if(correct_edc)
                            ctx->sectorSuffixDdtMini[corrected_sector_address] = SectorStatusMode2Form2Ok << 12;
                        else if(edc == 0)
                            ctx->sectorSuffixDdtMini[corrected_sector_address] = SectorStatusMode2Form2NoCrc << 12;
                        else
                        {
                            // Copy CD suffix from data buffer to suffix buffer
                            memcpy(ctx->sector_suffix + ctx->sector_suffix_offset, data + 2348, 4);
                            ctx->sectorSuffixDdtMini[corrected_sector_address] =
                                (uint16_t)(ctx->sector_suffix_offset / 288);
                            ctx->sectorSuffixDdtMini[corrected_sector_address] |= SectorStatusErrored << 12;
                            ctx->sector_suffix_offset += 288;

                            // Grow suffix buffer if needed
                            if(ctx->sector_suffix_offset >= ctx->sector_suffix_length)
                            {
                                ctx->sector_suffix_length *= 2;
                                ctx->sector_suffix = realloc(ctx->sector_suffix, ctx->sector_suffix_length);

                                if(ctx->sector_suffix == NULL)
                                {
                                    FATAL("Could not allocate memory for CD sector suffix buffer");

                                    TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                                    return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                                }
                            }
                        }

                        // Copy subheader from data buffer to subheader buffer
                        memcpy(ctx->mode2_subheaders + corrected_sector_address * 8, data + 0x10, 8);
                        return aaruf_write_sector(context, sector_address, negative, data + 24,
                                                  edc == 0      ? SectorStatusMode2Form2NoCrc
                                                  : correct_edc ? SectorStatusMode2Form2Ok
                                                                : SectorStatusErrored,
                                                  2324);
                    }

                    const bool     correct_ecc  = aaruf_ecc_cd_is_suffix_correct_mode2(context, data);
                    const uint32_t computed_edc = aaruf_edc_cd_compute(context, 0, data, 0x808, 0x10);
                    const uint32_t edc          = *(data + 0x818);
                    const bool     correct_edc  = computed_edc == edc;

                    if(correct_ecc && correct_edc)
                        ctx->sectorSuffixDdtMini[corrected_sector_address] = SectorStatusMode2Form1Ok << 12;
                    else
                    {
                        // Copy CD suffix from data buffer to suffix buffer
                        memcpy(ctx->sector_suffix + ctx->sector_suffix_offset, data + 2072, 280);
                        ctx->sectorSuffixDdtMini[corrected_sector_address] =
                            (uint16_t)(ctx->sector_suffix_offset / 288);
                        ctx->sectorSuffixDdtMini[corrected_sector_address] |= SectorStatusErrored << 12;
                        ctx->sector_suffix_offset += 288;

                        // Grow suffix buffer if needed
                        if(ctx->sector_suffix_offset >= ctx->sector_suffix_length)
                        {
                            ctx->sector_suffix_length *= 2;
                            ctx->sector_suffix = realloc(ctx->sector_suffix, ctx->sector_suffix_length);

                            if(ctx->sector_suffix == NULL)
                            {
                                FATAL("Could not allocate memory for CD sector suffix buffer");

                                TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                                return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                            }
                        }
                    }

                    // Copy subheader from data buffer to subheader buffer
                    memcpy(ctx->mode2_subheaders + corrected_sector_address * 8, data + 0x10, 8);
                    return aaruf_write_sector(
                        context, sector_address, negative, data + 24,
                        correct_edc && correct_ecc ? SectorStatusMode2Form1Ok : SectorStatusErrored, 2048);
            }

            break;
        case BlockMedia:
            switch(ctx->imageInfo.MediaType)
            {
                case AppleFileWare:
                case AppleProfile:
                case AppleSonyDS:
                case AppleSonySS:
                case AppleWidget:
                case PriamDataTower:
                    uint8_t *newTag;
                    int      newTagSize = 0;

                    switch(length - 512)
                    {
                        // Sony tag
                        case 12:
                            const sony_tag decoded_sony_tag = bytes_to_sony_tag(data + 512);

                            if(ctx->imageInfo.MediaType == AppleProfile || ctx->imageInfo.MediaType == AppleFileWare)
                            {
                                const profile_tag decoded_profile_tag = sony_tag_to_profile(decoded_sony_tag);
                                newTag                                = profile_tag_to_bytes(decoded_profile_tag);
                                newTagSize                            = 20;
                            }
                            else if(ctx->imageInfo.MediaType == PriamDataTower)
                            {
                                const priam_tag decoded_priam_tag = sony_tag_to_priam(decoded_sony_tag);
                                newTag                            = priam_tag_to_bytes(decoded_priam_tag);
                                newTagSize                        = 24;
                            }
                            else if(ctx->imageInfo.MediaType == AppleSonyDS || ctx->imageInfo.MediaType == AppleSonySS)
                            {
                                newTag = malloc(12);
                                memcpy(newTag, data + 512, 12);
                                newTagSize = 12;
                            }
                            break;
                        // Profile tag
                        case 20:
                            const profile_tag decoded_profile_tag = bytes_to_profile_tag(data + 512);

                            if(ctx->imageInfo.MediaType == AppleProfile || ctx->imageInfo.MediaType == AppleFileWare)
                            {
                                newTag = malloc(20);
                                memcpy(newTag, data + 512, 20);
                                newTagSize = 20;
                            }
                            else if(ctx->imageInfo.MediaType == PriamDataTower)
                            {
                                const priam_tag decoded_priam_tag = profile_tag_to_priam(decoded_profile_tag);
                                newTag                            = priam_tag_to_bytes(decoded_priam_tag);
                                newTagSize                        = 24;
                            }
                            else if(ctx->imageInfo.MediaType == AppleSonyDS || ctx->imageInfo.MediaType == AppleSonySS)
                            {
                                const sony_tag decoded_sony_tag = profile_tag_to_sony(decoded_profile_tag);
                                newTag                          = sony_tag_to_bytes(decoded_sony_tag);
                                newTagSize                      = 12;
                            }
                            break;
                        // Priam tag
                        case 24:
                            const priam_tag decoded_priam_tag = bytes_to_priam_tag(data + 512);
                            if(ctx->imageInfo.MediaType == AppleProfile || ctx->imageInfo.MediaType == AppleFileWare)
                            {
                                const profile_tag decoded_profile_tag = priam_tag_to_profile(decoded_priam_tag);
                                newTag                                = profile_tag_to_bytes(decoded_profile_tag);
                                newTagSize                            = 20;
                            }
                            else if(ctx->imageInfo.MediaType == PriamDataTower)
                            {
                                newTag = malloc(24);
                                memcpy(newTag, data + 512, 24);
                                newTagSize = 24;
                            }
                            else if(ctx->imageInfo.MediaType == AppleSonyDS || ctx->imageInfo.MediaType == AppleSonySS)
                            {
                                const sony_tag decoded_sony_tag = priam_tag_to_sony(decoded_priam_tag);
                                newTag                          = sony_tag_to_bytes(decoded_sony_tag);
                                newTagSize                      = 12;
                            }
                            break;
                        case 0:
                            newTagSize = 0;
                            break;
                        default:
                            FATAL("Incorrect sector size");
                            TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_INCORRECT_DATA_SIZE");
                            return AARUF_ERROR_INCORRECT_DATA_SIZE;
                    }

                    if(newTagSize == 0)
                        return aaruf_write_sector(context, sector_address, negative, data, sector_status, 512);

                    if(ctx->sector_subchannel == NULL)
                    {
                        ctx->sector_subchannel =
                            calloc(1, newTagSize * (ctx->imageInfo.Sectors + ctx->userDataDdtHeader.overflow));

                        if(ctx->sector_subchannel == NULL)
                        {
                            FATAL("Could not allocate memory for sector subchannel DDT");

                            free(newTag);

                            TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                        }
                    }

                    memcpy(ctx->sector_subchannel + sector_address * newTagSize, newTag, newTagSize);
                    free(newTag);

                    return aaruf_write_sector(context, sector_address, negative, data, sector_status, 512);
                default:
                    return AARUF_ERROR_INCORRECT_MEDIA_TYPE;
            }
        default:
            TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_INCORRECT_MEDIA_TYPE");
            return AARUF_ERROR_INCORRECT_MEDIA_TYPE;
    }

    // Fallback return when media type branch does not produce a value (satisfy non-void contract)
    return AARUF_ERROR_INCORRECT_MEDIA_TYPE;
}

int32_t aaruf_close_current_block(aaruformatContext *ctx)
{
    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC) return AARUF_ERROR_NOT_AARUFORMAT;

    // Check we are writing
    if(!ctx->isWriting) return AARUF_READ_ONLY;

    ctx->currentBlockHeader.length = ctx->currentBlockOffset * ctx->currentBlockHeader.sectorSize;

    TRACE("Initializing CRC64 context");
    ctx->crc64Context = aaruf_crc64_init();
    TRACE("Updating CRC64");
    aaruf_crc64_update(ctx->crc64Context, ctx->writingBuffer, ctx->currentBlockHeader.length);
    aaruf_crc64_final(ctx->crc64Context, &ctx->currentBlockHeader.crc64);

    switch(ctx->currentBlockHeader.compression)
    {
        case None:
            ctx->currentBlockHeader.cmpCrc64  = ctx->currentBlockHeader.crc64;
            ctx->currentBlockHeader.cmpLength = ctx->currentBlockHeader.length;
    }

    // Add to index
    TRACE("Adding block to index");
    IndexEntry index_entry;
    index_entry.blockType = DataBlock;
    index_entry.dataType  = UserData;
    index_entry.offset    = ctx->nextBlockPosition;

    utarray_push_back(ctx->indexEntries, &index_entry);
    TRACE("Block added to index at offset %" PRIu64, index_entry.offset);

    // Write block header to file

    // Move to expected block position
    fseek(ctx->imageStream, ctx->nextBlockPosition, SEEK_SET);

    // Write block header
    if(fwrite(&ctx->currentBlockHeader, sizeof(BlockHeader), 1, ctx->imageStream) != 1)
        return AARUF_ERROR_CANNOT_WRITE_BLOCK_HEADER;

    // Write block data
    if(fwrite(ctx->writingBuffer, ctx->currentBlockHeader.length, 1, ctx->imageStream) != 1)
        return AARUF_ERROR_CANNOT_WRITE_BLOCK_DATA;

    // Update nextBlockPosition to point to the next available aligned position
    uint64_t block_total_size = sizeof(BlockHeader) + ctx->currentBlockHeader.cmpLength;
    uint64_t alignment_mask   = (1ULL << ctx->userDataDdtHeader.blockAlignmentShift) - 1;
    ctx->nextBlockPosition    = ctx->nextBlockPosition + block_total_size + alignment_mask & ~alignment_mask;
    TRACE("Updated nextBlockPosition to %" PRIu64, ctx->nextBlockPosition);

    // Clear values
    free(ctx->writingBuffer);
    ctx->writingBuffer      = NULL;
    ctx->currentBlockOffset = 0;
    memset(&ctx->currentBlockHeader, 0, sizeof(BlockHeader));
    aaruf_crc64_free(ctx->crc64Context);
    ctx->writingBufferPosition = 0;

    return AARUF_STATUS_OK;
}