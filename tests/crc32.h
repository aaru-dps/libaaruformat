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

#ifndef LIBAARUFORMAT_TESTS_CRC32_H_
#define LIBAARUFORMAT_TESTS_CRC32_H_
#ifdef __cplusplus
extern "C"
{
#endif

    uint32_t crc32_data(const uint8_t* data, uint32_t len);
#ifdef __cplusplus
}
#endif

#endif // LIBAARUFORMAT_TESTS_CRC32_H_
