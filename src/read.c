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

uint8_t *read_media_tag(void *context, int tag)
{
    if(context == NULL)
    {
        errno = EINVAL;
        return NULL;
    }

    dicformatContext *ctx = context;

    // TODO: Cast this field without casting the whole structure, as this can buffer overflow
    // Not a libdicformat context
    if(ctx->magic != DIC_MAGIC)
    {
        errno = EINVAL;
        return NULL;
    }

    dataLinkedList *item = ctx->mediaTagsHead;

    while(item != NULL)
    {
        if(item->type == tag)
            return item->data;

        item = item->next;
    }

    return NULL;
}