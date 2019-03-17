// /***************************************************************************
// The Disc Image Chef
// ----------------------------------------------------------------------------
//
// Filename       : close.c
// Author(s)      : Natalia Portillo <claunia@claunia.com>
//
// Component      : libdicformat.
//
// --[ Description ] ----------------------------------------------------------
//
//     Handles closing DiscImageChef format disk images.
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

int close(void *context)
{
    if(context == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    dicformatContext *ctx = context;

    // TODO: Cast this field without casting the whole structure, as this can buffer overlow
    // Not a libdicformat context
    if(ctx->magic != DIC_MAGIC)
    {
        errno = EINVAL;
        return -1;
    }

    // This may do nothing if imageStream is NULL, but as the behaviour is undefined, better sure than sorry
    if(ctx->imageStream != NULL)
        fclose(ctx->imageStream);

    free(ctx->sectorPrefix);
    free(ctx->sectorPrefixCorrected);
    free(ctx->sectorSuffix);
    free(ctx->sectorSuffixCorrected);
    free(ctx->sectorSubchannel);
    free(ctx->mode2Subheaders);

    free(context);

    return 0;
}