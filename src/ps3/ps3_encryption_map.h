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
 * PS3 encryption map: plaintext region parsing, serialization, and lookup.
 */

#ifndef LIBAARUFORMAT_PS3_ENCRYPTION_MAP_H
#define LIBAARUFORMAT_PS3_ENCRYPTION_MAP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** A plaintext (unencrypted) region on a PS3 disc. */
typedef struct Ps3PlaintextRegion
{
    uint32_t start_sector; /**< First sector of plaintext region (inclusive). */
    uint32_t end_sector;   /**< Last sector of plaintext region (inclusive).  */
} Ps3PlaintextRegion;

#define PS3_MAX_PLAINTEXT_REGIONS 64

/**
 * @brief Parse the encryption map from PS3 disc sector 0 (big-endian on disc).
 *
 * @param sector0     Pointer to the 2048-byte sector 0 data.
 * @param length      Length of sector0 data (must be >= 264).
 * @param regions     Output: allocated array of plaintext regions. Caller must free().
 * @param count       Output: number of regions.
 * @return 0 on success, negative error code on failure.
 */
int32_t ps3_parse_encryption_map(const uint8_t *sector0, uint32_t length, Ps3PlaintextRegion **regions,
                                 uint32_t *count);

/**
 * @brief Serialize plaintext regions to little-endian binary for storage as a media tag.
 *
 * Format: [4B region_count LE] + region_count × [4B start LE, 4B end LE]
 *
 * @param regions   Array of plaintext regions.
 * @param count     Number of regions.
 * @param out_data  Output: allocated buffer. Caller must free().
 * @param out_length Output: size of the buffer in bytes.
 * @return 0 on success, negative error code on failure.
 */
int32_t ps3_serialize_encryption_map(const Ps3PlaintextRegion *regions, uint32_t count, uint8_t **out_data,
                                     uint32_t *out_length);

/**
 * @brief Deserialize plaintext regions from little-endian binary (media tag format).
 *
 * @param data     Serialized data buffer.
 * @param length   Length of data buffer.
 * @param regions  Output: allocated array of plaintext regions. Caller must free().
 * @param count    Output: number of regions.
 * @return 0 on success, negative error code on failure.
 */
int32_t ps3_deserialize_encryption_map(const uint8_t *data, uint32_t length, Ps3PlaintextRegion **regions,
                                       uint32_t *count);

/**
 * @brief Check whether a sector is encrypted (i.e., not in any plaintext region).
 *
 * @param plaintext_regions Array of plaintext regions.
 * @param region_count      Number of regions.
 * @param sector_address    Sector number to check.
 * @return true if the sector is encrypted (not in any plaintext region).
 */
bool ps3_is_sector_encrypted(const Ps3PlaintextRegion *plaintext_regions, uint32_t region_count,
                             uint64_t sector_address);

#ifdef __cplusplus
}
#endif

#endif /* LIBAARUFORMAT_PS3_ENCRYPTION_MAP_H */
