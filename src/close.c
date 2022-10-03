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
    if(ctx->imageStream != NULL) fclose(ctx->imageStream);

    free(ctx->sectorPrefix);
    free(ctx->sectorPrefixCorrected);
    free(ctx->sectorSuffix);
    free(ctx->sectorSuffixCorrected);
    free(ctx->sectorSubchannel);
    free(ctx->mode2Subheaders);

    if(ctx->mediaTagsTail != NULL)
    {
        dataLinkedList* mediaTag = ctx->mediaTagsTail;

        while(mediaTag->previous != NULL)
        {
            free(mediaTag->data);
            mediaTag = mediaTag->previous;
            free(mediaTag->next);
        }
    }

    if(ctx->mediaTagsHead != NULL)
    {
        free(ctx->mediaTagsHead->data);
        free(ctx->mediaTagsHead);
    }

#ifdef __linux__ // TODO: Implement
    if(!ctx->inMemoryDdt) { munmap(ctx->userDataDdt, ctx->mappedMemoryDdtSize); }
#endif

    free(ctx->sectorPrefixDdt);
    free(ctx->sectorSuffixDdt);

    free(ctx->metadataBlock);
    free(ctx->trackEntries);
    free(ctx->cicmBlock);

    if(ctx->dumpHardwareEntriesWithData != NULL)
    {
        for(i = 0; i < ctx->dumpHardwareHeader.entries; i++)
        {
            free(ctx->dumpHardwareEntriesWithData[i].extents);
            free(ctx->dumpHardwareEntriesWithData[i].manufacturer);
            free(ctx->dumpHardwareEntriesWithData[i].model);
            free(ctx->dumpHardwareEntriesWithData[i].revision);
            free(ctx->dumpHardwareEntriesWithData[i].firmware);
            free(ctx->dumpHardwareEntriesWithData[i].serial);
            free(ctx->dumpHardwareEntriesWithData[i].softwareName);
            free(ctx->dumpHardwareEntriesWithData[i].softwareVersion);
            free(ctx->dumpHardwareEntriesWithData[i].softwareOperatingSystem);
        }
    }

    free(ctx->readableSectorTags);

    free(context);

    return 0;
}