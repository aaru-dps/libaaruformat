// /***************************************************************************
// The Disc Image Chef
// ----------------------------------------------------------------------------
//
// Filename       : open.c
// Author(s)      : Natalia Portillo <claunia@claunia.com>
//
// Component      : libdicformat.
//
// --[ Description ] ----------------------------------------------------------
//
//     Handles opening DiscImageChef format disk images.
//
// --[ License ] --------------------------------------------------------------
//
//     This library is free software; you can redistribute it and/or modify
//     it under the terms of the GNU Lesser General Public License as
//     published by the Free Software Foundation; either version 2.1 of the
//     License, or (at your option) any later version.
//
//     This library is distributed in the hope that it will be useful, but
//     WITHOUT ANY WARRANTY; without even the implied warranty of
//     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
//     Lesser General Public License for more details.
//
//     You should have received a copy of the GNU Lesser General Public
//     License along with this library; if not, see <http://www.gnu.org/licenses/>.
//
// ----------------------------------------------------------------------------
// Copyright © 2011-2019 Natalia Portillo
// ****************************************************************************/

#include <stdio.h>
#include <dicformat.h>
#include <malloc.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <stdbool.h>
#include <sys/mman.h>

// TODO: Check CRC64 on structures
// TODO: ImageInfo
void *open(const char *filepath)
{
    dicformatContext *ctx             = malloc(sizeof(dicformatContext));
    int              errorNo;
    size_t           readBytes;
    long             pos;
    IndexHeader      idxHeader;
    IndexEntry       *idxEntries;
    uint64_t         ImageSize; // TODO: This should be in ImageInfo
    unsigned char    *data;

    memset(ctx, 0, sizeof(dicformatContext));

    if(ctx == NULL)
        return NULL;

    ctx->imageStream = fopen(filepath, "rb");

    if(ctx->imageStream == NULL)
    {
        errorNo = errno;
        free(ctx);
        errno = errorNo;

        return NULL;
    }

    fseek(ctx->imageStream, 0, SEEK_SET);
    readBytes = fread(&ctx->header, sizeof(DicHeader), 1, ctx->imageStream);

    if(readBytes != sizeof(DicHeader))
    {
        free(ctx);
        errno = DICF_ERROR_FILE_TOO_SMALL;

        return NULL;
    }

    if(ctx->header.identifier != DIC_MAGIC)
    {
        free(ctx);
        errno = DICF_ERROR_NOT_DICFORMAT;

        return NULL;
    }

    if(ctx->header.imageMajorVersion > DICF_VERSION)
    {
        free(ctx);
        errno = DICF_ERROR_INCOMPATIBLE_VERSION;

        return NULL;
    }

    fprintf(stderr,
            "libdicformat: Opening image version %d.%d",
            ctx->header.imageMajorVersion,
            ctx->header.imageMinorVersion);

    // Read the index header
    pos = fseek(ctx->imageStream, ctx->header.indexOffset, SEEK_CUR);
    if(pos < 0)
    {
        free(ctx);
        errno = DICF_ERROR_CANNOT_READ_INDEX;

        return NULL;
    }

    pos = ftell(ctx->imageStream);
    if(pos != ctx->header.indexOffset)
    {
        free(ctx);
        errno = DICF_ERROR_CANNOT_READ_INDEX;

        return NULL;
    }

    readBytes = fread(&idxHeader, sizeof(IndexHeader), 1, ctx->imageStream);

    if(readBytes != sizeof(IndexHeader) || idxHeader.identifier != IndexBlock)
    {
        free(ctx);
        errno = DICF_ERROR_CANNOT_READ_INDEX;

        return NULL;
    }

    fprintf(stderr, "libdicformat: Index at %"PRIu64" contains %d entries", ctx->header.indexOffset, idxHeader.entries);

    idxEntries = malloc(sizeof(IndexEntry) * idxHeader.entries);

    if(idxEntries == NULL)
    {
        errorNo = errno;
        free(ctx);
        errno = errorNo;

        return NULL;
    }

    memset(idxEntries, 0, sizeof(IndexEntry) * idxHeader.entries);
    readBytes = fread(idxEntries, sizeof(IndexEntry), idxHeader.entries, ctx->imageStream);

    if(readBytes != sizeof(IndexEntry) * idxHeader.entries)
    {
        free(idxEntries);
        free(ctx);
        errno = DICF_ERROR_CANNOT_READ_INDEX;

        return NULL;
    }

    for(int i = 0; i < idxHeader.entries; i++)
    {
        fprintf(stderr,
                "libdicformat: Block type %4s with data type %4s is indexed to be at %"PRIu64"",
                (char *)&idxEntries[i].blockType,
                (char *)&idxEntries[i].dataType,
                idxEntries[i].offset);
    }

    bool             foundUserDataDdt = false;
    ImageSize = 0;
    for(int i = 0; i < idxHeader.entries; i++)
    {
        pos = fseek(ctx->imageStream, idxEntries[i].offset, SEEK_SET);

        if(pos < 0 || ftell(ctx->imageStream) != idxEntries[i].offset)
        {
            fprintf(stderr,
                    "libdicformat: Could not seek to %"PRIu64" as indicated by index entry %d, continuing...",
                    idxEntries[i].offset,
                    i);

            continue;
        }

        BlockHeader blockHeader;
        DdtHeader   ddtHeader;

        switch(idxEntries[i].blockType)
        {
            case DataBlock:
                // NOP block, skip
                if(idxEntries[i].dataType == NoData)
                    break;

                readBytes = fread(&blockHeader, sizeof(BlockHeader), 1, ctx->imageStream);

                if(readBytes != sizeof(BlockHeader))
                {
                    fprintf(stderr, "libdicformat: Could not read block header at %"PRIu64"", idxEntries[i].offset);

                    break;
                }

                ImageSize += blockHeader.cmpLength;

                // Unused, skip
                if(idxEntries[i].dataType == UserData)
                {
                    // TODO: ImageInfo
                    //if(blockHeader.sectorSize > imageInfo.SectorSize)
                    //    imageInfo.SectorSize = blockHeader.sectorSize;

                    break;
                }

                if(blockHeader.identifier != idxEntries[i].blockType)
                {
                    fprintf(stderr,
                            "libdicformat: Incorrect identifier for data block at position %"PRIu64"",
                            idxEntries[i].offset);
                    break;
                }

                if(blockHeader.type != idxEntries[i].dataType)
                {
                    fprintf(stderr,
                            "libdicformat: Expected block with data type %4s at position %"PRIu64" but found data type %4s",
                            (char *)&idxEntries[i].blockType,
                            idxEntries[i].offset,
                            (char *)&blockHeader.type);
                    break;
                }

                fprintf(stderr,
                        "libdicformat: Found data block with type %4s at position %"PRIu64"",
                        (char *)&idxEntries[i].blockType,
                        idxEntries[i].offset);

                if(blockHeader.compression == None)
                {
                    data = malloc(blockHeader.length);
                    if(data == NULL)
                    {
                        fprintf(stderr, "Cannot allocate memory for block, continuing...");
                        break;
                    }

                    readBytes = fread(data, blockHeader.length, 1, ctx->imageStream);

                    if(readBytes != blockHeader.length)
                    {
                        free(data);
                        fprintf(stderr, "Could not read block, continuing...");
                        break;
                    }
                }
                else
                {
                    fprintf(stderr,
                            "libdicformat: Found unknown compression type %d, continuing...",
                            blockHeader.compression);
                    break;
                }

                // TODO: Check CRC, if not correct, skip it

                dataLinkedList *mediaTag = NULL;

                // Check if it's not a media tag, but a sector tag, and fill the appropriate table then
                switch(idxEntries[i].dataType)
                {
                    case CdSectorPrefix:
                    case CdSectorPrefixCorrected:
                        if(idxEntries[i].dataType == CdSectorPrefixCorrected)
                        {
                            ctx->sectorPrefixCorrected = data;
                        }
                        else
                            ctx->sectorPrefix = data;

                        // TODO: ReadableSectorTags
                        /*
                                                if(!imageInfo.ReadableSectorTags.Contains(SectorTagType.CdSectorSync))
                                                    imageInfo.ReadableSectorTags.Add(SectorTagType.CdSectorSync);
                                                if(!imageInfo.ReadableSectorTags.Contains(SectorTagType.CdSectorHeader))
                                                    imageInfo.ReadableSectorTags.Add(SectorTagType.CdSectorHeader);
                        */

                        break;
                    case CdSectorSuffix:
                    case CdSectorSuffixCorrected:
                        if(idxEntries[i].dataType == CdSectorSuffixCorrected)
                            ctx->sectorSuffixCorrected = data;
                        else
                            ctx->sectorSuffix = data;

                        // TODO: ReadableSectorTags
                        /*
                            if(!imageInfo.ReadableSectorTags.Contains(SectorTagType.CdSectorSubHeader))
                                imageInfo.ReadableSectorTags.Add(SectorTagType.CdSectorSubHeader);
                            if(!imageInfo.ReadableSectorTags.Contains(SectorTagType.CdSectorEcc))
                                imageInfo.ReadableSectorTags.Add(SectorTagType.CdSectorEcc);
                            if(!imageInfo.ReadableSectorTags.Contains(SectorTagType.CdSectorEccP))
                                imageInfo.ReadableSectorTags.Add(SectorTagType.CdSectorEccP);
                            if(!imageInfo.ReadableSectorTags.Contains(SectorTagType.CdSectorEccQ))
                                imageInfo.ReadableSectorTags.Add(SectorTagType.CdSectorEccQ);
                            if(!imageInfo.ReadableSectorTags.Contains(SectorTagType.CdSectorEdc))
                                imageInfo.ReadableSectorTags.Add(SectorTagType.CdSectorEdc);
                        */
                        break;
                    case CdSectorSubchannel:ctx->sectorSubchannel = data;
                        // TODO: ReadableSectorTags
                        /*
                            if(!imageInfo.ReadableSectorTags.Contains(SectorTagType.CdSectorSubchannel))
                                imageInfo.ReadableSectorTags.Add(SectorTagType.CdSectorSubchannel);
                        */
                        break;
                    case AppleProfileTag:
                    case AppleSonyTag:
                    case PriamDataTowerTag:ctx->sectorSubchannel = data;
                        // TODO: ReadableSectorTags
                        /*
                            if(!imageInfo.ReadableSectorTags.Contains(SectorTagType.AppleSectorTag))
                                imageInfo.ReadableSectorTags.Add(SectorTagType.AppleSectorTag);
                        */
                        break;
                    case CompactDiscMode2Subheader:ctx->mode2Subheaders = data;
                        break;
                    default:
                        if(ctx->mediaTagsHead != NULL)
                        {
                            mediaTag = ctx->mediaTagsHead;
                            while(mediaTag != NULL)
                            {
                                if(mediaTag->type == blockHeader.type)
                                {
                                    fprintf(stderr,
                                            "libdicformat: Media tag type %d duplicated, removing previous entry...",
                                            blockHeader.type);
                                    free(mediaTag->data);
                                    mediaTag->data = data;
                                    break;
                                }

                                mediaTag = mediaTag->next;
                            }
                        }

                        // If we mediaTag is NULL means we have arrived the end of the list without finding a duplicate or the list was empty
                        if(mediaTag != NULL)
                            break;

                        mediaTag = malloc(sizeof(dataLinkedList));

                        if(mediaTag == NULL)
                        {
                            fprintf(stderr, "libdicformat: Cannot allocate memory for media tag list entry.");
                            break;
                        }
                        memset(mediaTag, 0, sizeof(dataLinkedList));

                        // TODO: MediaTagType
                        // MediaTagType mediaTagType = GetMediaTagTypeForDataType(blockHeader.type);

                        mediaTag->type = blockHeader.type;
                        mediaTag->data = data;

                        if(ctx->mediaTagsHead == NULL)
                        {
                            ctx->mediaTagsHead = mediaTag;
                        }
                        else
                        {
                            mediaTag->previous       = ctx->mediaTagsTail;
                            ctx->mediaTagsTail->next = mediaTag;
                        }

                        ctx->mediaTagsTail = mediaTag;

                        break;
                }

                break;
            case DeDuplicationTable:readBytes = fread(&ddtHeader, sizeof(DdtHeader), 1, ctx->imageStream);

                if(readBytes != sizeof(DdtHeader))
                {
                    fprintf(stderr, "libdicformat: Could not read block header at %"PRIu64"", idxEntries[i].offset);

                    break;
                }

                foundUserDataDdt = true;

                if(idxEntries[i].dataType == UserData)
                {
                    ctx->shift = ddtHeader.shift;

                    // Check for DDT compression
                    switch(ddtHeader.compression)
                    {
                        case None:ctx->mappedMemoryDdtSize = sizeof(uint64_t) * ddtHeader.entries;
                            ctx->userDataDdt =
                                    mmap(NULL,
                                         ctx->mappedMemoryDdtSize,
                                         PROT_READ,
                                         MAP_SHARED,
                                         fileno(ctx->imageStream),
                                         idxEntries[i].offset + sizeof(ddtHeader));

                            if(ctx->userDataDdt == MAP_FAILED)
                            {
                                foundUserDataDdt = false;
                                fprintf(stderr, "libdicformat: Could not read map deduplication table.");
                                break;
                            }

                            ctx->inMemoryDdt = false;
                            break;
                        default:
                            fprintf(stderr,
                                    "libdicformat: Found unknown compression type %d, continuing...",
                                    blockHeader.compression);
                            foundUserDataDdt = false;
                            break;

                    }
                }
                break;
            case GeometryBlock:
                // TODO
                break;
            case MetadataBlock:
                // TODO
                break;
            case TracksBlock:
                // TODO
                break;
            case CicmBlock:
                // TODO
                break;
            case DumpHardwareBlock:
                // TODO
                break;
            default:
                fprintf(stderr,
                        "libdicformat: Unhandled block type %4s with data type %4s is indexed to be at %"PRIu64"",
                        (char *)&idxEntries[i].blockType,
                        (char *)&idxEntries[i].dataType,
                        idxEntries[i].offset);
                break;
        }
    }

    ctx->magic               = DIC_MAGIC;
    ctx->libraryMajorVersion = LIBDICFORMAT_MAJOR_VERSION;
    ctx->libraryMinorVersion = LIBDICFORMAT_MINOR_VERSION;

    free(idxEntries);
    return ctx;
}