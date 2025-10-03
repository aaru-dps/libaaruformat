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