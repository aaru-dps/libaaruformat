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

int read_sector(const unsigned long long sector_no, const char *path)
{
    aaruformat_context *ctx           = NULL;
    int32_t             res           = 0;
    uint32_t            length        = 0;
    uint8_t            *data          = NULL;
    uint8_t             sector_status = 0;

    ctx = aaruf_open(path, false, NULL);

    if(ctx == NULL)
    {
        printf("Error %d when opening AaruFormat image.\n", errno);
        return errno;
    }

    res = aaruf_read_sector(ctx, sector_no, false, NULL, &length, &sector_status);

    if(res != AARUF_STATUS_OK && res != AARUF_ERROR_BUFFER_TOO_SMALL)
    {
        printf("Error %d when guessing sector size.\n", res);
        aaruf_close(ctx);
        return res;
    }

    printf("Sector is %d bytes.\n", length);

    data = malloc(length);

    if(data == NULL)
    {
        printf("Error allocating memory for sector.\n");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    res = aaruf_read_sector(ctx, sector_no, false, data, &length, &sector_status);

    if(res != AARUF_STATUS_OK)
    {
        printf("Error %d when reading sector.\n", res);
        aaruf_close(ctx);
        return res;
    }

    printhex(data, length, 16, true);

    free(data);

    aaruf_close(ctx);

    return AARUF_STATUS_OK;
}

int read_long(const unsigned long long sector_no, const char *path)
{
    aaruformat_context *ctx           = NULL;
    int32_t             res           = 0;
    uint32_t            length        = 0;
    uint8_t            *data          = NULL;
    uint8_t             sector_status = 0;

    ctx = aaruf_open(path, false, NULL);

    if(ctx == NULL)
    {
        printf("Error %d when opening AaruFormat image.\n", errno);
        return errno;
    }

    res = aaruf_read_sector_long(ctx, sector_no, false, NULL, &length, &sector_status);

    if(res != AARUF_STATUS_OK && res != AARUF_ERROR_BUFFER_TOO_SMALL)
    {
        printf("Error %d when guessing sector size.\n", res);
        aaruf_close(ctx);
        return res;
    }

    printf("Sector is %d bytes.\n", length);

    data = malloc(length);

    if(data == NULL)
    {
        printf("Error allocating memory for sector.\n");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    res = aaruf_read_sector_long(ctx, sector_no, false, data, &length, &sector_status);

    if(res != AARUF_STATUS_OK)
    {
        printf("Error %d when reading sector.\n", res);
        aaruf_close(ctx);
        return res;
    }

    printhex(data, length, 16, true);

    free(data);

    aaruf_close(ctx);

    return AARUF_STATUS_OK;
}