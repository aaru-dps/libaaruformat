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
#include <stdbool.h>

int identify(const char *filename);

int identifyStream(FILE *imageStream);

void *open(const char *filepath);

int close(void *context);

int32_t read_media_tag(void *context, uint8_t *data, int32_t tag, uint32_t *length);

void *crc64_init(uint64_t polynomial, uint64_t seed);

void *crc64_init_ecma(void);

void crc64_update(void *context, const uint8_t *data, size_t len);

uint64_t crc64_final(void *context);

uint64_t crc64_data(const uint8_t *data, size_t len, uint64_t polynomial, uint64_t seed);

uint64_t crc64_data_ecma(const uint8_t *data, size_t len);

int32_t read_sector(void *context, uint64_t sectorAddress, uint8_t *data, uint32_t *length);

int32_t cst_transform(const uint8_t *interleaved, uint8_t *sequential, size_t length);

int32_t cst_untransform(const uint8_t *sequential, uint8_t *interleaved, size_t length);

void *ecc_cd_init();

bool ecc_cd_is_suffix_correct(void *context, const uint8_t *sector);

bool ecc_cd_is_suffix_correct_mode2(void *context, const uint8_t *sector);

bool ecc_cd_check(void *context,
                  const uint8_t *address,
                  const uint8_t *data,
                  uint32_t majorCount,
                  uint32_t minorCount,
                  uint32_t majorMult,
                  uint32_t minorInc,
                  const uint8_t *ecc,
                  int32_t addressOffset,
                  int32_t dataOffset,
                  int32_t eccOffset);

void ecc_cd_write(void *context,
                  const uint8_t *address,
                  const uint8_t *data,
                  uint32_t majorCount,
                  uint32_t minorCount,
                  uint32_t majorMult,
                  uint32_t minorInc,
                  uint8_t *ecc,
                  int32_t addressOffset,
                  int32_t dataOffset,
                  int32_t eccOffset);

void ecc_cd_write_sector(void *context,
                         const uint8_t *address,
                         const uint8_t *data,
                         uint8_t *ecc,
                         int32_t addressOffset,
                         int32_t dataOffset,
                         int32_t eccOffset);

void cd_lba_to_msf(int64_t pos, uint8_t *minute, uint8_t *second, uint8_t *frame);

void ecc_cd_reconstruct_prefix(uint8_t *sector, uint8_t type, int64_t lba);

void ecc_cd_reconstruct(void *context, uint8_t *sector, uint8_t type);

uint32_t edc_cd_compute(void *context, uint32_t edc, const uint8_t *src, int size, int pos);

#endif //LIBDICFORMAT_DECLS_H
