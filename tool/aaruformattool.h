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
#ifndef LIBAARUFORMAT_TOOL_AARUFORMATTOOL_H_
#define LIBAARUFORMAT_TOOL_AARUFORMATTOOL_H_

#include <stdbool.h>

#include <aaruformat.h>

int   identify(char* path);
int   info(char* path);
char* byte_array_to_hex_string(const unsigned char* array, int array_size);
int   read(unsigned long long sector_no, char* path);
int   printhex(unsigned char* array, unsigned int length, int width, bool color);
int   read_long(unsigned long long sector_no, char* path);
int   verify(char* path);
int   verify_sectors(char* path);
bool  check_cd_sector_channel(CdEccContext* context,
                              uint8_t*      sector,
                              bool*         unknown,
                              bool*         has_edc,
                              bool*         edc_correct,
                              bool*         has_ecc_p,
                              bool*         ecc_p_correct,
                              bool*         has_ecc_q,
                              bool*         ecc_q_correct);

#endif // LIBAARUFORMAT_TOOL_AARUFORMATTOOL_H_
