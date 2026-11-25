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

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <aaruformat.h>
#include <unicode/ucnv.h>
#include <unicode/ustring.h>

#include "aaruformattool.h"

// ANSI color codes for terminal output
#define ANSI_RESET  "\033[0m"
#define ANSI_BOLD   "\033[1m"
#define ANSI_RED    "\033[31m"
#define ANSI_GREEN  "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_BLUE   "\033[34m"
#define ANSI_CYAN   "\033[36m"
#define ANSI_WHITE  "\033[37m"

// Progress bar width
#define PROGRESS_BAR_WIDTH 40

// Format bytes to human-readable string
static void format_bytes(uint64_t bytes, char *buffer, size_t buffer_size)
{
    const char *units[]  = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    int         unit_idx = 0;
    double      size     = (double)bytes;

    while(size >= 1024.0 && unit_idx < 5)
    {
        size /= 1024.0;
        unit_idx++;
    }

    if(unit_idx == 0)
        snprintf(buffer, buffer_size, "%llu %s", (unsigned long long)bytes, units[unit_idx]);
    else
        snprintf(buffer, buffer_size, "%.2f %s", size, units[unit_idx]);
}

// Format time duration to human-readable string
static void format_duration(double seconds, char *buffer, size_t buffer_size)
{
    int secs = (int)seconds;
    if(seconds < 60)
        snprintf(buffer, buffer_size, "%.1f sec", seconds);
    else if(seconds < 3600)
        snprintf(buffer, buffer_size, "%d min %d sec", secs / 60, secs % 60);
    else
        snprintf(buffer, buffer_size, "%d hr %d min %d sec", secs / 3600, (secs % 3600) / 60, secs % 60);
}

// Draw a progress bar
static void draw_progress_bar(double percentage, double speed, double elapsed, double eta)
{
    char speed_str[32];
    char elapsed_str[32];
    char eta_str[32];

    format_bytes((uint64_t)speed, speed_str, sizeof(speed_str));
    format_duration(elapsed, elapsed_str, sizeof(elapsed_str));

    if(eta > 0 && eta < 86400 * 365)  // Less than a year
        format_duration(eta, eta_str, sizeof(eta_str));
    else
        snprintf(eta_str, sizeof(eta_str), "--:--");

    int filled = (int)(percentage / 100.0 * PROGRESS_BAR_WIDTH);
    if(filled > PROGRESS_BAR_WIDTH) filled = PROGRESS_BAR_WIDTH;

    printf("\r" ANSI_CYAN "  [");

    for(int i = 0; i < PROGRESS_BAR_WIDTH; i++)
        if(i < filled)
            printf(ANSI_GREEN "█");
        else if(i == filled)
            printf(ANSI_YELLOW "▓");
        else
            printf(ANSI_WHITE "░");

    printf(ANSI_CYAN "] " ANSI_WHITE "%5.1f%%" ANSI_RESET, percentage);
    printf(" │ %s/s │ %s │ ETA: %s   ", speed_str, elapsed_str, eta_str);

    fflush(stdout);
}

// Print a separator line
static void print_separator(void)
{
    printf(ANSI_BLUE "────────────────────────────────────────────────────────────────────────────────" ANSI_RESET
                     "\n");
}

// Print a header
static void print_header(const char *title)
{
    printf("\n" ANSI_BOLD ANSI_CYAN "  %s" ANSI_RESET "\n", title);
    print_separator();
}

// Print an info field
static void print_info(const char *label, const char *value)
{ printf("  " ANSI_YELLOW "%-20s" ANSI_RESET " %s\n", label, value); }

// Print success message
static void print_success(const char *message) { printf(ANSI_GREEN "  ✓ %s" ANSI_RESET "\n", message); }

// Print error message
static void print_error(const char *message) { printf(ANSI_RED "  ✗ %s" ANSI_RESET "\n", message); }

// Print warning message
static void print_warning(const char *message) { printf(ANSI_YELLOW "  ⚠ %s" ANSI_RESET "\n", message); }

// Helper to convert UTF-16LE metadata to displayable string
// Returns malloc'd string that must be freed, or NULL on failure
static char *utf16le_to_utf8(const uint8_t *utf16_data, int32_t utf16_length)
{
    if(utf16_data == NULL || utf16_length <= 0) return NULL;

    char *result = malloc(utf16_length + 1);
    if(result == NULL) return NULL;

    memset(result, 0, utf16_length + 1);
    UErrorCode u_error = U_ZERO_ERROR;
    ucnv_convert(NULL, "UTF-16LE", result, utf16_length, (const char *)utf16_data, utf16_length, &u_error);

    if(u_error != U_ZERO_ERROR)
    {
        free(result);
        return NULL;
    }

    return result;
}

/** A per-sector tag that can be copied through the sector tag API. */
typedef struct
{
    int32_t  tag;
    uint32_t size;
    bool     in_long_sector;  ///< Already part of the long sector, not copied separately in long mode.
} CopyableSectorTag;

static const CopyableSectorTag copyable_sector_tags[] = {
    {        kSectorTagCdSubchannel, 96, false},
    {         kSectorTagCdSubHeader,  8,  true},
    {kSectorTagDvdSectorInformation,  1,  true},
    {     kSectorTagDvdSectorNumber,  3,  true},
    {        kSectorTagDvdSectorIed,  2,  true},
    {              kSectorTagDvdCmi,  1,  true},
    {        kSectorTagDvdSectorEdc,  4,  true},
    {kSectorTagDvdTitleKeyDecrypted,  5, false},
    {           kSectorTagAppleSony, 12,  true},
    {        kSectorTagAppleProfile, 20,  true},
    {      kSectorTagPriamDataTower, 24,  true},
    {         kSectorTagBdSectorEdc,  4,  true},
};

/** Counters of what was copied. */
typedef struct
{
    uint64_t twins;
    uint64_t tags;
    uint64_t not_dumped;
    uint64_t bytes;
} CopyStatistics;

/**
 * Copies one physical instance (twin) of a sector, its data and the per-sector tags in @p tags.
 * Sectors that were not dumped are skipped (twins are kept as empty instances so later twins keep their index).
 */
static int32_t copy_sector_instance(aaruformat_context *input_ctx, aaruformat_context *output_ctx,
                                    const uint64_t sector, const bool negative, const uint32_t twin, bool use_long,
                                    uint8_t **sector_data, uint32_t *sector_size, const CopyableSectorTag *const *tags,
                                    const size_t tag_count, CopyStatistics *statistics)
{
    uint32_t length = 0;
    uint8_t  status = 0;
    int32_t  res    = use_long ? aaruf_read_sector_twin_long(input_ctx, sector, negative, twin, NULL, &length, &status)
                               : aaruf_read_sector_twin(input_ctx, sector, negative, twin, NULL, &length, &status);

    // Media without long sectors (e.g. a plain floppy) are copied with normal sectors
    if(use_long && res == AARUF_ERROR_INCORRECT_MEDIA_TYPE)
    {
        use_long = false;
        res      = aaruf_read_sector_twin(input_ctx, sector, negative, twin, NULL, &length, &status);
    }

    if(res == AARUF_ERROR_BUFFER_TOO_SMALL)
    {
        if(*sector_size < length)
        {
            uint8_t *grown = realloc(*sector_data, length);
            if(grown == NULL) return AARUF_ERROR_NOT_ENOUGH_MEMORY;
            *sector_data = grown;
            *sector_size = length;
        }

        // Long reads only find out the sector was not dumped when actually reading it
        res = use_long ? aaruf_read_sector_twin_long(input_ctx, sector, negative, twin, *sector_data, &length, &status)
                       : aaruf_read_sector_twin(input_ctx, sector, negative, twin, *sector_data, &length, &status);
    }

    if(res == AARUF_STATUS_SECTOR_NOT_DUMPED)
    {
        statistics->not_dumped++;

        if(twin > 0)
        {
            if(length == 0 || length > *sector_size) length = *sector_size;
            memset(*sector_data, 0, length);
            res = aaruf_write_sector_twin(output_ctx, sector, negative, twin, *sector_data, SectorStatusNotDumped,
                                          length);
            if(res != AARUF_STATUS_OK) return res;
        }
    }
    else if(res == AARUF_STATUS_OK)
    {
        if(twin == 0)
            res = use_long ? aaruf_write_sector_long(output_ctx, sector, negative, *sector_data, status, length)
                           : aaruf_write_sector(output_ctx, sector, negative, *sector_data, status, length);
        else
            res = use_long
                      ? aaruf_write_sector_twin_long(output_ctx, sector, negative, twin, *sector_data, status, length)
                      : aaruf_write_sector_twin(output_ctx, sector, negative, twin, *sector_data, status, length);

        if(res != AARUF_STATUS_OK) return res;

        statistics->bytes += length;
        if(twin > 0) statistics->twins++;
    }
    else
        return res;

    for(size_t t = 0; t < tag_count; t++)
    {
        uint8_t  tag_data[96];
        uint32_t tag_length = tags[t]->size;

        if(aaruf_read_sector_twin_tag(input_ctx, sector, negative, twin, tag_data, &tag_length, tags[t]->tag) !=
           AARUF_STATUS_OK)
            continue;

        res = aaruf_write_sector_twin_tag(output_ctx, sector, negative, twin, tag_data, tag_length, tags[t]->tag);
        if(res != AARUF_STATUS_OK) return res;

        statistics->tags++;
    }

    return AARUF_STATUS_OK;
}

/** Copies every physical instance of a sector. */
static int32_t copy_sector(aaruformat_context *input_ctx, aaruformat_context *output_ctx, const uint64_t sector,
                           const bool negative, const bool use_long, uint8_t **sector_data, uint32_t *sector_size,
                           const CopyableSectorTag *const *tags, const size_t tag_count, CopyStatistics *statistics)
{
    uint32_t instances = 1;
    if(aaruf_get_sector_twin_count(input_ctx, sector, negative, &instances) != AARUF_STATUS_OK || instances == 0)
        instances = 1;

    for(uint32_t twin = 0; twin < instances; twin++)
    {
        const int32_t res = copy_sector_instance(input_ctx, output_ctx, sector, negative, twin, use_long, sector_data,
                                                 sector_size, tags, tag_count, statistics);
        if(res != AARUF_STATUS_OK) return res;
    }

    return AARUF_STATUS_OK;
}

int convert_ec(const char *input_path, const char *output_path, bool use_long, uint16_t ec_k, uint16_t ec_m)
{
    aaruformat_context *input_ctx     = NULL;
    aaruformat_context *output_ctx    = NULL;
    int32_t             res           = 0;
    uint32_t            sector_size   = 0;
    uint64_t            total_sectors = 0;
    uint8_t            *sector_data   = NULL;
    uint8_t             sector_status = 0;
    clock_t             start_time;
    clock_t             current_time;
    uint64_t            bytes_processed = 0;
    char                buffer[64];

    // Print banner
    printf("\n");
    printf(ANSI_BOLD ANSI_CYAN "════════════════════════════════════════════════════════════════════════════════\n");
    printf("                           AARUFORMAT IMAGE CONVERTER\n");
    printf("════════════════════════════════════════════════════════════════════════════════" ANSI_RESET "\n");

    // Print conversion info
    print_header("Conversion Settings");
    print_info("Input file:", input_path);
    print_info("Output file:", output_path);
    print_info("Mode:", use_long ? "Long sectors (with metadata)" : "Standard sectors");

    // Open input image
    print_header("Opening Source Image");
    input_ctx = aaruf_open(input_path, false, NULL);
    if(input_ctx == NULL)
    {
        snprintf(buffer, sizeof(buffer), "Cannot open input image (error %d)", errno);
        print_error(buffer);
        return errno;
    }
    print_success("Source image opened successfully");

    // Get and display image information
    total_sectors = input_ctx->image_info.Sectors;
    sector_size   = input_ctx->image_info.SectorSize;

    print_header("Source Image Information");
    print_info("Media type:", media_type_to_string(input_ctx->header.mediaType));

    snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)total_sectors);
    print_info("Total sectors:", buffer);

    snprintf(buffer, sizeof(buffer), "%u bytes", sector_size);
    print_info("Sector size:", buffer);

    uint64_t total_size = total_sectors * sector_size;
    format_bytes(total_size, buffer, sizeof(buffer));
    print_info("Total size:", buffer);

    if(input_ctx->image_info.Application[0] != '\0')
    {
        snprintf(buffer, sizeof(buffer), "%s %s", input_ctx->image_info.Application,
                 input_ctx->image_info.ApplicationVersion);
        print_info("Created by:", buffer);
    }

    // Check and display dump hardware information
    size_t   dumphw_size    = 0;
    uint8_t *dumphw_data    = NULL;
    bool     has_dump_hw    = false;
    uint16_t dumphw_entries = 0;

    res = aaruf_get_dumphw(input_ctx, NULL, &dumphw_size);
    if(res == AARUF_ERROR_BUFFER_TOO_SMALL && dumphw_size > 0)
    {
        dumphw_data = malloc(dumphw_size);
        if(dumphw_data != NULL)
        {
            res = aaruf_get_dumphw(input_ctx, dumphw_data, &dumphw_size);
            if(res == AARUF_STATUS_OK)
            {
                has_dump_hw    = true;
                dumphw_entries = input_ctx->dump_hardware_header.entries;

                print_header("Dump Hardware Information");

                for(uint16_t i = 0; i < dumphw_entries; i++)
                {
                    DumpHardwareEntriesWithData *entry = &input_ctx->dump_hardware_entries_with_data[i];

                    if(dumphw_entries > 1)
                    {
                        snprintf(buffer, sizeof(buffer), "Hardware Set #%u", i + 1);
                        printf("  " ANSI_BOLD ANSI_WHITE "%s" ANSI_RESET "\n", buffer);
                    }

                    if(entry->manufacturer != NULL && entry->entry.manufacturerLength > 0)
                    {
                        snprintf(buffer, sizeof(buffer), "%.*s", entry->entry.manufacturerLength, entry->manufacturer);
                        print_info("  Manufacturer:", buffer);
                    }

                    if(entry->model != NULL && entry->entry.modelLength > 0)
                    {
                        snprintf(buffer, sizeof(buffer), "%.*s", entry->entry.modelLength, entry->model);
                        print_info("  Model:", buffer);
                    }

                    if(entry->revision != NULL && entry->entry.revisionLength > 0)
                    {
                        snprintf(buffer, sizeof(buffer), "%.*s", entry->entry.revisionLength, entry->revision);
                        print_info("  Revision:", buffer);
                    }

                    if(entry->firmware != NULL && entry->entry.firmwareLength > 0)
                    {
                        snprintf(buffer, sizeof(buffer), "%.*s", entry->entry.firmwareLength, entry->firmware);
                        print_info("  Firmware:", buffer);
                    }

                    if(entry->serial != NULL && entry->entry.serialLength > 0)
                    {
                        snprintf(buffer, sizeof(buffer), "%.*s", entry->entry.serialLength, entry->serial);
                        print_info("  Serial:", buffer);
                    }

                    if(entry->softwareName != NULL && entry->entry.softwareNameLength > 0)
                    {
                        char sw_info[256];
                        int  sw_len = snprintf(sw_info, sizeof(sw_info), "%.*s", entry->entry.softwareNameLength,
                                               entry->softwareName);
                        if(entry->softwareVersion != NULL && entry->entry.softwareVersionLength > 0)
                            snprintf(sw_info + sw_len, sizeof(sw_info) - sw_len, " %.*s",
                                     entry->entry.softwareVersionLength, entry->softwareVersion);
                        print_info("  Software:", sw_info);
                    }

                    if(entry->softwareOperatingSystem != NULL && entry->entry.softwareOperatingSystemLength > 0)
                    {
                        snprintf(buffer, sizeof(buffer), "%.*s", entry->entry.softwareOperatingSystemLength,
                                 entry->softwareOperatingSystem);
                        print_info("  OS:", buffer);
                    }

                    if(entry->entry.extents > 0)
                    {
                        snprintf(buffer, sizeof(buffer), "%u extent(s)", entry->entry.extents);
                        print_info("  Extents:", buffer);
                    }
                }
            }
        }
    }

    // Check for Aaru JSON metadata
    size_t   json_size = 0;
    uint8_t *json_data = NULL;
    bool     has_json  = false;

    res = aaruf_get_aaru_json_metadata(input_ctx, NULL, &json_size);
    if(res == AARUF_ERROR_BUFFER_TOO_SMALL && json_size > 0)
    {
        json_data = malloc(json_size);
        if(json_data != NULL)
        {
            res = aaruf_get_aaru_json_metadata(input_ctx, json_data, &json_size);
            if(res == AARUF_STATUS_OK)
            {
                has_json = true;
                format_bytes(json_size, buffer, sizeof(buffer));
                print_header("Aaru JSON Metadata");
                print_info("Size:", buffer);
                print_success("Aaru JSON metadata present");
            }
        }
    }

    // Check for basic metadata
    int32_t  meta_sequence          = 0;
    int32_t  meta_last_sequence     = 0;
    bool     has_sequence           = false;
    int32_t  meta_length            = 0;
    uint8_t *meta_creator           = NULL;
    int32_t  meta_creator_len       = 0;
    uint8_t *meta_comments          = NULL;
    int32_t  meta_comments_len      = 0;
    uint8_t *meta_title             = NULL;
    int32_t  meta_title_len         = 0;
    uint8_t *meta_media_mfr         = NULL;
    int32_t  meta_media_mfr_len     = 0;
    uint8_t *meta_media_model       = NULL;
    int32_t  meta_media_model_len   = 0;
    uint8_t *meta_media_serial      = NULL;
    int32_t  meta_media_serial_len  = 0;
    uint8_t *meta_media_barcode     = NULL;
    int32_t  meta_media_barcode_len = 0;
    uint8_t *meta_media_partno      = NULL;
    int32_t  meta_media_partno_len  = 0;
    uint8_t *meta_drive_mfr         = NULL;
    int32_t  meta_drive_mfr_len     = 0;
    uint8_t *meta_drive_model       = NULL;
    int32_t  meta_drive_model_len   = 0;
    uint8_t *meta_drive_serial      = NULL;
    int32_t  meta_drive_serial_len  = 0;
    uint8_t *meta_drive_fw          = NULL;
    int32_t  meta_drive_fw_len      = 0;
    bool     has_basic_metadata     = false;

    // Check sequence
    if(aaruf_get_media_sequence(input_ctx, &meta_sequence, &meta_last_sequence) == AARUF_STATUS_OK && meta_sequence > 0)
    {
        has_sequence       = true;
        has_basic_metadata = true;
    }

    // Check and get creator
    meta_length = 0;
    if(aaruf_get_creator(input_ctx, NULL, &meta_length) == AARUF_ERROR_BUFFER_TOO_SMALL && meta_length > 0)
    {
        meta_creator = malloc(meta_length);
        if(meta_creator != NULL)
        {
            meta_creator_len = meta_length;
            if(aaruf_get_creator(input_ctx, meta_creator, &meta_creator_len) != AARUF_STATUS_OK)
            {
                free(meta_creator);
                meta_creator     = NULL;
                meta_creator_len = 0;
            }
            else
                has_basic_metadata = true;
        }
    }

    // Check and get comments
    meta_length = 0;
    if(aaruf_get_comments(input_ctx, NULL, &meta_length) == AARUF_ERROR_BUFFER_TOO_SMALL && meta_length > 0)
    {
        meta_comments = malloc(meta_length);
        if(meta_comments != NULL)
        {
            meta_comments_len = meta_length;
            if(aaruf_get_comments(input_ctx, meta_comments, &meta_comments_len) != AARUF_STATUS_OK)
            {
                free(meta_comments);
                meta_comments     = NULL;
                meta_comments_len = 0;
            }
            else
                has_basic_metadata = true;
        }
    }

    // Check and get media title
    meta_length = 0;
    if(aaruf_get_media_title(input_ctx, NULL, &meta_length) == AARUF_ERROR_BUFFER_TOO_SMALL && meta_length > 0)
    {
        meta_title = malloc(meta_length);
        if(meta_title != NULL)
        {
            meta_title_len = meta_length;
            if(aaruf_get_media_title(input_ctx, meta_title, &meta_title_len) != AARUF_STATUS_OK)
            {
                free(meta_title);
                meta_title     = NULL;
                meta_title_len = 0;
            }
            else
                has_basic_metadata = true;
        }
    }

    // Check and get media manufacturer
    meta_length = 0;
    if(aaruf_get_media_manufacturer(input_ctx, NULL, &meta_length) == AARUF_ERROR_BUFFER_TOO_SMALL && meta_length > 0)
    {
        meta_media_mfr = malloc(meta_length);
        if(meta_media_mfr != NULL)
        {
            meta_media_mfr_len = meta_length;
            if(aaruf_get_media_manufacturer(input_ctx, meta_media_mfr, &meta_media_mfr_len) != AARUF_STATUS_OK)
            {
                free(meta_media_mfr);
                meta_media_mfr     = NULL;
                meta_media_mfr_len = 0;
            }
            else
                has_basic_metadata = true;
        }
    }

    // Check and get media model
    meta_length = 0;
    if(aaruf_get_media_model(input_ctx, NULL, &meta_length) == AARUF_ERROR_BUFFER_TOO_SMALL && meta_length > 0)
    {
        meta_media_model = malloc(meta_length);
        if(meta_media_model != NULL)
        {
            meta_media_model_len = meta_length;
            if(aaruf_get_media_model(input_ctx, meta_media_model, &meta_media_model_len) != AARUF_STATUS_OK)
            {
                free(meta_media_model);
                meta_media_model     = NULL;
                meta_media_model_len = 0;
            }
            else
                has_basic_metadata = true;
        }
    }

    // Check and get media serial number
    meta_length = 0;
    if(aaruf_get_media_serial_number(input_ctx, NULL, &meta_length) == AARUF_ERROR_BUFFER_TOO_SMALL && meta_length > 0)
    {
        meta_media_serial = malloc(meta_length);
        if(meta_media_serial != NULL)
        {
            meta_media_serial_len = meta_length;
            if(aaruf_get_media_serial_number(input_ctx, meta_media_serial, &meta_media_serial_len) != AARUF_STATUS_OK)
            {
                free(meta_media_serial);
                meta_media_serial     = NULL;
                meta_media_serial_len = 0;
            }
            else
                has_basic_metadata = true;
        }
    }

    // Check and get media barcode
    meta_length = 0;
    if(aaruf_get_media_barcode(input_ctx, NULL, &meta_length) == AARUF_ERROR_BUFFER_TOO_SMALL && meta_length > 0)
    {
        meta_media_barcode = malloc(meta_length);
        if(meta_media_barcode != NULL)
        {
            meta_media_barcode_len = meta_length;
            if(aaruf_get_media_barcode(input_ctx, meta_media_barcode, &meta_media_barcode_len) != AARUF_STATUS_OK)
            {
                free(meta_media_barcode);
                meta_media_barcode     = NULL;
                meta_media_barcode_len = 0;
            }
            else
                has_basic_metadata = true;
        }
    }

    // Check and get media part number
    meta_length = 0;
    if(aaruf_get_media_part_number(input_ctx, NULL, &meta_length) == AARUF_ERROR_BUFFER_TOO_SMALL && meta_length > 0)
    {
        meta_media_partno = malloc(meta_length);
        if(meta_media_partno != NULL)
        {
            meta_media_partno_len = meta_length;
            if(aaruf_get_media_part_number(input_ctx, meta_media_partno, &meta_media_partno_len) != AARUF_STATUS_OK)
            {
                free(meta_media_partno);
                meta_media_partno     = NULL;
                meta_media_partno_len = 0;
            }
            else
                has_basic_metadata = true;
        }
    }

    // Check and get drive manufacturer
    meta_length = 0;
    if(aaruf_get_drive_manufacturer(input_ctx, NULL, &meta_length) == AARUF_ERROR_BUFFER_TOO_SMALL && meta_length > 0)
    {
        meta_drive_mfr = malloc(meta_length);
        if(meta_drive_mfr != NULL)
        {
            meta_drive_mfr_len = meta_length;
            if(aaruf_get_drive_manufacturer(input_ctx, meta_drive_mfr, &meta_drive_mfr_len) != AARUF_STATUS_OK)
            {
                free(meta_drive_mfr);
                meta_drive_mfr     = NULL;
                meta_drive_mfr_len = 0;
            }
            else
                has_basic_metadata = true;
        }
    }

    // Check and get drive model
    meta_length = 0;
    if(aaruf_get_drive_model(input_ctx, NULL, &meta_length) == AARUF_ERROR_BUFFER_TOO_SMALL && meta_length > 0)
    {
        meta_drive_model = malloc(meta_length);
        if(meta_drive_model != NULL)
        {
            meta_drive_model_len = meta_length;
            if(aaruf_get_drive_model(input_ctx, meta_drive_model, &meta_drive_model_len) != AARUF_STATUS_OK)
            {
                free(meta_drive_model);
                meta_drive_model     = NULL;
                meta_drive_model_len = 0;
            }
            else
                has_basic_metadata = true;
        }
    }

    // Check and get drive serial number
    meta_length = 0;
    if(aaruf_get_drive_serial_number(input_ctx, NULL, &meta_length) == AARUF_ERROR_BUFFER_TOO_SMALL && meta_length > 0)
    {
        meta_drive_serial = malloc(meta_length);
        if(meta_drive_serial != NULL)
        {
            meta_drive_serial_len = meta_length;
            if(aaruf_get_drive_serial_number(input_ctx, meta_drive_serial, &meta_drive_serial_len) != AARUF_STATUS_OK)
            {
                free(meta_drive_serial);
                meta_drive_serial     = NULL;
                meta_drive_serial_len = 0;
            }
            else
                has_basic_metadata = true;
        }
    }

    // Check and get drive firmware revision
    meta_length = 0;
    if(aaruf_get_drive_firmware_revision(input_ctx, NULL, &meta_length) == AARUF_ERROR_BUFFER_TOO_SMALL &&
       meta_length > 0)
    {
        meta_drive_fw = malloc(meta_length);
        if(meta_drive_fw != NULL)
        {
            meta_drive_fw_len = meta_length;
            if(aaruf_get_drive_firmware_revision(input_ctx, meta_drive_fw, &meta_drive_fw_len) != AARUF_STATUS_OK)
            {
                free(meta_drive_fw);
                meta_drive_fw     = NULL;
                meta_drive_fw_len = 0;
            }
            else
                has_basic_metadata = true;
        }
    }

    // Display basic metadata if any exists
    if(has_basic_metadata)
    {
        print_header("Image Metadata");

        if(has_sequence)
        {
            snprintf(buffer, sizeof(buffer), "%d of %d", meta_sequence, meta_last_sequence);
            print_info("Media Sequence:", buffer);
        }

        if(meta_creator != NULL)
        {
            char *str = utf16le_to_utf8(meta_creator, meta_creator_len);
            if(str != NULL)
            {
                print_info("Creator:", str);
                free(str);
            }
        }

        if(meta_comments != NULL)
        {
            char *str = utf16le_to_utf8(meta_comments, meta_comments_len);
            if(str != NULL)
            {
                print_info("Comments:", str);
                free(str);
            }
        }

        if(meta_title != NULL)
        {
            char *str = utf16le_to_utf8(meta_title, meta_title_len);
            if(str != NULL)
            {
                print_info("Media Title:", str);
                free(str);
            }
        }

        if(meta_media_mfr != NULL)
        {
            char *str = utf16le_to_utf8(meta_media_mfr, meta_media_mfr_len);
            if(str != NULL)
            {
                print_info("Media Manufacturer:", str);
                free(str);
            }
        }

        if(meta_media_model != NULL)
        {
            char *str = utf16le_to_utf8(meta_media_model, meta_media_model_len);
            if(str != NULL)
            {
                print_info("Media Model:", str);
                free(str);
            }
        }

        if(meta_media_serial != NULL)
        {
            char *str = utf16le_to_utf8(meta_media_serial, meta_media_serial_len);
            if(str != NULL)
            {
                print_info("Media Serial:", str);
                free(str);
            }
        }

        if(meta_media_barcode != NULL)
        {
            char *str = utf16le_to_utf8(meta_media_barcode, meta_media_barcode_len);
            if(str != NULL)
            {
                print_info("Media Barcode:", str);
                free(str);
            }
        }

        if(meta_media_partno != NULL)
        {
            char *str = utf16le_to_utf8(meta_media_partno, meta_media_partno_len);
            if(str != NULL)
            {
                print_info("Media Part Number:", str);
                free(str);
            }
        }

        if(meta_drive_mfr != NULL)
        {
            char *str = utf16le_to_utf8(meta_drive_mfr, meta_drive_mfr_len);
            if(str != NULL)
            {
                print_info("Drive Manufacturer:", str);
                free(str);
            }
        }

        if(meta_drive_model != NULL)
        {
            char *str = utf16le_to_utf8(meta_drive_model, meta_drive_model_len);
            if(str != NULL)
            {
                print_info("Drive Model:", str);
                free(str);
            }
        }

        if(meta_drive_serial != NULL)
        {
            char *str = utf16le_to_utf8(meta_drive_serial, meta_drive_serial_len);
            if(str != NULL)
            {
                print_info("Drive Serial:", str);
                free(str);
            }
        }

        if(meta_drive_fw != NULL)
        {
            char *str = utf16le_to_utf8(meta_drive_fw, meta_drive_fw_len);
            if(str != NULL)
            {
                print_info("Drive Firmware:", str);
                free(str);
            }
        }
    }

    // Enumerate media tags
    size_t   media_tags_size  = 0;
    uint8_t *media_tags_map   = NULL;
    int      media_tags_count = 0;

    res = aaruf_get_readable_media_tags(input_ctx, NULL, &media_tags_size);
    if(res == AARUF_ERROR_BUFFER_TOO_SMALL && media_tags_size > 0)
    {
        media_tags_map = malloc(media_tags_size);
        if(media_tags_map != NULL)
        {
            res = aaruf_get_readable_media_tags(input_ctx, media_tags_map, &media_tags_size);
            if(res == AARUF_STATUS_OK)
            {
                // Count how many media tags are present
                for(size_t i = 0; i < media_tags_size; i++)
                    if(media_tags_map[i]) media_tags_count++;

                if(media_tags_count > 0)
                {
                    print_header("Media Tags");
                    snprintf(buffer, sizeof(buffer), "%d media tag(s) present", media_tags_count);
                    print_info("Count:", buffer);
                    printf("\n");

                    // List all present media tags
                    for(size_t i = 0; i < media_tags_size; i++)
                        if(media_tags_map[i])
                        {
                            const char *tag_name = media_tag_type_to_string((int32_t)i);
                            printf("    " ANSI_GREEN "•" ANSI_RESET " %s\n", tag_name ? tag_name : "Unknown");
                        }
                }
            }
        }
    }

    // Check geometry for BlockMedia
    uint32_t src_cylinders         = 0;
    uint32_t src_heads             = 0;
    uint32_t src_sectors_per_track = 0;
    bool     has_geometry          = false;

    if(input_ctx->image_info.MetadataMediaType == BlockMedia)
    {
        res = aaruf_get_geometry(input_ctx, &src_cylinders, &src_heads, &src_sectors_per_track);
        if(res == AARUF_STATUS_OK && (src_cylinders != 0 || src_heads != 0 || src_sectors_per_track != 0))
        {
            has_geometry = true;
            snprintf(buffer, sizeof(buffer), "%u cylinders, %u heads, %u sectors/track", src_cylinders, src_heads,
                     src_sectors_per_track);

            print_header("Geometry");
            print_info("Geometry:", buffer);
        }
    }

    const char *app_name_utf8     = "aaruformattool";
    size_t      app_name_utf8_len = strlen(app_name_utf8);

    // Create output image
    print_header("Creating Destination Image");
    uint32_t negative_sectors = 0;
    uint32_t overflow_sectors = 0;
    if(aaruf_get_negative_sectors(input_ctx, &negative_sectors) != AARUF_STATUS_OK) negative_sectors = 0;
    if(aaruf_get_overflow_sectors(input_ctx, &overflow_sectors) != AARUF_STATUS_OK) overflow_sectors = 0;

    output_ctx = aaruf_create(output_path, input_ctx->image_info.MediaType, sector_size, total_sectors,
                              negative_sectors, overflow_sectors,
                              NULL,  // options
                              app_name_utf8,
                              app_name_utf8_len,  // application name length in bytes
                              1,                  // major version
                              0,                  // minor version
                              false);

    if(output_ctx == NULL)
    {
        snprintf(buffer, sizeof(buffer), "Cannot create output image (error %d)", errno);
        print_error(buffer);
        aaruf_close(input_ctx);
        return errno;
    }
    print_success("Destination image created successfully");

    /* Enable erasure coding if requested */
    if(ec_k > 0 && ec_m > 0)
    {
        int32_t ec_res = aaruf_set_erasure_coding(output_ctx, 1 /* RS-Vandermonde */, ec_k, ec_m);
        if(ec_res != AARUF_STATUS_OK)
        {
            snprintf(buffer, sizeof(buffer), "Cannot enable erasure coding (error %d)", ec_res);
            print_error(buffer);
            aaruf_close(output_ctx);
            aaruf_close(input_ctx);
            return ec_res;
        }
        snprintf(buffer, sizeof(buffer), "RS(%u,%u) — %u data + %u parity blocks per stripe", ec_k, ec_m, ec_k, ec_m);
        print_info("Erasure coding:", buffer);
    }

    // Copy tracks if present
    size_t tracks_size = 0;
    res                = aaruf_get_tracks(input_ctx, sector_data, &tracks_size);

    if(res != AARUF_ERROR_BUFFER_TOO_SMALL && res != AARUF_ERROR_TRACK_NOT_FOUND)
    {
        snprintf(buffer, sizeof(buffer), "Cannot get tracks from input image (error %d)", res);
        print_error(buffer);
        aaruf_close(input_ctx);
        aaruf_close(output_ctx);
        return res;
    }

    if(res == AARUF_ERROR_BUFFER_TOO_SMALL)
    {
        sector_data = malloc(tracks_size);
        if(sector_data == NULL)
        {
            print_error("Cannot allocate memory for tracks buffer");
            aaruf_close(input_ctx);
            aaruf_close(output_ctx);
            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
        }

        res = aaruf_get_tracks(input_ctx, sector_data, &tracks_size);
        if(res != AARUF_STATUS_OK)
        {
            snprintf(buffer, sizeof(buffer), "Cannot read tracks from input image (error %d)", res);
            print_error(buffer);
            free(sector_data);
            aaruf_close(input_ctx);
            aaruf_close(output_ctx);
            return res;
        }

        res = aaruf_set_tracks(output_ctx, (TrackEntry *)sector_data, (int)(tracks_size / sizeof(TrackEntry)));
        if(res != AARUF_STATUS_OK)
        {
            snprintf(buffer, sizeof(buffer), "Cannot set tracks on output image (error %d)", res);
            print_error(buffer);
            free(sector_data);
            aaruf_close(input_ctx);
            aaruf_close(output_ctx);
            return res;
        }

        snprintf(buffer, sizeof(buffer), "Copied %zu track(s)", tracks_size / sizeof(TrackEntry));
        print_success(buffer);

        free(sector_data);
        sector_data = NULL;
    }

    // Allocate buffer for sector data
    sector_data = malloc(sector_size);
    if(sector_data == NULL)
    {
        print_error("Cannot allocate memory for sector buffer");
        aaruf_close(input_ctx);
        aaruf_close(output_ctx);
        return AARUF_ERROR_NOT_ENOUGH_MEMORY;
    }

    // Copy sectors from input to output
    print_header("Converting Sectors");
    printf("\n");

    start_time      = clock();
    bytes_processed = 0;
    int last_error  = AARUF_STATUS_OK;

    // Per-sector tags present in the source; in long mode only those not already part of the long sector
    const CopyableSectorTag *tags[sizeof(copyable_sector_tags) / sizeof(copyable_sector_tags[0])];
    size_t                   tag_count    = 0;
    size_t                   flags_length = 0;

    if(aaruf_get_readable_sector_tags(input_ctx, NULL, &flags_length) == AARUF_ERROR_BUFFER_TOO_SMALL)
    {
        bool *readable = calloc(1, flags_length);

        if(readable != NULL &&
           aaruf_get_readable_sector_tags(input_ctx, (uint8_t *)readable, &flags_length) == AARUF_STATUS_OK)
            for(size_t t = 0; t < sizeof(copyable_sector_tags) / sizeof(copyable_sector_tags[0]); t++)
            {
                const CopyableSectorTag *tag = &copyable_sector_tags[t];
                if((size_t)tag->tag < flags_length / sizeof(bool) && readable[tag->tag] &&
                   !(use_long && tag->in_long_sector))
                    tags[tag_count++] = tag;
            }

        free(readable);
    }

    // Negative sectors (from the most negative), the user area, then overflow sectors
    const uint64_t addresses  = negative_sectors + total_sectors + overflow_sectors;
    CopyStatistics statistics = {0};

    for(uint64_t i = 0; i < addresses; i++)
    {
        const bool     negative = i < negative_sectors;
        const uint64_t sector   = negative ? negative_sectors - i : i - negative_sectors;

        // Update progress every 100 sectors or on last sector
        if(i % 100 == 0 || i == addresses - 1)
        {
            current_time          = clock();
            double elapsed        = (double)(current_time - start_time) / CLOCKS_PER_SEC;
            double percentage     = (double)(i + 1) / addresses * 100.0;
            double speed          = elapsed > 0 ? statistics.bytes / elapsed : 0;
            double remaining_secs = speed > 0 ? ((total_size - statistics.bytes) / speed) : 0;

            draw_progress_bar(percentage, speed, elapsed, remaining_secs);
        }

        res = copy_sector(input_ctx, output_ctx, sector, negative, use_long, &sector_data, &sector_size, tags,
                          tag_count, &statistics);

        if(res != AARUF_STATUS_OK)
        {
            printf("\n");
            snprintf(buffer, sizeof(buffer), "Error copying sector %s%llu (error %d)", negative ? "-" : "",
                     (unsigned long long)sector, res);
            print_error(buffer);
            last_error = res;
            break;
        }
    }

    bytes_processed = statistics.bytes;

    printf("\n\n");

    if(negative_sectors > 0 || overflow_sectors > 0)
    {
        snprintf(buffer, sizeof(buffer), "Copied %u negative and %u overflow sector(s)", negative_sectors,
                 overflow_sectors);
        print_success(buffer);
    }

    if(statistics.twins > 0)
    {
        snprintf(buffer, sizeof(buffer), "Copied %llu twin instance(s)", (unsigned long long)statistics.twins);
        print_success(buffer);
    }

    if(statistics.tags > 0)
    {
        snprintf(buffer, sizeof(buffer), "Copied %llu sector tag(s)", (unsigned long long)statistics.tags);
        print_success(buffer);
    }

    if(statistics.not_dumped > 0)
    {
        snprintf(buffer, sizeof(buffer), "%llu sector(s) were not dumped", (unsigned long long)statistics.not_dumped);
        print_info("Not dumped:", buffer);
    }

    // Copy tracks again (in case they were modified during sector copy)
    size_t tracks_length = 0;
    res                  = aaruf_get_tracks(input_ctx, NULL, &tracks_length);
    if(res != AARUF_ERROR_TRACK_NOT_FOUND && res == AARUF_ERROR_BUFFER_TOO_SMALL)
    {
        uint8_t *tracks = calloc(1, tracks_length);

        res = aaruf_get_tracks(input_ctx, tracks, &tracks_length);
        if(res == AARUF_STATUS_OK)
        {
            res = aaruf_set_tracks(output_ctx, (TrackEntry *)tracks, (int)(tracks_length / sizeof(TrackEntry)));
            if(res != AARUF_STATUS_OK)
            {
                snprintf(buffer, sizeof(buffer), "Warning: Could not finalize tracks (error %d)", res);
                print_warning(buffer);
            }
        }
        free(tracks);
    }

    // Copy geometry if source is BlockMedia and has valid geometry
    if(has_geometry)
    {
        res = aaruf_set_geometry(output_ctx, src_cylinders, src_heads, src_sectors_per_track);
        if(res == AARUF_STATUS_OK)
            print_success("Geometry copied to destination image");
        else
        {
            snprintf(buffer, sizeof(buffer), "Warning: Could not copy geometry (error %d)", res);
            print_warning(buffer);
        }
    }

    // Copy dump hardware if available
    if(has_dump_hw && dumphw_data != NULL)
    {
        res = aaruf_set_dumphw(output_ctx, dumphw_data, dumphw_size);
        if(res == AARUF_STATUS_OK)
        {
            snprintf(buffer, sizeof(buffer), "Dump hardware information copied (%u set(s))", dumphw_entries);
            print_success(buffer);
        }
        else
        {
            snprintf(buffer, sizeof(buffer), "Warning: Could not copy dump hardware (error %d)", res);
            print_warning(buffer);
        }
    }

    // Copy Aaru JSON metadata if available
    if(has_json && json_data != NULL)
    {
        res = aaruf_set_aaru_json_metadata(output_ctx, json_data, json_size);
        if(res == AARUF_STATUS_OK)
        {
            format_bytes(json_size, buffer, sizeof(buffer));
            char msg[128];
            snprintf(msg, sizeof(msg), "Aaru JSON metadata copied (%s)", buffer);
            print_success(msg);
        }
        else
        {
            snprintf(buffer, sizeof(buffer), "Warning: Could not copy Aaru JSON metadata (error %d)", res);
            print_warning(buffer);
        }
    }

    // Copy media tags if any are present
    if(media_tags_count > 0 && media_tags_map != NULL)
    {
        int tags_copied = 0;
        int tags_failed = 0;

        for(size_t i = 0; i < media_tags_size; i++)
        {
            if(!media_tags_map[i]) continue;

            // First, query the size of this media tag
            uint32_t tag_length = 0;
            res                 = aaruf_read_media_tag(input_ctx, NULL, (int32_t)i, &tag_length);
            if(res != AARUF_ERROR_BUFFER_TOO_SMALL || tag_length == 0)
            {
                tags_failed++;
                continue;
            }

            // Allocate buffer and read the tag data
            uint8_t *tag_data = malloc(tag_length);
            if(tag_data == NULL)
            {
                tags_failed++;
                continue;
            }

            res = aaruf_read_media_tag(input_ctx, tag_data, (int32_t)i, &tag_length);
            if(res != AARUF_STATUS_OK)
            {
                free(tag_data);
                tags_failed++;
                continue;
            }

            // Write the tag to the destination image
            res = aaruf_write_media_tag(output_ctx, tag_data, (int32_t)i, tag_length);
            free(tag_data);

            if(res == AARUF_STATUS_OK)
                tags_copied++;
            else
                tags_failed++;
        }

        if(tags_copied > 0)
        {
            snprintf(buffer, sizeof(buffer), "Media tags copied (%d of %d)", tags_copied, media_tags_count);
            print_success(buffer);
        }
        if(tags_failed > 0)
        {
            snprintf(buffer, sizeof(buffer), "Warning: %d media tag(s) could not be copied", tags_failed);
            print_warning(buffer);
        }
    }

    // Copy basic metadata if available
    if(has_basic_metadata)
    {
        int meta_copied = 0;
        int meta_failed = 0;

        // Copy media sequence
        if(has_sequence)
        {
            res = aaruf_set_media_sequence(output_ctx, meta_sequence, meta_last_sequence);
            if(res == AARUF_STATUS_OK)
                meta_copied++;
            else
                meta_failed++;
        }

        // Copy creator
        if(meta_creator != NULL)
        {
            res = aaruf_set_creator(output_ctx, meta_creator, meta_creator_len);
            if(res == AARUF_STATUS_OK)
                meta_copied++;
            else
                meta_failed++;
        }

        // Copy comments
        if(meta_comments != NULL)
        {
            res = aaruf_set_comments(output_ctx, meta_comments, meta_comments_len);
            if(res == AARUF_STATUS_OK)
                meta_copied++;
            else
                meta_failed++;
        }

        // Copy media title
        if(meta_title != NULL)
        {
            res = aaruf_set_media_title(output_ctx, meta_title, meta_title_len);
            if(res == AARUF_STATUS_OK)
                meta_copied++;
            else
                meta_failed++;
        }

        // Copy media manufacturer
        if(meta_media_mfr != NULL)
        {
            res = aaruf_set_media_manufacturer(output_ctx, meta_media_mfr, meta_media_mfr_len);
            if(res == AARUF_STATUS_OK)
                meta_copied++;
            else
                meta_failed++;
        }

        // Copy media model
        if(meta_media_model != NULL)
        {
            res = aaruf_set_media_model(output_ctx, meta_media_model, meta_media_model_len);
            if(res == AARUF_STATUS_OK)
                meta_copied++;
            else
                meta_failed++;
        }

        // Copy media serial number
        if(meta_media_serial != NULL)
        {
            res = aaruf_set_media_serial_number(output_ctx, meta_media_serial, meta_media_serial_len);
            if(res == AARUF_STATUS_OK)
                meta_copied++;
            else
                meta_failed++;
        }

        // Copy media barcode
        if(meta_media_barcode != NULL)
        {
            res = aaruf_set_media_barcode(output_ctx, meta_media_barcode, meta_media_barcode_len);
            if(res == AARUF_STATUS_OK)
                meta_copied++;
            else
                meta_failed++;
        }

        // Copy media part number
        if(meta_media_partno != NULL)
        {
            res = aaruf_set_media_part_number(output_ctx, meta_media_partno, meta_media_partno_len);
            if(res == AARUF_STATUS_OK)
                meta_copied++;
            else
                meta_failed++;
        }

        // Copy drive manufacturer
        if(meta_drive_mfr != NULL)
        {
            res = aaruf_set_drive_manufacturer(output_ctx, meta_drive_mfr, meta_drive_mfr_len);
            if(res == AARUF_STATUS_OK)
                meta_copied++;
            else
                meta_failed++;
        }

        // Copy drive model
        if(meta_drive_model != NULL)
        {
            res = aaruf_set_drive_model(output_ctx, meta_drive_model, meta_drive_model_len);
            if(res == AARUF_STATUS_OK)
                meta_copied++;
            else
                meta_failed++;
        }

        // Copy drive serial number
        if(meta_drive_serial != NULL)
        {
            res = aaruf_set_drive_serial_number(output_ctx, meta_drive_serial, meta_drive_serial_len);
            if(res == AARUF_STATUS_OK)
                meta_copied++;
            else
                meta_failed++;
        }

        // Copy drive firmware revision
        if(meta_drive_fw != NULL)
        {
            res = aaruf_set_drive_firmware_revision(output_ctx, meta_drive_fw, meta_drive_fw_len);
            if(res == AARUF_STATUS_OK)
                meta_copied++;
            else
                meta_failed++;
        }

        if(meta_copied > 0)
        {
            snprintf(buffer, sizeof(buffer), "Basic metadata copied (%d field(s))", meta_copied);
            print_success(buffer);
        }
        if(meta_failed > 0)
        {
            snprintf(buffer, sizeof(buffer), "Warning: %d metadata field(s) could not be copied", meta_failed);
            print_warning(buffer);
        }
    }

    // Free dump hardware data if allocated
    if(dumphw_data != NULL)
    {
        free(dumphw_data);
        dumphw_data = NULL;
    }

    // Free JSON data if allocated
    if(json_data != NULL)
    {
        free(json_data);
        json_data = NULL;
    }

    // Free media tags map if allocated
    if(media_tags_map != NULL)
    {
        free(media_tags_map);
        media_tags_map = NULL;
    }

    // Free basic metadata buffers if allocated
    if(meta_creator != NULL) free(meta_creator);
    if(meta_comments != NULL) free(meta_comments);
    if(meta_title != NULL) free(meta_title);
    if(meta_media_mfr != NULL) free(meta_media_mfr);
    if(meta_media_model != NULL) free(meta_media_model);
    if(meta_media_serial != NULL) free(meta_media_serial);
    if(meta_media_barcode != NULL) free(meta_media_barcode);
    if(meta_media_partno != NULL) free(meta_media_partno);
    if(meta_drive_mfr != NULL) free(meta_drive_mfr);
    if(meta_drive_model != NULL) free(meta_drive_model);
    if(meta_drive_serial != NULL) free(meta_drive_serial);
    if(meta_drive_fw != NULL) free(meta_drive_fw);

    // Calculate final statistics
    clock_t end_time      = clock();
    double  total_elapsed = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    double  avg_speed     = total_elapsed > 0 ? bytes_processed / total_elapsed : 0;
    char    speed_str[32];
    char    elapsed_str[32];
    char    processed_str[32];

    format_bytes((uint64_t)avg_speed, speed_str, sizeof(speed_str));
    format_duration(total_elapsed, elapsed_str, sizeof(elapsed_str));
    format_bytes(bytes_processed, processed_str, sizeof(processed_str));

    // Print summary
    print_header("Conversion Summary");
    print_info("Data processed:", processed_str);
    print_info("Time elapsed:", elapsed_str);
    snprintf(buffer, sizeof(buffer), "%s/sec", speed_str);
    print_info("Average speed:", buffer);
    
    // Copy flux captures from input to output
    size_t flux_captures_length = 0;
    res                         = aaruf_get_flux_captures(input_ctx, NULL, &flux_captures_length);
    if(res == AARUF_ERROR_BUFFER_TOO_SMALL)
    {
        uint8_t *flux_captures = malloc(flux_captures_length);
        if(flux_captures == NULL)
        {
            printf("\nError allocating memory for flux captures buffer.\n");
            free(sector_data);
            aaruf_close(input_ctx);
            aaruf_close(output_ctx);
            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
        }

        res = aaruf_get_flux_captures(input_ctx, flux_captures, &flux_captures_length);
        if(res == AARUF_STATUS_OK)
        {
            size_t                 capture_count = flux_captures_length / sizeof(FluxCaptureMeta);
            const FluxCaptureMeta *captures      = (FluxCaptureMeta *)flux_captures;
            uint8_t               *index_data    = NULL;
            uint8_t               *data_data     = NULL;
            uint32_t               index_length  = 0;
            uint32_t               data_length   = 0;

            printf("Copying %zu flux captures...\n", capture_count);

            for(size_t i = 0; i < capture_count; i++)
            {
                // First call to get required buffer sizes
                res = aaruf_read_flux_capture(input_ctx, captures[i].head, captures[i].track, captures[i].subtrack,
                                               captures[i].captureIndex, NULL, &index_length, NULL, &data_length);

                if(res == AARUF_ERROR_BUFFER_TOO_SMALL)
                {
                    // Allocate buffers
                    if(index_length > 0)
                    {
                        index_data = malloc(index_length);
                        if(index_data == NULL)
                        {
                            printf("Error allocating memory for flux index buffer.\n");
                            free(flux_captures);
                            free(sector_data);
                            aaruf_close(input_ctx);
                            aaruf_close(output_ctx);
                            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                        }
                    }

                    if(data_length > 0)
                    {
                        data_data = malloc(data_length);
                        if(data_data == NULL)
                        {
                            printf("Error allocating memory for flux data buffer.\n");
                            free(index_data);
                            free(flux_captures);
                            free(sector_data);
                            aaruf_close(input_ctx);
                            aaruf_close(output_ctx);
                            return AARUF_ERROR_NOT_ENOUGH_MEMORY;
                        }
                    }

                    // Read flux capture data
                    res = aaruf_read_flux_capture(input_ctx, captures[i].head, captures[i].track, captures[i].subtrack,
                                                   captures[i].captureIndex, index_data, &index_length, data_data,
                                                   &data_length);

                    if(res == AARUF_STATUS_OK)
                    {
                        // Write flux capture to output
                        res = aaruf_write_flux_capture(output_ctx, captures[i].head, captures[i].track,
                                                        captures[i].subtrack, captures[i].captureIndex,
                                                        captures[i].dataResolution, captures[i].indexResolution, data_data,
                                                        data_length, index_data, index_length);

                        if(res != AARUF_STATUS_OK)
                        {
                            printf("Error %d when writing flux capture (head=%u, track=%u, subtrack=%u, index=%u) to output image.\n",
                                   res, captures[i].head, captures[i].track, captures[i].subtrack,
                                   captures[i].captureIndex);
                            free(index_data);
                            free(data_data);
                            free(flux_captures);
                            free(sector_data);
                            aaruf_close(input_ctx);
                            aaruf_close(output_ctx);
                            return res;
                        }
                    }
                    else
                    {
                        printf("Error %d when reading flux capture (head=%u, track=%u, subtrack=%u, index=%u) from input image.\n",
                               res, captures[i].head, captures[i].track, captures[i].subtrack, captures[i].captureIndex);
                        free(index_data);
                        free(data_data);
                        free(flux_captures);
                        free(sector_data);
                        aaruf_close(input_ctx);
                        aaruf_close(output_ctx);
                        return res;
                    }

                    free(index_data);
                    free(data_data);
                    index_data  = NULL;
                    data_data   = NULL;
                    index_length = 0;
                    data_length  = 0;
                }
                else if(res != AARUF_ERROR_FLUX_DATA_NOT_FOUND)
                {
                    printf("Error %d when reading flux capture (head=%u, track=%u, subtrack=%u, index=%u) from input image.\n",
                           res, captures[i].head, captures[i].track, captures[i].subtrack, captures[i].captureIndex);
                    free(flux_captures);
                    free(sector_data);
                    aaruf_close(input_ctx);
                    aaruf_close(output_ctx);
                    return res;
                }
            }

            printf("Successfully copied %zu flux captures.\n", capture_count);
        }
        else if(res != AARUF_ERROR_FLUX_DATA_NOT_FOUND)
        {
            printf("\nError %d when getting flux captures from input image.\n", res);
        }

        free(flux_captures);
    }
    else if(res != AARUF_ERROR_FLUX_DATA_NOT_FOUND)
    {
        printf("\nError %d when getting flux captures from input image.\n", res);
    }

    printf("\n");

    // Clean up
    free(sector_data);
    aaruf_close(input_ctx);
    res = aaruf_close(output_ctx);

    printf("\n");

    if(res == AARUF_STATUS_OK && last_error == AARUF_STATUS_OK)
    {
        printf(ANSI_GREEN ANSI_BOLD
               "  ══════════════════════════════════════════════════════════════════════════════\n");
        printf("                          ✓ CONVERSION COMPLETED SUCCESSFULLY\n");
        printf("  ══════════════════════════════════════════════════════════════════════════════" ANSI_RESET "\n\n");
    }
    else
    {
        snprintf(buffer, sizeof(buffer), "Conversion failed with error %d", res != AARUF_STATUS_OK ? res : last_error);
        printf(ANSI_RED ANSI_BOLD "  ══════════════════════════════════════════════════════════════════════════════\n");
        printf("                             ✗ CONVERSION FAILED\n");
        printf("  ══════════════════════════════════════════════════════════════════════════════" ANSI_RESET "\n");
        print_error(buffer);
        printf("\n");
    }

    return res != AARUF_STATUS_OK ? res : last_error;
}
