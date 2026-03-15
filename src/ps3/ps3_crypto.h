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
 * PS3 disc encryption: key derivation, sector encrypt/decrypt, IV derivation.
 */

#ifndef LIBAARUFORMAT_PS3_CRYPTO_H
#define LIBAARUFORMAT_PS3_CRYPTO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Derive a PS3 disc key from a data1 key.
 *
 * disc_key = AES-128-CBC-Encrypt(PS3_ERK, PS3_ERK_IV, data1)
 *
 * @param data1    16-byte data1 key (from disc or IRD).
 * @param disc_key Output: 16-byte derived disc key.
 */
void ps3_derive_disc_key(const uint8_t data1[16], uint8_t disc_key[16]);

/**
 * @brief Derive the per-sector AES IV from a sector number.
 *
 * IV = sector number as 128-bit big-endian integer, zero-padded left.
 *
 * @param sector_num Sector number.
 * @param iv         Output: 16-byte IV buffer.
 */
void ps3_derive_iv(uint64_t sector_num, uint8_t iv[16]);

/**
 * @brief Encrypt a sector using PS3 disc encryption (AES-128-CBC).
 *
 * @param disc_key 16-byte disc key.
 * @param sector_num Sector number (for IV derivation).
 * @param data     Buffer to encrypt in-place.
 * @param length   Number of bytes (must be multiple of 16).
 */
void ps3_encrypt_sector(const uint8_t disc_key[16], uint64_t sector_num, uint8_t *data, uint32_t length);

/**
 * @brief Decrypt a sector using PS3 disc encryption (AES-128-CBC).
 *
 * @param disc_key 16-byte disc key.
 * @param sector_num Sector number (for IV derivation).
 * @param data     Buffer to decrypt in-place.
 * @param length   Number of bytes (must be multiple of 16).
 */
void ps3_decrypt_sector(const uint8_t disc_key[16], uint64_t sector_num, uint8_t *data, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif /* LIBAARUFORMAT_PS3_CRYPTO_H */
