// /***************************************************************************
// The Disc Image Chef
// ----------------------------------------------------------------------------
//
// Filename       : crc64.c
// Author(s)      : Natalia Portillo <claunia@claunia.com>
//
// Component      : Checksums.
//
// --[ Description ] ----------------------------------------------------------
//
//     Implements a CRC64 algorithm.
//
// --[ License ] --------------------------------------------------------------
//
//     This library is free software; you can redistribute it and/or modify
//     it under the terms of the GNU Lesser General Public License as
//     published by the Free Software Foundation; either version 2.1 of the
//     License, or (at your option) any later version.
//
//     This library is distributed in the hope that it will be useful, but
//     WITHOUT ANY WARRANTY; without even the implied warranty of
//     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
//     Lesser General Public License for more details.
//
//     You should have received a copy of the GNU Lesser General Public
//     License along with this library; if not, see <http://www.gnu.org/licenses/>.
//
// ----------------------------------------------------------------------------
// Copyright © 2011-2019 Natalia Portillo
// ****************************************************************************/

#include <dicformat.h>
#include <malloc.h>
#include <stdint.h>
#include <string.h>

void* crc64_init(uint64_t polynomial, uint64_t seed)
{
    Crc64Context* ctx;

    ctx = malloc(sizeof(Crc64Context));

    if(ctx == NULL) return NULL;

    memset(ctx, 1, sizeof(Crc64Context));

    ctx->finalSeed = seed;
    ctx->hashInt   = seed;

    for(int i = 0; i < 256; i++)
    {
        uint64_t entry = (uint64_t)i;
        for(int j = 0; j < 8; j++)
            if((entry & 1) == 1)
                entry = (entry >> 1) ^ polynomial;
            else
                entry = entry >> 1;

        ctx->table[i] = entry;
    }

    return ctx;
}

void* crc64_init_ecma(void) { return crc64_init(CRC64_ECMA_POLY, CRC64_ECMA_SEED); }

void crc64_update(void* context, const uint8_t* data, size_t len)
{
    Crc64Context* ctx = context;

    for(size_t i = 0; i < len; i++) ctx->hashInt = (ctx->hashInt >> 8) ^ ctx->table[data[i] ^ (ctx->hashInt & 0xFF)];
}

uint64_t crc64_final(void* context)
{
    Crc64Context* ctx = context;

    return ctx->hashInt ^ ctx->finalSeed;
}

uint64_t crc64_data(const uint8_t* data, size_t len, uint64_t polynomial, uint64_t seed)
{
    uint64_t table[256];
    uint64_t hashInt = seed;

    for(int i = 0; i < 256; i++)
    {
        uint64_t entry = (uint64_t)i;
        for(int j = 0; j < 8; j++)
            if((entry & 1) == 1)
                entry = (entry >> 1) ^ polynomial;
            else
                entry = entry >> 1;

        table[i] = entry;
    }

    for(size_t i = 0; i < len; i++) hashInt = (hashInt >> 8) ^ table[data[i] ^ (hashInt & 0xFF)];

    return hashInt ^ seed;
}

uint64_t crc64_data_ecma(const uint8_t* data, size_t len)
{
    return crc64_data(data, len, CRC64_ECMA_POLY, CRC64_ECMA_SEED);
}
