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
 * Wii U disc encryption: partition key map, encrypt/decrypt, serialization.
 */

#ifndef LIBAARUFORMAT_WIIU_CRYPTO_H
#define LIBAARUFORMAT_WIIU_CRYPTO_H

#include <stdbool.h>
#include <stdint.h>

/* Forward declaration */
typedef struct aaruformat_context aaruformat_context;

#ifdef __cplusplus
extern "C"
{
#endif

#define WIIU_CRYPTO_SECTOR_SIZE      0x8000 /**< Wii U physical sector size (32 KiB). */
#define WIIU_LOGICAL_PER_PHYSICAL    16     /**< Number of 2048-byte logical sectors per physical sector. */
#define WIIU_HEADER_PHYSICAL_SECTORS 3      /**< Disc header occupies physical sectors 0-2 (plaintext). */
#define WIIU_MAX_PARTITIONS          8      /**< Maximum number of partitions supported. */

    /**
     * @brief A Wii U partition region entry (in-memory representation).
     *
     * Stores the physical sector range and AES-128 key for one partition.
     * Physical sector numbers are in 0x8000-byte units (matching disc TOC).
     * By convention, the partition's start_sector is a plaintext header;
     * sectors [start_sector+1, end_sector) are encrypted with key.
     */
    typedef struct WiiuPartitionRegion
    {
        uint32_t start_sector; /**< First physical sector of partition (plaintext header). */
        uint32_t end_sector;   /**< End physical sector (exclusive). */
        uint8_t  key[16];      /**< AES-128 key for encrypted sectors in this partition. */
    } WiiuPartitionRegion;

    /**
     * @brief Get the encryption key for a given logical sector (2048-byte).
     *
     * Converts the logical sector address to a physical sector address,
     * then looks up which partition it belongs to and whether it is encrypted.
     *
     * @param regions      Array of partition regions.
     * @param region_count Number of partition regions.
     * @param logical_sector Logical sector address (2048-byte units).
     * @return Pointer to the 16-byte key if the sector is encrypted, NULL if plaintext.
     */
    const uint8_t *wiiu_get_sector_key(const WiiuPartitionRegion *regions, uint32_t region_count,
                                       uint64_t logical_sector);

    /**
     * @brief Check if a logical sector (2048-byte) is in an encrypted region.
     *
     * @param regions      Array of partition regions.
     * @param region_count Number of partition regions.
     * @param logical_sector Logical sector address (2048-byte units).
     * @return true if the sector is encrypted, false if plaintext.
     */
    bool wiiu_is_sector_encrypted(const WiiuPartitionRegion *regions, uint32_t region_count, uint64_t logical_sector);

    /**
     * @brief Encrypt a full 0x8000-byte physical sector in-place.
     *
     * Uses AES-128-CBC with IV=0 (Wii U scheme).
     *
     * @param key    16-byte AES-128 key.
     * @param data   Buffer to encrypt in-place (must be 0x8000 bytes).
     * @param length Number of bytes (must be 0x8000).
     */
    void wiiu_encrypt_physical_sector(const uint8_t key[16], uint8_t *data, uint32_t length);

    /**
     * @brief Decrypt a full 0x8000-byte physical sector in-place.
     *
     * Uses AES-128-CBC with IV=0 (Wii U scheme).
     *
     * @param key    16-byte AES-128 key.
     * @param data   Buffer to decrypt in-place (must be 0x8000 bytes).
     * @param length Number of bytes (must be 0x8000).
     */
    void wiiu_decrypt_physical_sector(const uint8_t key[16], uint8_t *data, uint32_t length);

    /**
     * @brief Serialize a partition key map for storage as a media tag.
     *
     * Format (all little-endian):
     *   [4 bytes] partition_count
     *   For each partition (24 bytes):
     *     [4 bytes] start_sector
     *     [4 bytes] end_sector
     *     [16 bytes] key
     *
     * @param regions   Array of partition regions.
     * @param count     Number of regions.
     * @param out_data  Output: malloc'd buffer (caller must free).
     * @param out_len   Output: length of the buffer.
     * @return 0 on success, negative error code on failure.
     */
    int32_t wiiu_serialize_partition_key_map(const WiiuPartitionRegion *regions, uint32_t count, uint8_t **out_data,
                                             uint32_t *out_len);

    /**
     * @brief Deserialize a partition key map from a media tag buffer.
     *
     * @param data     Serialized data buffer.
     * @param data_len Length of the data buffer.
     * @param regions  Output: malloc'd array of WiiuPartitionRegion (caller must free).
     * @param count    Output: number of regions.
     * @return 0 on success, negative error code on failure.
     */
    int32_t wiiu_deserialize_partition_key_map(const uint8_t *data, uint32_t data_len, WiiuPartitionRegion **regions,
                                               uint32_t *count);

    /**
     * @brief Lazy initialization: load disc key and partition key map from media tags.
     *
     * Populates ctx->wiiu_disc_key and ctx->wiiu_partition_regions from media tags.
     * Allocates ctx->wiiu_encrypted_block_cache if needed.
     *
     * @param ctx AaruFormat context.
     */
    void wiiu_lazy_init(aaruformat_context *ctx);

#ifdef __cplusplus
}
#endif

#endif /* LIBAARUFORMAT_WIIU_CRYPTO_H */
