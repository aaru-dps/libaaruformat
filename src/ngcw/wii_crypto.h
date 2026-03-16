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
 * Nintendo Wii disc encryption: partition key map, group encrypt/decrypt.
 */

#ifndef LIBAARUFORMAT_NGCW_WII_CRYPTO_H
#define LIBAARUFORMAT_NGCW_WII_CRYPTO_H

#include <stdbool.h>
#include <stdint.h>

/* Forward declaration */
typedef struct aaruformat_context aaruformat_context;

#ifdef __cplusplus
extern "C"
{
#endif

#define WII_GROUP_SIZE        0x8000 /**< Wii physical group size (32 KiB). */
#define WII_GROUP_HASH_SIZE   0x0400 /**< Hash block size within a group (1 KiB). */
#define WII_GROUP_DATA_SIZE   0x7C00 /**< User data size within a group (31 KiB). */
#define WII_LOGICAL_PER_GROUP 16     /**< Number of 2048-byte logical sectors per group. */
#define WII_MAX_PARTITIONS    32     /**< Maximum number of partitions supported. */

    /**
     * @brief A Wii partition region entry (in-memory representation).
     *
     * Stores the physical sector range and AES-128 key for one partition.
     * Physical sector numbers are in 0x8000-byte units.
     */
    typedef struct WiiPartitionRegion
    {
        uint32_t start_sector; /**< First physical sector of partition. */
        uint32_t end_sector;   /**< End physical sector (exclusive). */
        uint8_t  key[16];      /**< AES-128 partition key. */
    } WiiPartitionRegion;

    /**
     * @brief Get the encryption key for a given logical sector (2048-byte).
     *
     * Converts the logical sector address to a physical sector address,
     * then looks up which partition it belongs to and whether it is encrypted.
     *
     * @param regions       Array of partition regions.
     * @param region_count  Number of partition regions.
     * @param logical_sector Logical sector address (2048-byte units).
     * @return Pointer to the 16-byte key if encrypted, NULL if plaintext.
     */
    const uint8_t *wii_get_sector_key(const WiiPartitionRegion *regions, uint32_t region_count,
                                      uint64_t logical_sector);

    /**
     * @brief Check if a logical sector is in an encrypted region.
     */
    bool wii_is_sector_encrypted(const WiiPartitionRegion *regions, uint32_t region_count, uint64_t logical_sector);

    /**
     * @brief Encrypt a Wii group (0x8000 bytes) from separate hash_block + data.
     *
     * The hash block IV is all zeros. The data IV is bytes 0x3D0..0x3DF of the
     * encrypted hash block output (matching Dolphin's VolumeWii::EncryptBlock).
     *
     * @param key        16-byte AES-128 partition key.
     * @param hash_block 0x400-byte hash block (plaintext input).
     * @param data_in    0x7C00-byte user data (plaintext input).
     * @param out        0x8000-byte output buffer (encrypted).
     */
    void wii_encrypt_group(const uint8_t key[16], const uint8_t *hash_block, const uint8_t *data_in, uint8_t *out);

    /**
     * @brief Decrypt a Wii group (0x8000 bytes) into separate hash_block + data.
     *
     * @param key        16-byte AES-128 partition key.
     * @param in         0x8000-byte encrypted input.
     * @param hash_block 0x400-byte output for hash block.
     * @param data_out   0x7C00-byte output for user data.
     */
    void wii_decrypt_group(const uint8_t key[16], const uint8_t *in, uint8_t *hash_block, uint8_t *data_out);

    /**
     * @brief Serialize a Wii partition key map for storage as a media tag.
     *
     * Same format as Wii U partition key map:
     *   [4 bytes] partition_count (uint32 LE)
     *   Per partition (24 bytes): start_sector (LE), end_sector (LE), key[16]
     */
    int32_t wii_serialize_partition_key_map(const WiiPartitionRegion *regions, uint32_t count, uint8_t **out_data,
                                            uint32_t *out_len);

    /**
     * @brief Deserialize a Wii partition key map from a media tag buffer.
     */
    int32_t wii_deserialize_partition_key_map(const uint8_t *data, uint32_t data_len, WiiPartitionRegion **regions,
                                              uint32_t *count);

    /**
     * @brief Lazy initialization: load partition key map from media tags.
     *
     * Populates ctx->wii_partition_regions and allocates the group cache.
     *
     * @param ctx AaruFormat context.
     */
    void wii_lazy_init(aaruformat_context *ctx);

#ifdef __cplusplus
}
#endif

#endif /* LIBAARUFORMAT_NGCW_WII_CRYPTO_H */
