// /***************************************************************************
// Aaru Data Preservation Suite
// ----------------------------------------------------------------------------
//
// Filename       : decls.h
// Author(s)      : Natalia Portillo <claunia@claunia.com>
//
// Component      : libaaruformat.
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
// Copyright © 2011-2020 Natalia Portillo
// ****************************************************************************/

#ifndef LIBAARUFORMAT_DECLS_H
#define LIBAARUFORMAT_DECLS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

#if defined(_WIN32)
#define AARU_CALL __stdcall
#define AARU_EXPORT EXTERNC __declspec(dllexport)
#define AARU_LOCAL
#ifndef PATH_MAX
#define PATH_MAX _MAX_PATH
#endif
#else
#define AARU_CALL
#if defined(__APPLE__)
#define AARU_EXPORT EXTERNC __attribute__((visibility("default")))
#define AARU_LOCAL __attribute__((visibility("hidden")))
#else
#if __GNUC__ >= 4
#define AARU_EXPORT EXTERNC __attribute__((visibility("default")))
#define AARU_LOCAL __attribute__((visibility("hidden")))
#else
#define AARU_EXPORT EXTERNC
#define AARU_LOCAL
#endif
#endif
#endif

#ifdef _MSC_VER
#define FORCE_INLINE static inline
#else
#define FORCE_INLINE static inline __attribute__((always_inline))
#endif

AARU_EXPORT int AARU_CALL identify(const char* filename);

AARU_EXPORT int AARU_CALL identifyStream(FILE* imageStream);

AARU_EXPORT void* AARU_CALL open(const char* filepath);

AARU_EXPORT int AARU_CALL close(void* context);

AARU_EXPORT int32_t AARU_CALL read_media_tag(void* context, uint8_t* data, int32_t tag, uint32_t* length);

AARU_EXPORT void* AARU_CALL crc64_init(uint64_t polynomial, uint64_t seed);

AARU_EXPORT void* AARU_CALL crc64_init_ecma(void);

AARU_EXPORT void AARU_CALL crc64_update(void* context, const uint8_t* data, size_t len);

AARU_EXPORT uint64_t AARU_CALL crc64_final(void* context);

AARU_EXPORT uint64_t AARU_CALL crc64_data(const uint8_t* data, size_t len, uint64_t polynomial, uint64_t seed);

AARU_EXPORT uint64_t AARU_CALL crc64_data_ecma(const uint8_t* data, size_t len);

AARU_EXPORT int32_t AARU_CALL read_sector(void* context, uint64_t sectorAddress, uint8_t* data, uint32_t* length);

AARU_EXPORT int32_t AARU_CALL cst_transform(const uint8_t* interleaved, uint8_t* sequential, size_t length);

AARU_EXPORT int32_t AARU_CALL cst_untransform(const uint8_t* sequential, uint8_t* interleaved, size_t length);

AARU_LOCAL void* AARU_CALL ecc_cd_init();

AARU_EXPORT bool AARU_CALL ecc_cd_is_suffix_correct(void* context, const uint8_t* sector);

AARU_EXPORT bool AARU_CALL ecc_cd_is_suffix_correct_mode2(void* context, const uint8_t* sector);

AARU_EXPORT bool AARU_CALL ecc_cd_check(void*          context,
                                        const uint8_t* address,
                                        const uint8_t* data,
                                        uint32_t       majorCount,
                                        uint32_t       minorCount,
                                        uint32_t       majorMult,
                                        uint32_t       minorInc,
                                        const uint8_t* ecc,
                                        int32_t        addressOffset,
                                        int32_t        dataOffset,
                                        int32_t        eccOffset);

AARU_EXPORT void AARU_CALL ecc_cd_write(void*          context,
                                        const uint8_t* address,
                                        const uint8_t* data,
                                        uint32_t       majorCount,
                                        uint32_t       minorCount,
                                        uint32_t       majorMult,
                                        uint32_t       minorInc,
                                        uint8_t*       ecc,
                                        int32_t        addressOffset,
                                        int32_t        dataOffset,
                                        int32_t        eccOffset);

AARU_EXPORT void AARU_CALL ecc_cd_write_sector(void*          context,
                                               const uint8_t* address,
                                               const uint8_t* data,
                                               uint8_t*       ecc,
                                               int32_t        addressOffset,
                                               int32_t        dataOffset,
                                               int32_t        eccOffset);

AARU_LOCAL void AARU_CALL cd_lba_to_msf(int64_t pos, uint8_t* minute, uint8_t* second, uint8_t* frame);

AARU_EXPORT void AARU_CALL ecc_cd_reconstruct_prefix(uint8_t* sector, uint8_t type, int64_t lba);

AARU_EXPORT void AARU_CALL ecc_cd_reconstruct(void* context, uint8_t* sector, uint8_t type);

AARU_EXPORT uint32_t AARU_CALL edc_cd_compute(void* context, uint32_t edc, const uint8_t* src, int size, int pos);

AARU_EXPORT int32_t AARU_CALL
    read_track_sector(void* context, uint8_t* data, uint64_t sectorAddress, uint32_t* length, uint8_t track);

AARU_LOCAL int32_t AARU_CALL GetMediaTagTypeForDataType(int32_t type);

AARU_LOCAL int32_t AARU_CALL GetXmlMediaType(int32_t type);

#endif // LIBAARUFORMAT_DECLS_H
