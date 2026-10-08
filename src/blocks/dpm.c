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

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aaruformat.h"
#include "erasure_internal.h"
#include "internal.h"
#include "log.h"

/**
 * @brief Computes the payload length a Data Position Measurement header describes.
 *
 * @param header DPM block header.
 * @return Payload length in bytes.
 */
uint64_t dpm_payload_length(const DpmHeader *header)
{
    return (uint64_t)header->layerEnds * sizeof(uint64_t) + (uint64_t)header->entries * sizeof(DpmEntry) +
           (uint64_t)header->calibrations * sizeof(DpmCalibration);
}

/**
 * @brief Validates a serialized Data Position Measurement block.
 *
 * Checks that the buffer holds a DpmHeader with the correct identifier, that the payload length matches both the
 * header length field and the counts of layer ends, entries and calibrations, and that the payload CRC64 matches.
 *
 * @param data Serialized block, header followed by its payload.
 * @param length Length of @p data in bytes.
 * @return AARUF_STATUS_OK if the block is valid, AARUF_ERROR_INCORRECT_DATA_SIZE if the sizes do not match,
 *         AARUF_ERROR_CANNOT_READ_BLOCK if the identifier is wrong, AARUF_ERROR_INVALID_BLOCK_CRC on CRC mismatch.
 */
int32_t validate_dpm_block(const uint8_t *data, size_t length)
{
    if(data == NULL || length < sizeof(DpmHeader)) return AARUF_ERROR_INCORRECT_DATA_SIZE;

    DpmHeader header;
    memcpy(&header, data, sizeof(DpmHeader));

    if(header.identifier != DataPositionMeasurementBlock) return AARUF_ERROR_CANNOT_READ_BLOCK;

    if(header.length != length - sizeof(DpmHeader) || header.length != dpm_payload_length(&header) ||
       header.length > UINT32_MAX)
        return AARUF_ERROR_INCORRECT_DATA_SIZE;

    if(aaruf_crc64_data(data + sizeof(DpmHeader), (uint32_t)header.length) != header.crc64)
        return AARUF_ERROR_INVALID_BLOCK_CRC;

    return AARUF_STATUS_OK;
}

/**
 * @brief Recovers a damaged Data Position Measurement block with erasure coding.
 *
 * The DPM block is part of the erasure coding metadata group. When it fails to load, its offset is remembered in
 * ctx->dpm_damaged_offset, and this rebuilds it from the parity, trims the alignment padding the recovered bytes
 * carry, validates it and keeps it in the context. aaruf_open() loads the erasure coding map before processing the
 * indexed blocks, so this works while opening.
 *
 * @param ctx Pointer to the aaruformat context.
 * @return AARUF_STATUS_OK if the block was recovered, an error status otherwise.
 */
int32_t recover_dpm_block(aaruformat_context *ctx)
{
    TRACE("Entering recover_dpm_block(%p)", ctx);

    if(ctx == NULL || ctx->dpm_damaged_offset == 0) return AARUF_ERROR_CANNOT_READ_BLOCK;

    uint8_t *recovered = NULL;
    uint32_t size      = 0;

    if(ec_recover_meta_block(ctx, ctx->dpm_damaged_offset, &recovered, &size) != AARUF_STATUS_OK ||
       recovered == NULL)
    {
        TRACE("Could not recover DPM block at %" PRIu64, ctx->dpm_damaged_offset);

        TRACE("Exiting recover_dpm_block() = AARUF_ERROR_CANNOT_READ_BLOCK");
        return AARUF_ERROR_CANNOT_READ_BLOCK;
    }

    int32_t status = AARUF_ERROR_CANNOT_READ_BLOCK;

    if(size >= sizeof(DpmHeader))
    {
        DpmHeader header;
        memcpy(&header, recovered, sizeof(DpmHeader));

        const size_t block_length = sizeof(DpmHeader) + (size_t)header.length;

        if(header.length <= UINT32_MAX && size >= block_length &&
           validate_dpm_block(recovered, block_length) == AARUF_STATUS_OK)
        {
            uint8_t *block = malloc(block_length);

            if(block != NULL)
            {
                TRACE("Recovered DPM block at %" PRIu64 " with erasure coding", ctx->dpm_damaged_offset);

                memcpy(block, recovered, block_length);
                free(ctx->dpm_block);
                ctx->dpm_block          = block;
                ctx->dpm_block_length   = block_length;
                ctx->dpm_damaged_offset = 0;
                status                  = AARUF_STATUS_OK;
            }
        }
    }

    free(recovered);

    TRACE("Exiting recover_dpm_block() = %d", status);
    return status;
}

/**
 * @brief Processes a Data Position Measurement block from the image stream.
 *
 * Reads the block pointed to by the index entry, validates it and keeps it, header included, in the context so
 * aaruf_get_dpm() can return it. The block is optional: if it cannot be read or is invalid it is ignored and the
 * image opens without it.
 *
 * On images opened for resume, the loaded block is not marked dirty, so its index entry is kept untouched on close
 * unless aaruf_set_dpm() or aaruf_clear_dpm() are called.
 *
 * @param ctx Pointer to the aaruformat context.
 * @param entry Index entry pointing to the block.
 */
void process_dpm_block(aaruformat_context *ctx, const IndexEntry *entry)
{
    TRACE("Entering process_dpm_block(%p, %p)", ctx, entry);

    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");

        TRACE("Exiting process_dpm_block()");
        return;
    }

    if(aaruf_fseek(ctx->imageStream, (aaru_off_t)entry->offset, SEEK_SET) < 0 ||
       aaruf_ftell(ctx->imageStream) != (aaru_off_t)entry->offset)
    {
        FATAL("Could not seek to %" PRIu64 " as indicated by index entry...", entry->offset);

        TRACE("Exiting process_dpm_block()");
        return;
    }

    DpmHeader header;

    // From here on, a damaged block is recovered with erasure coding if the image has it
    ctx->dpm_damaged_offset = entry->offset;

    if(fread(&header, 1, sizeof(DpmHeader), ctx->imageStream) != sizeof(DpmHeader))
    {
        TRACE("Could not read DPM header, trying to recover it...");
        recover_dpm_block(ctx);

        TRACE("Exiting process_dpm_block()");
        return;
    }

    if(header.identifier != DataPositionMeasurementBlock || header.length != dpm_payload_length(&header) ||
       header.length > UINT32_MAX)
    {
        TRACE("Incorrect DPM header at position %" PRIu64 ", trying to recover it...", entry->offset);
        recover_dpm_block(ctx);

        TRACE("Exiting process_dpm_block()");
        return;
    }

    const size_t block_length = sizeof(DpmHeader) + (size_t)header.length;
    uint8_t     *block        = malloc(block_length);

    if(block == NULL)
    {
        TRACE("Could not allocate memory for DPM block, continuing...");

        TRACE("Exiting process_dpm_block()");
        return;
    }

    memcpy(block, &header, sizeof(DpmHeader));

    if(fread(block + sizeof(DpmHeader), 1, header.length, ctx->imageStream) != header.length ||
       validate_dpm_block(block, block_length) != AARUF_STATUS_OK)
    {
        TRACE("Could not read DPM block or it is corrupted, trying to recover it...");
        free(block);
        recover_dpm_block(ctx);

        TRACE("Exiting process_dpm_block()");
        return;
    }

    free(ctx->dpm_block);
    ctx->dpm_block          = block;
    ctx->dpm_block_length   = block_length;
    ctx->dpm_damaged_offset = 0;

    ctx->image_info.ImageSize += header.length;

    TRACE("Found DPM block at %" PRIu64 " with %u entries.", entry->offset, header.entries);

    TRACE("Exiting process_dpm_block()");
}
