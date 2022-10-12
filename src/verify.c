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

#include <inttypes.h>

#include <aaruformat.h>

#define VERIFY_SIZE 1048576

int32_t aaruf_verify_image(void* context)
{
    aaruformatContext* ctx;
    uint64_t           crc64;
    int                i;
    IndexHeader        index_header;
    IndexEntry*        index_entries;
    size_t             read_bytes;
    void*              buffer;
    crc64_ctx*         crc64_context;
    BlockHeader        block_header;
    uint64_t           verified_bytes;
    DdtHeader          ddt_header;
    TracksHeader       tracks_header;

    if(context == NULL) return AARUF_ERROR_NOT_AARUFORMAT;

    ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC) return AARUF_ERROR_NOT_AARUFORMAT;

    // This will traverse all blocks and check their CRC64 without uncompressing them
    fprintf(stderr, "Checking index integrity at %lu.\n", ctx->header.indexOffset);
    fseek(ctx->imageStream, ctx->header.indexOffset, SEEK_SET);

    read_bytes = fread(&index_header, 1, sizeof(IndexHeader), ctx->imageStream);

    if(read_bytes != sizeof(IndexHeader))
    {
        fprintf(stderr, "Could not read index header.\n");
        return AARUF_ERROR_CANNOT_READ_HEADER;
    }

    if(index_header.identifier != IndexBlock)
    {
        fprintf(stderr, "Incorrect index identifier.\n");
        return AARUF_ERROR_CANNOT_READ_INDEX;
    }

    fprintf(stderr, "Index at %lu contains %d entries.\n", ctx->header.indexOffset, index_header.entries);

    index_entries = malloc(sizeof(IndexEntry) * index_header.entries);

    if(index_entries == NULL)
    {
        fprintf(stderr, "Cannot allocate memory for index entries.\n");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    read_bytes = fread(index_entries, 1, sizeof(IndexEntry) * index_header.entries, ctx->imageStream);

    if(read_bytes != sizeof(IndexEntry) * index_header.entries)
    {
        fprintf(stderr, "Could not read index entries.\n");
        free(index_entries);
        return AARUF_ERROR_CANNOT_READ_INDEX;
    }

    crc64 = aaruf_crc64_data((const uint8_t*)index_entries, sizeof(IndexEntry) * index_header.entries);

    // Due to how C# wrote it, it is effectively reversed
    if(ctx->header.imageMajorVersion <= AARUF_VERSION) crc64 = bswap_64(crc64);

    if(crc64 != index_header.crc64)
    {
        fprintf(stderr, "Expected index CRC 0x%16lX but got 0x%16lX.\n", index_header.crc64, crc64);
        free(index_entries);
        return AARUF_ERROR_INVALID_BLOCK_CRC;
    }

    buffer = malloc(VERIFY_SIZE);

    if(buffer == NULL)
    {
        fprintf(stderr, "Cannot allocate memory for buffer.\n");
        free(index_entries);
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    for(i = 0; i < index_header.entries; i++)
    {
        fprintf(stderr,
                "Checking block with type %4.4s at position %" PRIu64 "\n",
                (char*)&index_entries[i].blockType,
                index_entries[i].offset);

        fseek(ctx->imageStream, index_entries[i].offset, SEEK_SET);

        switch(index_entries[i].blockType)
        {
            case DataBlock:
                read_bytes = fread(&block_header, 1, sizeof(BlockHeader), ctx->imageStream);
                if(read_bytes != sizeof(BlockHeader))
                {
                    fprintf(stderr, "Could not read block header.\n");
                    free(index_entries);
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                crc64_context = aaruf_crc64_init();

                if(crc64_context == NULL)
                {
                    fprintf(stderr, "Could not initialize CRC64.\n");
                    free(index_entries);
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
                if(ctx->header.imageMajorVersion <= AARUF_VERSION) crc64 = bswap_64(crc64);

                if(crc64 != block_header.cmpCrc64)
                {
                    fprintf(stderr, "Expected block CRC 0x%16lX but got 0x%16lX.\n", block_header.cmpCrc64, crc64);
                    free(index_entries);
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                break;
            case DeDuplicationTable:
                read_bytes = fread(&ddt_header, 1, sizeof(DdtHeader), ctx->imageStream);
                if(read_bytes != sizeof(DdtHeader))
                {
                    fprintf(stderr, "Could not read DDT header.\n");
                    free(index_entries);
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                crc64_context = aaruf_crc64_init();

                if(crc64_context == NULL)
                {
                    fprintf(stderr, "Could not initialize CRC64.\n");
                    free(index_entries);
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
                if(ctx->header.imageMajorVersion <= AARUF_VERSION) crc64 = bswap_64(crc64);

                if(crc64 != ddt_header.cmpCrc64)
                {
                    fprintf(stderr, "Expected DDT CRC 0x%16lX but got 0x%16lX.\n", ddt_header.cmpCrc64, crc64);
                    free(index_entries);
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                break;
            case TracksBlock:
                read_bytes = fread(&tracks_header, 1, sizeof(TracksHeader), ctx->imageStream);
                if(read_bytes != sizeof(TracksHeader))
                {
                    fprintf(stderr, "Could not read tracks header.\n");
                    free(index_entries);
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                crc64_context = aaruf_crc64_init();

                if(crc64_context == NULL)
                {
                    fprintf(stderr, "Could not initialize CRC64.\n");
                    free(index_entries);
                    return AARUF_ERROR_CANNOT_READ_BLOCK;
                }

                read_bytes = fread(buffer, 1, tracks_header.entries * sizeof(TrackEntry), ctx->imageStream);
                aaruf_crc64_update(crc64_context, buffer, read_bytes);

                aaruf_crc64_final(crc64_context, &crc64);

                // Due to how C# wrote it, it is effectively reversed
                if(ctx->header.imageMajorVersion <= AARUF_VERSION) crc64 = bswap_64(crc64);

                if(crc64 != tracks_header.crc64)
                {
                    fprintf(stderr, "Expected DDT CRC 0x%16lX but got 0x%16lX.\n", tracks_header.crc64, crc64);
                    free(index_entries);
                    return AARUF_ERROR_INVALID_BLOCK_CRC;
                }

                break;
            default: fprintf(stderr, "Ignoring block type %4.4s.\n", (char*)&index_entries[i].blockType); break;
        }
    }

    return AARUF_STATUS_OK;
}