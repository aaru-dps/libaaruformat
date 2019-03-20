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
        fprintf(stderr, "libdicformat: Block type %4.4s with data type %4.4s is indexed to be at %"PRIu64"",
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
                            "libdicformat: Expected block with data type %4.4s at position %"PRIu64" but found data type %4.4s",
                            (char *)&idxEntries[i].blockType,
                            idxEntries[i].offset,
                            (char *)&blockHeader.type);
                    break;
                }

                fprintf(stderr, "libdicformat: Found data block with type %4.4s at position %"PRIu64"",
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
                else if(idxEntries[i].dataType == CdSectorPrefixCorrected ||
                        idxEntries[i].dataType == CdSectorSuffixCorrected)
                {
                    switch(ddtHeader.compression)
                    {
                        case None:data = malloc(ddtHeader.entries * sizeof(uint32_t));

                            if(mediaTag == NULL)
                            {
                                fprintf(stderr, "libdicformat: Cannot allocate memory for deduplication table.");
                                break;
                            }

                            readBytes = fread(data, ddtHeader.entries * sizeof(uint32_t), 1, ctx->imageStream);

                            if(readBytes != ddtHeader.entries * sizeof(uint32_t))
                            {
                                free(data);
                                fprintf(stderr, "libdicformat: Could not read deduplication table, continuing...");
                                break;
                            }

                            if(idxEntries[i].dataType == CdSectorPrefixCorrected)
                                ctx->sectorPrefixCorrected = data;
                            else if(idxEntries[i].dataType == CdSectorSuffixCorrected)
                                ctx->sectorPrefixCorrected = data;

                            break;
                        default:
                            fprintf(stderr,
                                    "libdicformat: Found unknown compression type %d, continuing...",
                                    blockHeader.compression);
                            break;
                    }
                }
                break;
                // Logical geometry block. It doesn't have a CRC coz, well, it's not so important
            case GeometryBlock:readBytes = fread(&ctx->geometryBlock, sizeof(GeometryBlockHeader), 1, ctx->imageStream);

                if(readBytes != sizeof(GeometryBlockHeader))
                {
                    memset(&ctx->geometryBlock, 0, sizeof(GeometryBlockHeader));
                    fprintf(stderr, "libdicformat: Could not read geometry block, continuing...");
                    break;
                }

                if(ctx->geometryBlock.identifier == GeometryBlock)
                {
                    fprintf(stderr,
                            "libdicformat: Geometry set to %d cylinders %d heads %d sectors per track",
                            ctx->geometryBlock.cylinders,
                            ctx->geometryBlock.heads,
                            ctx->geometryBlock.sectorsPerTrack);
                    // TODO: ImageInfo
                    /*
                        imageInfo.Cylinders       = geometryBlock.cylinders;
                        imageInfo.Heads           = geometryBlock.heads;
                        imageInfo.SectorsPerTrack = geometryBlock.sectorsPerTrack;
                    */
                }
                else
                    memset(&ctx->geometryBlock, 0, sizeof(GeometryBlockHeader));

                break;
                // Metadata block
            case MetadataBlock: readBytes =
                                        fread(&ctx->metadataBlockHeader,
                                              sizeof(MetadataBlockHeader),
                                              1,
                                              ctx->imageStream);

                if(readBytes != sizeof(MetadataBlockHeader))
                {
                    memset(&ctx->metadataBlockHeader, 0, sizeof(MetadataBlockHeader));
                    fprintf(stderr, "libdicformat: Could not read metadata block header, continuing...");
                    break;
                }

                if(ctx->metadataBlockHeader.identifier != idxEntries[i].blockType)
                {
                    memset(&ctx->metadataBlockHeader, 0, sizeof(MetadataBlockHeader));
                    fprintf(stderr,
                            "libdicformat: Incorrect identifier for data block at position %"PRIu64"",
                            idxEntries[i].offset);
                    break;
                }

                ctx->metadataBlock = malloc(ctx->metadataBlockHeader.blockSize);

                if(ctx->metadataBlock == NULL)
                {
                    memset(&ctx->metadataBlockHeader, 0, sizeof(MetadataBlockHeader));
                    fprintf(stderr, "libdicformat: Could not allocate memory for metadata block, continuing...");
                    break;
                }

                readBytes = fread(ctx->metadataBlock, ctx->metadataBlockHeader.blockSize, 1, ctx->imageStream);

                if(readBytes != ctx->metadataBlockHeader.blockSize)
                {
                    memset(&ctx->metadataBlockHeader, 0, sizeof(MetadataBlockHeader));
                    free(ctx->metadataBlock);
                    fprintf(stderr, "libdicformat: Could not read metadata block, continuing...");
                }

                break;
            case TracksBlock: readBytes = fread(&ctx->tracksHeader, sizeof(TracksHeader), 1, ctx->imageStream);

                if(readBytes != sizeof(TracksHeader))
                {
                    memset(&ctx->tracksHeader, 0, sizeof(TracksHeader));
                    fprintf(stderr, "libdicformat: Could not read tracks header, continuing...");
                    break;
                }

                if(ctx->tracksHeader.identifier != TracksBlock)
                {
                    memset(&ctx->tracksHeader, 0, sizeof(TracksHeader));
                    fprintf(stderr,
                            "libdicformat: Incorrect identifier for data block at position %"PRIu64"",
                            idxEntries[i].offset);
                }

                ctx->trackEntries = malloc(sizeof(TrackEntry) * ctx->tracksHeader.entries);

                if(ctx->trackEntries == NULL)
                {
                    memset(&ctx->tracksHeader, 0, sizeof(TracksHeader));
                    fprintf(stderr, "libdicformat: Could not allocate memory for metadata block, continuing...");
                    break;
                }

                readBytes = fread(ctx->trackEntries, sizeof(TrackEntry), ctx->tracksHeader.entries, ctx->imageStream);

                if(readBytes != ctx->metadataBlockHeader.blockSize)
                {
                    memset(&ctx->tracksHeader, 0, sizeof(TracksHeader));
                    free(ctx->trackEntries);
                    fprintf(stderr, "libdicformat: Could not read metadata block, continuing...");
                }

                fprintf(stderr,
                        "libdicformat: Found %d tracks at position %"PRIu64".",
                        ctx->tracksHeader.entries,
                        idxEntries[i].offset);

                // TODO: Cache flags and ISRCs
                // TODO: ImageInfo

                break;
                // CICM XML metadata block
            case CicmBlock:readBytes = fread(&ctx->cicmBlockHeader, sizeof(CicmMetadataBlock), 1, ctx->imageStream);

                if(readBytes != sizeof(CicmMetadataBlock))
                {
                    memset(&ctx->cicmBlockHeader, 0, sizeof(CicmMetadataBlock));
                    fprintf(stderr, "libdicformat: Could not read CICM XML metadata header, continuing...");
                    break;
                }

                if(ctx->cicmBlockHeader.identifier != CicmBlock)
                {
                    memset(&ctx->cicmBlockHeader, 0, sizeof(CicmMetadataBlock));
                    fprintf(stderr,
                            "libdicformat: Incorrect identifier for data block at position %"PRIu64"",
                            idxEntries[i].offset);
                }

                ctx->cicmBlock = malloc(ctx->cicmBlockHeader.length);

                if(ctx->cicmBlock == NULL)
                {
                    memset(&ctx->cicmBlockHeader, 0, sizeof(CicmMetadataBlock));
                    fprintf(stderr,
                            "libdicformat: Could not allocate memory for CICM XML metadata block, continuing...");
                    break;
                }

                readBytes = fread(ctx->cicmBlock, ctx->cicmBlockHeader.length, 1, ctx->imageStream);

                if(readBytes != ctx->metadataBlockHeader.blockSize)
                {
                    memset(&ctx->cicmBlockHeader, 0, sizeof(CicmMetadataBlock));
                    free(ctx->cicmBlock);
                    fprintf(stderr, "libdicformat: Could not read CICM XML metadata block, continuing...");
                }

                fprintf(stderr, "libdicformat: Found CICM XML metadata block %"PRIu64".", idxEntries[i].offset);
                break;
                // Dump hardware block
            case DumpHardwareBlock: readBytes =
                                            fread(&ctx->dumpHardwareHeader,
                                                  sizeof(DumpHardwareHeader),
                                                  1,
                                                  ctx->imageStream);

                if(readBytes != sizeof(DumpHardwareHeader))
                {
                    memset(&ctx->dumpHardwareHeader, 0, sizeof(DumpHardwareHeader));
                    fprintf(stderr, "libdicformat: Could not read dump hardware block header, continuing...");
                    break;
                }

                if(ctx->dumpHardwareHeader.identifier != DumpHardwareBlock)
                {
                    memset(&ctx->dumpHardwareHeader, 0, sizeof(DumpHardwareHeader));
                    fprintf(stderr,
                            "libdicformat: Incorrect identifier for data block at position %"PRIu64"",
                            idxEntries[i].offset);
                }

                ctx->dumpHardwareEntriesWithData =
                        malloc(sizeof(DumpHardwareEntriesWithData) * ctx->dumpHardwareHeader.entries);

                if(ctx->dumpHardwareEntriesWithData == NULL)
                {
                    memset(&ctx->dumpHardwareHeader, 0, sizeof(DumpHardwareHeader));
                    fprintf(stderr, "libdicformat: Could not allocate memory for dump hardware block, continuing...");
                    break;
                }

                memset(ctx->dumpHardwareEntriesWithData,
                       0,
                       sizeof(DumpHardwareEntriesWithData) * ctx->dumpHardwareHeader.entries);

                for(uint16_t e = 0; e < ctx->dumpHardwareHeader.entries; e++)
                {
                    readBytes =
                            fread(&ctx->dumpHardwareEntriesWithData[e].entry,
                                  sizeof(DumpHardwareEntry),
                                  1,
                                  ctx->imageStream);

                    if(readBytes != sizeof(DumpHardwareEntry))
                    {
                        ctx->dumpHardwareHeader.entries = e + (uint16_t)1;
                        fprintf(stderr, "libdicformat: Could not read dump hardware block entry, continuing...");
                        break;
                    }

                    if(ctx->dumpHardwareEntriesWithData[e].entry.manufacturerLength > 0)
                    {
                        ctx->dumpHardwareEntriesWithData[e].manufacturer =
                                malloc(ctx->dumpHardwareEntriesWithData[e].entry.manufacturerLength);

                        if(ctx->dumpHardwareEntriesWithData[e].manufacturer != NULL)
                        {
                            readBytes =
                                    fread(&ctx->dumpHardwareEntriesWithData[e].manufacturer,
                                          ctx->dumpHardwareEntriesWithData[e].entry.manufacturerLength,
                                          1,
                                          ctx->imageStream);

                            if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.manufacturerLength)
                            {
                                free(ctx->dumpHardwareEntriesWithData[e].manufacturer);
                                ctx->dumpHardwareEntriesWithData[e].entry.manufacturerLength = 0;
                                fprintf(stderr,
                                        "libdicformat: Could not read dump hardware block entry manufacturer, continuing...");
                            }
                        }
                    }

                    if(ctx->dumpHardwareEntriesWithData[e].entry.modelLength > 0)
                    {
                        ctx->dumpHardwareEntriesWithData[e].model =
                                malloc(ctx->dumpHardwareEntriesWithData[e].entry.modelLength);

                        if(ctx->dumpHardwareEntriesWithData[e].model != NULL)
                        {
                            readBytes =
                                    fread(&ctx->dumpHardwareEntriesWithData[e].model,
                                          ctx->dumpHardwareEntriesWithData[e].entry.modelLength,
                                          1,
                                          ctx->imageStream);

                            if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.modelLength)
                            {
                                free(ctx->dumpHardwareEntriesWithData[e].model);
                                ctx->dumpHardwareEntriesWithData[e].entry.modelLength = 0;
                                fprintf(stderr,
                                        "libdicformat: Could not read dump hardware block entry model, continuing...");
                            }
                        }
                    }

                    if(ctx->dumpHardwareEntriesWithData[e].entry.revisionLength > 0)
                    {
                        ctx->dumpHardwareEntriesWithData[e].revision =
                                malloc(ctx->dumpHardwareEntriesWithData[e].entry.revisionLength);

                        if(ctx->dumpHardwareEntriesWithData[e].revision != NULL)
                        {
                            readBytes =
                                    fread(&ctx->dumpHardwareEntriesWithData[e].revision,
                                          ctx->dumpHardwareEntriesWithData[e].entry.revisionLength,
                                          1,
                                          ctx->imageStream);

                            if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.revisionLength)
                            {
                                free(ctx->dumpHardwareEntriesWithData[e].revision);
                                ctx->dumpHardwareEntriesWithData[e].entry.revisionLength = 0;
                                fprintf(stderr,
                                        "libdicformat: Could not read dump hardware block entry revision, continuing...");
                            }
                        }
                    }

                    if(ctx->dumpHardwareEntriesWithData[e].entry.firmwareLength > 0)
                    {
                        ctx->dumpHardwareEntriesWithData[e].firmware =
                                malloc(ctx->dumpHardwareEntriesWithData[e].entry.firmwareLength);

                        if(ctx->dumpHardwareEntriesWithData[e].firmware != NULL)
                        {
                            readBytes =
                                    fread(&ctx->dumpHardwareEntriesWithData[e].firmware,
                                          ctx->dumpHardwareEntriesWithData[e].entry.firmwareLength,
                                          1,
                                          ctx->imageStream);

                            if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.firmwareLength)
                            {
                                free(ctx->dumpHardwareEntriesWithData[e].firmware);
                                ctx->dumpHardwareEntriesWithData[e].entry.firmwareLength = 0;
                                fprintf(stderr,
                                        "libdicformat: Could not read dump hardware block entry firmware, continuing...");
                            }
                        }
                    }

                    if(ctx->dumpHardwareEntriesWithData[e].entry.serialLength > 0)
                    {
                        ctx->dumpHardwareEntriesWithData[e].serial =
                                malloc(ctx->dumpHardwareEntriesWithData[e].entry.serialLength);

                        if(ctx->dumpHardwareEntriesWithData[e].serial != NULL)
                        {
                            readBytes =
                                    fread(&ctx->dumpHardwareEntriesWithData[e].serial,
                                          ctx->dumpHardwareEntriesWithData[e].entry.serialLength,
                                          1,
                                          ctx->imageStream);

                            if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.serialLength)
                            {
                                free(ctx->dumpHardwareEntriesWithData[e].serial);
                                ctx->dumpHardwareEntriesWithData[e].entry.serialLength = 0;
                                fprintf(stderr,
                                        "libdicformat: Could not read dump hardware block entry serial, continuing...");
                            }
                        }
                    }

                    if(ctx->dumpHardwareEntriesWithData[e].entry.softwareNameLength > 0)
                    {
                        ctx->dumpHardwareEntriesWithData[e].softwareName =
                                malloc(ctx->dumpHardwareEntriesWithData[e].entry.softwareNameLength);

                        if(ctx->dumpHardwareEntriesWithData[e].softwareName != NULL)
                        {
                            readBytes =
                                    fread(&ctx->dumpHardwareEntriesWithData[e].softwareName,
                                          ctx->dumpHardwareEntriesWithData[e].entry.softwareNameLength,
                                          1,
                                          ctx->imageStream);

                            if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.softwareNameLength)
                            {
                                free(ctx->dumpHardwareEntriesWithData[e].softwareName);
                                ctx->dumpHardwareEntriesWithData[e].entry.softwareNameLength = 0;
                                fprintf(stderr,
                                        "libdicformat: Could not read dump hardware block entry software name, continuing...");
                            }
                        }
                    }

                    if(ctx->dumpHardwareEntriesWithData[e].entry.softwareVersionLength > 0)
                    {
                        ctx->dumpHardwareEntriesWithData[e].softwareVersion =
                                malloc(ctx->dumpHardwareEntriesWithData[e].entry.softwareVersionLength);

                        if(ctx->dumpHardwareEntriesWithData[e].softwareVersion != NULL)
                        {
                            readBytes =
                                    fread(&ctx->dumpHardwareEntriesWithData[e].softwareVersion,
                                          ctx->dumpHardwareEntriesWithData[e].entry.softwareVersionLength,
                                          1,
                                          ctx->imageStream);

                            if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.softwareVersionLength)
                            {
                                free(ctx->dumpHardwareEntriesWithData[e].softwareVersion);
                                ctx->dumpHardwareEntriesWithData[e].entry.softwareVersionLength = 0;
                                fprintf(stderr,
                                        "libdicformat: Could not read dump hardware block entry software version, continuing...");
                            }
                        }
                    }

                    if(ctx->dumpHardwareEntriesWithData[e].entry.softwareOperatingSystemLength > 0)
                    {
                        ctx->dumpHardwareEntriesWithData[e].softwareOperatingSystem =
                                malloc(ctx->dumpHardwareEntriesWithData[e].entry.softwareOperatingSystemLength);

                        if(ctx->dumpHardwareEntriesWithData[e].softwareOperatingSystem != NULL)
                        {
                            readBytes =
                                    fread(&ctx->dumpHardwareEntriesWithData[e].softwareOperatingSystem,
                                          ctx->dumpHardwareEntriesWithData[e].entry.softwareOperatingSystemLength,
                                          1,
                                          ctx->imageStream);

                            if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.softwareOperatingSystemLength)
                            {
                                free(ctx->dumpHardwareEntriesWithData[e].softwareOperatingSystem);
                                ctx->dumpHardwareEntriesWithData[e].entry.softwareOperatingSystemLength = 0;
                                fprintf(stderr,
                                        "libdicformat: Could not read dump hardware block entry manufacturer, continuing...");
                            }
                        }
                    }

                    ctx->dumpHardwareEntriesWithData[e].extents =
                            malloc(sizeof(DumpExtent) * ctx->dumpHardwareEntriesWithData->entry.extents);

                    if(ctx->dumpHardwareEntriesWithData[e].extents == NULL)
                    {
                        fprintf(stderr,
                                "libdicformat: Could not allocate memory for dump hardware block extents, continuing...");
                        continue;
                    }

                    readBytes =
                            fread(ctx->dumpHardwareEntriesWithData[e].extents,
                                  sizeof(DumpExtent),
                                  ctx->dumpHardwareEntriesWithData[e].entry.extents,
                                  ctx->imageStream);

                    if(readBytes != sizeof(DumpExtent) * ctx->dumpHardwareEntriesWithData->entry.extents)
                    {
                        free(ctx->dumpHardwareEntriesWithData[e].extents);
                        fprintf(stderr, "libdicformat: Could not read dump hardware block extents, continuing...");
                        continue;
                    }

                    // TODO: qsort()
                }

                break;
            default:
                fprintf(stderr,
                        "libdicformat: Unhandled block type %4.4s with data type %4.4s is indexed to be at %"PRIu64"",
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