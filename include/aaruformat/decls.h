/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2022 Natalia Portillo.
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBAARUFORMAT_DECLS_H
#define LIBAARUFORMAT_DECLS_H

#include "simd.h"
#include "spamsum.h"
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

AARU_EXPORT int AARU_CALL aaruf_identify(const char* filename);

AARU_EXPORT int AARU_CALL aaruf_identify_stream(FILE* imageStream);

AARU_EXPORT void* AARU_CALL aaruf_open(const char* filepath);

AARU_EXPORT int AARU_CALL aaruf_close(void* context);

AARU_EXPORT int32_t AARU_CALL aaruf_read_media_tag(void* context, uint8_t* data, int32_t tag, uint32_t* length);

AARU_EXPORT crc64_ctx* AARU_CALL aaruf_crc64_init();
AARU_EXPORT int AARU_CALL        aaruf_crc64_update(crc64_ctx* ctx, const uint8_t* data, uint32_t len);
AARU_EXPORT int AARU_CALL        aaruf_crc64_final(crc64_ctx* ctx, uint64_t* crc);
AARU_EXPORT void AARU_CALL       aaruf_crc64_free(crc64_ctx* ctx);
AARU_EXPORT void AARU_CALL       aaruf_crc64_slicing(uint64_t* previous_crc, const uint8_t* data, uint32_t len);
AARU_EXPORT uint64_t AARU_CALL   aaruf_crc64_data(const uint8_t* data, uint32_t len);

AARU_EXPORT int32_t AARU_CALL aaruf_read_sector(void* context, uint64_t sectorAddress, uint8_t* data, uint32_t* length);

AARU_EXPORT int32_t AARU_CALL aaruf_cst_transform(const uint8_t* interleaved, uint8_t* sequential, size_t length);

AARU_EXPORT int32_t AARU_CALL aaruf_cst_untransform(const uint8_t* sequential, uint8_t* interleaved, size_t length);

AARU_EXPORT void* AARU_CALL aaruf_ecc_cd_init();

AARU_EXPORT bool AARU_CALL aaruf_ecc_cd_is_suffix_correct(void* context, const uint8_t* sector);

AARU_EXPORT bool AARU_CALL aaruf_ecc_cd_is_suffix_correct_mode2(void* context, const uint8_t* sector);

AARU_EXPORT bool AARU_CALL aaruf_ecc_cd_check(void*          context,
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

AARU_EXPORT void AARU_CALL aaruf_ecc_cd_write(void*          context,
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

AARU_EXPORT void AARU_CALL aaruf_ecc_cd_write_sector(void*          context,
                                                     const uint8_t* address,
                                                     const uint8_t* data,
                                                     uint8_t*       ecc,
                                                     int32_t        addressOffset,
                                                     int32_t        dataOffset,
                                                     int32_t        eccOffset);

AARU_LOCAL void AARU_CALL aaruf_cd_lba_to_msf(int64_t pos, uint8_t* minute, uint8_t* second, uint8_t* frame);

AARU_EXPORT void AARU_CALL aaruf_ecc_cd_reconstruct_prefix(uint8_t* sector, uint8_t type, int64_t lba);

AARU_EXPORT void AARU_CALL aaruf_ecc_cd_reconstruct(void* context, uint8_t* sector, uint8_t type);

AARU_EXPORT uint32_t AARU_CALL aaruf_edc_cd_compute(void* context, uint32_t edc, const uint8_t* src, int size, int pos);

AARU_EXPORT int32_t AARU_CALL
    aaruf_read_track_sector(void* context, uint8_t* data, uint64_t sectorAddress, uint32_t* length, uint8_t track);

AARU_LOCAL int32_t AARU_CALL aaruf_get_media_tag_type_for_datatype(int32_t type);

AARU_LOCAL int32_t AARU_CALL aaruf_get_xml_mediatype(int32_t type);

AARU_EXPORT spamsum_ctx* AARU_CALL aaruf_spamsum_init(void);
AARU_EXPORT int AARU_CALL          aaruf_spamsum_update(spamsum_ctx* ctx, const uint8_t* data, uint32_t len);
AARU_EXPORT int AARU_CALL          aaruf_spamsum_final(spamsum_ctx* ctx, uint8_t* result);
AARU_EXPORT void AARU_CALL         aaruf_spamsum_free(spamsum_ctx* ctx);

AARU_LOCAL void fuzzy_engine_step(spamsum_ctx* ctx, uint8_t c);
AARU_LOCAL void roll_hash(spamsum_ctx* ctx, uint8_t c);
AARU_LOCAL void fuzzy_try_reduce_blockhash(spamsum_ctx* ctx);
AARU_LOCAL void fuzzy_try_fork_blockhash(spamsum_ctx* ctx);

AARU_EXPORT size_t AARU_CALL aaruf_flac_decode_redbook_buffer(uint8_t*       dst_buffer,
                                                              size_t         dst_size,
                                                              const uint8_t* src_buffer,
                                                              size_t         src_size);

AARU_EXPORT size_t AARU_CALL aaruf_flac_encode_redbook_buffer(uint8_t*       dst_buffer,
                                                              size_t         dst_size,
                                                              const uint8_t* src_buffer,
                                                              size_t         src_size,
                                                              uint32_t       blocksize,
                                                              int32_t        do_mid_side_stereo,
                                                              int32_t        loose_mid_side_stereo,
                                                              const char*    apodization,
                                                              uint32_t       max_lpc_order,
                                                              uint32_t       qlp_coeff_precision,
                                                              int32_t        do_qlp_coeff_prec_search,
                                                              int32_t        do_exhaustive_model_search,
                                                              uint32_t       min_residual_partition_order,
                                                              uint32_t       max_residual_partition_order,
                                                              const char*    application_id,
                                                              uint32_t       application_id_len);

AARU_EXPORT int32_t AARU_CALL aaruf_lzma_decode_buffer(uint8_t*       dst_buffer,
                                                       size_t*        dst_size,
                                                       const uint8_t* src_buffer,
                                                       size_t*        src_size,
                                                       const uint8_t* props,
                                                       size_t         propsSize);

AARU_EXPORT int32_t AARU_CALL aaruf_lzma_encode_buffer(uint8_t*       dst_buffer,
                                                       size_t*        dst_size,
                                                       const uint8_t* src_buffer,
                                                       size_t         src_size,
                                                       uint8_t*       outProps,
                                                       size_t*        outPropsSize,
                                                       int32_t        level,
                                                       uint32_t       dictSize,
                                                       int32_t        lc,
                                                       int32_t        lp,
                                                       int32_t        pb,
                                                       int32_t        fb,
                                                       int32_t        numThreads);

#if defined(__x86_64__) || defined(__amd64) || defined(_M_AMD64) || defined(_M_X64) || defined(__I386__) ||            \
    defined(__i386__) || defined(__THW_INTEL) || defined(_M_IX86)

AARU_EXPORT int have_clmul();
AARU_EXPORT int have_ssse3();
AARU_EXPORT int have_avx2();

AARU_EXPORT CLMUL uint64_t AARU_CALL aaruf_crc64_clmul(uint64_t crc, const uint8_t* data, long length);
#endif

#if defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) || defined(_M_ARM)
AARU_EXPORT int have_neon();
AARU_EXPORT int have_arm_crc32();
AARU_EXPORT int have_arm_crypto();

AARU_EXPORT TARGET_WITH_SIMD uint64_t AARU_CALL aaruf_crc64_vmull(uint64_t previous_crc, const uint8_t* data, long len);
#endif

#endif // LIBAARUFORMAT_DECLS_H
