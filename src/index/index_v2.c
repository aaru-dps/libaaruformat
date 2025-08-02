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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "aaruformat.h"
#include "utarray.h"

UT_array *process_index_v2(aaruformatContext *ctx)
{
    UT_array  *index_entries = NULL;
    IndexEntry entry;

    if(ctx == NULL || ctx->imageStream == NULL) return NULL;

    // Initialize the index entries array
    UT_icd index_entry_icd = {sizeof(IndexEntry), NULL, NULL, NULL};

    utarray_new(index_entries, &index_entry_icd);

    // Read the index header
    fseek(ctx->imageStream, ctx->header.indexOffset, SEEK_SET);
    IndexHeader2 idx_header;
    fread(&idx_header, sizeof(IndexHeader2), 1, ctx->imageStream);

    // Check if the index header is valid
    if(idx_header.identifier != IndexBlock2)
    {
        fprintf(stderr, "Incorrect index identifier.\n");
        utarray_free(index_entries);
        return NULL;
    }

    for(int i = 0; i < idx_header.entries; i++)
    {
        fread(&entry, sizeof(IndexEntry), 1, ctx->imageStream);
        utarray_push_back(index_entries, &entry);
    }

    return index_entries;
}

int32_t verify_index_v2(aaruformatContext *ctx)
{
    size_t      read_bytes = 0;
    IndexHeader index_header;
    uint64_t    crc64         = 0;
    IndexEntry *index_entries = NULL;

    if(ctx == NULL || ctx->imageStream == NULL) return AARUF_ERROR_NOT_AARUFORMAT;

    // This will traverse all blocks and check their CRC64 without uncompressing them
    fprintf(stderr, "Checking index integrity at %llu.\n", ctx->header.indexOffset);
    fseek(ctx->imageStream, ctx->header.indexOffset, SEEK_SET);

    // Read the index header
    read_bytes = fread(&index_header, 1, sizeof(IndexHeader2), ctx->imageStream);

    if(read_bytes != sizeof(IndexHeader2))
    {
        fprintf(stderr, "Could not read index header.\n");
        return AARUF_ERROR_CANNOT_READ_HEADER;
    }

    if(index_header.identifier != IndexBlock)
    {
        fprintf(stderr, "Incorrect index identifier.\n");
        return AARUF_ERROR_CANNOT_READ_INDEX;
    }

    fprintf(stderr, "Index at %llu contains %d entries.\n", ctx->header.indexOffset, index_header.entries);

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

    crc64 = aaruf_crc64_data((const uint8_t *)index_entries, sizeof(IndexEntry) * index_header.entries);

    // Due to how C# wrote it, it is effectively reversed
    if(ctx->header.imageMajorVersion <= AARUF_VERSION_V1) crc64 = bswap_64(crc64);

    if(crc64 != index_header.crc64)
    {
        fprintf(stderr, "Expected index CRC 0x%16llX but got 0x%16llX.\n", index_header.crc64, crc64);
        free(index_entries);
        return AARUF_ERROR_INVALID_BLOCK_CRC;
    }

    return AARUF_STATUS_OK;
}