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

#include <dicformat.h>
#include <errno.h>
#include <inttypes.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

void* open(const char* filepath)
{
    dicformatContext* ctx;
    int               errorNo;
    size_t            readBytes;
    long              pos;
    IndexHeader       idxHeader;
    IndexEntry*       idxEntries;
    uint8_t*          data;
    uint32_t*         cdDdt;
    uint64_t          crc64;
    uint8_t           temp8u;
    int               i;
    uint16_t          e;

    ctx = (dicformatContext*)malloc(sizeof(dicformatContext));
    memset(ctx, 0, sizeof(dicformatContext));

    if(ctx == NULL) return NULL;

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

    ctx->readableSectorTags = (bool*)malloc(sizeof(bool) * MaxSectorTag);

    if(ctx->readableSectorTags == NULL)
    {
        free(ctx);
        errno = DICF_ERROR_NOT_ENOUGH_MEMORY;

        return NULL;
    }

    memset(ctx->readableSectorTags, 0, sizeof(bool) * MaxSectorTag);

    ctx->imageInfo.Application        = ctx->header.application;
    ctx->imageInfo.ApplicationVersion = (uint8_t*)malloc(32);
    if(ctx->imageInfo.ApplicationVersion != NULL)
    {
        memset(ctx->imageInfo.ApplicationVersion, 0, 32);
        sprintf((char*)ctx->imageInfo.ApplicationVersion,
                "%d.%d",
                ctx->header.applicationMajorVersion,
                ctx->header.applicationMinorVersion);
    }
    ctx->imageInfo.Version = (uint8_t*)malloc(32);
    if(ctx->imageInfo.Version != NULL)
    {
        memset(ctx->imageInfo.Version, 0, 32);
        sprintf((char*)ctx->imageInfo.Version, "%d.%d", ctx->header.imageMajorVersion, ctx->header.imageMinorVersion);
    }
    ctx->imageInfo.MediaType = ctx->header.mediaType;

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

    fprintf(
        stderr, "libdicformat: Index at %" PRIu64 " contains %d entries", ctx->header.indexOffset, idxHeader.entries);

    idxEntries = (IndexEntry*)malloc(sizeof(IndexEntry) * idxHeader.entries);

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

    for(i = 0; i < idxHeader.entries; i++)
    {
        fprintf(stderr,
                "libdicformat: Block type %4.4s with data type %4.4s is indexed to be at %" PRIu64 "",
                (char*)&idxEntries[i].blockType,
                (char*)&idxEntries[i].dataType,
                idxEntries[i].offset);
    }

    bool foundUserDataDdt    = false;
    ctx->imageInfo.ImageSize = 0;
    for(i = 0; i < idxHeader.entries; i++)
    {
        pos = fseek(ctx->imageStream, idxEntries[i].offset, SEEK_SET);

        if(pos < 0 || ftell(ctx->imageStream) != idxEntries[i].offset)
        {
            fprintf(stderr,
                    "libdicformat: Could not seek to %" PRIu64 " as indicated by index entry %d, continuing...",
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
                if(idxEntries[i].dataType == NoData) break;

                readBytes = fread(&blockHeader, sizeof(BlockHeader), 1, ctx->imageStream);

                if(readBytes != sizeof(BlockHeader))
                {
                    fprintf(stderr, "libdicformat: Could not read block header at %" PRIu64 "", idxEntries[i].offset);

                    break;
                }

                ctx->imageInfo.ImageSize += blockHeader.cmpLength;

                // Unused, skip
                if(idxEntries[i].dataType == UserData)
                {
                    if(blockHeader.sectorSize > ctx->imageInfo.SectorSize)
                        ctx->imageInfo.SectorSize = blockHeader.sectorSize;

                    break;
                }

                if(blockHeader.identifier != idxEntries[i].blockType)
                {
                    fprintf(stderr,
                            "libdicformat: Incorrect identifier for data block at position %" PRIu64 "",
                            idxEntries[i].offset);
                    break;
                }

                if(blockHeader.type != idxEntries[i].dataType)
                {
                    fprintf(stderr,
                            "libdicformat: Expected block with data type %4.4s at position %" PRIu64
                            " but found data type %4.4s",
                            (char*)&idxEntries[i].blockType,
                            idxEntries[i].offset,
                            (char*)&blockHeader.type);
                    break;
                }

                fprintf(stderr,
                        "libdicformat: Found data block with type %4.4s at position %" PRIu64 "",
                        (char*)&idxEntries[i].blockType,
                        idxEntries[i].offset);

                if(blockHeader.compression == None)
                {
                    data = (uint8_t*)malloc(blockHeader.length);
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

                crc64 = crc64_data_ecma(data, blockHeader.length);
                if(crc64 != blockHeader.crc64)
                {
                    fprintf(stderr,
                            "libdicformat: Incorrect CRC found: 0x%" PRIx64 " found, expected 0x%" PRIx64
                            ", continuing...",
                            crc64,
                            blockHeader.crc64);
                    break;
                }

                dataLinkedList* mediaTag = NULL;

                // Check if it's not a media tag, but a sector tag, and fill the appropriate table then
                switch(idxEntries[i].dataType)
                {
                    case CdSectorPrefix:
                    case CdSectorPrefixCorrected:
                        if(idxEntries[i].dataType == CdSectorPrefixCorrected) { ctx->sectorPrefixCorrected = data; }
                        else
                            ctx->sectorPrefix = data;

                        ctx->readableSectorTags[CdSectorSync]   = true;
                        ctx->readableSectorTags[CdSectorHeader] = true;

                        break;
                    case CdSectorSuffix:
                    case CdSectorSuffixCorrected:
                        if(idxEntries[i].dataType == CdSectorSuffixCorrected)
                            ctx->sectorSuffixCorrected = data;
                        else
                            ctx->sectorSuffix = data;

                        ctx->readableSectorTags[CdSectorSubHeader] = true;
                        ctx->readableSectorTags[CdSectorEcc]       = true;
                        ctx->readableSectorTags[CdSectorEccP]      = true;
                        ctx->readableSectorTags[CdSectorEccQ]      = true;
                        ctx->readableSectorTags[CdSectorEdc]       = true;
                        break;
                    case CdSectorSubchannel:
                        ctx->sectorSubchannel                       = data;
                        ctx->readableSectorTags[CdSectorSubchannel] = true;
                        break;
                    case AppleProfileTag:
                    case AppleSonyTag:
                    case PriamDataTowerTag:
                        ctx->sectorSubchannel                   = data;
                        ctx->readableSectorTags[AppleSectorTag] = true;
                        break;
                    case CompactDiscMode2Subheader: ctx->mode2Subheaders = data; break;
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

                        // If we mediaTag is NULL means we have arrived the end of the list without finding a duplicate
                        // or the list was empty
                        if(mediaTag != NULL) break;

                        mediaTag = (dataLinkedList*)malloc(sizeof(dataLinkedList));

                        if(mediaTag == NULL)
                        {
                            fprintf(stderr, "libdicformat: Cannot allocate memory for media tag list entry.");
                            break;
                        }
                        memset(mediaTag, 0, sizeof(dataLinkedList));

                        mediaTag->type   = GetMediaTagTypeForDataType(blockHeader.type);
                        mediaTag->data   = data;
                        mediaTag->length = blockHeader.length;

                        if(ctx->mediaTagsHead == NULL) { ctx->mediaTagsHead = mediaTag; }
                        else
                        {
                            mediaTag->previous       = ctx->mediaTagsTail;
                            ctx->mediaTagsTail->next = mediaTag;
                        }

                        ctx->mediaTagsTail = mediaTag;

                        break;
                }

                break;
            case DeDuplicationTable:
                readBytes = fread(&ddtHeader, sizeof(DdtHeader), 1, ctx->imageStream);

                if(readBytes != sizeof(DdtHeader))
                {
                    fprintf(stderr, "libdicformat: Could not read block header at %" PRIu64 "", idxEntries[i].offset);

                    break;
                }

                foundUserDataDdt = true;

                ctx->imageInfo.ImageSize += ddtHeader.cmpLength;

                if(idxEntries[i].dataType == UserData)
                {
                    ctx->imageInfo.Sectors = ddtHeader.entries;
                    ctx->shift             = ddtHeader.shift;

                    // Check for DDT compression
                    switch(ddtHeader.compression)
                    {
                        case None:
                            ctx->mappedMemoryDdtSize = sizeof(uint64_t) * ddtHeader.entries;
                            ctx->userDataDdt         = mmap(NULL,
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
                        case None:
                            cdDdt = (uint32_t*)malloc(ddtHeader.entries * sizeof(uint32_t));

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
                                ctx->sectorPrefixDdt = cdDdt;
                            else if(idxEntries[i].dataType == CdSectorSuffixCorrected)
                                ctx->sectorSuffixDdt = cdDdt;
                            else
                                free(cdDdt);

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
            case GeometryBlock:
                readBytes = fread(&ctx->geometryBlock, sizeof(GeometryBlockHeader), 1, ctx->imageStream);

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

                    ctx->imageInfo.Cylinders       = ctx->geometryBlock.cylinders;
                    ctx->imageInfo.Heads           = ctx->geometryBlock.heads;
                    ctx->imageInfo.SectorsPerTrack = ctx->geometryBlock.sectorsPerTrack;
                }
                else
                    memset(&ctx->geometryBlock, 0, sizeof(GeometryBlockHeader));

                break;
                // Metadata block
            case MetadataBlock:
                readBytes = fread(&ctx->metadataBlockHeader, sizeof(MetadataBlockHeader), 1, ctx->imageStream);

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
                            "libdicformat: Incorrect identifier for data block at position %" PRIu64 "",
                            idxEntries[i].offset);
                    break;
                }

                ctx->imageInfo.ImageSize += ctx->metadataBlockHeader.blockSize;

                ctx->metadataBlock = (uint8_t*)malloc(ctx->metadataBlockHeader.blockSize);

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

                if(ctx->metadataBlockHeader.mediaSequence > 0 && ctx->metadataBlockHeader.lastMediaSequence > 0)
                {
                    ctx->imageInfo.MediaSequence     = ctx->metadataBlockHeader.mediaSequence;
                    ctx->imageInfo.LastMediaSequence = ctx->metadataBlockHeader.lastMediaSequence;
                    fprintf(stderr,
                            "libdicformat: Setting media sequence as %d of %d",
                            ctx->imageInfo.MediaSequence,
                            ctx->imageInfo.LastMediaSequence);
                }

                if(ctx->metadataBlockHeader.creatorLength > 0 &&
                   ctx->metadataBlockHeader.creatorOffset + ctx->metadataBlockHeader.creatorLength <=
                       ctx->metadataBlockHeader.blockSize)
                {
                    ctx->imageInfo.Creator = (uint8_t*)malloc(ctx->metadataBlockHeader.creatorLength);
                    if(ctx->imageInfo.Creator != NULL)
                    {
                        memcpy(ctx->imageInfo.Creator,
                               ctx->metadataBlock + ctx->metadataBlockHeader.creatorOffset,
                               ctx->metadataBlockHeader.creatorLength);
                    }
                }

                if(ctx->metadataBlockHeader.commentsLength > 0 &&
                   ctx->metadataBlockHeader.commentsOffset + ctx->metadataBlockHeader.commentsLength <=
                       ctx->metadataBlockHeader.blockSize)
                {
                    ctx->imageInfo.Comments = (uint8_t*)malloc(ctx->metadataBlockHeader.commentsLength);
                    if(ctx->imageInfo.Comments != NULL)
                    {
                        memcpy(ctx->imageInfo.Comments,
                               ctx->metadataBlock + ctx->metadataBlockHeader.commentsOffset,
                               ctx->metadataBlockHeader.commentsLength);
                    }
                }

                if(ctx->metadataBlockHeader.mediaTitleLength > 0 &&
                   ctx->metadataBlockHeader.mediaTitleOffset + ctx->metadataBlockHeader.mediaTitleLength <=
                       ctx->metadataBlockHeader.blockSize)
                {
                    ctx->imageInfo.MediaTitle = (uint8_t*)malloc(ctx->metadataBlockHeader.mediaTitleLength);
                    if(ctx->imageInfo.MediaTitle != NULL)
                    {
                        memcpy(ctx->imageInfo.MediaTitle,
                               ctx->metadataBlock + ctx->metadataBlockHeader.mediaTitleOffset,
                               ctx->metadataBlockHeader.mediaTitleLength);
                    }
                }

                if(ctx->metadataBlockHeader.mediaManufacturerLength > 0 &&
                   ctx->metadataBlockHeader.mediaManufacturerOffset +
                           ctx->metadataBlockHeader.mediaManufacturerLength <=
                       ctx->metadataBlockHeader.blockSize)
                {
                    ctx->imageInfo.MediaManufacturer =
                        (uint8_t*)malloc(ctx->metadataBlockHeader.mediaManufacturerLength);
                    if(ctx->imageInfo.MediaManufacturer != NULL)
                    {
                        memcpy(ctx->imageInfo.MediaManufacturer,
                               ctx->metadataBlock + ctx->metadataBlockHeader.mediaManufacturerOffset,
                               ctx->metadataBlockHeader.mediaManufacturerLength);
                    }
                }

                if(ctx->metadataBlockHeader.mediaModelLength > 0 &&
                   ctx->metadataBlockHeader.mediaModelOffset + ctx->metadataBlockHeader.mediaModelLength <=
                       ctx->metadataBlockHeader.blockSize)
                {
                    ctx->imageInfo.MediaModel = (uint8_t*)malloc(ctx->metadataBlockHeader.mediaModelOffset);
                    if(ctx->imageInfo.MediaModel != NULL)
                    {
                        memcpy(ctx->imageInfo.MediaModel,
                               ctx->metadataBlock + ctx->metadataBlockHeader.mediaModelOffset,
                               ctx->metadataBlockHeader.mediaModelLength);
                    }
                }

                if(ctx->metadataBlockHeader.mediaSerialNumberLength > 0 &&
                   ctx->metadataBlockHeader.mediaSerialNumberOffset +
                           ctx->metadataBlockHeader.mediaSerialNumberLength <=
                       ctx->metadataBlockHeader.blockSize)
                {
                    ctx->imageInfo.MediaSerialNumber =
                        (uint8_t*)malloc(ctx->metadataBlockHeader.mediaSerialNumberLength);
                    if(ctx->imageInfo.MediaSerialNumber != NULL)
                    {
                        memcpy(ctx->imageInfo.MediaSerialNumber,
                               ctx->metadataBlock + ctx->metadataBlockHeader.mediaSerialNumberOffset,
                               ctx->metadataBlockHeader.mediaManufacturerLength);
                    }
                }

                if(ctx->metadataBlockHeader.mediaBarcodeLength > 0 &&
                   ctx->metadataBlockHeader.mediaBarcodeOffset + ctx->metadataBlockHeader.mediaBarcodeLength <=
                       ctx->metadataBlockHeader.blockSize)
                {
                    ctx->imageInfo.MediaBarcode = (uint8_t*)malloc(ctx->metadataBlockHeader.mediaBarcodeLength);
                    if(ctx->imageInfo.MediaBarcode != NULL)
                    {
                        memcpy(ctx->imageInfo.MediaBarcode,
                               ctx->metadataBlock + ctx->metadataBlockHeader.mediaBarcodeOffset,
                               ctx->metadataBlockHeader.mediaBarcodeLength);
                    }
                }

                if(ctx->metadataBlockHeader.mediaPartNumberLength > 0 &&
                   ctx->metadataBlockHeader.mediaPartNumberOffset + ctx->metadataBlockHeader.mediaPartNumberLength <=
                       ctx->metadataBlockHeader.blockSize)
                {
                    ctx->imageInfo.MediaPartNumber = (uint8_t*)malloc(ctx->metadataBlockHeader.mediaPartNumberLength);
                    if(ctx->imageInfo.MediaPartNumber != NULL)
                    {
                        memcpy(ctx->imageInfo.MediaPartNumber,
                               ctx->metadataBlock + ctx->metadataBlockHeader.mediaPartNumberOffset,
                               ctx->metadataBlockHeader.mediaPartNumberLength);
                    }
                }

                if(ctx->metadataBlockHeader.driveManufacturerLength > 0 &&
                   ctx->metadataBlockHeader.driveManufacturerOffset +
                           ctx->metadataBlockHeader.driveManufacturerLength <=
                       ctx->metadataBlockHeader.blockSize)
                {
                    ctx->imageInfo.DriveManufacturer =
                        (uint8_t*)malloc(ctx->metadataBlockHeader.driveManufacturerLength);
                    if(ctx->imageInfo.DriveManufacturer != NULL)
                    {
                        memcpy(ctx->imageInfo.DriveManufacturer,
                               ctx->metadataBlock + ctx->metadataBlockHeader.driveManufacturerOffset,
                               ctx->metadataBlockHeader.driveManufacturerLength);
                    }
                }

                if(ctx->metadataBlockHeader.driveModelLength > 0 &&
                   ctx->metadataBlockHeader.driveModelOffset + ctx->metadataBlockHeader.driveModelLength <=
                       ctx->metadataBlockHeader.blockSize)
                {
                    ctx->imageInfo.DriveModel = (uint8_t*)malloc(ctx->metadataBlockHeader.driveModelLength);
                    if(ctx->imageInfo.DriveModel != NULL)
                    {
                        memcpy(ctx->imageInfo.DriveModel,
                               ctx->metadataBlock + ctx->metadataBlockHeader.driveModelOffset,
                               ctx->metadataBlockHeader.driveModelLength);
                    }
                }

                if(ctx->metadataBlockHeader.driveSerialNumberLength > 0 &&
                   ctx->metadataBlockHeader.driveSerialNumberOffset +
                           ctx->metadataBlockHeader.driveSerialNumberLength <=
                       ctx->metadataBlockHeader.blockSize)
                {
                    ctx->imageInfo.DriveSerialNumber =
                        (uint8_t*)malloc(ctx->metadataBlockHeader.driveSerialNumberLength);
                    if(ctx->imageInfo.DriveSerialNumber != NULL)
                    {
                        memcpy(ctx->imageInfo.DriveSerialNumber,
                               ctx->metadataBlock + ctx->metadataBlockHeader.driveSerialNumberOffset,
                               ctx->metadataBlockHeader.driveSerialNumberLength);
                    }
                }

                if(ctx->metadataBlockHeader.driveManufacturerLength > 0 &&
                   ctx->metadataBlockHeader.driveFirmwareRevisionOffset +
                           ctx->metadataBlockHeader.driveManufacturerLength <=
                       ctx->metadataBlockHeader.blockSize)
                {
                    ctx->imageInfo.DriveFirmwareRevision =
                        (uint8_t*)malloc(ctx->metadataBlockHeader.driveFirmwareRevisionLength);
                    if(ctx->imageInfo.DriveFirmwareRevision != NULL)
                    {
                        memcpy(ctx->imageInfo.DriveFirmwareRevision,
                               ctx->metadataBlock + ctx->metadataBlockHeader.driveFirmwareRevisionLength,
                               ctx->metadataBlockHeader.driveFirmwareRevisionLength);
                    }
                }

                break;
            case TracksBlock:
                readBytes = fread(&ctx->tracksHeader, sizeof(TracksHeader), 1, ctx->imageStream);

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
                            "libdicformat: Incorrect identifier for data block at position %" PRIu64 "",
                            idxEntries[i].offset);
                }

                ctx->imageInfo.ImageSize += sizeof(TrackEntry) * ctx->tracksHeader.entries;

                ctx->trackEntries = (TrackEntry*)malloc(sizeof(TrackEntry) * ctx->tracksHeader.entries);

                if(ctx->trackEntries == NULL)
                {
                    memset(&ctx->tracksHeader, 0, sizeof(TracksHeader));
                    fprintf(stderr, "libdicformat: Could not allocate memory for metadata block, continuing...");
                    break;
                }

                readBytes = fread(ctx->trackEntries, sizeof(TrackEntry), ctx->tracksHeader.entries, ctx->imageStream);

                if(readBytes != sizeof(TrackEntry) * ctx->tracksHeader.entries)
                {
                    memset(&ctx->tracksHeader, 0, sizeof(TracksHeader));
                    free(ctx->trackEntries);
                    fprintf(stderr, "libdicformat: Could not read metadata block, continuing...");
                }

                crc64 =
                    crc64_data_ecma((const uint8_t*)ctx->trackEntries, ctx->tracksHeader.entries * sizeof(TrackEntry));
                if(crc64 != ctx->tracksHeader.crc64)
                {
                    fprintf(stderr,
                            "libdicformat: Incorrect CRC found: 0x%" PRIx64 " found, expected 0x%" PRIx64
                            ", continuing...",
                            crc64,
                            ctx->tracksHeader.crc64);
                    break;
                }

                fprintf(stderr,
                        "libdicformat: Found %d tracks at position %" PRIu64 ".",
                        ctx->tracksHeader.entries,
                        idxEntries[i].offset);

                ctx->imageInfo.HasPartitions = true;
                ctx->imageInfo.HasSessions   = true;

                ctx->numberOfDataTracks = 0;

                // TODO: Handle track 0
                for(i = 0, temp8u = 0; i < ctx->tracksHeader.entries; i++)
                {
                    if(ctx->trackEntries[i].sequence > temp8u && ctx->trackEntries[i].sequence <= 99)
                        temp8u = ctx->trackEntries[i].sequence;
                }

                if(temp8u > 0)
                {
                    ctx->dataTracks = (TrackEntry*)malloc(sizeof(TrackEntry) * temp8u);

                    if(ctx->dataTracks == NULL) break;

                    ctx->numberOfDataTracks = temp8u;

                    for(i = 0, temp8u = 0; i < ctx->tracksHeader.entries; i++)
                    {
                        if(ctx->trackEntries[i].sequence > 99) continue;

                        memcpy(
                            &ctx->dataTracks[ctx->trackEntries[i].sequence], &ctx->trackEntries[i], sizeof(TrackEntry));
                    }
                }

                break;
                // CICM XML metadata block
            case CicmBlock:
                readBytes = fread(&ctx->cicmBlockHeader, sizeof(CicmMetadataBlock), 1, ctx->imageStream);

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
                            "libdicformat: Incorrect identifier for data block at position %" PRIu64 "",
                            idxEntries[i].offset);
                }

                ctx->imageInfo.ImageSize += ctx->cicmBlockHeader.length;

                ctx->cicmBlock = (uint8_t*)malloc(ctx->cicmBlockHeader.length);

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

                fprintf(stderr, "libdicformat: Found CICM XML metadata block %" PRIu64 ".", idxEntries[i].offset);
                break;
                // Dump hardware block
            case DumpHardwareBlock:
                readBytes = fread(&ctx->dumpHardwareHeader, sizeof(DumpHardwareHeader), 1, ctx->imageStream);

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
                            "libdicformat: Incorrect identifier for data block at position %" PRIu64 "",
                            idxEntries[i].offset);
                }

                data = (uint8_t*)malloc(ctx->dumpHardwareHeader.length);
                if(data != NULL)
                {
                    readBytes = fread(data, ctx->dumpHardwareHeader.length, 1, ctx->imageStream);

                    if(readBytes == ctx->dumpHardwareHeader.length)
                    {
                        crc64 = crc64_data_ecma(data, ctx->dumpHardwareHeader.length);
                        if(crc64 != ctx->dumpHardwareHeader.crc64)
                        {
                            free(data);
                            fprintf(stderr,
                                    "libdicformat: Incorrect CRC found: 0x%" PRIx64 " found, expected 0x%" PRIx64
                                    ", continuing...",
                                    crc64,
                                    ctx->dumpHardwareHeader.crc64);
                            break;
                        }
                    }

                    free(data);
                    fseek(ctx->imageStream, -readBytes, SEEK_CUR);
                }

                ctx->dumpHardwareEntriesWithData = (DumpHardwareEntriesWithData*)malloc(
                    sizeof(DumpHardwareEntriesWithData) * ctx->dumpHardwareHeader.entries);

                if(ctx->dumpHardwareEntriesWithData == NULL)
                {
                    memset(&ctx->dumpHardwareHeader, 0, sizeof(DumpHardwareHeader));
                    fprintf(stderr, "libdicformat: Could not allocate memory for dump hardware block, continuing...");
                    break;
                }

                memset(ctx->dumpHardwareEntriesWithData,
                       0,
                       sizeof(DumpHardwareEntriesWithData) * ctx->dumpHardwareHeader.entries);

                for(e = 0; e < ctx->dumpHardwareHeader.entries; e++)
                {
                    readBytes = fread(
                        &ctx->dumpHardwareEntriesWithData[e].entry, sizeof(DumpHardwareEntry), 1, ctx->imageStream);

                    if(readBytes != sizeof(DumpHardwareEntry))
                    {
                        ctx->dumpHardwareHeader.entries = e + (uint16_t)1;
                        fprintf(stderr, "libdicformat: Could not read dump hardware block entry, continuing...");
                        break;
                    }

                    if(ctx->dumpHardwareEntriesWithData[e].entry.manufacturerLength > 0)
                    {
                        ctx->dumpHardwareEntriesWithData[e].manufacturer =
                            (uint8_t*)malloc(ctx->dumpHardwareEntriesWithData[e].entry.manufacturerLength);

                        if(ctx->dumpHardwareEntriesWithData[e].manufacturer != NULL)
                        {
                            readBytes = fread(&ctx->dumpHardwareEntriesWithData[e].manufacturer,
                                              ctx->dumpHardwareEntriesWithData[e].entry.manufacturerLength,
                                              1,
                                              ctx->imageStream);

                            if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.manufacturerLength)
                            {
                                free(ctx->dumpHardwareEntriesWithData[e].manufacturer);
                                ctx->dumpHardwareEntriesWithData[e].entry.manufacturerLength = 0;
                                fprintf(stderr,
                                        "libdicformat: Could not read dump hardware block entry manufacturer, "
                                        "continuing...");
                            }
                        }
                    }

                    if(ctx->dumpHardwareEntriesWithData[e].entry.modelLength > 0)
                    {
                        ctx->dumpHardwareEntriesWithData[e].model =
                            (uint8_t*)malloc(ctx->dumpHardwareEntriesWithData[e].entry.modelLength);

                        if(ctx->dumpHardwareEntriesWithData[e].model != NULL)
                        {
                            readBytes = fread(&ctx->dumpHardwareEntriesWithData[e].model,
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
                            (uint8_t*)malloc(ctx->dumpHardwareEntriesWithData[e].entry.revisionLength);

                        if(ctx->dumpHardwareEntriesWithData[e].revision != NULL)
                        {
                            readBytes = fread(&ctx->dumpHardwareEntriesWithData[e].revision,
                                              ctx->dumpHardwareEntriesWithData[e].entry.revisionLength,
                                              1,
                                              ctx->imageStream);

                            if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.revisionLength)
                            {
                                free(ctx->dumpHardwareEntriesWithData[e].revision);
                                ctx->dumpHardwareEntriesWithData[e].entry.revisionLength = 0;
                                fprintf(
                                    stderr,
                                    "libdicformat: Could not read dump hardware block entry revision, continuing...");
                            }
                        }
                    }

                    if(ctx->dumpHardwareEntriesWithData[e].entry.firmwareLength > 0)
                    {
                        ctx->dumpHardwareEntriesWithData[e].firmware =
                            (uint8_t*)malloc(ctx->dumpHardwareEntriesWithData[e].entry.firmwareLength);

                        if(ctx->dumpHardwareEntriesWithData[e].firmware != NULL)
                        {
                            readBytes = fread(&ctx->dumpHardwareEntriesWithData[e].firmware,
                                              ctx->dumpHardwareEntriesWithData[e].entry.firmwareLength,
                                              1,
                                              ctx->imageStream);

                            if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.firmwareLength)
                            {
                                free(ctx->dumpHardwareEntriesWithData[e].firmware);
                                ctx->dumpHardwareEntriesWithData[e].entry.firmwareLength = 0;
                                fprintf(
                                    stderr,
                                    "libdicformat: Could not read dump hardware block entry firmware, continuing...");
                            }
                        }
                    }

                    if(ctx->dumpHardwareEntriesWithData[e].entry.serialLength > 0)
                    {
                        ctx->dumpHardwareEntriesWithData[e].serial =
                            (uint8_t*)malloc(ctx->dumpHardwareEntriesWithData[e].entry.serialLength);

                        if(ctx->dumpHardwareEntriesWithData[e].serial != NULL)
                        {
                            readBytes = fread(&ctx->dumpHardwareEntriesWithData[e].serial,
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
                            (uint8_t*)malloc(ctx->dumpHardwareEntriesWithData[e].entry.softwareNameLength);

                        if(ctx->dumpHardwareEntriesWithData[e].softwareName != NULL)
                        {
                            readBytes = fread(&ctx->dumpHardwareEntriesWithData[e].softwareName,
                                              ctx->dumpHardwareEntriesWithData[e].entry.softwareNameLength,
                                              1,
                                              ctx->imageStream);

                            if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.softwareNameLength)
                            {
                                free(ctx->dumpHardwareEntriesWithData[e].softwareName);
                                ctx->dumpHardwareEntriesWithData[e].entry.softwareNameLength = 0;
                                fprintf(stderr,
                                        "libdicformat: Could not read dump hardware block entry software name, "
                                        "continuing...");
                            }
                        }
                    }

                    if(ctx->dumpHardwareEntriesWithData[e].entry.softwareVersionLength > 0)
                    {
                        ctx->dumpHardwareEntriesWithData[e].softwareVersion =
                            (uint8_t*)malloc(ctx->dumpHardwareEntriesWithData[e].entry.softwareVersionLength);

                        if(ctx->dumpHardwareEntriesWithData[e].softwareVersion != NULL)
                        {
                            readBytes = fread(&ctx->dumpHardwareEntriesWithData[e].softwareVersion,
                                              ctx->dumpHardwareEntriesWithData[e].entry.softwareVersionLength,
                                              1,
                                              ctx->imageStream);

                            if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.softwareVersionLength)
                            {
                                free(ctx->dumpHardwareEntriesWithData[e].softwareVersion);
                                ctx->dumpHardwareEntriesWithData[e].entry.softwareVersionLength = 0;
                                fprintf(stderr,
                                        "libdicformat: Could not read dump hardware block entry software version, "
                                        "continuing...");
                            }
                        }
                    }

                    if(ctx->dumpHardwareEntriesWithData[e].entry.softwareOperatingSystemLength > 0)
                    {
                        ctx->dumpHardwareEntriesWithData[e].softwareOperatingSystem =
                            (uint8_t*)malloc(ctx->dumpHardwareEntriesWithData[e].entry.softwareOperatingSystemLength);

                        if(ctx->dumpHardwareEntriesWithData[e].softwareOperatingSystem != NULL)
                        {
                            readBytes = fread(&ctx->dumpHardwareEntriesWithData[e].softwareOperatingSystem,
                                              ctx->dumpHardwareEntriesWithData[e].entry.softwareOperatingSystemLength,
                                              1,
                                              ctx->imageStream);

                            if(readBytes != ctx->dumpHardwareEntriesWithData[e].entry.softwareOperatingSystemLength)
                            {
                                free(ctx->dumpHardwareEntriesWithData[e].softwareOperatingSystem);
                                ctx->dumpHardwareEntriesWithData[e].entry.softwareOperatingSystemLength = 0;
                                fprintf(stderr,
                                        "libdicformat: Could not read dump hardware block entry manufacturer, "
                                        "continuing...");
                            }
                        }
                    }

                    ctx->dumpHardwareEntriesWithData[e].extents =
                        (DumpExtent*)malloc(sizeof(DumpExtent) * ctx->dumpHardwareEntriesWithData->entry.extents);

                    if(ctx->dumpHardwareEntriesWithData[e].extents == NULL)
                    {
                        fprintf(
                            stderr,
                            "libdicformat: Could not allocate memory for dump hardware block extents, continuing...");
                        continue;
                    }

                    readBytes = fread(ctx->dumpHardwareEntriesWithData[e].extents,
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
                        "libdicformat: Unhandled block type %4.4s with data type %4.4s is indexed to be at %" PRIu64 "",
                        (char*)&idxEntries[i].blockType,
                        (char*)&idxEntries[i].dataType,
                        idxEntries[i].offset);
                break;
        }
    }

    free(idxEntries);

    if(!foundUserDataDdt)
    {
        fprintf(stderr, "libdicformat: Could not find user data deduplication table, aborting...");
        close(ctx);
        return NULL;
    }

    ctx->imageInfo.CreationTime         = ctx->header.creationTime;
    ctx->imageInfo.LastModificationTime = ctx->header.lastWrittenTime;
    ctx->imageInfo.XmlMediaType         = GetXmlMediaType(ctx->header.mediaType);

    if(ctx->geometryBlock.identifier != GeometryBlock && ctx->imageInfo.XmlMediaType == BlockMedia)
    {
        ctx->imageInfo.Cylinders       = (uint32_t)(ctx->imageInfo.Sectors / 16 / 63);
        ctx->imageInfo.Heads           = 16;
        ctx->imageInfo.SectorsPerTrack = 63;
    }

    // TODO: Caches
    /*
        // Initialize caches
        blockCache       = new Dictionary<ulong, byte[]>();
        blockHeaderCache = new Dictionary<ulong, BlockHeader>();
        currentCacheSize = 0;
        if(!inMemoryDdt) ddtEntryCache = new Dictionary<ulong, ulong>();
    */

    // TODO: Cache tracks and sessions?

    // Initialize ECC for Compact Disc
    ctx->eccCdContext = (CdEccContext*)ecc_cd_init();

    ctx->magic               = DIC_MAGIC;
    ctx->libraryMajorVersion = LIBDICFORMAT_MAJOR_VERSION;
    ctx->libraryMinorVersion = LIBDICFORMAT_MINOR_VERSION;

    free(idxEntries);
    return ctx;
}