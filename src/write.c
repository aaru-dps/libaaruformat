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
 * @brief Writes a sector to the AaruFormat image.
 *
 * Writes the given data to the specified sector address in the image, with the given status and length.
 *
 * @param context Pointer to the aaruformat context.
 * @param sector_address Logical sector address to write.
 * @param data Pointer to the data buffer to write.
 * @param sector_status Status of the sector to write.
 * @param length Length of the data buffer.
 * @return AARUF_STATUS_OK on success, or an error code on failure.
 */
int32_t aaruf_write_sector(void *context, uint64_t sector_address, const uint8_t *data, uint8_t sector_status,
                           uint32_t length)
{
    TRACE("Entering aaruf_write_sector(%p, %" PRIu64 ", %p, %u, %u)", context, sector_address, data, sector_status,
          length);

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

    // TODO: Check not trying to write beyond media limits

    // TODO: Check rewinded for disabling checksums

    // TODO: If optical disc check track

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

    set_ddt_entry_v2(ctx, sector_address, ctx->currentBlockOffset, ctx->nextBlockPosition, sector_status);

    // No block set
    if(ctx->writingBufferPosition == 0)
    {
        TRACE("Creating new writing block");
        ctx->currentBlockHeader.identifier  = DataBlock;
        ctx->currentBlockHeader.type        = UserData;
        ctx->currentBlockHeader.compression = None;  // TODO: Compression
        ctx->currentBlockHeader.sectorSize  = length;

        // TODO: Optical discs
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
    ctx->nextBlockPosition    = (ctx->nextBlockPosition + block_total_size + alignment_mask) & ~alignment_mask;
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