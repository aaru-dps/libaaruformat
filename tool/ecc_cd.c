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

#include <stdbool.h>
#include <stdint.h>

#include <aaruformat.h>

bool check_cd_sector_channel(CdEccContext* context,
                             uint8_t*      sector,
                             bool*         unknown,
                             bool*         has_edc,
                             bool*         edc_correct,
                             bool*         has_ecc_p,
                             bool*         ecc_p_correct,
                             bool*         has_ecc_q,
                             bool*         ecc_q_correct)
{
    int      i;
    uint32_t storedEdc, edc, calculatedEdc;
    int      size, pos;
    uint8_t  zeroaddress[4];

    *has_edc       = false;
    *has_ecc_p     = false;
    *has_ecc_q     = false;
    *edc_correct   = false;
    *ecc_p_correct = false;
    *ecc_q_correct = false;
    *unknown       = false;

    if(sector[0x000] != 0x00 || sector[0x001] != 0xFF || sector[0x002] != 0xFF || sector[0x003] != 0xFF ||
       sector[0x004] != 0xFF || sector[0x005] != 0xFF || sector[0x006] != 0xFF || sector[0x007] != 0xFF ||
       sector[0x008] != 0xFF || sector[0x009] != 0xFF || sector[0x00A] != 0xFF || sector[0x00B] != 0x00)
    {
        *unknown = true;
        return false;
    }

    if((sector[0x00F] & 0x03) == 0x00) // Mode 0
    {
        for(i = 0x010; i < 0x930; i++)
            if(sector[i] != 0x00)
            {
                fprintf(stderr,
                        "Mode 0 sector with error at address: %2X:%2X:%2X.\n",
                        sector[0x00C],
                        sector[0x00D],
                        sector[0x00E]);
                return false;
            }

        return true;
    }

    if((sector[0x00F] & 0x03) == 0x01) // Mode 1
    {
        if(sector[0x814] != 0x00 ||
           // reserved (8 bytes)
           sector[0x815] != 0x00 || sector[0x816] != 0x00 || sector[0x817] != 0x00 || sector[0x818] != 0x00 ||
           sector[0x819] != 0x00 || sector[0x81A] != 0x00 || sector[0x81B] != 0x00)
        {
            fprintf(stderr,
                    "Mode 1 with data in reserved bytes at address: %2X:%2X:%2X.\n",
                    sector[0x00C],
                    sector[0x00D],
                    sector[0x00E]);
            return false;
        }

        *has_edc   = true;
        *has_ecc_p = true;
        *has_ecc_q = true;

        *ecc_p_correct = aaruf_ecc_cd_check(context, sector, sector, 86, 24, 2, 86, sector, 0xC, 0x10, 0x81C);
        *ecc_q_correct = aaruf_ecc_cd_check(context, sector, sector, 52, 43, 86, 88, sector, 0xC, 0x10, 0x81C + 0xAC);

        storedEdc =
            (sector[0x813] << 24) + (sector[0x812] << 16) + (sector[0x811] << 8) + sector[0x810]; // TODO: Check casting
        edc  = 0;
        size = 0x810;
        pos  = 0;
        for(; size > 0; size--) edc = (edc >> 8) ^ context->edcTable[(edc ^ sector[pos++]) & 0xFF];
        calculatedEdc = edc;

        *edc_correct = calculatedEdc == storedEdc;

        if(!*edc_correct)
        {
            fprintf(stderr,
                    "Mode 1 sector at address: %2X:%2X:%2X, got CRC 0x%8X expected 0x%8X\n",
                    sector[0x00C],
                    sector[0x00D],
                    sector[0x00E],
                    calculatedEdc,
                    storedEdc);
        }

        if(!*ecc_p_correct)
        {
            fprintf(stderr,
                    "Mode 1 sector at address: %2X:%2X:%2X, fails ECC P check.\n",
                    sector[0x00C],
                    sector[0x00D],
                    sector[0x00E]);
        }

        if(!*ecc_q_correct)
        {
            fprintf(stderr,
                    "Mode 1 sector at address: %2X:%2X:%2X, fails ECC Q check.\n",
                    sector[0x00C],
                    sector[0x00D],
                    sector[0x00E]);
        }

        return *edc_correct && *ecc_p_correct && *ecc_q_correct;
    }

    if((sector[0x00F] & 0x03) == 0x02) // Mode 2
    {
        if((sector[0x012] & 0x20) == 0x20) // mode 2 form 2
        {
            if(sector[0x010] != sector[0x014] || sector[0x011] != sector[0x015] || sector[0x012] != sector[0x016] ||
               sector[0x013] != sector[0x017])
                fprintf(stderr,
                        "Subheader copies differ in mode 2 form 2 sector at address: %2X:%2X:%2X",
                        sector[0x00C],
                        sector[0x00D],
                        sector[0x00E]);

            storedEdc = sector[0x91C];

            if(storedEdc == 0) return true;

            *has_edc = true;

            storedEdc = (sector[0x81B] << 24) + (sector[0x81A] << 16) + (sector[0x819] << 8) + sector[0x818];
            edc       = 0;
            size      = 0x808;
            pos       = 0x10;
            for(; size > 0; size--) edc = (edc >> 8) ^ context->edcTable[(edc ^ sector[pos++]) & 0xFF];
            calculatedEdc = edc;

            *edc_correct = calculatedEdc == storedEdc;

            if(!*edc_correct)
            {
                fprintf(stderr,
                        "Mode 2 sector at address: %2X:%2X:%2X, got CRC 0x%8X expected 0x%8X\n",
                        sector[0x00C],
                        sector[0x00D],
                        sector[0x00E],
                        calculatedEdc,
                        storedEdc);
            }

            return *edc_correct;
        }

        *ecc_p_correct = aaruf_ecc_cd_check(context, zeroaddress, sector, 86, 24, 2, 86, sector, 0, 0x10, 0x81C);

        *ecc_q_correct =
            aaruf_ecc_cd_check(context, zeroaddress, sector, 52, 43, 86, 88, sector, 0, 0x10, 0x81C + 0xAC);

        storedEdc = sector[0x818]; // TODO: Check cast
        edc       = 0;
        size      = 0x808;
        pos       = 0x10;
        for(; size > 0; size--) edc = (edc >> 8) ^ context->edcTable[(edc ^ sector[pos++]) & 0xFF];
        calculatedEdc = edc;

        *edc_correct = calculatedEdc == storedEdc;

        if(!*edc_correct)
        {
            fprintf(stderr,
                    "Mode 2 sector at address: %2X:%2X:%2X, got CRC 0x%8X expected 0x%8X\n",
                    sector[0x00C],
                    sector[0x00D],
                    sector[0x00E],
                    calculatedEdc,
                    storedEdc);
        }

        if(!*ecc_p_correct)
        {
            fprintf(stderr,
                    "Mode 2 sector at address: %2X:%2X:%2X, fails ECC P check.\n",
                    sector[0x00C],
                    sector[0x00D],
                    sector[0x00E]);
        }

        if(!*ecc_q_correct)
        {
            fprintf(stderr,
                    "Mode 2 sector at address: %2X:%2X:%2X, fails ECC Q check.\n",
                    sector[0x00C],
                    sector[0x00D],
                    sector[0x00E]);
        }

        return *edc_correct && *ecc_p_correct && *ecc_q_correct;
    }

    fprintf(stderr,
            "Unknown mode %d sector at address: %2X:%2X:%2X",
            sector[0x00F],
            sector[0x00C],
            sector[0x00D],
            sector[0x00E]);

    return false;
}