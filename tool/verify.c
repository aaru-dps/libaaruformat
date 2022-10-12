/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2022 Natalia Portillo.
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

#include <aaruformat.h>

int verify(char* path)
{
    aaruformatContext* ctx;
    uint32_t           res;

    ctx = aaruf_open(path);

    if(ctx == NULL)
    {
        printf("Error %d when opening AaruFormat image.\n", errno);
        return errno;
    }

    res = aaruf_verify_image(ctx);

    if(res == AARUF_STATUS_OK) printf("Image blocks contain no errors.\n");
    else if(res == AARUF_ERROR_INVALID_BLOCK_CRC)
        printf("A block contains an invalid CRC value.\n");
    else
        printf("Error %d verifying image.\n", res);

    return res;
}