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

#ifndef LIBAARUFORMAT_ERASURE_INTERNAL_H
#define LIBAARUFORMAT_ERASURE_INTERNAL_H

#include "aaruformat/context.h"
#include "aaruformat/structs/data.h"

/**
 * @brief Accumulate parity for a data block just written to disk.
 *
 * Called from aaruf_close_current_block() after writing header + payload.
 */
void ec_accumulate_data_block(aaruformat_context *ctx, const BlockHeader *block_header, const uint8_t *lzma_props,
                              const uint8_t *payload, uint32_t payload_size, uint64_t file_offset);

/**
 * @brief Flush a completed data stripe slot: write parity blocks and record descriptor.
 */
void ec_flush_data_stripe(aaruformat_context *ctx, uint32_t slot);

/**
 * @brief Flush partial stripes, write ECMB and recovery footer.
 *
 * Called from aaruf_finalize_write() after index is written.
 */
void ec_finalize(aaruformat_context *ctx);

/**
 * @brief Free all erasure coding state.
 *
 * Called from aaruf_close().
 */
void ec_free(aaruformat_context *ctx);

#endif /* LIBAARUFORMAT_ERASURE_INTERNAL_H */
