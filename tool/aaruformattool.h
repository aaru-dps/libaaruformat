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
#ifndef LIBAARUFORMAT_TOOL_AARUFORMATTOOL_H_
#define LIBAARUFORMAT_TOOL_AARUFORMATTOOL_H_

#include <stdbool.h>

#include <aaruformat.h>

int         identify(const char *path);
int         info(const char *path);
char       *byte_array_to_hex_string(const unsigned char *array, int array_size);
const char *media_type_to_string(MediaType type);
const char *media_tag_type_to_string(int32_t type);
const char *sector_tag_type_to_string(int32_t type);
const char *data_type_to_string(uint16_t type);
int         read_sector(unsigned long long sector_no, const char *path);
int         printhex(unsigned char *array, unsigned int length, int width, bool color);
int         read_long(unsigned long long sector_no, const char *path);
int         verify(const char *path);
int         verify_sectors(const char *path);
bool        check_cd_sector_channel(CdEccContext *context, const uint8_t *sector, bool *unknown, bool *has_edc,
                                    bool *edc_correct, bool *has_ecc_p, bool *ecc_p_correct, bool *has_ecc_q,
                                    bool *ecc_q_correct);
int         compare(const char *path1, const char *path2);
int         cli_compare(const char *path1, const char *path2, bool use_long);
int         convert(const char *input_path, const char *output_path, bool use_long);
int         upgrade_ddt_to_alpha21(const char *path);
int         inject_media_tag(const char *tag_type, const char *media_tag_file, const char *image_file);
int convert_ps3(const char *input_path, const char *output_path, const char *disc_key_hex, const char *data1_key_hex,
                const char *ird_path);
int convert_wiiu(const char *input_path, const char *output_path, const char *disc_key_hex);

#endif  // LIBAARUFORMAT_TOOL_AARUFORMATTOOL_H_
