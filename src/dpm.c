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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "aaruformat.h"
#include "internal.h"
#include "log.h"

/**
 * @brief Retrieves the Data Position Measurement block.
 *
 * Copies the serialized block, a DpmHeader followed by its payload exactly as stored in the image, to @p buffer.
 * When @p buffer is NULL or too small, @p length is set to the required size and AARUF_ERROR_BUFFER_TOO_SMALL is
 * returned, so the caller can allocate and call again.
 *
 * Works on images opened for reading and on images being created or resumed.
 *
 * @param context Pointer to the aaruformat context.
 * @param buffer Buffer that receives the block, may be NULL to query the size.
 * @param length On input the size of @p buffer, on output the size of the block.
 * @return AARUF_STATUS_OK on success, AARUF_ERROR_NOT_AARUFORMAT if the context is invalid,
 *         AARUF_ERROR_METADATA_NOT_PRESENT if the image has no DPM, AARUF_ERROR_BUFFER_TOO_SMALL if @p buffer is
 *         too small.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_get_dpm(const void *context, uint8_t *buffer, size_t *length)
{
    TRACE("Entering aaruf_get_dpm(%p, %p, %p)", context, buffer, length);

    if(context == NULL || length == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_dpm() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    const aaruformat_context *ctx = context;

    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_get_dpm() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(ctx->dpm_block == NULL || ctx->dpm_block_length == 0)
    {
        TRACE("No DPM present");
        *length = 0;

        TRACE("Exiting aaruf_get_dpm() = AARUF_ERROR_METADATA_NOT_PRESENT");
        return AARUF_ERROR_METADATA_NOT_PRESENT;
    }

    if(buffer == NULL || *length < ctx->dpm_block_length)
    {
        *length = ctx->dpm_block_length;

        TRACE("Exiting aaruf_get_dpm() = AARUF_ERROR_BUFFER_TOO_SMALL");
        return AARUF_ERROR_BUFFER_TOO_SMALL;
    }

    memcpy(buffer, ctx->dpm_block, ctx->dpm_block_length);
    *length = ctx->dpm_block_length;

    TRACE("Exiting aaruf_get_dpm() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Sets the Data Position Measurement block.
 *
 * Takes the serialized block, a DpmHeader followed by its payload, validates its identifier, sizes and CRC64, and
 * keeps a copy that is written when the image is closed, replacing any DPM the image already had.
 *
 * @param context Pointer to the aaruformat context, opened for writing.
 * @param data Serialized block.
 * @param length Length of @p data in bytes.
 * @return AARUF_STATUS_OK on success, AARUF_ERROR_NOT_AARUFORMAT if the context is invalid, AARUF_READ_ONLY if the
 *         image is not being written, AARUF_ERROR_INCORRECT_DATA_SIZE, AARUF_ERROR_CANNOT_READ_BLOCK or
 *         AARUF_ERROR_INVALID_BLOCK_CRC if the block is invalid, AARUF_ERROR_NOT_ENOUGH_MEMORY.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_set_dpm(void *context, const uint8_t *data, size_t length)
{
    TRACE("Entering aaruf_set_dpm(%p, %p, %zu)", context, data, length);

    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_dpm() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_set_dpm() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_set_dpm() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    const int32_t status = validate_dpm_block(data, length);

    if(status != AARUF_STATUS_OK)
    {
        FATAL("Invalid DPM block");

        TRACE("Exiting aaruf_set_dpm() = %d", status);
        return status;
    }

    uint8_t *copy = malloc(length);

    if(copy == NULL)
    {
        FATAL("Could not allocate memory for DPM block");

        TRACE("Exiting aaruf_set_dpm() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    memcpy(copy, data, length);
    free(ctx->dpm_block);
    ctx->dpm_block        = copy;
    ctx->dpm_block_length = length;
    ctx->dirty_dpm_block  = true;

    TRACE("Exiting aaruf_set_dpm() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}

/**
 * @brief Removes the Data Position Measurement block.
 *
 * Discards the DPM kept in the context. When the image is closed, the DPM block is removed from the index, so a
 * resumed image loses the DPM it had.
 *
 * @param context Pointer to the aaruformat context, opened for writing.
 * @return AARUF_STATUS_OK on success, AARUF_ERROR_NOT_AARUFORMAT if the context is invalid, AARUF_READ_ONLY if the
 *         image is not being written.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_clear_dpm(void *context)
{
    TRACE("Entering aaruf_clear_dpm(%p)", context);

    if(context == NULL)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_dpm() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    aaruformat_context *ctx = context;

    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");

        TRACE("Exiting aaruf_clear_dpm() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    if(!ctx->is_writing)
    {
        FATAL("Trying to write a read-only image");

        TRACE("Exiting aaruf_clear_dpm() = AARUF_READ_ONLY");
        return AARUF_READ_ONLY;
    }

    free(ctx->dpm_block);
    ctx->dpm_block        = NULL;
    ctx->dpm_block_length = 0;
    ctx->dirty_dpm_block  = true;

    TRACE("Exiting aaruf_clear_dpm() = AARUF_STATUS_OK");
    return AARUF_STATUS_OK;
}
