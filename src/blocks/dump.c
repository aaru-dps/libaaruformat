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

void process_dumphw_block(aaruformatContext *ctx, const IndexEntry *entry)
{
    int      pos       = 0;
    size_t   readBytes = 0;
    uint64_t crc64     = 0;
    uint16_t e         = 0;
    uint8_t *data      = NULL;

    // Check if the context and image stream are valid
    if(ctx == NULL || ctx->imageStream == NULL)
    {
        FATAL("Invalid context or image stream.\n");
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
    readBytes = fread(&ctx->dumpHardwareHeader, 1, sizeof(DumpHardwareHeader), ctx->imageStream);

    if(readBytes != sizeof(DumpHardwareHeader))
    {
        memset(&ctx->dumpHardwareHeader, 0, sizeof(DumpHardwareHeader));
        TRACE("Could not read dump hardware block header, continuing...\n");
        return;
    }

    if(ctx->dumpHardwareHeader.identifier != DumpHardwareBlock)
    {
        memset(&ctx->dumpHardwareHeader, 0, sizeof(DumpHardwareHeader));
        TRACE("Incorrect identifier for data block at position %" PRIu64 "\n", entry->offset);
    }

    data = (uint8_t *)malloc(ctx->dumpHardwareHeader.length);

    if(data == NULL)
    {
        memset(&ctx->dumpHardwareHeader, 0, sizeof(DumpHardwareHeader));
        TRACE("Could not allocate memory for dump hardware block, continuing...\n");
        return;
    }

    readBytes = fread(data, 1, ctx->dumpHardwareHeader.length, ctx->imageStream);

    if(readBytes == ctx->dumpHardwareHeader.length)
    {
        crc64 = aaruf_crc64_data(data, ctx->dumpHardwareHeader.length);

        // Due to how C# wrote it, it is effectively reversed
        if(ctx->header.imageMajorVersion <= AARUF_VERSION_V1) crc64 = bswap_64(crc64);

        if(crc64 != ctx->dumpHardwareHeader.crc64)
        {
            free(data);
            TRACE("Incorrect CRC found: 0x%" PRIx64 " found, expected 0x%" PRIx64 ", continuing...\n", crc64,
                  ctx->dumpHardwareHeader.crc64);
            return;
        }
    }

    free(data);
    fseek(ctx->imageStream, -(long)readBytes, SEEK_CUR);

    ctx->dumpHardwareEntriesWithData =
        (DumpHardwareEntriesWithData *)malloc(sizeof(DumpHardwareEntriesWithData) * ctx->dumpHardwareHeader.entries);

    if(ctx->dumpHardwareEntriesWithData == NULL)
    {
        memset(&ctx->dumpHardwareHeader, 0, sizeof(DumpHardwareHeader));
        TRACE("Could not allocate memory for dump hardware block, continuing...\n");
        return;
    }

    memset(ctx->dumpHardwareEntriesWithData, 0, sizeof(DumpHardwareEntriesWithData) * ctx->dumpHardwareHeader.entries);

    for(e = 0; e < ctx->dumpHardwareHeader.entries; e++)
    {
        readBytes = fread(&ctx->dumpHardwareEntriesWithData[e].entry, 1, sizeof(DumpHardwareEntry), ctx->imageStream);

        if(readBytes != sizeof(DumpHardwareEntry))
        {
            ctx->dumpHardwareHeader.entries = e;
            TRACE("Could not read dump hardware block entry, continuing...\n");
            break;
        }

        if(ctx->dumpHardwareEntriesWithData[e].entry.manufacturerLength > 0)
        {
            ctx->dumpHardwareEntriesWithData[e].manufacturer =
                (uint8_t *)malloc(ctx->dumpHardwareEntriesWithData[e].entry.manufacturerLength + 1);

            if(ctx->dumpHardwareEntriesWithData[e].manufacturer != NULL)
            {
                ctx->dumpHardwareEntriesWithData[e]
                    .manufacturer[ctx->dumpHardwareEntriesWithData[e].entry.manufacturerLength] = 0;
                readBytes = fread(ctx->dumpHardwareEntriesWithData[e].manufacturer, 1,
                                  ctx->dumpHardwareEntriesWithData[e].entry.manufacturerLength, ctx->imageStream);

                if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.manufacturerLength)
                {
                    free(ctx->dumpHardwareEntriesWithData[e].manufacturer);
                    ctx->dumpHardwareEntriesWithData[e].entry.manufacturerLength = 0;
                    TRACE("Could not read dump hardware block entry manufacturer, "
                          "continuing...\n");
                }
            }
        }

        if(ctx->dumpHardwareEntriesWithData[e].entry.modelLength > 0)
        {
            ctx->dumpHardwareEntriesWithData[e].model =
                (uint8_t *)malloc(ctx->dumpHardwareEntriesWithData[e].entry.modelLength + 1);

            if(ctx->dumpHardwareEntriesWithData[e].model != NULL)
            {
                ctx->dumpHardwareEntriesWithData[e].model[ctx->dumpHardwareEntriesWithData[e].entry.modelLength] = 0;
                readBytes = fread(ctx->dumpHardwareEntriesWithData[e].model, 1,
                                  ctx->dumpHardwareEntriesWithData[e].entry.modelLength, ctx->imageStream);

                if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.modelLength)
                {
                    free(ctx->dumpHardwareEntriesWithData[e].model);
                    ctx->dumpHardwareEntriesWithData[e].entry.modelLength = 0;
                    TRACE("Could not read dump hardware block entry model, continuing...\n");
                }
            }
        }

        if(ctx->dumpHardwareEntriesWithData[e].entry.revisionLength > 0)
        {
            ctx->dumpHardwareEntriesWithData[e].revision =
                (uint8_t *)malloc(ctx->dumpHardwareEntriesWithData[e].entry.revisionLength + 1);

            if(ctx->dumpHardwareEntriesWithData[e].revision != NULL)
            {
                ctx->dumpHardwareEntriesWithData[e].revision[ctx->dumpHardwareEntriesWithData[e].entry.revisionLength] =
                    0;
                readBytes = fread(ctx->dumpHardwareEntriesWithData[e].revision, 1,
                                  ctx->dumpHardwareEntriesWithData[e].entry.revisionLength, ctx->imageStream);

                if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.revisionLength)
                {
                    free(ctx->dumpHardwareEntriesWithData[e].revision);
                    ctx->dumpHardwareEntriesWithData[e].entry.revisionLength = 0;
                    TRACE("Could not read dump hardware block entry revision, "
                          "continuing...\n");
                }
            }
        }

        if(ctx->dumpHardwareEntriesWithData[e].entry.firmwareLength > 0)
        {
            ctx->dumpHardwareEntriesWithData[e].firmware =
                (uint8_t *)malloc(ctx->dumpHardwareEntriesWithData[e].entry.firmwareLength + 1);

            if(ctx->dumpHardwareEntriesWithData[e].firmware != NULL)
            {
                ctx->dumpHardwareEntriesWithData[e].firmware[ctx->dumpHardwareEntriesWithData[e].entry.firmwareLength] =
                    0;
                readBytes = fread(ctx->dumpHardwareEntriesWithData[e].firmware, 1,
                                  ctx->dumpHardwareEntriesWithData[e].entry.firmwareLength, ctx->imageStream);

                if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.firmwareLength)
                {
                    free(ctx->dumpHardwareEntriesWithData[e].firmware);
                    ctx->dumpHardwareEntriesWithData[e].entry.firmwareLength = 0;
                    TRACE("Could not read dump hardware block entry firmware, "
                          "continuing...\n");
                }
            }
        }

        if(ctx->dumpHardwareEntriesWithData[e].entry.serialLength > 0)
        {
            ctx->dumpHardwareEntriesWithData[e].serial =
                (uint8_t *)malloc(ctx->dumpHardwareEntriesWithData[e].entry.serialLength + 1);

            if(ctx->dumpHardwareEntriesWithData[e].serial != NULL)
            {
                ctx->dumpHardwareEntriesWithData[e].serial[ctx->dumpHardwareEntriesWithData[e].entry.serialLength] = 0;
                readBytes = fread(ctx->dumpHardwareEntriesWithData[e].serial, 1,
                                  ctx->dumpHardwareEntriesWithData[e].entry.serialLength, ctx->imageStream);

                if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.serialLength)
                {
                    free(ctx->dumpHardwareEntriesWithData[e].serial);
                    ctx->dumpHardwareEntriesWithData[e].entry.serialLength = 0;
                    TRACE("Could not read dump hardware block entry serial, continuing...\n");
                }
            }
        }

        if(ctx->dumpHardwareEntriesWithData[e].entry.softwareNameLength > 0)
        {
            ctx->dumpHardwareEntriesWithData[e].softwareName =
                (uint8_t *)malloc(ctx->dumpHardwareEntriesWithData[e].entry.softwareNameLength + 1);

            if(ctx->dumpHardwareEntriesWithData[e].softwareName != NULL)
            {
                ctx->dumpHardwareEntriesWithData[e]
                    .softwareName[ctx->dumpHardwareEntriesWithData[e].entry.softwareNameLength] = 0;
                readBytes = fread(ctx->dumpHardwareEntriesWithData[e].softwareName, 1,
                                  ctx->dumpHardwareEntriesWithData[e].entry.softwareNameLength, ctx->imageStream);

                if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.softwareNameLength)
                {
                    free(ctx->dumpHardwareEntriesWithData[e].softwareName);
                    ctx->dumpHardwareEntriesWithData[e].entry.softwareNameLength = 0;
                    TRACE("Could not read dump hardware block entry software name, "
                          "continuing...\n");
                }
            }
        }

        if(ctx->dumpHardwareEntriesWithData[e].entry.softwareVersionLength > 0)
        {
            ctx->dumpHardwareEntriesWithData[e].softwareVersion =
                (uint8_t *)malloc(ctx->dumpHardwareEntriesWithData[e].entry.softwareVersionLength + 1);

            if(ctx->dumpHardwareEntriesWithData[e].softwareVersion != NULL)
            {
                ctx->dumpHardwareEntriesWithData[e]
                    .softwareVersion[ctx->dumpHardwareEntriesWithData[e].entry.softwareVersionLength] = 0;
                readBytes = fread(ctx->dumpHardwareEntriesWithData[e].softwareVersion, 1,
                                  ctx->dumpHardwareEntriesWithData[e].entry.softwareVersionLength, ctx->imageStream);

                if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.softwareVersionLength)
                {
                    free(ctx->dumpHardwareEntriesWithData[e].softwareVersion);
                    ctx->dumpHardwareEntriesWithData[e].entry.softwareVersionLength = 0;
                    TRACE("Could not read dump hardware block entry software version, "
                          "continuing...\n");
                }
            }
        }

        if(ctx->dumpHardwareEntriesWithData[e].entry.softwareOperatingSystemLength > 0)
        {
            ctx->dumpHardwareEntriesWithData[e].softwareOperatingSystem =
                (uint8_t *)malloc(ctx->dumpHardwareEntriesWithData[e].entry.softwareOperatingSystemLength + 1);

            if(ctx->dumpHardwareEntriesWithData[e].softwareOperatingSystem != NULL)
            {
                ctx->dumpHardwareEntriesWithData[e]
                    .softwareOperatingSystem[ctx->dumpHardwareEntriesWithData[e].entry.softwareOperatingSystemLength] =
                    0;
                readBytes =
                    fread(ctx->dumpHardwareEntriesWithData[e].softwareOperatingSystem, 1,
                          ctx->dumpHardwareEntriesWithData[e].entry.softwareOperatingSystemLength, ctx->imageStream);

                if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.softwareOperatingSystemLength)
                {
                    free(ctx->dumpHardwareEntriesWithData[e].softwareOperatingSystem);
                    ctx->dumpHardwareEntriesWithData[e].entry.softwareOperatingSystemLength = 0;
                    TRACE("Could not read dump hardware block entry manufacturer, "
                          "continuing...\n");
                }
            }
        }

        ctx->dumpHardwareEntriesWithData[e].extents =
            (DumpExtent *)malloc(sizeof(DumpExtent) * ctx->dumpHardwareEntriesWithData->entry.extents);

        if(ctx->dumpHardwareEntriesWithData[e].extents == NULL)
        {
            TRACE("Could not allocate memory for dump hardware block extents, "
                  "continuing...\n");
            continue;
        }

        readBytes = fread(ctx->dumpHardwareEntriesWithData[e].extents, sizeof(DumpExtent),
                          ctx->dumpHardwareEntriesWithData[e].entry.extents, ctx->imageStream);

        if(readBytes != ctx->dumpHardwareEntriesWithData->entry.extents)
        {
            free(ctx->dumpHardwareEntriesWithData[e].extents);
            TRACE("Could not read dump hardware block extents, continuing...\n");
            continue;
        }

        // TODO: qsort()
    }
}