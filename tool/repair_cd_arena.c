/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*
 * repair-cd-arena
 *
 * Recovers CD sector prefix/suffix arenas in AaruFormat v2 images damaged by the resume bug in which a
 * resumed dump session restarted the custom prefix/suffix arena at offset 0. That left the final
 * deduplication table (DDT2) referencing slot indexes that point past the (shrunken) final data block,
 * so errored sectors read back as garbage.
 *
 * Such an image still physically contains every dump session's prefix/suffix data block and DDT2 (the old
 * generations are merely orphaned, not overwritten). This tool scans the file for all generations, and for
 * every custom entry in the final DDT2 finds the generation whose DDT2 held that exact (status,index) value
 * and whose data block actually contains the slot, then rebuilds a fresh compact arena + DDT2 with
 * renumbered indexes and rewrites the index to point at them.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <aaruformat.h>
#include <aaruformat/consts.h>
#include <aaruformat/enums.h>
#include <aaruformat/structs/data.h>
#include <aaruformat/structs/ddt.h>
#include <aaruformat/structs/header.h>
#include <aaruformat/structs/index.h>

#include "aaruformattool.h"
#include "usage.h"

#define PREFIX_SLOT 16u
#define SUFFIX_SLOT 288u

/// A decoded prefix/suffix generation: one data block and its matching DDT2, both for the same data type.
typedef struct
{
    uint16_t  data_type;    // 69 (prefix) or 70 (suffix)
    uint64_t  block_offset;  // file offset of the DataBlock (for ordering: session order == file order)
    uint8_t  *block;         // decoded arena bytes
    uint64_t  block_length;  // arena length in bytes
    uint64_t  ddt_offset;    // file offset of the DDT2
    uint64_t *ddt;           // decoded DDT2 entries (one uint64_t per internal sector)
    uint64_t  ddt_entries;   // number of DDT2 entries
} Generation;

static uint32_t status_of(const uint64_t entry) { return (uint32_t)(entry >> 60); }
static uint64_t index_of(const uint64_t entry) { return entry & 0x0FFFFFFFFFFFFFFFULL; }

/// True when the entry references a stored slot (custom bytes), false for the inline/regenerable statuses.
static bool is_custom(const uint64_t entry)
{
    switch(status_of(entry))
    {
        case SectorStatusNotDumped:
        case SectorStatusMode1Correct:
        case SectorStatusMode2Form1Ok:
        case SectorStatusMode2Form2Ok:
        case SectorStatusMode2Form2NoCrc: return false;
        default: return true;
    }
}

/// Decodes a DataBlock/DDT2 payload (compression none/lzma/zstd) into a freshly allocated buffer whose CRC64
/// matches @p crc64. The compressed payload starts at @p payload in the in-memory image, @p cmp_length long.
static uint8_t *decode_payload(const uint8_t *payload, const uint16_t compression, const uint64_t cmp_length,
                               const uint64_t length, const uint64_t crc64)
{
    uint8_t *out = NULL;

    if(compression == kCompressionNone)
    {
        out = malloc(length);
        if(out != NULL) memcpy(out, payload, length);
    }
    else if(compression == kCompressionLzma)
    {
        if(cmp_length < LZMA_PROPERTIES_LENGTH) return NULL;
        out = malloc(length);
        if(out != NULL)
        {
            size_t out_size = length;
            size_t src_size = cmp_length - LZMA_PROPERTIES_LENGTH;
            if(aaruf_lzma_decode_buffer(out, &out_size, payload + LZMA_PROPERTIES_LENGTH, &src_size,
                                        payload, LZMA_PROPERTIES_LENGTH) != 0 ||
               out_size != length)
            {
                free(out);
                out = NULL;
            }
        }
    }
    else if(compression == kCompressionZstd)
    {
        out = malloc(length);
        if(out != NULL && aaruf_zstd_decode_buffer(out, length, payload, cmp_length) != length)
        {
            free(out);
            out = NULL;
        }
    }

    if(out != NULL && aaruf_crc64_data(out, (uint32_t)length) != crc64)
    {
        free(out);
        return NULL;
    }

    return out;
}

/// Scans the in-memory image for prefix/suffix DataBlock and DDT2 blocks, decoding and CRC-validating each,
/// and pairs each DDT2 with the nearest preceding data block of the same type into a Generation.
static Generation *scan_generations(const uint8_t *img, const uint64_t file_size, size_t *out_count)
{
    // Collect decoded data blocks and DDT2s separately, then pair by (type, order).
    typedef struct
    {
        uint16_t type;
        uint64_t offset;
        uint8_t *data;
        uint64_t length;
    } Decoded;

    Decoded *blocks = NULL, *ddts = NULL;
    size_t   nblocks = 0, nddts = 0, capb = 0, capd = 0;

    for(uint64_t pos = 0; pos + sizeof(BlockHeader) <= file_size; pos++)
    {
        uint32_t ident;
        memcpy(&ident, img + pos, sizeof(ident));

        if(ident == DataBlock)
        {
            BlockHeader bh;
            memcpy(&bh, img + pos, sizeof(bh));
            if(bh.type != kDataTypeCdSectorPrefix && bh.type != kDataTypeCdSectorSuffix) continue;
            if(bh.length == 0 || bh.length > file_size) continue;

            uint64_t payload = pos + sizeof(BlockHeader);
            if(bh.compression == kCompressionLzma) payload += LZMA_PROPERTIES_LENGTH;
            if(payload + bh.cmpLength > file_size) continue;
            uint8_t *data = decode_payload(img + payload, bh.compression, bh.cmpLength, bh.length, bh.crc64);
            if(data == NULL) continue;

            if(nblocks == capb)
            {
                capb    = capb ? capb * 2 : 8;
                blocks  = realloc(blocks, capb * sizeof(Decoded));
            }
            blocks[nblocks++] = (Decoded){bh.type, pos, data, bh.length};
        }
        else if(ident == DeDuplicationTable2)
        {
            DdtHeader2 dh;
            if(pos + sizeof(DdtHeader2) > file_size) continue;
            memcpy(&dh, img + pos, sizeof(dh));
            if(dh.type != kDataTypeCdSectorPrefix && dh.type != kDataTypeCdSectorSuffix) continue;
            if(dh.length == 0 || dh.length > file_size || (dh.length % sizeof(uint64_t)) != 0) continue;

            uint64_t payload = pos + sizeof(DdtHeader2);
            if(dh.compression == kCompressionLzma) payload += LZMA_PROPERTIES_LENGTH;
            if(payload + dh.cmpLength > file_size) continue;
            uint8_t *data = decode_payload(img + payload, dh.compression, dh.cmpLength, dh.length, dh.crc64);
            if(data == NULL) continue;

            if(nddts == capd)
            {
                capd = capd ? capd * 2 : 8;
                ddts = realloc(ddts, capd * sizeof(Decoded));
            }
            ddts[nddts++] = (Decoded){dh.type, pos, data, dh.length};
        }
    }

    // Pair every DDT2 with the closest preceding data block of the same type (they are written together).
    Generation *gens  = calloc(nddts, sizeof(Generation));
    size_t      ngens = 0;

    for(size_t i = 0; i < nddts; i++)
    {
        Decoded *best = NULL;
        for(size_t j = 0; j < nblocks; j++)
            if(blocks[j].type == ddts[i].type && blocks[j].offset < ddts[i].offset &&
               (best == NULL || blocks[j].offset > best->offset))
                best = &blocks[j];

        if(best == NULL) continue;

        gens[ngens].data_type    = ddts[i].type;
        gens[ngens].block_offset = best->offset;
        gens[ngens].block        = best->data;
        gens[ngens].block_length = best->length;
        gens[ngens].ddt_offset   = ddts[i].offset;
        gens[ngens].ddt          = (uint64_t *)ddts[i].data;
        gens[ngens].ddt_entries  = ddts[i].length / sizeof(uint64_t);
        ngens++;
    }

    free(blocks);
    free(ddts);
    *out_count = ngens;
    return gens;
}

/// Locates the slot bytes for one custom entry by walking generations for a matching (status,index) whose
/// block holds the slot. Returns a pointer into a generation's block, or NULL if unrecoverable.
static const uint8_t *find_slot(const Generation *gens, const size_t ngens, const uint16_t type,
                                const uint64_t sector, const uint64_t entry, const uint32_t slot_size)
{
    const uint64_t idx = index_of(entry);

    for(size_t g = 0; g < ngens; g++)
    {
        if(gens[g].data_type != type) continue;
        if(sector >= gens[g].ddt_entries) continue;
        if(gens[g].ddt[sector] != entry) continue;
        if((idx + 1) * slot_size > gens[g].block_length) continue;
        return gens[g].block + idx * slot_size;
    }

    return NULL;
}

/// Rebuilds one arena (prefix or suffix) from the final DDT2. On success returns the new compact arena and
/// fills @p new_ddt (caller-provided, entries long) with renumbered entries; reports recovered/lost counts.
static uint8_t *rebuild_arena(const Generation *gens, const size_t ngens, const uint16_t type,
                              const uint64_t *final_ddt, const uint64_t entries, const uint32_t slot_size,
                              uint64_t *new_ddt, uint64_t *out_length, uint64_t *recovered, uint64_t *lost)
{
    uint64_t customs = 0;
    for(uint64_t s = 0; s < entries; s++)
        if(is_custom(final_ddt[s])) customs++;

    uint8_t *arena      = customs ? calloc(customs, slot_size) : NULL;
    uint64_t next_slot  = 0;
    *recovered          = 0;
    *lost               = 0;

    for(uint64_t s = 0; s < entries; s++)
    {
        if(!is_custom(final_ddt[s]))
        {
            new_ddt[s] = final_ddt[s];
            continue;
        }

        const uint8_t *slot = find_slot(gens, ngens, type, s, final_ddt[s], slot_size);
        if(slot == NULL)
        {
            // Unrecoverable: mark NotDumped so the sector reads back as a gap instead of garbage.
            new_ddt[s] = (uint64_t)SectorStatusNotDumped << 60;
            (*lost)++;
            continue;
        }

        memcpy(arena + next_slot * slot_size, slot, slot_size);
        new_ddt[s] = ((uint64_t)status_of(final_ddt[s]) << 60) | next_slot;
        next_slot++;
        (*recovered)++;
    }

    *out_length = next_slot * slot_size;
    return arena;
}

/// Appends a DataBlock (uncompressed) at an aligned EOF and returns its file offset (via @p out_offset).
static int append_data_block(FILE *fp, const uint16_t type, const uint8_t *data, const uint64_t length,
                             const uint8_t alignment_shift, uint64_t *out_offset)
{
    fseek(fp, 0, SEEK_END);
    uint64_t       pos  = (uint64_t)ftell(fp);
    const uint64_t mask = (1ULL << alignment_shift) - 1;
    pos                 = (pos + mask) & ~mask;
    fseek(fp, (long)pos, SEEK_SET);

    BlockHeader bh = {0};
    bh.identifier  = DataBlock;
    bh.type        = type;
    bh.compression = kCompressionNone;
    bh.sectorSize  = type == kDataTypeCdSectorPrefix ? PREFIX_SLOT : SUFFIX_SLOT;
    bh.cmpLength   = (uint32_t)length;
    bh.length      = (uint32_t)length;
    bh.crc64       = length ? aaruf_crc64_data(data, (uint32_t)length) : 0;
    bh.cmpCrc64    = bh.crc64;

    if(fwrite(&bh, sizeof(bh), 1, fp) != 1) return EIO;
    if(length && fwrite(data, length, 1, fp) != 1) return EIO;

    *out_offset = pos;
    return 0;
}

/// Appends a DDT2 (uncompressed) at an aligned EOF, cloning @p templ's geometry, and returns its offset.
static int append_ddt2(FILE *fp, const DdtHeader2 *templ, const uint16_t type, const uint64_t *ddt,
                       const uint64_t entries, uint64_t *out_offset)
{
    fseek(fp, 0, SEEK_END);
    uint64_t       pos  = (uint64_t)ftell(fp);
    const uint64_t mask = (1ULL << templ->blockAlignmentShift) - 1;
    pos                 = (pos + mask) & ~mask;
    fseek(fp, (long)pos, SEEK_SET);

    const uint64_t length = entries * sizeof(uint64_t);

    DdtHeader2 dh   = *templ;
    dh.identifier   = DeDuplicationTable2;
    dh.type         = type;
    dh.compression  = kCompressionNone;
    dh.entries      = entries;
    dh.cmpLength    = length;
    dh.length       = length;
    dh.crc64        = aaruf_crc64_data((const uint8_t *)ddt, (uint32_t)length);
    dh.cmpCrc64     = dh.crc64;

    if(fwrite(&dh, sizeof(dh), 1, fp) != 1) return EIO;
    if(length && fwrite(ddt, length, 1, fp) != 1) return EIO;

    *out_offset = pos;
    return 0;
}

int repair_cd_arena(const char *path, const bool dry_run)
{
    print_banner();
    printf("repair-cd-arena on '%s'%s\n\n", path, dry_run ? " (dry run)" : "");

    FILE *fp = fopen(path, dry_run ? "rb" : "r+b");
    if(fp == NULL)
    {
        printf("ERROR: cannot open '%s': %s\n", path, strerror(errno));
        return errno;
    }

    AaruHeaderV2 header;
    if(fread(&header, 1, sizeof(header), fp) != sizeof(header))
    {
        printf("ERROR: cannot read header\n");
        fclose(fp);
        return EIO;
    }
    if(header.identifier != AARU_MAGIC || header.imageMajorVersion != AARUF_VERSION_V2)
    {
        printf("ERROR: not an AaruFormat v2 image\n");
        fclose(fp);
        return EINVAL;
    }

    fseek(fp, 0, SEEK_END);
    const uint64_t file_size = (uint64_t)ftell(fp);

    // Load the whole image into memory for fast scanning/decoding.
    uint8_t *img = malloc(file_size);
    if(img == NULL)
    {
        printf("ERROR: cannot allocate %llu bytes for image\n", (unsigned long long)file_size);
        fclose(fp);
        return ENOMEM;
    }
    fseek(fp, 0, SEEK_SET);
    if(fread(img, 1, file_size, fp) != file_size)
    {
        printf("ERROR: cannot read image into memory\n");
        free(img);
        fclose(fp);
        return EIO;
    }

    // --- Read the current index (IndexBlock3 chain) ---
    size_t      index_count = 0, index_cap = 0;
    IndexEntry *index       = NULL;
    uint64_t    idx_off     = header.indexOffset;
    while(idx_off)
    {
        IndexHeader3 ih;
        if(fseek(fp, (long)idx_off, SEEK_SET) != 0 || fread(&ih, 1, sizeof(ih), fp) != sizeof(ih) ||
           ih.identifier != IndexBlock3)
        {
            printf("ERROR: cannot read index at %llu\n", (unsigned long long)idx_off);
            free(index);
            fclose(fp);
            return EIO;
        }
        for(uint64_t i = 0; i < ih.entries; i++)
        {
            IndexEntry e;
            if(fread(&e, 1, sizeof(e), fp) != sizeof(e)) break;
            if(index_count == index_cap)
            {
                index_cap = index_cap ? index_cap * 2 : 32;
                index     = realloc(index, index_cap * sizeof(IndexEntry));
            }
            index[index_count++] = e;
        }
        idx_off = ih.previous;
    }

    // --- Locate the final prefix/suffix DDT2 the index references ---
    uint64_t final_prefix_ddt_off = 0, final_suffix_ddt_off = 0;
    for(size_t i = 0; i < index_count; i++)
        if(index[i].blockType == DeDuplicationTable2)
        {
            if(index[i].dataType == kDataTypeCdSectorPrefix) final_prefix_ddt_off = index[i].offset;
            else if(index[i].dataType == kDataTypeCdSectorSuffix)
                final_suffix_ddt_off = index[i].offset;
        }

    if(final_prefix_ddt_off == 0 && final_suffix_ddt_off == 0)
    {
        printf("Image has no CD sector prefix/suffix DDT2; nothing to repair.\n");
        free(index);
        fclose(fp);
        return 0;
    }

    // --- Scan all generations ---
    size_t      ngens = 0;
    Generation *gens  = scan_generations(img, file_size, &ngens);
    printf("Found %zu prefix/suffix generations.\n", ngens);

    int rc = 0;

    // Process both arenas.
    struct
    {
        uint16_t  type;
        uint64_t  final_ddt_off;
        uint32_t  slot;
        uint64_t  new_block_off;
        uint64_t  new_ddt_off;
    } jobs[2] = {
        {kDataTypeCdSectorPrefix, final_prefix_ddt_off, PREFIX_SLOT, 0, 0},
        {kDataTypeCdSectorSuffix, final_suffix_ddt_off, SUFFIX_SLOT, 0, 0},
    };

    for(int j = 0; j < 2; j++)
    {
        if(jobs[j].final_ddt_off == 0) continue;

        // Find the final DDT2 among the scanned generations (by offset) and its header template.
        const Generation *final_gen = NULL;
        for(size_t g = 0; g < ngens; g++)
            if(gens[g].data_type == jobs[j].type && gens[g].ddt_offset == jobs[j].final_ddt_off)
                final_gen = &gens[g];

        if(final_gen == NULL)
        {
            printf("WARNING: final %s DDT2 could not be decoded; skipping.\n",
                   jobs[j].type == kDataTypeCdSectorPrefix ? "prefix" : "suffix");
            continue;
        }

        DdtHeader2 templ;
        if(fseek(fp, (long)jobs[j].final_ddt_off, SEEK_SET) != 0 ||
           fread(&templ, 1, sizeof(templ), fp) != sizeof(templ))
        {
            rc = EIO;
            break;
        }

        const uint64_t entries  = final_gen->ddt_entries;
        uint64_t      *new_ddt  = malloc(entries * sizeof(uint64_t));
        uint64_t       reclen = 0, recovered = 0, lost = 0;
        uint8_t       *arena =
            rebuild_arena(gens, ngens, jobs[j].type, final_gen->ddt, entries, jobs[j].slot, new_ddt, &reclen,
                          &recovered, &lost);

        printf("  %s: %llu custom slots recovered, %llu unrecoverable (marked not-dumped).\n",
               jobs[j].type == kDataTypeCdSectorPrefix ? "prefix" : "suffix",
               (unsigned long long)recovered, (unsigned long long)lost);

        if(!dry_run)
        {
            if(append_data_block(fp, jobs[j].type, arena, reclen, templ.blockAlignmentShift,
                                 &jobs[j].new_block_off) != 0 ||
               append_ddt2(fp, &templ, jobs[j].type, new_ddt, entries, &jobs[j].new_ddt_off) != 0)
                rc = EIO;
        }

        free(arena);
        free(new_ddt);
        if(rc) break;
    }

    // --- Rewrite the index pointing at the new blocks, and update the header ---
    if(!dry_run && rc == 0)
    {
        for(size_t i = 0; i < index_count; i++)
        {
            if(index[i].blockType == DataBlock && index[i].dataType == kDataTypeCdSectorPrefix && jobs[0].new_block_off)
                index[i].offset = jobs[0].new_block_off;
            else if(index[i].blockType == DeDuplicationTable2 && index[i].dataType == kDataTypeCdSectorPrefix &&
                    jobs[0].new_ddt_off)
                index[i].offset = jobs[0].new_ddt_off;
            else if(index[i].blockType == DataBlock && index[i].dataType == kDataTypeCdSectorSuffix &&
                    jobs[1].new_block_off)
                index[i].offset = jobs[1].new_block_off;
            else if(index[i].blockType == DeDuplicationTable2 && index[i].dataType == kDataTypeCdSectorSuffix &&
                    jobs[1].new_ddt_off)
                index[i].offset = jobs[1].new_ddt_off;
        }

        fseek(fp, 0, SEEK_END);
        uint64_t       new_index = (uint64_t)ftell(fp);
        const uint64_t mask      = (1ULL << header.blockAlignmentShift) - 1;
        new_index                = (new_index + mask) & ~mask;
        fseek(fp, (long)new_index, SEEK_SET);

        IndexHeader3 ih = {0};
        ih.identifier   = IndexBlock3;
        ih.entries      = index_count;
        ih.previous     = 0;  // single self-contained index segment
        ih.crc64        = aaruf_crc64_data((const uint8_t *)index, (uint32_t)(index_count * sizeof(IndexEntry)));

        if(fwrite(&ih, sizeof(ih), 1, fp) != 1 || fwrite(index, sizeof(IndexEntry), index_count, fp) != index_count)
            rc = EIO;
        else
        {
            header.indexOffset = new_index;
            fseek(fp, 0, SEEK_SET);
            if(fwrite(&header, sizeof(header), 1, fp) != 1) rc = EIO;
        }

        if(rc == 0)
        {
            fflush(fp);
            printf("\nRepair written. New index at %llu.\n", (unsigned long long)new_index);
        }
    }
    else if(dry_run)
        printf("\nDry run only; no changes written.\n");

    for(size_t g = 0; g < ngens; g++)
    {
        free(gens[g].block);
        free(gens[g].ddt);
    }
    free(gens);
    free(index);
    free(img);
    fclose(fp);

    if(rc) printf("ERROR: repair failed (%d)\n", rc);
    return rc;
}
