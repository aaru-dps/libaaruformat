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
#include <locale.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unicode/ucnv.h>
#include <unicode/ustring.h>

#if defined(_WIN32) || defined(_WIN64)
#include <io.h>
#ifndef isatty
#define isatty _isatty
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO _fileno(stdout)
#endif
#else
#include <unistd.h>
#endif

#include <aaruformat.h>
#include <sys/types.h>

#include "aaruformattool.h"

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

// ANSI color codes
#define ANSI_RESET  "\033[0m"
#define ANSI_BOLD   "\033[1m"
#define ANSI_CYAN   "\033[36m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_WHITE  "\033[37m"
#define ANSI_BLUE   "\033[34m"
#define ANSI_GREEN  "\033[32m"
#define ANSI_RED    "\033[31m"

// Color pair definitions
#define COLOR_HEADER  1
#define COLOR_LABEL   2
#define COLOR_VALUE   3
#define COLOR_BOX     4
#define COLOR_TITLE   5
#define COLOR_ERROR   6
#define COLOR_SUCCESS 7

// Check if we're in a terminal that supports colors
static bool use_colors = false;

// Initialize color support
static void init_display(void)
{
    // Check if stdout is a terminal and TERM is set
    if(isatty(STDOUT_FILENO))
    {
        const char *term = getenv("TERM");
        // Enable colors if we have a terminal that likely supports them
        // If TERM is not set but we're in a TTY, assume color support (common on macOS)
        if(term == NULL)
            // Default to enabling colors if we're in a TTY
            use_colors = true;
        else if(strstr(term, "color") != NULL || strstr(term, "xterm") != NULL || strstr(term, "screen") != NULL ||
                strstr(term, "tmux") != NULL || strcmp(term, "linux") == 0 || strcmp(term, "vt100") == 0)
            use_colors = true;
        // Check for explicit no-color requests
        if(getenv("NO_COLOR") != NULL) use_colors = false;
    }
}

// Cleanup display (no-op for ANSI colors)
static void cleanup_display(void)
{
    // Reset colors on exit
    if(use_colors)
    {
        printf(ANSI_RESET);
        fflush(stdout);
    }
}

// Draw box top border with title
static void draw_box_top(const char *title, int color_pair)
{
    if(use_colors)
        // Select color based on color pair
        switch(color_pair)
        {
            case COLOR_HEADER:
                printf(ANSI_CYAN ANSI_BOLD);
                break;
            case COLOR_BOX:
                printf(ANSI_BLUE ANSI_BOLD);
                break;
            default:
                printf(ANSI_BOLD);
                break;
        }

    printf("┌─ %s ", title);

    // Calculate padding needed
    int title_len = strlen(title);
    int padding   = 80 - 5 - title_len - 1;  // 5 = "┌─ " + " ", -1 for "┐"

    for(int i = 0; i < padding; i++) printf("─");
    printf("┐\n");

    if(use_colors) printf(ANSI_RESET);
}

// Draw box bottom border
static void draw_box_bottom(int color_pair)
{
    if(use_colors)
        // Select color based on color pair
        switch(color_pair)
        {
            case COLOR_HEADER:
                printf(ANSI_CYAN ANSI_BOLD);
                break;
            case COLOR_BOX:
                printf(ANSI_BLUE ANSI_BOLD);
                break;
            default:
                printf(ANSI_BOLD);
                break;
        }

    printf("└");
    for(int i = 0; i < 77; i++) printf("─");
    printf("┘\n\n");

    if(use_colors) printf(ANSI_RESET);
}

// Print a formatted field with label and value, with proper padding
static void print_field(const char *label, const char *value, int label_width)
{
    char line[78];
    snprintf(line, sizeof(line), "%-*s %s", label_width, label, value);

    if(use_colors)
    {
        printf(ANSI_BLUE "│ " ANSI_RESET);
        printf(ANSI_YELLOW "%-*s" ANSI_RESET, label_width, label);
        printf(" ");

        int value_width = 76 - label_width - 1;  // 76 = 78 - 2 (borders), -1 for space
        printf(ANSI_WHITE "%-*s" ANSI_RESET, value_width, value);

        printf(ANSI_BLUE "│\n" ANSI_RESET);
    }
    else
        printf("│ %-76s │\n", line);
}

// Converts Windows FILETIME (100ns intervals since 1601-01-01) to time_t (seconds since 1970-01-01)
static const char *format_filetime(uint64_t filetime)
{
    static char    buf[64];
    const uint64_t EPOCH_DIFF = 116444736000000000ULL;
#if defined(_WIN32) || defined(_WIN64)
    FILETIME   ft;
    SYSTEMTIME st;
    ft.dwLowDateTime  = (DWORD)(filetime & 0xFFFFFFFF);
    ft.dwHighDateTime = (DWORD)(filetime >> 32);
    if(FileTimeToSystemTime(&ft, &st))
        // Format: YYYY-MM-DD HH:MM:SS
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                 st.wSecond);
    else
        snprintf(buf, sizeof(buf), "%llu", filetime);
    return buf;
#else
    time_t    t;
    struct tm tm_info;
    if(filetime < EPOCH_DIFF)
    {
        snprintf(buf, sizeof(buf), "%llu", filetime);
        return buf;
    }
    t = (time_t)((filetime - EPOCH_DIFF) / 10000000ULL);
    if(localtime_r(&t, &tm_info))
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);
    else
        snprintf(buf, sizeof(buf), "%llu", filetime);
    return buf;
#endif
}

int info(const char *path)
{
    aaruformat_context  *ctx          = NULL;
    char                *strBuffer    = NULL;
    UErrorCode           u_error_code = U_ZERO_ERROR;
    uint32_t             i            = 0;
    mediaTagEntry const *mediaTag     = NULL;
    mediaTagEntry const *tmpMediaTag  = NULL;

    // Initialize ncurses and colors
    init_display();

    ctx = aaruf_open(path, false, NULL);

    if(ctx == NULL)
    {
        if(use_colors)
            printf(ANSI_RED "\n❌ Error: Cannot open AaruFormat image (error code: %d)\n\n" ANSI_RESET, errno);
        else
            printf("\n❌ Error: Cannot open AaruFormat image (error code: %d)\n\n", errno);
        cleanup_display();
        return errno;
    }

    printf("\n");

    if(use_colors)
    {
        printf(ANSI_GREEN ANSI_BOLD);
        printf("================================================================================\n");
        printf("                        AARUFORMAT IMAGE INFORMATION\n");
        printf("================================================================================\n\n");
        printf(ANSI_RESET);
    }
    else
    {
        printf("================================================================================\n");
        printf("                        AARUFORMAT IMAGE INFORMATION\n");
        printf("================================================================================\n\n");
    }

    // Image Format Section
    draw_box_top("IMAGE FORMAT", COLOR_BOX);

    char magic_str[9];
    snprintf(magic_str, sizeof(magic_str), "%8.8s", (char *)&ctx->magic);
    print_field("Magic:", magic_str, 20);

    char version_str[32];
    snprintf(version_str, sizeof(version_str), "%d.%d", ctx->header.imageMajorVersion, ctx->header.imageMinorVersion);
    print_field("Format Version:", version_str, 20);

    snprintf(version_str, sizeof(version_str), "%d.%d", ctx->library_major_version, ctx->library_minor_version);
    print_field("Library Version:", version_str, 20);

    char identifier_str[9];
    snprintf(identifier_str, sizeof(identifier_str), "%8.8s", (char *)&ctx->header.identifier);
    print_field("Identifier:", identifier_str, 20);

    draw_box_bottom(COLOR_BOX);

    // Creator Application Section
    draw_box_top("CREATOR APPLICATION", COLOR_HEADER);

    print_field("Application:", (char *)ctx->header.application, 20);

    char app_version[32];
    snprintf(app_version, sizeof(app_version), "%d.%d", ctx->header.applicationMajorVersion,
             ctx->header.applicationMinorVersion);
    print_field("Version:", app_version, 20);

    print_field("Created:", format_filetime(ctx->header.creationTime), 20);
    print_field("Last Modified:", format_filetime(ctx->header.lastWrittenTime), 20);

    draw_box_bottom(COLOR_HEADER);

    // Media Information Section
    draw_box_top("MEDIA INFORMATION", COLOR_HEADER);

    print_field("Media Type:", media_type_to_string(ctx->header.mediaType), 20);

    uint32_t cylinders       = 0;
    uint32_t heads           = 0;
    uint32_t sectorsPerTrack = 0;
    if(aaruf_get_geometry(ctx, &cylinders, &heads, &sectorsPerTrack) == AARUF_STATUS_OK)
    {
        char geometry[64];
        snprintf(geometry, sizeof(geometry), "C:%u / H:%u / S:%u", cylinders, heads, sectorsPerTrack);
        print_field("Geometry:", geometry, 20);
    }

    char image_size[64];
    snprintf(image_size, sizeof(image_size), "%llu bytes", ctx->image_info.ImageSize);
    print_field("Image Size:", image_size, 20);

    char sectors[32];
    snprintf(sectors, sizeof(sectors), "%llu", ctx->image_info.Sectors);
    print_field("Total Sectors:", sectors, 20);

    char sector_size[32];
    snprintf(sector_size, sizeof(sector_size), "%d bytes", ctx->image_info.SectorSize);
    print_field("Sector Size:", sector_size, 20);

    print_field("Has Partitions:", ctx->image_info.HasPartitions ? "Yes" : "No", 20);
    print_field("Has Sessions:", ctx->image_info.HasSessions ? "Yes" : "No", 20);

    draw_box_bottom(COLOR_HEADER);

    // DDT2 UserData Header Section (if using DDT v2)
    if(ctx->ddt_version == 2 && ctx->user_data_ddt_header.identifier == DeDuplicationTable2)
    {
        draw_box_top("DDT2 USER DATA HEADER", COLOR_HEADER);

        char ddt_type_str[32];
        switch(ctx->user_data_ddt_header.type)
        {
            case kDataTypeUserData:
                snprintf(ddt_type_str, sizeof(ddt_type_str), "UserData");
                break;
            default:
                snprintf(ddt_type_str, sizeof(ddt_type_str), "%d", ctx->user_data_ddt_header.type);
                break;
        }
        print_field("Data Type:", ddt_type_str, 25);

        char compression_str[32];
        switch(ctx->user_data_ddt_header.compression)
        {
            case kCompressionNone:
                snprintf(compression_str, sizeof(compression_str), "None");
                break;
            case kCompressionLzma:
                snprintf(compression_str, sizeof(compression_str), "LZMA");
                break;
            case kCompressionFlac:
                snprintf(compression_str, sizeof(compression_str), "FLAC");
                break;
            case kCompressionLzmaCst:
                snprintf(compression_str, sizeof(compression_str), "LZMA+CST");
                break;
            default:
                snprintf(compression_str, sizeof(compression_str), "%d", ctx->user_data_ddt_header.compression);
                break;
        }
        print_field("Compression:", compression_str, 25);

        char levels_str[32];
        snprintf(levels_str, sizeof(levels_str), "%u (current level: %u)", ctx->user_data_ddt_header.levels,
                 ctx->user_data_ddt_header.tableLevel);
        print_field("Hierarchy Levels:", levels_str, 25);

        if(ctx->user_data_ddt_header.previousLevelOffset > 0)
        {
            char prev_offset_str[32];
            snprintf(prev_offset_str, sizeof(prev_offset_str), "0x%llX",
                     (unsigned long long)ctx->user_data_ddt_header.previousLevelOffset);
            print_field("Previous Level Offset:", prev_offset_str, 25);
        }

        char blocks_str[64];
        snprintf(blocks_str, sizeof(blocks_str), "%llu", (unsigned long long)ctx->user_data_ddt_header.blocks);
        print_field("Total Blocks:", blocks_str, 25);

        char negative_str[32];
        snprintf(negative_str, sizeof(negative_str), "%u", ctx->user_data_ddt_header.negative);
        print_field("Negative Sectors:", negative_str, 25);

        char overflow_str[32];
        snprintf(overflow_str, sizeof(overflow_str), "%u", ctx->user_data_ddt_header.overflow);
        print_field("Overflow Sectors:", overflow_str, 25);

        char     user_sectors_str[64];
        uint64_t user_sectors =
            ctx->user_data_ddt_header.blocks - ctx->user_data_ddt_header.negative - ctx->user_data_ddt_header.overflow;
        snprintf(user_sectors_str, sizeof(user_sectors_str), "%llu", (unsigned long long)user_sectors);
        print_field("User Sectors:", user_sectors_str, 25);

        if(ctx->user_data_ddt_header.start > 0)
        {
            char start_str[32];
            snprintf(start_str, sizeof(start_str), "%llu", (unsigned long long)ctx->user_data_ddt_header.start);
            print_field("Start Index:", start_str, 25);
        }

        char block_align_str[32];
        snprintf(block_align_str, sizeof(block_align_str), "%u (2^%u = %u bytes)",
                 ctx->user_data_ddt_header.blockAlignmentShift, ctx->user_data_ddt_header.blockAlignmentShift,
                 1 << ctx->user_data_ddt_header.blockAlignmentShift);
        print_field("Block Alignment Shift:", block_align_str, 25);

        char data_shift_str[32];
        snprintf(data_shift_str, sizeof(data_shift_str), "%u (2^%u = %u sectors/block)",
                 ctx->user_data_ddt_header.dataShift, ctx->user_data_ddt_header.dataShift,
                 1 << ctx->user_data_ddt_header.dataShift);
        print_field("Data Shift:", data_shift_str, 25);

        char table_shift_str[64];
        if(ctx->user_data_ddt_header.tableShift > 0)
            snprintf(table_shift_str, sizeof(table_shift_str), "%u (2^%u = %u sectors/entry)",
                     ctx->user_data_ddt_header.tableShift, ctx->user_data_ddt_header.tableShift,
                     1 << ctx->user_data_ddt_header.tableShift);
        else
            snprintf(table_shift_str, sizeof(table_shift_str), "0 (single-level)");
        print_field("Table Shift:", table_shift_str, 25);

        char entries_str[32];
        snprintf(entries_str, sizeof(entries_str), "%llu", (unsigned long long)ctx->user_data_ddt_header.entries);
        print_field("Entries:", entries_str, 25);

        char length_str[64];
        snprintf(length_str, sizeof(length_str), "%llu bytes (compressed: %llu bytes)",
                 (unsigned long long)ctx->user_data_ddt_header.length,
                 (unsigned long long)ctx->user_data_ddt_header.cmpLength);
        print_field("Table Size:", length_str, 25);

        char crc_str[64];
        snprintf(crc_str, sizeof(crc_str), "0x%016llX", (unsigned long long)ctx->user_data_ddt_header.crc64);
        print_field("CRC64 (uncompressed):", crc_str, 25);

        snprintf(crc_str, sizeof(crc_str), "0x%016llX", (unsigned long long)ctx->user_data_ddt_header.cmpCrc64);
        print_field("CRC64 (compressed):", crc_str, 25);

        draw_box_bottom(COLOR_HEADER);
    }

    // Metadata Section
    int32_t sequence     = 0;
    int32_t lastSequence = 0;
    bool    hasMetadata  = false;
    int32_t length       = 0;

    // Check if we have any metadata
    if((aaruf_get_media_sequence(ctx, &sequence, &lastSequence) == AARUF_STATUS_OK && sequence > 0) ||
       aaruf_get_creator(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_comments(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_media_title(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_media_manufacturer(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_media_model(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_media_serial_number(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_media_barcode(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_media_part_number(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_drive_manufacturer(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_drive_model(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_drive_serial_number(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL ||
       aaruf_get_drive_firmware_revision(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL)
        hasMetadata = true;

    if(hasMetadata)
    {
        draw_box_top("METADATA", COLOR_HEADER);

        if(aaruf_get_media_sequence(ctx, &sequence, &lastSequence) == AARUF_STATUS_OK && sequence > 0)
        {
            char seq_str[32];
            snprintf(seq_str, sizeof(seq_str), "%d of %d", sequence, lastSequence);
            print_field("Media Sequence:", seq_str, 20);
        }

        length = 0;
        if(aaruf_get_creator(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
        {
            uint8_t *utf16Buffer = malloc(length);
            if(utf16Buffer != NULL)
            {
                if(aaruf_get_creator(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
                {
                    strBuffer = malloc(length + 1);
                    if(strBuffer != NULL)
                    {
                        memset(strBuffer, 0, length + 1);
                        u_error_code = U_ZERO_ERROR;
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) print_field("Creator:", strBuffer, 20);
                        free(strBuffer);
                    }
                }
                free(utf16Buffer);
            }
        }

        length = 0;
        if(aaruf_get_comments(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
        {
            uint8_t *utf16Buffer = malloc(length);
            if(utf16Buffer != NULL)
            {
                if(aaruf_get_comments(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
                {
                    strBuffer = malloc(length + 1);
                    if(strBuffer != NULL)
                    {
                        memset(strBuffer, 0, length + 1);
                        u_error_code = U_ZERO_ERROR;
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) print_field("Comments:", strBuffer, 20);
                        free(strBuffer);
                    }
                }
                free(utf16Buffer);
            }
        }

        length = 0;
        if(aaruf_get_media_title(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
        {
            uint8_t *utf16Buffer = malloc(length);
            if(utf16Buffer != NULL)
            {
                if(aaruf_get_media_title(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
                {
                    strBuffer = malloc(length + 1);
                    if(strBuffer != NULL)
                    {
                        memset(strBuffer, 0, length + 1);
                        u_error_code = U_ZERO_ERROR;
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) print_field("Media Title:", strBuffer, 20);
                        free(strBuffer);
                    }
                }
                free(utf16Buffer);
            }
        }

        length = 0;
        if(aaruf_get_media_manufacturer(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
        {
            uint8_t *utf16Buffer = malloc(length);
            if(utf16Buffer != NULL)
            {
                if(aaruf_get_media_manufacturer(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
                {
                    strBuffer = malloc(length + 1);
                    if(strBuffer != NULL)
                    {
                        memset(strBuffer, 0, length + 1);
                        u_error_code = U_ZERO_ERROR;
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) print_field("Media Manufacturer:", strBuffer, 20);
                        free(strBuffer);
                    }
                }
                free(utf16Buffer);
            }
        }

        length = 0;
        if(aaruf_get_media_model(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
        {
            uint8_t *utf16Buffer = malloc(length);
            if(utf16Buffer != NULL)
            {
                if(aaruf_get_media_model(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
                {
                    strBuffer = malloc(length + 1);
                    if(strBuffer != NULL)
                    {
                        memset(strBuffer, 0, length + 1);
                        u_error_code = U_ZERO_ERROR;
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) print_field("Media Model:", strBuffer, 20);
                        free(strBuffer);
                    }
                }
                free(utf16Buffer);
            }
        }

        length = 0;
        if(aaruf_get_media_serial_number(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
        {
            uint8_t *utf16Buffer = malloc(length);
            if(utf16Buffer != NULL)
            {
                if(aaruf_get_media_serial_number(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
                {
                    strBuffer = malloc(length + 1);
                    if(strBuffer != NULL)
                    {
                        memset(strBuffer, 0, length + 1);
                        u_error_code = U_ZERO_ERROR;
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) print_field("Media Serial:", strBuffer, 20);
                        free(strBuffer);
                    }
                }
                free(utf16Buffer);
            }
        }

        length = 0;
        if(aaruf_get_media_barcode(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
        {
            uint8_t *utf16Buffer = malloc(length);
            if(utf16Buffer != NULL)
            {
                if(aaruf_get_media_barcode(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
                {
                    strBuffer = malloc(length + 1);
                    if(strBuffer != NULL)
                    {
                        memset(strBuffer, 0, length + 1);
                        u_error_code = U_ZERO_ERROR;
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) print_field("Media Barcode:", strBuffer, 20);
                        free(strBuffer);
                    }
                }
                free(utf16Buffer);
            }
        }

        length = 0;
        if(aaruf_get_media_part_number(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
        {
            uint8_t *utf16Buffer = malloc(length);
            if(utf16Buffer != NULL)
            {
                if(aaruf_get_media_part_number(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
                {
                    strBuffer = malloc(length + 1);
                    if(strBuffer != NULL)
                    {
                        memset(strBuffer, 0, length + 1);
                        u_error_code = U_ZERO_ERROR;
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) print_field("Media Part Number:", strBuffer, 20);
                        free(strBuffer);
                    }
                }
                free(utf16Buffer);
            }
        }

        length = 0;
        if(aaruf_get_drive_manufacturer(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
        {
            uint8_t *utf16Buffer = malloc(length);
            if(utf16Buffer != NULL)
            {
                if(aaruf_get_drive_manufacturer(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
                {
                    strBuffer = malloc(length + 1);
                    if(strBuffer != NULL)
                    {
                        memset(strBuffer, 0, length + 1);
                        u_error_code = U_ZERO_ERROR;
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) print_field("Drive Manufacturer:", strBuffer, 20);
                        free(strBuffer);
                    }
                }
                free(utf16Buffer);
            }
        }

        length = 0;
        if(aaruf_get_drive_model(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
        {
            uint8_t *utf16Buffer = malloc(length);
            if(utf16Buffer != NULL)
            {
                if(aaruf_get_drive_model(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
                {
                    strBuffer = malloc(length + 1);
                    if(strBuffer != NULL)
                    {
                        memset(strBuffer, 0, length + 1);
                        u_error_code = U_ZERO_ERROR;
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) print_field("Drive Model:", strBuffer, 20);
                        free(strBuffer);
                    }
                }
                free(utf16Buffer);
            }
        }

        length = 0;
        if(aaruf_get_drive_serial_number(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
        {
            uint8_t *utf16Buffer = malloc(length);
            if(utf16Buffer != NULL)
            {
                if(aaruf_get_drive_serial_number(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
                {
                    strBuffer = malloc(length + 1);
                    if(strBuffer != NULL)
                    {
                        memset(strBuffer, 0, length + 1);
                        u_error_code = U_ZERO_ERROR;
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) print_field("Drive Serial:", strBuffer, 20);
                        free(strBuffer);
                    }
                }
                free(utf16Buffer);
            }
        }

        length = 0;
        if(aaruf_get_drive_firmware_revision(ctx, NULL, &length) == AARUF_ERROR_BUFFER_TOO_SMALL && length > 0)
        {
            uint8_t *utf16Buffer = malloc(length);
            if(utf16Buffer != NULL)
            {
                if(aaruf_get_drive_firmware_revision(ctx, utf16Buffer, &length) == AARUF_STATUS_OK)
                {
                    strBuffer = malloc(length + 1);
                    if(strBuffer != NULL)
                    {
                        memset(strBuffer, 0, length + 1);
                        u_error_code = U_ZERO_ERROR;
                        ucnv_convert(NULL, "UTF-16LE", strBuffer, length, (const char *)utf16Buffer, length,
                                     &u_error_code);
                        if(u_error_code == U_ZERO_ERROR) print_field("Drive Firmware:", strBuffer, 20);
                        free(strBuffer);
                    }
                }
                free(utf16Buffer);
            }
        }

        draw_box_bottom(COLOR_HEADER);
    }

    // Tracks Section
    if(ctx->tracks_header.identifier == TracksBlock && ctx->tracks_header.entries > 0)
    {
        char title[64];
        snprintf(title, sizeof(title), "TRACKS (%u total)", ctx->tracks_header.entries);
        draw_box_top(title, COLOR_HEADER);

        for(i = 0; i < ctx->tracks_header.entries && i < 20; i++)  // Limit to first 20 tracks
        {
            char track_info[128];
            snprintf(track_info, sizeof(track_info), "Track #%-2d: Seq=%d Type=%d Start=%lld End=%lld Session=%d",
                     i + 1, ctx->track_entries[i].sequence, ctx->track_entries[i].type, ctx->track_entries[i].start,
                     ctx->track_entries[i].end, ctx->track_entries[i].session);
            print_field("", track_info, 0);
        }
        if(ctx->tracks_header.entries > 20)
        {
            char more_info[64];
            snprintf(more_info, sizeof(more_info), "... and %u more track(s)", ctx->tracks_header.entries - 20);
            print_field("", more_info, 0);
        }
        draw_box_bottom(COLOR_HEADER);
    }

    // Dump Hardware Section
    if(ctx->dump_hardware_header.identifier == DumpHardwareBlock && ctx->dump_hardware_header.entries > 0)
    {
        char title[64];
        snprintf(title, sizeof(title), "DUMP HARDWARE (%u device(s))", ctx->dump_hardware_header.entries);
        draw_box_top(title, COLOR_HEADER);

        for(i = 0; i < ctx->dump_hardware_header.entries; i++)
        {
            char device_label[32];
            snprintf(device_label, sizeof(device_label), "Device #%d:", i + 1);
            print_field(device_label, "", 20);

            if(ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength > 0)
            {
                strBuffer = malloc(ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength + 1);
                memset(strBuffer, 0, ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength + 1);
                ucnv_convert(NULL, "UTF-8", strBuffer,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength,
                             (char *)ctx->dump_hardware_entries_with_data[i].manufacturer,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.manufacturerLength, &u_error_code);
                print_field("  Manufacturer:", strBuffer, 20);
                free(strBuffer);
            }

            if(ctx->dump_hardware_entries_with_data[i].entry.modelLength > 0)
            {
                strBuffer = malloc(ctx->dump_hardware_entries_with_data[i].entry.modelLength + 1);
                memset(strBuffer, 0, ctx->dump_hardware_entries_with_data[i].entry.modelLength + 1);
                ucnv_convert(NULL, "UTF-8", strBuffer, (int)ctx->dump_hardware_entries_with_data[i].entry.modelLength,
                             (char *)ctx->dump_hardware_entries_with_data[i].model,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.modelLength, &u_error_code);
                print_field("  Model:", strBuffer, 20);
                free(strBuffer);
            }

            if(ctx->dump_hardware_entries_with_data[i].entry.softwareNameLength > 0)
            {
                strBuffer = malloc(ctx->dump_hardware_entries_with_data[i].entry.softwareNameLength + 1);
                memset(strBuffer, 0, ctx->dump_hardware_entries_with_data[i].entry.softwareNameLength + 1);
                ucnv_convert(NULL, "UTF-8", strBuffer,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.softwareNameLength,
                             (char *)ctx->dump_hardware_entries_with_data[i].softwareName,
                             (int)ctx->dump_hardware_entries_with_data[i].entry.softwareNameLength, &u_error_code);
                print_field("  Software:", strBuffer, 20);
                free(strBuffer);
            }

            if(ctx->dump_hardware_entries_with_data[i].entry.extents > 0 &&
               ctx->dump_hardware_entries_with_data[i].entry.extents <= 3)
                for(uint32_t j = 0; j < ctx->dump_hardware_entries_with_data[i].entry.extents; j++)
                {
                    char extent_str[80];
                    snprintf(extent_str, sizeof(extent_str), "%llu - %llu",
                             ctx->dump_hardware_entries_with_data[i].extents[j].start,
                             ctx->dump_hardware_entries_with_data[i].extents[j].end);
                    print_field("  Extent:", extent_str, 20);
                }
            else if(ctx->dump_hardware_entries_with_data[i].entry.extents > 3)
            {
                char extent_str[80];
                snprintf(extent_str, sizeof(extent_str), "%u extent(s)",
                         ctx->dump_hardware_entries_with_data[i].entry.extents);
                print_field("  Extents:", extent_str, 20);
            }
        }
        draw_box_bottom(COLOR_HEADER);
    }

    if(ctx->flux_data_header.entries > 0)
    {
        printf("Image contains %d flux captures.\n", ctx->flux_data_header.entries);

        // Get flux capture metadata
        size_t  flux_captures_length = 0;
        int32_t res                  = aaruf_get_flux_captures(ctx, NULL, &flux_captures_length);
        if(res == AARUF_ERROR_BUFFER_TOO_SMALL)
        {
            uint8_t *flux_captures = malloc(flux_captures_length);
            if(flux_captures != NULL)
            {
                res = aaruf_get_flux_captures(ctx, flux_captures, &flux_captures_length);
                if(res == AARUF_STATUS_OK)
                {
                    size_t                 capture_count = flux_captures_length / sizeof(FluxCaptureMeta);
                    const FluxCaptureMeta *captures      = (const FluxCaptureMeta *)flux_captures;

                    printf("Flux capture details:\n");

                    // Calculate statistics
                    uint64_t min_index_res = UINT64_MAX;
                    uint64_t max_index_res = 0;
                    uint64_t min_data_res  = UINT64_MAX;
                    uint64_t max_data_res  = 0;

                    // Track unique heads and tracks
                    uint16_t *seen_tracks = calloc(capture_count, sizeof(uint16_t));
                    uint32_t *seen_heads  = calloc(capture_count, sizeof(uint32_t));
                    uint32_t  track_count = 0;
                    uint32_t  head_count  = 0;

                    for(size_t i = 0; i < capture_count; i++)
                    {
                        // Update resolution statistics
                        if(captures[i].indexResolution < min_index_res) min_index_res = captures[i].indexResolution;
                        if(captures[i].indexResolution > max_index_res) max_index_res = captures[i].indexResolution;
                        if(captures[i].dataResolution < min_data_res) min_data_res = captures[i].dataResolution;
                        if(captures[i].dataResolution > max_data_res) max_data_res = captures[i].dataResolution;

                        // Track unique tracks
                        bool track_seen = false;
                        for(uint32_t j = 0; j < track_count; j++)
                            if(seen_tracks[j] == captures[i].track)
                            {
                                track_seen = true;
                                break;
                            }
                        if(!track_seen) seen_tracks[track_count++] = captures[i].track;

                        // Track unique heads
                        bool head_seen = false;
                        for(uint32_t j = 0; j < head_count; j++)
                            if(seen_heads[j] == captures[i].head)
                            {
                                head_seen = true;
                                break;
                            }
                        if(!head_seen) seen_heads[head_count++] = captures[i].head;
                    }

                    free(seen_tracks);
                    free(seen_heads);

                    // Display statistics
                    printf("\tStatistics:\n");
                    printf("\t\tUnique heads: %u\n", head_count);
                    printf("\t\tUnique tracks: %u\n", track_count);
                    if(min_index_res != UINT64_MAX)
                        printf("\t\tIndex resolution: %llu - %llu picoseconds (%.3f - %.3f nanoseconds)\n",
                               (unsigned long long)min_index_res, (unsigned long long)max_index_res,
                               min_index_res / 1000.0, max_index_res / 1000.0);
                    if(min_data_res != UINT64_MAX)
                        printf("\t\tData resolution: %llu - %llu picoseconds (%.3f - %.3f nanoseconds)\n",
                               (unsigned long long)min_data_res, (unsigned long long)max_data_res,
                               min_data_res / 1000.0, max_data_res / 1000.0);

                    // Display individual captures
                    printf("\tCaptures:\n");
                    for(size_t i = 0; i < capture_count; i++)
                    {
                        printf("\t\tCapture %zu:\n", i + 1);
                        printf("\t\t\tHead: %u\n", captures[i].head);
                        printf("\t\t\tTrack: %u\n", captures[i].track);
                        printf("\t\t\tSubtrack: %u\n", captures[i].subtrack);
                        printf("\t\t\tCapture index: %u\n", captures[i].captureIndex);
                        printf("\t\t\tIndex resolution: %llu picoseconds (%.3f nanoseconds)\n",
                               (unsigned long long)captures[i].indexResolution, captures[i].indexResolution / 1000.0);
                        printf("\t\t\tData resolution: %llu picoseconds (%.3f nanoseconds)\n",
                               (unsigned long long)captures[i].dataResolution, captures[i].dataResolution / 1000.0);
                    }
                }
                free(flux_captures);
            }
        }
    }

    // Checksums Section
    bool hasChecksums =
        ctx->checksums.hasMd5 || ctx->checksums.hasSha1 || ctx->checksums.hasSha256 || ctx->checksums.hasSpamSum;

    if(hasChecksums)
    {
        draw_box_top("CHECKSUMS", COLOR_HEADER);

        if(ctx->checksums.hasMd5)
        {
            strBuffer = byte_array_to_hex_string(ctx->checksums.md5, MD5_DIGEST_LENGTH);
            print_field("MD5:", strBuffer, 20);
            free(strBuffer);
        }

        if(ctx->checksums.hasSha1)
        {
            strBuffer = byte_array_to_hex_string(ctx->checksums.sha1, SHA1_DIGEST_LENGTH);
            print_field("SHA-1:", strBuffer, 20);
            free(strBuffer);
        }

        if(ctx->checksums.hasSha256)
        {
            strBuffer = byte_array_to_hex_string(ctx->checksums.sha256, SHA256_DIGEST_LENGTH);
            print_field("SHA-256:", strBuffer, 20);
            free(strBuffer);
        }

        if(ctx->checksums.hasSpamSum) print_field("SpamSum:", (char *)ctx->checksums.spamsum, 20);

        draw_box_bottom(COLOR_HEADER);
    }

    // Media Tags Section
    if(ctx->mediaTags != NULL)
    {
        uint32_t tag_count = HASH_COUNT(ctx->mediaTags);
        char     title[64];
        snprintf(title, sizeof(title), "MEDIA TAGS (%u total)", tag_count);
        draw_box_top(title, COLOR_HEADER);

        uint32_t displayed = 0;
        HASH_ITER(hh, ctx->mediaTags, mediaTag, tmpMediaTag)
        if(displayed < 20)  // Limit display to first 20 tags
        {
            char tag_info[128];
            snprintf(tag_info, sizeof(tag_info), "%s (%d bytes)", media_tag_type_to_string((uint16_t)mediaTag->type),
                     mediaTag->length);
            print_field("", tag_info, 0);
            displayed++;
        }

        if(tag_count > 20)
        {
            char more_info[64];
            snprintf(more_info, sizeof(more_info), "... and %u more tag(s)", tag_count - 20);
            print_field("", more_info, 0);
        }

        draw_box_bottom(COLOR_HEADER);
    }

    // Readable Sector Tags Section
    if(ctx->readableSectorTags != NULL)
    {
        draw_box_top("READABLE SECTOR TAGS", COLOR_HEADER);

        uint32_t readable_count = 0;
        // Count readable tags
        for(int i = 0; i <= MaxSectorTag; i++)
            if(ctx->readableSectorTags[i]) readable_count++;

        if(readable_count > 0)
        {
            uint32_t displayed = 0;
            for(int i = 0; i <= MaxSectorTag; i++)
                if(ctx->readableSectorTags[i])
                {
                    print_field("", sector_tag_type_to_string(i), 0);
                    displayed++;
                    if(displayed >= 20)
                    {
                        if(readable_count > 20)
                        {
                            char more_info[64];
                            snprintf(more_info, sizeof(more_info), "... and %u more tag(s)", readable_count - 20);
                            print_field("", more_info, 0);
                        }
                        break;
                    }
                }
        }
        else
            print_field("", "(No readable sector tags)", 0);

        draw_box_bottom(COLOR_HEADER);
    }

    printf("================================================================================\n\n");

    aaruf_close(ctx);

    // Cleanup ncurses
    cleanup_display();

    return 0;
}
