/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2025 Natalia Portillo.
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

#include "internal.h"
#include "utarray.h"

#define VERIFY_SIZE 1048576

int32_t aaruf_verify_image(void *context)
{
    aaruformatContext *ctx           = NULL;
    uint64_t           crc64         = 0;
    size_t             read_bytes    = 0;
    void              *buffer        = NULL;
    crc64_ctx         *crc64_context = NULL;
    BlockHeader        block_header;
    uint64_t           verified_bytes = 0;
    DdtHeader          ddt_header;
    TracksHeader       tracks_header;
    uint32_t           signature     = 0;
    UT_array          *index_entries = NULL;
    int32_t            err           = 0;

    if(context == NULL) return AARUF_ERROR_NOT_AARUFORMAT;

    ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC) return AARUF_ERROR_NOT_AARUFORMAT;

    fseek(ctx->imageStream, ctx->header.indexOffset, SEEK_SET);

    read_bytes = fread(&signature, 1, sizeof(uint32_t), ctx->imageStream);
    if(read_bytes != sizeof(uint32_t))
    {
        fprintf(stderr, "Could not read index signature.\n");
        return AARUF_ERROR_CANNOT_READ_HEADER;
    }

    if(signature != IndexBlock && signature != IndexBlock2 && signature != IndexBlock3)
    {
        fprintf(stderr, "Incorrect index signature.\n");
        return AARUF_ERROR_CANNOT_READ_INDEX;
    }

    // Check if the index is correct
    if(signature == IndexBlock)
        err = verify_index_v1(ctx);
    else if(signature == IndexBlock2)
        err = verify_index_v2(ctx);
    else if(signature == IndexBlock3)
        err = verify_index_v3(ctx);

    if(err != AARUF_STATUS_OK)
    {
        fprintf(stderr, "Index verification failed with error code %d.\n", err);
        return err;
    }

    // Process the index
    if(signature == IndexBlock)
        index_entries = process_index_v1(ctx);
    else if(signature == IndexBlock2)
        index_entries = process_index_v2(ctx);
    else if(signature == IndexBlock3)
        index_entries = process_index_v3(ctx);

    if(index_entries == NULL)
    {
        fprintf(stderr, "Could not process index.\n");
        return AARUF_ERROR_CANNOT_READ_INDEX;
    }

    buffer = malloc(VERIFY_SIZE);

    if(buffer == NULL)
    {
        fprintf(stderr, "Cannot allocate memory for buffer.\n");
        utarray_free(index_entries);
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    for(int i = 0; i < utarray_len(index_entries); i++)
    {
        IndexEntry *entry = (IndexEntry *)utarray_eltptr(index_entries, i);
        fprintf(stderr, "Checking block with type %4.4s at position %" PRIu64 "\n", (char *)&entry->blockType,
                entry->offset);

        fseek(ctx->imageStream, entry->offset, SEEK_SET);

        switch(entry->blockType)
        {
            case DataBlock:
                read_bytes = fread(&block_header, 1, sizeof(BlockHeader), ctx->imageStream);
                if(read_bytes != sizeof(BlockHeader))
                {
                    fprintf(stderr, "Could not read block header.\n");
                    utarray_free(index_entries);
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                crc64_context = aaruf_crc64_init();

                if(crc64_context == NULL)
                {
                    fprintf(stderr, "Could not initialize CRC64.\n");
                    utarray_free(index_entries);
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                verified_bytes = 0;

                while(verified_bytes + VERIFY_SIZE < block_header.cmpLength)
                {
                    read_bytes = fread(buffer, 1, VERIFY_SIZE, ctx->imageStream);
                    aaruf_crc64_update(crc64_context, buffer, read_bytes);
                    verified_bytes += read_bytes;
                }

                read_bytes = fread(buffer, 1, block_header.cmpLength - verified_bytes, ctx->imageStream);
                aaruf_crc64_update(crc64_context, buffer, read_bytes);

                aaruf_crc64_final(crc64_context, &crc64);

                // Due to how C# wrote it, it is effectively reversed
                if(ctx->header.imageMajorVersion <= AARUF_VERSION_V1) crc64 = bswap_64(crc64);

                if(crc64 != block_header.cmpCrc64)
                {
                    fprintf(stderr, "Expected block CRC 0x%16llX but got 0x%16llX.\n", block_header.cmpCrc64, crc64);
                    utarray_free(index_entries);
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                break;
            case DeDuplicationTable:
                read_bytes = fread(&ddt_header, 1, sizeof(DdtHeader), ctx->imageStream);
                if(read_bytes != sizeof(DdtHeader))
                {
                    fprintf(stderr, "Could not read DDT header.\n");
                    utarray_free(index_entries);
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                crc64_context = aaruf_crc64_init();

                if(crc64_context == NULL)
                {
                    fprintf(stderr, "Could not initialize CRC64.\n");
                    utarray_free(index_entries);
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                verified_bytes = 0;

                while(verified_bytes + VERIFY_SIZE < ddt_header.cmpLength)
                {
                    read_bytes = fread(buffer, 1, VERIFY_SIZE, ctx->imageStream);
                    aaruf_crc64_update(crc64_context, buffer, read_bytes);
                    verified_bytes += read_bytes;
                }

                read_bytes = fread(buffer, 1, ddt_header.cmpLength - verified_bytes, ctx->imageStream);
                aaruf_crc64_update(crc64_context, buffer, read_bytes);

                aaruf_crc64_final(crc64_context, &crc64);

                // Due to how C# wrote it, it is effectively reversed
                if(ctx->header.imageMajorVersion <= AARUF_VERSION_V1) crc64 = bswap_64(crc64);

                if(crc64 != ddt_header.cmpCrc64)
                {
                    fprintf(stderr, "Expected DDT CRC 0x%16llX but got 0x%16llX.\n", ddt_header.cmpCrc64, crc64);
                    utarray_free(index_entries);
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                break;
            case TracksBlock:
                read_bytes = fread(&tracks_header, 1, sizeof(TracksHeader), ctx->imageStream);
                if(read_bytes != sizeof(TracksHeader))
                {
                    fprintf(stderr, "Could not read tracks header.\n");
                    utarray_free(index_entries);
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                crc64_context = aaruf_crc64_init();

                if(crc64_context == NULL)
                {
                    fprintf(stderr, "Could not initialize CRC64.\n");
                    utarray_free(index_entries);
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                read_bytes = fread(buffer, 1, tracks_header.entries * sizeof(TrackEntry), ctx->imageStream);
                aaruf_crc64_update(crc64_context, buffer, read_bytes);

                aaruf_crc64_final(crc64_context, &crc64);

                // Due to how C# wrote it, it is effectively reversed
                if(ctx->header.imageMajorVersion <= AARUF_VERSION_V1) crc64 = bswap_64(crc64);

                if(crc64 != tracks_header.crc64)
                {
                    fprintf(stderr, "Expected DDT CRC 0x%16llX but got 0x%16llX.\n", tracks_header.crc64, crc64);
                    utarray_free(index_entries);
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                break;
            default:
                fprintf(stderr, "Ignoring block type %4.4s.\n", (char *)&entry->blockType);
                break;
        }
    }

    return AARUF_STATUS_OK;
}