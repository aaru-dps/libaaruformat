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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __linux__
#include <sys/mman.h>
#endif

#include <aaruformat.h>

int aaruf_close(void* context)
{
    int i;

    if(context == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    aaruformatContext* ctx = context;

    // Not a libaaruformat context
    if(ctx->magic != AARU_MAGIC)
    {
        errno = EINVAL;
        return -1;
    }

    // This may do nothing if imageStream is NULL, but as the behaviour is undefined, better sure than sorry
    if(ctx->imageStream != NULL)
    {
        fclose(ctx->imageStream);
        ctx->imageStream = NULL;
    }

    free(ctx->sectorPrefix);
    ctx->sectorPrefix = NULL;
    free(ctx->sectorPrefixCorrected);
    ctx->sectorPrefixCorrected = NULL;
    free(ctx->sectorSuffix);
    ctx->sectorSuffix = NULL;
    free(ctx->sectorSuffixCorrected);
    ctx->sectorSuffixCorrected = NULL;
    free(ctx->sectorSubchannel);
    ctx->sectorSubchannel = NULL;
    free(ctx->mode2Subheaders);
    ctx->mode2Subheaders = NULL;

    if(ctx->mediaTagsTail != NULL)
    {
        dataLinkedList* mediaTag = ctx->mediaTagsTail;

        while(mediaTag->previous != NULL)
        {
            free(mediaTag->data);
            mediaTag->data = NULL;
            mediaTag       = mediaTag->previous;
            free(mediaTag->next);
            mediaTag->next = NULL;
        }

        ctx->mediaTagsTail = NULL;
    }

    if(ctx->mediaTagsHead != NULL)
    {
        free(ctx->mediaTagsHead->data);
        ctx->mediaTagsHead->data = NULL;
        free(ctx->mediaTagsHead);
        ctx->mediaTagsHead = NULL;
    }

#ifdef __linux__ // TODO: Implement
    if(!ctx->inMemoryDdt)
    {
        munmap(ctx->userDataDdt, ctx->mappedMemoryDdtSize);
        ctx->userDataDdt = NULL;
    }
#endif

    free(ctx->sectorPrefixDdt);
    ctx->sectorPrefixDdt = NULL;
    free(ctx->sectorSuffixDdt);
    ctx->sectorSuffixDdt = NULL;

    free(ctx->metadataBlock);
    ctx->metadataBlock = NULL;
    free(ctx->trackEntries);
    ctx->trackEntries = NULL;
    free(ctx->cicmBlock);
    ctx->cicmBlock = NULL;

    if(ctx->dumpHardwareEntriesWithData != NULL)
    {
        for(i = 0; i < ctx->dumpHardwareHeader.entries; i++)
        {
            free(ctx->dumpHardwareEntriesWithData[i].extents);
            ctx->dumpHardwareEntriesWithData[i].extents = NULL;
            free(ctx->dumpHardwareEntriesWithData[i].manufacturer);
            ctx->dumpHardwareEntriesWithData[i].manufacturer = NULL;
            free(ctx->dumpHardwareEntriesWithData[i].model);
            ctx->dumpHardwareEntriesWithData[i].model = NULL;
            free(ctx->dumpHardwareEntriesWithData[i].revision);
            ctx->dumpHardwareEntriesWithData[i].revision = NULL;
            free(ctx->dumpHardwareEntriesWithData[i].firmware);
            ctx->dumpHardwareEntriesWithData[i].firmware = NULL;
            free(ctx->dumpHardwareEntriesWithData[i].serial);
            ctx->dumpHardwareEntriesWithData[i].serial = NULL;
            free(ctx->dumpHardwareEntriesWithData[i].softwareName);
            ctx->dumpHardwareEntriesWithData[i].softwareName = NULL;
            free(ctx->dumpHardwareEntriesWithData[i].softwareVersion);
            ctx->dumpHardwareEntriesWithData[i].softwareVersion = NULL;
            free(ctx->dumpHardwareEntriesWithData[i].softwareOperatingSystem);
            ctx->dumpHardwareEntriesWithData[i].softwareOperatingSystem = NULL;
        }
        ctx->dumpHardwareEntriesWithData = NULL;
    }

    free(ctx->readableSectorTags);
    ctx->readableSectorTags = NULL;

    free(ctx->eccCdContext);
    ctx->eccCdContext = NULL;

    // TODO: Free caches

    free(context);

    return 0;
}