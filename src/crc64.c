// /***************************************************************************
// Aaru Data Preservation Suite
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
// Copyright © 2011-2022 Natalia Portillo
// ****************************************************************************/

#include <malloc.h>
#include <stdint.h>
#include <string.h>

#include <aaruformat.h>

void* aaruf_crc64_init(uint64_t polynomial, uint64_t seed)
{
    Crc64Context* ctx;
    int           i, j;

    ctx = malloc(sizeof(Crc64Context));

    if(ctx == NULL) return NULL;

    memset(ctx, 1, sizeof(Crc64Context));

    ctx->finalSeed = seed;
    ctx->hashInt   = seed;

    for(i = 0; i < 256; i++)
    {
        uint64_t entry = (uint64_t)i;
        for(j = 0; j < 8; j++)
            if((entry & 1) == 1) entry = (entry >> 1) ^ polynomial;
            else
                entry = entry >> 1;

        ctx->table[i] = entry;
    }

    return ctx;
}

void* aaruf_crc64_init_ecma(void) { return aaruf_crc64_init(CRC64_ECMA_POLY, CRC64_ECMA_SEED); }

void aaruf_crc64_update(void* context, const uint8_t* data, size_t len)
{
    Crc64Context* ctx = context;
    size_t        i;

    for(i = 0; i < len; i++) ctx->hashInt = (ctx->hashInt >> 8) ^ ctx->table[data[i] ^ (ctx->hashInt & 0xFF)];
}

uint64_t aaruf_crc64_final(void* context)
{
    Crc64Context* ctx = context;

    return ctx->hashInt ^ ctx->finalSeed;
}

uint64_t aaruf_crc64_data(const uint8_t* data, size_t len, uint64_t polynomial, uint64_t seed)
{
    uint64_t table[256];
    uint64_t hashInt = seed;
    int      i, j;
    size_t   s;

    for(i = 0; i < 256; i++)
    {
        uint64_t entry = (uint64_t)i;
        for(j = 0; j < 8; j++)
            if((entry & 1) == 1) entry = (entry >> 1) ^ polynomial;
            else
                entry = entry >> 1;

        table[i] = entry;
    }

    for(s = 0; s < len; s++) hashInt = (hashInt >> 8) ^ table[data[s] ^ (hashInt & 0xFF)];

    return hashInt ^ seed;
}

uint64_t aaruf_crc64_data_ecma(const uint8_t* data, size_t len)
{
    return aaruf_crc64_data(data, len, CRC64_ECMA_POLY, CRC64_ECMA_SEED);
}
