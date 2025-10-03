/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2025 Natalia Portillo.
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

#ifndef LIBAARUFORMAT_CONTEXT_H
#define LIBAARUFORMAT_CONTEXT_H

#include "crc64.h"
#include "hash_map.h"
#include "lru.h"
#include "md5.h"
#include "sha1.h"
#include "sha256.h"
#include "structs.h"
#include "utarray.h"

/** \file aaruformat/context.h
 *  \brief Central runtime context structures for libaaruformat (image state, caches, checksum buffers).
 *
 *  The principal structure, \ref aaruformatContext, aggregates: header metadata, open stream handle, deduplication
 *  tables (DDT) currently in memory, optical disc auxiliary data (sector prefix/suffix/subchannel), track listings,
 *  geometry & metadata blocks, checksum accumulators, CRC & ECC helper contexts, hash map for deduplication, and
 *  transient write buffers.
 *
 *  Memory ownership model (unless otherwise stated): if a pointer field is non-NULL it is owned by the context and
 *  will be freed (or otherwise released) during context close / destruction. Callers must not free or reallocate
 *  these pointers directly. External callers should treat all internal buffers as read‑only unless explicitly writing.
 *
 *  Threading: a single context instance is NOT thread-safe; serialize access if used across threads.
 *  Lifetime: allocate, initialize/open, perform read/write/verify operations, then close/free.
 *
 *  Deduplication tables (DDT): only a subset (primary table + an active secondary + optional cache) is retained in RAM;
 *  large images may rely on lazy loading of secondary tables. Flags (inMemoryDdt, userDataDdt*, cachedSecondary*)
 *  indicate what is currently resident.
 *
 *  Optical auxiliary buffers (sectorPrefix / sectorSuffix / subchannel / corrected variants) are populated only for
 *  images where those components exist (e.g., raw CD dumps). They may be NULL for block devices / non‑optical media.
 *
 *  Index handling: indexEntries (UT_array) holds a flattened list of \ref IndexEntry structures (regardless of
 * v1/v2/v3). hash_map_t *sectorHashMap provides fast duplicate detection keyed by content fingerprint / sparse sector
 * key.
 *
 *  Invariants / sanity expectations (not strictly enforced everywhere):
 *   - magic == AARU_MAGIC after successful open/create.
 *   - header.imageMajorVersion <= AARUF_VERSION.
 *   - imageStream != NULL when any I/O method is in progress.
 *   - If deduplicate == false, sectorHashMap may still be populated for bookkeeping but duplicates are stored
 * independently.
 *   - If userDataDdtMini != NULL then userDataDdtBig == NULL (and vice versa) for a given level.
 */

#ifndef MD5_DIGEST_LENGTH
#define MD5_DIGEST_LENGTH 16
#endif

#ifndef SHA1_DIGEST_LENGTH
#define SHA1_DIGEST_LENGTH 20
#endif

#ifndef SHA256_DIGEST_LENGTH
#define SHA256_DIGEST_LENGTH 32
#endif

/** \struct CdEccContext
 *  \brief Lookup tables and state for Compact Disc EDC/ECC (P/Q) regeneration / verification.
 *
 *  Fields may be lazily allocated; inited_edc indicates tables are ready.
 */
typedef struct CdEccContext
{
    bool      inited_edc;   ///< True once EDC/ECC tables have been initialized.
    uint8_t  *ecc_b_table;  ///< Backward (B) ECC table (allocated, size implementation-defined).
    uint8_t  *ecc_f_table;  ///< Forward (F) ECC table.
    uint32_t *edc_table;    ///< EDC (CRC) lookup table.
} CdEccContext;

/** \struct Checksums
 *  \brief Collected whole‑image checksums / hashes present in a checksum block.
 *
 *  Only hash arrays with corresponding has* flags set contain valid data. spamsum is a dynamically allocated
 *  NUL‑terminated buffer (original SpamSum signature bytes followed by appended '\0').
 */
typedef struct Checksums
{
    bool     hasMd5;                        ///< True if md5[] buffer populated.
    bool     hasSha1;                       ///< True if sha1[] buffer populated.
    bool     hasSha256;                     ///< True if sha256[] buffer populated.
    bool     hasSpamSum;                    ///< True if spamsum pointer allocated and signature read.
    uint8_t  md5[MD5_DIGEST_LENGTH];        ///< MD5 digest (16 bytes).
    uint8_t  sha1[SHA1_DIGEST_LENGTH];      ///< SHA-1 digest (20 bytes).
    uint8_t  sha256[SHA256_DIGEST_LENGTH];  ///< SHA-256 digest (32 bytes).
    uint8_t *spamsum;                       ///< SpamSum fuzzy hash (ASCII), allocated length+1 with trailing 0.
} Checksums;

/** \struct mediaTagEntry
 *  \brief Hash table entry for an arbitrary media tag (e.g., proprietary drive/medium descriptor).
 *
 *  Stored via uthash (hh handle). Type is a format‑specific integer identifier mapping to external interpretation.
 */
typedef struct mediaTagEntry
{
    uint8_t       *data;    ///< Tag data blob (opaque to library core); length bytes long.
    int32_t        type;    ///< Numeric type identifier.
    uint32_t       length;  ///< Length in bytes of data.
    UT_hash_handle hh;      ///< uthash linkage.
} mediaTagEntry;

/** \struct aaruformatContext
 *  \brief Master context representing an open or in‑creation Aaru image.
 *
 *  Contains stream handle, parsed headers, deduplication structures, optical extras, metadata blocks, checksum
 *  information, caches, and write-state. Allocate with library factory (or zero‑init + explicit open) and destroy
 *  with corresponding close/free routine.
 *
 *  Field grouping:
 *   - Core & header: magic, library*Version, imageStream, header.
 *   - Optical sector adjuncts: sectorPrefix/sectorSuffix/subchannel plus corrected variants & mode2Subheaders.
 *   - Deduplication: inMemoryDdt, userDataDdt*, userDataDdtHeader, mini/big/cached secondary arrays, version tags.
 *   - Metadata & geometry: geometryBlock, metadataBlockHeader+metadataBlock, cicmBlockHeader+cicmBlock, tracksHeader.
 *   - Tracks & hardware: trackEntries, dataTracks, dumpHardwareHeader, dumpHardwareEntriesWithData.
 *   - Integrity & ECC: checksums, eccCdContext, crc64Context.
 *   - Index & dedup lookup: indexEntries (UT_array of IndexEntry), sectorHashMap (duplicate detection), deduplicate
 * flag.
 *   - Write path: isWriting, currentBlockHeader, writingBuffer(+position/offset), nextBlockPosition.
 *
 *  Notes:
 *   - userDataDdt points to memory-mapped or fully loaded DDT (legacy path); userDataDdtMini / userDataDdtBig
 * supersede.
 *   - shift retained for backward compatibility with earlier single‑level address shift semantics.
 *   - mappedMemoryDdtSize is meaningful only if userDataDdt references an mmapped region.
 */
typedef struct aaruformatContext
{
    uint64_t     magic;                ///< File magic (AARU_MAGIC) post-open.
    uint8_t      libraryMajorVersion;  ///< Linked library major version.
    uint8_t      libraryMinorVersion;  ///< Linked library minor version.
    FILE        *imageStream;          ///< Underlying FILE* stream (binary mode).
    AaruHeaderV2 header;               ///< Parsed container header (v2).

    /* Optical auxiliary buffers (NULL if not present) */
    uint8_t *sectorPrefix;           ///< Raw per-sector prefix (e.g., sync+header) uncorrected.
    uint8_t *sectorPrefixCorrected;  ///< Corrected variant (post error correction) if stored.
    uint8_t *sectorSuffix;           ///< Raw per-sector suffix (EDC/ECC) uncorrected.
    uint8_t *sectorSuffixCorrected;  ///< Corrected suffix if stored separately.
    uint8_t *sectorSubchannel;       ///< Raw 96-byte subchannel (if captured).
    uint8_t *mode2Subheaders;        ///< MODE2 Form1/Form2 8-byte subheaders (concatenated).

    uint8_t   shift;                ///< Legacy overall shift (deprecated by data_shift/table_shift).
    bool      inMemoryDdt;          ///< True if primary (and possibly secondary) DDT loaded.
    uint64_t *userDataDdt;          ///< Legacy flat DDT pointer (NULL when using v2 mini/big arrays).
    size_t    mappedMemoryDdtSize;  ///< Length of mmapped DDT if userDataDdt is mmapped.
    uint32_t *sectorPrefixDdt;      ///< Legacy CD sector prefix DDT (deprecated by *_Mini/Big).
    uint32_t *sectorSuffixDdt;      ///< Legacy CD sector suffix DDT.

    GeometryBlockHeader                 geometryBlock;        ///< Logical geometry block (if present).
    MetadataBlockHeader                 metadataBlockHeader;  ///< Metadata block header.
    uint8_t                            *metadataBlock;        ///< Raw metadata UTF-16LE concatenated strings.
    TracksHeader                        tracksHeader;         ///< Tracks header (optical) if present.
    TrackEntry                         *trackEntries;         ///< Full track list (tracksHeader.entries elements).
    CicmMetadataBlock                   cicmBlockHeader;      ///< CICM metadata header (if present).
    uint8_t                            *cicmBlock;            ///< CICM XML payload.
    DumpHardwareHeader                  dumpHardwareHeader;   ///< Dump hardware header.
    struct DumpHardwareEntriesWithData *dumpHardwareEntriesWithData;  ///< Array of dump hardware entries + strings.
    ImageInfo                           imageInfo;                    ///< Exposed high-level image info summary.

    CdEccContext *eccCdContext;        ///< CD ECC/EDC helper tables (allocated on demand).
    uint8_t       numberOfDataTracks;  ///< Count of tracks considered "data" (sequence 1..99 heuristics).
    TrackEntry   *dataTracks;          ///< Filtered list of data tracks (subset of trackEntries).
    bool         *readableSectorTags;  ///< Per-sector boolean array (optical tags read successfully?).

    struct CacheHeader blockHeaderCache;  ///< LRU/Cache header for block headers.
    struct CacheHeader blockCache;        ///< LRU/Cache header for block payloads.

    Checksums      checksums;  ///< Whole-image checksums discovered.
    mediaTagEntry *mediaTags;  ///< Hash table of extra media tags (uthash root).

    DdtHeader2 userDataDdtHeader;    ///< Active user data DDT v2 header (primary table meta).
    int        ddtVersion;           ///< DDT version in use (1=legacy, 2=v2 hierarchical).
    uint16_t  *userDataDdtMini;      ///< DDT entries (small variant) primary/secondary current.
    uint32_t  *userDataDdtBig;       ///< DDT entries (big variant) primary/secondary current.
    uint16_t  *sectorPrefixDdtMini;  ///< CD sector prefix corrected DDT (small) if present.
    uint16_t  *sectorSuffixDdtMini;  ///< CD sector suffix corrected DDT (small) if present.

    uint64_t  cachedDdtOffset;          ///< File offset of currently cached secondary DDT (0=none).
    uint64_t  cachedDdtPosition;        ///< Position index of cached secondary DDT.
    uint64_t  primaryDdtOffset;         ///< File offset of the primary DDT v2 table.
    uint16_t *cachedSecondaryDdtSmall;  ///< Cached secondary table (small entries) or NULL.
    uint32_t *cachedSecondaryDdtBig;    ///< Cached secondary table (big entries) or NULL.

    bool        isWriting;              ///< True if context opened/created for writing.
    BlockHeader currentBlockHeader;     ///< Header for block currently being assembled (write path).
    uint8_t    *writingBuffer;          ///< Accumulation buffer for current block data.
    int         currentBlockOffset;     ///< Logical offset inside block (units: bytes or sectors depending on path).
    crc64_ctx  *crc64Context;           ///< Opaque CRC64 context for streaming updates.
    int         writingBufferPosition;  ///< Current size / position within writingBuffer.
    uint64_t    nextBlockPosition;      ///< Absolute file offset where next block will be written.

    UT_array   *indexEntries;   ///< Flattened index entries (UT_array of IndexEntry).
    hash_map_t *sectorHashMap;  ///< Deduplication hash map (fingerprint->entry mapping).
    bool        deduplicate;    ///< Storage deduplication active (duplicates coalesce).

    bool       rewinded;            ///< True if stream has been rewound after open (write path).
    uint64_t   last_written_block;  ///< Last written block number (write path).
    bool       calculating_md5;     ///< True if whole-image MD5 being calculated on-the-fly.
    md5_ctx    md5_context;         ///< Opaque MD5 context for streaming updates
    bool       calculating_sha1;    ///< True if whole-image SHA-1 being calculated on-the-fly.
    sha1_ctx   sha1_context;        ///< Opaque SHA-1 context for streaming updates
    bool       calculating_sha256;  ///< True if whole-image SHA-256 being calculated on-the-fly.
    sha256_ctx sha256_context;      ///< Opaque SHA-256 context for streaming updates
} aaruformatContext;

/** \struct DumpHardwareEntriesWithData
 *  \brief In-memory representation of a dump hardware entry plus decoded variable-length fields & extents.
 *
 *  All string pointers are NUL-terminated UTF-8 copies of on-disk data (or NULL if absent). extents array may be NULL
 *  when no ranges were recorded. Freed during context teardown.
 */
typedef struct DumpHardwareEntriesWithData
{
    DumpHardwareEntry  entry;                    ///< Fixed-size header with lengths & counts.
    struct DumpExtent *extents;                  ///< Array of extents (entry.extents elements) or NULL.
    uint8_t           *manufacturer;             ///< Manufacturer string (UTF-8) or NULL.
    uint8_t           *model;                    ///< Model string or NULL.
    uint8_t           *revision;                 ///< Hardware revision string or NULL.
    uint8_t           *firmware;                 ///< Firmware version string or NULL.
    uint8_t           *serial;                   ///< Serial number string or NULL.
    uint8_t           *softwareName;             ///< Dump software name or NULL.
    uint8_t           *softwareVersion;          ///< Dump software version or NULL.
    uint8_t           *softwareOperatingSystem;  ///< Host operating system string or NULL.
} DumpHardwareEntriesWithData;

#pragma pack(push, 1)

/** \struct DumpExtent
 *  \brief Inclusive [start,end] logical sector range contributed by a single hardware environment.
 */
typedef struct DumpExtent
{
    uint64_t start;  ///< Starting LBA (inclusive).
    uint64_t end;    ///< Ending LBA (inclusive); >= start.
} DumpExtent;

#pragma pack(pop)

#endif  // LIBAARUFORMAT_CONTEXT_H
