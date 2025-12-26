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

#include "aaruformat.h"
#include "usage.h"
#include "version.h"  // Optional if you want a central place for version macros

void print_banner()
{
    printf("AaruFormat Tool version %d.%d\n", AARUFORMAT_TOOL_MAJOR_VERSION, AARUFORMAT_TOOL_MINOR_VERSION);
    printf("Copyright (C) 2019-2026 Natalia Portillo\n");
    printf("libaaruformat version %d.%d\n\n", LIBAARUFORMAT_MAJOR_VERSION, LIBAARUFORMAT_MINOR_VERSION);
}

void usage()
{
    printf("\nUsage:\n");
    printf("  aaruformattool <verb> [arguments]\n\n");
    printf("Available verbs:\n");
    printf("  identify         Identifies if the indicated file is a supported AaruFormat image.\n");
    printf("  info             Prints information about a given AaruFormat image.\n");
    printf("  read             Reads a sector and prints it out on screen.\n");
    printf("  read_long        Reads a sector with all its prefixes and suffixes.\n");
    printf("  verify           Verifies the integrity of blocks in an image.\n");
    printf("  verify_sectors   Verifies the integrity of all sectors in an image.\n");
    printf("  compare          Compares two AaruFormat images.\n");
    printf("  cli-compare      Compares two AaruFormat images sector by sector (CLI mode).\n");
    printf("  convert          Converts an AaruFormat image to another AaruFormat image.\n");
    printf("  upgrade-ddt-to-alpha21  Upgrades a DDT image to alpha21 format.\n\n");
    printf("For help with any verb, run:\n");
    printf("  aaruformattool <verb> --help\n");
}

void usage_identify()
{
    printf("\nUsage:\n");
    printf("  aaruformattool identify <filename>\n\n");
    printf("Identifies if the file is a supported AaruFormat image.\n");
    printf("Arguments:\n");
    printf("  <filename>       Path to the image file.\n");
}

void usage_info()
{
    printf("\nUsage:\n");
    printf("  aaruformattool info <filename>\n\n");
    printf("Prints metadata about the image.\n");
    printf("Arguments:\n");
    printf("  <filename>       Path to AaruFormat image.\n");
}

void usage_read()
{
    printf("\nUsage:\n");
    printf("  aaruformattool read <sector_number> <filename>\n\n");
    printf("Reads a sector and prints it out.\n");
    printf("Arguments:\n");
    printf("  <sector_number>  Sector number to read.\n");
    printf("  <filename>       Path to image file.\n");
}

void usage_read_long()
{
    printf("\nUsage:\n");
    printf("  aaruformattool read_long <sector_number> <filename>\n\n");
    printf("Reads sector with all metadata and prints it out.\n");
    printf("Arguments:\n");
    printf("  <sector_number>  Sector number to read.\n");
    printf("  <filename>       Path to image file.\n");
}

void usage_verify()
{
    printf("\nUsage:\n");
    printf("  aaruformattool verify <filename>\n\n");
    printf("Checks block-level integrity.\n");
    printf("Arguments:\n");
    printf("  <filename>       Path to image file.\n");
}

void usage_verify_sectors()
{
    printf("\nUsage:\n");
    printf("  aaruformattool verify_sectors <filename>\n\n");
    printf("Checks sector-level integrity.\n");
    printf("Arguments:\n");
    printf("  <filename>       Path to image file.\n");
}

void usage_compare()
{
    printf("\nUsage:\n");
    printf("  aaruformattool compare <filename1> <filename2>\n\n");
    printf("Compares two AaruFormat images.\n");
    printf("Arguments:\n");
    printf("  <filename1>      Path to first image file.\n");
    printf("  <filename2>      Path to second image file.\n");
}

void usage_cli_compare()
{
    printf("\nUsage:\n");
    printf("  aaruformattool cli-compare [-l] <filename1> <filename2>\n\n");
    printf("Compares two AaruFormat images sector by sector and lists all different sectors.\n");
    printf("Arguments:\n");
    printf("  <filename1>      Path to first image file.\n");
    printf("  <filename2>      Path to second image file.\n");
    printf("Options:\n");
    printf("  -l               Use long sector read (includes tags and metadata).\n");
}

void usage_convert()
{
    printf("\nUsage:\n");
    printf("  aaruformattool convert [-l] <input> <output>\n\n");
    printf("Converts an AaruFormat image by reading all sectors from input and writing them to output.\n");
    printf("Arguments:\n");
    printf("  <input>          Path to input image file.\n");
    printf("  <output>         Path to output image file.\n");
    printf("Options:\n");
    printf("  -l               Use long sector read/write (includes tags and metadata).\n");
}

void usage_upgrade_ddt_to_alpha21()
{
    printf("\nUsage:\n");
    printf("  aaruformattool upgrade-ddt-to-alpha21 <filename>\n\n");
    printf("Upgrades a DDT image to alpha21 format.\n");
    printf("Arguments:\n");
    printf("  <filename>       Path to the DDT image file to upgrade.\n");
}
