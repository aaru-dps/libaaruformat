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
#ifdef __cplusplus
extern "C"
{
#endif
#include <stdint.h>

#define CRC32_ISO_POLY 0xEDB88320
#define CRC32_ISO_SEED 0xFFFFFFFF

    uint32_t crc32_data(const uint8_t* data, uint32_t len)
    {
        uint32_t localHashInt = CRC32_ISO_SEED;
        uint32_t localTable[256];
        int      i, j;

        for(i = 0; i < 256; i++)
        {
            uint32_t entry = (uint32_t)i;

            for(j = 0; j < 8; j++)
                if((entry & 1) == 1) entry = (entry >> 1) ^ CRC32_ISO_POLY;
                else
                    entry >>= 1;

            localTable[i] = entry;
        }

        for(i = 0; i < len; i++) localHashInt = (localHashInt >> 8) ^ localTable[data[i] ^ (localHashInt & 0xff)];

        localHashInt ^= CRC32_ISO_SEED;

        return localHashInt;
    }
#ifdef __cplusplus
}
#endif