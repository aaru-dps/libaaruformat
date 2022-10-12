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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <aaruformat.h>

#include "aaruformattool.h"
#include "main.h"

void usage()
{
    printf("\n");
    printf("Usage:\n");
    printf("aaruformattool <verb> [arguments]\n");
    printf("\n");
    printf("Available verbs:\n");
    printf("\tidentify\tIdentifies if the indicated file is a supported AaruFormat image.\n");
    printf("\tinfo\tPrints information about a given AaruFormat image.\n");
    printf("\tread\tReads a sector and prints it out on screen.\n");
    printf("\n");
    printf("For help on the verb invoke the tool with the verb and no arguments.\n");
}

void usage_identify()
{
    printf("\n");
    printf("Usage:\n");
    printf("aaruformattool identify <filename>\n");
    printf("Identifies if the indicated file is a support AaruFormat image.\n");
    printf("\n");
    printf("Arguments:\n");
    printf("\t<filename>\tPath to file to identify if it is a supported AaruFormat image.\n");
}

void usage_info()
{
    printf("\n");
    printf("Usage:\n");
    printf("aaruformattool info <filename>\n");
    printf("Prints information about a given AaruFormat image.\n");
    printf("\n");
    printf("Arguments:\n");
    printf("\t<filename>\tPath to AaruFormat image to print information from.\n");
}

void usage_read()
{
    printf("\n");
    printf("Usage:\n");
    printf("aaruformattool read <sector_number> <filename>\n");
    printf("Reads a sector and prints it out on screen.\n");
    printf("\n");
    printf("Arguments:\n");
    printf("\t<sector_number>\tSector number to read and print out.\n");
    printf("\t<filename>\tPath to AaruFormat image to print information from.\n");
}

int main(int argc, char* argv[])
{
    uint64_t sector_no = 0;

    printf("AaruFormat Tool version %d.%d\n", AARUFORMAT_TOOL_MAJOR_VERSION, AARUFORMAT_TOOL_MINOR_VERSION);
    printf("Copyright (C) 2019-2022 Natalia Portillo\n");
    printf("libaaruformat version %d.%d\n", LIBAARUFORMAT_MAJOR_VERSION, LIBAARUFORMAT_MINOR_VERSION);
    printf("\n");

    if(argc < 2)
    {
        usage();
        return -1;
    }

    if(strncmp(argv[1], "identify", strlen("identify")) == 0)
    {
        if(argc == 2)
        {
            usage_identify();
            return -1;
        }

        if(argc > 3)
        {
            fprintf(stderr, "Invalid number of arguments\n");
            usage_identify();
            return -1;
        }

        return identify(argv[2]);
    }

    if(strncmp(argv[1], "info", strlen("info")) == 0)
    {
        if(argc == 2)
        {
            usage_info();
            return -1;
        }

        if(argc > 3)
        {
            fprintf(stderr, "Invalid number of arguments\n");
            usage_info();
            return -1;
        }

        return info(argv[2]);
    }

    if(strncmp(argv[1], "read", strlen("read")) == 0)
    {
        if(argc < 4)
        {
            usage_read();
            return -1;
        }

        if(argc > 5)
        {
            fprintf(stderr, "Invalid number of arguments\n");
            usage_read();
            return -1;
        }

        errno = 0;

        sector_no = strtoll(argv[2], NULL, 10);

        if(errno != 0)
        {
            fprintf(stderr, "Invalid sector number\n");
            usage_read();
            return -1;
        }

        return read(sector_no, argv[3]);
    }

    return 0;
}
