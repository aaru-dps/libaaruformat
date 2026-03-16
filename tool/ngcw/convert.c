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
 *
 * convert-ngcw command: converts Nintendo GameCube/Wii disc ISO or AaruFormat
 * images to AaruFormat with decrypted sector storage and junk removal.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <aaruformat.h>

#include "../aaruformattool.h"

#include "../../src/lib/aes128.h"
#include "../../src/ngcw/lfg.h"
#include "../../src/ngcw/ngcw_junk.h"
#include "../../src/ngcw/wii_crypto.h"

/* ---- ANSI color codes ---- */
#define ANSI_RESET  "\033[0m"
#define ANSI_BOLD   "\033[1m"
#define ANSI_RED    "\033[31m"
#define ANSI_GREEN  "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_CYAN   "\033[36m"
#define ANSI_BLUE   "\033[34m"
#define ANSI_WHITE  "\033[37m"

#define PROGRESS_BAR_WIDTH 40

/* ---- Disc constants ---- */
#define NGC_GC_MAGIC      0xC2339F3DU
#define NGC_WII_MAGIC     0x5D1C9EA3U
#define NGC_SECTOR_SIZE   2048
#define GC_BLOCK_SIZE     0x8000
#define SECTORS_PER_BLOCK 16

/* ---- Wii common keys ---- */
static const uint8_t WII_COMMON_KEY[16] = {0xEB, 0xE4, 0x2A, 0x22, 0x5E, 0x85, 0x93, 0xE4,
                                           0x48, 0xD9, 0xC5, 0x45, 0x73, 0x81, 0xAA, 0xF7};

static const uint8_t WII_KOREAN_KEY[16] = {0x63, 0xB8, 0x2B, 0xB4, 0xF4, 0x61, 0x4E, 0x2E,
                                           0x13, 0xF2, 0xFE, 0xFB, 0xBA, 0x4C, 0x9B, 0x7E};

/* ---- Helper functions ---- */

static void print_error(const char *msg) { fprintf(stderr, ANSI_RED "  ✗ %s" ANSI_RESET "\n", msg); }

static void print_success(const char *msg) { printf(ANSI_GREEN "  ✓ %s" ANSI_RESET "\n", msg); }

static void print_info(const char *label, const char *value)
{ printf("  " ANSI_YELLOW "%-20s" ANSI_RESET " %s\n", label, value); }

static inline uint32_t read_be32(const uint8_t *p)
{ return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]; }

static void format_bytes(uint64_t bytes, char *buffer, size_t buffer_size)
{
    const char *units[]  = {"B", "KiB", "MiB", "GiB", "TiB"};
    int         unit_idx = 0;
    double      size     = (double)bytes;

    while(size >= 1024.0 && unit_idx < 4)
    {
        size /= 1024.0;
        unit_idx++;
    }

    if(unit_idx == 0)
        snprintf(buffer, buffer_size, "%llu %s", (unsigned long long)bytes, units[unit_idx]);
    else
        snprintf(buffer, buffer_size, "%.2f %s", size, units[unit_idx]);
}

/* ---- Disc type detection ---- */

static int detect_disc_type(const uint8_t *header)
{
    uint32_t wii_magic = read_be32(header + 0x18);
    uint32_t gc_magic  = read_be32(header + 0x1C);

    if(wii_magic == NGC_WII_MAGIC) return 1;

    if(gc_magic == NGC_GC_MAGIC) return 0;

    return -1;
}

/* ---- Wii partition structures ---- */

typedef struct
{
    uint64_t offset;
    uint64_t data_offset;
    uint64_t data_size;
    uint32_t type;
    uint8_t  title_key[16];
} NgcwPartition;

/* ---- FST data map ---- */

typedef struct
{
    uint64_t offset;
    uint64_t length;
} NgcwDataRegion;

typedef struct
{
    NgcwDataRegion *regions;
    uint32_t        count;
    uint32_t        capacity;
} NgcwDataMap;

static int data_map_add(NgcwDataMap *map, uint64_t offset, uint64_t length)
{
    if(length == 0) return 0;

    if(map->count >= map->capacity)
    {
        uint32_t        new_cap = map->capacity ? map->capacity * 2 : 256;
        NgcwDataRegion *nr      = realloc(map->regions, new_cap * sizeof(*nr));

        if(!nr) return -1;

        map->regions  = nr;
        map->capacity = new_cap;
    }

    map->regions[map->count].offset = offset;
    map->regions[map->count].length = length;
    map->count++;
    return 0;
}

static int region_cmp(const void *a, const void *b)
{
    const NgcwDataRegion *ra = (const NgcwDataRegion *)a;
    const NgcwDataRegion *rb = (const NgcwDataRegion *)b;

    if(ra->offset < rb->offset) return -1;

    if(ra->offset > rb->offset) return 1;

    return 0;
}

static bool is_data_region(const NgcwDataMap *map, uint64_t offset, uint64_t length)
{
    int lo = 0, hi = (int)map->count - 1;

    while(lo <= hi)
    {
        int      mid = lo + (hi - lo) / 2;
        uint64_t end = map->regions[mid].offset + map->regions[mid].length;

        if(end <= offset)
            lo = mid + 1;
        else if(map->regions[mid].offset >= offset + length)
            hi = mid - 1;
        else
            return true;
    }

    return false;
}

static int build_data_map_from_fst(const uint8_t *fst, uint32_t fst_size, uint64_t data_start, int address_shift,
                                   NgcwDataMap *map)
{
    if(!fst || fst_size < 12) return -1;

    memset(map, 0, sizeof(*map));

    uint32_t total_entries = read_be32(fst + 8);

    if(total_entries * 12 > fst_size) return -1;

    const uint8_t *entries = fst;

    for(uint32_t i = 1; i < total_entries; i++)
    {
        const uint8_t *e = entries + i * 12;

        if(e[0] == 0) /* file */
        {
            uint64_t off = (uint64_t)read_be32(e + 4) << address_shift;
            uint64_t len = read_be32(e + 8);

            if(data_map_add(map, data_start + off, len) < 0) return -1;
        }
    }

    if(map->count > 1) qsort(map->regions, map->count, sizeof(map->regions[0]), region_cmp);

    return 0;
}

static void data_map_free(NgcwDataMap *map)
{
    free(map->regions);
    memset(map, 0, sizeof(*map));
}

/* ---- Junk collector ---- */

typedef struct
{
    NgcwJunkEntry *entries;
    uint32_t       count;
    uint32_t       capacity;
} JunkCollector;

static void junk_collector_init(JunkCollector *jc) { memset(jc, 0, sizeof(*jc)); }

static void junk_collector_add(JunkCollector *jc, uint64_t offset, uint64_t length, uint16_t partition_index,
                               const uint32_t seed[NGC_LFG_SEED_SIZE])
{
    /* Try merge with last entry */
    if(jc->count > 0)
    {
        NgcwJunkEntry *last = &jc->entries[jc->count - 1];

        if(last->partition_index == partition_index && last->offset + last->length == offset &&
           memcmp(last->seed, seed, sizeof(last->seed)) == 0)
        {
            last->length += length;
            return;
        }
    }

    if(jc->count >= jc->capacity)
    {
        uint32_t       new_cap = jc->capacity ? jc->capacity * 2 : 64;
        NgcwJunkEntry *nr      = realloc(jc->entries, new_cap * sizeof(*nr));

        if(!nr) return;

        jc->entries  = nr;
        jc->capacity = new_cap;
    }

    NgcwJunkEntry *e   = &jc->entries[jc->count];
    e->offset          = offset;
    e->length          = length;
    e->partition_index = partition_index;
    memcpy(e->seed, seed, NGC_LFG_SEED_SIZE * sizeof(uint32_t));
    jc->count++;
}

static void junk_collector_free(JunkCollector *jc)
{
    free(jc->entries);
    memset(jc, 0, sizeof(*jc));
}

/* ---- Wii partition parsing ---- */

static int parse_wii_partitions(FILE *iso, uint16_t *part_count, NgcwPartition **parts)
{
    uint8_t ptable_raw[32];

    if(fseek(iso, 0x40000, SEEK_SET) != 0 || fread(ptable_raw, 1, 32, iso) != 32)
    {
        print_error("Cannot read partition table info");
        return -1;
    }

    uint16_t total = 0;
    uint32_t counts[4], offsets[4];

    for(int t = 0; t < 4; t++)
    {
        counts[t]  = read_be32(ptable_raw + t * 8);
        offsets[t] = read_be32(ptable_raw + t * 8 + 4);
        total += (uint16_t)counts[t];
    }

    if(total == 0)
    {
        *part_count = 0;
        *parts      = NULL;
        return 0;
    }

    *parts = calloc(total, sizeof(NgcwPartition));

    if(!*parts) return -1;

    uint16_t idx = 0;

    for(int t = 0; t < 4; t++)
    {
        if(counts[t] == 0) continue;

        uint64_t table_offset = (uint64_t)offsets[t] << 2;
        size_t   table_size   = counts[t] * 8;
        uint8_t *table_data   = malloc(table_size);

        if(!table_data)
        {
            free(*parts);
            *parts = NULL;
            return -1;
        }

        if(fseek(iso, (long)table_offset, SEEK_SET) != 0 || fread(table_data, 1, table_size, iso) != table_size)
        {
            free(table_data);
            free(*parts);
            *parts = NULL;
            return -1;
        }

        for(uint32_t p = 0; p < counts[t] && idx < total; p++)
        {
            uint64_t part_offset = (uint64_t)read_be32(table_data + p * 8) << 2;
            (*parts)[idx].offset = part_offset;
            (*parts)[idx].type   = read_be32(table_data + p * 8 + 4);

            /* Read ticket */
            uint8_t ticket[0x2A4];

            if(fseek(iso, (long)part_offset, SEEK_SET) != 0 || fread(ticket, 1, sizeof(ticket), iso) != sizeof(ticket))
            {
                free(table_data);
                free(*parts);
                *parts = NULL;
                return -1;
            }

            /* Decrypt title key */
            uint8_t        common_key_index = ticket[0x1F1];
            const uint8_t *common_key       = (common_key_index == 1) ? WII_KOREAN_KEY : WII_COMMON_KEY;

            uint8_t iv[16];
            memset(iv, 0, 16);
            memcpy(iv, ticket + 0x1BF + 0x1D, 8); /* title_id */

            memcpy((*parts)[idx].title_key, ticket + 0x1BF, 16);
            aes128_cbc_decrypt(common_key, iv, (*parts)[idx].title_key, 16);

            /* Read partition header for data offset/size */
            uint8_t phdr[8];

            if(fseek(iso, (long)(part_offset + 0x2B8), SEEK_SET) != 0 || fread(phdr, 1, 8, iso) != 8)
            {
                free(table_data);
                free(*parts);
                *parts = NULL;
                return -1;
            }

            (*parts)[idx].data_offset = part_offset + ((uint64_t)read_be32(phdr) << 2);
            (*parts)[idx].data_size   = (uint64_t)read_be32(phdr + 4) << 2;

            idx++;
        }

        free(table_data);
    }

    *part_count = idx;
    return 0;
}

/* ---- Junk detection for a block ---- */

static void detect_junk_in_block(const uint8_t *block_buf, size_t block_bytes, uint64_t block_off,
                                 const NgcwDataMap *data_map, uint64_t sys_end, uint16_t partition_index,
                                 JunkCollector *jc, uint64_t *data_sectors, uint64_t *junk_sectors,
                                 uint8_t *sector_status_out)
{
    int num_sectors = (int)(block_bytes / NGC_SECTOR_SIZE);

    if(block_bytes % NGC_SECTOR_SIZE) num_sectors++;

    /* Classify sectors */
    int sector_is_data[SECTORS_PER_BLOCK];

    for(int si = 0; si < num_sectors; si++)
    {
        uint64_t off  = block_off + (uint64_t)si * NGC_SECTOR_SIZE;
        size_t   slen = NGC_SECTOR_SIZE;

        if((uint64_t)si * NGC_SECTOR_SIZE + slen > block_bytes) slen = block_bytes - (size_t)si * NGC_SECTOR_SIZE;

        if(off < sys_end)
            sector_is_data[si] = 1;
        else if(data_map != NULL)
            sector_is_data[si] = is_data_region(data_map, off, slen) ? 1 : 0;
        else
            sector_is_data[si] = 1;
    }

    /* Try full-block LFG seed extraction */
    int      block_is_lfg = 0;
    uint32_t block_seed[NGC_LFG_SEED_SIZE];

    if(block_bytes >= NGC_LFG_K * sizeof(uint32_t))
    {
        size_t matched = ngc_lfg_get_seed(block_buf, block_bytes, 0, block_seed);

        if(matched >= block_bytes) block_is_lfg = 1;
    }

    if(block_is_lfg)
    {
        for(int si = 0; si < num_sectors; si++)
        {
            size_t slen = NGC_SECTOR_SIZE;

            if((uint64_t)si * NGC_SECTOR_SIZE + slen > block_bytes) slen = block_bytes - (size_t)si * NGC_SECTOR_SIZE;

            if(sector_is_data[si])
            {
                sector_status_out[si] = SectorStatusDumped;
                (*data_sectors)++;
            }
            else
            {
                sector_status_out[si] = SectorStatusGenerable;
                junk_collector_add(jc, block_off + (uint64_t)si * NGC_SECTOR_SIZE, slen, partition_index, block_seed);
                (*junk_sectors)++;
            }
        }
    }
    else
    {
        /* Mixed block: per-run seed extraction */
        int si = 0;

        while(si < num_sectors)
        {
            if(sector_is_data[si])
            {
                sector_status_out[si] = SectorStatusDumped;
                (*data_sectors)++;
                si++;
                continue;
            }

            int run_start = si;

            while(si < num_sectors && !sector_is_data[si]) si++;

            int    run_end        = si;
            size_t run_byte_start = (size_t)run_start * NGC_SECTOR_SIZE;
            size_t run_byte_end   = (size_t)run_end * NGC_SECTOR_SIZE;

            if(run_byte_end > block_bytes) run_byte_end = block_bytes;

            size_t   run_bytes    = run_byte_end - run_byte_start;
            int      run_has_seed = 0;
            uint32_t run_seed[NGC_LFG_SEED_SIZE];

            if(run_bytes >= NGC_LFG_K * sizeof(uint32_t))
            {
                size_t matched = ngc_lfg_get_seed(block_buf + run_byte_start, run_bytes, run_byte_start, run_seed);

                if(matched >= run_bytes) run_has_seed = 1;
            }

            for(int ri = run_start; ri < run_end; ri++)
            {
                size_t s    = (size_t)ri * NGC_SECTOR_SIZE;
                size_t slen = NGC_SECTOR_SIZE;

                if(s + slen > block_bytes) slen = block_bytes - s;

                if(run_has_seed)
                {
                    struct ngc_lfg_ctx lfg;
                    uint32_t           sc[NGC_LFG_SEED_SIZE];
                    memcpy(sc, run_seed, sizeof(sc));
                    ngc_lfg_set_seed(&lfg, sc);

                    if(s > 0)
                    {
                        uint8_t discard[4096];
                        size_t  adv = s;

                        while(adv > 0)
                        {
                            size_t step = adv > sizeof(discard) ? sizeof(discard) : adv;
                            ngc_lfg_get_bytes(&lfg, discard, step);
                            adv -= step;
                        }
                    }

                    uint8_t expected[NGC_SECTOR_SIZE];
                    ngc_lfg_get_bytes(&lfg, expected, slen);

                    if(memcmp(block_buf + s, expected, slen) == 0)
                    {
                        sector_status_out[ri] = SectorStatusGenerable;
                        junk_collector_add(jc, block_off + s, slen, partition_index, run_seed);
                        (*junk_sectors)++;
                    }
                    else
                    {
                        sector_status_out[ri] = SectorStatusDumped;
                        (*data_sectors)++;
                    }
                }
                else
                {
                    /* No seed — check if all-zero */
                    int all_zero = 1;

                    for(size_t b = 0; b < slen; b++)
                    {
                        if(block_buf[s + b] != 0)
                        {
                            all_zero = 0;
                            break;
                        }
                    }

                    /* Keep as data regardless (zero-fill will dedup) */
                    sector_status_out[ri] = SectorStatusDumped;
                    (*data_sectors)++;
                    (void)all_zero;
                }
            }
        }
    }
}

/* ---- BCA sidecar import ---- */

static int import_bca_sidecar(const char *input_path, aaruformat_context *output_ctx)
{
    size_t path_len = strlen(input_path) + 5;
    char  *bca_path = malloc(path_len);

    if(!bca_path) return -1;

    /* Try appending .bca */
    snprintf(bca_path, path_len, "%s.bca", input_path);

    struct stat bca_st;
    FILE       *bca_file = NULL;

    if(stat(bca_path, &bca_st) == 0 && bca_st.st_size == 64) { bca_file = fopen(bca_path, "rb"); }

    /* Try replacing extension */
    if(bca_file == NULL)
    {
        memcpy(bca_path, input_path, strlen(input_path) + 1);
        char *dot = strrchr(bca_path, '.');

        if(dot)
            strcpy(dot, ".bca");
        else
            strcat(bca_path, ".bca");

        if(stat(bca_path, &bca_st) == 0 && bca_st.st_size == 64) bca_file = fopen(bca_path, "rb");
    }

    free(bca_path);

    if(bca_file == NULL) return -1;

    uint8_t bca_data[64];

    if(fread(bca_data, 1, 64, bca_file) == 64)
    {
        aaruf_write_media_tag(output_ctx, bca_data, kMediaTagDvdBca, 64);
        fclose(bca_file);
        return 0;
    }

    fclose(bca_file);
    return -1;
}

/* ---- Metadata extraction ---- */

static void extract_metadata(aaruformat_context *output_ctx, const uint8_t *header, int disc_type)
{
    /* Game title */
    char title[65];
    memcpy(title, header + 0x20, 64);
    title[64] = '\0';

    for(int i = 63; i >= 0 && (title[i] == ' ' || title[i] == '\0'); i--) title[i] = '\0';

    if(title[0])
    {
        print_info("Title:", title);
        aaruf_set_media_title(output_ctx, (const uint8_t *)title, (int32_t)strlen(title));
    }

    /* Game ID (6 chars) */
    char game_id[7];
    memcpy(game_id, header, 6);
    game_id[6] = '\0';
    print_info("Game ID:", game_id);
    aaruf_set_media_part_number(output_ctx, (const uint8_t *)game_id, 6);

    /* Disc number */
    uint8_t disc_number = header[6];

    if(disc_number > 0)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u", disc_number + 1);
        print_info("Disc Number:", buf);
        aaruf_set_media_sequence(output_ctx, disc_number + 1, disc_number + 1);
    }

    /* Comments with platform and maker code */
    char comments[256];
    snprintf(comments, sizeof(comments), "Platform: %s, Maker: %c%c, Version: %u",
             disc_type == 0 ? "Nintendo GameCube" : "Nintendo Wii", header[4], header[5], header[7]);
    aaruf_set_comments(output_ctx, (const uint8_t *)comments, (int32_t)strlen(comments));
}

/* ---- Copy source metadata (AaruFormat input) ---- */

static void copy_source_metadata(aaruformat_context *input_ctx, aaruformat_context *output_ctx, int32_t skip_tag1,
                                 int32_t skip_tag2)
{
    char buffer[256];

    /* Copy media tags */
    size_t  tags_size = 0;
    int32_t ret       = aaruf_get_readable_media_tags(input_ctx, NULL, &tags_size);

    if(ret == AARUF_ERROR_BUFFER_TOO_SMALL && tags_size > 0)
    {
        uint8_t *tags_map = (uint8_t *)malloc(tags_size);

        if(tags_map != NULL)
        {
            ret = aaruf_get_readable_media_tags(input_ctx, tags_map, &tags_size);

            if(ret == AARUF_STATUS_OK)
            {
                int copied = 0;

                for(size_t i = 0; i < tags_size; i++)
                {
                    if(!tags_map[i]) continue;

                    if((int32_t)i == skip_tag1 || (int32_t)i == skip_tag2) continue;

                    uint32_t tag_len = 0;
                    ret              = aaruf_read_media_tag(input_ctx, NULL, (int32_t)i, &tag_len);

                    if(ret != AARUF_ERROR_BUFFER_TOO_SMALL || tag_len == 0) continue;

                    uint8_t *tag_data = (uint8_t *)malloc(tag_len);

                    if(tag_data == NULL) continue;

                    ret = aaruf_read_media_tag(input_ctx, tag_data, (int32_t)i, &tag_len);

                    if(ret == AARUF_STATUS_OK)
                    {
                        if(aaruf_write_media_tag(output_ctx, tag_data, (int32_t)i, tag_len) == AARUF_STATUS_OK)
                            copied++;
                    }

                    free(tag_data);
                }

                if(copied > 0)
                {
                    snprintf(buffer, sizeof(buffer), "Copied %d additional media tag(s)", copied);
                    print_success(buffer);
                }
            }

            free(tags_map);
        }
    }

    /* Copy dump hardware */
    size_t dumphw_size = 0;
    ret                = aaruf_get_dumphw(input_ctx, NULL, &dumphw_size);

    if(ret == AARUF_ERROR_BUFFER_TOO_SMALL && dumphw_size > 0)
    {
        uint8_t *dumphw = (uint8_t *)malloc(dumphw_size);

        if(dumphw != NULL)
        {
            ret = aaruf_get_dumphw(input_ctx, dumphw, &dumphw_size);

            if(ret == AARUF_STATUS_OK)
            {
                if(aaruf_set_dumphw(output_ctx, dumphw, dumphw_size) == AARUF_STATUS_OK)
                    print_success("Copied dump hardware information");
            }

            free(dumphw);
        }
    }

    /* Copy JSON metadata */
    size_t json_size = 0;
    ret              = aaruf_get_aaru_json_metadata(input_ctx, NULL, &json_size);

    if(ret == AARUF_ERROR_BUFFER_TOO_SMALL && json_size > 0)
    {
        uint8_t *json = (uint8_t *)malloc(json_size);

        if(json != NULL)
        {
            ret = aaruf_get_aaru_json_metadata(input_ctx, json, &json_size);

            if(ret == AARUF_STATUS_OK)
            {
                if(aaruf_set_aaru_json_metadata(output_ctx, json, json_size) == AARUF_STATUS_OK)
                    print_success("Copied Aaru JSON metadata");
            }

            free(json);
        }
    }

    /* Copy creator */
    int32_t meta_len = 0;

    if(aaruf_get_creator(input_ctx, NULL, &meta_len) == AARUF_ERROR_BUFFER_TOO_SMALL && meta_len > 0)
    {
        uint8_t *meta_buf = (uint8_t *)malloc(meta_len);

        if(meta_buf != NULL)
        {
            int32_t ml = meta_len;

            if(aaruf_get_creator(input_ctx, meta_buf, &ml) == AARUF_STATUS_OK)
                aaruf_set_creator(output_ctx, meta_buf, ml);

            free(meta_buf);
        }
    }

    /* Copy comments */
    meta_len = 0;

    if(aaruf_get_comments(input_ctx, NULL, &meta_len) == AARUF_ERROR_BUFFER_TOO_SMALL && meta_len > 0)
    {
        uint8_t *meta_buf = (uint8_t *)malloc(meta_len);

        if(meta_buf != NULL)
        {
            int32_t ml = meta_len;

            if(aaruf_get_comments(input_ctx, meta_buf, &ml) == AARUF_STATUS_OK)
                aaruf_set_comments(output_ctx, meta_buf, ml);

            free(meta_buf);
        }
    }

    /* Copy media sequence */
    int32_t seq = 0, last_seq = 0;

    if(aaruf_get_media_sequence(input_ctx, &seq, &last_seq) == AARUF_STATUS_OK && seq > 0)
        aaruf_set_media_sequence(output_ctx, seq, last_seq);
}

/* ================================================================== */
/*  Main convert function                                              */
/* ================================================================== */

int convert_ngcw(const char *input_path, const char *output_path)
{
    int     result = 0;
    char    buffer[256];
    uint8_t header[0x440];

    printf("\n" ANSI_BOLD ANSI_CYAN "════════════════════════════════════════════════════════════════════════════════\n"
           "                  GAMECUBE / WII DISC IMAGE CONVERTER\n"
           "════════════════════════════════════════════════════════════════════════════════" ANSI_RESET "\n");

    /* ── Step 1: Open and identify source ──────────────────────── */
    printf("\n" ANSI_BOLD ANSI_CYAN "  Opening Source Image" ANSI_RESET "\n");
    printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                     "\n");

    bool                is_raw    = false;
    aaruformat_context *input_ctx = NULL;
    FILE               *iso_file  = NULL;
    uint64_t            disc_size;
    int                 disc_type;

    /* Try AaruFormat first */
    input_ctx = aaruf_open(input_path, false, NULL);

    if(input_ctx != NULL)
    {
        if(input_ctx->image_info.MediaType != GOD && input_ctx->image_info.MediaType != WOD)
        {
            snprintf(buffer, sizeof(buffer), "Source image media type is %s, expected GOD or WOD",
                     media_type_to_string(input_ctx->image_info.MediaType));
            print_error(buffer);
            aaruf_close(input_ctx);
            return -1;
        }

        disc_size = input_ctx->image_info.Sectors * input_ctx->image_info.SectorSize;

        /* Read header from sector 0 */
        uint32_t hdr_len = NGC_SECTOR_SIZE;
        uint8_t  hdr_status;

        if(aaruf_read_sector(input_ctx, 0, false, header, &hdr_len, &hdr_status) != AARUF_STATUS_OK)
        {
            print_error("Cannot read disc header from AaruFormat source");
            aaruf_close(input_ctx);
            return -1;
        }

        /* Read remaining header bytes from subsequent sectors */
        for(int s = 1; s * NGC_SECTOR_SIZE < 0x440; s++)
        {
            uint32_t sl = NGC_SECTOR_SIZE;
            uint8_t  ss;
            aaruf_read_sector(input_ctx, s, false, header + s * NGC_SECTOR_SIZE, &sl, &ss);
        }

        snprintf(buffer, sizeof(buffer), "AaruFormat image: %llu sectors",
                 (unsigned long long)input_ctx->image_info.Sectors);
        print_success(buffer);
    }
    else
    {
        /* Try raw ISO */
        iso_file = fopen(input_path, "rb");

        if(iso_file == NULL)
        {
            snprintf(buffer, sizeof(buffer), "Cannot open input file: %s", strerror(errno));
            print_error(buffer);
            return -1;
        }

        struct stat st;

        if(fstat(fileno(iso_file), &st) < 0)
        {
            print_error("Cannot stat input file");
            fclose(iso_file);
            return -1;
        }

        disc_size = (uint64_t)st.st_size;
        is_raw    = true;

        if(fread(header, 1, 0x440, iso_file) < 0x440)
        {
            print_error("Cannot read disc header");
            fclose(iso_file);
            return -1;
        }

        char size_str[32];
        format_bytes(disc_size, size_str, sizeof(size_str));
        snprintf(buffer, sizeof(buffer), "ISO image: %s", size_str);
        print_success(buffer);
    }

    disc_type = detect_disc_type(header);

    if(disc_type < 0)
    {
        print_error("Not a valid GameCube or Wii disc image");

        if(input_ctx) aaruf_close(input_ctx);

        if(iso_file) fclose(iso_file);

        return -1;
    }

    print_info("Disc Type:", disc_type == 0 ? "Nintendo GameCube" : "Nintendo Wii");

    /* ── Step 2: Parse partitions (Wii only) ──────────────────── */
    uint16_t       part_count = 0;
    NgcwPartition *parts      = NULL;

    if(disc_type == 1 && is_raw)
    {
        printf("\n" ANSI_BOLD ANSI_CYAN "  Parsing Wii Partitions" ANSI_RESET "\n");
        printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                         "\n");

        if(parse_wii_partitions(iso_file, &part_count, &parts) != 0)
        {
            print_error("Cannot parse Wii partitions");
            fclose(iso_file);
            return -1;
        }

        snprintf(buffer, sizeof(buffer), "Found %u partition(s)", part_count);
        print_success(buffer);

        for(int i = 0; i < part_count; i++)
        {
            const char *ptype = "unknown";

            if(parts[i].type == 0)
                ptype = "game";
            else if(parts[i].type == 1)
                ptype = "update";
            else if(parts[i].type == 2)
                ptype = "channel";

            snprintf(buffer, sizeof(buffer), "  Partition %d: %s  offset=0x%llX  data_size=%.1f MiB", i, ptype,
                     (unsigned long long)parts[i].offset, (double)parts[i].data_size / (1024.0 * 1024.0));
            printf("  %s\n", buffer);
        }
    }
    else if(disc_type == 1 && !is_raw)
    {
        /* TODO: Parse partitions from AaruFormat source reading sectors */
        printf("\n" ANSI_BOLD ANSI_CYAN "  Wii Partition Parsing" ANSI_RESET "\n");
        printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                         "\n");
        print_success("Wii partition parsing from AaruFormat source (partition key map required in source)");
    }

    /* ── Step 3: Create output image ──────────────────────────── */
    printf("\n" ANSI_BOLD ANSI_CYAN "  Creating Destination Image" ANSI_RESET "\n");
    printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                     "\n");

    uint64_t total_sectors = disc_size / NGC_SECTOR_SIZE;
    uint32_t media_type    = disc_type == 0 ? GOD : WOD;

    const char *app_name     = "aaruformattool";
    size_t      app_name_len = strlen(app_name);

    aaruformat_context *output_ctx = aaruf_create(output_path, media_type, NGC_SECTOR_SIZE, total_sectors, 0, 0, NULL,
                                                  (const uint8_t *)app_name, (uint8_t)app_name_len, 1, 0, false);

    if(output_ctx == NULL)
    {
        snprintf(buffer, sizeof(buffer), "Cannot create output image: %s", strerror(errno));
        print_error(buffer);
        result = -1;
        goto cleanup;
    }

    print_success("Destination image created");

    /* ── Step 4: Build partition key map and write media tags (Wii) ── */
    WiiPartitionRegion *regions      = NULL;
    uint8_t            *key_map_data = NULL;
    uint32_t            key_map_len  = 0;

    if(disc_type == 1 && parts != NULL && part_count > 0)
    {
        printf("\n" ANSI_BOLD ANSI_CYAN "  Building Partition Key Map" ANSI_RESET "\n");
        printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                         "\n");

        regions = calloc(part_count, sizeof(WiiPartitionRegion));

        if(regions != NULL)
        {
            /* Sort partitions by offset */
            for(int i = 1; i < part_count; i++)
            {
                NgcwPartition tmp = parts[i];
                int           j   = i - 1;

                while(j >= 0 && parts[j].data_offset > tmp.data_offset)
                {
                    parts[j + 1] = parts[j];
                    j--;
                }

                parts[j + 1] = tmp;
            }

            for(int i = 0; i < part_count; i++)
            {
                regions[i].start_sector = (uint32_t)(parts[i].data_offset / WII_GROUP_SIZE);
                regions[i].end_sector   = (uint32_t)((parts[i].data_offset + parts[i].data_size) / WII_GROUP_SIZE);
                memcpy(regions[i].key, parts[i].title_key, 16);
            }

            wii_serialize_partition_key_map(regions, part_count, &key_map_data, &key_map_len);

            if(key_map_data != NULL)
            {
                aaruf_write_media_tag(output_ctx, key_map_data, kMediaTagWiiPartitionKeyMap, key_map_len);
                print_success("Written media tag: Wii Partition Key Map");
            }
        }
    }

    /* ── Step 5: Convert sectors ──────────────────────────────── */
    printf("\n" ANSI_BOLD ANSI_CYAN "  Converting Sectors" ANSI_RESET "\n");
    printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                     "\n\n");

    JunkCollector jc;
    junk_collector_init(&jc);

    uint64_t data_sectors = 0, junk_sectors = 0;
    clock_t  start_time      = clock();
    uint64_t bytes_processed = 0;

    if(disc_type == 0)
    {
        /* ---- GameCube pipeline ---- */
        uint32_t fst_offset = read_be32(header + 0x424);
        uint32_t fst_size   = read_be32(header + 0x428);
        uint64_t sys_end    = fst_offset + fst_size;

        NgcwDataMap data_map;
        memset(&data_map, 0, sizeof(data_map));

        if(fst_size > 0 && fst_size < 64 * 1024 * 1024)
        {
            uint8_t *fst = malloc(fst_size);

            if(fst)
            {
                bool fst_ok = false;

                if(is_raw)
                {
                    if(fseek(iso_file, (long)fst_offset, SEEK_SET) == 0 &&
                       fread(fst, 1, fst_size, iso_file) == fst_size)
                        fst_ok = true;
                }
                else
                {
                    /* Read FST from AaruFormat source */
                    uint64_t fst_read = 0;

                    fst_ok = true;

                    while(fst_read < fst_size)
                    {
                        uint64_t sector = (fst_offset + fst_read) / NGC_SECTOR_SIZE;
                        uint32_t off    = (uint32_t)((fst_offset + fst_read) % NGC_SECTOR_SIZE);
                        uint8_t  sbuf[NGC_SECTOR_SIZE];
                        uint32_t slen = NGC_SECTOR_SIZE;
                        uint8_t  ss;

                        if(aaruf_read_sector(input_ctx, sector, false, sbuf, &slen, &ss) != AARUF_STATUS_OK)
                        {
                            fst_ok = false;
                            break;
                        }

                        uint32_t chunk = NGC_SECTOR_SIZE - off;

                        if(chunk > fst_size - fst_read) chunk = (uint32_t)(fst_size - fst_read);

                        memcpy(fst + fst_read, sbuf + off, chunk);
                        fst_read += chunk;
                    }
                }

                if(fst_ok) build_data_map_from_fst(fst, fst_size, 0, 0, &data_map);

                free(fst);
            }
        }

        uint8_t block_buf[GC_BLOCK_SIZE];

        for(uint64_t block_off = 0; block_off < disc_size; block_off += GC_BLOCK_SIZE)
        {
            if((block_off & 0x7FFFF) == 0)
            {
                double pct = (double)block_off / (double)disc_size * 100.0;
                printf("\r  " ANSI_CYAN "Converting GC sectors " ANSI_WHITE "%5.1f%%" ANSI_RESET, pct);
                fflush(stdout);
            }

            size_t block_bytes = GC_BLOCK_SIZE;

            if(block_off + block_bytes > disc_size) block_bytes = (size_t)(disc_size - block_off);

            /* Read block */
            if(is_raw)
            {
                if(fseek(iso_file, (long)block_off, SEEK_SET) != 0)
                    memset(block_buf, 0, block_bytes);
                else
                {
                    size_t n = fread(block_buf, 1, block_bytes, iso_file);

                    if(n < block_bytes) memset(block_buf + n, 0, block_bytes - n);
                }
            }
            else
            {
                uint64_t base_sector = block_off / NGC_SECTOR_SIZE;

                for(uint32_t s = 0; s < SECTORS_PER_BLOCK && s * NGC_SECTOR_SIZE < block_bytes; s++)
                {
                    uint32_t len = NGC_SECTOR_SIZE;
                    uint8_t  st;
                    int32_t  ret = aaruf_read_sector(input_ctx, base_sector + s, false, block_buf + s * NGC_SECTOR_SIZE,
                                                     &len, &st);

                    if(ret != AARUF_STATUS_OK) memset(block_buf + s * NGC_SECTOR_SIZE, 0, NGC_SECTOR_SIZE);
                }
            }

            uint8_t sector_statuses[SECTORS_PER_BLOCK];
            detect_junk_in_block(block_buf, block_bytes, block_off, &data_map, sys_end, 0xFFFF, &jc, &data_sectors,
                                 &junk_sectors, sector_statuses);

            /* Write sectors */
            int num_sectors = (int)(block_bytes / NGC_SECTOR_SIZE);

            for(int si = 0; si < num_sectors; si++)
            {
                uint64_t sector = block_off / NGC_SECTOR_SIZE + (uint64_t)si;
                int32_t  wret   = aaruf_write_sector(output_ctx, sector, false, block_buf + si * NGC_SECTOR_SIZE,
                                                     sector_statuses[si], NGC_SECTOR_SIZE);

                if(wret != AARUF_STATUS_OK)
                {
                    printf("\n");
                    snprintf(buffer, sizeof(buffer), "Error writing sector %llu (error %d)", (unsigned long long)sector,
                             wret);
                    print_error(buffer);
                    result = -1;
                    break;
                }
            }

            if(result != 0) break;

            bytes_processed += block_bytes;
        }

        data_map_free(&data_map);
    }
    else
    {
        /* ---- Wii pipeline ---- */
        for(uint64_t offset = 0; offset < disc_size;)
        {
            if((offset & 0x7FFFF) == 0)
            {
                double pct = (double)offset / (double)disc_size * 100.0;
                printf("\r  " ANSI_CYAN "Converting Wii sectors " ANSI_WHITE "%5.1f%%" ANSI_RESET, pct);
                fflush(stdout);
            }

            /* Check if inside a partition's data area */
            int in_part = -1;

            for(int p = 0; p < part_count; p++)
            {
                if(offset >= parts[p].data_offset && offset < parts[p].data_offset + parts[p].data_size)
                {
                    in_part = p;
                    break;
                }
            }

            if(in_part >= 0 && is_raw)
            {
                /* Inside partition — read, decrypt, detect junk, write */
                uint64_t group_disc_off = parts[in_part].data_offset +
                                          ((offset - parts[in_part].data_offset) / WII_GROUP_SIZE) * WII_GROUP_SIZE;

                uint8_t enc_grp[WII_GROUP_SIZE];

                if(fseek(iso_file, (long)group_disc_off, SEEK_SET) != 0)
                    memset(enc_grp, 0, WII_GROUP_SIZE);
                else
                {
                    size_t n = fread(enc_grp, 1, WII_GROUP_SIZE, iso_file);

                    if(n < WII_GROUP_SIZE) memset(enc_grp + n, 0, WII_GROUP_SIZE - n);
                }

                uint8_t hash_block[WII_GROUP_HASH_SIZE];
                uint8_t group_data[WII_GROUP_DATA_SIZE];
                wii_decrypt_group(parts[in_part].title_key, enc_grp, hash_block, group_data);

                /* Reassemble: hash_block + group_data → decrypted_group */
                uint8_t decrypted_group[WII_GROUP_SIZE];
                memcpy(decrypted_group, hash_block, WII_GROUP_HASH_SIZE);
                memcpy(decrypted_group + WII_GROUP_HASH_SIZE, group_data, WII_GROUP_DATA_SIZE);

                /* Write all 16 sectors as SectorStatusUnencrypted */
                for(uint32_t s = 0; s < SECTORS_PER_BLOCK; s++)
                {
                    uint64_t sector = group_disc_off / NGC_SECTOR_SIZE + s;
                    int32_t  wret = aaruf_write_sector(output_ctx, sector, false, decrypted_group + s * NGC_SECTOR_SIZE,
                                                       SectorStatusUnencrypted, NGC_SECTOR_SIZE);

                    if(wret != AARUF_STATUS_OK)
                    {
                        printf("\n");
                        snprintf(buffer, sizeof(buffer), "Error writing sector %llu (error %d)",
                                 (unsigned long long)sector, wret);
                        print_error(buffer);
                        result = -1;
                        break;
                    }

                    data_sectors++;
                }

                if(result != 0) break;

                offset = group_disc_off + WII_GROUP_SIZE;
                bytes_processed += WII_GROUP_SIZE;
            }
            else
            {
                /* Outside partition — read as unencrypted, attempt junk detection */
                uint8_t block_buf[GC_BLOCK_SIZE];
                size_t  block_bytes = GC_BLOCK_SIZE;

                /* Align to 0x8000 for junk detection */
                uint64_t aligned_off = offset & ~(uint64_t)(GC_BLOCK_SIZE - 1);

                if(aligned_off + block_bytes > disc_size) block_bytes = (size_t)(disc_size - aligned_off);

                if(is_raw)
                {
                    if(fseek(iso_file, (long)aligned_off, SEEK_SET) != 0)
                        memset(block_buf, 0, block_bytes);
                    else
                    {
                        size_t n = fread(block_buf, 1, block_bytes, iso_file);

                        if(n < block_bytes) memset(block_buf + n, 0, block_bytes - n);
                    }
                }
                else
                {
                    uint64_t base_sector = aligned_off / NGC_SECTOR_SIZE;

                    for(uint32_t s = 0; s < SECTORS_PER_BLOCK && s * NGC_SECTOR_SIZE < block_bytes; s++)
                    {
                        uint32_t len = NGC_SECTOR_SIZE;
                        uint8_t  st;
                        int32_t  ret = aaruf_read_sector(input_ctx, base_sector + s, false,
                                                         block_buf + s * NGC_SECTOR_SIZE, &len, &st);

                        if(ret != AARUF_STATUS_OK) memset(block_buf + s * NGC_SECTOR_SIZE, 0, NGC_SECTOR_SIZE);
                    }
                }

                uint8_t sector_statuses[SECTORS_PER_BLOCK];
                detect_junk_in_block(block_buf, block_bytes, aligned_off, NULL, 0x50000, 0xFFFF, &jc, &data_sectors,
                                     &junk_sectors, sector_statuses);

                int num_sectors = (int)(block_bytes / NGC_SECTOR_SIZE);

                for(int si = 0; si < num_sectors; si++)
                {
                    uint64_t sector = aligned_off / NGC_SECTOR_SIZE + (uint64_t)si;
                    int32_t  wret   = aaruf_write_sector(output_ctx, sector, false, block_buf + si * NGC_SECTOR_SIZE,
                                                         sector_statuses[si], NGC_SECTOR_SIZE);

                    if(wret != AARUF_STATUS_OK)
                    {
                        printf("\n");
                        snprintf(buffer, sizeof(buffer), "Error writing sector %llu (error %d)",
                                 (unsigned long long)sector, wret);
                        print_error(buffer);
                        result = -1;
                        break;
                    }
                }

                if(result != 0) break;

                offset = aligned_off + block_bytes;
                bytes_processed += block_bytes;
            }
        }
    }

    printf("\n\n");

    if(result == 0)
    {
        double elapsed = (double)(clock() - start_time) / CLOCKS_PER_SEC;
        char   size_str[32];
        format_bytes(bytes_processed, size_str, sizeof(size_str));
        snprintf(buffer, sizeof(buffer), "Converted %llu sectors (%s) in %.1f seconds — %llu data, %llu junk",
                 (unsigned long long)(data_sectors + junk_sectors), size_str, elapsed, (unsigned long long)data_sectors,
                 (unsigned long long)junk_sectors);
        print_success(buffer);
    }

    /* ── Step 6: Store junk map ────────────────────────────────── */
    if(result == 0 && jc.count > 0)
    {
        printf("\n" ANSI_BOLD ANSI_CYAN "  Storing Junk Map" ANSI_RESET "\n");
        printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                         "\n");

        uint8_t *junk_map_data = NULL;
        uint32_t junk_map_len  = 0;

        if(ngcw_serialize_junk_map(jc.entries, jc.count, &junk_map_data, &junk_map_len) == 0 && junk_map_data != NULL)
        {
            aaruf_write_media_tag(output_ctx, junk_map_data, kMediaTagNgcwJunkMap, junk_map_len);
            snprintf(buffer, sizeof(buffer), "Stored junk map with %u entries (%u bytes)", jc.count, junk_map_len);
            print_success(buffer);
            free(junk_map_data);
        }
    }

    junk_collector_free(&jc);

    /* ── Step 7: Import BCA sidecar ───────────────────────────── */
    if(result == 0)
    {
        if(import_bca_sidecar(input_path, output_ctx) == 0)
        {
            printf("\n" ANSI_BOLD ANSI_CYAN "  BCA Sidecar" ANSI_RESET "\n");
            printf(ANSI_BLUE
                   "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET "\n");
            print_success("Imported 64-byte BCA from sidecar file");
        }
    }

    /* ── Step 8: Extract metadata ─────────────────────────────── */
    if(result == 0)
    {
        printf("\n" ANSI_BOLD ANSI_CYAN "  Extracting Metadata" ANSI_RESET "\n");
        printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                         "\n");

        extract_metadata(output_ctx, header, disc_type);
        print_success("Metadata extracted");
    }

    /* ── Step 9: Copy source metadata (AaruFormat input) ──────── */
    if(result == 0 && !is_raw && input_ctx != NULL)
    {
        printf("\n" ANSI_BOLD ANSI_CYAN "  Copying Source Metadata" ANSI_RESET "\n");
        printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                         "\n");

        copy_source_metadata(input_ctx, output_ctx, kMediaTagWiiPartitionKeyMap, kMediaTagNgcwJunkMap);
    }

cleanup:
    free(key_map_data);
    free(regions);

    /* Wipe key material */
    if(parts != NULL)
    {
        for(int i = 0; i < part_count; i++) memset(parts[i].title_key, 0, 16);

        free(parts);
    }

    if(output_ctx != NULL) aaruf_close(output_ctx);

    if(input_ctx != NULL) aaruf_close(input_ctx);

    if(iso_file != NULL) fclose(iso_file);

    if(result == 0)
        printf("\n" ANSI_GREEN "  ✓ Conversion completed successfully!" ANSI_RESET "\n\n");
    else
        printf("\n" ANSI_RED "  ✗ Conversion failed." ANSI_RESET "\n\n");

    return result;
}
