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
 * @brief Writes a full ("long") raw sector (2352 bytes) from optical media, splitting and validating structure.
 *
 * This function is specialized for raw CD sector ingestion when the caller provides the complete 2352-byte
 * (or derived) raw sector including synchronization pattern, header (prefix), user data area and suffix
 * (EDC/ECC / parity depending on the mode). It supports:
 *   - Audio (2352 bytes of PCM) and Data raw sectors which are simply forwarded to aaruf_write_sector().
 *   - CD Mode 1 sectors (sync + header + 2048 user bytes + EDC + ECC P + ECC Q).
 *   - CD Mode 2 (Form 1, Form 2 and Formless) sectors, handling sub-headers, EDC, and ECC as appropriate.
 *
 * For each sector, the function:
 *   1. Locates the track definition covering the provided logical sector (LBA) and derives its type.
 *   2. Validates input length (must be exactly 2352 for optical raw sectors at present).
 *   3. Performs rewind detection: if sectors are written out of strictly increasing order, ongoing hash
 *      calculations (MD5, SHA1, SHA256, SpamSum, BLAKE3) are disabled to prevent incorrect streaming digests.
 *   4. Optionally updates hash digests if still enabled and the sector lies within the user range (i.e. not
 *      negative / overflow) – this is performed on the full raw content for long sectors.
 *   5. Splits the sector into prefix, user data, and suffix portions depending on mode and validates:
 *        - Prefix (sync + header) timing/address fields (MM:SS:FF → LBA) and mode byte.
 *        - For Mode 1: checks prefix conformity and ECC / EDC correctness via helper routines.
 *        - For Mode 2: distinguishes Form 1 vs Form 2 (bit flags), validates ECC (Form 1) and EDC (both forms),
 *          and extracts sub-header (8 bytes) for separate storage.
 *   6. Stores anomalous (non-conforming or errored) prefix/suffix fragments into dynamically growing buffers
 *      (sectorPrefixBuffer / sectorSuffixBuffer) and records miniature DDT entries (sectorPrefixDdtMini /
 *      sectorSuffixDdtMini) with offsets and status bits. Correct standard patterns are recorded without
 *      copying (status code only) to save space.
 *   7. Writes only the user data portion (2048 for Mode 1 & Mode 2 Form 1, 2324 for Mode 2 Form 2, 2352 for
 *      audio or for already treated Data) to the standard user data path by delegating to aaruf_write_sector(),
 *      passing an appropriate derived sector status (e.g. SectorStatusMode1Correct, SectorStatusMode2Form1Ok,
 *      SectorStatusErrored, etc.).
 *
 * Deduplication: Long sector handling itself does not directly hash for deduplication; dedupe is applied when
 * aaruf_write_sector() is invoked for the extracted user data segment.
 *
 * Memory allocation strategy:
 *   - Mini DDT arrays (prefix/suffix) are lazily allocated on first need sized for the total addressable sector
 *     span (negative + user + overflow).
 *   - Prefix (16-byte units) and suffix buffers (288-byte units; or smaller copies for Form 2 EDC) grow by
 *     doubling when capacity would be exceeded.
 *   - Mode 2 sub-header storage (8 bytes per sector) is also lazily allocated.
 *
 * Address normalization: Internally a corrected index (corrected_sector_address) is computed by offsetting the
 * raw logical sector number with the negative-region size to provide a linear index across negative, user and
 * overflow regions.
 *
 * Sector status encoding (high nibble of 16-bit mini DDT entries):
 *   - SectorStatusMode1Correct / SectorStatusMode2Form1Ok / SectorStatusMode2Form2Ok / ... mark validated content.
 *   - SectorStatusErrored marks stored anomalous fragments whose data was copied into side buffers.
 *   - SectorStatusNotDumped marks all-zero (empty) raw sectors treated as not dumped.
 *   - Additional specific codes differentiate Mode 2 Form 2 without CRC vs with correct CRC, etc.
 *
 * Rewind side effects: Once a rewind is detected (writing an LBA <= previously written), all on-the-fly hash
 * computations are disabled permanently for the session in order to maintain integrity guarantees of the
 * produced digest values.
 *
 * Limitations / TODO:
 *   - BlockMedia (non-optical) handling is currently unimplemented and returns AARUF_ERROR_INCORRECT_MEDIA_TYPE.
 *   - Only 2352-byte raw sectors are accepted; other raw lengths (e.g. 2448 with subchannel) are not handled here.
 *   - Compression for block writing is not yet implemented (mirrors limitations of aaruf_write_sector()).
 *
 * Thread safety: This function is not thread-safe. The context carries mutable shared buffers and state.
 *
 * Error handling model: On encountering an error (allocation failure, bounds, incorrect size, media type) the
 * function logs a fatal message via FATAL() and returns immediately with an appropriate negative error code.
 * Partial allocations made earlier in the same call are not rolled back beyond what standard free-on-close does.
 *
 * Preconditions:
 *   - context is a valid aaruformatContext with magic == AARU_MAGIC.
 *   - Image opened for writing (isWriting == true).
 *   - sector_address within allowed negative / user / overflow ranges depending on 'negative'.
 *
 * Postconditions on success:
 *   - User data DDT updated (indirectly via aaruf_write_sector()).
 *   - For Mode 1 / Mode 2 sectors: prefix/suffix / sub-header metadata structures updated accordingly.
 *   - User data portion appended (buffered) into current block (or new block created) via aaruf_write_sector().
 *   - Hash contexts updated unless rewind occurred.
 *
 * @param context Pointer to the aaruformat context.
 * @param sector_address Logical Block Address (LBA) for the raw sector (0-based user LBA,
 *        can include negative region when 'negative' is true).
 * @param negative true if sector_address refers to the negative (pre-gap) region; false for user/overflow.
 * @param data Pointer to 2352-byte raw sector buffer (sync+header+userdata+EDC/ECC) or audio PCM.
 * @param sector_status Initial sector status hint provided by caller (may be overridden for derived writing call).
 * @param length Length in bytes of the provided raw buffer. Must be exactly 2352 for optical sectors.
 *
 * @return One of:
 * @retval AARUF_STATUS_OK Sector processed and (user data portion) queued/written successfully.
 * @retval AARUF_ERROR_NOT_AARUFORMAT Invalid or NULL context / magic mismatch.
 * @retval AARUF_READ_ONLY Image opened read-only.
 * @retval AARUF_ERROR_SECTOR_OUT_OF_BOUNDS sector_address outside negative/user/overflow bounds.
 * @retval AARUF_ERROR_INCORRECT_DATA_SIZE length != 2352 for optical disc long sector ingestion.
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY Failed to allocate or grow any required buffer (mini DDT,
 *         prefix, suffix, sub-headers).
 * @retval AARUF_ERROR_INCORRECT_MEDIA_TYPE Media type unsupported for long sector writes (non OpticalDisc).
 * @retval AARUF_ERROR_CANNOT_SET_DDT_ENTRY Propagated from underlying aaruf_write_sector() when DDT update fails.
 * @retval AARUF_ERROR_CANNOT_WRITE_BLOCK_HEADER Propagated from block flush inside aaruf_write_sector().
 * @retval AARUF_ERROR_CANNOT_WRITE_BLOCK_DATA Propagated from block flush inside aaruf_write_sector().
 *
 * @note The function returns immediately after delegating to aaruf_write_sector() for user data; any status
 *       described there may propagate. That function performs deduplication and block finalization.
 *
 * @warning Do not mix calls to aaruf_write_sector() and aaruf_write_sector_long() with out-of-order addresses
 *          if you rely on calculated digests; rewind will disable digest updates irrevocably.
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

                    if(ctx->sectorPrefixBuffer == NULL)
                    {
                        ctx->sectorPrefixBufferLength = 16 * (ctx->userDataDdtHeader.negative + ctx->imageInfo.Sectors +
                                                              ctx->userDataDdtHeader.overflow);
                        ctx->sectorPrefixBuffer       = malloc(ctx->sectorPrefixBufferLength);

                        if(ctx->sectorPrefixBuffer == NULL)
                        {
                            FATAL("Could not allocate memory for CD sector prefix buffer");

                            TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                        }
                    }

                    if(ctx->sectorSuffixBuffer == NULL)
                    {
                        ctx->sectorSuffixBufferLength =
                            288 * (ctx->userDataDdtHeader.negative + ctx->imageInfo.Sectors +
                                   ctx->userDataDdtHeader.overflow);
                        ctx->sectorSuffixBuffer = malloc(ctx->sectorSuffixBufferLength);

                        if(ctx->sectorSuffixBuffer == NULL)
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
                        memcpy(ctx->sectorPrefixBuffer + ctx->sectorPrefixBufferOffset, data, 16);
                        ctx->sectorPrefixDdtMini[corrected_sector_address] =
                            (uint16_t)(ctx->sectorPrefixBufferOffset / 16);
                        ctx->sectorPrefixDdtMini[corrected_sector_address] |= SectorStatusErrored << 12;
                        ctx->sectorPrefixBufferOffset += 16;

                        // Grow prefix buffer if needed
                        if(ctx->sectorPrefixBufferOffset >= ctx->sectorPrefixBufferLength)
                        {
                            ctx->sectorPrefixBufferLength *= 2;
                            ctx->sectorPrefixBuffer = realloc(ctx->sectorPrefixBuffer, ctx->sectorPrefixBufferLength);

                            if(ctx->sectorPrefixBuffer == NULL)
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
                        memcpy(ctx->sectorSuffixBuffer + ctx->sectorSuffixBufferOffset, data + 2064, 288);
                        ctx->sectorSuffixDdtMini[corrected_sector_address] =
                            (uint16_t)(ctx->sectorSuffixBufferOffset / 288);
                        ctx->sectorSuffixDdtMini[corrected_sector_address] |= SectorStatusErrored << 12;
                        ctx->sectorSuffixBufferOffset += 288;

                        // Grow suffix buffer if needed
                        if(ctx->sectorSuffixBufferOffset >= ctx->sectorSuffixBufferLength)
                        {
                            ctx->sectorSuffixBufferLength *= 2;
                            ctx->sectorSuffixBuffer = realloc(ctx->sectorSuffixBuffer, ctx->sectorSuffixBufferLength);

                            if(ctx->sectorSuffixBuffer == NULL)
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

                    if(ctx->sectorPrefixBuffer == NULL)
                    {
                        ctx->sectorPrefixBufferLength = 16 * (ctx->userDataDdtHeader.negative + ctx->imageInfo.Sectors +
                                                              ctx->userDataDdtHeader.overflow);
                        ctx->sectorPrefixBuffer       = malloc(ctx->sectorPrefixBufferLength);

                        if(ctx->sectorPrefixBuffer == NULL)
                        {
                            FATAL("Could not allocate memory for CD sector prefix buffer");

                            TRACE("Exiting aaruf_write_sector() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
                            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                        }
                    }

                    if(ctx->sectorSuffixBuffer == NULL)
                    {
                        ctx->sectorSuffixBufferLength =
                            288 * (ctx->userDataDdtHeader.negative + ctx->imageInfo.Sectors +
                                   ctx->userDataDdtHeader.overflow);
                        ctx->sectorSuffixBuffer = malloc(ctx->sectorSuffixBufferLength);

                        if(ctx->sectorSuffixBuffer == NULL)
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
                        memcpy(ctx->sectorPrefixBuffer + ctx->sectorPrefixBufferOffset, data, 16);
                        ctx->sectorPrefixDdtMini[corrected_sector_address] =
                            (uint16_t)(ctx->sectorPrefixBufferOffset / 16);
                        ctx->sectorPrefixDdtMini[corrected_sector_address] |= SectorStatusErrored << 12;
                        ctx->sectorPrefixBufferOffset += 16;

                        // Grow prefix buffer if needed
                        if(ctx->sectorPrefixBufferOffset >= ctx->sectorPrefixBufferLength)
                        {
                            ctx->sectorPrefixBufferLength *= 2;
                            ctx->sectorPrefixBuffer = realloc(ctx->sectorPrefixBuffer, ctx->sectorPrefixBufferLength);

                            if(ctx->sectorPrefixBuffer == NULL)
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
                            memcpy(ctx->sectorSuffixBuffer + ctx->sectorSuffixBufferOffset, data + 2348, 4);
                            ctx->sectorSuffixDdtMini[corrected_sector_address] =
                                (uint16_t)(ctx->sectorSuffixBufferOffset / 288);
                            ctx->sectorSuffixDdtMini[corrected_sector_address] |= SectorStatusErrored << 12;
                            ctx->sectorSuffixBufferOffset += 288;

                            // Grow suffix buffer if needed
                            if(ctx->sectorSuffixBufferOffset >= ctx->sectorSuffixBufferLength)
                            {
                                ctx->sectorSuffixBufferLength *= 2;
                                ctx->sectorSuffixBuffer =
                                    realloc(ctx->sectorSuffixBuffer, ctx->sectorSuffixBufferLength);

                                if(ctx->sectorSuffixBuffer == NULL)
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
                        memcpy(ctx->sectorSuffixBuffer + ctx->sectorSuffixBufferOffset, data + 2072, 280);
                        ctx->sectorSuffixDdtMini[corrected_sector_address] =
                            (uint16_t)(ctx->sectorSuffixBufferOffset / 288);
                        ctx->sectorSuffixDdtMini[corrected_sector_address] |= SectorStatusErrored << 12;
                        ctx->sectorSuffixBufferOffset += 288;

                        // Grow suffix buffer if needed
                        if(ctx->sectorSuffixBufferOffset >= ctx->sectorSuffixBufferLength)
                        {
                            ctx->sectorSuffixBufferLength *= 2;
                            ctx->sectorSuffixBuffer = realloc(ctx->sectorSuffixBuffer, ctx->sectorSuffixBufferLength);

                            if(ctx->sectorSuffixBuffer == NULL)
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
            // TODO: Implement
            break;
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