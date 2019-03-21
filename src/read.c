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
#include <malloc.h>
#include <string.h>

uint8_t *read_media_tag(void *context, int32_t tag, uint32_t *length)
{
    dicformatContext *ctx;
    dataLinkedList   *item;
    *length = 0;

    if(context == NULL)
    {
        errno = EINVAL;
        return NULL;
    }

    ctx = context;

    // TODO: Cast this field without casting the whole structure, as this can buffer overflow
    // Not a libdicformat context
    if(ctx->magic != DIC_MAGIC)
    {
        errno = EINVAL;
        return NULL;
    }

    item = ctx->mediaTagsHead;

    while(item != NULL)
    {
        if(item->type == tag)
        {
            *length = item->length;
            return item->data;
        }

        item = item->next;
    }

    return NULL;
}

uint8_t *read_sector(void *context, uint64_t sectorAddress, uint32_t *length)
{
    dicformatContext *ctx;
    uint64_t         ddtEntry;
    uint32_t         offsetMask;
    uint64_t         offset;
    uint64_t         blockOffset;
    uint8_t          *sector;
    BlockHeader      blockHeader;
    uint8_t          *block;
    size_t           readBytes;

    *length = 0;

    if(context == NULL)
    {
        errno = EINVAL;
        return NULL;
    }

    ctx = context;

    // TODO: Cast this field without casting the whole structure, as this can buffer overflow
    // Not a libdicformat context
    if(ctx->magic != DIC_MAGIC)
    {
        errno = EINVAL;
        return NULL;
    }

    if(sectorAddress > ctx->imageInfo.Sectors - 1)
    {
        errno = DICF_ERROR_SECTOR_OUT_OF_BOUNDS;
        return NULL;
    }

    ddtEntry    = ctx->userDataDdt[sectorAddress];
    offsetMask  = (uint32_t)((1 << ctx->shift) - 1);
    offset      = ddtEntry & offsetMask;
    blockOffset = ddtEntry >> ctx->shift;

    // Partially written image... as we can't know the real sector size just assume it's common :/
    if(ddtEntry == 0)
    {
        sector = (uint8_t *)malloc(ctx->imageInfo.SectorSize);

        if(sector == NULL)
        {
            errno = ENOMEM;
            return NULL;
        }

        memset(sector, 0, ctx->imageInfo.SectorSize);
        *length = ctx->imageInfo.SectorSize;
        return sector;
    }

    // Check if block is cached
    // TODO: Caches

    // Read block header
    fseek(ctx->imageStream, blockOffset, SEEK_SET);
    readBytes = fread(&blockHeader, sizeof(BlockHeader), 1, ctx->imageStream);

    if(readBytes != sizeof(BlockHeader))
    {
        errno = DICF_ERROR_CANNOT_READ_HEADER;
        return NULL;
    }

    // Decompress block
    switch(blockHeader.compression)
    {
        case None:block = (uint8_t *)malloc(blockHeader.length);
            if(block == NULL)
            {
                errno = ENOMEM;
                return NULL;
            }

            readBytes = fread(block, blockHeader.length, 1, ctx->imageStream);

            if(readBytes != blockHeader.length)
            {
                free(block);
                errno = DICF_ERROR_CANNOT_READ_BLOCK;
                return NULL;
            }

            break;
        default:errno = DICF_ERROR_UNSUPPORTED_COMPRESSION;
            return NULL;
    }

    // Check if cache needs to be emptied
    // TODO: Caches

    // Add block to cache
    // TODO: Caches

    sector = (uint8_t *)malloc(blockHeader.sectorSize);

    if(sector == NULL)
    {
        free(block);
        errno = ENOMEM;
        return NULL;
    }

    memcpy(sector, block + offset, blockHeader.sectorSize);
    *length = blockHeader.sectorSize;
    free(block);
    return sector;
}