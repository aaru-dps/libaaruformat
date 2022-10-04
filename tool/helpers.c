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

#include <stdlib.h>
#include <string.h>

#include "aaruformattool.h"

char* byte_array_to_hex_string(const unsigned char* array, int array_size)
{
    char* hex_string = NULL;
    int   j;
    int   i;

    hex_string = malloc(array_size * 2 + 1);

    if(hex_string == NULL) return NULL;

    j = 0;
    for(i = 0; i < array_size; i++)
    {
        hex_string[j] = (array[i] >> 4) + '0';
        if(hex_string[j] > '9') hex_string[j] += 0x7;

        j++;

        hex_string[j] = (array[i] & 0xF) + '0';
        if(hex_string[j] > '9') hex_string[j] += 0x7;

        j++;
    }

    hex_string[j] = 0;

    return hex_string;
}
