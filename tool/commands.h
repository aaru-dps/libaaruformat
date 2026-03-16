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

#ifndef LIBAARUFORMAT_COMMANDS_H
#define LIBAARUFORMAT_COMMANDS_H

typedef int (*command_func)(int argc, char *argv[]);

typedef struct
{
    const char  *verb;
    command_func handler;
} Command;

extern Command      commands[];
extern const size_t num_commands;

// Command wrappers
int cmd_identify(int argc, char *argv[]);
int cmd_info(int argc, char *argv[]);
int cmd_read(int argc, char *argv[]);
int cmd_read_long(int argc, char *argv[]);
int cmd_verify(int argc, char *argv[]);
int cmd_verify_sectors(int argc, char *argv[]);
int cmd_compare(int argc, char *argv[]);
int cmd_cli_compare(int argc, char *argv[]);
int cmd_convert(int argc, char *argv[]);
int cmd_upgrade_ddt_to_alpha21(int argc, char *argv[]);
int cmd_inject_media_tag(int argc, char *argv[]);
int cmd_convert_ps3(int argc, char *argv[]);

#endif  // LIBAARUFORMAT_COMMANDS_H
