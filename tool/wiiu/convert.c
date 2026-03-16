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
 * convert-wiiu command: converts WUD, WUX, or AaruFormat Wii U disc images
 * to AaruFormat with decrypted sector storage and proper media tags/metadata.
 */

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <aaruformat.h>

#include "../aaruformattool.h"
#include "reader.h"

#include "../../src/lib/aes128.h"
#include "../../src/wiiu/wiiu_crypto.h"

/* ANSI color codes */
#define ANSI_RESET  "\033[0m"
#define ANSI_BOLD   "\033[1m"
#define ANSI_RED    "\033[31m"
#define ANSI_GREEN  "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_CYAN   "\033[36m"
#define ANSI_WHITE  "\033[37m"
#define ANSI_BLUE   "\033[34m"

#define PROGRESS_BAR_WIDTH       40
#define WIIU_LOGICAL_SECTOR_SIZE 2048

/* Wii U disc constants */
#define WIIU_ENCRYPTED_OFFSET 0x18000U
#define WIIU_TOC_SIGNATURE    0xCCA6E67BU
#define WIIU_TOC_ENTRIES_OFF  0x800U
#define WIIU_TOC_ENTRY_SIZE   0x80U

/* Wii U common key (publicly known constant) */
static const uint8_t WIIU_COMMON_KEY[16] = {0xD7, 0xB0, 0x04, 0x02, 0x65, 0x9B, 0xA2, 0xAB,
                                            0xD2, 0xCB, 0x0D, 0xB2, 0x7F, 0xA2, 0xB6, 0x56};

/* Partition info (parsed from TOC) */
typedef struct
{
    char     identifier[26];
    char     name[128];
    uint32_t start_sector; /* Physical sector (0x8000 units) */
    uint8_t  key[16];
    int      has_title_key;
} WiiuPartition;

static void print_error_wiiu(const char *msg) { fprintf(stderr, ANSI_RED "  ✗ %s" ANSI_RESET "\n", msg); }

static void print_success_wiiu(const char *msg) { printf(ANSI_GREEN "  ✓ %s" ANSI_RESET "\n", msg); }

static void print_info_wiiu(const char *label, const char *value)
{ printf("  " ANSI_YELLOW "%-20s" ANSI_RESET " %s\n", label, value); }

static void format_bytes_wiiu(uint64_t bytes, char *buffer, size_t buffer_size)
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

static int parse_hex_key_wiiu(const char *hex, uint8_t key[16])
{
    if(strlen(hex) != 32) return -1;

    for(int i = 0; i < 16; i++)
    {
        unsigned int byte;

        if(sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;

        key[i] = (uint8_t)byte;
    }

    return 0;
}

static int try_sidecar_key_file(const char *input_path, const char *suffix, uint8_t *out, size_t expected_len)
{
    size_t path_len = strlen(input_path) + strlen(suffix) + 1;
    char  *path     = (char *)malloc(path_len);

    if(path == NULL) return -1;

    /* First try: append suffix (e.g., "input.wud.disckey") */
    snprintf(path, path_len, "%s%s", input_path, suffix);

    FILE *f = fopen(path, "rb");

    /* Second try: replace extension (e.g., "input.disckey") */
    if(f == NULL)
    {
        const char *dot = strrchr(input_path, '.');
        const char *sep = strrchr(input_path, '/');
#ifdef _WIN32
        const char *sep2 = strrchr(input_path, '\\');

        if(sep2 != NULL && (sep == NULL || sep2 > sep)) sep = sep2;
#endif

        if(dot != NULL && (sep == NULL || dot > sep))
        {
            size_t base_len = (size_t)(dot - input_path);
            size_t new_len  = base_len + strlen(suffix) + 1;
            char  *alt_path = (char *)malloc(new_len);

            if(alt_path != NULL)
            {
                memcpy(alt_path, input_path, base_len);
                memcpy(alt_path + base_len, suffix, strlen(suffix) + 1);
                f = fopen(alt_path, "rb");
                free(alt_path);
            }
        }
    }

    free(path);

    if(f == NULL) return -1;

    size_t n = fread(out, 1, expected_len, f);
    fclose(f);

    return n == expected_len ? 0 : -1;
}

static inline uint32_t read_be32(const uint8_t *p)
{ return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]; }

/* Case-insensitive comparison for filenames. */
static int strnicmp_wiiu(const char *a, const char *b, size_t n)
{
    for(size_t i = 0; i < n; i++)
    {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);

        if(ca != cb) return ca - cb;

        if(ca == 0) return 0;
    }

    return 0;
}

/**
 * @brief Generic disc read callback type.
 *
 * Reads `count` bytes from disc offset `offset` into `buf`.
 * Returns bytes read, or -1 on error.
 */
typedef int64_t (*DiscReadFn)(void *user_data, void *buf, size_t count, uint64_t offset);

/** Disc read callback for WiiuReader (WUD/WUX). */
static int64_t wud_disc_read(void *user_data, void *buf, size_t count, uint64_t offset)
{ return wiiu_reader_read_at((WiiuReader *)user_data, buf, count, offset); }

/** Disc read callback for AaruFormat source (reads logical sectors and assembles). */
static int64_t aaruf_disc_read(void *user_data, void *buf, size_t count, uint64_t offset)
{
    aaruformat_context *ctx  = (aaruformat_context *)user_data;
    uint8_t            *out  = (uint8_t *)buf;
    size_t              done = 0;

    while(done < count)
    {
        uint64_t cur_off        = offset + done;
        uint64_t logical_sector = cur_off / WIIU_LOGICAL_SECTOR_SIZE;
        uint32_t sector_offset  = (uint32_t)(cur_off % WIIU_LOGICAL_SECTOR_SIZE);

        uint8_t  sector_buf[WIIU_LOGICAL_SECTOR_SIZE];
        uint32_t len    = WIIU_LOGICAL_SECTOR_SIZE;
        uint8_t  status = 0;
        int32_t  ret    = aaruf_read_sector(ctx, logical_sector, false, sector_buf, &len, &status);

        if(ret != AARUF_STATUS_OK)
        {
            if(done > 0) break;

            return -1;
        }

        size_t chunk = WIIU_LOGICAL_SECTOR_SIZE - sector_offset;

        if(chunk > count - done) chunk = count - done;

        memcpy(out + done, sector_buf + sector_offset, chunk);
        done += chunk;
    }

    return done > 0 ? (int64_t)done : -1;
}

/**
 * @brief Read and decrypt data from a partition at a given file offset within the partition.
 */
static int read_volume_decrypted(DiscReadFn read_fn, void *read_ctx, const uint8_t key[16],
                                 uint64_t partition_disc_offset, uint64_t file_offset, void *buf, size_t size)
{
    uint8_t *out  = (uint8_t *)buf;
    size_t   done = 0;

    while(done < size)
    {
        uint64_t cur     = file_offset + done;
        uint64_t sec_idx = cur / WIIU_SECTOR_SIZE;
        uint64_t sec_off = cur % WIIU_SECTOR_SIZE;

        uint64_t disc_off = partition_disc_offset + sec_idx * WIIU_SECTOR_SIZE;

        uint8_t enc[WIIU_SECTOR_SIZE];
        int64_t n = read_fn(read_ctx, enc, WIIU_SECTOR_SIZE, disc_off);

        if(n < (int64_t)WIIU_SECTOR_SIZE)
        {
            if(n > 0)
                memset(enc + n, 0, WIIU_SECTOR_SIZE - (size_t)n);
            else
                memset(enc, 0, WIIU_SECTOR_SIZE);
        }

        uint8_t iv[16];
        memset(iv, 0, sizeof(iv));
        aes128_cbc_decrypt(key, iv, enc, WIIU_SECTOR_SIZE);

        size_t chunk = WIIU_SECTOR_SIZE - (size_t)sec_off;

        if(chunk > size - done) chunk = size - done;

        memcpy(out + done, enc + sec_off, chunk);
        done += chunk;
    }

    return 0;
}

/**
 * @brief Parse the TOC from sector 3, decrypt with disc key, extract partition entries.
 */
static int wiiu_parse_toc(DiscReadFn read_fn, void *read_ctx, const uint8_t disc_key[16], WiiuPartition *parts,
                          int *part_count)
{
    uint8_t enc_sector[WIIU_SECTOR_SIZE];
    int64_t n = read_fn(read_ctx, enc_sector, WIIU_SECTOR_SIZE, WIIU_ENCRYPTED_OFFSET);

    if(n < (int64_t)WIIU_SECTOR_SIZE)
    {
        print_error_wiiu("Cannot read TOC sector");
        return -1;
    }

    /* Decrypt with disc key, IV = all zeros */
    uint8_t iv[16];
    memset(iv, 0, sizeof(iv));
    aes128_cbc_decrypt(disc_key, iv, enc_sector, WIIU_SECTOR_SIZE);

    /* Verify TOC signature */
    if(read_be32(enc_sector) != WIIU_TOC_SIGNATURE)
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "TOC decryption failed (signature 0x%08X, expected 0x%08X)", read_be32(enc_sector),
                 WIIU_TOC_SIGNATURE);
        print_error_wiiu(buf);
        print_error_wiiu("Check that the disc key is correct");
        return -1;
    }

    uint32_t toc_part_count = read_be32(enc_sector + 0x1C);

    if(toc_part_count > WIIU_MAX_PARTITIONS) toc_part_count = WIIU_MAX_PARTITIONS;

    *part_count = (int)toc_part_count;

    for(int i = 0; i < (int)toc_part_count; i++)
    {
        const uint8_t *entry = enc_sector + WIIU_TOC_ENTRIES_OFF + (uint32_t)i * WIIU_TOC_ENTRY_SIZE;

        memcpy(parts[i].identifier, entry, 25);
        parts[i].identifier[25] = '\0';

        memcpy(parts[i].name, entry, WIIU_TOC_ENTRY_SIZE);
        parts[i].name[WIIU_TOC_ENTRY_SIZE - 1] = '\0';

        parts[i].start_sector  = read_be32(entry + 0x20);
        parts[i].has_title_key = 0;
        memset(parts[i].key, 0, 16);
    }

    return 0;
}

/**
 * @brief Extract title keys from TITLE.TIK files in SI/GI partitions.
 */
static int wiiu_extract_title_keys(DiscReadFn read_fn, void *read_ctx, const uint8_t disc_key[16], WiiuPartition *parts,
                                   int part_count)
{
    int keys_found = 0;

    for(int p = 0; p < part_count; p++)
    {
        /* Only scan SI and GI partitions */
        if(strncmp(parts[p].identifier, "SI", 2) != 0 && strncmp(parts[p].identifier, "GI", 2) != 0) continue;

        uint64_t part_disc_off = WIIU_ENCRYPTED_OFFSET + (uint64_t)parts[p].start_sector * WIIU_SECTOR_SIZE - 0x10000;

        /* Read FST header */
        uint8_t fst_hdr[WIIU_SECTOR_SIZE];

        if(read_volume_decrypted(read_fn, read_ctx, disc_key, part_disc_off, 0, fst_hdr, WIIU_SECTOR_SIZE) != 0)
            continue;

        if(memcmp(fst_hdr, "FST\0", 4) != 0) continue;

        uint32_t offset_factor = read_be32(fst_hdr + 4);
        uint32_t cluster_count = read_be32(fst_hdr + 8);

        uint64_t *cluster_offsets = (uint64_t *)calloc(cluster_count, sizeof(uint64_t));

        if(cluster_offsets == NULL) continue;

        for(uint32_t c = 0; c < cluster_count; c++)
        {
            uint32_t raw       = read_be32(fst_hdr + 0x20 + c * 0x20);
            uint64_t start     = (uint64_t)raw * WIIU_SECTOR_SIZE;
            cluster_offsets[c] = (start > WIIU_SECTOR_SIZE) ? start - WIIU_SECTOR_SIZE : 0;
        }

        uint64_t entries_offset = (uint64_t)offset_factor * cluster_count + 0x20;

        /* Read root entry to get total entries */
        uint8_t root_entry[0x10];

        if(read_volume_decrypted(read_fn, read_ctx, disc_key, part_disc_off, entries_offset, root_entry, 0x10) != 0)
        {
            free(cluster_offsets);
            continue;
        }

        uint32_t total_entries = read_be32(root_entry + 8);

        if(total_entries > 100000)
        {
            free(cluster_offsets);
            continue;
        }

        uint64_t name_table_offset = entries_offset + (uint64_t)total_entries * 0x10;

        /* Read entries + name table */
        size_t fst_data_size = (size_t)((name_table_offset - entries_offset) + 0x10000);

        if(fst_data_size > 16 * 1024 * 1024)
        {
            free(cluster_offsets);
            continue;
        }

        uint8_t *fst_data = (uint8_t *)malloc(fst_data_size);

        if(fst_data == NULL)
        {
            free(cluster_offsets);
            continue;
        }

        if(read_volume_decrypted(read_fn, read_ctx, disc_key, part_disc_off, entries_offset, fst_data, fst_data_size) !=
           0)
        {
            free(fst_data);
            free(cluster_offsets);
            continue;
        }

        /* Scan for TITLE.TIK files */
        for(uint32_t e = 0; e < total_entries; e++)
        {
            const uint8_t *ent        = fst_data + e * 0x10;
            uint8_t        type       = ent[0];
            uint32_t       name_off   = read_be32(ent) & 0x00FFFFFF;
            uint64_t       file_off   = (uint64_t)read_be32(ent + 4) << 5;
            uint32_t       file_size  = read_be32(ent + 8);
            uint16_t       cluster_id = (uint16_t)((ent[0x0E] << 8) | ent[0x0F]);

            if(type == 1) continue; /* directory */

            if(file_size < 0x200) continue;

            uint64_t fname_off = (uint64_t)name_off;

            if(fname_off >= fst_data_size - (name_table_offset - entries_offset)) continue;

            const char *fname = (const char *)(fst_data + (name_table_offset - entries_offset) + name_off);

            if(strnicmp_wiiu(fname, "title.tik", 9) != 0) continue;

            if(cluster_id >= cluster_count) continue;

            /* Read ticket fields: encrypted title key at +0x1BF, title ID at +0x1DC */
            uint8_t  tik_buf[0x10 + 0x1D + 8];
            uint64_t tik_volume_off = cluster_offsets[cluster_id] + file_off;

            if(read_volume_decrypted(read_fn, read_ctx, disc_key, part_disc_off, tik_volume_off + 0x1BF, tik_buf,
                                     sizeof(tik_buf)) != 0)
                continue;

            uint8_t enc_title_key[16];
            uint8_t title_id[8];
            memcpy(enc_title_key, tik_buf, 16);
            memcpy(title_id, tik_buf + 0x1D, 8);

            /* Decrypt title key: AES-128-CBC with common key, IV = title_id + 8 zero bytes */
            uint8_t dec_title_key[16];
            uint8_t tik_iv[16];
            memset(tik_iv, 0, 16);
            memcpy(tik_iv, title_id, 8);
            memcpy(dec_title_key, enc_title_key, 16);
            aes128_cbc_decrypt(WIIU_COMMON_KEY, tik_iv, dec_title_key, 16);

            /* Build expected GM partition name from title ID */
            char gm_name[19];
            snprintf(gm_name, sizeof(gm_name), "GM%02X%02X%02X%02X%02X%02X%02X%02X", title_id[0], title_id[1],
                     title_id[2], title_id[3], title_id[4], title_id[5], title_id[6], title_id[7]);

            /* Match to a GM partition */
            for(int g = 0; g < part_count; g++)
            {
                if(strncmp(parts[g].identifier, "GM", 2) != 0) continue;

                if(strncmp(parts[g].identifier, gm_name, 18) == 0)
                {
                    memcpy(parts[g].key, dec_title_key, 16);
                    parts[g].has_title_key = 1;
                    keys_found++;

                    char msg[128];
                    snprintf(msg, sizeof(msg), "Title key for %s extracted", gm_name);
                    print_success_wiiu(msg);
                    break;
                }
            }
        }

        free(fst_data);
        free(cluster_offsets);
    }

    /* Set disc key for all partitions that didn't get a title key */
    for(int i = 0; i < part_count; i++)
    {
        if(!parts[i].has_title_key) memcpy(parts[i].key, disc_key, 16);
    }

    return keys_found;
}

int convert_wiiu(const char *input_path, const char *output_path, const char *disc_key_hex)
{
    int     result = 0;
    char    buffer[256];
    uint8_t disc_key[16];
    bool    have_disc_key = false;

    printf("\n" ANSI_BOLD ANSI_CYAN "════════════════════════════════════════════════════════════════════════════════\n"
           "                       WII U DISC IMAGE CONVERTER\n"
           "════════════════════════════════════════════════════════════════════════════════" ANSI_RESET "\n");

    /* ── Step 1: Resolve disc key ────────────────────────────────── */
    printf("\n" ANSI_BOLD ANSI_CYAN "  Key Resolution" ANSI_RESET "\n");
    printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                     "\n");

    /* 1a: --disc-key */
    if(disc_key_hex != NULL)
    {
        if(parse_hex_key_wiiu(disc_key_hex, disc_key) == 0)
        {
            have_disc_key = true;
            print_success_wiiu("Disc key provided via --disc-key");
        }
        else
            print_error_wiiu("Invalid --disc-key format (expected 32 hex chars)");
    }

    /* 1b: Sidecar files */
    if(!have_disc_key)
    {
        if(try_sidecar_key_file(input_path, ".disckey", disc_key, 16) == 0)
        {
            have_disc_key = true;
            print_success_wiiu("Disc key loaded from sidecar .disckey file");
        }
    }

    if(!have_disc_key)
    {
        if(try_sidecar_key_file(input_path, ".key", disc_key, 16) == 0)
        {
            have_disc_key = true;
            print_success_wiiu("Disc key loaded from sidecar .key file");
        }
    }

    /* ── Step 2: Open source image ───────────────────────────────── */
    printf("\n" ANSI_BOLD ANSI_CYAN "  Opening Source Image" ANSI_RESET "\n");
    printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                     "\n");

    bool                is_raw    = false;
    aaruformat_context *input_ctx = NULL;
    WiiuReader          wud_reader;
    bool                wud_open = false;
    uint64_t            total_logical_sectors;
    uint64_t            total_physical_sectors;

    memset(&wud_reader, 0, sizeof(wud_reader));

    /* Try AaruFormat first */
    input_ctx = aaruf_open(input_path, false, NULL);

    if(input_ctx != NULL)
    {
        if(input_ctx->image_info.MediaType != WUOD)
        {
            snprintf(buffer, sizeof(buffer), "Source image media type is %s, expected WUOD",
                     media_type_to_string(input_ctx->image_info.MediaType));
            print_error_wiiu(buffer);
            aaruf_close(input_ctx);
            return -1;
        }

        total_logical_sectors  = input_ctx->image_info.Sectors;
        total_physical_sectors = total_logical_sectors / WIIU_LOGICAL_PER_PHYSICAL;

        snprintf(buffer, sizeof(buffer), "AaruFormat WUOD image: %llu logical sectors",
                 (unsigned long long)total_logical_sectors);
        print_success_wiiu(buffer);

        /* 1c: Try media tags for disc key */
        if(!have_disc_key)
        {
            uint32_t tag_len = 16;

            if(aaruf_read_media_tag(input_ctx, disc_key, kMediaTagWiiUDiscKey, &tag_len) == AARUF_STATUS_OK)
            {
                have_disc_key = true;
                print_success_wiiu("Disc key loaded from source image media tags");
            }
        }
    }
    else
    {
        /* Try WUD/WUX */
        if(wiiu_reader_open(input_path, &wud_reader) != 0)
        {
            snprintf(buffer, sizeof(buffer), "Cannot open input file: %s", strerror(errno));
            print_error_wiiu(buffer);
            return -1;
        }

        wud_open = true;
        is_raw   = true;

        total_physical_sectors = wud_reader.disc_size / WIIU_SECTOR_SIZE;
        total_logical_sectors  = total_physical_sectors * WIIU_LOGICAL_PER_PHYSICAL;

        snprintf(buffer, sizeof(buffer), "%s image: %llu physical sectors (%.2f GiB)",
                 wud_reader.is_wux ? "WUX" : "WUD", (unsigned long long)total_physical_sectors,
                 (double)wud_reader.disc_size / (1024.0 * 1024.0 * 1024.0));
        print_success_wiiu(buffer);
    }

    if(!have_disc_key)
    {
        print_error_wiiu("No disc key found. Provide --disc-key or place a sidecar .disckey / .key file.");

        if(input_ctx != NULL) aaruf_close(input_ctx);

        if(wud_open) wiiu_reader_close(&wud_reader);

        return -1;
    }

    /* ── Step 3: Parse TOC and derive partition keys ─────────────── */
    printf("\n" ANSI_BOLD ANSI_CYAN "  Parsing Partition Table" ANSI_RESET "\n");
    printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                     "\n");

    WiiuPartition parts[WIIU_MAX_PARTITIONS];
    int           part_count = 0;

    /* Set up the generic disc read callback for both source types */
    DiscReadFn read_fn;
    void      *read_ctx;

    if(is_raw)
    {
        read_fn  = wud_disc_read;
        read_ctx = &wud_reader;
    }
    else
    {
        read_fn  = aaruf_disc_read;
        read_ctx = input_ctx;
    }

    if(wiiu_parse_toc(read_fn, read_ctx, disc_key, parts, &part_count) != 0)
    {
        print_error_wiiu("Cannot parse Wii U TOC");

        if(input_ctx != NULL) aaruf_close(input_ctx);

        if(wud_open) wiiu_reader_close(&wud_reader);

        return -1;
    }

    snprintf(buffer, sizeof(buffer), "Found %d partition(s)", part_count);
    print_success_wiiu(buffer);

    for(int i = 0; i < part_count; i++)
    {
        snprintf(buffer, sizeof(buffer), "  Partition %d: %-18s start_sector=%u", i, parts[i].identifier,
                 parts[i].start_sector);
        printf("  %s\n", buffer);
    }

    /* Extract title keys from SI/GI partitions */
    {
        int keys = wiiu_extract_title_keys(read_fn, read_ctx, disc_key, parts, part_count);
        snprintf(buffer, sizeof(buffer), "Extracted %d title key(s)", keys);
        print_success_wiiu(buffer);
    }

    /* ── Step 4: Build partition region map ──────────────────────── */
    printf("\n" ANSI_BOLD ANSI_CYAN "  Building Partition Key Map" ANSI_RESET "\n");
    printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                     "\n");

    WiiuPartitionRegion regions[WIIU_MAX_PARTITIONS];

    /* Sort partitions by start_sector (simple insertion sort) */
    for(int i = 1; i < part_count; i++)
    {
        WiiuPartition tmp = parts[i];
        int           j   = i - 1;

        while(j >= 0 && parts[j].start_sector > tmp.start_sector)
        {
            parts[j + 1] = parts[j];
            j--;
        }

        parts[j + 1] = tmp;
    }

    for(int i = 0; i < part_count; i++)
    {
        regions[i].start_sector = parts[i].start_sector;
        memcpy(regions[i].key, parts[i].key, 16);

        /* End sector: next partition's start, or disc end */
        if(i + 1 < part_count)
            regions[i].end_sector = parts[i + 1].start_sector;
        else
            regions[i].end_sector = (uint32_t)total_physical_sectors;

        snprintf(buffer, sizeof(buffer), "  Region %d: sectors %u–%u (%s)", i, regions[i].start_sector,
                 regions[i].end_sector, parts[i].identifier);
        printf("  %s\n", buffer);
    }

    /* Serialize for media tag */
    uint8_t *key_map_data   = NULL;
    uint32_t key_map_length = 0;
    wiiu_serialize_partition_key_map(regions, (uint32_t)part_count, &key_map_data, &key_map_length);

    /* ── Step 5: Create destination AaruFormat image ─────────────── */
    printf("\n" ANSI_BOLD ANSI_CYAN "  Creating Destination Image" ANSI_RESET "\n");
    printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                     "\n");

    const char *app_name     = "aaruformattool";
    size_t      app_name_len = strlen(app_name);

    aaruformat_context *output_ctx =
        aaruf_create(output_path, WUOD, WIIU_LOGICAL_SECTOR_SIZE, total_logical_sectors, 0, 0, NULL,
                     (const uint8_t *)app_name, (uint8_t)app_name_len, 1, 0, false);

    if(output_ctx == NULL)
    {
        snprintf(buffer, sizeof(buffer), "Cannot create output image: %s", strerror(errno));
        print_error_wiiu(buffer);
        free(key_map_data);

        if(input_ctx != NULL) aaruf_close(input_ctx);

        if(wud_open) wiiu_reader_close(&wud_reader);

        return -1;
    }

    print_success_wiiu("Destination image created");

    /* Write media tags before sectors */
    aaruf_write_media_tag(output_ctx, disc_key, kMediaTagWiiUDiscKey, 16);
    print_success_wiiu("Written media tag: Wii U Disc Key");

    if(key_map_data != NULL)
    {
        aaruf_write_media_tag(output_ctx, key_map_data, kMediaTagWiiUPartitionKeyMap, key_map_length);
        print_success_wiiu("Written media tag: Wii U Partition Key Map");
    }

    /* ── Step 6: Copy sectors ────────────────────────────────────── */
    printf("\n" ANSI_BOLD ANSI_CYAN "  Converting Sectors" ANSI_RESET "\n");
    printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                     "\n\n");

    uint8_t *phys_sector_buf = (uint8_t *)malloc(WIIU_SECTOR_SIZE);

    if(phys_sector_buf == NULL)
    {
        print_error_wiiu("Cannot allocate sector buffer");
        result = -1;
        goto cleanup;
    }

    clock_t  start_time      = clock();
    uint64_t bytes_processed = 0;

    for(uint64_t phys = 0; phys < total_physical_sectors; phys++)
    {
        /* Progress */
        if(phys % 100 == 0 || phys == total_physical_sectors - 1)
        {
            double percentage = (double)(phys + 1) / (double)total_physical_sectors * 100.0;
            double elapsed    = (double)(clock() - start_time) / CLOCKS_PER_SEC;
            double speed      = elapsed > 0 ? (double)bytes_processed / elapsed : 0;

            char speed_str[32];
            format_bytes_wiiu((uint64_t)speed, speed_str, sizeof(speed_str));

            int filled = (int)(percentage / 100.0 * PROGRESS_BAR_WIDTH);

            if(filled > PROGRESS_BAR_WIDTH) filled = PROGRESS_BAR_WIDTH;

            printf("\r  " ANSI_CYAN "[");

            for(int b = 0; b < PROGRESS_BAR_WIDTH; b++)
            {
                if(b < filled)
                    printf(ANSI_GREEN "█");
                else if(b == filled)
                    printf(ANSI_YELLOW "▓");
                else
                    printf(ANSI_WHITE "░");
            }

            printf(ANSI_CYAN "] " ANSI_WHITE "%5.1f%%" ANSI_RESET " │ %s/s   ", percentage, speed_str);
            fflush(stdout);
        }

        /* Read the physical sector from source */
        if(is_raw)
        {
            int64_t n = wiiu_reader_read_at(&wud_reader, phys_sector_buf, WIIU_SECTOR_SIZE, phys * WIIU_SECTOR_SIZE);

            if(n < (int64_t)WIIU_SECTOR_SIZE)
            {
                if(n > 0)
                    memset(phys_sector_buf + n, 0, WIIU_SECTOR_SIZE - (size_t)n);
                else
                    memset(phys_sector_buf, 0, WIIU_SECTOR_SIZE);
            }
        }
        else
        {
            /* Read 16 logical sectors from AaruFormat source */
            uint64_t base_logical = phys * WIIU_LOGICAL_PER_PHYSICAL;

            for(uint32_t s = 0; s < WIIU_LOGICAL_PER_PHYSICAL; s++)
            {
                uint32_t len = WIIU_LOGICAL_SECTOR_SIZE;
                uint8_t  status;
                int32_t  ret = aaruf_read_sector(input_ctx, base_logical + s, false,
                                                 phys_sector_buf + s * WIIU_LOGICAL_SECTOR_SIZE, &len, &status);

                if(ret != AARUF_STATUS_OK)
                {
                    memset(phys_sector_buf + s * WIIU_LOGICAL_SECTOR_SIZE, 0, WIIU_LOGICAL_SECTOR_SIZE);
                }
            }
        }

        /* Determine if this physical sector is encrypted and decrypt if so */
        int     is_plaintext = 0;
        uint8_t write_status;

        if(phys < WIIU_HEADER_PHYSICAL_SECTORS) { is_plaintext = 1; }
        else
        {
            for(int pi = 0; pi < part_count; pi++)
            {
                if(phys == parts[pi].start_sector)
                {
                    is_plaintext = 1;
                    break;
                }
            }
        }

        if(is_plaintext) { write_status = SectorStatusDumped; }
        else
        {
            /* Find which partition this sector belongs to and decrypt */
            const uint8_t *part_key = NULL;

            for(int pi = 0; pi < part_count; pi++)
            {
                if(phys > (uint64_t)regions[pi].start_sector && phys < (uint64_t)regions[pi].end_sector)
                {
                    part_key = regions[pi].key;
                    break;
                }
            }

            if(part_key != NULL)
            {
                wiiu_decrypt_physical_sector(part_key, phys_sector_buf, WIIU_SECTOR_SIZE);
                write_status = SectorStatusUnencrypted;
            }
            else
            {
                /* Outside any partition — store as plaintext */
                write_status = SectorStatusDumped;
            }
        }

        /* Write 16 logical sectors */
        for(uint32_t s = 0; s < WIIU_LOGICAL_PER_PHYSICAL; s++)
        {
            uint64_t logical = phys * WIIU_LOGICAL_PER_PHYSICAL + s;
            int32_t  wret =
                aaruf_write_sector(output_ctx, logical, false, phys_sector_buf + s * WIIU_LOGICAL_SECTOR_SIZE,
                                   write_status, WIIU_LOGICAL_SECTOR_SIZE);

            if(wret != AARUF_STATUS_OK)
            {
                printf("\n");
                snprintf(buffer, sizeof(buffer), "Error writing logical sector %llu (error %d)",
                         (unsigned long long)logical, wret);
                print_error_wiiu(buffer);
                result = -1;
                break;
            }
        }

        if(result != 0) break;

        bytes_processed += WIIU_SECTOR_SIZE;
    }

    printf("\n\n");

    if(result == 0)
    {
        double elapsed = (double)(clock() - start_time) / CLOCKS_PER_SEC;
        char   size_str[32];
        format_bytes_wiiu(bytes_processed, size_str, sizeof(size_str));
        snprintf(buffer, sizeof(buffer), "Converted %llu physical sectors (%s) in %.1f seconds",
                 (unsigned long long)total_physical_sectors, size_str, elapsed);
        print_success_wiiu(buffer);
    }

    /* ── Step 7: Extract metadata ────────────────────────────────── */
    if(result == 0)
    {
        printf("\n" ANSI_BOLD ANSI_CYAN "  Extracting Metadata" ANSI_RESET "\n");
        printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                         "\n");

        /* Read disc header from physical sector 0 (already in first write or can re-read) */
        uint8_t header[WIIU_LOGICAL_SECTOR_SIZE];

        if(is_raw)
        {
            if(wiiu_reader_read_at(&wud_reader, header, WIIU_LOGICAL_SECTOR_SIZE, 0) >=
               (int64_t)WIIU_LOGICAL_SECTOR_SIZE)
            {
                /* Product code: first 10 bytes */
                char product_code[11];
                memcpy(product_code, header, 10);
                product_code[10] = '\0';

                /* Trim trailing spaces/nulls */
                for(int i = 9; i >= 0 && (product_code[i] == '\0' || product_code[i] == ' '); i--)
                    product_code[i] = '\0';

                if(product_code[0] != '\0')
                {
                    print_info_wiiu("Product Code:", product_code);
                    aaruf_set_media_part_number(output_ctx, (const uint8_t *)product_code,
                                                (int32_t)strlen(product_code));
                }

                /* Disc number: byte at offset 0x15 (ASCII digit) */
                if(header[0x15] >= '0' && header[0x15] <= '9')
                {
                    int32_t disc_num = header[0x15] - '0' + 1;
                    snprintf(buffer, sizeof(buffer), "%d", disc_num);
                    print_info_wiiu("Disc Number:", buffer);
                    aaruf_set_media_sequence(output_ctx, disc_num, disc_num);
                }

                print_success_wiiu("Metadata extracted from disc header");
            }
        }
        else
        {
            /* Read from AaruFormat source */
            uint32_t hdr_len = WIIU_LOGICAL_SECTOR_SIZE;
            uint8_t  hdr_status;

            if(aaruf_read_sector(input_ctx, 0, false, header, &hdr_len, &hdr_status) == AARUF_STATUS_OK)
            {
                char product_code[11];
                memcpy(product_code, header, 10);
                product_code[10] = '\0';

                for(int i = 9; i >= 0 && (product_code[i] == '\0' || product_code[i] == ' '); i--)
                    product_code[i] = '\0';

                if(product_code[0] != '\0')
                {
                    print_info_wiiu("Product Code:", product_code);
                    aaruf_set_media_part_number(output_ctx, (const uint8_t *)product_code,
                                                (int32_t)strlen(product_code));
                }

                if(header[0x15] >= '0' && header[0x15] <= '9')
                {
                    int32_t disc_num = header[0x15] - '0' + 1;
                    snprintf(buffer, sizeof(buffer), "%d", disc_num);
                    print_info_wiiu("Disc Number:", buffer);
                    aaruf_set_media_sequence(output_ctx, disc_num, disc_num);
                }

                print_success_wiiu("Metadata extracted from disc header");
            }
        }
    }

    /* ── Step 8: Copy metadata from source AaruFormat image ──────── */
    if(result == 0 && !is_raw && input_ctx != NULL)
    {
        printf("\n" ANSI_BOLD ANSI_CYAN "  Copying Source Metadata" ANSI_RESET "\n");
        printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                         "\n");

        /* Copy media tags (skip Wii U-specific ones already written) */
        size_t   tags_size = 0;
        uint8_t *tags_map  = NULL;
        int32_t  ret       = aaruf_get_readable_media_tags(input_ctx, NULL, &tags_size);

        if(ret == AARUF_ERROR_BUFFER_TOO_SMALL && tags_size > 0)
        {
            tags_map = (uint8_t *)malloc(tags_size);

            if(tags_map != NULL)
            {
                ret = aaruf_get_readable_media_tags(input_ctx, tags_map, &tags_size);

                if(ret == AARUF_STATUS_OK)
                {
                    int copied = 0;

                    for(size_t i = 0; i < tags_size; i++)
                    {
                        if(!tags_map[i]) continue;

                        /* Skip Wii U-specific tags we already wrote */
                        if((int32_t)i == kMediaTagWiiUDiscKey || (int32_t)i == kMediaTagWiiUPartitionKeyMap) continue;

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
                        print_success_wiiu(buffer);
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
                        print_success_wiiu("Copied dump hardware information");
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
                        print_success_wiiu("Copied Aaru JSON metadata");
                }

                free(json);
            }
        }

        /* Copy basic metadata fields */
        int32_t  meta_len = 0;
        uint8_t *meta_buf = NULL;

        /* Creator */
        meta_len = 0;

        if(aaruf_get_creator(input_ctx, NULL, &meta_len) == AARUF_ERROR_BUFFER_TOO_SMALL && meta_len > 0)
        {
            meta_buf = (uint8_t *)malloc(meta_len);

            if(meta_buf != NULL)
            {
                int32_t ml = meta_len;

                if(aaruf_get_creator(input_ctx, meta_buf, &ml) == AARUF_STATUS_OK)
                    aaruf_set_creator(output_ctx, meta_buf, ml);

                free(meta_buf);
            }
        }

        /* Comments */
        meta_len = 0;

        if(aaruf_get_comments(input_ctx, NULL, &meta_len) == AARUF_ERROR_BUFFER_TOO_SMALL && meta_len > 0)
        {
            meta_buf = (uint8_t *)malloc(meta_len);

            if(meta_buf != NULL)
            {
                int32_t ml = meta_len;

                if(aaruf_get_comments(input_ctx, meta_buf, &ml) == AARUF_STATUS_OK)
                    aaruf_set_comments(output_ctx, meta_buf, ml);

                free(meta_buf);
            }
        }

        /* Media sequence */
        int32_t seq = 0, last_seq = 0;

        if(aaruf_get_media_sequence(input_ctx, &seq, &last_seq) == AARUF_STATUS_OK && seq > 0)
            aaruf_set_media_sequence(output_ctx, seq, last_seq);
    }

cleanup:
    free(phys_sector_buf);
    free(key_map_data);

    /* Securely wipe key material */
    memset(disc_key, 0, 16);

    for(int i = 0; i < part_count; i++) memset(parts[i].key, 0, 16);

    for(int i = 0; i < part_count; i++) memset(regions[i].key, 0, 16);

    /* Close images */
    if(output_ctx != NULL) aaruf_close(output_ctx);

    if(input_ctx != NULL) aaruf_close(input_ctx);

    if(wud_open) wiiu_reader_close(&wud_reader);

    if(result == 0)
        printf("\n" ANSI_GREEN "  ✓ Conversion completed successfully!" ANSI_RESET "\n\n");
    else
        printf("\n" ANSI_RED "  ✗ Conversion failed." ANSI_RESET "\n\n");

    return result;
}
