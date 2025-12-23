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

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aaruformat/context.h"
#include "aaruformat/enums.h"
#include "aaruformat/structs/checksum.h"
#include "aaruformat/structs/index.h"
#include "log.h"

/**
 * @brief Processes a checksum block from the image stream.
 *
 * Reads a checksum block, parses its entries, and stores the checksums (MD5, SHA1, SHA256, SpamSum) in the context.
 *
 * @param ctx Pointer to the aaruformat context.
 * @param entry Pointer to the index entry describing the checksum block.
 */
void process_checksum_block(aaruformat_context *ctx, const IndexEntry *entry)
{
    TRACE("Entering process_checksum_block(%p, %p)", ctx, entry);

    int                  seek_result = 0;
    size_t               read_bytes  = 0;
    ChecksumHeader       checksum_header;
    ChecksumEntry const *checksum_entry = NULL;
    uint8_t             *data           = NULL;
    int                  j              = 0;
    size_t               payload_pos    = 0;

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");
        return;
    }

    // Seek to block
    seek_result = fseek(ctx->imageStream, entry->offset, SEEK_SET);
    if(seek_result < 0 || ftell(ctx->imageStream) != entry->offset)
    {
        FATAL("Could not seek to %" PRIu64 " as indicated by index entry...", entry->offset);

        return;
    }

    // Even if those two checks shall have been done before
    TRACE("Reading checksum block header at position %" PRIu64, entry->offset);
    read_bytes = fread(&checksum_header, 1, sizeof(ChecksumHeader), ctx->imageStream);

    if(read_bytes != sizeof(ChecksumHeader))
    {
        memset(&checksum_header, 0, sizeof(ChecksumHeader));
        FATAL("Could not read checksums block header, continuing...\n");
        return;
    }

    if(checksum_header.identifier != ChecksumBlock)
    {
        memset(&checksum_header, 0, sizeof(ChecksumHeader));
        FATAL("Incorrect identifier for checksum block at position %" PRIu64 "\n", entry->offset);
        return;
    }

    TRACE("Allocating %u bytes for checksum block", checksum_header.length);
    data = (uint8_t *)malloc(checksum_header.length);

    if(data == NULL)
    {
        memset(&checksum_header, 0, sizeof(ChecksumHeader));
        FATAL("Could not allocate memory for checksum block, continuing...\n");
        return;
    }

    TRACE("Reading checksum block data at position %" PRIu64, entry->offset + sizeof(ChecksumHeader));
    read_bytes = fread(data, 1, checksum_header.length, ctx->imageStream);

    if(read_bytes != checksum_header.length)
    {
        memset(&checksum_header, 0, sizeof(ChecksumHeader));
        free(data);
        FATAL("Could not read checksums block, continuing...\n");
        return;
    }

    payload_pos = 0;
    TRACE("Processing %u checksum entries", checksum_header.entries);
    for(j = 0; j < checksum_header.entries; j++)
    {
        if(payload_pos + sizeof(ChecksumEntry) > checksum_header.length)
        {
            FATAL("Checksum entry %d exceeds block payload size", j);
            break;
        }

        checksum_entry = (const ChecksumEntry *)&data[payload_pos];
        payload_pos += sizeof(ChecksumEntry);

        if(payload_pos + checksum_entry->length > checksum_header.length)
        {
            FATAL("Checksum payload for entry %d exceeds block payload size", j);
            break;
        }

        switch(checksum_entry->type)
        {
            case Md5:
                if(checksum_entry->length != MD5_DIGEST_LENGTH)
                {
                    FATAL("MD5 checksum entry has invalid length %u", checksum_entry->length);
                    break;
                }

                TRACE("Found MD5 checksum");
                memcpy(ctx->checksums.md5, &data[payload_pos], MD5_DIGEST_LENGTH);
                ctx->checksums.hasMd5 = true;
                break;
            case Sha1:
                if(checksum_entry->length != SHA1_DIGEST_LENGTH)
                {
                    FATAL("SHA1 checksum entry has invalid length %u", checksum_entry->length);
                    break;
                }

                TRACE("Found SHA1 checksum");
                memcpy(ctx->checksums.sha1, &data[payload_pos], SHA1_DIGEST_LENGTH);
                ctx->checksums.hasSha1 = true;
                break;
            case Sha256:
                if(checksum_entry->length != SHA256_DIGEST_LENGTH)
                {
                    FATAL("SHA256 checksum entry has invalid length %u", checksum_entry->length);
                    break;
                }

                TRACE("Found SHA256 checksum");
                memcpy(ctx->checksums.sha256, &data[payload_pos], SHA256_DIGEST_LENGTH);
                ctx->checksums.hasSha256 = true;
                break;
            case SpamSum:
                TRACE("Found SpamSum checksum of size %u", checksum_entry->length);
                free(ctx->checksums.spamsum);
                ctx->checksums.spamsum = NULL;

                ctx->checksums.spamsum = malloc(checksum_entry->length + 1);

                if(ctx->checksums.spamsum == NULL)
                {
                    FATAL("Could not allocate memory for SpamSum digest");
                    break;
                }

                memcpy(ctx->checksums.spamsum, &data[payload_pos], checksum_entry->length);
                ctx->checksums.spamsum[checksum_entry->length] = '\0';
                ctx->checksums.hasSpamSum                      = true;
                break;
            default:
                TRACE("Unknown checksum type %u, skipping", checksum_entry->type);
                break;
        }

        payload_pos += checksum_entry->length;
    }

    checksum_entry = NULL;
    free(data);

    TRACE("Exiting process_checksum_block()");
}