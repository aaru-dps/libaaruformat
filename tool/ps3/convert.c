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
 * convert-ps3 command: converts ISO or AaruFormat PS3 disc images to AaruFormat
 * with decrypted sector storage and proper media tags/metadata.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <aaruformat.h>

#include "aaruformattool.h"
#include "ird.h"
#include "iso9660_mini.h"
#include "sfo.h"

#include "../src/ps3/ps3_crypto.h"
#include "../src/ps3/ps3_encryption_map.h"

/* ANSI color codes */
#define ANSI_RESET  "\033[0m"
#define ANSI_BOLD   "\033[1m"
#define ANSI_RED    "\033[31m"
#define ANSI_GREEN  "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_CYAN   "\033[36m"
#define ANSI_WHITE  "\033[37m"
#define ANSI_BLUE   "\033[34m"

#define PROGRESS_BAR_WIDTH 40
#define PS3_SECTOR_SIZE    2048

static void print_error_ps3(const char *msg) { fprintf(stderr, ANSI_RED "  ✗ %s" ANSI_RESET "\n", msg); }

static void print_success_ps3(const char *msg) { printf(ANSI_GREEN "  ✓ %s" ANSI_RESET "\n", msg); }

static void print_info_ps3(const char *label, const char *value)
{ printf("  " ANSI_YELLOW "%-20s" ANSI_RESET " %s\n", label, value); }

static void format_bytes_ps3(uint64_t bytes, char *buffer, size_t buffer_size)
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

static int parse_hex_key(const char *hex, uint8_t key[16])
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

static int try_sidecar_file(const char *input_path, const char *suffix, uint8_t *out, size_t expected_len)
{
    size_t path_len = strlen(input_path) + strlen(suffix) + 1;
    char  *path     = malloc(path_len);

    if(path == NULL) return -1;

    /* First try: append suffix (e.g., "input.iso.disc_key") */
    snprintf(path, path_len, "%s%s", input_path, suffix);

    FILE *f = fopen(path, "rb");

    /* Second try: replace extension (e.g., "input.disc_key") */
    if(f == NULL)
    {
        const char *dot = strrchr(input_path, '.');
        const char *sep = strrchr(input_path, '/');

        /* Only replace if dot is after last path separator (i.e., it's a file extension) */
        if(dot != NULL && (sep == NULL || dot > sep))
        {
            size_t base_len = (size_t)(dot - input_path);
            size_t new_len  = base_len + strlen(suffix) + 1;
            char  *alt_path = malloc(new_len);

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

    size_t read = fread(out, 1, expected_len, f);
    fclose(f);

    return read == expected_len ? 0 : -1;
}

/* ISO sector read callback for iso9660_read_file */
typedef struct
{
    FILE    *fp;
    uint64_t total_sectors;
} IsoReadCtx;

static int32_t iso_read_sector_cb(void *user_data, uint64_t sector, uint8_t *buffer)
{
    IsoReadCtx *ctx = (IsoReadCtx *)user_data;

    if(sector >= ctx->total_sectors) return -1;

    if(fseek(ctx->fp, (long)(sector * PS3_SECTOR_SIZE), SEEK_SET) != 0) return -1;

    if(fread(buffer, 1, PS3_SECTOR_SIZE, ctx->fp) != PS3_SECTOR_SIZE) return -1;

    return 0;
}

/* AaruFormat sector read callback for iso9660_read_file */
typedef struct
{
    aaruformat_context *ctx;
} AarufReadCtx;

static int32_t aaruf_read_sector_cb(void *user_data, uint64_t sector, uint8_t *buffer)
{
    AarufReadCtx *ctx    = (AarufReadCtx *)user_data;
    uint32_t      length = PS3_SECTOR_SIZE;
    uint8_t       status = 0;

    int32_t ret = aaruf_read_sector(ctx->ctx, sector, false, buffer, &length, &status);

    return ret == AARUF_STATUS_OK ? 0 : -1;
}

int convert_ps3(const char *input_path, const char *output_path, const char *disc_key_hex, const char *data1_key_hex,
                const char *ird_path)
{
    int     result = 0;
    char    buffer[256];
    uint8_t disc_key[16];
    bool    have_disc_key = false;
    uint8_t data1_key[16];
    bool    have_data1 = false;
    IrdFile ird;
    bool    have_ird = false;

    printf("\n" ANSI_BOLD ANSI_CYAN "════════════════════════════════════════════════════════════════════════════════\n"
           "                        PS3 DISC IMAGE CONVERTER\n"
           "════════════════════════════════════════════════════════════════════════════════" ANSI_RESET "\n");

    memset(&ird, 0, sizeof(ird));

    /* ── Step 1: Resolve key material ────────────────────────────── */
    printf("\n" ANSI_BOLD ANSI_CYAN "  Key Resolution" ANSI_RESET "\n");
    printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                     "\n");

    /* 1a: --disc-key */
    if(disc_key_hex != NULL)
    {
        if(parse_hex_key(disc_key_hex, disc_key) == 0)
        {
            have_disc_key = true;
            print_success_ps3("Disc key provided via --disc-key");
        }
        else
            print_error_ps3("Invalid --disc-key format (expected 32 hex chars)");
    }

    /* 1b: --data1-key */
    if(!have_disc_key && data1_key_hex != NULL)
    {
        if(parse_hex_key(data1_key_hex, data1_key) == 0)
        {
            have_data1 = true;
            ps3_derive_disc_key(data1_key, disc_key);
            have_disc_key = true;
            print_success_ps3("Disc key derived from --data1-key");
        }
        else
            print_error_ps3("Invalid --data1-key format (expected 32 hex chars)");
    }

    /* 1c: --ird */
    if(ird_path != NULL)
    {
        if(ps3_parse_ird(ird_path, &ird) == 0 && ird.valid)
        {
            have_ird = true;
            snprintf(buffer, sizeof(buffer), "IRD loaded: %s — %s (v%d)", ird.game_id, ird.game_name, ird.version);
            print_success_ps3(buffer);

            if(!have_disc_key)
            {
                memcpy(data1_key, ird.d1, 16);
                have_data1 = true;
                ps3_derive_disc_key(data1_key, disc_key);
                have_disc_key = true;
                print_success_ps3("Disc key derived from IRD data1");
            }
        }
        else
            print_error_ps3("Cannot parse IRD file");
    }

    /* 1d: Sidecar files */
    if(!have_disc_key)
    {
        if(try_sidecar_file(input_path, ".disc_key", disc_key, 16) == 0)
        {
            have_disc_key = true;
            print_success_ps3("Disc key loaded from sidecar .disc_key file");
        }
    }

    if(!have_disc_key)
    {
        if(try_sidecar_file(input_path, ".data1", data1_key, 16) == 0)
        {
            have_data1 = true;
            ps3_derive_disc_key(data1_key, disc_key);
            have_disc_key = true;
            print_success_ps3("Disc key derived from sidecar .data1 file");
        }
    }

    if(!have_disc_key || !have_ird)
    {
        /* Try sidecar .ird — both "input.iso.ird" and "input.ird" */
        size_t ird_sidecar_len = strlen(input_path) + 5;
        char  *ird_sidecar     = malloc(ird_sidecar_len);

        if(ird_sidecar != NULL)
        {
            /* First: append .ird to full path */
            snprintf(ird_sidecar, ird_sidecar_len, "%s.ird", input_path);

            if(!have_ird && ps3_parse_ird(ird_sidecar, &ird) == 0 && ird.valid)
            {
                have_ird = true;
                snprintf(buffer, sizeof(buffer), "Sidecar IRD loaded: %s — %s", ird.game_id, ird.game_name);
                print_success_ps3(buffer);

                if(!have_disc_key)
                {
                    memcpy(data1_key, ird.d1, 16);
                    have_data1 = true;
                    ps3_derive_disc_key(data1_key, disc_key);
                    have_disc_key = true;
                    print_success_ps3("Disc key derived from sidecar IRD data1");
                }
            }

            free(ird_sidecar);
        }

        /* Second: replace extension with .ird (e.g., "input.iso" → "input.ird") */
        if(!have_ird)
        {
            const char *dot = strrchr(input_path, '.');
            const char *sep = strrchr(input_path, '/');

            if(dot != NULL && (sep == NULL || dot > sep))
            {
                size_t base_len    = (size_t)(dot - input_path);
                size_t alt_ird_len = base_len + 5;
                char  *alt_ird     = malloc(alt_ird_len);

                if(alt_ird != NULL)
                {
                    memcpy(alt_ird, input_path, base_len);
                    memcpy(alt_ird + base_len, ".ird", 5);

                    if(ps3_parse_ird(alt_ird, &ird) == 0 && ird.valid)
                    {
                        have_ird = true;
                        snprintf(buffer, sizeof(buffer), "Sidecar IRD loaded: %s — %s", ird.game_id, ird.game_name);
                        print_success_ps3(buffer);

                        if(!have_disc_key)
                        {
                            memcpy(data1_key, ird.d1, 16);
                            have_data1 = true;
                            ps3_derive_disc_key(data1_key, disc_key);
                            have_disc_key = true;
                            print_success_ps3("Disc key derived from sidecar IRD data1");
                        }
                    }

                    free(alt_ird);
                }
            }
        }
    }

    /* ── Step 2: Open source image ───────────────────────────────── */
    printf("\n" ANSI_BOLD ANSI_CYAN "  Opening Source Image" ANSI_RESET "\n");
    printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                     "\n");

    bool                is_iso    = false;
    aaruformat_context *input_ctx = NULL;
    FILE               *iso_fp    = NULL;
    uint64_t            total_sectors;
    uint32_t            media_type = PS3BD;

    /* Try to open as AaruFormat first */
    input_ctx = aaruf_open(input_path, false, NULL);

    if(input_ctx != NULL)
    {
        media_type = input_ctx->image_info.MediaType;

        if(media_type != PS3DVD && media_type != PS3BD)
        {
            snprintf(buffer, sizeof(buffer), "Source image media type is %s, expected PS3DVD or PS3BD",
                     media_type_to_string(media_type));
            print_error_ps3(buffer);
            aaruf_close(input_ctx);
            ps3_free_ird(&ird);
            return -1;
        }

        total_sectors = input_ctx->image_info.Sectors;
        snprintf(buffer, sizeof(buffer), "AaruFormat image: %s, %llu sectors", media_type_to_string(media_type),
                 (unsigned long long)total_sectors);
        print_success_ps3(buffer);

        /* 1e: Try media tags for keys if not yet resolved */
        if(!have_disc_key)
        {
            uint32_t tag_len = 16;
            if(aaruf_read_media_tag(input_ctx, disc_key, kMediaTagPs3DiscKey, &tag_len) == AARUF_STATUS_OK)
            {
                have_disc_key = true;
                print_success_ps3("Disc key loaded from source image media tags");
            }
        }

        if(!have_disc_key)
        {
            uint32_t tag_len = 16;
            if(aaruf_read_media_tag(input_ctx, data1_key, kMediaTagPs3Data1, &tag_len) == AARUF_STATUS_OK)
            {
                have_data1 = true;
                ps3_derive_disc_key(data1_key, disc_key);
                have_disc_key = true;
                print_success_ps3("Disc key derived from source image data1 media tag");
            }
        }
    }
    else
    {
        /* Try as raw ISO */
        iso_fp = fopen(input_path, "rb");

        if(iso_fp == NULL)
        {
            snprintf(buffer, sizeof(buffer), "Cannot open input file: %s", strerror(errno));
            print_error_ps3(buffer);
            ps3_free_ird(&ird);
            return -1;
        }

        fseek(iso_fp, 0, SEEK_END);
        long file_size = ftell(iso_fp);
        rewind(iso_fp);

        if(file_size <= 0 || file_size % PS3_SECTOR_SIZE != 0)
        {
            snprintf(buffer, sizeof(buffer), "Invalid ISO file size (%ld bytes, not a multiple of %d)", file_size,
                     PS3_SECTOR_SIZE);
            print_error_ps3(buffer);
            fclose(iso_fp);
            ps3_free_ird(&ird);
            return -1;
        }

        is_iso        = true;
        total_sectors = (uint64_t)file_size / PS3_SECTOR_SIZE;
        media_type    = PS3BD;

        /* Check sector 1 for PlayStation 3 identification */
        if(total_sectors >= 2)
        {
            uint8_t sector1[PS3_SECTOR_SIZE];

            if(fseek(iso_fp, PS3_SECTOR_SIZE, SEEK_SET) == 0 &&
               fread(sector1, 1, PS3_SECTOR_SIZE, iso_fp) == PS3_SECTOR_SIZE)
            {
                if(memcmp(sector1, "PlayStation3", 12) != 0)
                {
                    print_error_ps3("Sector 1 does not contain PlayStation 3 identification.");
                    fclose(iso_fp);
                    ps3_free_ird(&ird);
                    return -1;
                }

                print_success_ps3("PlayStation 3 identification found in sector 1");
            }

            rewind(iso_fp);
        }

        snprintf(buffer, sizeof(buffer), "Raw ISO image: %llu sectors (%.2f GiB)", (unsigned long long)total_sectors,
                 (double)file_size / (1024.0 * 1024.0 * 1024.0));
        print_success_ps3(buffer);
        print_info_ps3("Media type:", "PS3BD (default for ISO)");
    }

    if(!have_disc_key)
    {
        print_error_ps3("No disc key found. Provide --disc-key, --data1-key, --ird, or place sidecar files.");
        if(input_ctx != NULL) aaruf_close(input_ctx);
        if(iso_fp != NULL) fclose(iso_fp);
        ps3_free_ird(&ird);
        return -1;
    }

    /* ── Step 3: Read sector 0 and parse encryption map ──────────── */
    printf("\n" ANSI_BOLD ANSI_CYAN "  Parsing Encryption Map" ANSI_RESET "\n");
    printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                     "\n");

    uint8_t sector0[PS3_SECTOR_SIZE];

    if(is_iso)
    {
        rewind(iso_fp);

        if(fread(sector0, 1, PS3_SECTOR_SIZE, iso_fp) != PS3_SECTOR_SIZE)
        {
            print_error_ps3("Cannot read sector 0 from ISO");
            fclose(iso_fp);
            ps3_free_ird(&ird);
            return -1;
        }
    }
    else
    {
        uint32_t s0_len = PS3_SECTOR_SIZE;
        uint8_t  s0_status;
        int32_t  s0_ret = aaruf_read_sector(input_ctx, 0, false, sector0, &s0_len, &s0_status);

        if(s0_ret != AARUF_STATUS_OK)
        {
            snprintf(buffer, sizeof(buffer), "Cannot read sector 0 from AaruFormat image (error %d)", s0_ret);
            print_error_ps3(buffer);
            aaruf_close(input_ctx);
            ps3_free_ird(&ird);
            return -1;
        }
    }

    Ps3PlaintextRegion *regions      = NULL;
    uint32_t            region_count = 0;

    int32_t map_ret = ps3_parse_encryption_map(sector0, PS3_SECTOR_SIZE, &regions, &region_count);

    if(map_ret != 0)
    {
        snprintf(buffer, sizeof(buffer), "Cannot parse encryption map from sector 0 (error %d)", map_ret);
        print_error_ps3(buffer);
        if(input_ctx != NULL) aaruf_close(input_ctx);
        if(iso_fp != NULL) fclose(iso_fp);
        ps3_free_ird(&ird);
        return -1;
    }

    snprintf(buffer, sizeof(buffer), "Encryption map: %u plaintext region(s)", region_count);
    print_success_ps3(buffer);

    for(uint32_t i = 0; i < region_count; i++)
    {
        snprintf(buffer, sizeof(buffer), "  Region %u: sectors %u–%u", i, regions[i].start_sector,
                 regions[i].end_sector);
        printf("  %s\n", buffer);
    }

    /* Serialize encryption map for media tag */
    uint8_t *enc_map_data   = NULL;
    uint32_t enc_map_length = 0;
    ps3_serialize_encryption_map(regions, region_count, &enc_map_data, &enc_map_length);

    /* ── Step 4: Create destination AaruFormat image ─────────────── */
    printf("\n" ANSI_BOLD ANSI_CYAN "  Creating Destination Image" ANSI_RESET "\n");
    printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                     "\n");

    const char *app_name     = "aaruformattool";
    size_t      app_name_len = strlen(app_name);

    aaruformat_context *output_ctx = aaruf_create(output_path, media_type, PS3_SECTOR_SIZE, total_sectors, 0, 0, NULL,
                                                  (const uint8_t *)app_name, (uint8_t)app_name_len, 1, 0, false);

    if(output_ctx == NULL)
    {
        snprintf(buffer, sizeof(buffer), "Cannot create output image: %s", strerror(errno));
        print_error_ps3(buffer);
        free(enc_map_data);
        free(regions);
        if(input_ctx != NULL) aaruf_close(input_ctx);
        if(iso_fp != NULL) fclose(iso_fp);
        ps3_free_ird(&ird);
        return -1;
    }

    print_success_ps3("Destination image created");

    /* Write media tags (before sectors so lazy init can find them) */
    aaruf_write_media_tag(output_ctx, disc_key, kMediaTagPs3DiscKey, 16);
    print_success_ps3("Written media tag: PS3 Disc Key");

    if(have_data1)
    {
        aaruf_write_media_tag(output_ctx, data1_key, kMediaTagPs3Data1, 16);
        print_success_ps3("Written media tag: PS3 Data1");
    }

    if(have_ird)
    {
        aaruf_write_media_tag(output_ctx, ird.d2, kMediaTagPs3Data2, 16);
        print_success_ps3("Written media tag: PS3 Data2");

        if(ird.has_pic)
        {
            aaruf_write_media_tag(output_ctx, ird.pic, kMediaTagPs3Pic, 115);
            print_success_ps3("Written media tag: PS3 PIC");
        }
    }

    if(enc_map_data != NULL)
    {
        aaruf_write_media_tag(output_ctx, enc_map_data, kMediaTagPs3EncryptionMap, enc_map_length);
        print_success_ps3("Written media tag: PS3 Encryption Map");
    }

    /* ── Step 5: Copy sectors ────────────────────────────────────── */
    printf("\n" ANSI_BOLD ANSI_CYAN "  Converting Sectors" ANSI_RESET "\n");
    printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                     "\n\n");

    uint8_t *sector_data = malloc(PS3_SECTOR_SIZE);

    if(sector_data == NULL)
    {
        print_error_ps3("Cannot allocate sector buffer");
        result = -1;
        goto cleanup;
    }

    clock_t  start_time      = clock();
    uint64_t bytes_processed = 0;

    for(uint64_t sector = 0; sector < total_sectors; sector++)
    {
        /* Progress */
        if(sector % 1000 == 0 || sector == total_sectors - 1)
        {
            double percentage = (double)(sector + 1) / (double)total_sectors * 100.0;
            double elapsed    = (double)(clock() - start_time) / CLOCKS_PER_SEC;
            double speed      = elapsed > 0 ? (double)bytes_processed / elapsed : 0;

            char speed_str[32];
            format_bytes_ps3((uint64_t)speed, speed_str, sizeof(speed_str));

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

        /* Read sector from source */
        if(is_iso)
        {
            if(fseek(iso_fp, (long)(sector * PS3_SECTOR_SIZE), SEEK_SET) != 0 ||
               fread(sector_data, 1, PS3_SECTOR_SIZE, iso_fp) != PS3_SECTOR_SIZE)
            {
                printf("\n");
                snprintf(buffer, sizeof(buffer), "Error reading ISO sector %llu", (unsigned long long)sector);
                print_error_ps3(buffer);
                result = -1;
                break;
            }
        }
        else
        {
            uint32_t read_len = PS3_SECTOR_SIZE;
            uint8_t  status;
            int32_t  ret = aaruf_read_sector(input_ctx, sector, false, sector_data, &read_len, &status);

            if(ret != AARUF_STATUS_OK)
            {
                printf("\n");
                snprintf(buffer, sizeof(buffer), "Error reading sector %llu (error %d)", (unsigned long long)sector,
                         ret);
                print_error_ps3(buffer);
                result = -1;
                break;
            }
        }

        /* Determine sector status based on encryption map */
        uint8_t write_status;

        if(ps3_is_sector_encrypted(regions, region_count, sector))
            write_status = SectorStatusUnencrypted; /* Encrypted on disc → store decrypted */
        else
            write_status = SectorStatusDumped; /* Plaintext region → store as-is */

        /* Write sector to output */
        int32_t wret = aaruf_write_sector(output_ctx, sector, false, sector_data, write_status, PS3_SECTOR_SIZE);

        if(wret != AARUF_STATUS_OK)
        {
            printf("\n");
            snprintf(buffer, sizeof(buffer), "Error writing sector %llu (error %d)", (unsigned long long)sector, wret);
            print_error_ps3(buffer);
            result = -1;
            break;
        }

        bytes_processed += PS3_SECTOR_SIZE;
    }

    printf("\n\n");

    if(result == 0)
    {
        double elapsed = (double)(clock() - start_time) / CLOCKS_PER_SEC;
        char   size_str[32];
        format_bytes_ps3(bytes_processed, size_str, sizeof(size_str));
        snprintf(buffer, sizeof(buffer), "Converted %llu sectors (%s) in %.1f seconds",
                 (unsigned long long)total_sectors, size_str, elapsed);
        print_success_ps3(buffer);
    }

    /* ── Step 6: Extract and write metadata ──────────────────────── */
    if(result == 0)
    {
        printf("\n" ANSI_BOLD ANSI_CYAN "  Extracting Metadata" ANSI_RESET "\n");
        printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                         "\n");

        /* Try to read PARAM.SFO from the ISO filesystem */
        uint8_t *sfo_data   = NULL;
        uint32_t sfo_length = 0;

        if(is_iso)
        {
            IsoReadCtx iso_ctx = {.fp = iso_fp, .total_sectors = total_sectors};
            iso9660_read_file(iso_read_sector_cb, &iso_ctx, "/PS3_GAME/PARAM.SFO", &sfo_data, &sfo_length);
        }
        else
        {
            AarufReadCtx aaruf_ctx = {.ctx = input_ctx};
            iso9660_read_file(aaruf_read_sector_cb, &aaruf_ctx, "/PS3_GAME/PARAM.SFO", &sfo_data, &sfo_length);
        }

        if(sfo_data != NULL && sfo_length > 0)
        {
            SfoFile sfo;

            if(ps3_parse_sfo(sfo_data, sfo_length, &sfo) == 0)
            {
                const char *title    = ps3_sfo_get_string(&sfo, "TITLE");
                const char *title_id = ps3_sfo_get_string(&sfo, "TITLE_ID");

                if(title != NULL)
                {
                    print_info_ps3("Title:", title);
                    aaruf_set_media_title(output_ctx, (const uint8_t *)title, (int32_t)strlen(title));
                }

                if(title_id != NULL)
                {
                    print_info_ps3("Title ID:", title_id);
                    aaruf_set_media_part_number(output_ctx, (const uint8_t *)title_id, (int32_t)strlen(title_id));
                }

                if(title != NULL || title_id != NULL)
                    print_success_ps3("Metadata extracted from PARAM.SFO");
                else
                    printf("  " ANSI_YELLOW "⚠ PARAM.SFO found but no mappable fields" ANSI_RESET "\n");

                ps3_free_sfo(&sfo);
            }
            else
                printf("  " ANSI_YELLOW "⚠ Cannot parse PARAM.SFO" ANSI_RESET "\n");

            free(sfo_data);
        }
        else
            printf("  " ANSI_YELLOW "⚠ PARAM.SFO not found in filesystem" ANSI_RESET "\n");

        /* If IRD provides game name, use it as title if not already set */
        if(have_ird && ird.game_name[0] != '\0')
        {
            print_info_ps3("IRD Game Name:", ird.game_name);
            print_info_ps3("IRD Game ID:", ird.game_id);
        }
    }

    /* ── Step 7: Copy metadata from source AaruFormat image ──────── */
    if(result == 0 && !is_iso && input_ctx != NULL)
    {
        printf("\n" ANSI_BOLD ANSI_CYAN "  Copying Source Metadata" ANSI_RESET "\n");
        printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                         "\n");

        /* Copy media tags (skip ones we already wrote) */
        size_t   tags_size = 0;
        uint8_t *tags_map  = NULL;
        int32_t  ret       = aaruf_get_readable_media_tags(input_ctx, NULL, &tags_size);

        if(ret == AARUF_ERROR_BUFFER_TOO_SMALL && tags_size > 0)
        {
            tags_map = malloc(tags_size);

            if(tags_map != NULL)
            {
                ret = aaruf_get_readable_media_tags(input_ctx, tags_map, &tags_size);

                if(ret == AARUF_STATUS_OK)
                {
                    int copied = 0;

                    for(size_t i = 0; i < tags_size; i++)
                    {
                        if(!tags_map[i]) continue;

                        /* Skip PS3-specific tags we already wrote */
                        if((int32_t)i == kMediaTagPs3DiscKey || (int32_t)i == kMediaTagPs3Data1 ||
                           (int32_t)i == kMediaTagPs3Data2 || (int32_t)i == kMediaTagPs3Pic ||
                           (int32_t)i == kMediaTagPs3EncryptionMap)
                            continue;

                        uint32_t tag_len = 0;
                        ret              = aaruf_read_media_tag(input_ctx, NULL, (int32_t)i, &tag_len);

                        if(ret != AARUF_ERROR_BUFFER_TOO_SMALL || tag_len == 0) continue;

                        uint8_t *tag_data = malloc(tag_len);

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
                        print_success_ps3(buffer);
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
            uint8_t *dumphw = malloc(dumphw_size);

            if(dumphw != NULL)
            {
                ret = aaruf_get_dumphw(input_ctx, dumphw, &dumphw_size);

                if(ret == AARUF_STATUS_OK)
                {
                    if(aaruf_set_dumphw(output_ctx, dumphw, dumphw_size) == AARUF_STATUS_OK)
                        print_success_ps3("Copied dump hardware information");
                }

                free(dumphw);
            }
        }

        /* Copy JSON metadata */
        size_t json_size = 0;
        ret              = aaruf_get_aaru_json_metadata(input_ctx, NULL, &json_size);

        if(ret == AARUF_ERROR_BUFFER_TOO_SMALL && json_size > 0)
        {
            uint8_t *json = malloc(json_size);

            if(json != NULL)
            {
                ret = aaruf_get_aaru_json_metadata(input_ctx, json, &json_size);

                if(ret == AARUF_STATUS_OK)
                {
                    if(aaruf_set_aaru_json_metadata(output_ctx, json, json_size) == AARUF_STATUS_OK)
                        print_success_ps3("Copied Aaru JSON metadata");
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
            meta_buf = malloc(meta_len);

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
            meta_buf = malloc(meta_len);

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
    free(sector_data);
    free(enc_map_data);
    free(regions);

    /* Close images */
    if(output_ctx != NULL) aaruf_close(output_ctx);
    if(input_ctx != NULL) aaruf_close(input_ctx);
    if(iso_fp != NULL) fclose(iso_fp);
    ps3_free_ird(&ird);

    if(result == 0) { printf("\n" ANSI_GREEN "  ✓ Conversion completed successfully!" ANSI_RESET "\n\n"); }
    else
    {
        printf("\n" ANSI_RED "  ✗ Conversion failed." ANSI_RESET "\n\n");
    }

    return result;
}
