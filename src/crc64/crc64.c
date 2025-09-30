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

#include <stdlib.h>

#include <aaruformat.h>

#include "log.h"

/**
 * @brief Initializes a CRC64 context.
 *
 * Allocates and initializes a CRC64 context for checksum calculations.
 *
 * @return Pointer to the initialized crc64_ctx structure, or NULL on failure.
 */
AARU_EXPORT crc64_ctx *AARU_CALL aaruf_crc64_init(void)
{
    TRACE("Entering aaruf_crc64_init()");
    crc64_ctx *ctx = (crc64_ctx *)malloc(sizeof(crc64_ctx));

    if(!ctx) return NULL;

    ctx->crc = CRC64_ECMA_SEED;

    TRACE("Exiting aaruf_crc64_init()");
    return ctx;
}

/**
 * @brief Updates the CRC64 context with new data.
 *
 * Processes the given data buffer and updates the CRC64 value in the context.
 *
 * @param ctx Pointer to the CRC64 context.
 * @param data Pointer to the data buffer.
 * @param len Length of the data buffer.
 * @return 0 on success, or -1 on error.
 */
AARU_EXPORT int AARU_CALL aaruf_crc64_update(crc64_ctx *ctx, const uint8_t *data, uint32_t len)
{
    TRACE("Entering aaruf_crc64_update(%p, %p, %u)", ctx, data, len);
    if(!ctx || !data)
    {
        TRACE("Exiting aaruf_crc64_update() = -1");
        return -1;
    }

#if defined(__x86_64__) || defined(__amd64) || defined(_M_AMD64) || defined(_M_X64) || defined(__I386__) || \
    defined(__i386__) || defined(__THW_INTEL) || defined(_M_IX86)
    if(have_clmul())
    {
        ctx->crc = ~aaruf_crc64_clmul(~ctx->crc, data, len);

        TRACE("Exiting aaruf_crc64_update() = 0");
        return 0;
    }
#endif

#if defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) || defined(_M_ARM)
    if(have_neon())
    {
        ctx->crc = ~aaruf_crc64_vmull(~ctx->crc, data, len);

        TRACE("Exiting aaruf_crc64_update() = 0");
        return 0;
    }
#endif

    // Unroll according to Intel slicing by uint8_t
    // http://www.intel.com/technology/comms/perfnet/download/CRC_generators.pdf
    // http://sourceforge.net/projects/slicing-by-8/

    aaruf_crc64_slicing(&ctx->crc, data, len);

    TRACE("Exiting aaruf_crc64_update() = 0");
    return 0;
}

/**
 * @brief Updates a CRC64 value using the slicing-by-8 algorithm.
 *
 * @param previous_crc Pointer to the previous CRC64 value (input/output).
 * @param data Pointer to the data buffer.
 * @param len Length of the data buffer in bytes.
 */
AARU_EXPORT void AARU_CALL aaruf_crc64_slicing(uint64_t *previous_crc, const uint8_t *data, uint32_t len)
{
    uint64_t c = *previous_crc;

    if(len > 4)
    {
        const uint8_t *limit = NULL;

        while((uintptr_t)(data) & 3)
        {
            c = crc64_table[0][*data++ ^ ((c) & 0xFF)] ^ ((c) >> 8);
            --len;
        }

        limit = data + (len & ~(uint32_t)(3));
        len &= (uint32_t)(3);

        while(data < limit)
        {
            const uint32_t tmp = c ^ *(const uint32_t *)(data);
            data += 4;

            c = crc64_table[3][((tmp) & 0xFF)] ^ crc64_table[2][(((tmp) >> 8) & 0xFF)] ^ ((c) >> 32) ^
                crc64_table[1][(((tmp) >> 16) & 0xFF)] ^ crc64_table[0][((tmp) >> 24)];
        }
    }

    while(len-- != 0) c = crc64_table[0][*data++ ^ ((c) & 0xFF)] ^ ((c) >> 8);

    *previous_crc = c;
}

/**
 * @brief Computes the final CRC64 value from the context.
 *
 * @param ctx Pointer to the CRC64 context.
 * @param crc Pointer to store the resulting CRC64 value.
 * @return 0 on success, -1 on error.
 */
AARU_EXPORT int AARU_CALL aaruf_crc64_final(crc64_ctx *ctx, uint64_t *crc)
{
    if(!ctx) return -1;

    *crc = ctx->crc ^ CRC64_ECMA_SEED;

    return 0;
}

/**
 * @brief Frees a CRC64 context.
 *
 * @param ctx Pointer to the CRC64 context to free.
 */
AARU_EXPORT void AARU_CALL aaruf_crc64_free(crc64_ctx *ctx)
{
    if(ctx) free(ctx);
}

AARU_EXPORT uint64_t AARU_CALL aaruf_crc64_data(const uint8_t *data, uint32_t len)
{
    crc64_ctx *ctx = aaruf_crc64_init();
    uint64_t   crc = 0;

    if(!ctx) return crc;

    aaruf_crc64_update(ctx, data, len);
    aaruf_crc64_final(ctx, &crc);
    aaruf_crc64_free(ctx);

    return crc;
}
