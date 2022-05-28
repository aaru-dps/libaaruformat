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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <aaruformat.h>

#include "../3rdparty/flac/include/FLAC/metadata.h"
#include "../3rdparty/flac/include/FLAC/stream_decoder.h"
#include "../3rdparty/flac/include/FLAC/stream_encoder.h"
#include "flac.h"

static FLAC__StreamDecoderReadStatus
    read_callback(const FLAC__StreamDecoder* decoder, FLAC__byte buffer[], size_t* bytes, void* client_data);
static FLAC__StreamDecoderWriteStatus write_callback(const FLAC__StreamDecoder* decoder,
                                                     const FLAC__Frame*         frame,
                                                     const FLAC__int32* const   buffer[],
                                                     void*                      client_data);
static void
    error_callback(const FLAC__StreamDecoder* decoder, FLAC__StreamDecoderErrorStatus status, void* client_data);

AARU_EXPORT size_t AARU_CALL aaruf_flac_decode_redbook_buffer(uint8_t*       dst_buffer,
                                                              size_t         dst_size,
                                                              const uint8_t* src_buffer,
                                                              size_t         src_size)
{
    FLAC__StreamDecoder*          decoder;
    FLAC__StreamDecoderInitStatus init_status;
    aaru_flac_ctx*                ctx = (aaru_flac_ctx*)malloc(sizeof(aaru_flac_ctx));
    size_t                        ret_size;

    memset(ctx, 0, sizeof(aaru_flac_ctx));

    ctx->src_buffer = src_buffer;
    ctx->src_len    = src_size;
    ctx->src_pos    = 0;
    ctx->dst_buffer = dst_buffer;
    ctx->dst_len    = dst_size;
    ctx->dst_pos    = 0;
    ctx->error      = 0;

    decoder = FLAC__stream_decoder_new();

    if(!decoder)
    {
        free(ctx);
        return -1;
    }

    FLAC__stream_decoder_set_md5_checking(decoder, false);

    init_status = FLAC__stream_decoder_init_stream(
        decoder, read_callback, NULL, NULL, NULL, NULL, write_callback, NULL, error_callback, ctx);

    if(init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK)
    {
        free(ctx);
        return -1;
    }

    // TODO: Return error somehow
    FLAC__stream_decoder_process_until_end_of_stream(decoder);

    FLAC__stream_decoder_delete(decoder);

    ret_size = ctx->dst_pos;

    free(ctx);

    return ret_size;
}

static FLAC__StreamDecoderReadStatus
    read_callback(const FLAC__StreamDecoder* decoder, FLAC__byte buffer[], size_t* bytes, void* client_data)
{
    aaru_flac_ctx* ctx = (aaru_flac_ctx*)client_data;

    if(ctx->src_len - ctx->src_pos < *bytes) *bytes = ctx->src_len - ctx->src_pos;

    if(*bytes == 0) return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;

    memcpy(buffer, ctx->src_buffer + ctx->src_pos, *bytes);
    ctx->src_pos += *bytes;

    return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

static FLAC__StreamDecoderWriteStatus write_callback(const FLAC__StreamDecoder* decoder,
                                                     const FLAC__Frame*         frame,
                                                     const FLAC__int32* const   buffer[],
                                                     void*                      client_data)
{
    aaru_flac_ctx* ctx = (aaru_flac_ctx*)client_data;
    size_t         i;
    uint16_t*      buffer16 = (uint16_t*)(ctx->dst_buffer + ctx->dst_pos);

    // Why FLAC does not interleave the channels as PCM do, oh the mistery, we could use memcpy instead of looping
    for(i = 0; i < frame->header.blocksize && ctx->dst_pos < ctx->dst_len; i++)
    {
        // Left channel
        *(buffer16++) = (FLAC__int16)buffer[0][i];
        // Right channel
        *(buffer16++) = (FLAC__int16)buffer[1][i];

        ctx->dst_pos += 4;

        /* TODO: Big-endian (use bswap?)
        // Left channel
        ctx->dst_buffer[ctx->dst_pos++] = (FLAC__uint16)(FLAC__int16)buffer[0][i];
        ctx->dst_buffer[ctx->dst_pos++] = (FLAC__uint16)(FLAC__int16)buffer[0][i] >> 8;
        // Right channel
        ctx->dst_buffer[ctx->dst_pos++] = (FLAC__uint16)(FLAC__int16)buffer[1][i];
        ctx->dst_buffer[ctx->dst_pos++] = (FLAC__uint16)(FLAC__int16)buffer[1][i] >> 8;
        */
    }

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void error_callback(const FLAC__StreamDecoder* decoder, FLAC__StreamDecoderErrorStatus status, void* client_data)
{
    aaru_flac_ctx* ctx = (aaru_flac_ctx*)client_data;

    fprintf(stderr, "Got error callback: %s\n", FLAC__StreamDecoderErrorStatusString[status]);

    ctx->error = 1;
}

static FLAC__StreamEncoderWriteStatus encoder_write_callback(const FLAC__StreamEncoder* encoder,
                                                             const FLAC__byte           buffer[],
                                                             size_t                     bytes,
                                                             uint32_t                   samples,
                                                             uint32_t                   current_frame,
                                                             void*                      client_data);

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
                                                              uint32_t       application_id_len)
{
    FLAC__StreamEncoder*          encoder;
    aaru_flac_ctx*                ctx = (aaru_flac_ctx*)malloc(sizeof(aaru_flac_ctx));
    FLAC__StreamEncoderInitStatus init_status;
    size_t                        ret_size;
    FLAC__int32*                  pcm;
    int                           i;
    int16_t*                      buffer16 = (int16_t*)src_buffer;
    FLAC__StreamMetadata*         metadata[1];

    memset(ctx, 0, sizeof(aaru_flac_ctx));

    ctx->src_buffer = src_buffer;
    ctx->src_len    = src_size;
    ctx->src_pos    = 0;
    ctx->dst_buffer = dst_buffer;
    ctx->dst_len    = dst_size;
    ctx->dst_pos    = 0;
    ctx->error      = 0;

    encoder = FLAC__stream_encoder_new();

    if(!encoder)
    {
        free(ctx);
        return -1;
    }

    // TODO: Error detection here
    FLAC__stream_encoder_set_verify(encoder, false);
    FLAC__stream_encoder_set_streamable_subset(encoder, false);
    FLAC__stream_encoder_set_channels(encoder, 2);
    FLAC__stream_encoder_set_bits_per_sample(encoder, 16);
    FLAC__stream_encoder_set_sample_rate(encoder, 44100);
    FLAC__stream_encoder_set_blocksize(encoder, blocksize);
    // true compresses more
    FLAC__stream_encoder_set_do_mid_side_stereo(encoder, do_mid_side_stereo);
    // false compresses more
    FLAC__stream_encoder_set_loose_mid_side_stereo(encoder, loose_mid_side_stereo);
    // Apodization
    FLAC__stream_encoder_set_apodization(encoder, apodization);
    FLAC__stream_encoder_set_max_lpc_order(encoder, max_lpc_order);
    FLAC__stream_encoder_set_qlp_coeff_precision(encoder, qlp_coeff_precision);
    FLAC__stream_encoder_set_do_qlp_coeff_prec_search(encoder, do_qlp_coeff_prec_search);
    FLAC__stream_encoder_set_do_exhaustive_model_search(encoder, do_exhaustive_model_search);
    FLAC__stream_encoder_set_min_residual_partition_order(encoder, min_residual_partition_order);
    FLAC__stream_encoder_set_max_residual_partition_order(encoder, max_residual_partition_order);
    FLAC__stream_encoder_set_total_samples_estimate(encoder, src_size / 4);

    /* TODO: This is ignored by FLAC, need to replace it
    if((metadata[0] = FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT)) != NULL)
    {
        memset(&vorbis_entry, 0, sizeof(FLAC__StreamMetadata_VorbisComment_Entry));
        vorbis_entry.entry = (unsigned char *)"Aaru.Compression.Native";
        vorbis_entry.length = strlen("Aaru.Compression.Native");

        FLAC__metadata_object_vorbiscomment_set_vendor_string(metadata[0], vorbis_entry, true);
    }
    */

    if(application_id_len > 0 && application_id != NULL)
        if((metadata[0] = FLAC__metadata_object_new(FLAC__METADATA_TYPE_APPLICATION)) != NULL)
            FLAC__metadata_object_application_set_data(
                metadata[0], (unsigned char*)application_id, application_id_len, true);

    FLAC__stream_encoder_set_metadata(encoder, metadata, 1);

    init_status = FLAC__stream_encoder_init_stream(encoder, encoder_write_callback, NULL, NULL, NULL, ctx);

    if(init_status != FLAC__STREAM_ENCODER_INIT_STATUS_OK)
    {
        free(ctx);
        return -1;
    }

    pcm = malloc((src_size / 2) * sizeof(FLAC__int32));

    for(i = 0; i < src_size / 2; i++) pcm[i] = (FLAC__int32) * (buffer16++);

    FLAC__stream_encoder_process_interleaved(encoder, pcm, src_size / 4);

    FLAC__stream_encoder_finish(encoder);

    FLAC__stream_encoder_delete(encoder);

    ret_size = ctx->dst_pos;

    free(ctx);
    free(pcm);
    FLAC__metadata_object_delete(metadata[0]);

    return ret_size;
}

static FLAC__StreamEncoderWriteStatus encoder_write_callback(const FLAC__StreamEncoder* encoder,
                                                             const FLAC__byte           buffer[],
                                                             size_t                     bytes,
                                                             uint32_t                   samples,
                                                             uint32_t                   current_frame,
                                                             void*                      client_data)
{
    aaru_flac_ctx* ctx = (aaru_flac_ctx*)client_data;

    if(bytes > ctx->dst_len - ctx->dst_pos) bytes = ctx->dst_len - ctx->dst_pos;

    memcpy(ctx->dst_buffer + ctx->dst_pos, buffer, bytes);

    ctx->dst_pos += bytes;

    return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}