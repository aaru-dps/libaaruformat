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

// TODO: Check CRC64 on structures
// TODO: ImageInfo
void *open(const char *filepath)
{
    dicformatContext *ctx = malloc(sizeof(dicformatContext));
    int              errorNo;
    size_t           readBytes;
    long             pos;
    IndexHeader      idxHeader;
    IndexEntry       *idxEntries;

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

    ctx->magic               = DIC_MAGIC;
    ctx->libraryMajorVersion = LIBDICFORMAT_MAJOR_VERSION;
    ctx->libraryMinorVersion = LIBDICFORMAT_MINOR_VERSION;

    free(idxEntries);
    return ctx;
}