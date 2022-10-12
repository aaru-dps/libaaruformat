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
#include <stddef.h>
#include <stdio.h>
#include <string.h>

int printhex(unsigned char* array, unsigned int length, int width, bool color)
{
    char          length_hex[17];
    char          str[256];
    char          format[256];
    int           rows;
    int           last;
    int           offset_length;
    int           str_length = strlen("Offset");
    int           i;
    int           b;
    int           last_bytes;
    int           last_spaces;
    int           j;
    unsigned char v;

    if(array == NULL) return 0;

    memset(length_hex, 0, 17);
    memset(format, 0, 256);

    sprintf(length_hex, "%X", length);

    rows          = length / width;
    last          = length % width;
    offset_length = strlen(length_hex);

    if(last > 0) rows++;

    if(last == 0) last = width;

    if(offset_length < str_length) offset_length = str_length;

    while(str_length < offset_length) str_length++;

    if(str_length > 255) str_length = 255;

    memset(str, ' ', str_length);
    str[str_length + 1] = 0;
    strcpy(str, "Offset");

    if(color) printf("\x1b[36m");

    printf("%s", str);
    printf("  ");

    for(i = 0; i < width; i++) printf(" %02x", i);

    if(color) printf("\x1b[0m");

    printf("\n");

    b = 0;

    sprintf(format, "%%0%dX", offset_length);

    for(i = 0; i < rows; i++)
    {
        if(color) printf("\x1b[36m");

        printf(format, b);

        if(color) printf("\x1b[0m");

        printf("  ");

        last_bytes  = i == rows - 1 ? last : width;
        last_spaces = width - last_bytes;

        for(j = 0; j < last_bytes; j++)
        {
            printf(" %02X", array[b]);
            b++;
        }

        for(j = 0; j < last_spaces; j++) printf("   ");

        b -= last_bytes;
        printf("   ");

        for(j = 0; j < last_bytes; j++)
        {
            v = array[b];
            printf("%c", v > 31 && v < 127 || v > 159 ? v : '.');
            b++;
        }

        printf("\n");
    }

    return 0;
}