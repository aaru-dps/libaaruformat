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

void process_tracks_block(aaruformatContext *ctx, const IndexEntry *entry)
{
    int      pos       = 0;
    size_t   readBytes = 0;
    uint64_t crc64     = 0;
    int      j = 0, k = 0;

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.");
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
    readBytes = fread(&ctx->tracksHeader, 1, sizeof(TracksHeader), ctx->imageStream);

    if(readBytes != sizeof(TracksHeader))
    {
        memset(&ctx->tracksHeader, 0, sizeof(TracksHeader));
        TRACE("Could not read tracks header, continuing...\n");
        return;
    }

    if(ctx->tracksHeader.identifier != TracksBlock)
    {
        memset(&ctx->tracksHeader, 0, sizeof(TracksHeader));
        TRACE("Incorrect identifier for data block at position %" PRIu64 "\n", entry->offset);
    }

    ctx->imageInfo.ImageSize += sizeof(TrackEntry) * ctx->tracksHeader.entries;

    ctx->trackEntries = (TrackEntry *)malloc(sizeof(TrackEntry) * ctx->tracksHeader.entries);

    if(ctx->trackEntries == NULL)
    {
        memset(&ctx->tracksHeader, 0, sizeof(TracksHeader));
        FATAL("Could not allocate memory for metadata block, continuing...\n");
        return;
    }

    readBytes = fread(ctx->trackEntries, sizeof(TrackEntry), ctx->tracksHeader.entries, ctx->imageStream);

    if(readBytes != ctx->tracksHeader.entries)
    {
        memset(&ctx->tracksHeader, 0, sizeof(TracksHeader));
        free(ctx->trackEntries);
        FATAL("Could not read metadata block, continuing...\n");

        return;
    }

    crc64 = aaruf_crc64_data((const uint8_t *)ctx->trackEntries, ctx->tracksHeader.entries * sizeof(TrackEntry));

    // Due to how C# wrote it, it is effectively reversed
    if(ctx->header.imageMajorVersion <= AARUF_VERSION_V1) crc64 = bswap_64(crc64);

    if(crc64 != ctx->tracksHeader.crc64)
    {
        TRACE("Incorrect CRC found: 0x%" PRIx64 " found, expected 0x%" PRIx64 ", continuing...\n", crc64,
              ctx->tracksHeader.crc64);
        return;
    }

    TRACE("Found %d tracks at position %" PRIu64 ".\n", ctx->tracksHeader.entries, entry->offset);

    ctx->imageInfo.HasPartitions = true;
    ctx->imageInfo.HasSessions   = true;

    ctx->numberOfDataTracks = 0;

    for(j = 0; j < ctx->tracksHeader.entries; j++)
    {
        if(ctx->trackEntries[j].sequence > 0 && ctx->trackEntries[j].sequence <= 99) ctx->numberOfDataTracks++;
    }

    ctx->dataTracks = malloc(sizeof(TrackEntry) * ctx->numberOfDataTracks);

    k = 0;
    for(j = 0; j < ctx->tracksHeader.entries; j++)
    {
        if(ctx->trackEntries[j].sequence > 0 && ctx->trackEntries[j].sequence <= 99)
            memcpy(&ctx->dataTracks[k++], &ctx->trackEntries[j], sizeof(TrackEntry));
    }
}