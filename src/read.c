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
// Copyright © 2011-2020 Natalia Portillo
// ****************************************************************************/

#include <dicformat.h>
#include <malloc.h>
#include <string.h>

int32_t read_media_tag(void* context, uint8_t* data, int32_t tag, uint32_t* length)
{
    dicformatContext* ctx;
    dataLinkedList*   item;

    if(context == NULL) return DICF_ERROR_NOT_DICFORMAT;

    ctx = context;

    // Not a libdicformat context
    if(ctx->magic != DIC_MAGIC) return DICF_ERROR_NOT_DICFORMAT;

    item = ctx->mediaTagsHead;

    while(item != NULL)
    {
        if(item->type == tag)
        {
            if(data == NULL || *length < item->length)
            {
                *length = item->length;
                return DICF_ERROR_BUFFER_TOO_SMALL;
            }

            *length = item->length;
            memcpy(data, item->data, item->length);
        }

        item = item->next;
    }

    *length = 0;
    return DICF_ERROR_MEDIA_TAG_NOT_PRESENT;
}

int32_t read_sector(void* context, uint64_t sectorAddress, uint8_t* data, uint32_t* length)
{
    dicformatContext* ctx;
    uint64_t          ddtEntry;
    uint32_t          offsetMask;
    uint64_t          offset;
    uint64_t          blockOffset;
    BlockHeader       blockHeader;
    uint8_t*          block;
    size_t            readBytes;

    if(context == NULL) return DICF_ERROR_NOT_DICFORMAT;

    ctx = context;

    // Not a libdicformat context
    if(ctx->magic != DIC_MAGIC) return DICF_ERROR_NOT_DICFORMAT;

    if(sectorAddress > ctx->imageInfo.Sectors - 1) return DICF_ERROR_SECTOR_OUT_OF_BOUNDS;

    ddtEntry    = ctx->userDataDdt[sectorAddress];
    offsetMask  = (uint32_t)((1 << ctx->shift) - 1);
    offset      = ddtEntry & offsetMask;
    blockOffset = ddtEntry >> ctx->shift;

    // Partially written image... as we can't know the real sector size just assume it's common :/
    if(ddtEntry == 0)
    {
        memset(data, 0, ctx->imageInfo.SectorSize);
        *length = ctx->imageInfo.SectorSize;
        return DICF_STATUS_SECTOR_NOT_DUMPED;
    }

    // Check if block is cached
    // TODO: Caches

    // Read block header
    fseek(ctx->imageStream, blockOffset, SEEK_SET);
    readBytes = fread(&blockHeader, sizeof(BlockHeader), 1, ctx->imageStream);

    if(readBytes != sizeof(BlockHeader)) return DICF_ERROR_CANNOT_READ_HEADER;

    if(data == NULL || *length < blockHeader.sectorSize)
    {
        *length = blockHeader.sectorSize;
        return DICF_ERROR_BUFFER_TOO_SMALL;
    }

    // Decompress block
    switch(blockHeader.compression)
    {
        case None:
            block = (uint8_t*)malloc(blockHeader.length);
            if(block == NULL) return DICF_ERROR_NOT_ENOUGH_MEMORY;

            readBytes = fread(block, blockHeader.length, 1, ctx->imageStream);

            if(readBytes != blockHeader.length)
            {
                free(block);
                return DICF_ERROR_CANNOT_READ_BLOCK;
            }

            break;
        default: return DICF_ERROR_UNSUPPORTED_COMPRESSION;
    }

    // Check if cache needs to be emptied
    // TODO: Caches

    // Add block to cache
    // TODO: Caches

    memcpy(data, block + offset, blockHeader.sectorSize);
    *length = blockHeader.sectorSize;
    free(block);
    return DICF_STATUS_OK;
}

int32_t read_track_sector(void* context, uint8_t* data, uint64_t sectorAddress, uint32_t* length, uint8_t track)
{
    dicformatContext* ctx;
    int               i;

    if(context == NULL) return DICF_ERROR_NOT_DICFORMAT;

    ctx = context;

    // Not a libdicformat context
    if(ctx->magic != DIC_MAGIC) return DICF_ERROR_NOT_DICFORMAT;

    if(ctx->imageInfo.XmlMediaType != OpticalDisc) return DICF_ERROR_INCORRECT_MEDIA_TYPE;

    for(i = 0; i < ctx->numberOfDataTracks; i++)
    {
        if(ctx->dataTracks[i].sequence == track)
        { return read_sector(context, ctx->dataTracks[i].start + sectorAddress, data, length); }
    }

    return DICF_ERROR_TRACK_NOT_FOUND;
}

int32_t read_sector_long(void* context, uint8_t* data, uint64_t sectorAddress, uint32_t* length)
{
    dicformatContext* ctx;
    uint32_t          bareLength;
    uint32_t          tagLength;
    uint8_t*          bareData;
    int32_t           res;
    TrackEntry        trk;
    int               i;
    bool              trkFound;

    if(context == NULL) return DICF_ERROR_NOT_DICFORMAT;

    ctx = context;

    // Not a libdicformat context
    if(ctx->magic != DIC_MAGIC) return DICF_ERROR_NOT_DICFORMAT;

    switch(ctx->imageInfo.XmlMediaType)
    {
        case OpticalDisc:
            if(*length < 2352 || data == NULL)
            {
                *length = 2352;
                return DICF_ERROR_BUFFER_TOO_SMALL;
            }
            if((ctx->sectorSuffix == NULL || ctx->sectorPrefix == NULL) &&
               (ctx->sectorSuffixCorrected == NULL || ctx->sectorPrefixCorrected == NULL))
                return read_sector(context, sectorAddress, data, length);

            bareLength = 0;
            read_sector(context, sectorAddress, NULL, &bareLength);

            bareData = (uint8_t*)malloc(bareLength);

            if(bareData == NULL) return DICF_ERROR_NOT_ENOUGH_MEMORY;

            res = read_sector(context, sectorAddress, bareData, &bareLength);

            if(res < DICF_STATUS_OK) return res;

            trkFound = false;

            for(i = 0; i < ctx->numberOfDataTracks; i++)
            {
                if(ctx->dataTracks[i].start >= sectorAddress && ctx->dataTracks[i].end <= sectorAddress)
                {
                    trkFound = true;
                    trk      = ctx->dataTracks[i];
                    break;
                }
            }

            if(!trkFound) return DICF_ERROR_TRACK_NOT_FOUND;

            switch(trk.type)
            {
                case Audio:
                case Data: memcpy(bareData, data, bareLength); return res;
                case CdMode1:
                    memcpy(bareData, data + 16, 2048);

                    if(ctx->sectorPrefix != NULL)
                        memcpy(data, ctx->sectorPrefix + (sectorAddress * 16), 16);
                    else if(ctx->sectorPrefixDdt != NULL)
                    {
                        if((ctx->sectorPrefixDdt[sectorAddress] & CD_XFIX_MASK) == Correct)
                        {
                            ecc_cd_reconstruct_prefix(data, trk.type, sectorAddress);
                            res = DICF_STATUS_OK;
                        }
                        else if((ctx->sectorPrefixDdt[sectorAddress] & CD_XFIX_MASK) == NotDumped)
                        {
                            res = DICF_STATUS_SECTOR_NOT_DUMPED;
                        }
                        else
                        {
                            memcpy(data,
                                   ctx->sectorPrefixCorrected +
                                       ((ctx->sectorPrefixDdt[sectorAddress] & CD_DFIX_MASK) - 1) * 16,
                                   16);
                        }
                    }
                    else
                        return DICF_ERROR_REACHED_UNREACHABLE_CODE;

                    if(ctx->sectorSuffix != NULL)
                        memcpy(data + 2064, ctx->sectorSuffix + sectorAddress * 288, 288);
                    else if(ctx->sectorSuffixDdt != NULL)
                    {
                        if((ctx->sectorSuffixDdt[sectorAddress] & CD_XFIX_MASK) == Correct)
                        {
                            ecc_cd_reconstruct(ctx->eccCdContext, data, trk.type);
                            res = DICF_STATUS_OK;
                        }
                        else if((ctx->sectorSuffixDdt[sectorAddress] & CD_XFIX_MASK) == NotDumped)
                        {
                            res = DICF_STATUS_SECTOR_NOT_DUMPED;
                        }
                        else
                        {
                            memcpy(data + 2064,
                                   ctx->sectorSuffixCorrected +
                                       ((ctx->sectorSuffixDdt[sectorAddress] & CD_DFIX_MASK) - 1) * 288,
                                   288);
                        }
                    }
                    else
                        return DICF_ERROR_REACHED_UNREACHABLE_CODE;

                    return res;
                case CdMode2Formless:
                case CdMode2Form1:
                case CdMode2Form2:
                    if(ctx->sectorPrefix != NULL)
                        memcpy(data, ctx->sectorPrefix + sectorAddress * 16, 16);
                    else if(ctx->sectorPrefixDdt != NULL)
                    {
                        if((ctx->sectorPrefixDdt[sectorAddress] & CD_XFIX_MASK) == Correct)
                        {
                            ecc_cd_reconstruct_prefix(data, trk.type, sectorAddress);
                            res = DICF_STATUS_OK;
                        }
                        else if((ctx->sectorPrefixDdt[sectorAddress] & CD_XFIX_MASK) == NotDumped)
                        {
                            res = DICF_STATUS_SECTOR_NOT_DUMPED;
                        }
                        else
                        {
                            memcpy(data,
                                   ctx->sectorPrefixCorrected +
                                       ((ctx->sectorPrefixDdt[sectorAddress] & CD_DFIX_MASK) - 1) * 16,
                                   16);
                        }
                    }
                    else
                        return DICF_ERROR_REACHED_UNREACHABLE_CODE;

                    if(ctx->mode2Subheaders != NULL && ctx->sectorSuffixDdt != NULL)
                    {
                        memcpy(data + 16, ctx->mode2Subheaders + sectorAddress * 8, 8);

                        if((ctx->sectorSuffixDdt[sectorAddress] & CD_XFIX_MASK) == Mode2Form1Ok)
                        {
                            memcpy(data + 24, bareData, 2048);
                            ecc_cd_reconstruct(ctx->eccCdContext, data, CdMode2Form1);
                        }
                        else if((ctx->sectorSuffixDdt[sectorAddress] & CD_XFIX_MASK) == Mode2Form2Ok ||
                                (ctx->sectorSuffixDdt[sectorAddress] & CD_XFIX_MASK) == Mode2Form2NoCrc)
                        {
                            memcpy(data + 24, bareData, 2324);
                            if((ctx->sectorSuffixDdt[sectorAddress] & CD_XFIX_MASK) == Mode2Form2Ok)
                                ecc_cd_reconstruct(ctx->eccCdContext, data, CdMode2Form2);
                        }
                        else if((ctx->sectorSuffixDdt[sectorAddress] & CD_XFIX_MASK) == NotDumped)
                        {
                            res = DICF_STATUS_SECTOR_NOT_DUMPED;
                        }
                        else
                            // Mode 2 where ECC failed
                            memcpy(data + 24, bareData, 2328);
                    }
                    else if(ctx->mode2Subheaders != NULL)
                    {
                        memcpy(data + 16, ctx->mode2Subheaders + sectorAddress * 8, 8);
                        memcpy(data + 24, bareData, 2328);
                    }
                    else
                        memcpy(data + 16, bareData, 2336);

                    return res;
                default: return DICF_ERROR_INVALID_TRACK_FORMAT;
            }
        case BlockMedia:
            switch(ctx->imageInfo.MediaType)
            {
                case AppleFileWare:
                case AppleProfile:
                case AppleSonySS:
                case AppleSonyDS:
                case AppleWidget:
                case PriamDataTower:
                    if(ctx->sectorSubchannel == NULL) return read_sector(context, sectorAddress, data, length);

                    switch(ctx->imageInfo.MediaType)
                    {
                        case AppleFileWare:
                        case AppleProfile:
                        case AppleWidget: tagLength = 20; break;
                        case AppleSonySS:
                        case AppleSonyDS: tagLength = 12; break;
                        case PriamDataTower: tagLength = 24; break;
                        default: return DICF_ERROR_INCORRECT_MEDIA_TYPE;
                    }

                    bareLength = 512;

                    if(*length < tagLength + bareLength || data == NULL)
                    {
                        *length = tagLength + bareLength;
                        return DICF_ERROR_BUFFER_TOO_SMALL;
                    }

                    bareData = malloc(bareLength);

                    if(bareData == NULL) return DICF_ERROR_NOT_ENOUGH_MEMORY;

                    res = read_sector(context, sectorAddress, bareData, &bareLength);

                    if(bareLength != 512) return res;

                    memcpy(data, ctx->sectorSubchannel + sectorAddress * tagLength, tagLength);
                    memcpy(data, bareData, 512);

                    free(bareData);

                    return res;
                default: return DICF_ERROR_INCORRECT_MEDIA_TYPE;
            }
        default: return DICF_ERROR_INCORRECT_MEDIA_TYPE;
    }
}