/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2022 Natalia Portillo.
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

#ifndef LIBAARUFORMAT_NATIVE_ARM_VMULL_H
#define LIBAARUFORMAT_NATIVE_ARM_VMULL_H

#if defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) || defined(_M_ARM)

TARGET_WITH_CRYPTO static uint64x2_t sse2neon_vmull_p64_crypto(uint64x1_t _a, uint64x1_t _b);
TARGET_WITH_SIMD uint64x2_t          sse2neon_vmull_p64(uint64x1_t _a, uint64x1_t _b);
TARGET_WITH_SIMD uint64x2_t          mm_shuffle_epi8(uint64x2_t a, uint64x2_t b);
TARGET_WITH_SIMD uint64x2_t          mm_srli_si128(uint64x2_t a, int imm);
TARGET_WITH_SIMD uint64x2_t          mm_slli_si128(uint64x2_t a, int imm);

#endif

#endif // LIBAARUFORMAT_NATIVE_ARM_VMULL_H
