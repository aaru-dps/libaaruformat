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
    {
        if(i < filled)
            printf(ANSI_GREEN "█");
        else if(i == filled)
            printf(ANSI_YELLOW "▓");
        else
            printf(ANSI_WHITE "░");
    }

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

int convert(const char *input_path, const char *output_path, bool use_long)
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
                        {
                            snprintf(sw_info + sw_len, sizeof(sw_info) - sw_len, " %.*s",
                                     entry->entry.softwareVersionLength, entry->softwareVersion);
                        }
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
    output_ctx = aaruf_create(output_path, input_ctx->image_info.MediaType, sector_size, total_sectors,
                              0,     // negative sectors
                              0,     // overflow sectors
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

    for(uint64_t sector = 0; sector < total_sectors; sector++)
    {
        uint32_t read_length = 0;

        // Update progress every 100 sectors or on last sector
        if(sector % 100 == 0 || sector == total_sectors - 1)
        {
            current_time          = clock();
            double elapsed        = (double)(current_time - start_time) / CLOCKS_PER_SEC;
            double percentage     = (double)(sector + 1) / total_sectors * 100.0;
            double speed          = elapsed > 0 ? bytes_processed / elapsed : 0;
            double remaining_secs = speed > 0 ? ((total_size - bytes_processed) / speed) : 0;

            draw_progress_bar(percentage, speed, elapsed, remaining_secs);
        }

        // Check sector size
        if(use_long)
            res = aaruf_read_sector_long(input_ctx, sector, false, sector_data, &read_length, &sector_status);
        else
            res = aaruf_read_sector(input_ctx, sector, false, sector_data, &read_length, &sector_status);

        if(res != AARUF_ERROR_BUFFER_TOO_SMALL)
        {
            printf("\n");
            snprintf(buffer, sizeof(buffer), "Error reading sector %llu (error %d)", (unsigned long long)sector, res);
            print_error(buffer);
            last_error = res;
            break;
        }

        if(sector_size < read_length)
        {
            free(sector_data);
            sector_size = read_length;
            sector_data = malloc(sector_size);
            if(sector_data == NULL)
            {
                printf("\n");
                print_error("Cannot allocate memory for larger sector buffer");
                aaruf_close(input_ctx);
                aaruf_close(output_ctx);
                return AARUF_ERROR_NOT_ENOUGH_MEMORY;
            }
        }

        // Read sector from input
        if(use_long)
            res = aaruf_read_sector_long(input_ctx, sector, false, sector_data, &read_length, &sector_status);
        else
            res = aaruf_read_sector(input_ctx, sector, false, sector_data, &read_length, &sector_status);

        if(res != AARUF_STATUS_OK)
        {
            printf("\n");
            snprintf(buffer, sizeof(buffer), "Error reading sector %llu (error %d)", (unsigned long long)sector, res);
            print_error(buffer);
            last_error = res;
            break;
        }

        // Write sector to output
        if(use_long)
            res = aaruf_write_sector_long(output_ctx, sector, false, sector_data, sector_status, read_length);
        else
            res = aaruf_write_sector(output_ctx, sector, false, sector_data, sector_status, read_length);

        if(res != AARUF_STATUS_OK)
        {
            printf("\n");
            snprintf(buffer, sizeof(buffer), "Error writing sector %llu (error %d)", (unsigned long long)sector, res);
            print_error(buffer);
            last_error = res;
            break;
        }

        bytes_processed += read_length;
    }

    printf("\n\n");

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

    // Free dump hardware data if allocated
    if(dumphw_data != NULL)
    {
        free(dumphw_data);
        dumphw_data = NULL;
    }

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
