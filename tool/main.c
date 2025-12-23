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
 */

#include <stdio.h>
#include <string.h>

#include "commands.h"
#include "usage.h"

int main(int argc, char *argv[])
{
    print_banner();

    if(argc < 2)
    {
        usage();
        return -1;
    }

    const char *verb = argv[1];
    argc--;
    argv++;  // Shift to pass only args to verb handlers

    for(size_t i = 0; i < num_commands; ++i)
    {
        if(strcmp(commands[i].verb, verb) == 0) { return commands[i].handler(argc, argv); }
    }

    fprintf(stderr, "Unknown verb: %s\n", verb);
    usage();
    return -1;
}
