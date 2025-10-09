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

#include <aaruformat.h>

#include "aaruformattool.h"

int verify(const char *path)
{
    aaruformatContext *ctx = NULL;
    uint32_t           res = 0;

    ctx = aaruf_open(path);

    if(ctx == NULL)
    {
        printf("Error %d when opening AaruFormat image.\n", errno);
        return errno;
    }

    res = aaruf_verify_image(ctx);

    if(res == AARUF_STATUS_OK)
        printf("Image blocks contain no errors.\n");
    else if(res == AARUF_ERROR_INVALID_BLOCK_CRC)
        printf("A block contains an invalid CRC value.\n");
    else
        printf("Error %d verifying image.\n", res);

    return res;
}

int verify_sectors(const char *path)
{
    aaruformatContext *ctx            = NULL;
    uint8_t           *buffer         = NULL;
    uint32_t           buffer_len     = 2352;
    int32_t            res            = 0;
    CdEccContext      *cd_ecc_context = NULL;
    ctx                               = aaruf_open(path);
    bool     verify_result            = false;
    bool     has_edc = false, has_ecc_p = false, ecc_p_correct = false, has_ecc_q = false, ecc_q_correct = false;
    bool     edc_correct = false;
    bool     unknown     = false;
    uint64_t errors = 0, unknowns = 0;
    bool     any_error = false;

    if(ctx == NULL)
    {
        printf("Error %d when opening AaruFormat image.\n", errno);
        return errno;
    }

    if(ctx->imageInfo.MetadataMediaType != OpticalDisc)
    {
        printf("Image sectors do not contain checksums, cannot verify.\n");
        return 0;
    }

    cd_ecc_context = aaruf_ecc_cd_init();
    errors         = 0;
    unknowns       = 0;
    any_error      = false;

    for(uint64_t s = 0; s < ctx->imageInfo.Sectors; s++)
    {
        printf("\rVerifying sector %llu...", s);
        res = aaruf_read_sector_long(ctx, s, buffer, false, &buffer_len);

        if(res != AARUF_STATUS_OK)
        {
            fprintf(stderr, "\rError %d reading sector %llu\n.", res, s);
            continue;
        }

        verify_result = check_cd_sector_channel(cd_ecc_context, buffer, &unknown, &has_edc, &edc_correct, &has_ecc_p,
                                                &ecc_p_correct, &has_ecc_q, &ecc_q_correct);

        if(verify_result) continue;

        if(unknown)
        {
            unknowns++;
            printf("\rSector %llu cannot be verified.\n", s);
            continue;
        }

        if(has_edc && !edc_correct) printf("\rSector %llu has an incorrect EDC value.\n", s);

        if(has_ecc_p && !ecc_p_correct) printf("\rSector %llu has an incorrect EDC value.\n", s);

        if(has_ecc_q && !ecc_q_correct) printf("\rSector %llu has an incorrect EDC value.\n", s);

        errors++;
        any_error = true;
    }

    if(any_error)
        printf("\rSome sectors had incorrect checksums.\n");
    else
        printf("\rAll sector checksums are correct.\n");

    printf("Total sectors........... %llu\n", ctx->imageInfo.Sectors);
    printf("Total errors............ %llu\n", errors);
    printf("Total unknowns.......... %llu\n", unknowns);
    printf("Total errors+unknowns... %llu\n", errors + unknowns);

    return AARUF_STATUS_OK;
}