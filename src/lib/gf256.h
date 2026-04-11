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

#ifndef LIBAARUFORMAT_GF256_H
#define LIBAARUFORMAT_GF256_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Multiply two elements in GF(2^8) with polynomial 0x11D.
 * @param a First operand.
 * @param b Second operand.
 * @return Product a*b in GF(2^8).
 */
uint8_t gf256_mul(uint8_t a, uint8_t b);

/**
 * @brief Divide two elements in GF(2^8).
 * @param a Dividend.
 * @param b Divisor (must be non-zero).
 * @return Quotient a/b in GF(2^8).
 */
uint8_t gf256_div(uint8_t a, uint8_t b);

/**
 * @brief Compute multiplicative inverse in GF(2^8).
 * @param a Element (must be non-zero).
 * @return Inverse a^(-1) in GF(2^8).
 */
uint8_t gf256_inv(uint8_t a);

/**
 * @brief Multiply-accumulate a region: dst[i] ^= GF_mul(src[i], coeff) for all i.
 *
 * Uses SIMD acceleration when available (AVX2 > SSSE3 > NEON > scalar).
 * If coeff is 0, this is a no-op. If coeff is 1, this is XOR.
 *
 * @param dst Destination buffer (read-modify-write).
 * @param src Source buffer (read-only).
 * @param coeff GF(2^8) coefficient.
 * @param len Number of bytes to process.
 */
void gf256_mul_region(uint8_t *dst, const uint8_t *src, uint8_t coeff, size_t len);

/**
 * @brief XOR a region: dst[i] ^= src[i] for all i.
 *
 * Uses SIMD acceleration when available.
 *
 * @param dst Destination buffer (read-modify-write).
 * @param src Source buffer (read-only).
 * @param len Number of bytes to process.
 */
void gf256_xor_region(uint8_t *dst, const uint8_t *src, size_t len);

#endif /* LIBAARUFORMAT_GF256_H */
