/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2025 Natalia Portillo.
 * ECC algorithm from ECM(c) 2002-2011 Neill Corlett
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

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "aaruformat.h"
#include "log.h"

void *aaruf_ecc_cd_init()
{
    TRACE("Entering aaruf_ecc_cd_init()");
    CdEccContext *context = NULL;
    uint32_t      edc = 0, i = 0, j = 0;

    TRACE("Creating context");
    context = (CdEccContext *)malloc(sizeof(CdEccContext));

    if(context == NULL) return NULL;

    TRACE("Allocating memory for ECC F table");
    context->eccFTable = (uint8_t *)malloc(sizeof(uint8_t) * 256);

    if(context->eccFTable == NULL)
    {
        free(context);
        return NULL;
    }

    TRACE("Allocating memory for ECC B table");
    context->eccBTable = (uint8_t *)malloc(sizeof(uint8_t) * 256);

    if(context->eccBTable == NULL)
    {
        free(context->eccFTable);
        free(context);
        return NULL;
    }

    TRACE("Allocating memory for EDC table");

    context->edcTable = (uint32_t *)malloc(sizeof(uint32_t) * 256);

    if(context->edcTable == NULL)
    {
        free(context->eccFTable);
        free(context->eccBTable);
        free(context);
        return NULL;
    }

    TRACE("Initializing EDC tables");
    for(i = 0; i < 256; i++)
    {
        edc                       = i;
        j                         = (uint32_t)((i << 1) ^ ((i & 0x80) == 0x80 ? 0x11D : 0));
        context->eccFTable[i]     = (uint8_t)j;
        context->eccBTable[i ^ j] = (uint8_t)i;
        for(j = 0; j < 8; j++) edc = (edc >> 1) ^ ((edc & 1) > 0 ? 0xD8018001 : 0);
        context->edcTable[i] = edc;
    }

    context->initedEdc = true;

    TRACE("Exiting aaruf_ecc_cd_init()");
    return context;
}

bool aaruf_ecc_cd_is_suffix_correct(void *context, const uint8_t *sector)
{
    TRACE("Entering aaruf_ecc_cd_is_suffix_correct(%p, %p)", context, sector);
    CdEccContext *ctx;
    uint32_t      storedEdc, edc, calculatedEdc;
    int           size, pos;

    if(context == NULL || sector == NULL)
    {
        TRACE("Exiting aaruf_ecc_cd_is_suffix_correct() without initialized context");
        return false;
    }

    ctx = (CdEccContext *)context;
    if(!ctx->initedEdc)
    {
        TRACE("Exiting aaruf_ecc_cd_is_suffix_correct() without initialized context");
        return false;
    }

    if(sector[0x814] != 0x00 ||
       // reserved (8 bytes)
       sector[0x815] != 0x00 || sector[0x816] != 0x00 || sector[0x817] != 0x00 || sector[0x818] != 0x00 ||
       sector[0x819] != 0x00 || sector[0x81A] != 0x00 || sector[0x81B] != 0x00)
    {
        TRACE("Exiting aaruf_ecc_cd_is_suffix_correct() = false");
        return false;
    }

    bool correctEccP = aaruf_ecc_cd_check(context, sector, sector, 86, 24, 2, 86, sector, 0xC, 0x10, 0x81C);
    if(!correctEccP)
    {
        TRACE("Exiting aaruf_ecc_cd_is_suffix_correct() = false");
        return false;
    }

    bool correctEccQ = aaruf_ecc_cd_check(context, sector, sector, 52, 43, 86, 88, sector, 0xC, 0x10, 0x81C + 0xAC);
    if(!correctEccQ)
    {
        TRACE("Exiting aaruf_ecc_cd_is_suffix_correct() = false");
        return false;
    }

    storedEdc = (sector[0x813] << 24) + (sector[0x812] << 16) + (sector[0x811] << 8) + sector[0x810];
    edc       = 0;
    size      = 0x810;
    pos       = 0;
    for(; size > 0; size--) edc = (edc >> 8) ^ ctx->edcTable[(edc ^ sector[pos++]) & 0xFF];
    calculatedEdc = edc;

    TRACE("Exiting aaruf_ecc_cd_is_suffix_correct() = %u == %u", calculatedEdc, storedEdc);
    return calculatedEdc == storedEdc;
}

bool aaruf_ecc_cd_is_suffix_correct_mode2(void *context, const uint8_t *sector)
{
    TRACE("Entering aaruf_ecc_cd_is_suffix_correct_mode2(%p, %p)", context, sector);
    CdEccContext *ctx;
    uint32_t      storedEdc, edc, calculatedEdc;
    int           size, pos;
    uint8_t       zeroaddress[4];

    if(context == NULL || sector == NULL)
    {
        TRACE("Exiting aaruf_ecc_cd_is_suffix_correct_mode2() without initialized context");
        return false;
    }

    ctx = (CdEccContext *)context;

    if(!ctx->initedEdc)
    {
        TRACE("Exiting aaruf_ecc_cd_is_suffix_correct_mode2() without initialized context");
        return false;
    }

    memset(&zeroaddress, 4, sizeof(uint8_t));

    bool correctEccP = aaruf_ecc_cd_check(context, zeroaddress, sector, 86, 24, 2, 86, sector, 0, 0x10, 0x81C);
    if(!correctEccP)
    {
        TRACE("Exiting aaruf_ecc_cd_is_suffix_correct_mode2() = false");
        return false;
    }
    bool correctEccQ = aaruf_ecc_cd_check(context, zeroaddress, sector, 52, 43, 86, 88, sector, 0, 0x10, 0x81C + 0xAC);
    if(!correctEccQ)
    {
        TRACE("Exiting aaruf_ecc_cd_is_suffix_correct_mode2() = false");
        return false;
    }
    storedEdc = (sector[0x81B] << 24) + (sector[0x81A] << 16) + (sector[0x819] << 8) + sector[0x818];
    edc       = 0;
    size      = 0x808;
    pos       = 0x10;
    for(; size > 0; size--) edc = (edc >> 8) ^ ctx->edcTable[(edc ^ sector[pos++]) & 0xFF];
    calculatedEdc = edc;

    TRACE("Exiting aaruf_ecc_cd_is_suffix_correct_mode2() = %u == %u", calculatedEdc, storedEdc);
    return calculatedEdc == storedEdc;
}

bool aaruf_ecc_cd_check(void *context, const uint8_t *address, const uint8_t *data, uint32_t majorCount,
                        uint32_t minorCount, uint32_t majorMult, uint32_t minorInc, const uint8_t *ecc,
                        int32_t addressOffset, int32_t dataOffset, int32_t eccOffset)
{
    TRACE("Entering aaruf_ecc_cd_check(%p, %p, %p, %u, %u, %u, %u, %p, %d, %d, %d)", context, address, data, majorCount,
          minorCount, majorMult, minorInc, ecc, addressOffset, dataOffset, eccOffset);

    CdEccContext *ctx;
    uint32_t      size, major, idx, minor;
    uint8_t       eccA, eccB, temp;

    if(context == NULL || address == NULL || data == NULL || ecc == NULL)
    {
        TRACE("Exiting aaruf_ecc_cd_check() with missing data");
        return false;
    }

    ctx = (CdEccContext *)context;

    if(!ctx->initedEdc)
    {
        TRACE("Exiting aaruf_ecc_cd_check() without initialized context");
        return false;
    }

    size = majorCount * minorCount;
    for(major = 0; major < majorCount; major++)
    {
        idx  = (major >> 1) * majorMult + (major & 1);
        eccA = 0;
        eccB = 0;
        for(minor = 0; minor < minorCount; minor++)
        {
            temp = idx < 4 ? address[idx + addressOffset] : data[idx + dataOffset - 4];
            idx += minorInc;
            if(idx >= size) idx -= size;
            eccA ^= temp;
            eccB ^= temp;
            eccA = ctx->eccFTable[eccA];
        }

        eccA = ctx->eccBTable[ctx->eccFTable[eccA] ^ eccB];
        if(ecc[major + eccOffset] != eccA || ecc[major + majorCount + eccOffset] != (eccA ^ eccB))
        {
            TRACE("Exiting aaruf_ecc_cd_check() = false, ECC mismatch at major %u", major);
            return false;
        }
    }

    TRACE("Exiting aaruf_ecc_cd_check() = true, ECC matches");
    return true;
}

void aaruf_ecc_cd_write(void *context, const uint8_t *address, const uint8_t *data, uint32_t majorCount,
                        uint32_t minorCount, uint32_t majorMult, uint32_t minorInc, uint8_t *ecc, int32_t addressOffset,
                        int32_t dataOffset, int32_t eccOffset)
{
    TRACE("Entering aaruf_ecc_cd_write(%p, %p, %p, %u, %u, %u, %u, %p, %d, %d, %d)", context, address, data, majorCount,
          minorCount, majorMult, minorInc, ecc, addressOffset, dataOffset, eccOffset);

    CdEccContext *ctx;
    uint32_t      size, major, idx, minor;
    uint8_t       eccA, eccB, temp;

    if(context == NULL || address == NULL || data == NULL || ecc == NULL)
    {
        TRACE("Exiting aaruf_ecc_cd_write() with missing data");
        return;
    }

    ctx = (CdEccContext *)context;

    if(!ctx->initedEdc)
    {
        TRACE("Exiting aaruf_ecc_cd_write() without initialized context");
        return;
    }

    size = majorCount * minorCount;
    for(major = 0; major < majorCount; major++)
    {
        idx  = (major >> 1) * majorMult + (major & 1);
        eccA = 0;
        eccB = 0;

        for(minor = 0; minor < minorCount; minor++)
        {
            temp = idx < 4 ? address[idx + addressOffset] : data[idx + dataOffset - 4];
            idx += minorInc;
            if(idx >= size) idx -= size;
            eccA ^= temp;
            eccB ^= temp;
            eccA = ctx->eccFTable[eccA];
        }

        eccA                                = ctx->eccBTable[ctx->eccFTable[eccA] ^ eccB];
        ecc[major + eccOffset]              = eccA;
        ecc[major + majorCount + eccOffset] = (eccA ^ eccB);
    }

    TRACE("Exiting aaruf_ecc_cd_write()");
}

void aaruf_ecc_cd_write_sector(void *context, const uint8_t *address, const uint8_t *data, uint8_t *ecc,
                               int32_t addressOffset, int32_t dataOffset, int32_t eccOffset)
{
    TRACE("Entering aaruf_ecc_cd_write_sector(%p, %p, %p, %p, %d, %d, %d)", context, address, data, ecc, addressOffset,
          dataOffset, eccOffset);

    aaruf_ecc_cd_write(context, address, data, 86, 24, 2, 86, ecc, addressOffset, dataOffset, eccOffset);          // P
    aaruf_ecc_cd_write(context, address, data, 52, 43, 86, 88, ecc, addressOffset, dataOffset, eccOffset + 0xAC);  // Q

    TRACE("Exiting aaruf_ecc_cd_write_sector()");
}

void aaruf_cd_lba_to_msf(int64_t pos, uint8_t *minute, uint8_t *second, uint8_t *frame)
{
    TRACE("Entering aaruf_cd_lba_to_msf(%lld, %p, %p, %p)", pos, minute, second, frame);

    *minute = (uint8_t)((pos + 150) / 75 / 60);
    *second = (uint8_t)((pos + 150) / 75 % 60);
    *frame  = (uint8_t)((pos + 150) % 75);

    TRACE("Exiting aaruf_cd_lba_to_msf() = %u:%u:%u", *minute, *second, *frame);
}

void aaruf_ecc_cd_reconstruct_prefix(uint8_t *sector,  // must point to a full 2352-byte sector
                                     uint8_t type, int64_t lba)
{
    TRACE("Entering aaruf_ecc_cd_reconstruct_prefix(%p, %u, %lld)", sector, type, lba);

    uint8_t minute, second, frame;

    if(sector == NULL) return;

    //
    // Sync
    //
    sector[0x000] = 0x00;
    sector[0x001] = 0xFF;
    sector[0x002] = 0xFF;
    sector[0x003] = 0xFF;
    sector[0x004] = 0xFF;
    sector[0x005] = 0xFF;
    sector[0x006] = 0xFF;
    sector[0x007] = 0xFF;
    sector[0x008] = 0xFF;
    sector[0x009] = 0xFF;
    sector[0x00A] = 0xFF;
    sector[0x00B] = 0x00;

    aaruf_cd_lba_to_msf(lba, &minute, &second, &frame);

    sector[0x00C] = (uint8_t)(((minute / 10) << 4) + minute % 10);
    sector[0x00D] = (uint8_t)(((second / 10) << 4) + second % 10);
    sector[0x00E] = (uint8_t)(((frame / 10) << 4) + frame % 10);

    switch((TrackType)type)
    {
        case CdMode1:
            //
            // Mode
            //
            sector[0x00F] = 0x01;
            break;
        case CdMode2Form1:
        case CdMode2Form2:
        case CdMode2Formless:
            //
            // Mode
            //
            sector[0x00F] = 0x02;
            //
            // Flags
            //
            sector[0x010] = sector[0x014];
            sector[0x011] = sector[0x015];
            sector[0x012] = sector[0x016];
            sector[0x013] = sector[0x017];
            break;
        default:
            return;
    }

    TRACE("Exiting aaruf_ecc_cd_reconstruct_prefix()");
}

void aaruf_ecc_cd_reconstruct(void    *context,
                              uint8_t *sector,  // must point to a full 2352-byte sector
                              uint8_t  type)
{
    TRACE("Entering aaruf_ecc_cd_reconstruct(%p, %p, %u)", context, sector, type);

    uint32_t computedEdc;
    uint8_t  zeroaddress[4];

    CdEccContext *ctx;

    if(context == NULL || sector == NULL)
    {
        TRACE("Exiting aaruf_ecc_cd_reconstruct() with missing data");
        return;
    }

    ctx = (CdEccContext *)context;

    if(!ctx->initedEdc)
    {
        TRACE("Exiting aaruf_ecc_cd_reconstruct() without initialized context");
        return;
    }

    switch(type)
    {
        //
        // Compute EDC
        //
        case CdMode1:
            computedEdc = aaruf_edc_cd_compute(context, 0, sector, 0x810, 0);
            memcpy(sector + 0x810, &computedEdc, 4);
            break;
        case CdMode2Form1:
            computedEdc = aaruf_edc_cd_compute(context, 0, sector, 0x808, 0x10);
            memcpy(sector + 0x818, &computedEdc, 4);
            break;
        case CdMode2Form2:
            computedEdc = aaruf_edc_cd_compute(context, 0, sector, 0x91C, 0x10);
            memcpy(sector + 0x92C, &computedEdc, 4);
            break;
        default:
            TRACE("Exiting aaruf_ecc_cd_reconstruct() with unknown type %u", type);
            return;
    }

    memset(&zeroaddress, 0, 4);

    switch(type)
    {
        //
        // Compute ECC
        //
        case CdMode1:
            //
            // Reserved
            //
            sector[0x814] = 0x00;
            sector[0x815] = 0x00;
            sector[0x816] = 0x00;
            sector[0x817] = 0x00;
            sector[0x818] = 0x00;
            sector[0x819] = 0x00;
            sector[0x81A] = 0x00;
            sector[0x81B] = 0x00;
            aaruf_ecc_cd_write_sector(context, sector, sector, sector, 0xC, 0x10, 0x81C);
            break;
        case CdMode2Form1:
            aaruf_ecc_cd_write_sector(context, zeroaddress, sector, sector, 0, 0x10, 0x81C);
            break;
        default:
            TRACE("Exiting aaruf_ecc_cd_reconstruct() with unknown type %u", type);
            return;
    }

    //
    // Done
    //
    TRACE("Exiting aaruf_ecc_cd_reconstruct()");
}

uint32_t aaruf_edc_cd_compute(void *context, uint32_t edc, const uint8_t *src, int size, int pos)
{
    TRACE("Entering aaruf_edc_cd_compute(%p, %u, %p, %d, %d)", context, edc, src, size, pos);

    CdEccContext *ctx;

    if(context == NULL || src == NULL)
    {
        TRACE("Exiting aaruf_edc_cd_compute() with missing data");
        return 0;
    }

    ctx = (CdEccContext *)context;

    if(!ctx->initedEdc)
    {
        TRACE("Exiting aaruf_edc_cd_compute() without initialized context");
        return 0;
    }

    for(; size > 0; size--) edc = (edc >> 8) ^ ctx->edcTable[(edc ^ src[pos++]) & 0xFF];

    TRACE("Exiting aaruf_edc_cd_compute() = 0x%08X", edc);
    return edc;
}
