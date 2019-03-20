// /***************************************************************************
// The Disc Image Chef
// ----------------------------------------------------------------------------
//
// Filename       : decsl.h
// Author(s)      : Natalia Portillo <claunia@claunia.com>
//
// Component      : libdicformat.
//
// --[ Description ] ----------------------------------------------------------
//
//     Declares exportable functions.
//
// --[ License ] --------------------------------------------------------------
//
//     This library is free software; you can redistribute it and/or modify
//     it under the terms of the GNU Lesser General Public License as
//     published by the Free Software Foundation; either version 2.1 of the
//     License, or (at your option) any later version.
//
//     This library is distributed in the hope that it will be useful, but
//     WITHOUT ANY WARRANTY; without even the implied warranty of
//     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
//     Lesser General Public License for more details.
//
//     You should have received a copy of the GNU Lesser General Public
//     License along with this library; if not, see <http://www.gnu.org/licenses/>.
//
// ----------------------------------------------------------------------------
// Copyright © 2011-2019 Natalia Portillo
// ****************************************************************************/

#ifndef LIBDICFORMAT_DECLS_H
#define LIBDICFORMAT_DECLS_H

#include <stdint.h>
#include <stdio.h>

int identify(const char *filename);

int identifyStream(FILE *imageStream);

void *open(const char *filepath);

int close(void *context);

uint8_t *read_media_tag(void *context, int tag);

void *crc64_init(uint64_t polynomial, uint64_t seed);

void *crc64_init_ecma(void);

void crc64_update(void *context, const char *data, size_t len);

uint64_t crc64_final(void *context);

uint64_t crc64_data(const char *data, size_t len, uint64_t polynomial, uint64_t seed);

uint64_t crc64_data_ecma(const char *data, size_t len);

#endif //LIBDICFORMAT_DECLS_H
