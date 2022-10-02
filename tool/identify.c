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

#include <aaruformat.h>

int identify(char* path)
{
    int ret;

    ret = aaruf_identify(path);

    if(ret < 0) { printf("Error %d while trying to identify file.\n", ret); }
    else if(ret == 0)
    {
        printf("The specified file is not a support AaruFormat image.\n");
        ret = 1;
    }
    else if(ret == 100)
    {
        printf("The specified file is a supported AaruFormat image.\n");
        ret = 0;
    }
    else
    {
        printf("The library has not enough confidence this is an AaruFormat image, and this shall NEVER happen.\n");
        ret = 2;
    }

    return ret;
}