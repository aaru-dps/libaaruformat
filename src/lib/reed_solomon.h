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

#ifndef LIBAARUFORMAT_REED_SOLOMON_H
#define LIBAARUFORMAT_REED_SOLOMON_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Opaque Reed-Solomon codec context.
 */
typedef struct rs_context rs_context;

/**
 * @brief Create a Reed-Solomon codec for RS(K, M) over GF(2^8).
 *
 * K = number of data shards, M = number of parity shards.
 * K + M must be <= 255 (GF(2^8) field size minus 1).
 *
 * The codec precomputes the Vandermonde-derived generator matrix
 * (M rows x K columns) used for encoding.
 *
 * @param K Number of data shards (>= 1).
 * @param M Number of parity shards (>= 1).
 * @return Codec context, or NULL on error.
 */
rs_context *rs_create(uint16_t K, uint16_t M);

/**
 * @brief Free a Reed-Solomon codec context.
 * @param ctx Context returned by rs_create().
 */
void rs_free(rs_context *ctx);

/**
 * @brief Get the generator matrix coefficient for parity shard m, data shard k.
 *
 * During incremental encoding, call this to get the coefficient, then call
 * rs_encode_incremental() with it.
 *
 * @param ctx Codec context.
 * @param m Parity shard index (0 .. M-1).
 * @param k Data shard index (0 .. K-1).
 * @return GF(2^8) coefficient.
 */
uint8_t rs_get_coefficient(const rs_context *ctx, uint16_t m, uint16_t k);

/**
 * @brief Incrementally accumulate one data shard's contribution to one parity shard.
 *
 * Computes: parity[i] ^= GF_mul(data[i], coeff) for all i in [0, shard_size).
 *
 * This is the core primitive for streaming write: for each data block written,
 * call this M times (once per parity shard) with the appropriate coefficient.
 *
 * For M=1 (XOR-only), coeff is always 1, and this reduces to XOR.
 *
 * @param coeff GF(2^8) coefficient from rs_get_coefficient().
 * @param data Data shard bytes (read-only, shard_size bytes).
 * @param parity Parity shard accumulator (read-write, shard_size bytes, must be zeroed before first call).
 * @param shard_size Number of bytes per shard.
 */
void rs_encode_incremental(uint8_t coeff, const uint8_t *data, uint8_t *parity, size_t shard_size);

/**
 * @brief Decode (reconstruct) erased shards.
 *
 * Given K+M shards where some are erased, reconstruct the erased ones using
 * Gaussian elimination over GF(2^8).
 *
 * @param ctx Codec context.
 * @param shards Array of K+M shard pointers (each shard_size bytes). Erased shards must
 *               point to allocated buffers of shard_size bytes (content will be overwritten).
 * @param present Boolean array of K+M entries: 1 = shard is valid, 0 = shard is erased.
 * @param shard_size Number of bytes per shard.
 * @return 0 on success, -1 if too many erasures (> M), -2 on allocation failure.
 */
int rs_decode(const rs_context *ctx, uint8_t **shards, const uint8_t *present, size_t shard_size);

#endif /* LIBAARUFORMAT_REED_SOLOMON_H */
