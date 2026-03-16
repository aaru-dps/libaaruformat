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
 * Nintendo GameCube/Wii junk map: serialization, deserialization, regeneration.
 */

#ifndef LIBAARUFORMAT_NGCW_JUNK_H
#define LIBAARUFORMAT_NGCW_JUNK_H

#include <stdbool.h>
#include <stdint.h>

#include "lfg.h"

/* Forward declaration — guard against redefinition when aaruformat.h is also included */
#ifndef AARUFORMAT_CONTEXT_DECLARED
#define AARUFORMAT_CONTEXT_DECLARED
typedef struct aaruformat_context aaruformat_context;
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#define NGCW_JUNK_MAP_VERSION 1 /**< Current junk map serialization version. */

    /**
     * @brief In-memory junk map entry.
     *
     * Each entry describes a contiguous region of LFG-generated junk on disc.
     */
    typedef struct NgcwJunkEntry
    {
        uint64_t offset;                  /**< Disc byte offset where junk starts. */
        uint64_t length;                  /**< Length of junk region in bytes. */
        uint16_t partition_index;         /**< Partition index (0xFFFF for GC / inter-partition). */
        uint32_t seed[NGC_LFG_SEED_SIZE]; /**< LFG seed (17 words, big-endian). */
    } NgcwJunkEntry;

    /**
     * @brief Serialize a junk map for storage as a media tag.
     *
     * Format (all little-endian):
     *   [2 bytes] version (uint16)
     *   [4 bytes] entry_count (uint32)
     *   [2 bytes] seed_size (uint16) — NGC_LFG_SEED_SIZE
     *   For each entry (86 bytes when seed_size=17):
     *     [8 bytes] offset (uint64)
     *     [8 bytes] length (uint64)
     *     [2 bytes] partition_index (uint16)
     *     [seed_size * 4 bytes] seed (raw bytes, big-endian uint32 words)
     *
     * @param entries   Array of junk entries.
     * @param count     Number of entries.
     * @param out_data  Output: malloc'd buffer (caller must free).
     * @param out_len   Output: length of the buffer.
     * @return 0 on success, negative error code on failure.
     */
    int32_t ngcw_serialize_junk_map(const NgcwJunkEntry *entries, uint32_t count, uint8_t **out_data,
                                    uint32_t *out_len);

    /**
     * @brief Deserialize a junk map from a media tag buffer.
     *
     * @param data      Serialized data buffer.
     * @param data_len  Length of the data buffer.
     * @param entries   Output: malloc'd array of NgcwJunkEntry (caller must free).
     * @param count     Output: number of entries.
     * @param seed_size Output: seed size in uint32 words.
     * @return 0 on success, negative error code on failure.
     */
    int32_t ngcw_deserialize_junk_map(const uint8_t *data, uint32_t data_len, NgcwJunkEntry **entries, uint32_t *count,
                                      uint16_t *seed_size);

    /**
     * @brief Regenerate a junk sector from the junk map.
     *
     * Looks up the given disc byte offset in the junk map, initializes the LFG
     * with the matching entry's seed, advances to the correct stream position,
     * and generates the requested number of bytes.
     *
     * @param entries      Array of junk entries (sorted by offset).
     * @param entry_count  Number of entries.
     * @param disc_offset  Disc byte offset of the sector to regenerate.
     * @param output       Output buffer to fill.
     * @param length       Number of bytes to generate.
     * @return 0 on success, -1 if the offset is not in the junk map.
     */
    int ngcw_regenerate_junk_sector(const NgcwJunkEntry *entries, uint32_t entry_count, uint64_t disc_offset,
                                    uint8_t *output, uint32_t length);

    /**
     * @brief Lazy initialization: load junk map from media tags.
     *
     * Populates ctx->ngcw_junk_entries from the kMediaTagNgcwJunkMap media tag.
     *
     * @param ctx AaruFormat context.
     */
    void ngcw_junk_lazy_init(aaruformat_context *ctx);

#ifdef __cplusplus
}
#endif

#endif /* LIBAARUFORMAT_NGCW_JUNK_H */
