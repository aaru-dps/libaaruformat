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

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "aaruformat.h"
#include "log.h"

void process_checksum_block(aaruformatContext *ctx, const IndexEntry *entry)
{
    int                  pos       = 0;
    size_t               readBytes = 0;
    ChecksumHeader       checksum_header;
    ChecksumEntry const *checksum_entry = NULL;
    uint8_t             *data           = NULL;
    int                  j              = 0;

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        fprintf(stderr, "Invalid context or image stream.\n");
        return;
    }

    // Seek to block
    pos = fseek(ctx->imageStream, entry->offset, SEEK_SET);
    if(pos < 0 || ftell(ctx->imageStream) != entry->offset)
    {
        FATAL("Could not seek to %" PRIu64 " as indicated by index entry...\n", entry->offset);

        return;
    }

    // Even if those two checks shall have been done before
    readBytes = fread(&checksum_header, 1, sizeof(ChecksumHeader), ctx->imageStream);

    if(readBytes != sizeof(ChecksumHeader))
    {
        memset(&checksum_header, 0, sizeof(ChecksumHeader));
        FATAL("Could not read checksums block header, continuing...\n");
        return;
    }

    if(checksum_header.identifier != ChecksumBlock)
    {
        memset(&checksum_header, 0, sizeof(ChecksumHeader));
        FATAL("Incorrect identifier for checksum block at position %" PRIu64 "\n", entry->offset);
    }

    data = (uint8_t *)malloc(checksum_header.length);

    if(data == NULL)
    {
        memset(&checksum_header, 0, sizeof(ChecksumHeader));
        FATAL("Could not allocate memory for checksum block, continuing...\n");
        return;
    }

    readBytes = fread(data, 1, checksum_header.length, ctx->imageStream);

    if(readBytes != checksum_header.length)
    {
        memset(&checksum_header, 0, sizeof(ChecksumHeader));
        free(data);
        FATAL("Could not read checksums block, continuing...\n");
        return;
    }

    pos = 0;
    for(j = 0; j < checksum_header.entries; j++)
    {
        checksum_entry = (ChecksumEntry *)(&data[pos]);
        pos += sizeof(ChecksumEntry);

        if(checksum_entry->type == Md5)
        {
            memcpy(ctx->checksums.md5, &data[pos], MD5_DIGEST_LENGTH);
            ctx->checksums.hasMd5 = true;
        }
        else if(checksum_entry->type == Sha1)
        {
            memcpy(ctx->checksums.sha1, &data[pos], SHA1_DIGEST_LENGTH);
            ctx->checksums.hasSha1 = true;
        }
        else if(checksum_entry->type == Sha256)
        {
            memcpy(ctx->checksums.sha256, &data[pos], SHA256_DIGEST_LENGTH);
            ctx->checksums.hasSha256 = true;
        }
        else if(checksum_entry->type == SpamSum)
        {
            ctx->checksums.spamsum = malloc(checksum_entry->length + 1);

            if(ctx->checksums.spamsum != NULL)
            {
                memcpy(ctx->checksums.spamsum, &data[pos], checksum_entry->length);
                ctx->checksums.hasSpamSum = true;
            }

            ctx->checksums.spamsum[checksum_entry->length] = 0;
        }

        pos += checksum_entry->length;
    }

    checksum_entry = NULL;
    free(data);
}