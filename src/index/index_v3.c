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
#include "internal.h"
#include "log.h"
#include "utarray.h"

/**
 * @brief Processes an index block (version 3) from the image stream.
 *
 * Reads and parses an index block (version 3) from the image, returning an array of index entries.
 *
 * @param ctx Pointer to the aaruformat context.
 * @return Pointer to a UT_array of IndexEntry structures, or NULL on failure.
 */
UT_array *process_index_v3(aaruformatContext *ctx)
{
    TRACE("Entering process_index_v3(%p)", ctx);

    UT_array  *index_entries = NULL;
    IndexEntry entry;

    if(ctx == NULL || ctx->imageStream == NULL) return NULL;

    // Initialize the index entries array
    UT_icd index_entry_icd = {sizeof(IndexEntry), NULL, NULL, NULL};

    utarray_new(index_entries, &index_entry_icd);

    // Read the index header
    TRACE("Reading index header at position %llu", ctx->header.indexOffset);
    fseek(ctx->imageStream, ctx->header.indexOffset, SEEK_SET);
    IndexHeader3 idx_header;
    fread(&idx_header, sizeof(IndexHeader3), 1, ctx->imageStream);

    // Check if the index header is valid
    if(idx_header.identifier != IndexBlock3)
    {
        FATAL("Incorrect index identifier.");
        utarray_free(index_entries);

        TRACE("Exiting process_index_v3() = NULL");
        return NULL;
    }

    for(int i = 0; i < idx_header.entries; i++)
    {
        fread(&entry, sizeof(IndexEntry), 1, ctx->imageStream);
        if(entry.blockType == IndexBlock3)
        {
            // If the entry is a subindex, we need to read its entries
            add_subindex_entries(ctx, index_entries, &entry);
            continue;
        }
        utarray_push_back(index_entries, &entry);
    }

    TRACE("Read %d index entries from index block at position %llu", idx_header.entries, ctx->header.indexOffset);
    return index_entries;
}

/**
 * @brief Adds entries from a subindex block (version 3) to the main index entries array.
 *
 * Recursively reads subindex blocks and appends their entries to the main index entries array.
 *
 * @param ctx Pointer to the aaruformat context.
 * @param index_entries Pointer to the UT_array of main index entries.
 * @param subindex_entry Pointer to the subindex entry to process.
 */
void add_subindex_entries(aaruformatContext *ctx, UT_array *index_entries, IndexEntry *subindex_entry)
{
    TRACE("Entering add_subindex_entries(%p, %p, %p)", ctx, index_entries, subindex_entry);

    IndexHeader3 subindex_header;
    IndexEntry   entry;

    if(ctx == NULL || ctx->imageStream == NULL || index_entries == NULL || subindex_entry == NULL) return;

    // Seek to the subindex
    fseek(ctx->imageStream, subindex_entry->offset, SEEK_SET);

    // Read the subindex header
    fread(&subindex_header, sizeof(IndexHeader3), 1, ctx->imageStream);

    // Check if the subindex header is valid
    if(subindex_header.identifier != IndexBlock3)
    {
        FATAL("Incorrect subindex identifier.");

        TRACE("Exiting add_subindex_entries() without adding entries");
        return;
    }

    // Read each entry in the subindex and add it to the main index entries array
    for(int i = 0; i < subindex_header.entries; i++)
    {
        fread(&entry, sizeof(IndexEntry), 1, ctx->imageStream);
        if(entry.blockType == IndexBlock3)
        {
            // If the entry is a subindex, we need to read its entries
            add_subindex_entries(ctx, index_entries, &entry);
            continue;
        }
        utarray_push_back(index_entries, &entry);
    }

    TRACE("Exiting add_subindex_entries() after adding %d entries", subindex_header.entries);
}

/**
 * @brief Verifies the integrity of an index block (version 3) in the image stream.
 *
 * Checks the CRC64 of the index block and all subindexes without decompressing them.
 *
 * @param ctx Pointer to the aaruformat context.
 * @return Status code (AARUF_STATUS_OK on success, or an error code).
 */
int32_t verify_index_v3(aaruformatContext *ctx)
{
    TRACE("Entering verify_index_v3(%p)", ctx);

    size_t       read_bytes = 0;
    IndexHeader3 index_header;
    uint64_t     crc64         = 0;
    IndexEntry  *index_entries = NULL;

    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");

        TRACE("Exiting verify_index_v2() = AARUF_ERROR_NOT_AARUFORMAT");
        return AARUF_ERROR_NOT_AARUFORMAT;
    }

    // This will traverse all blocks and check their CRC64 without uncompressing them
    TRACE("Checking index integrity at %llu.", ctx->header.indexOffset);
    fseek(ctx->imageStream, ctx->header.indexOffset, SEEK_SET);

    // Read the index header
    TRACE("Reading index header at position %llu", ctx->header.indexOffset);
    read_bytes = fread(&index_header, 1, sizeof(IndexHeader3), ctx->imageStream);

    if(read_bytes != sizeof(IndexHeader3))
    {
        FATAL("Could not read index header.");

        TRACE("Exiting verify_index_v3() = AARUF_ERROR_CANNOT_READ_HEADER");
        return AARUF_ERROR_CANNOT_READ_HEADER;
    }

    if(index_header.identifier != IndexBlock3)
    {
        FATAL("Incorrect index identifier.");

        TRACE("Exiting verify_index_v3() = AARUF_ERROR_CANNOT_READ_INDEX");
        return AARUF_ERROR_CANNOT_READ_INDEX;
    }

    TRACE("Index at %llu contains %d entries.", ctx->header.indexOffset, index_header.entries);

    index_entries = malloc(sizeof(IndexEntry) * index_header.entries);

    if(index_entries == NULL)
    {
        FATAL("Cannot allocate memory for index entries.");

        TRACE("Exiting verify_index_v3() = AARUF_ERROR_NOT_ENOUGH_MEMORY");
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    read_bytes = fread(index_entries, 1, sizeof(IndexEntry) * index_header.entries, ctx->imageStream);

    if(read_bytes != sizeof(IndexEntry) * index_header.entries)
    {
        FATAL("Could not read index entries.");
        free(index_entries);

        TRACE("Exiting verify_index_v2() = AARUF_ERROR_CANNOT_READ_INDEX");
        return AARUF_ERROR_CANNOT_READ_INDEX;
    }

    crc64 = aaruf_crc64_data((const uint8_t *)index_entries, sizeof(IndexEntry) * index_header.entries);

    // Due to how C# wrote it, it is effectively reversed
    if(ctx->header.imageMajorVersion <= AARUF_VERSION_V1) crc64 = bswap_64(crc64);

    if(crc64 != index_header.crc64)
    {
        FATAL("Expected index CRC 0x%16llX but got 0x%16llX.", index_header.crc64, crc64);
        free(index_entries);

        TRACE("Exiting verify_index_v3() = AARUF_ERROR_INVALID_BLOCK_CRC");
        return AARUF_ERROR_INVALID_BLOCK_CRC;
    }

    TRACE("Exiting verify_index_v3() = AARUF_OK");
    return AARUF_STATUS_OK;
}