// /***************************************************************************
// The Disc Image Chef
// ----------------------------------------------------------------------------
//
// Filename       : consts.h
// Author(s)      : Natalia Portillo <claunia@claunia.com>
//
// Component      : libdicformat.
//
// --[ Description ] ----------------------------------------------------------
//
//     Contains constants for DiscImageChef format disk images.
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

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedMacroInspection"
#ifndef LIBDICFORMAT_CONSTS_H
#define LIBDICFORMAT_CONSTS_H

/** Magic identidier = "DICMFMT". */
#define DIC_MAGIC 0x544D52464D434944
/** Image format version. A change in this number indicates an incompatible change to the format that prevents older implementations from reading it correctly, if at all. */
#define DICF_VERSION 1
/** Maximum read cache size, 256MiB. */
#define MAX_CACHE_SIZE 256 * 1024 * 1024
/** Size in bytes of LZMA properties. */
#define LZMA_PROPERTIES_LENGTH 5
/** Maximum number of entries for the DDT cache. */
#define MAX_DDT_ENTRY_CACHE 16000000
/** How many samples are contained in a RedBook sector. */
#define SAMPLES_PER_SECTOR 588
/** Maximum number of samples for a FLAC block. Bigger than 4608 gives no benefit. */
#define MAX_FLAKE_BLOCK 4608
/** Minimum number of samples for a FLAC block. CUETools.Codecs.FLAKE does not support it to be smaller than 256. */
#define MIN_FLAKE_BLOCK 256
/** This mask is to check for flags in CompactDisc suffix/prefix DDT */
#define CD_XFIX_MASK 0xFF000000
/** This mask is to check for position in CompactDisc suffix/prefix deduplicated block */
#define CD_DFIX_MASK 0x00FFFFFF

#define CRC64_ECMA_POLY 0xC96C5795D7870F42
#define CRC64_ECMA_SEED 0xFFFFFFFFFFFFFFFF

#endif //LIBDICFORMAT_CONSTS_H

#pragma clang diagnostic pop