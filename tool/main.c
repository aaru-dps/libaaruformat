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

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>
#endif

#include <stdio.h>
#include <string.h>

#include "commands.h"
#include "usage.h"

#if defined(_WIN32) || defined(_WIN64)
// The narrow argv is in the ANSI code page, so rebuild it as UTF-8 from the wide command line.
// The converted array is kept for the lifetime of the process.
static char **get_utf8_argv(int *argc)
{
    int       wargc = 0;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);

    if(wargv == NULL) return NULL;

    char **utf8_argv = calloc((size_t)wargc + 1, sizeof(char *));

    if(utf8_argv == NULL)
    {
        LocalFree(wargv);
        return NULL;
    }

    for(int i = 0; i < wargc; i++)
    {
        const int length = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL, NULL);

        if(length > 0) utf8_argv[i] = malloc((size_t)length);

        if(utf8_argv[i] == NULL ||
           WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, utf8_argv[i], length, NULL, NULL) <= 0)
        {
            for(int j = 0; j <= i; j++) free(utf8_argv[j]);
            free(utf8_argv);
            LocalFree(wargv);
            return NULL;
        }
    }

    LocalFree(wargv);
    *argc = wargc;
    return utf8_argv;
}
#endif

int main(int argc, char *argv[])
{
#if defined(_WIN32) || defined(_WIN64)
    SetConsoleOutputCP(CP_UTF8);

    char **utf8_argv = get_utf8_argv(&argc);

    if(utf8_argv != NULL) argv = utf8_argv;
#endif

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
