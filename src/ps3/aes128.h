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
 * AES-128 CBC encrypt/decrypt implementation.
 * Derived from public domain tiny-AES-c by kokke.
 */

#ifndef LIBAARUFORMAT_AES128_H
#define LIBAARUFORMAT_AES128_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief AES-128 CBC encrypt data in-place.
 *
 * @param key  16-byte encryption key.
 * @param iv   16-byte initialization vector (not modified).
 * @param data Buffer to encrypt in-place. Must be a multiple of 16 bytes.
 * @param length Number of bytes to encrypt. Must be a multiple of 16.
 */
void aes128_cbc_encrypt(const uint8_t key[16], const uint8_t iv[16], uint8_t *data, uint32_t length);

/**
 * @brief AES-128 CBC decrypt data in-place.
 *
 * @param key  16-byte decryption key.
 * @param iv   16-byte initialization vector (not modified).
 * @param data Buffer to decrypt in-place. Must be a multiple of 16 bytes.
 * @param length Number of bytes to decrypt. Must be a multiple of 16.
 */
void aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16], uint8_t *data, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif /* LIBAARUFORMAT_AES128_H */
