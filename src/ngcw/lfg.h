/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <https://www.gnu.org/licenses/>.
 *
 * Lagged Fibonacci Generator for Nintendo GameCube/Wii junk fill.
 * Based on Dolphin emulator's LaggedFibonacciGenerator (CC0 licensed).
 */

#ifndef LIBAARUFORMAT_NGCW_LFG_H
#define LIBAARUFORMAT_NGCW_LFG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define NGC_LFG_K         521 /**< LFG buffer size (number of uint32 words in state). */
#define NGC_LFG_J         32  /**< LFG second tap. */
#define NGC_LFG_SEED_SIZE 17  /**< Number of uint32 words needed to seed the LFG. */

    /**
     * @brief LFG context holding the 521-word circular buffer and byte position.
     */
    struct ngc_lfg_ctx
    {
        uint32_t buffer[NGC_LFG_K];
        size_t   position_bytes;
    };

    /**
     * @brief Initialize the LFG from a 17-word big-endian seed.
     *
     * @param ctx      LFG context to initialize.
     * @param seed     Array of 17 uint32 seed words (big-endian).
     */
    void ngc_lfg_set_seed(struct ngc_lfg_ctx *ctx, const uint32_t seed[NGC_LFG_SEED_SIZE]);

    /**
     * @brief Generate count bytes of junk data into out.
     *
     * @param ctx      LFG context (must be initialized via ngc_lfg_set_seed).
     * @param out      Output buffer to fill.
     * @param count    Number of bytes to generate.
     */
    void ngc_lfg_get_bytes(struct ngc_lfg_ctx *ctx, uint8_t *out, size_t count);

    /**
     * @brief Try to extract the LFG seed from a chunk of data.
     *
     * Attempts to reverse-engineer the 17-word seed from a buffer of suspected
     * PRNG output. Requires at least NGC_LFG_K * 4 = 2084 bytes of contiguous
     * PRNG output aligned to uint32 boundaries.
     *
     * @param data        Pointer to the data to test.
     * @param size        Number of bytes available.
     * @param data_offset Byte offset of data[0] within its LFG block
     *                    (0x8000-aligned for Wii, arbitrary for GC).
     * @param seed_out    If successful, receives the 17-word seed (big-endian).
     *
     * @return The number of consecutive bytes from data[0] that match the LFG.
     *         Returns 0 if the data does not look like junk.
     */
    size_t ngc_lfg_get_seed(const uint8_t *data, size_t size, size_t data_offset, uint32_t seed_out[NGC_LFG_SEED_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* LIBAARUFORMAT_NGCW_LFG_H */
