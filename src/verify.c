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

#include <aaruformat.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "internal.h"
#include "log.h"
#include "utarray.h"

#define VERIFY_SIZE 1048576

static int32_t update_crc64_from_stream(FILE *stream, const uint64_t total_length, void *buffer, size_t buffer_size,
                                        crc64_ctx *crc_ctx, const char *label)
{
    uint64_t processed = 0;

    while(processed < total_length)
    {
        size_t   chunk     = buffer_size;
        uint64_t remaining = total_length - processed;

        if(remaining < chunk) chunk = (size_t)remaining;

        if(chunk == 0) break;

        size_t read_bytes = fread(buffer, 1, chunk, stream);
        if(read_bytes != chunk)
        {
            FATAL("Could not read %s chunk (expected %zu bytes, got %zu)", label, chunk, read_bytes);
            return AARUF_ERROR_CANNOT_READ_BLOCK;
        }

        aaruf_crc64_update(crc_ctx, buffer, read_bytes);
        processed += read_bytes;
    }

    return AARUF_STATUS_OK;
}

/**
 * @brief Verifies the integrity of an AaruFormat image file.
 *
 * Checks the integrity of all blocks and deduplication tables in the image by validating CRC64
 * checksums for each indexed block. This function performs comprehensive verification of data blocks,
 * DDT v1 and v2 tables, tracks blocks, and other structural components. It reads blocks in chunks
 * to optimize memory usage during verification and supports version-specific CRC endianness handling.
 *
 * @param context Pointer to the aaruformat context.
 *
 * @return Returns one of the following status codes:
 * @retval AARUF_STATUS_OK (0) Successfully verified image integrity. This is returned when:
 *         - All indexed blocks are successfully read and processed
 *         - All CRC64 checksums match their expected values
 *         - Index verification passes for the detected index version
 *         - All block headers are readable and valid
 *         - Memory allocation for verification buffer succeeds
 *
 * @retval AARUF_ERROR_NOT_AARUFORMAT (-1) The context is invalid. This occurs when:
 *         - The context parameter is NULL
 *         - The context magic number doesn't match AARU_MAGIC (invalid context type)
 *
 * @retval AARUF_ERROR_CANNOT_READ_HEADER (-6) Failed to read critical headers. This occurs when:
 *         - Cannot read the index signature from the image stream
 *         - File I/O errors prevent reading header structures
 *
 * @retval AARUF_ERROR_CANNOT_READ_INDEX (-4) Index processing or validation failed. This occurs when:
 *         - Index signature is not a recognized type (IndexBlock, IndexBlock2, or IndexBlock3)
 *         - Index verification functions return error codes
 *         - Index processing functions return NULL (corrupted or invalid index)
 *
 * @retval AARUF_ERROR_NOT_ENOUGH_MEMORY (-9) Memory allocation failed. This occurs when:
 *         - Cannot allocate VERIFY_SIZE (1MB) buffer for reading block data during verification
 *         - System is out of available memory for verification operations
 *
 * @retval AARUF_ERROR_CANNOT_READ_BLOCK (-7) Block reading failed. This occurs when:
 *         - Cannot read block headers (DataBlock, DeDuplicationTable, DeDuplicationTable2, TracksBlock)
 *         - File I/O errors prevent reading block structure data
 *         - CRC64 context initialization fails (internal error)
 *
 * @retval AARUF_ERROR_INVALID_BLOCK_CRC (-18) CRC verification failed. This occurs when:
 *         - Calculated CRC64 doesn't match the expected CRC64 in block headers
 *         - Data corruption is detected in any verified block
 *         - This applies to DataBlock, DeDuplicationTable, DeDuplicationTable2, and TracksBlock types
 *
 * @note Verification Process:
 *       - Reads blocks in VERIFY_SIZE (1MB) chunks to optimize memory usage
 *       - Supports version-specific CRC64 endianness (v1 uses byte-swapped CRC64)
 *       - Verifies only blocks that have CRC checksums (skips blocks without checksums)
 *       - Unknown block types are logged but don't cause verification failure
 *
 * @note Block Types Verified:
 *       - DataBlock: Verifies compressed data CRC64 against block header
 *       - DeDuplicationTable (v1): Verifies DDT data CRC64 with version-specific endianness
 *       - DeDuplicationTable2 (v2): Verifies DDT data CRC64 (no endianness conversion)
 *       - TracksBlock: Verifies track entries CRC64 with version-specific endianness
 *       - Other block types are ignored during verification
 *
 * @note Performance Considerations:
 *       - Uses chunked reading to minimize memory footprint during verification
 *       - Processes blocks sequentially to maintain good disk access patterns
 *       - CRC64 contexts are created and destroyed for each block to prevent memory leaks
 *
 * @warning This function performs read-only verification and does not modify the image.
 *          It requires the image to be properly opened with a valid context.
 *
 * @warning Verification failure indicates data corruption or tampering. Images that
 *          fail verification should not be trusted for data recovery operations.
 *
 * @warning The function allocates a 1MB buffer for verification. Ensure sufficient
 *          memory is available before calling this function on resource-constrained systems.
 */
AARU_EXPORT int32_t AARU_CALL aaruf_verify_image(void *context)
{
    TRACE("Entering aaruf_verify_image(%p)", context);

    aaruformat_context *ctx           = NULL;
    uint64_t            crc64         = 0;
    size_t              read_bytes    = 0;
    void               *buffer        = NULL;
    crc64_ctx          *crc64_context = NULL;
    BlockHeader         block_header;
    DdtHeader           ddt_header;
    DdtHeader2          ddt2_header;
    TracksHeader        tracks_header;
    uint32_t            signature     = 0;
    UT_array           *index_entries = NULL;
    int32_t             status        = AARUF_STATUS_OK;

    if(context == NULL)
    {
        FATAL("Invalid context");
        status = AARUF_ERROR_NOT_AARUFORMAT;
        goto cleanup;
    }

    ctx = context;

    if(ctx->magic != AARU_MAGIC)
    {
        FATAL("Invalid context");
        status = AARUF_ERROR_NOT_AARUFORMAT;
        goto cleanup;
    }

    if(ctx->imageStream == NULL)
    {
        FATAL("Image stream is not available");
        status = AARUF_ERROR_NOT_AARUFORMAT;
        goto cleanup;
    }

    if(aaruf_fseek(ctx->imageStream, (aaru_off_t)ctx->header.indexOffset, SEEK_SET) != 0)
    {
        FATAL("Could not seek to index offset %" PRIu64, ctx->header.indexOffset);
        status = AARUF_ERROR_CANNOT_READ_HEADER;
        goto cleanup;
    }

    TRACE("Reading index signature at position %" PRIu64, ctx->header.indexOffset);
    read_bytes = fread(&signature, 1, sizeof(signature), ctx->imageStream);
    if(read_bytes != sizeof(signature))
    {
        FATAL("Could not read index signature");
        status = AARUF_ERROR_CANNOT_READ_HEADER;
        goto cleanup;
    }

    if(signature != IndexBlock && signature != IndexBlock2 && signature != IndexBlock3)
    {
        FATAL("Incorrect index signature %4.4s", (char *)&signature);
        status = AARUF_ERROR_CANNOT_READ_INDEX;
        goto cleanup;
    }

    if(signature == IndexBlock)
        status = verify_index_v1(ctx);
    else if(signature == IndexBlock2)
        status = verify_index_v2(ctx);
    else
        status = verify_index_v3(ctx);

    if(status != AARUF_STATUS_OK)
    {
        FATAL("Index verification failed with error code %d", status);
        goto cleanup;
    }

    if(signature == IndexBlock)
        index_entries = process_index_v1(ctx);
    else if(signature == IndexBlock2)
        index_entries = process_index_v2(ctx);
    else
        index_entries = process_index_v3(ctx);

    if(index_entries == NULL)
    {
        FATAL("Could not process index");
        status = AARUF_ERROR_CANNOT_READ_INDEX;
        goto cleanup;
    }

    buffer = malloc(VERIFY_SIZE);
    if(buffer == NULL)
    {
        FATAL("Cannot allocate memory for verification buffer");
        status = AARUF_ERROR_NOT_ENOUGH_MEMORY;
        goto cleanup;
    }

    uint64_t           crc_length;
    const unsigned int entry_count = utarray_len(index_entries);

    for(unsigned int i = 0; i < entry_count; i++)
    {
        IndexEntry *entry = utarray_eltptr(index_entries, i);
        TRACE("Checking block with type %4.4s at position %" PRIu64, (char *)&entry->blockType, entry->offset);

        if(aaruf_fseek(ctx->imageStream, (aaru_off_t)entry->offset, SEEK_SET) != 0)
        {
            FATAL("Could not seek to block at offset %" PRIu64, entry->offset);
            status = AARUF_ERROR_CANNOT_READ_BLOCK;
            goto cleanup;
        }

        switch(entry->blockType)
        {
            case DataBlock:
                read_bytes = fread(&block_header, 1, sizeof(BlockHeader), ctx->imageStream);
                if(read_bytes != sizeof(BlockHeader))
                {
                    FATAL("Could not read block header");
                    status = AARUF_ERROR_CANNOT_READ_BLOCK;
                    goto cleanup;
                }

                crc64_context = aaruf_crc64_init();
                if(crc64_context == NULL)
                {
                    FATAL("Could not initialize CRC64 context");
                    status = AARUF_ERROR_CANNOT_READ_BLOCK;
                    goto cleanup;
                }

                // For LZMA compression in v2+, skip the 5-byte properties header.
                // V1 images included LZMA properties in cmpCrc64; v2+ excludes them.
                crc_length = block_header.cmpLength;
                if((block_header.compression == kCompressionLzma || block_header.compression == kCompressionLzmaCst) &&
                   ctx->header.imageMajorVersion > AARUF_VERSION_V1)
                {
                    // Skip LZMA properties (v2+ only)
                    uint8_t props[LZMA_PROPERTIES_LENGTH];
                    size_t  read_props = fread(props, 1, LZMA_PROPERTIES_LENGTH, ctx->imageStream);
                    if(read_props != LZMA_PROPERTIES_LENGTH)
                    {
                        FATAL("Could not read LZMA properties");
                        status = AARUF_ERROR_CANNOT_READ_BLOCK;
                        goto cleanup;
                    }
                    crc_length -= LZMA_PROPERTIES_LENGTH;
                }

                status = update_crc64_from_stream(ctx->imageStream, crc_length, buffer, VERIFY_SIZE, crc64_context,
                                                  "data block");
                if(status != AARUF_STATUS_OK) goto cleanup;

                if(aaruf_crc64_final(crc64_context, &crc64) != 0)
                {
                    FATAL("Could not finalize CRC64 for data block");
                    status = AARUF_ERROR_CANNOT_READ_BLOCK;
                    goto cleanup;
                }

                if(ctx->header.imageMajorVersion <= AARUF_VERSION_V1) crc64 = bswap_64(crc64);

                if(crc64 != block_header.cmpCrc64)
                {
                    FATAL("Expected block CRC 0x%16llX but got 0x%16llX", block_header.cmpCrc64, crc64);
                    status = AARUF_ERROR_INVALID_BLOCK_CRC;
                    goto cleanup;
                }

                aaruf_crc64_free(crc64_context);
                crc64_context = NULL;
                break;
            case DeDuplicationTable:
                read_bytes = fread(&ddt_header, 1, sizeof(DdtHeader), ctx->imageStream);
                if(read_bytes != sizeof(DdtHeader))
                {
                    FATAL("Could not read DDT header");
                    status = AARUF_ERROR_CANNOT_READ_BLOCK;
                    goto cleanup;
                }

                crc64_context = aaruf_crc64_init();
                if(crc64_context == NULL)
                {
                    FATAL("Could not initialize CRC64 context");
                    status = AARUF_ERROR_CANNOT_READ_BLOCK;
                    goto cleanup;
                }

                // For LZMA compression in v2+, skip the 5-byte properties header.
                // V1 images included LZMA properties in cmpCrc64; v2+ excludes them.
                crc_length = ddt_header.cmpLength;
                if((ddt_header.compression == kCompressionLzma || ddt_header.compression == kCompressionLzmaCst) &&
                   ctx->header.imageMajorVersion > AARUF_VERSION_V1)
                {
                    // Skip LZMA properties (v2+ only)
                    uint8_t props[LZMA_PROPERTIES_LENGTH];
                    size_t  read_props = fread(props, 1, LZMA_PROPERTIES_LENGTH, ctx->imageStream);
                    if(read_props != LZMA_PROPERTIES_LENGTH)
                    {
                        FATAL("Could not read LZMA properties");
                        status = AARUF_ERROR_CANNOT_READ_BLOCK;
                        goto cleanup;
                    }
                    crc_length -= LZMA_PROPERTIES_LENGTH;
                }

                status = update_crc64_from_stream(ctx->imageStream, crc_length, buffer, VERIFY_SIZE, crc64_context,
                                                  "DDT block");
                if(status != AARUF_STATUS_OK) goto cleanup;

                if(aaruf_crc64_final(crc64_context, &crc64) != 0)
                {
                    FATAL("Could not finalize CRC64 for DDT block");
                    status = AARUF_ERROR_CANNOT_READ_BLOCK;
                    goto cleanup;
                }

                if(ctx->header.imageMajorVersion <= AARUF_VERSION_V1) crc64 = bswap_64(crc64);

                if(crc64 != ddt_header.cmpCrc64)
                {
                    FATAL("Expected DDT CRC 0x%16llX but got 0x%16llX", ddt_header.cmpCrc64, crc64);
                    status = AARUF_ERROR_INVALID_BLOCK_CRC;
                    goto cleanup;
                }

                aaruf_crc64_free(crc64_context);
                crc64_context = NULL;
                break;
            case DeDuplicationTable2:
                read_bytes = fread(&ddt2_header, 1, sizeof(DdtHeader2), ctx->imageStream);
                if(read_bytes != sizeof(DdtHeader2))
                {
                    FATAL("Could not read DDT2 header");
                    status = AARUF_ERROR_CANNOT_READ_BLOCK;
                    goto cleanup;
                }

                crc64_context = aaruf_crc64_init();
                if(crc64_context == NULL)
                {
                    FATAL("Could not initialize CRC64 context");
                    status = AARUF_ERROR_CANNOT_READ_BLOCK;
                    goto cleanup;
                }

                // DDT2 only appears in v2+ images, so always skip LZMA properties.
                crc_length = ddt2_header.cmpLength;
                if(ddt2_header.compression == kCompressionLzma || ddt2_header.compression == kCompressionLzmaCst)
                {
                    // Skip LZMA properties
                    uint8_t props[LZMA_PROPERTIES_LENGTH];
                    size_t  read_props = fread(props, 1, LZMA_PROPERTIES_LENGTH, ctx->imageStream);
                    if(read_props != LZMA_PROPERTIES_LENGTH)
                    {
                        FATAL("Could not read LZMA properties");
                        status = AARUF_ERROR_CANNOT_READ_BLOCK;
                        goto cleanup;
                    }
                    crc_length -= LZMA_PROPERTIES_LENGTH;
                }

                status = update_crc64_from_stream(ctx->imageStream, crc_length, buffer, VERIFY_SIZE, crc64_context,
                                                  "DDT2 block");
                if(status != AARUF_STATUS_OK) goto cleanup;

                if(aaruf_crc64_final(crc64_context, &crc64) != 0)
                {
                    FATAL("Could not finalize CRC64 for DDT2 block");
                    status = AARUF_ERROR_CANNOT_READ_BLOCK;
                    goto cleanup;
                }

                if(crc64 != ddt2_header.cmpCrc64)
                {
                    FATAL("Expected DDT2 CRC 0x%16llX but got 0x%16llX", ddt2_header.cmpCrc64, crc64);
                    status = AARUF_ERROR_INVALID_BLOCK_CRC;
                    goto cleanup;
                }

                aaruf_crc64_free(crc64_context);
                crc64_context = NULL;
                break;
            case TracksBlock:
            {
                read_bytes = fread(&tracks_header, 1, sizeof(TracksHeader), ctx->imageStream);
                if(read_bytes != sizeof(TracksHeader))
                {
                    FATAL("Could not read tracks header");
                    status = AARUF_ERROR_CANNOT_READ_BLOCK;
                    goto cleanup;
                }

                const uint64_t tracks_bytes = (uint64_t)tracks_header.entries * sizeof(TrackEntry);
                if(tracks_header.entries != 0 && tracks_bytes / sizeof(TrackEntry) != tracks_header.entries)
                {
                    FATAL("Tracks header length overflow (entries=%u)", tracks_header.entries);
                    status = AARUF_ERROR_CANNOT_READ_BLOCK;
                    goto cleanup;
                }

                crc64_context = aaruf_crc64_init();
                if(crc64_context == NULL)
                {
                    FATAL("Could not initialize CRC64 context");
                    status = AARUF_ERROR_CANNOT_READ_BLOCK;
                    goto cleanup;
                }

                status = update_crc64_from_stream(ctx->imageStream, tracks_bytes, buffer, VERIFY_SIZE, crc64_context,
                                                  "tracks block");
                if(status != AARUF_STATUS_OK) goto cleanup;

                if(aaruf_crc64_final(crc64_context, &crc64) != 0)
                {
                    FATAL("Could not finalize CRC64 for tracks block");
                    status = AARUF_ERROR_CANNOT_READ_BLOCK;
                    goto cleanup;
                }

                if(ctx->header.imageMajorVersion <= AARUF_VERSION_V1) crc64 = bswap_64(crc64);

                if(crc64 != tracks_header.crc64)
                {
                    FATAL("Expected tracks CRC 0x%16llX but got 0x%16llX", tracks_header.crc64, crc64);
                    status = AARUF_ERROR_INVALID_BLOCK_CRC;
                    goto cleanup;
                }

                aaruf_crc64_free(crc64_context);
                crc64_context = NULL;
                break;
            }
            default:
                TRACE("Ignoring block type %4.4s", (char *)&entry->blockType);
                break;
        }
    }

    status = AARUF_STATUS_OK;

cleanup:
    if(crc64_context != NULL)
    {
        aaruf_crc64_free(crc64_context);
        crc64_context = NULL;
    }

    if(buffer != NULL)
    {
        free(buffer);
        buffer = NULL;
    }

    if(index_entries != NULL)
    {
        utarray_free(index_entries);
        index_entries = NULL;
    }

    TRACE("Exiting aaruf_verify_image() = %d", status);
    return status;
}