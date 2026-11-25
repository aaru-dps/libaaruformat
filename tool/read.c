/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <errno.h>
#include <stdint.h>

#include <aaruformat.h>

#include "aaruformattool.h"

static const char *sector_status_name(const uint8_t status)
{
    switch(status)
    {
        case SectorStatusNotDumped:
            return "not dumped";
        case SectorStatusDumped:
            return "dumped";
        case SectorStatusErrored:
            return "errored";
        case SectorStatusMode1Correct:
            return "Mode 1 correct";
        case SectorStatusMode2Form1Ok:
            return "Mode 2 Form 1 correct";
        case SectorStatusMode2Form2Ok:
            return "Mode 2 Form 2 correct";
        case SectorStatusMode2Form2NoCrc:
            return "Mode 2 Form 2 without CRC";
        case SectorStatusTwin:
            return "twin";
        case SectorStatusUnrecorded:
            return "unrecorded";
        case SectorStatusEncrypted:
            return "encrypted";
        case SectorStatusUnencrypted:
            return "unencrypted";
        case SectorStatusGenerable:
            return "generable";
        default:
            return "unknown";
    }
}

/** Reads and prints one instance of a sector. */
static int32_t print_instance(aaruformat_context *ctx, const unsigned long long sector_no, const bool long_mode,
                              const uint32_t twin)
{
    uint32_t length        = 0;
    uint8_t  sector_status = 0;
    int32_t  res = long_mode ? aaruf_read_sector_twin_long(ctx, sector_no, false, twin, NULL, &length, &sector_status)
                             : aaruf_read_sector_twin(ctx, sector_no, false, twin, NULL, &length, &sector_status);

    if(res != AARUF_STATUS_OK && res != AARUF_ERROR_BUFFER_TOO_SMALL)
    {
        printf("Error %d when guessing sector size.\n", res);
        return res;
    }

    uint8_t *data = malloc(length);

    if(data == NULL)
    {
        printf("Error allocating memory for sector.\n");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    res = long_mode ? aaruf_read_sector_twin_long(ctx, sector_no, false, twin, data, &length, &sector_status)
                    : aaruf_read_sector_twin(ctx, sector_no, false, twin, data, &length, &sector_status);

    if(res != AARUF_STATUS_OK)
    {
        printf("Error %d when reading sector.\n", res);
        free(data);
        return res;
    }

    printf("Sector is %u bytes, status %s.\n", length, sector_status_name(sector_status));
    printhex(data, length, 16, true);

    free(data);
    return AARUF_STATUS_OK;
}

/**
 * Reads a sector and prints it. When the sector has twins, every instance is printed, unless twin is not negative,
 * in which case only that instance is printed.
 */
static int read_common(const unsigned long long sector_no, const char *path, const bool long_mode, const int twin)
{
    aaruformat_context *ctx = aaruf_open(path, false, NULL);

    if(ctx == NULL)
    {
        printf("Error %d when opening AaruFormat image.\n", errno);
        return errno;
    }

    uint32_t count = 1;
    int32_t  res   = aaruf_get_sector_twin_count(ctx, sector_no, false, &count);

    if(res != AARUF_STATUS_OK)
    {
        printf("Error %d when getting the number of instances of the sector.\n", res);
        aaruf_close(ctx);
        return res;
    }

    if(twin >= 0)
    {
        if((uint32_t)twin >= (count == 0 ? 1 : count))
        {
            printf("Sector %llu has %u instance(s), there is no twin %d.\n", sector_no, count, twin);
            aaruf_close(ctx);
            return AARUF_ERROR_TWIN_INDEX_OUT_OF_RANGE;
        }

        if(count > 1) printf("Twin %d of %u:\n", twin, count);
        res = print_instance(ctx, sector_no, long_mode, (uint32_t)twin);
        aaruf_close(ctx);
        return res;
    }

    if(count > 1) printf("Sector %llu has %u instances (twin sectors).\n", sector_no, count);

    for(uint32_t i = 0; i < (count == 0 ? 1 : count); i++)
    {
        if(count > 1) printf("\nTwin %u of %u:\n", i, count);

        res = print_instance(ctx, sector_no, long_mode, i);
        if(res != AARUF_STATUS_OK) break;
    }

    aaruf_close(ctx);
    return res;
}

int read_sector(const unsigned long long sector_no, const char *path, const int twin)
{ return read_common(sector_no, path, false, twin); }

int read_long(const unsigned long long sector_no, const char *path, const int twin)
{ return read_common(sector_no, path, true, twin); }
