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
 *
 * convert-ngcw command: converts Nintendo GameCube/Wii disc ISO or AaruFormat
 * images to AaruFormat with decrypted sector storage and junk removal.
 */

#include <stdio.h>

#include "../aaruformattool.h"

int convert_ngcw(const char *input_path, const char *output_path)
{
    (void)input_path;
    (void)output_path;

    fprintf(stderr, "convert-ngcw: not yet implemented\n");
    return -1;
}
