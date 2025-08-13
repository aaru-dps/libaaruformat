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

int32_t aaruf_write_sector(void *context, uint64_t sectorAddress, uint8_t *data, uint8_t sectorStatus, uint32_t length)
{
    // Check context is correct AaruFormat context
    if(context == NULL) return AARUF_ERROR_NOT_AARUFORMAT;

    aaruformatContext *ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC) return AARUF_ERROR_NOT_AARUFORMAT;

    // Check we are writing
    if(!ctx->isWriting) return AARUF_READ_ONLY;

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
        int error = aaruf_close_current_block(ctx);

        if(error != AARUF_STATUS_OK) return error;
    }

    // No block set
    if(ctx->writingBufferPosition == 0)
    {
        ctx->currentBlockHeader.identifier  = DataBlock;
        ctx->currentBlockHeader.type        = UserData;
        ctx->currentBlockHeader.compression = None;  // TODO: Compression
        ctx->currentBlockHeader.sectorSize  = length;

        // TODO: Optical discs

        uint32_t maxBufferSize = (1 << ctx->userDataDdtHeader.dataShift) * ctx->currentBlockHeader.sectorSize;
        ctx->writingBuffer     = (uint8_t *)malloc(maxBufferSize);
        if(ctx->writingBuffer == NULL) return AARUF_ERROR_NOT_ENOUGH_MEMORY;

        ctx->crc64Context = aaruf_crc64_init();
    }

    // TODO: DDT entry

    memcpy(ctx->writingBuffer, data, length);
    ctx->writingBufferPosition += length;
    aaruf_crc64_update(ctx->crc64Context, data, length);
    ctx->currentBlockOffset++;

    return AARUF_STATUS_OK;
}

int32_t aaruf_close_current_block(aaruformatContext *ctx)
{
    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC) return AARUF_ERROR_NOT_AARUFORMAT;

    // Check we are writing
    if(!ctx->isWriting) return AARUF_READ_ONLY;

    ctx->currentBlockHeader.length = ctx->currentBlockOffset * ctx->currentBlockHeader.sectorSize;
    aaruf_crc64_final(ctx->crc64Context, &ctx->currentBlockHeader.crc64);

    switch(ctx->currentBlockHeader.compression)
    {
        case None:
            ctx->currentBlockHeader.cmpCrc64  = ctx->currentBlockHeader.crc64;
            ctx->currentBlockHeader.cmpLength = ctx->currentBlockHeader.length;
    }

    // TODO: Add to index

    // Write block header to file

    // Get file position
    long pos = ftell(ctx->imageStream);

    // Fill file with zeroes until next aligned position according to DDT's block alignment shift
    long next_alignment =
        pos / (1 << ctx->userDataDdtHeader.blockAlignmentShift) * (1 << ctx->userDataDdtHeader.blockAlignmentShift);
    fwrite("\0", 1, next_alignment - pos, ctx->imageStream);

    // Write block header
    if(fwrite(&ctx->currentBlockHeader, sizeof(BlockHeader), 1, ctx->imageStream) != 1)
        return AARUF_ERROR_CANNOT_WRITE_BLOCK_HEADER;

    // Write block data
    if(fwrite(ctx->writingBuffer, ctx->currentBlockHeader.length, 1, ctx->imageStream) != 1)
        return AARUF_ERROR_CANNOT_WRITE_BLOCK_DATA;

    // Clear values
    free(ctx->writingBuffer);
    ctx->writingBuffer      = NULL;
    ctx->currentBlockOffset = 0;
    memset(&ctx->currentBlockHeader, 0, sizeof(BlockHeader));
    aaruf_crc64_free(ctx->crc64Context);
    ctx->writingBufferPosition = 0;

    return AARUF_STATUS_OK;
}