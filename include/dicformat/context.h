// /***************************************************************************
// The Disc Image Chef
// ----------------------------------------------------------------------------
//
// Filename       : context.h
// Author(s)      : Natalia Portillo <claunia@claunia.com>
//
// Component      : libdicformat.
//
// --[ Description ] ----------------------------------------------------------
//
//     Describes context to be passed between libdicformat functions.
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
#ifndef LIBDICFORMAT_CONTEXT_H
#define LIBDICFORMAT_CONTEXT_H

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

typedef struct dicformatContext
{
    uint64_t              magic;
    uint8_t               libraryMajorVersion;
    uint8_t               libraryMinorVersion;
    FILE                  *imageStream;
    DicHeader             header;
    struct dataLinkedList *mediaTagsHead;
    struct dataLinkedList *mediaTagsTail;
    unsigned char         *sectorPrefix;
    unsigned char         *sectorPrefixCorrected;
    unsigned char         *sectorSuffix;
    unsigned char         *sectorSuffixCorrected;
    unsigned char         *sectorSubchannel;
    unsigned char         *mode2Subheaders;
    unsigned char         shift;
    bool                  inMemoryDdt;
    uint64_t              *userDataDdt;
    size_t                mappedMemoryDdtSize;
    uint32_t              *sectorPrefixDdt;
    uint32_t              *sectorSuffixDdt;
    GeometryBlockHeader   geometryBlock;
} dicformatContext;

typedef struct dataLinkedList
{
    struct dataLinkedList *previous;
    struct dataLinkedList *next;
    unsigned char         *data;
    int                   type;
} dataLinkedList;

#endif //LIBDICFORMAT_CONTEXT_H
