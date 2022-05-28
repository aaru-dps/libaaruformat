/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2022 Natalia Portillo.
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma clang diagnostic push
#pragma ide diagnostic   ignored "OCUnusedMacroInspection"
#ifndef LIBAARUFORMAT_CONSTS_H
#define LIBAARUFORMAT_CONSTS_H

/** Magic identidier = "DICMFRMT". */
#define DIC_MAGIC 0x544D52464D434944
/** Magic identidier = "AARUFRMT". */
#define AARU_MAGIC 0x544D524655524141
/** Image format version. A change in this number indicates an incompatible change to the format that prevents older
 * implementations from reading it correctly, if at all. */
#define AARUF_VERSION 1
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

#endif // LIBAARUFORMAT_CONSTS_H

#pragma clang diagnostic pop