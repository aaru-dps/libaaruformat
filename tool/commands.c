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

    const int result = identify(filename->sval[0]);
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

    const int result = info(filename->sval[0]);
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return result;
}

int cmd_compare(int argc, char *argv[])
{
    struct arg_str *filename1  = arg_str1(NULL, NULL, "<filename1>", "First image to compare");
    struct arg_str *filename2  = arg_str1(NULL, NULL, "<filename2>", "Second image to compare");
    struct arg_end *end        = arg_end(10);
    void           *argtable[] = {filename1, filename2, end};

    if(arg_parse(argc, argv, argtable) > 0)
    {
        arg_print_errors(stderr, end, "compare");
        usage_compare();
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return -1;
    }

    const int result = compare(filename1->sval[0], filename2->sval[0]);
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return result;
}

int cmd_cli_compare(int argc, char *argv[])
{
    struct arg_lit *use_long   = arg_lit0("l", NULL, "Use long sector read/write (includes tags and metadata)");
    struct arg_str *filename1  = arg_str1(NULL, NULL, "<filename1>", "First image to compare");
    struct arg_str *filename2  = arg_str1(NULL, NULL, "<filename2>", "Second image to compare");
    struct arg_end *end        = arg_end(10);
    void           *argtable[] = {use_long, filename1, filename2, end};

    if(arg_parse(argc, argv, argtable) > 0)
    {
        arg_print_errors(stderr, end, "cli-compare");
        usage_cli_compare();
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return -1;
    }

    const int result = cli_compare(filename1->sval[0], filename2->sval[0], use_long->count > 0);
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

    const int result =
        long_mode ? read_long(sector->ival[0], filename->sval[0]) : read_sector(sector->ival[0], filename->sval[0]);

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

    const int result = sectors_mode ? verify_sectors(filename->sval[0]) : verify(filename->sval[0]);

    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return result;
}

int cmd_verify(int argc, char *argv[]) { return cmd_verify_common(argc, argv, false); }

int cmd_verify_sectors(int argc, char *argv[]) { return cmd_verify_common(argc, argv, true); }

int cmd_convert(int argc, char *argv[])
{
    struct arg_lit *use_long        = arg_lit0("l", NULL, "Use long sector read/write (includes tags and metadata)");
    struct arg_str *input_filename  = arg_str1(NULL, NULL, "<input>", "Input image file");
    struct arg_str *output_filename = arg_str1(NULL, NULL, "<output>", "Output image file");
    struct arg_end *end             = arg_end(10);
    void           *argtable[]      = {use_long, input_filename, output_filename, end};

    if(arg_parse(argc, argv, argtable) > 0)
    {
        arg_print_errors(stderr, end, "convert");
        usage_convert();
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return -1;
    }

    const int result = convert(input_filename->sval[0], output_filename->sval[0], use_long->count > 0);
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return result;
}

int cmd_upgrade_ddt_to_alpha21(int argc, char *argv[])
{
    struct arg_str *filename   = arg_str1(NULL, NULL, "<filename>", "Image to upgrade");
    struct arg_end *end        = arg_end(10);
    void           *argtable[] = {filename, end};

    if(arg_parse(argc, argv, argtable) > 0)
    {
        arg_print_errors(stderr, end, "upgrade-ddt-to-alpha21");
        usage_upgrade_ddt_to_alpha21();
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return -1;
    }

    const int result = upgrade_ddt_to_alpha21(filename->sval[0]);
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return result;
}

int cmd_inject_media_tag(int argc, char *argv[])
{
    struct arg_str *tag_type       = arg_str1(NULL, NULL, "<tag-type>", "Media tag type to inject");
    struct arg_str *media_tag_file = arg_str1(NULL, NULL, "<media-tag-file>", "Path to media tag data file");
    struct arg_str *image_file     = arg_str1(NULL, NULL, "<image-file>", "Path to AaruFormat image");
    struct arg_end *end            = arg_end(10);
    void           *argtable[]     = {tag_type, media_tag_file, image_file, end};

    if(arg_parse(argc, argv, argtable) > 0)
    {
        arg_print_errors(stderr, end, "inject-media-tag");
        usage_inject_media_tag();
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return -1;
    }

    const int result = inject_media_tag(tag_type->sval[0], media_tag_file->sval[0], image_file->sval[0]);
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return result;
}

int cmd_convert_ps3(int argc, char *argv[])
{
    struct arg_str *input_filename  = arg_str1(NULL, NULL, "<input>", "Input ISO or AaruFormat image");
    struct arg_str *output_filename = arg_str1(NULL, NULL, "<output>", "Output AaruFormat image");
    struct arg_str *disc_key_arg    = arg_str0(NULL, "disc-key", "<hex>", "32-char hex disc key");
    struct arg_str *data1_key_arg   = arg_str0(NULL, "data1-key", "<hex>", "32-char hex data1 key");
    struct arg_str *ird_arg         = arg_str0(NULL, "ird", "<path>", "Path to IRD file");
    struct arg_end *end             = arg_end(10);
    void           *argtable[]      = {input_filename, output_filename, disc_key_arg, data1_key_arg, ird_arg, end};

    if(arg_parse(argc, argv, argtable) > 0)
    {
        arg_print_errors(stderr, end, "convert-ps3");
        usage_convert_ps3();
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return -1;
    }

    const char *disc_key_hex  = disc_key_arg->count > 0 ? disc_key_arg->sval[0] : NULL;
    const char *data1_key_hex = data1_key_arg->count > 0 ? data1_key_arg->sval[0] : NULL;
    const char *ird_path      = ird_arg->count > 0 ? ird_arg->sval[0] : NULL;

    const int result =
        convert_ps3(input_filename->sval[0], output_filename->sval[0], disc_key_hex, data1_key_hex, ird_path);
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return result;
}

int cmd_convert_wiiu(int argc, char *argv[])
{
    struct arg_str *input_filename  = arg_str1(NULL, NULL, "<input>", "Input WUD, WUX, or AaruFormat image");
    struct arg_str *output_filename = arg_str1(NULL, NULL, "<output>", "Output AaruFormat image");
    struct arg_str *disc_key_arg    = arg_str0(NULL, "disc-key", "<hex>", "32-char hex disc key");
    struct arg_end *end             = arg_end(10);
    void           *argtable[]      = {input_filename, output_filename, disc_key_arg, end};

    if(arg_parse(argc, argv, argtable) > 0)
    {
        arg_print_errors(stderr, end, "convert-wiiu");
        usage_convert_wiiu();
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return -1;
    }

    const char *disc_key_hex = disc_key_arg->count > 0 ? disc_key_arg->sval[0] : NULL;

    const int result = convert_wiiu(input_filename->sval[0], output_filename->sval[0], disc_key_hex);
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return result;
}

int cmd_convert_ngcw(int argc, char *argv[])
{
    struct arg_str *input_filename  = arg_str1(NULL, NULL, "<input>", "Input ISO or AaruFormat image");
    struct arg_str *output_filename = arg_str1(NULL, NULL, "<output>", "Output AaruFormat image");
    struct arg_end *end             = arg_end(10);
    void           *argtable[]      = {input_filename, output_filename, end};

    if(arg_parse(argc, argv, argtable) > 0)
    {
        arg_print_errors(stderr, end, "convert-ngcw");
        usage_convert_ngcw();
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return -1;
    }

    const int result = convert_ngcw(input_filename->sval[0], output_filename->sval[0]);
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return result;
}

Command commands[] = {
    {              "identify",               cmd_identify},
    {                  "info",                   cmd_info},
    {                  "read",                   cmd_read},
    {             "read_long",              cmd_read_long},
    {                "verify",                 cmd_verify},
    {        "verify_sectors",         cmd_verify_sectors},
    {               "compare",                cmd_compare},
    {           "cli-compare",            cmd_cli_compare},
    {               "convert",                cmd_convert},
    {"upgrade-ddt-to-alpha21", cmd_upgrade_ddt_to_alpha21},
    {      "inject-media-tag",       cmd_inject_media_tag},
    {           "convert-ps3",            cmd_convert_ps3},
    {          "convert-wiiu",           cmd_convert_wiiu},
    {          "convert-ngcw",           cmd_convert_ngcw},
};

const size_t num_commands = sizeof(commands) / sizeof(commands[0]);
