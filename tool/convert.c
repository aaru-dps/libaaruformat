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
#include <unicode/ucnv.h>
#include <unicode/ustring.h>

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

    printf("Input image has %llu sectors of %u bytes each.\n", total_sectors, sector_size);

    // Convert application name from UTF-8 to UTF-16LE using libicu
    const char *app_name_utf8      = "aaruformattool";
    size_t      app_name_utf8_len  = strlen(app_name_utf8);
    UErrorCode  status             = U_ZERO_ERROR;
    int32_t     app_name_utf16_len = 0;
    // Get required length for UTF-16
    u_strFromUTF8(NULL, 0, &app_name_utf16_len, app_name_utf8, (int32_t)app_name_utf8_len, &status);
    status                = U_ZERO_ERROR;
    UChar *app_name_utf16 = (UChar *)malloc((app_name_utf16_len + 1) * sizeof(UChar));
    if(app_name_utf16 == NULL)
    {
        printf("Error allocating memory for UTF-16 application name.\n");
        aaruf_close(input_ctx);
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }
    u_strFromUTF8(app_name_utf16, app_name_utf16_len + 1, NULL, app_name_utf8, (int32_t)app_name_utf8_len, &status);
    if(U_FAILURE(status))
    {
        printf("Error converting application name to UTF-16LE: %d\n", status);
        free(app_name_utf16);
        aaruf_close(input_ctx);
        return status;
    }
    // Convert UChar (UTF-16, host endian) to raw UTF-16LE bytes
    uint8_t *app_name_utf16le = (uint8_t *)malloc(app_name_utf16_len * 2);
    if(app_name_utf16le == NULL)
    {
        printf("Error allocating memory for UTF-16LE application name.\n");
        free(app_name_utf16);
        aaruf_close(input_ctx);
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }
    for(int32_t i = 0; i < app_name_utf16_len; i++)
    {
        app_name_utf16le[i * 2]     = (uint8_t)(app_name_utf16[i] & 0xFF);
        app_name_utf16le[i * 2 + 1] = (uint8_t)((app_name_utf16[i] >> 8) & 0xFF);
    }
    free(app_name_utf16);

    // Create output image
    output_ctx = aaruf_create(output_path, input_ctx->imageInfo.MediaType, sector_size, total_sectors,
                              0,     // negative sectors
                              0,     // overflow sectors
                              NULL,  // options
                              app_name_utf16le,
                              app_name_utf16_len * 2,  // application name length in bytes
                              1,                       // major version
                              0,                       // minor version
                              false);
    free(app_name_utf16le);

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
            printf("\rProgress: %llu/%llu sectors (%.1f%%)", sector + 1, total_sectors,
                   (double)(sector + 1) / total_sectors * 100.0);
            fflush(stdout);
        }

        // Read sector from input
        res = aaruf_read_sector(input_ctx, sector, false, sector_data, &read_length);
        if(res != AARUF_STATUS_OK)
        {
            printf("\nError %d when reading sector %llu from input image.\n", res, (unsigned long long)sector);
            break;
        }

        // Write sector to output
        res = aaruf_write_sector(output_ctx, sector, false, sector_data, SectorStatusDumped, read_length);
        if(res != AARUF_STATUS_OK)
        {
            printf("\nError %d when writing sector %llu to output image.\n", res, (unsigned long long)sector);
            break;
        }
    }

    size_t tracks_length = 0;
    res                  = aaruf_get_tracks(input_ctx, NULL, &tracks_length);
    if(res != AARUF_ERROR_TRACK_NOT_FOUND && res == AARUF_ERROR_BUFFER_TOO_SMALL)
    {
        uint8_t *tracks = calloc(1, tracks_length);

        res = aaruf_get_tracks(input_ctx, tracks, &tracks_length);
        if(res == AARUF_STATUS_OK)
        {
            res = aaruf_set_tracks(output_ctx, (TrackEntry *)tracks, (int)(tracks_length / sizeof(TrackEntry)));
            if(res != AARUF_STATUS_OK) printf("\nError %d when setting tracks on output image.\n", res);
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
