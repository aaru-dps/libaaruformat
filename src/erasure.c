/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
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

/**
 * @file erasure.c
 * @brief Erasure coding setup, parity accumulation, and write-path integration.
 */

#include <stdlib.h>
#include <string.h>

#include "aaruformat.h"
#include "aaruformat/context.h"
#include "aaruformat/consts.h"
#include "aaruformat/enums.h"
#include "aaruformat/errors.h"
#include "aaruformat/structs/data.h"
#include "aaruformat/structs/erasure.h"
#include "internal.h"
#include "log.h"
#include "lib/gf256.h"
#include "lib/reed_solomon.h"

/* =========================================================================
 * Read-path structures (not in on-disk format, internal only)
 * ========================================================================= */

/** @brief In-memory representation of one data stripe (parsed from ECMB). */
typedef struct EcReadStripe
{
    uint16_t              actual_k;       ///< Number of data blocks in this stripe.
    StripeDataBlockEntry *data_entries;   ///< Array of actual_k entries.
    uint64_t             *parity_offsets; ///< Array of M parity block file offsets.
} EcReadStripe;

/** @brief Hash table entry mapping block file offset -> stripe index + position. */
typedef struct EcBlockLookupEntry
{
    uint64_t       block_offset;   ///< Key: file offset of the data block.
    uint32_t       stripe_index;   ///< Index into ec_read_stripes array.
    uint16_t       position;       ///< Position within the stripe (0..actual_k-1).
    UT_hash_handle hh;
} EcBlockLookupEntry;

/* UT_array icd for completed stripe descriptors.
 * Each descriptor is a variable-length blob serialized in-place. We store
 * them as flat byte buffers since the size per stripe depends on K and M. */
static UT_icd ec_stripe_icd = {sizeof(uint8_t), NULL, NULL, NULL};

/* Forward declarations */
void ec_flush_data_stripe(aaruformat_context *ctx, uint32_t slot);

/**
 * @brief Configure erasure coding for a newly created image.
 *
 * Must be called after aaruf_create() and before the first aaruf_write_sector().
 * Allocates K interleaved stripe slots, each with M parity buffers.
 *
 * @param context Opaque context from aaruf_create().
 * @param algorithm ErasureCodingAlgorithm (0=XOR, 1=RS).
 * @param K Data blocks per stripe (>= 1).
 * @param M Parity blocks per stripe (>= 1).
 * @return AARUF_STATUS_OK on success, error code otherwise.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_set_erasure_coding(void *context, uint8_t algorithm, uint16_t K, uint16_t M)
{
    TRACE("Entering aaruf_set_erasure_coding(%p, %u, %u, %u)", context, algorithm, K, M);

    if(context == NULL) return AARUF_STATUS_INVALID_CONTEXT;

    aaruformat_context *ctx = (aaruformat_context *)context;
    if(ctx->magic != AARU_MAGIC) return AARUF_STATUS_INVALID_CONTEXT;
    if(!ctx->is_writing) return AARUF_READ_ONLY;

    /* Validate parameters */
    if(K == 0 || M == 0) return AARUF_ERROR_INCORRECT_DATA_SIZE;
    if((uint32_t)K + M > 255) return AARUF_ERROR_INCORRECT_DATA_SIZE;
    if(algorithm == kErasureCodingXor && M != 1) return AARUF_ERROR_INCORRECT_DATA_SIZE;

    /* Compute data shard size: max possible on-disk block size.
     * = sizeof(BlockHeader) + LZMA_PROPERTIES_LENGTH + (1 << dataShift) * sectorSize
     * This is the worst case: uncompressed block + LZMA properties header. */
    uint32_t sectors_per_block = 1U << ctx->user_data_ddt_header.dataShift;
    uint32_t max_payload       = sectors_per_block * ctx->current_block_header.sectorSize;
    if(max_payload == 0) max_payload = sectors_per_block * 512; /* fallback if sectorSize not yet set */
    uint32_t shard_size = (uint32_t)sizeof(BlockHeader) + LZMA_PROPERTIES_LENGTH + max_payload;

    /* Create RS codec */
    rs_context *rs = rs_create(K, M);
    if(!rs) return AARUF_ERROR_NOT_ENOUGH_MEMORY;

    /* Allocate K stripe slots × M parity buffers */
    uint8_t **parity = (uint8_t **)calloc((size_t)K * M, sizeof(uint8_t *));
    if(!parity) { rs_free(rs); return AARUF_ERROR_NOT_ENOUGH_MEMORY; }

    for(uint32_t i = 0; i < (uint32_t)K * M; i++)
    {
        parity[i] = (uint8_t *)calloc(1, shard_size);
        if(!parity[i])
        {
            for(uint32_t j = 0; j < i; j++) free(parity[j]);
            free(parity);
            rs_free(rs);
            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
        }
    }

    /* Allocate tracking arrays (K entries per slot × K slots) */
    uint64_t *offsets = (uint64_t *)calloc((size_t)K * K, sizeof(uint64_t));
    uint32_t *sizes   = (uint32_t *)calloc((size_t)K * K, sizeof(uint32_t));
    uint64_t *crcs    = (uint64_t *)calloc((size_t)K * K, sizeof(uint64_t));
    uint16_t *counts  = (uint16_t *)calloc(K, sizeof(uint16_t));

    if(!offsets || !sizes || !crcs || !counts)
    {
        free(offsets); free(sizes); free(crcs); free(counts);
        for(uint32_t i = 0; i < (uint32_t)K * M; i++) free(parity[i]);
        free(parity);
        rs_free(rs);
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    /* Initialize completed stripes array */
    UT_array *stripes = NULL;
    utarray_new(stripes, &ec_stripe_icd);

    /* Store in context */
    ctx->ec_algorithm         = algorithm;
    ctx->ec_K                 = K;
    ctx->ec_M                 = M;
    ctx->ec_data_shard_size   = shard_size;
    ctx->ec_rs_ctx            = rs;
    ctx->ec_data_parity       = parity;
    ctx->ec_data_block_offsets = offsets;
    ctx->ec_data_block_sizes  = sizes;
    ctx->ec_data_shard_crcs   = crcs;
    ctx->ec_data_stripe_counts = counts;
    ctx->ec_total_data_blocks = 0;
    ctx->ec_data_stripes      = stripes;
    ctx->ec_enabled           = true;

    /* Set feature flag so old readers know parity data exists */
    ctx->header.featureCompatibleRo |= AARU_FEATURE_ROCOMPAT_ERASURE;

    TRACE("Erasure coding configured: algorithm=%u K=%u M=%u shard_size=%u", algorithm, K, M, shard_size);
    TRACE("Exiting aaruf_set_erasure_coding() = 0");
    return AARUF_STATUS_OK;
}

/**
 * @brief Accumulate parity for a data block that was just written to disk.
 *
 * Called from aaruf_close_current_block() after writing BlockHeader + payload
 * to the file but before freeing the compressed data buffer.
 *
 * @param ctx Context with EC enabled.
 * @param block_header Pointer to the BlockHeader that was written.
 * @param lzma_props LZMA properties (may be NULL if not LZMA).
 * @param payload Compressed payload data (or uncompressed if no compression).
 * @param payload_size Size of the payload data.
 * @param file_offset File offset where the block was written.
 */
void ec_accumulate_data_block(aaruformat_context *ctx, const BlockHeader *block_header, const uint8_t *lzma_props,
                              const uint8_t *payload, uint32_t payload_size, uint64_t file_offset)
{
    if(!ctx->ec_enabled) return;

    const uint16_t K     = ctx->ec_K;
    const uint16_t M     = ctx->ec_M;
    const uint32_t shard = ctx->ec_data_shard_size;

    /* Determine which stripe slot this block goes to (interleaved round-robin) */
    uint32_t slot = ctx->ec_total_data_blocks % K;

    /* Position within this slot's stripe */
    uint16_t pos = ctx->ec_data_stripe_counts[slot];

    /* Build on-disk shard in a temp buffer:
     * [BlockHeader] [LZMA props if LZMA] [payload]
     * Remaining bytes to shard_size are implicitly zero (parity buffers were calloc'd) */
    uint32_t actual_size = (uint32_t)sizeof(BlockHeader);
    if(block_header->compression == kCompressionLzma && lzma_props)
        actual_size += LZMA_PROPERTIES_LENGTH;
    actual_size += payload_size;

    /* We need a temporary flat copy of the on-disk representation for CRC64 and parity accumulation */
    uint8_t *shard_buf = (uint8_t *)calloc(1, shard);
    if(!shard_buf) return; /* Best effort — if OOM, skip parity for this block */

    /* Copy BlockHeader */
    memcpy(shard_buf, block_header, sizeof(BlockHeader));
    uint32_t offset = sizeof(BlockHeader);

    /* Copy LZMA properties if applicable */
    if(block_header->compression == kCompressionLzma && lzma_props)
    {
        memcpy(shard_buf + offset, lzma_props, LZMA_PROPERTIES_LENGTH);
        offset += LZMA_PROPERTIES_LENGTH;
    }

    /* Copy payload */
    memcpy(shard_buf + offset, payload, payload_size);

    /* Compute CRC64 of the zero-padded shard */
    uint64_t shard_crc = aaruf_crc64_data(shard_buf, shard);

    /* Record tracking info */
    size_t tracking_idx = (size_t)slot * K + pos;
    ctx->ec_data_block_offsets[tracking_idx] = file_offset;
    ctx->ec_data_block_sizes[tracking_idx]   = actual_size;
    ctx->ec_data_shard_crcs[tracking_idx]    = shard_crc;

    /* Accumulate into parity buffers for this slot */
    for(uint16_t m = 0; m < M; m++)
    {
        uint8_t coeff = rs_get_coefficient((rs_context *)ctx->ec_rs_ctx, m, pos);
        size_t  parity_idx = (size_t)slot * M + m;
        rs_encode_incremental(coeff, shard_buf, ctx->ec_data_parity[parity_idx], shard);
    }

    free(shard_buf);

    ctx->ec_data_stripe_counts[slot]++;
    ctx->ec_total_data_blocks++;

    /* Check if this stripe slot is full → write parity blocks */
    if(ctx->ec_data_stripe_counts[slot] == K)
    {
        ec_flush_data_stripe(ctx, slot);
    }
}

/**
 * @brief Write M parity blocks for a completed data stripe slot and record the stripe descriptor.
 *
 * @param ctx Context with EC enabled.
 * @param slot Stripe slot index (0 .. K-1).
 */
void ec_flush_data_stripe(aaruformat_context *ctx, uint32_t slot)
{
    const uint16_t K     = ctx->ec_K;
    const uint16_t M     = ctx->ec_M;
    const uint32_t shard = ctx->ec_data_shard_size;
    uint16_t actual_k    = ctx->ec_data_stripe_counts[slot];

    if(actual_k == 0) return;

    /* Build and serialize stripe descriptor:
     * [actualK: uint16_t]
     * [actualK × StripeDataBlockEntry: offset(8) + onDiskSize(4) + shardCrc64(8) = 20 bytes each]
     * [M × StripeParityBlockEntry: offset(8) = 8 bytes each]
     */
    size_t desc_data_size = sizeof(uint16_t) + (size_t)actual_k * sizeof(StripeDataBlockEntry) +
                            (size_t)M * sizeof(StripeParityBlockEntry);
    uint8_t *desc = (uint8_t *)calloc(1, desc_data_size);
    if(!desc) return;

    uint8_t *p = desc;

    /* Write actualK */
    memcpy(p, &actual_k, sizeof(uint16_t)); p += sizeof(uint16_t);

    /* Write data block entries */
    for(uint16_t k = 0; k < actual_k; k++)
    {
        size_t idx = (size_t)slot * K + k;
        StripeDataBlockEntry entry;
        entry.offset     = ctx->ec_data_block_offsets[idx];
        entry.onDiskSize = ctx->ec_data_block_sizes[idx];
        entry.shardCrc64 = ctx->ec_data_shard_crcs[idx];
        memcpy(p, &entry, sizeof(StripeDataBlockEntry)); p += sizeof(StripeDataBlockEntry);
    }

    /* Write M parity blocks to disk */
    uint64_t alignment_mask = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;

    for(uint16_t m = 0; m < M; m++)
    {
        size_t parity_idx = (size_t)slot * M + m;
        uint8_t *parity_data = ctx->ec_data_parity[parity_idx];

        /* Compress the parity shard using the same settings as data blocks */
        BlockHeader parity_header;
        memset(&parity_header, 0, sizeof(BlockHeader));
        parity_header.identifier  = DataBlock;
        parity_header.type        = kDataTypeErasureParity;
        parity_header.compression = kCompressionNone;
        parity_header.sectorSize  = 0;
        parity_header.length      = shard;
        parity_header.cmpLength   = shard;
        parity_header.crc64       = aaruf_crc64_data(parity_data, shard);
        parity_header.cmpCrc64    = parity_header.crc64;

        /* Try compression */
        uint8_t *cmp_buf = NULL;
        size_t cmp_size = 0;

        if(ctx->compression_enabled)
        {
            cmp_buf = (uint8_t *)malloc((size_t)shard * 2);
            if(cmp_buf)
            {
                if(ctx->use_zstd)
                {
                    cmp_size = aaruf_zstd_encode_buffer(cmp_buf, (size_t)shard * 2, parity_data, shard,
                                                        ctx->zstd_level, ctx->num_threads);
                    if(cmp_size > 0 && cmp_size < shard)
                    {
                        parity_header.compression = kCompressionZstd;
                        parity_header.cmpLength   = (uint32_t)cmp_size;
                        parity_header.cmpCrc64    = aaruf_crc64_data(cmp_buf, (uint32_t)cmp_size);
                        ctx->has_zstd_blocks = true;
                    }
                    else
                    {
                        free(cmp_buf);
                        cmp_buf = NULL;
                    }
                }
                else
                {
                    size_t dst_size = (size_t)shard * 2;
                    size_t props_size = LZMA_PROPERTIES_LENGTH;
                    uint8_t lzma_props[LZMA_PROPERTIES_LENGTH] = {0};
                    aaruf_lzma_encode_buffer(cmp_buf, &dst_size, parity_data, shard, lzma_props, &props_size, 9,
                                             ctx->lzma_dict_size, 4, 0, 2, 273, LZMA_THREADS(ctx));
                    if(dst_size + LZMA_PROPERTIES_LENGTH < shard)
                    {
                        parity_header.compression = kCompressionLzma;
                        parity_header.cmpLength   = (uint32_t)(dst_size + LZMA_PROPERTIES_LENGTH);
                        parity_header.cmpCrc64    = aaruf_crc64_data(cmp_buf, (uint32_t)dst_size);

                        /* Write: header + lzma_props + compressed data */
                        aaruf_fseek(ctx->imageStream, 0, SEEK_END);
                        uint64_t parity_offset = (uint64_t)aaruf_ftell(ctx->imageStream);
                        parity_offset = (parity_offset + alignment_mask) & ~alignment_mask;
                        aaruf_fseek(ctx->imageStream, (aaru_off_t)parity_offset, SEEK_SET);

                        fwrite(&parity_header, sizeof(BlockHeader), 1, ctx->imageStream);
                        fwrite(lzma_props, LZMA_PROPERTIES_LENGTH, 1, ctx->imageStream);
                        fwrite(cmp_buf, dst_size, 1, ctx->imageStream);
                        free(cmp_buf);

                        /* Record parity offset in descriptor */
                        StripeParityBlockEntry pentry;
                        pentry.offset = parity_offset;
                        memcpy(p, &pentry, sizeof(StripeParityBlockEntry)); p += sizeof(StripeParityBlockEntry);

                        /* Add index entry */
                        IndexEntry ie = {.blockType = DataBlock, .dataType = kDataTypeErasureParity,
                                         .offset = parity_offset};
                        utarray_push_back(ctx->index_entries, &ie);
                        ctx->dirty_index_block = true;

                        /* Update next_block_position */
                        uint64_t total = sizeof(BlockHeader) + parity_header.cmpLength;
                        ctx->next_block_position = (parity_offset + total + alignment_mask) & ~alignment_mask;

                        /* Reset parity buffer */
                        memset(parity_data, 0, shard);
                        continue;
                    }
                    else
                    {
                        free(cmp_buf);
                        cmp_buf = NULL;
                    }
                }
            }
        }

        /* Write uncompressed (or compressed non-LZMA) parity */
        aaruf_fseek(ctx->imageStream, 0, SEEK_END);
        uint64_t parity_offset = (uint64_t)aaruf_ftell(ctx->imageStream);
        parity_offset = (parity_offset + alignment_mask) & ~alignment_mask;
        aaruf_fseek(ctx->imageStream, (aaru_off_t)parity_offset, SEEK_SET);

        fwrite(&parity_header, sizeof(BlockHeader), 1, ctx->imageStream);
        if(cmp_buf)
        {
            fwrite(cmp_buf, cmp_size, 1, ctx->imageStream);
            free(cmp_buf);
        }
        else
        {
            fwrite(parity_data, shard, 1, ctx->imageStream);
        }

        /* Record parity offset in descriptor */
        StripeParityBlockEntry pentry;
        pentry.offset = parity_offset;
        memcpy(p, &pentry, sizeof(StripeParityBlockEntry)); p += sizeof(StripeParityBlockEntry);

        /* Add index entry */
        IndexEntry ie = {.blockType = DataBlock, .dataType = kDataTypeErasureParity, .offset = parity_offset};
        utarray_push_back(ctx->index_entries, &ie);
        ctx->dirty_index_block = true;

        /* Update next_block_position */
        uint64_t total = sizeof(BlockHeader) + parity_header.cmpLength;
        ctx->next_block_position = (parity_offset + total + alignment_mask) & ~alignment_mask;

        /* Reset parity buffer */
        memset(parity_data, 0, shard);
    }

    /* Store the completed stripe descriptor */
    for(size_t i = 0; i < desc_data_size; i++)
        utarray_push_back(ctx->ec_data_stripes, &desc[i]);

    free(desc);

    /* Reset stripe slot tracking */
    for(uint16_t k = 0; k < K; k++)
    {
        size_t idx = (size_t)slot * K + k;
        ctx->ec_data_block_offsets[idx] = 0;
        ctx->ec_data_block_sizes[idx]   = 0;
        ctx->ec_data_shard_crcs[idx]    = 0;
    }
    ctx->ec_data_stripe_counts[slot] = 0;
}

/**
 * @brief Flush all partial data stripes and write the ECMB + recovery footer.
 *
 * Called from aaruf_finalize_write() after all metadata/DDT/index blocks are written.
 *
 * @param ctx Context with EC enabled.
 */
void ec_finalize(aaruformat_context *ctx)
{
    if(!ctx->ec_enabled) return;

    const uint16_t K = ctx->ec_K;

    /* Flush any partial stripe slots */
    for(uint16_t slot = 0; slot < K; slot++)
    {
        if(ctx->ec_data_stripe_counts[slot] > 0)
            ec_flush_data_stripe(ctx, slot);
    }

    /* Count completed stripes */
    uint32_t stripe_count = 0;
    /* Walk the flat stripe descriptor array to count entries.
     * Each stripe starts with a uint16_t actualK, followed by actualK × 20-byte entries + M × 8-byte entries.
     * Since we appended byte-by-byte, we need to parse. */
    {
        size_t total_bytes = utarray_len(ctx->ec_data_stripes);
        uint8_t *base = (uint8_t *)utarray_front(ctx->ec_data_stripes);
        size_t pos = 0;
        while(base && pos + sizeof(uint16_t) <= total_bytes)
        {
            uint16_t ak;
            memcpy(&ak, base + pos, sizeof(uint16_t));
            pos += sizeof(uint16_t);
            pos += (size_t)ak * sizeof(StripeDataBlockEntry);
            pos += (size_t)ctx->ec_M * sizeof(StripeParityBlockEntry);
            stripe_count++;
        }
    }

    /* Build ECMB payload: 1 stripe group (data only for now) */
    StripeGroupDescriptor group;
    memset(&group, 0, sizeof(group));
    group.groupType       = kECGroupData;
    group.K               = ctx->ec_K;
    group.M               = ctx->ec_M;
    group.shardSize       = ctx->ec_data_shard_size;
    group.stripeCount     = stripe_count;
    group.interleaveDepth = ctx->ec_K; /* full interleave */

    size_t stripe_data_len = utarray_len(ctx->ec_data_stripes);
    size_t payload_len = sizeof(StripeGroupDescriptor) + stripe_data_len;
    uint8_t *payload = (uint8_t *)malloc(payload_len);
    if(!payload) return;

    memcpy(payload, &group, sizeof(StripeGroupDescriptor));
    if(stripe_data_len > 0)
    {
        uint8_t *base = (uint8_t *)utarray_front(ctx->ec_data_stripes);
        if(base) memcpy(payload + sizeof(StripeGroupDescriptor), base, stripe_data_len);
    }

    /* CRC64 of payload */
    uint64_t payload_crc = aaruf_crc64_data(payload, (uint32_t)payload_len);

    /* Build ECMB header */
    ErasureCodingMapHeader ecmb;
    memset(&ecmb, 0, sizeof(ecmb));
    ecmb.identifier       = ErasureCodingMapBlock;
    ecmb.algorithm        = ctx->ec_algorithm;
    ecmb.stripeGroupCount = 1;
    ecmb.compression      = kCompressionNone;
    ecmb.length           = payload_len;
    ecmb.cmpLength        = payload_len;
    ecmb.crc64            = payload_crc;
    ecmb.cmpCrc64         = payload_crc;

    /* Write ECMB to file */
    uint64_t alignment_mask = (1ULL << ctx->user_data_ddt_header.blockAlignmentShift) - 1;
    aaruf_fseek(ctx->imageStream, 0, SEEK_END);
    uint64_t ecmb_offset = (uint64_t)aaruf_ftell(ctx->imageStream);
    ecmb_offset = (ecmb_offset + alignment_mask) & ~alignment_mask;
    aaruf_fseek(ctx->imageStream, (aaru_off_t)ecmb_offset, SEEK_SET);

    fwrite(&ecmb, sizeof(ErasureCodingMapHeader), 1, ctx->imageStream);
    fwrite(payload, payload_len, 1, ctx->imageStream);

    uint64_t ecmb_total = sizeof(ErasureCodingMapHeader) + payload_len;

    /* Write duplicate ECMB */
    uint64_t ecmb2_offset = (ecmb_offset + ecmb_total + alignment_mask) & ~alignment_mask;
    aaruf_fseek(ctx->imageStream, (aaru_off_t)ecmb2_offset, SEEK_SET);
    fwrite(&ecmb, sizeof(ErasureCodingMapHeader), 1, ctx->imageStream);
    fwrite(payload, payload_len, 1, ctx->imageStream);

    free(payload);

    /* Write recovery footer */
    AaruRecoveryFooter footer;
    memset(&footer, 0, sizeof(footer));
    footer.ecmbOffset  = ecmb_offset;
    footer.ecmbLength  = ecmb_total;
    footer.headerCrc64 = aaruf_crc64_data((const uint8_t *)&ctx->header, sizeof(AaruHeaderV2));
    memcpy(&footer.backupHeader, &ctx->header, sizeof(AaruHeaderV2));
    footer.footerMagic = AARU_RECOVERY_FOOTER_MAGIC;

    aaruf_fseek(ctx->imageStream, 0, SEEK_END);
    fwrite(&footer, sizeof(AaruRecoveryFooter), 1, ctx->imageStream);

    TRACE("Wrote ECMB at offset %" PRIu64 " (%" PRIu64 " bytes), footer at EOF", ecmb_offset, ecmb_total);
}

/**
 * @brief Free all erasure coding state from the context.
 *
 * Called from aaruf_close().
 *
 * @param ctx Context.
 */
void ec_free(aaruformat_context *ctx)
{
    if(!ctx->ec_enabled) return;

    if(ctx->ec_rs_ctx)
    {
        rs_free((rs_context *)ctx->ec_rs_ctx);
        ctx->ec_rs_ctx = NULL;
    }

    if(ctx->ec_data_parity)
    {
        for(uint32_t i = 0; i < (uint32_t)ctx->ec_K * ctx->ec_M; i++)
            free(ctx->ec_data_parity[i]);
        free(ctx->ec_data_parity);
        ctx->ec_data_parity = NULL;
    }

    free(ctx->ec_data_block_offsets);  ctx->ec_data_block_offsets = NULL;
    free(ctx->ec_data_block_sizes);    ctx->ec_data_block_sizes = NULL;
    free(ctx->ec_data_shard_crcs);     ctx->ec_data_shard_crcs = NULL;
    free(ctx->ec_data_stripe_counts);  ctx->ec_data_stripe_counts = NULL;

    if(ctx->ec_data_stripes)
    {
        utarray_free(ctx->ec_data_stripes);
        ctx->ec_data_stripes = NULL;
    }

    ctx->ec_enabled = false;

    /* Free read-path state */
    if(ctx->ec_read_stripes)
    {
        EcReadStripe *stripes = (EcReadStripe *)ctx->ec_read_stripes;
        for(uint32_t i = 0; i < ctx->ec_read_stripe_count; i++)
        {
            free(stripes[i].data_entries);
            free(stripes[i].parity_offsets);
        }
        free(stripes);
        ctx->ec_read_stripes = NULL;
    }
    ctx->ec_read_stripe_count = 0;

    /* Free block lookup hashmap */
    if(ctx->ec_block_lookup)
    {
        EcBlockLookupEntry *root = (EcBlockLookupEntry *)ctx->ec_block_lookup;
        EcBlockLookupEntry *entry, *tmp;
        HASH_ITER(hh, root, entry, tmp)
        {
            HASH_DEL(root, entry);
            free(entry);
        }
        ctx->ec_block_lookup = NULL;
    }

    ctx->ec_recovery_available = false;
}

/* =========================================================================
 * ECMB loading (read path)
 * ========================================================================= */

void ec_load_ecmb(aaruformat_context *ctx)
{
    TRACE("Entering ec_load_ecmb(%p)", (void *)ctx);

    /* Read recovery footer from last 160 bytes of file */
    aaruf_fseek(ctx->imageStream, 0, SEEK_END);
    int64_t file_size = aaruf_ftell(ctx->imageStream);
    if(file_size < (int64_t)sizeof(AaruRecoveryFooter))
    {
        TRACE("File too small for recovery footer");
        return;
    }

    aaruf_fseek(ctx->imageStream, (aaru_off_t)(file_size - (int64_t)sizeof(AaruRecoveryFooter)), SEEK_SET);

    AaruRecoveryFooter footer;
    if(fread(&footer, sizeof(AaruRecoveryFooter), 1, ctx->imageStream) != 1)
    {
        TRACE("Cannot read recovery footer");
        return;
    }

    if(footer.footerMagic != AARU_RECOVERY_FOOTER_MAGIC)
    {
        TRACE("Recovery footer magic mismatch: 0x%016" PRIx64, footer.footerMagic);
        return;
    }

    /* Read ECMB header */
    aaruf_fseek(ctx->imageStream, (aaru_off_t)footer.ecmbOffset, SEEK_SET);

    ErasureCodingMapHeader ecmb;
    if(fread(&ecmb, sizeof(ErasureCodingMapHeader), 1, ctx->imageStream) != 1)
    {
        TRACE("Cannot read ECMB header");
        return;
    }

    if(ecmb.identifier != ErasureCodingMapBlock)
    {
        TRACE("ECMB identifier mismatch");
        return;
    }

    /* Read payload (uncompressed only for now) */
    if(ecmb.length == 0 || ecmb.length > 256 * 1024 * 1024) return;  /* sanity limit */

    uint8_t *payload = (uint8_t *)malloc((size_t)ecmb.length);
    if(!payload) return;

    if(ecmb.compression == kCompressionNone)
    {
        if(fread(payload, (size_t)ecmb.cmpLength, 1, ctx->imageStream) != 1)
        {
            free(payload);
            return;
        }
    }
    else
    {
        /* Compressed ECMB payload — read compressed, decompress */
        uint8_t *cmp = (uint8_t *)malloc((size_t)ecmb.cmpLength);
        if(!cmp) { free(payload); return; }
        if(fread(cmp, (size_t)ecmb.cmpLength, 1, ctx->imageStream) != 1) { free(cmp); free(payload); return; }

        if(ecmb.compression == kCompressionLzma)
        {
            size_t out_size = (size_t)ecmb.length;
            size_t lzma_src_size = (size_t)ecmb.cmpLength - LZMA_PROPERTIES_LENGTH;
            aaruf_lzma_decode_buffer(payload, &out_size, cmp + LZMA_PROPERTIES_LENGTH,
                                     &lzma_src_size, cmp, LZMA_PROPERTIES_LENGTH);
        }
        else if(ecmb.compression == kCompressionZstd)
        {
            aaruf_zstd_decode_buffer(payload, (size_t)ecmb.length, cmp, (size_t)ecmb.cmpLength);
        }
        free(cmp);
    }

    /* Verify CRC64 */
    uint64_t computed_crc = aaruf_crc64_data(payload, (uint32_t)ecmb.length);
    if(computed_crc != ecmb.crc64)
    {
        TRACE("ECMB payload CRC64 mismatch");
        free(payload);
        return;
    }

    /* Parse stripe group descriptor */
    if(ecmb.length < sizeof(StripeGroupDescriptor)) { free(payload); return; }

    StripeGroupDescriptor group;
    memcpy(&group, payload, sizeof(StripeGroupDescriptor));

    ctx->ec_algorithm       = ecmb.algorithm;
    ctx->ec_K               = group.K;
    ctx->ec_M               = group.M;
    ctx->ec_data_shard_size = group.shardSize;

    /* Parse stripe descriptors */
    uint8_t *p = payload + sizeof(StripeGroupDescriptor);
    size_t   remaining = (size_t)ecmb.length - sizeof(StripeGroupDescriptor);

    EcReadStripe *stripes = (EcReadStripe *)calloc(group.stripeCount, sizeof(EcReadStripe));
    if(!stripes) { free(payload); return; }

    EcBlockLookupEntry *lookup_root = NULL;

    for(uint32_t s = 0; s < group.stripeCount; s++)
    {
        if(remaining < sizeof(uint16_t)) break;
        uint16_t ak;
        memcpy(&ak, p, sizeof(uint16_t)); p += sizeof(uint16_t); remaining -= sizeof(uint16_t);
        stripes[s].actual_k = ak;

        size_t data_bytes = (size_t)ak * sizeof(StripeDataBlockEntry);
        size_t parity_bytes = (size_t)group.M * sizeof(StripeParityBlockEntry);
        if(remaining < data_bytes + parity_bytes) break;

        stripes[s].data_entries = (StripeDataBlockEntry *)malloc(data_bytes);
        if(!stripes[s].data_entries) break;
        memcpy(stripes[s].data_entries, p, data_bytes); p += data_bytes; remaining -= data_bytes;

        stripes[s].parity_offsets = (uint64_t *)malloc((size_t)group.M * sizeof(uint64_t));
        if(!stripes[s].parity_offsets) break;
        for(uint16_t m = 0; m < group.M; m++)
        {
            StripeParityBlockEntry pe;
            memcpy(&pe, p, sizeof(StripeParityBlockEntry)); p += sizeof(StripeParityBlockEntry);
            remaining -= sizeof(StripeParityBlockEntry);
            stripes[s].parity_offsets[m] = pe.offset;
        }

        /* Build lookup hashmap entries for each data block in this stripe */
        for(uint16_t k = 0; k < ak; k++)
        {
            EcBlockLookupEntry *le = (EcBlockLookupEntry *)calloc(1, sizeof(EcBlockLookupEntry));
            if(!le) break;
            le->block_offset = stripes[s].data_entries[k].offset;
            le->stripe_index = s;
            le->position     = k;
            HASH_ADD(hh, lookup_root, block_offset, sizeof(uint64_t), le);
        }
    }

    free(payload);

    ctx->ec_read_stripes      = stripes;
    ctx->ec_read_stripe_count = group.stripeCount;
    ctx->ec_block_lookup      = lookup_root;
    ctx->ec_recovery_available = true;

    /* Create RS codec for decoding */
    if(!ctx->ec_rs_ctx)
        ctx->ec_rs_ctx = rs_create(group.K, group.M);

    TRACE("ECMB loaded: K=%u M=%u shard_size=%u stripes=%u", group.K, group.M, group.shardSize, group.stripeCount);
}

/* =========================================================================
 * Data block recovery (read path)
 * ========================================================================= */

int32_t ec_recover_data_block(aaruformat_context *ctx, uint64_t block_offset, uint64_t offset,
                              uint8_t *data, uint32_t *length, uint8_t sector_status)
{
    if(!ctx->ec_recovery_available || ctx->ec_recovery_in_progress) return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;

    ctx->ec_recovery_in_progress = true;

    /* Look up which stripe this block belongs to */
    EcBlockLookupEntry *le = NULL;
    HASH_FIND(hh, (EcBlockLookupEntry *)ctx->ec_block_lookup, &block_offset, sizeof(uint64_t), le);
    if(!le) { ctx->ec_recovery_in_progress = false; return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK; }

    uint32_t si  = le->stripe_index;
    EcReadStripe *stripes = (EcReadStripe *)ctx->ec_read_stripes;
    EcReadStripe *stripe  = &stripes[si];
    uint16_t K = ctx->ec_K;
    uint16_t M = ctx->ec_M;
    uint32_t shard_size = ctx->ec_data_shard_size;

    /* Always use K+M shards for RS. For partial stripes (actual_k < K),
     * positions actual_k..K-1 are all-zero (calloc'd) and marked present.
     * This works because the encoding used the K-size generator matrix
     * and zero-contributions for unused positions. */
    uint16_t total_shards = K + M;

    /* Allocate shard pointers and present flags */
    uint8_t **shards  = (uint8_t **)calloc(total_shards, sizeof(uint8_t *));
    uint8_t  *present = (uint8_t *)calloc(total_shards, 1);
    if(!shards || !present) { free(shards); free(present); ctx->ec_recovery_in_progress = false; return AARUF_ERROR_NOT_ENOUGH_MEMORY; }

    for(uint16_t i = 0; i < total_shards; i++)
    {
        shards[i] = (uint8_t *)calloc(1, shard_size);
        if(!shards[i])
        {
            for(uint16_t j = 0; j < i; j++) free(shards[j]);
            free(shards); free(present);
            ctx->ec_recovery_in_progress = false;
            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
        }
    }

    /* Read data shards from file and verify each one's CRC64 against ECMB */
    for(uint16_t k = 0; k < stripe->actual_k; k++)
    {
        StripeDataBlockEntry *de = &stripe->data_entries[k];
        uint32_t read_size = de->onDiskSize;
        if(read_size > shard_size) read_size = shard_size;

        aaruf_fseek(ctx->imageStream, (aaru_off_t)de->offset, SEEK_SET);
        if(fread(shards[k], read_size, 1, ctx->imageStream) != 1)
        {
            present[k] = 0;
            continue;
        }

        /* Verify CRC64 (zero-padded to shard_size via calloc) */
        uint64_t crc = aaruf_crc64_data(shards[k], shard_size);
        present[k] = (crc == de->shardCrc64) ? 1 : 0;
    }

    /* Positions actual_k..K-1 are all-zero and present (unused stripe positions) */
    for(uint16_t k = stripe->actual_k; k < K; k++)
        present[k] = 1; /* All-zero shards, implicitly correct */

    /* Read parity shards */
    for(uint16_t m = 0; m < M; m++)
    {
        uint16_t shard_idx = K + m;
        uint64_t parity_offset = stripe->parity_offsets[m];

        aaruf_fseek(ctx->imageStream, (aaru_off_t)parity_offset, SEEK_SET);

        /* Read parity block header */
        BlockHeader parity_header;
        if(fread(&parity_header, sizeof(BlockHeader), 1, ctx->imageStream) != 1)
        {
            present[shard_idx] = 0;
            continue;
        }

        /* Read and decompress parity payload */
        if(parity_header.compression == kCompressionNone)
        {
            uint32_t to_read = parity_header.length;
            if(to_read > shard_size) to_read = shard_size;
            if(fread(shards[shard_idx], to_read, 1, ctx->imageStream) != 1)
            {
                present[shard_idx] = 0;
                continue;
            }
        }
        else if(parity_header.compression == kCompressionLzma)
        {
            uint32_t cmp_data_len = parity_header.cmpLength - LZMA_PROPERTIES_LENGTH;
            uint8_t lzma_props[LZMA_PROPERTIES_LENGTH];
            if(fread(lzma_props, LZMA_PROPERTIES_LENGTH, 1, ctx->imageStream) != 1 )
            {
                present[shard_idx] = 0;
                continue;
            }
            uint8_t *cmp = (uint8_t *)malloc(cmp_data_len);
            if(!cmp) { present[shard_idx] = 0; continue; }
            if(fread(cmp, cmp_data_len, 1, ctx->imageStream) != 1) { free(cmp); present[shard_idx] = 0; continue; }

            size_t out_size = shard_size;
            size_t lzma_src = (size_t)cmp_data_len;
            aaruf_lzma_decode_buffer(shards[shard_idx], &out_size, cmp, &lzma_src, lzma_props, LZMA_PROPERTIES_LENGTH);
            free(cmp);
        }
        else if(parity_header.compression == kCompressionZstd)
        {
            uint8_t *cmp = (uint8_t *)malloc(parity_header.cmpLength);
            if(!cmp) { present[shard_idx] = 0; continue; }
            if(fread(cmp, parity_header.cmpLength, 1, ctx->imageStream) != 1) { free(cmp); present[shard_idx] = 0; continue; }

            aaruf_zstd_decode_buffer(shards[shard_idx], shard_size, cmp, parity_header.cmpLength);
            free(cmp);
        }
        else
        {
            present[shard_idx] = 0;
            continue;
        }
        present[shard_idx] = 1;
    }

    /* Always use the original RS(K,M) codec — partial stripes have zero-padded unused positions */
    rs_context *rs = (rs_context *)ctx->ec_rs_ctx;
    if(!rs)
    {
        for(uint16_t i = 0; i < total_shards; i++) free(shards[i]);
        free(shards); free(present);
        ctx->ec_recovery_in_progress = false;
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    /* RS decode */
    int rc = rs_decode(rs, shards, present, shard_size);

    if(rc != 0)
    {
        for(uint16_t i = 0; i < total_shards; i++) free(shards[i]);
        free(shards); free(present);
        ctx->ec_recovery_in_progress = false;
        return AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;
    }

    /* Find the shard corresponding to our corrupted block */
    uint16_t our_pos = le->position;
    uint8_t *recovered_shard = shards[our_pos];

    /* Parse the recovered BlockHeader */
    BlockHeader recovered_header;
    memcpy(&recovered_header, recovered_shard, sizeof(BlockHeader));

    /* Decompress the recovered payload */
    uint32_t hdr_size = sizeof(BlockHeader);
    uint8_t *recovered_payload = recovered_shard + hdr_size;
    uint32_t payload_len = recovered_header.cmpLength;

    uint8_t *block = NULL;

    if(recovered_header.compression == kCompressionNone)
    {
        block = (uint8_t *)malloc(recovered_header.length);
        if(block) memcpy(block, recovered_payload, recovered_header.length);
    }
    else if(recovered_header.compression == kCompressionLzma)
    {
        uint8_t *lzma_props = recovered_payload;
        uint8_t *lzma_data  = recovered_payload + LZMA_PROPERTIES_LENGTH;
        uint32_t lzma_data_len = payload_len - LZMA_PROPERTIES_LENGTH;

        block = (uint8_t *)malloc(recovered_header.length);
        if(block)
        {
            size_t out_size = recovered_header.length;
            size_t lzma_src2 = (size_t)lzma_data_len;
            aaruf_lzma_decode_buffer(block, &out_size, lzma_data, &lzma_src2, lzma_props, LZMA_PROPERTIES_LENGTH);
        }
    }
    else if(recovered_header.compression == kCompressionZstd)
    {
        block = (uint8_t *)malloc(recovered_header.length);
        if(block)
            aaruf_zstd_decode_buffer(block, recovered_header.length, recovered_payload, payload_len);
    }
    else if(recovered_header.compression == kCompressionFlac)
    {
        block = (uint8_t *)malloc(recovered_header.length);
        if(block)
            aaruf_flac_decode_redbook_buffer(block, recovered_header.length, recovered_payload, payload_len);
    }

    int32_t result = AARUF_ERROR_CANNOT_DECOMPRESS_BLOCK;

    if(block)
    {
        /* Verify recovered uncompressed data CRC64 */
        uint64_t block_crc = aaruf_crc64_data(block, recovered_header.length);
        if(block_crc == recovered_header.crc64)
        {
            /* Extract the requested sector */
            uint32_t sector_size = recovered_header.sectorSize;
            if(sector_size > 0 && offset * sector_size + sector_size <= recovered_header.length)
            {
                memcpy(data, block + offset * sector_size, sector_size);
                *length = sector_size;
                result = AARUF_STATUS_OK;

                /* Cache the recovered block so subsequent sector reads from the same
                 * block don't re-trigger recovery (this is the critical optimization). */
                add_to_cache_uint64(&ctx->block_cache, block_offset, block);

                /* Also cache the recovered BlockHeader */
                BlockHeader *cached_hdr = (BlockHeader *)malloc(sizeof(BlockHeader));
                if(cached_hdr)
                {
                    memcpy(cached_hdr, &recovered_header, sizeof(BlockHeader));
                    add_to_cache_uint64(&ctx->block_header_cache, block_offset, cached_hdr);
                }

                block = NULL; /* Ownership transferred to cache — don't free */
            }
        }
        free(block); /* Only frees if not transferred to cache */
    }

    for(uint16_t i = 0; i < total_shards; i++) free(shards[i]);
    free(shards);
    free(present);

    ctx->ec_recovery_in_progress = false;
    return result;
}
