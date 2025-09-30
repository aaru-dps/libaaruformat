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

#include <aaruformat.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aaruformattool.h"

int cli_compare(const char *path1, const char *path2)
{
    aaruformatContext *ctx1              = NULL;
    aaruformatContext *ctx2              = NULL;
    uint8_t           *buffer1           = NULL;
    uint8_t           *buffer2           = NULL;
    uint32_t           buffer1_length    = 0;
    uint32_t           buffer2_length    = 0;
    uint64_t           total_sectors     = 0;
    uint64_t           different_sectors = 0;
    uint64_t           sectors_processed = 0;
    int                result            = 0;
    int32_t            read_result1      = 0;
    int32_t            read_result2      = 0;

    printf("Opening first image: %s\n", path1);
    ctx1 = aaruf_open(path1);
    if(ctx1 == NULL)
    {
        fprintf(stderr, "Error: Could not open first image '%s'\n", path1);
        return -1;
    }

    printf("Opening second image: %s\n", path2);
    ctx2 = aaruf_open(path2);
    if(ctx2 == NULL)
    {
        fprintf(stderr, "Error: Could not open second image '%s'\n", path2);
        aaruf_close(ctx1);
        return -1;
    }

    // Access image information through context structure
    printf("\nImage Information:\n");
    printf("Image 1: %llu sectors, %u bytes per sector\n", (unsigned long long)ctx1->imageInfo.Sectors,
           ctx1->imageInfo.SectorSize);
    printf("Image 2: %llu sectors, %u bytes per sector\n", (unsigned long long)ctx2->imageInfo.Sectors,
           ctx2->imageInfo.SectorSize);

    if(ctx1->imageInfo.Sectors != ctx2->imageInfo.Sectors)
    {
        fprintf(stderr, "Warning: Images have different number of sectors\n");
        total_sectors =
            ctx1->imageInfo.Sectors < ctx2->imageInfo.Sectors ? ctx1->imageInfo.Sectors : ctx2->imageInfo.Sectors;
        printf("Will compare first %llu sectors\n", (unsigned long long)total_sectors);
    }
    else { total_sectors = ctx1->imageInfo.Sectors; }

    if(ctx1->imageInfo.SectorSize != ctx2->imageInfo.SectorSize)
    {
        fprintf(stderr, "Error: Images have different sector sizes (%u vs %u)\n", ctx1->imageInfo.SectorSize,
                ctx2->imageInfo.SectorSize);
        aaruf_close(ctx1);
        aaruf_close(ctx2);
        return -1;
    }

    // Allocate buffers for sector data
    buffer1 = malloc(ctx1->imageInfo.SectorSize);
    buffer2 = malloc(ctx2->imageInfo.SectorSize);
    if(buffer1 == NULL || buffer2 == NULL)
    {
        fprintf(stderr, "Error: Could not allocate memory for sector buffers\n");
        if(buffer1) free(buffer1);
        if(buffer2) free(buffer2);
        aaruf_close(ctx1);
        aaruf_close(ctx2);
        return -1;
    }

    printf("\nStarting sector-by-sector comparison...\n");
    printf("Progress: 0%% (0/%llu sectors)\r", (unsigned long long)total_sectors);
    fflush(stdout);

    // Compare sectors
    for(uint64_t sector = 0; sector < total_sectors; sector++)
    {
        buffer1_length = ctx1->imageInfo.SectorSize;
        buffer2_length = ctx2->imageInfo.SectorSize;

        read_result1 = aaruf_read_sector(ctx1, sector, buffer1, &buffer1_length);
        read_result2 = aaruf_read_sector(ctx2, sector, buffer2, &buffer2_length);

        // Handle read errors or missing sectors
        bool sector1_available = (read_result1 == AARUF_STATUS_OK);
        bool sector2_available = (read_result2 == AARUF_STATUS_OK);
        bool sectors_different = false;

        if(!sector1_available && !sector2_available)
        {
            // Both sectors are not available - consider them the same
            sectors_different = false;
        }
        else if(sector1_available != sector2_available)
        {
            // One sector is available, the other is not - they're different
            sectors_different = true;
        }
        else if(sector1_available && sector2_available &&
                (buffer1_length != buffer2_length || memcmp(buffer1, buffer2, buffer1_length) != 0))
        {
            // Both sectors are available - compare their content
            sectors_different = true;
        }

        if(sectors_different)
        {
            printf("Sector %llu: DIFFERENT", (unsigned long long)sector);
            if(!sector1_available)
                printf(" (missing in image 1)");
            else if(!sector2_available)
                printf(" (missing in image 2)");
            else if(buffer1_length != buffer2_length)
                printf(" (different sizes: %u vs %u)", buffer1_length, buffer2_length);
            printf("\n");
            different_sectors++;
        }

        sectors_processed++;

        // Update progress every 1000 sectors or at the end
        if(sectors_processed % 1000 == 0 || sector == total_sectors - 1)
        {
            int progress = (int)((sectors_processed * 100) / total_sectors);
            printf("Progress: %d%% (%llu/%llu sectors)\r", progress, (unsigned long long)sectors_processed,
                   (unsigned long long)total_sectors);
            fflush(stdout);
        }
    }

    printf("\n\nComparison completed!\n");
    printf("Total sectors compared: %llu\n", (unsigned long long)total_sectors);
    printf("Different sectors: %llu\n", (unsigned long long)different_sectors);
    printf("Identical sectors: %llu\n", (unsigned long long)(total_sectors - different_sectors));

    if(different_sectors == 0)
    {
        printf("✓ Images are identical!\n");
        result = 0;
    }
    else
    {
        printf("✗ Images are different (%llu sectors differ)\n", (unsigned long long)different_sectors);
        result = 1;  // Non-zero exit code to indicate differences
    }

    // Cleanup
    free(buffer1);
    free(buffer2);
    aaruf_close(ctx1);
    aaruf_close(ctx2);

    return result;
}
