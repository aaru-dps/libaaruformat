/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2025 Natalia Portillo.
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
#include <stdio.h>
#include <stdlib.h>

#include <aaruformat.h>

#include "aaruformattool.h"

int convert(const char *input_path, const char *output_path)
{
    aaruformatContext *input_ctx     = NULL;
    aaruformatContext *output_ctx    = NULL;
    int32_t            res           = 0;
    uint32_t           sector_size   = 0;
    uint64_t           total_sectors = 0;
    uint8_t           *sector_data   = NULL;

    printf("Converting image from %s to %s...\n", input_path, output_path);

    // Open input image
    input_ctx = aaruf_open(input_path);
    if(input_ctx == NULL)
    {
        printf("Error %d when opening input AaruFormat image.\n", errno);
        return errno;
    }

    // Get image information from input
    total_sectors = input_ctx->imageInfo.Sectors;
    sector_size   = input_ctx->imageInfo.SectorSize;

    printf("Input image has %llu sectors of %u bytes each.\n", (unsigned long long)total_sectors, sector_size);

    // Create output image
    output_ctx = aaruf_create(output_path, input_ctx->imageInfo.MediaType, sector_size, total_sectors,
                              0,     // negative sectors
                              0,     // overflow sectors
                              NULL,  // options
                              (const uint8_t *)"aaruformattool",
                              14,  // application name length
                              1,   // major version
                              0    // minor version
    );

    if(output_ctx == NULL)
    {
        printf("Error %d when creating output AaruFormat image.\n", errno);
        aaruf_close(input_ctx);
        return errno;
    }

    // Allocate buffer for sector data
    sector_data = malloc(sector_size);
    if(sector_data == NULL)
    {
        printf("Error allocating memory for sector buffer.\n");
        aaruf_close(input_ctx);
        aaruf_close(output_ctx);
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy sectors from input to output
    for(uint64_t sector = 0; sector < total_sectors; sector++)
    {
        uint32_t read_length = sector_size;

        // Show progress every 1000 sectors
        if(sector % 1000 == 0 || sector == total_sectors - 1)
        {
            printf("\rProgress: %llu/%llu sectors (%.1f%%)", (unsigned long long)sector + 1,
                   (unsigned long long)total_sectors, ((double)(sector + 1) / total_sectors) * 100.0);
            fflush(stdout);
        }

        // Read sector from input
        res = aaruf_read_sector(input_ctx, sector, sector_data, &read_length);
        if(res != AARUF_STATUS_OK)
        {
            printf("\nError %d when reading sector %llu from input image.\n", res, (unsigned long long)sector);
            break;
        }

        // Write sector to output
        res = aaruf_write_sector(output_ctx, sector, sector_data, SectorStatusDumped, read_length);
        if(res != AARUF_STATUS_OK)
        {
            printf("\nError %d when writing sector %llu to output image.\n", res, (unsigned long long)sector);
            break;
        }
    }

    printf("\n");

    // Clean up
    free(sector_data);
    aaruf_close(input_ctx);
    res = aaruf_close(output_ctx);

    if(res == AARUF_STATUS_OK)
        printf("Conversion completed successfully.\n");
    else
        printf("Conversion failed with error %d.\n", res);

    return res;
}
