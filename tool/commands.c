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

#include <argtable3.h>
#include <stdbool.h>
#include <stdio.h>

#include "aaruformattool.h"
#include "commands.h"
#include "usage.h"

int cmd_identify(int argc, char *argv[])
{
    struct arg_str *filename   = arg_str1(NULL, NULL, "<filename>", "Image to identify");
    struct arg_end *end        = arg_end(10);
    void           *argtable[] = {filename, end};

    if(arg_parse(argc, argv, argtable) > 0)
    {
        arg_print_errors(stderr, end, "identify");
        usage_identify();
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return -1;
    }

    int result = identify(filename->sval[0]);
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return result;
}

int cmd_info(int argc, char *argv[])
{
    struct arg_str *filename   = arg_str1(NULL, NULL, "<filename>", "Image to inspect");
    struct arg_end *end        = arg_end(10);
    void           *argtable[] = {filename, end};

    if(arg_parse(argc, argv, argtable) > 0)
    {
        arg_print_errors(stderr, end, "info");
        usage_info();
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return -1;
    }

    int result = info(filename->sval[0]);
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return result;
}

int cmd_read_common(int argc, char *argv[], bool long_mode)
{
    struct arg_int *sector     = arg_int1(NULL, NULL, "<sector>", "Sector number");
    struct arg_str *filename   = arg_str1(NULL, NULL, "<filename>", "Image file");
    struct arg_end *end        = arg_end(10);
    void           *argtable[] = {sector, filename, end};

    if(arg_parse(argc, argv, argtable) > 0 || sector->ival[0] < 0)
    {
        arg_print_errors(stderr, end, long_mode ? "read_long" : "read");
        long_mode ? usage_read_long() : usage_read();
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return -1;
    }

    int result = long_mode ? read_long(sector->ival[0], filename->sval[0]) : read(sector->ival[0], filename->sval[0]);

    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return result;
}

int cmd_read(int argc, char *argv[]) { return cmd_read_common(argc, argv, false); }

int cmd_read_long(int argc, char *argv[]) { return cmd_read_common(argc, argv, true); }

int cmd_verify_common(int argc, char *argv[], bool sectors_mode)
{
    struct arg_str *filename   = arg_str1(NULL, NULL, "<filename>", "Image file");
    struct arg_end *end        = arg_end(10);
    void           *argtable[] = {filename, end};

    if(arg_parse(argc, argv, argtable) > 0)
    {
        arg_print_errors(stderr, end, sectors_mode ? "verify_sectors" : "verify");
        sectors_mode ? usage_verify_sectors() : usage_verify();
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return -1;
    }

    int result = sectors_mode ? verify_sectors(filename->sval[0]) : verify(filename->sval[0]);

    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return result;
}

int cmd_verify(int argc, char *argv[]) { return cmd_verify_common(argc, argv, false); }

int cmd_verify_sectors(int argc, char *argv[]) { return cmd_verify_common(argc, argv, true); }

Command commands[] = {
    {      "identify",       cmd_identify},
    {          "info",           cmd_info},
    {          "read",           cmd_read},
    {     "read_long",      cmd_read_long},
    {        "verify",         cmd_verify},
    {"verify_sectors", cmd_verify_sectors},
};

const size_t num_commands = sizeof(commands) / sizeof(commands[0]);
