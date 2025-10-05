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

#ifndef LIBAARUFORMAT_INTERNAL_H
#define LIBAARUFORMAT_INTERNAL_H

#include "utarray.h"

UT_array *process_index_v1(aaruformatContext *ctx);
int32_t   verify_index_v1(aaruformatContext *ctx);
UT_array *process_index_v2(aaruformatContext *ctx);
int32_t   verify_index_v2(aaruformatContext *ctx);
UT_array *process_index_v3(aaruformatContext *ctx);
int32_t   verify_index_v3(aaruformatContext *ctx);
int32_t   process_data_block(aaruformatContext *ctx, IndexEntry *entry);
int32_t   process_ddt_v1(aaruformatContext *ctx, IndexEntry *entry, bool *found_user_data_ddt);
int32_t   process_ddt_v2(aaruformatContext *ctx, IndexEntry *entry, bool *found_user_data_ddt);
void      process_metadata_block(aaruformatContext *ctx, const IndexEntry *entry);
void      process_geometry_block(aaruformatContext *ctx, const IndexEntry *entry);
void      process_tracks_block(aaruformatContext *ctx, const IndexEntry *entry);
void      process_cicm_block(aaruformatContext *ctx, const IndexEntry *entry);
void      process_aaru_metadata_json_block(aaruformatContext *ctx, const IndexEntry *entry);
void      process_dumphw_block(aaruformatContext *ctx, const IndexEntry *entry);
void      process_checksum_block(aaruformatContext *ctx, const IndexEntry *entry);
void      add_subindex_entries(aaruformatContext *ctx, UT_array *index_entries, IndexEntry *subindex_entry);
int32_t   decode_ddt_entry_v1(aaruformatContext *ctx, uint64_t sector_address, uint64_t *offset, uint64_t *block_offset,
                              uint8_t *sector_status);
int32_t   decode_ddt_entry_v2(aaruformatContext *ctx, uint64_t sector_address, bool negative, uint64_t *offset,
                              uint64_t *block_offset, uint8_t *sector_status);
int32_t   decode_ddt_single_level_v2(aaruformatContext *ctx, uint64_t sector_address, bool negative, uint64_t *offset,
                                     uint64_t *block_offset, uint8_t *sector_status);
int32_t   decode_ddt_multi_level_v2(aaruformatContext *ctx, uint64_t sector_address, bool negative, uint64_t *offset,
                                    uint64_t *block_offset, uint8_t *sector_status);
bool      set_ddt_entry_v2(aaruformatContext *ctx, uint64_t sector_address, bool negative, uint64_t offset,
                           uint64_t block_offset, uint8_t sector_status, uint64_t *ddt_entry);
bool      set_ddt_single_level_v2(aaruformatContext *ctx, uint64_t sector_address, bool negative, uint64_t offset,
                                  uint64_t block_offset, uint8_t sector_status, uint64_t *ddt_entry);
bool      set_ddt_multi_level_v2(aaruformatContext *ctx, uint64_t sector_address, bool negative, uint64_t offset,
                                 uint64_t block_offset, uint8_t sector_status, uint64_t *ddt_entry);
aaru_options parse_options(const char *options);
uint64_t     get_filetime_uint64();
int32_t      aaruf_close_current_block(aaruformatContext *ctx);

#endif  // LIBAARUFORMAT_INTERNAL_H
