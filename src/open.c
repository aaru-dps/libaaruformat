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

void *open(const char *filepath)
{
    dicformatContext *ctx = malloc(sizeof(dicformatContext));
    int              errorNo;
    size_t           readBytes;

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

    ctx->magic               = DIC_MAGIC;
    ctx->libraryMajorVersion = LIBDICFORMAT_MAJOR_VERSION;
    ctx->libraryMinorVersion = LIBDICFORMAT_MINOR_VERSION;

    fprintf(stderr,
            "libdicformat: Opened image version %d.%d",
            ctx->header.imageMajorVersion,
            ctx->header.imageMinorVersion);

    return ctx;
}